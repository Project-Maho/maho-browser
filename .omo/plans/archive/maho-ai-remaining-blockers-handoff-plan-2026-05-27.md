# Maho AI Panel: Remaining Blockers Handoff Plan

**Date:** 2026-05-27
**Status:** Ready for execution. Picks up after the silent blank-panel class of bugs was mostly eliminated.
**Audience:** A fresh session continuing this work without access to the prior conversation.

---

## Objective

Close the last three known blockers preventing the `maho_ai` side panel from rendering reliably. Each blocker is concrete, isolated, and individually verifiable. No architectural rework, no scope expansion.

---

## Current Verified State

- The earlier "silent blank panel" class of bugs is **mostly eliminated**. The panel boots in the common path.
- Package-local checks are **green** as of this handoff:
  - `bun run test` passes.
  - `bun run typecheck` passes.
- Three discrete blockers remain (listed below). The panel is **not** considered fully fixed until all three are addressed and re-verified.

Do not claim the panel is fully fixed before the acceptance criteria at the bottom of this document pass.

---

## Remaining Blockers (Exact)

1. **C++: silent return in `CreatePageHandler()` when no browser is found.**
   - File: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
   - Symptom: `CreatePageHandler()` returns silently when no `Browser*` is resolvable, leaving the WebUI bound to a half-initialized handler. The renderer side has no signal and the panel stays blank.
   - Fix shape: surface the failure. Either bind a no-op handler that reports the error to the renderer, or DCHECK + early-return with a logged warning so QA sees it. Do not keep the silent return.

2. **TS: `resumeSession` argument semantics inverted.**
   - Call site uses `resumeSession(sessionId, session?.isReadOnly ?? false)`.
   - The second positional argument on `resumeSession` is actually `fetchHistory`, not `isReadOnly`. The current call passes a read-only flag where a "should fetch history" boolean is expected, so resumed sessions skip history fetch when `isReadOnly` is true and over-fetch when it is false.
   - Fix shape: pass the correct intent for `fetchHistory` (typically `true` on resume), and route `isReadOnly` through whatever channel actually needs it (state setter, separate prop, or a follow-up call). Do not silently flip booleans without checking the function signature.

3. **GN: `react/boot-guard.ts` missing from bundle inputs.**
   - File: `maho-chromium/browser/resources/maho_ai/BUILD.gn`
   - Symptom: `react/boot-guard.ts` is referenced by the bundle but not declared as an input, so incremental builds can serve a stale bundle that omits the boot guard, reintroducing blank-panel symptoms intermittently.
   - Fix shape: add `react/boot-guard.ts` to the appropriate `inputs`/`sources` list so the bundler picks it up and ninja invalidates the bundle when it changes.

---

## Exact Files To Edit

- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc` (Blocker 1)
- The TS file containing the `resumeSession(sessionId, session?.isReadOnly ?? false)` call (Blocker 2). Locate via:
  ```bash
  rg -n "resumeSession\\(" maho-chromium/browser/resources/maho_ai
  ```
  Update the call site, and adjust any local state/prop wiring needed to keep `isReadOnly` honored without piggy-backing on `fetchHistory`.
- `maho-chromium/browser/resources/maho_ai/BUILD.gn` (Blocker 3)

If the `resumeSession` definition itself needs a clarifying comment about the second argument, add one. Do not rename the parameter as part of this pass unless it is trivially safe and contained to a single file.

---

## Ordered Implementation Steps

1. **Read the three target files first.** Confirm the bug shape matches what is described above. If the code has drifted, stop and reconcile before editing.
2. **Blocker 3 first (cheapest, unblocks bundle correctness).**
   - Open `maho-chromium/browser/resources/maho_ai/BUILD.gn`.
   - Add `react/boot-guard.ts` to the bundle inputs list alongside the other `react/*.ts` entries. Match existing ordering and quoting style.
3. **Blocker 2 (TS semantics).**
   - Find the `resumeSession(sessionId, session?.isReadOnly ?? false)` call.
   - Replace the second argument with the correct `fetchHistory` value (default to `true` on resume unless context proves otherwise).
   - Wire `isReadOnly` through the right channel (existing setter, prop, or follow-up call). Keep the change minimal.
4. **Blocker 1 (C++ silent return).**
   - In `CreatePageHandler()`, replace the silent early-return with one of:
     - Log a warning (`LOG(WARNING)` or the project's standard logger) and bind a handler that surfaces the no-browser state to the renderer, **or**
     - `DCHECK` in dev plus a logged warning in release, with a clearly inert handler so the renderer can render an error state instead of nothing.
   - Match the surrounding `DCHECK_CALLED_ON_VALID_SEQUENCE` and namespace conventions already in the file.
5. **Run verification (next section).**
6. **If anything fails, fix root cause, then re-run the full verification block. Do not declare done on partial green.**

---

## Verification Steps

Run from the repo root unless noted.

1. **TS/JS local checks (must stay green):**
   ```bash
   cd maho-chromium/browser/resources/maho_ai && bun run typecheck
   cd maho-chromium/browser/resources/maho_ai && bun run test
   ```
2. **LSP diagnostics on every edited file** (use the `lsp_diagnostics` tool). Zero errors required on:
   - `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
   - The TS file containing the fixed `resumeSession` call
   - `maho-chromium/browser/resources/maho_ai/BUILD.gn`
3. **Rust workspace sanity (cheap):**
   ```bash
   cd maho && bun run typecheck
   cd maho && bun run test
   ```
4. **Chromium build (canonical):**
   ```bash
   python3 maho-chromium/build/scripts/build_maho.py
   ```
   - Exit code 0 required.
   - **Do not** edit any `BUILD.gn` files outside `maho-chromium/browser/resources/maho_ai/BUILD.gn` to "fix" build failures. If GN regen fails elsewhere, stop and report. See root `AGENTS.md` "빌드 실패 시 GN 파일 강제 수정 금지".
5. **Manual smoke (when build succeeds):** open the maho_ai panel in a fresh profile. Confirm it renders content (not blank), survives a session resume, and that boot-guard messages appear when expected.

---

## Acceptance Criteria

All of the following must hold simultaneously:

- [ ] `CreatePageHandler()` no longer returns silently on missing browser. It either logs and binds an inert handler or `DCHECK`s with a logged warning. Renderer sees a state, not silence.
- [ ] `resumeSession(...)` is called with the correct `fetchHistory` argument. `isReadOnly` is preserved through a different channel and verified to still take effect.
- [ ] `react/boot-guard.ts` is listed as an input in `maho-chromium/browser/resources/maho_ai/BUILD.gn` so incremental builds invalidate on change.
- [ ] Package-local `bun run test` and `bun run typecheck` remain green.
- [ ] `lsp_diagnostics` reports zero errors on each edited file.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
- [ ] Manual smoke: panel renders, resume works, no silent blank.

---

## Handoff Notes

- The silent blank panel was **mostly** eliminated before this handoff. Treat any new blank-panel sighting during smoke as a regression and trace it back to one of the three blockers above before assuming a new bug.
- Keep edits surgical. No drive-by refactors, no naming sweeps, no extra logging beyond what each blocker needs.
- Respect the workspace `.build.lock`. Do not delete it. If a build hangs waiting on the lock, follow the procedure in the root `AGENTS.md` ("BUILD LOCK") rather than deleting the file.
- If you discover the code has drifted from the description here (function moved, file renamed, signature changed), pause and reconcile. Do not force-fit the plan onto code that no longer matches.
- Commit message style: plain user-style, no AI attribution, no `Co-authored-by:` trailers. One commit per blocker is fine; a single squashed commit is also fine if the diff is small.
