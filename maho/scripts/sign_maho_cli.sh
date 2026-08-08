#!/bin/sh
# Code-sign (Developer ID, hardened runtime) and notarize the standalone macOS
# `maho` binary. Same identity + APPLE_API_KEY/_KEY_ID/_ISSUER convention as
# build_maho.py. Usage: sign_maho_cli.sh <path-to-binary>
# A bare Mach-O cannot be stapled, so Gatekeeper checks notarization online.

set -eu

bin="${1:?usage: sign_maho_cli.sh <path-to-binary>}"
[ -f "$bin" ] || { printf 'error: binary not found: %s\n' "$bin" >&2; exit 1; }

# Notarization requires a "Developer ID Application" cert (App Store /
# Distribution certs are rejected for out-of-store notarization).
identity="${MAHO_CODESIGN_IDENTITY:-}"
if [ -z "$identity" ]; then
  identity="$(security find-identity -v -p codesigning \
    | awk -F'"' '/Developer ID Application/ {print $2; exit}')"
fi
[ -n "$identity" ] || { printf 'error: no "Developer ID Application" codesigning identity found; notarization needs one\n' >&2; exit 1; }

printf 'Signing %s with %s\n' "$bin" "$identity" >&2
codesign --force --timestamp --options runtime --sign "$identity" "$bin"
codesign --verify --strict --verbose=2 "$bin"

api_key="${APPLE_API_KEY:-}"
key_id="${APPLE_API_KEY_ID:-}"
issuer="${APPLE_API_ISSUER:-}"
if [ -z "$api_key" ] || [ -z "$key_id" ] || [ -z "$issuer" ]; then
  printf 'warning: APPLE_API_KEY / APPLE_API_KEY_ID / APPLE_API_ISSUER missing; skipping notarization\n' >&2
  exit 0
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/maho-notarize.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT INT TERM
key_file="${tmp}/AuthKey.p8"
printf '%s' "$api_key" > "$key_file"
zip_path="${tmp}/maho.zip"
ditto -c -k "$bin" "$zip_path"

printf 'Submitting to Apple Notary Service...\n' >&2
xcrun notarytool submit "$zip_path" --key-id "$key_id" --issuer "$issuer" --key "$key_file" --wait
printf 'Notarization complete (bare binary: no stapling; Gatekeeper checks online).\n' >&2
