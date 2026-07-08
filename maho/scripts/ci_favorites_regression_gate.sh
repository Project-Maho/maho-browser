#!/bin/bash
# L3 regression gate: rejects any reintroduction of is_favorite flag pattern
VIOLATIONS=$(git grep -n \
  -e 'is_favorite\b' -e 'isFavorite\b' \
  -e 'IDC_MAHO_TAB_ADD_FAVORITE' -e 'IDC_MAHO_TAB_REMOVE_FAVORITE' \
  -- maho/ maho-chromium/ \
  | grep -v 'DEPRECATED' \
  | grep -v '// L3-EXEMPT' \
  | grep -iv 'test' \
  | grep -iv 'deprecated' \
  | grep -v 'ios-shell' \
  | grep -v 'macos-shell' \
  | grep -v 'android-shell' \
  | grep -v 'examples/' \
  | grep -v 'ci_favorites_regression_gate.sh' \
  | grep -v '.pre_migration_backup')

if [ -n "$VIOLATIONS" ]; then
  echo "L3 REGRESSION GATE FAILED: is_favorite references found:"
  echo "$VIOLATIONS"
  exit 1
fi
echo "L3 REGRESSION GATE PASSED"
exit 0
