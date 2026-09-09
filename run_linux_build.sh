#!/bin/bash
set -e
export PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH"
export CARGO_TARGET_DIR="$HOME/cargo_target_linux"
export SCCACHE_DIR="$HOME/.cache/sccache"
export RUSTC_WRAPPER=""

cd "$(dirname "$(realpath "$0")")/chromium/src"
exec python3 maho/build/scripts/build_maho_linux_packages.py --format all --release
