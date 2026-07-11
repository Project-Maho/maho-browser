#!/usr/bin/env python3
"""Generate update manifests for a Maho Browser release.

Produces two release assets consumed by the in-app updaters:

  * ``appcast.xml``    — Sparkle 2 feed for macOS. The enclosure carries a
                         ``sparkle:edSignature`` produced by ed25519-signing the
                         raw ``.dmg`` bytes with the release private key.
  * ``update-win.json`` — manifest read by ``maho_update_delegate_win.cc``:
                          version, download URL, SHA-256 and size.

The private key is supplied out-of-band (``--private-key`` file or the
``SPARKLE_ED_PRIVATE_KEY`` environment variable, base64-encoded 32-byte seed)
and is never written to disk by this script. See
docs/operations/release-key-management.md.
"""

import argparse
import base64
import hashlib
import os
import sys
from email.utils import formatdate
from xml.sax.saxutils import escape

RELEASE_BASE = "https://github.com/Project-Maho/release/releases/latest/download"


def _eprint(*args):
    print(*args, file=sys.stderr)


def _load_private_seed(path):
    if path:
        raw = open(path, "r", encoding="utf-8").read().strip()
    else:
        raw = os.environ.get("SPARKLE_ED_PRIVATE_KEY", "").strip()
        if not raw:
            _eprint("error: no private key. Pass --private-key FILE or set "
                    "SPARKLE_ED_PRIVATE_KEY.")
            sys.exit(2)
    seed = base64.b64decode(raw)
    if len(seed) != 32:
        _eprint(f"error: private key must decode to 32 bytes (got {len(seed)}).")
        sys.exit(2)
    return seed


def _ed_sign(seed, data):
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )
    except ImportError:
        _eprint("error: the 'cryptography' package is required for signing.")
        _eprint("       install it with: pip install cryptography")
        sys.exit(2)
    sk = Ed25519PrivateKey.from_private_bytes(seed)
    return base64.b64encode(sk.sign(data)).decode()


def _version_from_chrome_version(path):
    parts = {}
    for line in open(path, encoding="utf-8"):
        if "=" in line:
            k, v = line.strip().split("=", 1)
            parts[k] = v
    return ".".join(parts.get(k, "0")
                    for k in ("MAJOR", "MINOR", "BUILD", "PATCH"))


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


def build_win_manifest(version, exe_path, base_url, pub_date):
    import json
    sha256, size = _file_stats(exe_path)
    exe_url = f"{base_url}/{os.path.basename(exe_path)}"
    return json.dumps({
        "schemaVersion": 1,
        "version": version,
        "pubDate": pub_date,
        "platform": "windows-x64",
        "url": exe_url,
        "sha256": sha256,
        "size": size,
        "mandatory": False,
    }, indent=2) + "\n"


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
    p.add_argument("--base-url", default=RELEASE_BASE,
                   help="Base download URL for release assets.")
    p.add_argument("--notes-url",
                   default=f"{RELEASE_BASE}/release-notes.html",
                   help="Release notes URL for the appcast item.")
    p.add_argument("--output-dir", default="chromium/src/out/Default",
                   help="Where to write the manifests.")
    args = p.parse_args(argv)

    if not args.dmg and not args.win_exe:
        p.error("provide at least one of --dmg / --win-exe")

    version = args.version or _version_from_chrome_version(
        args.chrome_version_file)
    pub_date = formatdate(usegmt=True)
    os.makedirs(args.output_dir, exist_ok=True)

    if args.dmg:
        seed = _load_private_seed(args.private_key)
        appcast = build_appcast(version, args.dmg, seed, args.base_url,
                                args.notes_url, pub_date)
        out = os.path.join(args.output_dir, "appcast.xml")
        with open(out, "w", encoding="utf-8") as f:
            f.write(appcast)
        print(f"wrote {out}")

    if args.win_exe:
        manifest = build_win_manifest(version, args.win_exe, args.base_url,
                                      pub_date)
        out = os.path.join(args.output_dir, "update-win.json")
        with open(out, "w", encoding="utf-8") as f:
            f.write(manifest)
        print(f"wrote {out}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
