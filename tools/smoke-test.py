#!/usr/bin/env python3
"""
smoke-test.py - headless Jamulus client/server connectivity smoke test.

Spike/CI script for jamulussoftware/jamulus#2428 ("Automatic Client/Server
Smoke Test in CI"). Starts a headless Jamulus server and a headless
Jamulus client (backed by JACK's "dummy" audio driver by default, since no
real sound hardware is needed or available in most CI runners), waits for
the client to actually establish a protocol-level connection to the
server (verified via each side's JSON-RPC interface), and reports
pass/fail with a hard timeout. Always tears down every process it
started, on every exit path -- including Ctrl-C and Windows, where
process trees are killed with taskkill rather than POSIX signals.

Pure standard library (subprocess/socket/json/argparse/tempfile) so it
runs identically on Linux, macOS and Windows with only a system Python 3
interpreter -- no pip install step required in CI.

Usage:
    python3 tools/smoke-test.py --jamulus-bin ./Jamulus
    python3 tools/smoke-test.py --jamulus-bin ./Jamulus --skip-jackd   # e.g. CoreAudio/ASIO builds

Build the binary first, e.g.:
    qmake6 Jamulus.pro CONFIG+=headless && make -j"$(nproc)"

Exit status: 0 if the connection was verified before the timeout,
non-zero otherwise (timeout, crash, or missing prerequisites). On
success, prints the single unambiguous line:
    SMOKE TEST PASSED: client and server both report connected
which CI can grep for in addition to trusting the exit code.
"""
from __future__ import annotations

import argparse
import json
import os
import secrets
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

DEFAULT_SERVER_PORT = 22124  # Jamulus DEFAULT_PORT_NUMBER (src/global.h)
DEFAULT_SERVER_RPC_PORT = 22222
DEFAULT_CLIENT_RPC_PORT = 22223
DEFAULT_TIMEOUT = 20.0
DEFAULT_POLL_INTERVAL = 0.5

IS_WINDOWS = os.name == "nt"


def log(msg: str) -> None:
    print(msg, flush=True)


def make_secret() -> str:
    # Jamulus only requires "at least 16 characters" (docs/JSON-RPC.md);
    # this is plenty and avoids depending on the `openssl` CLI being on PATH.
    return secrets.token_urlsafe(16)


class ManagedProcess:
    """A subprocess this script started, guaranteed to be torn down."""

    def __init__(self, name: str, cmd: list[str], log_path: Path):
        self.name = name
        self.cmd = cmd
        self.log_path = log_path
        self.proc: subprocess.Popen | None = None
        self._log_file = None

    def start(self) -> None:
        self._log_file = open(self.log_path, "wb")
        kwargs: dict = {}
        if IS_WINDOWS:
            kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            kwargs["start_new_session"] = True  # own process group, like setsid
        self.proc = subprocess.Popen(
            self.cmd, stdout=self._log_file, stderr=subprocess.STDOUT, **kwargs
        )

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def stop(self) -> None:
        if self.proc is None or self.proc.poll() is not None:
            if self._log_file:
                self._log_file.close()
            return
        try:
            if IS_WINDOWS:
                subprocess.run(
                    ["taskkill", "/PID", str(self.proc.pid), "/T", "/F"],
                    capture_output=True,
                )
            else:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
        except (ProcessLookupError, OSError):
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                if IS_WINDOWS:
                    subprocess.run(
                        ["taskkill", "/PID", str(self.proc.pid), "/T", "/F"],
                        capture_output=True,
                    )
                else:
                    os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except (ProcessLookupError, OSError):
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        if self._log_file:
            self._log_file.close()


# ---------------------------------------------------------------------------
# JSON-RPC client (docs/JSON-RPC.md): newline-delimited JSON over a plain
# TCP socket, authenticated per-connection with jamulus/apiAuth.
# ---------------------------------------------------------------------------


def jrpc_call(port: int, secret: str, method: str, timeout: float = 2.0):
    """Return the "result" object of `method`, or None on any failure
    (connection refused, auth failure, timeout, bad JSON) -- callers treat
    None as "not ready yet" rather than crashing the polling loop."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            sock_file = sock.makefile("rwb", buffering=0)

            def send(obj):
                sock_file.write((json.dumps(obj) + "\n").encode())

            def recv_line():
                line = b""
                while not line.endswith(b"\n"):
                    chunk = sock_file.read(1)
                    if not chunk:
                        break
                    line += chunk
                return line.decode("utf-8", "replace").strip()

            send({"id": 1, "jsonrpc": "2.0", "method": "jamulus/apiAuth", "params": {"secret": secret}})
            auth_line = recv_line()
            auth_resp = json.loads(auth_line) if auth_line else {}
            if auth_resp.get("result") != "ok":
                return None

            send({"id": 2, "jsonrpc": "2.0", "method": method, "params": {}})
            result_line = recv_line()
            resp = json.loads(result_line) if result_line else {}
            return resp.get("result")
    except (OSError, ValueError):
        return None


def wait_for_jack(jack_lsp_cmd: list[str], attempts: int, interval: float) -> bool:
    if shutil.which(jack_lsp_cmd[0]) is None:
        log(f"NOTE: {jack_lsp_cmd[0]!r} not found on PATH; skipping readiness probe "
            f"and just waiting {interval * attempts:.1f}s before continuing.")
        time.sleep(interval * attempts)
        return True
    for _ in range(attempts):
        try:
            if subprocess.run(jack_lsp_cmd, capture_output=True, timeout=2).returncode == 0:
                return True
        except (OSError, subprocess.TimeoutExpired):
            pass
        time.sleep(interval)
    return False


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jamulus-bin", required=True, help="path to the Jamulus binary to test")
    parser.add_argument("--server-port", type=int, default=DEFAULT_SERVER_PORT)
    parser.add_argument("--server-rpc-port", type=int, default=DEFAULT_SERVER_RPC_PORT)
    parser.add_argument("--client-rpc-port", type=int, default=DEFAULT_CLIENT_RPC_PORT)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="hard timeout in seconds for the whole test (default: %(default)s)")
    parser.add_argument("--poll-interval", type=float, default=DEFAULT_POLL_INTERVAL)
    parser.add_argument("--keep-logs", action="store_true", help="don't delete the temp workdir with logs on exit")
    parser.add_argument(
        "--skip-jackd",
        action="store_true",
        help="don't start jackd -- use when the binary was built against a "
        "native backend that doesn't need an external JACK daemon (e.g. "
        "CoreAudio on macOS, ASIO on Windows)",
    )
    parser.add_argument(
        "--jackd-cmd",
        default="jackd -d dummy -r 48000 -p 512",
        help="shell-style command used to start the JACK server (default: %(default)r)",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    jamulus_bin = Path(args.jamulus_bin).resolve()
    if not jamulus_bin.is_file():
        log(f"SMOKE TEST FAILED: Jamulus binary not found at: {jamulus_bin}")
        return 1

    workdir = Path(tempfile.mkdtemp(prefix="jamulus-smoke-test-"))
    server_log = workdir / "server.log"
    server_stdout = workdir / "server_stdout.log"
    client_stdout = workdir / "client_stdout.log"
    jackd_stdout = workdir / "jackd_stdout.log"

    server_secret = make_secret()
    client_secret = make_secret()

    processes: list[ManagedProcess] = []
    start_time = time.monotonic()

    try:
        # -------------------------------------------------------------
        # 1. Fake sound backend: JACK "dummy" driver, unless the caller
        #    says the client build doesn't need one.
        # -------------------------------------------------------------
        if not args.skip_jackd:
            jackd_cmd = shlex.split(args.jackd_cmd, posix=not IS_WINDOWS)
            jackd_proc = ManagedProcess("jackd", jackd_cmd, jackd_stdout)
            jackd_proc.start()
            processes.append(jackd_proc)

            jack_lsp_cmd = ["jack_lsp"]
            if not wait_for_jack(jack_lsp_cmd, attempts=20, interval=0.25):
                log(f"SMOKE TEST FAILED: jackd did not become ready; see {jackd_stdout}")
                return 1
            if not jackd_proc.alive():
                log(f"SMOKE TEST FAILED: jackd exited unexpectedly; see {jackd_stdout}")
                return 1

        # -------------------------------------------------------------
        # 2. Headless server
        # -------------------------------------------------------------
        server_cmd = [
            str(jamulus_bin), "-s", "-n",
            "-l", str(server_log),
            "--jsonrpcport", str(args.server_rpc_port),
            "--jsonrpcsecretfile", str(_write_secret_file(workdir / "server_secret.txt", server_secret)),
            "--serverinfo", "SmokeTestServer;;",
        ]
        server_proc = ManagedProcess("server", server_cmd, server_stdout)
        server_proc.start()
        processes.append(server_proc)

        # -------------------------------------------------------------
        # 3. Headless client, connecting to the server over loopback
        # -------------------------------------------------------------
        client_cmd = [
            str(jamulus_bin), "-n",
            "-c", f"127.0.0.1:{args.server_port}",
            "--jsonrpcport", str(args.client_rpc_port),
            "--jsonrpcsecretfile", str(_write_secret_file(workdir / "client_secret.txt", client_secret)),
            "--clientname", "SmokeTestClient",
        ]
        client_proc = ManagedProcess("client", client_cmd, client_stdout)
        client_proc.start()
        processes.append(client_proc)

        # -------------------------------------------------------------
        # 4. Verification: poll each side's JSON-RPC interface until BOTH
        #    agree the connection is up, or until the timeout expires.
        #
        #    - jamulusclient/getClientInfo -> result.connected == true
        #    - jamulusserver/getClients    -> result.connections >= 1
        #
        #    This is the strongest simple signal available: it is the
        #    same connection state Jamulus itself tracks and exposes over
        #    its documented JSON-RPC API (docs/JSON-RPC.md), rather than
        #    inferring connectivity from log text or raw packet counts.
        # -------------------------------------------------------------
        deadline = time.monotonic() + args.timeout
        verified = False
        server_connections = 0
        while time.monotonic() < deadline:
            if not server_proc.alive():
                log(f"SMOKE TEST FAILED: server process exited unexpectedly; see {server_stdout}")
                _dump_logs(server_stdout, client_stdout)
                return 1
            if not client_proc.alive():
                log(f"SMOKE TEST FAILED: client process exited unexpectedly; see {client_stdout}")
                _dump_logs(server_stdout, client_stdout)
                return 1

            client_info = jrpc_call(args.client_rpc_port, client_secret, "jamulusclient/getClientInfo")
            server_info = jrpc_call(args.server_rpc_port, server_secret, "jamulusserver/getClients")

            client_connected = bool(client_info and client_info.get("connected"))
            server_connections = (server_info or {}).get("connections", 0)

            if client_connected and isinstance(server_connections, (int, float)) and server_connections >= 1:
                verified = True
                break

            time.sleep(args.poll_interval)

        elapsed = time.monotonic() - start_time

        if verified:
            log(
                f"SMOKE TEST PASSED: client and server both report connected "
                f"(verified in {elapsed:.2f}s; client.connected=true, server.connections={server_connections})"
            )
            return 0
        else:
            log(f"SMOKE TEST FAILED: connection not verified within {args.timeout:.0f}s timeout (elapsed {elapsed:.2f}s)")
            _dump_logs(server_stdout, client_stdout)
            return 1
    finally:
        # Always tear down every process we started, regardless of how we
        # got here (success, failure, or an unexpected exception) -- in
        # reverse start order (client, server, jackd).
        for proc in reversed(processes):
            proc.stop()
        if args.keep_logs:
            log(f"Logs kept in: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


def _write_secret_file(path: Path, secret: str) -> Path:
    path.write_text(secret + "\n")
    return path


def _dump_logs(server_stdout: Path, client_stdout: Path) -> None:
    for label, path in (("server_stdout", server_stdout), ("client_stdout", client_stdout)):
        log(f"--- {label} ({path}) ---")
        try:
            log(path.read_text(errors="replace"))
        except OSError as exc:
            log(f"(could not read log: {exc})")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
