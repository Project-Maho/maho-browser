#!/bin/bash
set -e

export PATH="$HOME/.bun/bin:/opt/homebrew/bin:/opt/homebrew/sbin:$HOME/.local/bin:$HOME/.cargo/bin:$HOME/depot_tools:$PATH"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
export MAHO_NINJA_JOBS="${MAHO_NINJA_JOBS:-8}"

export SCCACHE_BUCKET="${SCCACHE_BUCKET:-sccache}"
export SCCACHE_ENDPOINT="${SCCACHE_ENDPOINT:-127.0.0.1:9000}"
export SCCACHE_REGION="${SCCACHE_REGION:-us-east-1}"
export SCCACHE_S3_KEY_PREFIX="${SCCACHE_S3_KEY_PREFIX:-/maho/}"
export SCCACHE_S3_USE_SSL="${SCCACHE_S3_USE_SSL:-false}"
export AWS_ACCESS_KEY_ID="${AWS_ACCESS_KEY_ID:-mahocache}"
export AWS_SECRET_ACCESS_KEY="${AWS_SECRET_ACCESS_KEY:-bbc80195e1fbda1ee05ac8629ba56ffdacd0fb329fa94ff2}"

sccache --start-server 2>/dev/null || true

cd "$(dirname "$0")"

if [ -d "website" ]; then
  if [ ! -d "website/node_modules" ] || [ ! -f "website/node_modules/.bin/tailwindcss" ] || [ ! -d "website/node_modules/esbuild" ]; then
    echo "Preflight: bootstrapping website/node_modules via bun..."
    (cd website && bun install && bun add @tailwindcss/cli)
  fi
fi

python3 maho-chromium/build/scripts/build_maho.py \
  --release \
  --out-dir out/Release \
  --no-launch \
  --skip-lucide-check \
  --skip-rust \
  "$@"
