#!/usr/bin/env python3
"""
Minimal JSON-RPC client for Jamulus's --jsonrpcport interface
(docs/JSON-RPC.md), used by tools/smoke-test.sh to poll connection state.

Usage:
    smoke-test-jrpc.py <port> <secretfile> <method> [result.field.path]

Opens a fresh TCP connection, authenticates with the secret from
<secretfile>, calls <method> with empty params, and prints either the
raw JSON result object (if no field path given) or the value at the
dotted field path (e.g. "connected", "connections").

Exits non-zero (printing nothing useful) on any connection/protocol
error, so the caller can treat a missing/blank field as "not yet
connected" without crashing the polling loop.
"""
import json
import socket
import sys


def recv_line(sock_file):
    line = b""
    while not line.endswith(b"\n"):
        chunk = sock_file.read(1)
        if not chunk:
            break
        line += chunk
    return line.decode("utf-8", "replace").strip()


def main():
    if len(sys.argv) < 4:
        print("usage: smoke-test-jrpc.py <port> <secretfile> <method> [field.path]", file=sys.stderr)
        return 2

    port = int(sys.argv[1])
    secretfile = sys.argv[2]
    method = sys.argv[3]
    field_path = sys.argv[4] if len(sys.argv) > 4 else None

    with open(secretfile) as f:
        secret = f.read().strip()

    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
    sock.settimeout(2)
    sock_file = sock.makefile("rwb", buffering=0)

    def send(obj):
        sock_file.write((json.dumps(obj) + "\n").encode())

    send({"id": 1, "jsonrpc": "2.0", "method": "jamulus/apiAuth", "params": {"secret": secret}})
    auth_line = recv_line(sock_file)
    auth_resp = json.loads(auth_line) if auth_line else {}
    if auth_resp.get("result") != "ok":
        print(f"auth failed: {auth_line}", file=sys.stderr)
        sock.close()
        return 1

    send({"id": 2, "jsonrpc": "2.0", "method": method, "params": {}})
    result_line = recv_line(sock_file)
    sock.close()

    resp = json.loads(result_line) if result_line else {}
    result = resp.get("result", {})

    if field_path is None:
        print(json.dumps(result))
        return 0

    value = result
    for key in field_path.split("."):
        if isinstance(value, dict):
            value = value.get(key)
        else:
            value = None
            break

    print(value)
    return 0


if __name__ == "__main__":
    sys.exit(main())
