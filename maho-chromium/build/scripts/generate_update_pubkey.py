#!/usr/bin/env python3
"""Sparkle 2 ed25519 update-key utility for Maho Browser.

Modes:
  --generate --output-dir DIR   Generate a fresh ed25519 keypair. Writes the
                                private key (base64 seed, chmod 600) and the
                                32-byte raw public key, and prints the
                                SUPublicEDKey base64 value for the Info.plist
                                branding patch. Refuses to write inside the
                                git repo.
  --export-public PRIVKEY       Re-derive and print the SUPublicEDKey base64
                                value from a stored private key.
  --embed-public IN.bin OUT.h   Emit a C++ header embedding the 32-byte public
                                key (kMahoUpdateManifestPublicKey) plus the
                                kMahoUpdateManifestPublicKeyIsPlaceholder flag.
                                Invoked by the GN build rule. Pure stdlib.
  --check                       Self-test used by CI drift detection. Exits 0
                                only when every check passes.

The private key is NEVER committed. Only the 32-byte raw public key
(browser/keys/manifest_pubkey.bin) and the SUPublicEDKey base64 string enter
the repo. See docs/operations/release-key-management.md.
"""

import argparse
import ast
import base64
import os
import subprocess
import sys
import tempfile

PUBKEY_LEN = 32
PLACEHOLDER = b"\x00" * PUBKEY_LEN


def _eprint(*args):
    print(*args, file=sys.stderr)


def _load_cryptography():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )
        return Ed25519PrivateKey, serialization
    except ImportError:
        _eprint("error: the 'cryptography' package is required for this mode.")
        _eprint("       install it with: pip install cryptography")
        sys.exit(2)


def _read_private_seed(path):
    """Load a base64-encoded 32-byte ed25519 seed from PATH."""
    raw = open(path, "r", encoding="utf-8").read().strip()
    seed = base64.b64decode(raw)
    if len(seed) != PUBKEY_LEN:
        _eprint(f"error: {path} does not contain a 32-byte ed25519 seed "
                f"(decoded {len(seed)} bytes).")
        sys.exit(2)
    return seed


def _public_from_seed(seed):
    Ed25519PrivateKey, serialization = _load_cryptography()
    sk = Ed25519PrivateKey.from_private_bytes(seed)
    return sk.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )


def _inside_git_repo(path):
    """Return True if PATH resolves inside a git working tree."""
    target = os.path.abspath(path)
    try:
        top = subprocess.check_output(
            ["git", "-C", target if os.path.isdir(target) else os.path.dirname(target),
             "rev-parse", "--show-toplevel"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return False
    if not top:
        return False
    return os.path.commonpath([target, top]) == top


def cmd_generate(output_dir):
    Ed25519PrivateKey, serialization = _load_cryptography()

    if _inside_git_repo(output_dir):
        _eprint(f"error: refusing to write a private key inside the git repo: "
                f"{os.path.abspath(output_dir)}")
        _eprint("       choose an --output-dir outside the repository (e.g. "
                "~/maho-release-keys/).")
        sys.exit(2)

    os.makedirs(output_dir, exist_ok=True)
    sk = Ed25519PrivateKey.generate()
    seed = sk.private_bytes(
        serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
        serialization.NoEncryption(),
    )
    pub = sk.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )

    priv_path = os.path.join(output_dir, "maho_update_privkey.pem")
    pub_path = os.path.join(output_dir, "maho_update_pubkey.bin")
    # Open with 0600 from the start so the seed is never world-readable.
    fd = os.open(priv_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(base64.b64encode(seed).decode())
        f.write("\n")
    with open(pub_path, "wb") as f:
        f.write(pub)

    sup = base64.b64encode(pub).decode()
    print(f"Private key written to : {priv_path}  (chmod 600, KEEP OFFLINE)")
    print(f"Public key binary      : {pub_path}")
    print()
    print("SUPublicEDKey (paste into Info.plist branding patch):")
    print(f"  {sup}")
    print()
    print("Next steps:")
    print("  1. Store maho_update_privkey.pem in 1Password / KMS — never commit it.")
    print("  2. Copy maho_update_pubkey.bin to "
          "maho-chromium/browser/keys/manifest_pubkey.bin")
    print("  3. Add the SUPublicEDKey value to the Info.plist branding patch.")
    return 0


def cmd_export_public(privkey_path):
    seed = _read_private_seed(privkey_path)
    pub = _public_from_seed(seed)
    print(base64.b64encode(pub).decode())
    return 0


def cmd_embed_public(in_bin, out_h):
    with open(in_bin, "rb") as f:
        pub = f.read()
    if len(pub) != PUBKEY_LEN:
        _eprint(f"error: {in_bin} must be exactly {PUBKEY_LEN} bytes "
                f"(got {len(pub)}).")
        sys.exit(2)
    is_placeholder = pub == PLACEHOLDER
    byte_list = ", ".join(f"0x{b:02x}" for b in pub)
    header = (
        "// Generated by generate_update_pubkey.py. DO NOT EDIT.\n"
        "#ifndef MAHO_UPDATE_MANIFEST_PUBLIC_KEY_H_\n"
        "#define MAHO_UPDATE_MANIFEST_PUBLIC_KEY_H_\n"
        "\n"
        "#include <array>\n"
        "#include <cstdint>\n"
        "\n"
        "namespace maho {\n"
        "namespace updates {\n"
        "\n"
        f"inline constexpr std::array<uint8_t, {PUBKEY_LEN}> "
        "kMahoUpdateManifestPublicKey = {\n"
        f"    {byte_list}}};\n"
        "\n"
        "inline constexpr bool kMahoUpdateManifestPublicKeyIsPlaceholder = "
        f"{'true' if is_placeholder else 'false'};\n"
        "\n"
        "}  // namespace updates\n"
        "}  // namespace maho\n"
        "\n"
        "#endif  // MAHO_UPDATE_MANIFEST_PUBLIC_KEY_H_\n"
    )
    os.makedirs(os.path.dirname(os.path.abspath(out_h)), exist_ok=True)
    with open(out_h, "w", encoding="utf-8") as f:
        f.write(header)
    if is_placeholder:
        _eprint("warning: manifest public key is the all-zero PLACEHOLDER. "
                "Sparkle will reject every update until a real key is "
                "committed to browser/keys/manifest_pubkey.bin.")
    return 0


def cmd_check():
    failures = []

    # 1. This script parses without syntax errors.
    try:
        with open(os.path.abspath(__file__), "r", encoding="utf-8") as f:
            ast.parse(f.read())
    except SyntaxError as e:
        failures.append(f"ast.parse: {e}")

    with tempfile.TemporaryDirectory() as tmp:
        # 2. --embed-public placeholder and real-key round-trips.
        ph_bin = os.path.join(tmp, "ph.bin")
        ph_h = os.path.join(tmp, "ph.h")
        with open(ph_bin, "wb") as f:
            f.write(PLACEHOLDER)
        cmd_embed_public(ph_bin, ph_h)
        ph_txt = open(ph_h, encoding="utf-8").read()
        if "kMahoUpdateManifestPublicKeyIsPlaceholder = true" not in ph_txt:
            failures.append("embed-public: placeholder flag not set for zero key")

        real_bin = os.path.join(tmp, "real.bin")
        real_h = os.path.join(tmp, "real.h")
        with open(real_bin, "wb") as f:
            f.write(bytes(range(PUBKEY_LEN)))
        cmd_embed_public(real_bin, real_h)
        real_txt = open(real_h, encoding="utf-8").read()
        if "kMahoUpdateManifestPublicKeyIsPlaceholder = false" not in real_txt:
            failures.append("embed-public: placeholder flag set for real key")

        # 3 + 4 require cryptography.
        try:
            from cryptography.hazmat.primitives import serialization  # noqa: F401
            from cryptography.hazmat.primitives.asymmetric.ed25519 import (  # noqa: F401,E501
                Ed25519PrivateKey,
            )
            have_crypto = True
        except ImportError:
            have_crypto = False
            print("note: 'cryptography' not installed; skipping keygen checks.")

        if have_crypto:
            # 3. Defensive repo-write guard fires inside the repo.
            repo_dir = os.path.dirname(os.path.abspath(__file__))
            if not _inside_git_repo(repo_dir):
                print("note: script dir not in a git repo; skipping write-guard "
                      "check.")
            else:
                try:
                    cmd_generate(repo_dir)
                    failures.append("write-guard: --generate did not refuse a "
                                    "repo path")
                except SystemExit as e:
                    if e.code != 2:
                        failures.append(f"write-guard: unexpected exit {e.code}")

            # 4. --generate + --export-public produce consistent public bytes.
            keydir = os.path.join(tmp, "keys")
            cmd_generate(keydir)
            priv = os.path.join(keydir, "maho_update_privkey.pem")
            pub_bin = open(os.path.join(keydir, "maho_update_pubkey.bin"),
                           "rb").read()
            derived = _public_from_seed(_read_private_seed(priv))
            if derived != pub_bin:
                failures.append("consistency: export-public != generated .bin")

    if failures:
        _eprint("CHECK FAILED:")
        for f in failures:
            _eprint(f"  - {f}")
        return 1
    print("All checks passed.")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--generate", action="store_true",
                       help="Generate a fresh ed25519 keypair.")
    group.add_argument("--export-public", metavar="PRIVKEY",
                       help="Print SUPublicEDKey base64 from a private key.")
    group.add_argument("--embed-public", nargs=2, metavar=("IN_BIN", "OUT_H"),
                       help="Emit a C++ header embedding the public key.")
    group.add_argument("--check", action="store_true",
                       help="Run the CI self-test.")
    parser.add_argument("--output-dir", help="Destination for --generate.")
    args = parser.parse_args(argv)

    if args.generate:
        if not args.output_dir:
            parser.error("--generate requires --output-dir")
        return cmd_generate(args.output_dir)
    if args.export_public:
        return cmd_export_public(args.export_public)
    if args.embed_public:
        return cmd_embed_public(args.embed_public[0], args.embed_public[1])
    if args.check:
        return cmd_check()
    parser.error("no mode selected")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
