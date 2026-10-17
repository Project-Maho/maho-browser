#!/usr/bin/env python3
"""Test-only HTTPS relying party for the Maho WebAuthn fixture.

The server, not the page, decides whether a ceremony passed: it issues
single-use challenges and verifies clientDataJSON type/challenge/origin, the
authenticator data RP ID hash and flags, the sign counter, and the ES256
signature against the key stored at registration. Pure standard library, so it
runs unchanged on the Windows and Linux QA hosts.
"""
import argparse
import base64
import hashlib
import http.server
import json
import os
import secrets
import ssl
import subprocess
import sys
import tempfile
import threading

RP_ID = "localhost"

# --- P-256 ECDSA verification (FIPS 186-4), standard library only ----------
_P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
_A = _P - 3
_N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
_G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
      0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)
_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B


def _on_curve(pt):
    x, y = pt
    return (y * y - (x * x * x + _A * x + _B)) % _P == 0


def _add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    (x1, y1), (x2, y2) = p1, p2
    if x1 == x2 and (y1 + y2) % _P == 0:
        return None
    if p1 == p2:
        lam = (3 * x1 * x1 + _A) * pow(2 * y1, -1, _P) % _P
    else:
        lam = (y2 - y1) * pow(x2 - x1, -1, _P) % _P
    x3 = (lam * lam - x1 - x2) % _P
    return (x3, (lam * (x1 - x3) - y1) % _P)


def _mul(k, pt):
    out = None
    while k:
        if k & 1:
            out = _add(out, pt)
        pt = _add(pt, pt)
        k >>= 1
    return out


def _der_to_rs(der):
    if len(der) < 8 or der[0] != 0x30:
        raise ValueError("signature is not a DER sequence")
    i = 2
    vals = []
    for _ in range(2):
        if der[i] != 0x02:
            raise ValueError("signature integer missing")
        ln = der[i + 1]
        vals.append(int.from_bytes(der[i + 2:i + 2 + ln], "big"))
        i += 2 + ln
    return vals


def es256_verify(pub, message, der_sig):
    r, s = _der_to_rs(der_sig)
    if not (1 <= r < _N and 1 <= s < _N) or not _on_curve(pub):
        return False
    e = int.from_bytes(hashlib.sha256(message).digest(), "big")
    w = pow(s, -1, _N)
    pt = _add(_mul(e * w % _N, _G), _mul(r * w % _N, pub))
    return pt is not None and pt[0] % _N == r


# --- minimal CBOR decoder (maps, arrays, ints, byte/text strings) ----------
def cbor_decode(data, i=0):
    ib = data[i]
    major, info = ib >> 5, ib & 0x1F
    i += 1
    if info < 24:
        val = info
    elif info in (24, 25, 26, 27):
        size = 1 << (info - 24)
        val = int.from_bytes(data[i:i + size], "big")
        i += size
    else:
        raise ValueError("unsupported CBOR length")
    if major == 0:
        return val, i
    if major == 1:
        return -1 - val, i
    if major in (2, 3):
        raw = data[i:i + val]
        return (raw if major == 2 else raw.decode()), i + val
    if major == 4:
        out = []
        for _ in range(val):
            item, i = cbor_decode(data, i)
            out.append(item)
        return out, i
    if major == 5:
        out = {}
        for _ in range(val):
            k, i = cbor_decode(data, i)
            v, i = cbor_decode(data, i)
            out[k] = v
        return out, i
    raise ValueError("unsupported CBOR major type")


def b64u(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


class RelyingParty:
    def __init__(self, origin):
        self.origin = origin
        self.lock = threading.Lock()
        self.challenges = set()
        self.credentials = {}  # credential id (b64url) -> {"key": (x, y), "count": int}

    def issue_challenge(self):
        c = secrets.token_bytes(32)
        with self.lock:
            self.challenges.add(c)
        return base64.urlsafe_b64encode(c).rstrip(b"=").decode()

    def _client_data(self, raw, expected_type):
        cd = json.loads(raw)
        if cd.get("type") != expected_type:
            raise ValueError(f"clientData type {cd.get('type')!r} != {expected_type!r}")
        if cd.get("origin") != self.origin:
            raise ValueError(f"origin {cd.get('origin')!r} != {self.origin!r}")
        chal = b64u(cd.get("challenge", ""))
        with self.lock:
            if chal not in self.challenges:
                raise ValueError("challenge was not issued or was already used")
            self.challenges.discard(chal)
        return cd

    @staticmethod
    def _auth_data(auth):
        if len(auth) < 37:
            raise ValueError("authenticatorData too short")
        if auth[:32] != hashlib.sha256(RP_ID.encode()).digest():
            raise ValueError("rpIdHash does not match localhost")
        flags = auth[32]
        if not flags & 0x01:
            raise ValueError("user presence flag not set")
        return flags, int.from_bytes(auth[33:37], "big")

    def register(self, body):
        self._client_data(b64u(body["clientDataJSON"]), "webauthn.create")
        att, _ = cbor_decode(b64u(body["attestationObject"]))
        auth = att["authData"]
        flags, count = self._auth_data(auth)
        if not flags & 0x40:
            raise ValueError("attested credential data flag missing")
        cid_len = int.from_bytes(auth[53:55], "big")
        cid = auth[55:55 + cid_len]
        cose, _ = cbor_decode(auth, 55 + cid_len)
        if cose.get(3) != -7 or cose.get(-1) != 1:
            raise ValueError("credential key is not ES256 / P-256")
        key = (int.from_bytes(cose[-2], "big"), int.from_bytes(cose[-3], "big"))
        if not _on_curve(key):
            raise ValueError("credential public key is not on P-256")
        cid_b64 = base64.urlsafe_b64encode(cid).rstrip(b"=").decode()
        if cid_b64 != body.get("id"):
            raise ValueError("credential id mismatch")
        with self.lock:
            self.credentials[cid_b64] = {"key": key, "count": count}
        return {"registered": cid_b64, "signCount": count, "uv": bool(flags & 0x04)}

    def verify(self, body):
        raw_cd = b64u(body["clientDataJSON"])
        self._client_data(raw_cd, "webauthn.get")
        auth = b64u(body["authenticatorData"])
        flags, count = self._auth_data(auth)
        with self.lock:
            cred = self.credentials.get(body.get("id"))
        if cred is None:
            raise ValueError("unknown credential id")
        signed = auth + hashlib.sha256(raw_cd).digest()
        if not es256_verify(cred["key"], signed, b64u(body["signature"])):
            raise ValueError("signature does not verify against the registered key")
        if (count or cred["count"]) and count <= cred["count"]:
            raise ValueError(f"sign counter did not increase ({count} <= {cred['count']})")
        with self.lock:
            cred["count"] = count
        return {"result": "assertion verified", "signCount": count,
                "uv": bool(flags & 0x04)}


def create_ephemeral_cert(temp_dir):
    cert_path = os.path.join(temp_dir, "localhost.crt")
    key_path = os.path.join(temp_dir, "localhost.key")
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-keyout", key_path,
         "-out", cert_path, "-days", "1", "-nodes", "-subj", "/CN=localhost",
         "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cert_path, key_path


def make_handler(rp):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def _json(self, status, payload):
            data = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            routes = {"/challenge": lambda b: {"challenge": rp.issue_challenge()},
                      "/register": rp.register, "/verify": rp.verify}
            route = routes.get(self.path)
            if route is None:
                return self._json(404, {"error": "no such endpoint"})
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body = json.loads(self.rfile.read(length) or b"{}")
                result = route(body)
            except (ValueError, KeyError, IndexError, TypeError) as err:
                print(f"RP {self.path} REJECTED: {err}", flush=True)
                return self._json(400, {"error": str(err)})
            print(f"RP {self.path} OK: {json.dumps(result)}", flush=True)
            return self._json(200, result)

    return Handler


def main():
    parser = argparse.ArgumentParser(description="Test-only HTTPS relying party for the WebAuthn fixture")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    temp_dir = None
    cert_path, key_path = args.cert, args.key
    if not cert_path or not key_path:
        temp_dir = tempfile.mkdtemp(prefix="webauthn-ssl-")
        try:
            cert_path, key_path = create_ephemeral_cert(temp_dir)
        except (OSError, subprocess.CalledProcessError) as err:
            sys.exit(f"Failed to generate ephemeral SSL certificate: {err}")

    rp = RelyingParty(f"https://localhost:{args.port}")
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(rp))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=cert_path, keyfile=key_path)
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    print(f"Serving WebAuthn fixture at https://localhost:{args.port}/fixture.html", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
        if temp_dir:
            import shutil
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
