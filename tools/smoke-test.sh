#!/usr/bin/env bash
#
# smoke-test.sh - headless Jamulus client/server connectivity smoke test.
#
# Spike for jamulussoftware/jamulus#2428 ("Automatic Client/Server Smoke Test
# in CI"). Starts a headless Jamulus server and a headless Jamulus client
# (backed by JACK's "dummy" audio driver, since no real sound hardware is
# needed or available in CI), waits for the client to actually establish a
# protocol-level connection to the server (verified via each side's
# JSON-RPC interface), and reports pass/fail with a hard timeout. Always
# tears down every process it started, even on failure.
#
# Usage:
#   tools/smoke-test.sh [path-to-jamulus-binary]
#
# The binary defaults to $JAMULUS_BIN, then to ./Jamulus in the current
# directory. Build it first, e.g.:
#   qmake6 Jamulus.pro CONFIG+=headless
#   make -j"$(nproc)"
#
# Exit status: 0 if the connection was verified before the timeout,
# non-zero otherwise (timeout, crash, or missing prerequisites).

set -u
set -o pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

JAMULUS_BIN="${1:-${JAMULUS_BIN:-./Jamulus}}"
SERVER_PORT=22124          # Jamulus DEFAULT_PORT_NUMBER (src/global.h)
SERVER_RPC_PORT=22222
CLIENT_RPC_PORT=22223
POLL_INTERVAL=0.5          # seconds between connection-status polls
TIMEOUT_SECS="${SMOKE_TEST_TIMEOUT:-20}"   # hard timeout for the whole test

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKDIR="$(mktemp -d /tmp/jamulus-smoke-test.XXXXXX)"

SERVER_SECRET="$WORKDIR/server_secret.txt"
CLIENT_SECRET="$WORKDIR/client_secret.txt"
SERVER_LOG="$WORKDIR/server.log"
SERVER_STDOUT="$WORKDIR/server_stdout.log"
CLIENT_STDOUT="$WORKDIR/client_stdout.log"
JACKD_STDOUT="$WORKDIR/jackd.log"

JACKD_PID=""
SERVER_PID=""
CLIENT_PID=""

# ---------------------------------------------------------------------------
# Cleanup: always kill everything we started, never leak daemons.
# ---------------------------------------------------------------------------

cleanup() {
    local exit_code=$?
    for pid in "$CLIENT_PID" "$SERVER_PID" "$JACKD_PID"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
        fi
    done
    # Give them a moment, then force-kill anything still alive.
    sleep 0.3
    for pid in "$CLIENT_PID" "$SERVER_PID" "$JACKD_PID"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null
        fi
    done
    if [ "${SMOKE_TEST_KEEP_LOGS:-0}" != "1" ]; then
        rm -rf "$WORKDIR"
    else
        echo "Logs kept in: $WORKDIR" >&2
    fi
    exit "$exit_code"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

[ -x "$JAMULUS_BIN" ] || fail "Jamulus binary not found/executable at: $JAMULUS_BIN"
command -v jackd >/dev/null 2>&1 || fail "jackd not found (apt install jackd2)"
command -v python3 >/dev/null 2>&1 || fail "python3 not found (used for JSON-RPC checks)"
command -v openssl >/dev/null 2>&1 || fail "openssl not found (used to generate RPC secrets)"

JAMULUS_BIN="$(cd "$(dirname "$JAMULUS_BIN")" && pwd)/$(basename "$JAMULUS_BIN")"

START_TIME=$(date +%s.%N)

# ---------------------------------------------------------------------------
# 1. Fake sound backend: JACK "dummy" driver (no real sound hardware needed).
# ---------------------------------------------------------------------------

jackd -d dummy -r 48000 -p 512 >"$JACKD_STDOUT" 2>&1 &
JACKD_PID=$!

# Wait for the JACK server to be ready by polling jack_lsp.
jack_ready=0
for _ in $(seq 1 20); do
    if jack_lsp >/dev/null 2>&1; then
        jack_ready=1
        break
    fi
    sleep 0.25
done
[ "$jack_ready" = "1" ] || fail "jackd (dummy driver) did not become ready; see $JACKD_STDOUT"

# ---------------------------------------------------------------------------
# 2. Headless server
# ---------------------------------------------------------------------------

openssl rand -base64 16 >"$SERVER_SECRET"
openssl rand -base64 16 >"$CLIENT_SECRET"

"$JAMULUS_BIN" -s -n \
    -l "$SERVER_LOG" \
    --jsonrpcport "$SERVER_RPC_PORT" \
    --jsonrpcsecretfile "$SERVER_SECRET" \
    --serverinfo "SmokeTestServer;;" \
    >"$SERVER_STDOUT" 2>&1 &
SERVER_PID=$!

# ---------------------------------------------------------------------------
# 3. Headless client, connecting to the server over loopback
# ---------------------------------------------------------------------------

"$JAMULUS_BIN" -n \
    -c "127.0.0.1:$SERVER_PORT" \
    --jsonrpcport "$CLIENT_RPC_PORT" \
    --jsonrpcsecretfile "$CLIENT_SECRET" \
    --clientname "SmokeTestClient" \
    >"$CLIENT_STDOUT" 2>&1 &
CLIENT_PID=$!

# ---------------------------------------------------------------------------
# 4. Verification: poll each side's JSON-RPC interface until BOTH agree the
#    connection is up, or until the timeout expires.
#
#    - jamulusclient/getClientInfo -> result.connected == true
#    - jamulusserver/getClients    -> result.connections >= 1
#
#    This is the strongest simple signal available: it is the same
#    connection state Jamulus itself tracks and exposes over its
#    documented JSON-RPC API (docs/JSON-RPC.md), rather than us inferring
#    connectivity from log text or raw packet counts.
# ---------------------------------------------------------------------------

jrpc() {
    # jrpc <port> <secretfile> <method> <field.path> -> prints the field value
    python3 "$SCRIPT_DIR/smoke-test-jrpc.py" "$1" "$2" "$3" "$4" 2>/dev/null
}

verified=0
server_connections=0
deadline=$(( $(date +%s) + TIMEOUT_SECS ))
while [ "$(date +%s)" -lt "$deadline" ]; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        fail "server process exited unexpectedly; see $SERVER_STDOUT"
    fi
    if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
        fail "client process exited unexpectedly; see $CLIENT_STDOUT"
    fi

    client_connected="$(jrpc "$CLIENT_RPC_PORT" "$CLIENT_SECRET" jamulusclient/getClientInfo connected)"
    server_connections="$(jrpc "$SERVER_RPC_PORT" "$SERVER_SECRET" jamulusserver/getClients connections)"

    if [ "$client_connected" = "True" ] && [ "${server_connections:-0}" -ge 1 ] 2>/dev/null; then
        verified=1
        break
    fi

    sleep "$POLL_INTERVAL"
done

END_TIME=$(date +%s.%N)
ELAPSED=$(python3 -c "print(f'{$END_TIME - $START_TIME:.2f}')")

if [ "$verified" = "1" ]; then
    echo "PASS: client<->server connection verified in ${ELAPSED}s (client.connected=true, server.connections=$server_connections)"
    exit 0
else
    echo "server_stdout:"; cat "$SERVER_STDOUT" >&2
    echo "client_stdout:"; cat "$CLIENT_STDOUT" >&2
    fail "connection not verified within ${TIMEOUT_SECS}s timeout (elapsed ${ELAPSED}s)"
fi
