#!/bin/sh
# Maho CLI installer — inspect before running:
#   curl -fsSL https://mahobrowser.com/install-cli.sh | sh
# Installs the `maho` binary to ~/.maho/bin (override: MAHO_INSTALL_DIR).
# Supported: macOS (arm64, x86_64), Linux (x86_64, arm64). Native Windows: use WSL.

set -eu

RELEASE_BASE="${MAHO_RELEASE_BASE:-https://github.com/Project-Maho/release/releases/latest/download}"
INSTALL_DIR="${MAHO_INSTALL_DIR:-${HOME}/.maho/bin}"
BIN_NAME="maho"

err() { printf 'error: %s\n' "$1" >&2; exit 1; }
info() { printf '%s\n' "$1" >&2; }

os="$(uname -s)"
case "$os" in
  Darwin) os_token="darwin" ;;
  Linux)  os_token="linux" ;;
  *) err "unsupported operating system: ${os}. macOS and Linux only (use WSL on Windows)." ;;
esac

arch="$(uname -m)"
case "$arch" in
  arm64|aarch64) arch_token="arm64" ;;
  x86_64|amd64)  arch_token="x86_64" ;;
  *) err "unsupported CPU architecture: ${arch}." ;;
esac

asset="maho-cli-${os_token}-${arch_token}.tar.gz"
url="${RELEASE_BASE}/${asset}"

if command -v curl >/dev/null 2>&1; then
  dl() { curl -fsSL "$1" -o "$2"; }
elif command -v wget >/dev/null 2>&1; then
  dl() { wget -qO "$2" "$1"; }
else
  err "neither curl nor wget found; cannot download ${asset}."
fi

sha_cmd=""
if command -v sha256sum >/dev/null 2>&1; then
  sha_cmd="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
  sha_cmd="shasum -a 256"
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/maho-cli.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT INT TERM

info "Downloading ${asset} ..."
dl "$url" "${tmp}/${asset}" || err "download failed: ${url}"

if dl "${url}.sha256" "${tmp}/${asset}.sha256" 2>/dev/null; then
  if [ -n "$sha_cmd" ]; then
    expected="$(awk '{print $1}' "${tmp}/${asset}.sha256")"
    actual="$($sha_cmd "${tmp}/${asset}" | awk '{print $1}')"
    [ "$expected" = "$actual" ] || err "checksum mismatch for ${asset} (expected ${expected}, got ${actual})."
    info "Checksum verified."
  else
    info "warning: no sha256 tool found; skipping checksum verification."
  fi
else
  info "warning: no published checksum for ${asset}; skipping verification."
fi

tar -xzf "${tmp}/${asset}" -C "$tmp" || err "failed to extract ${asset}."
[ -f "${tmp}/${BIN_NAME}" ] || err "archive did not contain a '${BIN_NAME}' binary."

mkdir -p "$INSTALL_DIR"
mv -f "${tmp}/${BIN_NAME}" "${INSTALL_DIR}/${BIN_NAME}"
chmod +x "${INSTALL_DIR}/${BIN_NAME}"

# macOS Gatekeeper quarantines downloaded binaries; clear the flag so it can launch.
if [ "$os_token" = "darwin" ] && command -v xattr >/dev/null 2>&1; then
  xattr -d com.apple.quarantine "${INSTALL_DIR}/${BIN_NAME}" 2>/dev/null || true
fi

info "Installed ${BIN_NAME} to ${INSTALL_DIR}/${BIN_NAME}"

case ":${PATH}:" in
  *":${INSTALL_DIR}:"*)
    info "Run 'maho --help' to get started."
    ;;
  *)
    info ""
    info "${INSTALL_DIR} is not on your PATH. Add it, then restart your shell:"
    info "  echo 'export PATH=\"${INSTALL_DIR}:\$PATH\"' >> ~/.profile"
    info "Then run 'maho --help'."
    ;;
esac
