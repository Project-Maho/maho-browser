#!/usr/bin/env bash
# check_unretained_cross_thread.sh
#
# Regression lint: detect base::Unretained() used within ±5 lines of a
# cross-thread PostTask pattern in maho-chromium/browser/.
#
# Checks for: PostTask | PostDelayedTask | PostTaskAndReply | BindPostTask
#
# Exit codes:
#   0 — no new violations (all violations are allowlisted)
#   1 — new violations found (must be reviewed and either fixed or allowlisted)
#
# Allowlist: scripts/ci/.ci-allowlist-unretained
#   One entry per line in "file:line" format.
#   Lines starting with "#" or blank lines are ignored.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SEARCH_DIR="${REPO_ROOT}/maho-chromium/browser"
ALLOWLIST="${SCRIPT_DIR}/.ci-allowlist-unretained"

allowed_entries=""
if [[ -f "${ALLOWLIST}" ]]; then
    while IFS= read -r entry; do
        entry="${entry#"${entry%%[![:space:]]*}"}"
        entry="${entry%"${entry##*[![:space:]]}"}"
        [[ -z "${entry}" || "${entry}" == \#* ]] && continue
        allowed_entries="${allowed_entries}
${entry}"
    done < "${ALLOWLIST}"
fi

allowlist_count=0
if [[ -n "${allowed_entries}" ]]; then
    allowlist_count=$(printf '%s\n' "${allowed_entries}" | grep -c '[^[:space:]]' || true)
fi

is_allowed() {
    local key="$1"
    printf '%s\n' "${allowed_entries}" | grep -qxF "${key}" 2>/dev/null
}

violations=""

while IFS= read -r -d '' filepath; do
    relpath="${filepath#${REPO_ROOT}/}"

    file_lines=()
    while IFS= read -r raw_line; do
        file_lines+=("${raw_line}")
    done < "${filepath}" 2>/dev/null || continue

    total=${#file_lines[@]}

    for (( i=0; i<total; i++ )); do
        line="${file_lines[$i]}"

        case "${line}" in
            *"base::Unretained("*) ;;
            *) continue ;;
        esac

        # Check ±5 lines for PostTask pattern
        lo=$(( i - 5 ))
        hi=$(( i + 5 ))
        [[ ${lo} -lt 0 ]] && lo=0
        [[ ${hi} -ge ${total} ]] && hi=$(( total - 1 ))

        found_post=0
        for (( j=lo; j<=hi; j++ )); do
            ctx="${file_lines[$j]}"
            case "${ctx}" in
                *PostTask*|*PostDelayedTask*|*PostTaskAndReply*|*BindPostTask*)
                    found_post=1
                    break
                    ;;
            esac
        done

        if [[ ${found_post} -eq 1 ]]; then
            linenum=$(( i + 1 ))
            key="${relpath}:${linenum}"
            if ! is_allowed "${key}"; then
                violations="${violations}
  ${key}"
            fi
        fi
    done
done < <(find "${SEARCH_DIR}" -type f \( \
    -name "*.cc" -o -name "*.cpp" -o -name "*.mm" -o -name "*.h" \
\) -print0)

new_count=0
if [[ -n "${violations}" ]]; then
    new_count=$(printf '%s\n' "${violations}" | grep -c '[^[:space:]]' || true)
fi

if [[ ${new_count} -eq 0 ]]; then
    echo "[check_unretained_cross_thread] OK — no new violations (allowlist size: ${allowlist_count})"
    exit 0
else
    echo "[check_unretained_cross_thread] FAIL — ${new_count} new violation(s) found:"
    printf '%s\n' "${violations}"
    echo ""
    echo "Either fix these usages or add them to:"
    echo "  ${ALLOWLIST}"
    echo ""
    echo "Format: one 'file:line' entry per line (relative to repo root)."
    exit 1
fi
