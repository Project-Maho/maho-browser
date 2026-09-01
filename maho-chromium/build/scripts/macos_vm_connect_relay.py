#!/usr/bin/env python3
"""Host-side HTTP CONNECT relay for Tart live-webpage E2E suites.

The Tart shared network's outbound NAT is non-functional on this host, but
guests can always reach the host itself (the vmnet gateway). Live-webpage
suites point the guest browser's --proxy-server at this relay so the guest
browser opens end-to-end TLS to real production sites through the host's
internet link. After CONNECT (or an absolute-GET rewrite) the relay is a
pure TCP pipe and never inspects or modifies content.
"""
from __future__ import annotations

import argparse
import select
import socket
import threading

IDLE_TIMEOUT = 180


def _relay(a: socket.socket, b: socket.socket) -> None:
    sockets = [a, b]
    try:
        while True:
            readable, _, _ = select.select(sockets, [], [], IDLE_TIMEOUT)
            if not readable:
                break
            for sock in readable:
                data = sock.recv(65536)
                if not data:
                    return
                (b if sock is a else a).sendall(data)
    except OSError:
        pass
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass


def _handle(client: socket.socket) -> None:
    upstream = None
    try:
        client.settimeout(60)
        request = b""
        while b"\r\n\r\n" not in request:
            chunk = client.recv(65536)
            if not chunk:
                return
            request += chunk
        head = request.split(b"\r\n", 1)[0].decode("latin1")
        method, _, target = head.partition(" ")
        target = target.split(" ")[0]
        if method == "CONNECT":
            host, _, port = target.rpartition(":")
            upstream = socket.create_connection((host, int(port)), timeout=30)
            client.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            _relay(client, upstream)
            return
        if method in ("GET", "POST") and target.startswith("http://"):
            rest = target[7:]
            hostport, _, path = rest.partition("/")
            host, _, port = hostport.partition(":")
            upstream = socket.create_connection(
                (host, int(port or 80)), timeout=30
            )
            lines = request.split(b"\r\n")
            filtered = [
                line
                for line in lines[1:]
                if line and not line.lower().startswith(b"proxy-")
            ]
            upstream.sendall(
                f"{method} /{path} HTTP/1.1\r\n".encode("latin1")
                + b"\r\n".join(filtered)
                + b"\r\n\r\n"
            )
            _relay(client, upstream)
            return
        client.sendall(b"HTTP/1.1 405 Method Not Allowed\r\n\r\n")
    except OSError:
        pass
    finally:
        for sock in (client, upstream):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass


def serve(bind: str, port: int) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((bind, port))
    server.listen(64)
    print(f"maho connect relay listening on {(bind, port)}", flush=True)
    while True:
        client, _ = server.accept()
        threading.Thread(target=_handle, args=(client,), daemon=True).start()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8899)
    args = parser.parse_args()
    serve(args.bind, args.port)


if __name__ == "__main__":
    main()
