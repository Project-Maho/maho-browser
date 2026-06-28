#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ALLOWLIST="${SCRIPT_DIR}/.ci-allowlist-ffi-safety"
FFI_SRC="maho/crates/maho-ffi/src"

violations="$(
  find "${FFI_SRC}" -name "*.rs" | sort | xargs awk -v allowlist="${ALLOWLIST}" '
    BEGIN {
      if ((getline line < allowlist) < 0) { }
      close(allowlist)
      while ((getline line < allowlist) > 0) {
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
        if (line == "" || substr(line, 1, 1) == "#") continue
        allowed[line] = 1
      }
    }
    FNR == 1 {
      delete buf
      buflen = 0
    }
    {
      buf[FNR] = $0
    }
    /pub[[:space:]]+unsafe[[:space:]]+extern[[:space:]]+"C"[[:space:]]+fn[[:space:]]+[A-Za-z0-9_]+/ {
      fn_name = $0
      sub(/.*pub[[:space:]]+unsafe[[:space:]]+extern[[:space:]]+"C"[[:space:]]+fn[[:space:]]+/, "", fn_name)
      sub(/[^A-Za-z0-9_].*/, "", fn_name)
      has_safety = 0
      # Widened from 3 → 10 lines to tolerate #[no_mangle] / TODO comments / multi-line
      # safety doc blocks between the doc and the fn declaration.
      for (off = 1; off <= 10; off++) {
        idx = FNR - off
        if (idx >= 1 && buf[idx] ~ /# Safety/) { has_safety = 1; break }
      }
      if (!has_safety && !(fn_name in allowed)) {
        print "VIOLATION: " FILENAME ":" FNR ": '"'"'" fn_name "'"'"' is missing '"'"'/// # Safety'"'"' and is not in the allowlist"
        count++
      }
    }
    END {
      exit (count > 0) ? count : 0
    }
  '
)" || awk_exit=$?

count=$(echo "${violations}" | grep -c "^VIOLATION:" 2>/dev/null || true)

if [ "${count:-0}" -gt 0 ]; then
  echo "${violations}"
  echo ""
  echo "ERROR: ${count} new pub unsafe extern \"C\" fn declaration(s) lack '/// # Safety' docs."
  echo "  -> Add a '/// # Safety' doc comment within 10 lines above the function, OR"
  echo "  -> Add its name to: ${ALLOWLIST}"
  exit 1
fi

echo "OK: all pub unsafe extern \"C\" fn declarations are either documented or allowlisted."
exit 0
