#!/usr/bin/env python3
import argparse
import http.server
import os
import ssl
import subprocess
import sys
import tempfile


def create_ephemeral_cert(temp_dir: str):
    cert_path = os.path.join(temp_dir, "localhost.crt")
    key_path = os.path.join(temp_dir, "localhost.key")
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-keyout",
            key_path,
            "-out",
            cert_path,
            "-days",
            "1",
            "-nodes",
            "-subj",
            "/CN=localhost",
            "-addext",
            "subjectAltName=DNS:localhost,IP:127.0.0.1",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return cert_path, key_path


def main():
    parser = argparse.ArgumentParser(description="Test-only HTTPS server for WebAuthn fixture")
    parser.add_argument("--port", type=int, default=8443, help="HTTPS port (default: 8443)")
    parser.add_argument("--cert", help="Path to TLS certificate file")
    parser.add_argument("--key", help="Path to TLS private key file")
    args = parser.parse_args()

    serve_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(serve_dir)

    temp_dir = None
    cert_path = args.cert
    key_path = args.key

    if not cert_path or not key_path:
        temp_dir = tempfile.mkdtemp(prefix="webauthn-ssl-")
        try:
            cert_path, key_path = create_ephemeral_cert(temp_dir)
        except Exception as e:
            sys.exit(f"Failed to generate ephemeral SSL certificate: {e}")

    handler = http.server.SimpleHTTPRequestHandler
    httpd = http.server.HTTPServer(("127.0.0.1", args.port), handler)

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=cert_path, keyfile=key_path)
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

    print(f"Serving WebAuthn fixture at https://localhost:{args.port}/fixture.html")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping HTTPS server.")
    finally:
        httpd.server_close()
        if temp_dir and os.path.exists(temp_dir):
            import shutil
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
