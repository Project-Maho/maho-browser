#!/bin/bash
set -e
export PATH="/home/sook/.cargo/bin:/home/sook/.local/bin:$PATH"
export CARGO_TARGET_DIR="/home/sook/cargo_target_linux"
export SCCACHE_DIR="/home/sook/.cache/sccache"
export RUSTC_WRAPPER=""

cd /mnt/c/Users/sook/chromium/src
exec python3 maho/build/scripts/build_maho_linux_packages.py --format all --release
