#!/usr/bin/env bash
# Add labels that maho-bot uses to Project-Maho/maho-browser.
# Idempotent: skips existing labels.
set -euo pipefail

REPO="Project-Maho/maho-browser"

declare -a LABELS=(
  "discord-sourced|8b5cf6|Reported via Discord community"
  "p0|b60205|Critical — crash, data loss, blocked users"
  "p1|d93f0b|Major — broken feature or wanted feature"
  "p2|fbca04|Minor bug or nice-to-have feature"
  "p3|c2e0c6|Trivial or long-tail"
  "ready-to-implement|0e8a16|Analyzed and scoped for implementation"
)

for entry in "${LABELS[@]}"; do
  IFS='|' read -r name color desc <<< "$entry"
  if gh label list -R "$REPO" --json name -q ".[].name" | grep -qx "$name"; then
    echo "  = $name (exists)"
  else
    gh label create "$name" -R "$REPO" --color "$color" --description "$desc"
    echo "  + $name"
  fi
done

echo "Done."
