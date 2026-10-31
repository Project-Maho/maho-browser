#!/usr/bin/env python3
"""Generate update manifests for a Maho Browser release.

Produces two release assets consumed by the in-app updaters:

  * ``appcast.xml``    — Sparkle 2 feed for macOS. The enclosure carries a
                         ``sparkle:edSignature`` produced by ed25519-signing the
                         raw ``.dmg`` bytes with the release private key.
  * ``update-win.json`` — signed JSON envelope read by
                          ``maho_update_delegate_win.cc``:
                          {"signature": "<128-hex ed25519>", "payload": "<inner JSON>"}
                          Payload carries version, installer_url, installer_sha256,
                          installer_signer_cn, and rollout_bucket.
  * ``update-linux.json`` — signed envelope of the same shape read by
                            ``maho_update_delegate_linux.cc`` from the relay
                            (``GET /updates/linux/check``). Linux never
                            self-updates, so the payload carries only
                            version and download_url (banner guidance).

The private key is supplied out-of-band (``--private-key`` file or the
``SPARKLE_ED_PRIVATE_KEY`` environment variable, base64-encoded 32-byte seed)
and is never written to disk or logged by this script. See
docs/operations/release-key-management.md.
"""

import argparse
import base64
import hashlib
import json
import os
import re
import sys
import tempfile
from email.utils import formatdate
from xml.sax.saxutils import escape

RELEASE_BASE = "https://github.com/Project-Maho/maho-browser/releases/latest/download"
MANIFEST_PUBKEY_HEX = "8b076b75cb7896810997c59b1ec7e184730d714ca0679e0deae22f7aba0d4dbc"
MANIFEST_PUBKEY_BYTES = bytes.fromhex(MANIFEST_PUBKEY_HEX)


def _eprint(*args):
    print(*args, file=sys.stderr)


def _load_private_seed(path=None):
    raw = ""
    if path:
        if not os.path.exists(path):
            _eprint(f"error: private key file not found: {path}")
            sys.exit(2)
        with open(path, "r", encoding="utf-8") as f:
            raw = f.read().strip()
    else:
        raw = os.environ.get("SPARKLE_ED_PRIVATE_KEY", "").strip()
        if not raw:
            _eprint("error: no private key. Pass --private-key FILE or set "
                    "SPARKLE_ED_PRIVATE_KEY.")
            sys.exit(2)
    try:
        seed = base64.b64decode(raw)
    except Exception:
        _eprint("error: private key is not valid base64.")
        sys.exit(2)
    if len(seed) != 32:
        _eprint(f"error: private key must decode to 32 bytes (got {len(seed)}).")
        sys.exit(2)
    return seed


def _get_ed25519_key(seed):
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )
    except ImportError:
        _eprint("error: the 'cryptography' package is required for signing.")
        _eprint("       install it with: pip install cryptography")
        sys.exit(2)
    return Ed25519PrivateKey.from_private_bytes(seed)


def _ed_sign(seed, data):
    sk = _get_ed25519_key(seed)
    return base64.b64encode(sk.sign(data)).decode()


def _ed_sign_hex(seed, data):
    sk = _get_ed25519_key(seed)
    return sk.sign(data).hex().lower()


def _version_from_chrome_version(path):
    parts = {}
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if "=" in line:
                k, v = line.strip().split("=", 1)
                parts[k] = v
        return ".".join(parts.get(k, "0")
                        for k in ("MAJOR", "MINOR", "BUILD", "PATCH"))
    return "0.1.0"


def _file_stats(path):
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def build_appcast(version, dmg_path, seed, base_url, notes_url, pub_date):
    with open(dmg_path, "rb") as f:
        data = f.read()
    ed_sig = _ed_sign(seed, data)
    length = len(data)
    dmg_url = f"{base_url}/{os.path.basename(dmg_path)}"
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<rss version="2.0" '
        'xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" '
        'xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        "  <channel>\n"
        "    <title>Maho Browser</title>\n"
        "    <item>\n"
        f"      <title>Version {escape(version)}</title>\n"
        f"      <pubDate>{pub_date}</pubDate>\n"
        f'      <sparkle:releaseNotesLink>{escape(notes_url)}'
        "</sparkle:releaseNotesLink>\n"
        f'      <sparkle:minimumSystemVersion>14.0</sparkle:minimumSystemVersion>\n'
        f'      <enclosure url="{escape(dmg_url)}" '
        f'sparkle:version="{escape(version)}" '
        f'sparkle:shortVersionString="{escape(version)}" '
        f'type="application/octet-stream" '
        f'length="{length}" '
        f'sparkle:edSignature="{escape(ed_sig)}" />\n'
        "    </item>\n"
        "  </channel>\n"
        "</rss>\n"
    )


def build_win_manifest(version, exe_path, seed, base_url,
                       installer_signer_cn="Maho Browser",
                       rollout_bucket=100):
    sha256, _ = _file_stats(exe_path)
    exe_url = f"{base_url}/{os.path.basename(exe_path)}"
    payload_obj = {
        "version": version,
        "installer_url": exe_url,
        "installer_sha256": sha256,
        "installer_signer_cn": installer_signer_cn,
        "rollout_bucket": int(rollout_bucket),
    }
    payload_str = json.dumps(payload_obj, separators=(",", ":"))
    sig_hex = _ed_sign_hex(seed, payload_str.encode("utf-8"))
    envelope = {
        "signature": sig_hex,
        "payload": payload_str,
    }
    return json.dumps(envelope, indent=2) + "\n"


def build_linux_manifest(version, seed, download_url):
    payload_obj = {
        "version": version,
        "download_url": download_url,
    }
    payload_str = json.dumps(payload_obj, separators=(",", ":"))
    sig_hex = _ed_sign_hex(seed, payload_str.encode("utf-8"))
    return json.dumps({"signature": sig_hex, "payload": payload_str},
                      indent=2) + "\n"


def run_self_test(args):
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
            Ed25519PublicKey,
        )
    except ImportError:
        _eprint("error: the 'cryptography' package is required for self-test.")
        _eprint("       install it with: pip install cryptography")
        return 1

    seed = _load_private_seed(args.private_key)
    sk = Ed25519PrivateKey.from_private_bytes(seed)
    pub_bytes = sk.public_key().public_bytes_raw()

    expected_pub_bytes = MANIFEST_PUBKEY_BYTES
    if pub_bytes != expected_pub_bytes:
        _eprint(
            f"error: private key derives public key {pub_bytes.hex()}, "
            f"expected {expected_pub_bytes.hex()} (manifest_pubkey.h)."
        )
        return 1

    vk = Ed25519PublicKey.from_public_bytes(expected_pub_bytes)

    # 1. Raw sign -> verify roundtrip
    test_msg = b"maho-manifest-verification-selftest"
    test_sig = sk.sign(test_msg)
    try:
        vk.verify(test_sig, test_msg)
    except InvalidSignature:
        _eprint("error: ed25519 raw verification failed.")
        return 1

    # Tampered message check
    try:
        vk.verify(test_sig, b"tampered-message")
        _eprint("error: ed25519 verification unexpectedly accepted tampered message.")
        return 1
    except InvalidSignature:
        pass

    # 2. Windows manifest build & envelope structure check
    with tempfile.TemporaryDirectory() as tmp_dir:
        dummy_exe = os.path.join(tmp_dir, "MahoSetup-test.exe")
        dummy_content = b"MZ\x90\x00dummy-executable-content-for-testing"
        with open(dummy_exe, "wb") as f:
            f.write(dummy_content)

        signer_cn = args.installer_signer_cn or "Maho Browser"
        rollout_bucket = args.rollout_bucket if args.rollout_bucket is not None else 100
        manifest_str = build_win_manifest(
            version="1.2.3",
            exe_path=dummy_exe,
            seed=seed,
            base_url="https://release.example.com",
            installer_signer_cn=signer_cn,
            rollout_bucket=rollout_bucket,
        )

        try:
            envelope = json.loads(manifest_str)
        except Exception as e:
            _eprint(f"error: generated manifest is not valid JSON: {e}")
            return 1

        if not isinstance(envelope, dict):
            _eprint("error: manifest envelope is not a JSON object.")
            return 1

        expected_envelope_keys = {"signature", "payload"}
        if set(envelope.keys()) != expected_envelope_keys:
            _eprint(
                f"error: manifest envelope keys {set(envelope.keys())} != "
                f"expected {expected_envelope_keys}"
            )
            return 1

        sig_hex = envelope["signature"]
        if not isinstance(sig_hex, str) or len(sig_hex) != 128:
            _eprint(f"error: signature must be a 128-char hex string.")
            return 1

        if sig_hex != sig_hex.lower() or not all(c in "0123456789abcdef" for c in sig_hex):
            _eprint("error: signature must be lowercase hex.")
            return 1

        payload_str = envelope["payload"]
        if not isinstance(payload_str, str):
            _eprint("error: envelope payload must be a string.")
            return 1

        # Verify signature over payload_str unescaped string bytes
        sig_bytes = bytes.fromhex(sig_hex)
        try:
            vk.verify(sig_bytes, payload_str.encode("utf-8"))
        except InvalidSignature:
            _eprint("error: envelope signature verification failed against public key.")
            return 1

        # Check payload JSON fields
        try:
            payload = json.loads(payload_str)
        except Exception as e:
            _eprint(f"error: inner payload is not valid JSON: {e}")
            return 1

        if not isinstance(payload, dict):
            _eprint("error: inner payload is not a JSON object.")
            return 1

        expected_payload_keys = {
            "version",
            "installer_url",
            "installer_sha256",
            "installer_signer_cn",
            "rollout_bucket",
        }
        if set(payload.keys()) != expected_payload_keys:
            _eprint(
                f"error: payload keys {set(payload.keys())} != "
                f"expected {expected_payload_keys}"
            )
            return 1

        if payload["version"] != "1.2.3":
            _eprint(f"error: payload version mismatch: {payload['version']}")
            return 1
        if payload["installer_url"] != "https://release.example.com/MahoSetup-test.exe":
            _eprint(f"error: payload installer_url mismatch: {payload['installer_url']}")
            return 1
        expected_sha256 = hashlib.sha256(dummy_content).hexdigest()
        if payload["installer_sha256"] != expected_sha256:
            _eprint(f"error: payload installer_sha256 mismatch: {payload['installer_sha256']}")
            return 1
        if payload["installer_signer_cn"] != signer_cn:
            _eprint(f"error: payload installer_signer_cn mismatch: {payload['installer_signer_cn']}")
            return 1
        if payload["rollout_bucket"] != rollout_bucket:
            _eprint(f"error: payload rollout_bucket mismatch: {payload['rollout_bucket']}")
            return 1
        if not isinstance(payload["rollout_bucket"], int):
            _eprint(f"error: payload rollout_bucket is not an int: {type(payload['rollout_bucket'])}")
            return 1

        # 3. macOS appcast build check
        dummy_dmg = os.path.join(tmp_dir, "Maho-test.dmg")
        dummy_dmg_content = b"koly-dmg-dummy-data"
        with open(dummy_dmg, "wb") as f:
            f.write(dummy_dmg_content)

        notes_url = args.notes_url or "https://example.com/notes.html"
        pub_date = formatdate(usegmt=True)
        appcast_xml = build_appcast("1.2.3", dummy_dmg, seed, "https://release.example.com", notes_url, pub_date)

        if f"<sparkle:releaseNotesLink>{notes_url}</sparkle:releaseNotesLink>" not in appcast_xml:
            _eprint("error: releaseNotesLink missing or incorrect in appcast.")
            return 1

        sig_match = re.search(r'sparkle:edSignature="([^"]+)"', appcast_xml)
        if not sig_match:
            _eprint("error: sparkle:edSignature missing from appcast.")
            return 1
        dmg_sig_b64 = sig_match.group(1)
        dmg_sig_bytes = base64.b64decode(dmg_sig_b64)
        try:
            vk.verify(dmg_sig_bytes, dummy_dmg_content)
        except InvalidSignature:
            _eprint("error: appcast edSignature verification failed against public key.")
            return 1

    print("Self-test passed: ed25519 signing, verification roundtrips, and manifest schemas valid.")
    return 0


def main(argv):
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--version", help="Release version (e.g. 0.1.0). "
                   "Defaults to chrome/VERSION.")
    p.add_argument("--chrome-version-file",
                   default="chromium/src/chrome/VERSION",
                   help="Fallback VERSION file when --version is omitted.")
    p.add_argument("--private-key",
                   help="File with base64 ed25519 seed. Falls back to "
                        "SPARKLE_ED_PRIVATE_KEY env.")
    p.add_argument("--dmg", help="macOS .dmg path (enables appcast.xml).")
    p.add_argument("--win-exe", help="Windows .exe path (enables "
                                     "update-win.json).")
    p.add_argument("--linux", action="store_true",
                   help="Emit update-linux.json (signed version envelope).")
    p.add_argument("--linux-download-url",
                   default="https://mahobrowser.com/download/",
                   help="Download page carried in the Linux payload.")
    p.add_argument("--base-url", default=RELEASE_BASE,
                   help="Base download URL for release assets.")
    p.add_argument("--notes-url",
                   default=f"{RELEASE_BASE}/release-notes.html",
                   help="Release notes URL for the appcast item.")
    p.add_argument("--rollout-bucket", type=int, default=100,
                   help="Rollout bucket percentage (0-100, default: 100).")
    p.add_argument("--installer-signer-cn", default="Maho Browser",
                   help="Expected Windows authenticode certificate Common Name.")
    p.add_argument("--output-dir", default="chromium/src/out/Default",
                   help="Where to write the manifests.")
    p.add_argument("--self-test", action="store_true",
                   help="Run ed25519 signing and verification self-test.")
    args = p.parse_args(argv)

    if args.self_test:
        return run_self_test(args)

    if not args.dmg and not args.win_exe and not args.linux:
        p.error("provide at least one of --dmg / --win-exe / --linux / "
                "--self-test")

    version = args.version or _version_from_chrome_version(
        args.chrome_version_file)
    pub_date = formatdate(usegmt=True)
    os.makedirs(args.output_dir, exist_ok=True)

    seed = None
    if args.dmg or args.win_exe or args.linux:
        seed = _load_private_seed(args.private_key)

    if args.dmg:
        appcast = build_appcast(version, args.dmg, seed, args.base_url,
                                args.notes_url, pub_date)
        out = os.path.join(args.output_dir, "appcast.xml")
        with open(out, "w", encoding="utf-8") as f:
            f.write(appcast)
        print(f"wrote {out}")

    if args.win_exe:
        manifest = build_win_manifest(
            version=version,
            exe_path=args.win_exe,
            seed=seed,
            base_url=args.base_url,
            installer_signer_cn=args.installer_signer_cn,
            rollout_bucket=args.rollout_bucket,
        )
        out = os.path.join(args.output_dir, "update-win.json")
        with open(out, "w", encoding="utf-8") as f:
            f.write(manifest)
        print(f"wrote {out}")

    if args.linux:
        manifest = build_linux_manifest(version, seed, args.linux_download_url)
        out = os.path.join(args.output_dir, "update-linux.json")
        with open(out, "w", encoding="utf-8") as f:
            f.write(manifest)
        print(f"wrote {out}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
