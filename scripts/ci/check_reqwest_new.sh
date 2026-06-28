#!/usr/bin/env bash
# check_reqwest_new.sh
# Regression-lint: detect new reqwest::Client::new() usage in maho-core.
# Exit 0 if all hits are in the allowlist, exit 1 on any new violation.
# Compatible with bash 3 (macOS default) and bash 4+/Linux.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ALLOWLIST="${SCRIPT_DIR}/.ci-allowlist-reqwest-new"
SRC_DIR="${SCRIPT_DIR}/../../maho/crates/maho-core/src"

if [[ ! -f "${ALLOWLIST}" ]]; then
  echo "ERROR: allowlist not found: ${ALLOWLIST}" >&2
  exit 1
fi

found_violation=0

# Find all .rs files under maho-core/src
while IFS= read -r rs_file; do
  # Make path relative to SRC_DIR
  rel="${rs_file#${SRC_DIR}/}"

  # grep for reqwest::Client::new() with line numbers
  while IFS=: read -r lineno _rest; do
    entry="${rel}:${lineno}"
    if ! grep -qxF "${entry}" "${ALLOWLIST}" 2>/dev/null; then
      echo "ERROR: New reqwest::Client::new() usage: ${entry}" >&2
      found_violation=1
    fi
  done < <(grep -n "reqwest::Client::new()" "${rs_file}" 2>/dev/null || true)
done < <(find "${SRC_DIR}" -name "*.rs" -type f)

if [[ "${found_violation}" -eq 1 ]]; then
  echo "" >&2
  echo "Add new intentional usages to: ${ALLOWLIST}" >&2
  exit 1
fi

echo "OK: No new reqwest::Client::new() violations."
exit 0
