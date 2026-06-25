# Uncommitted-Work Integration Hyperplan

**Generated:** 2026-06-25
**Branch:** main (3 commits ahead of origin)
**Uncommitted scope:** 71 files changed, +3985 / −3868 lines (not counting untracked)
**Methodology:** Adversarial 5-member hyperplan debate (skeptic / validator / researcher / architect / creative) → distilled insight bundle → executable plan via plan agent

---

## Context

| Bucket | Domain | Scope |
|--------|--------|-------|
| A | Phase 4 Import Migration | **ALREADY SHIPPED** (commit `5a3faf9b`). Plan file is stale and must be archived, not executed. |
| B | maho-settings finalization | 10 files, −2616/+281 lines (React migration, `app.ts` deletion, 3 Lucide icons) |
| C | maho_welcome React | Entirely untracked new directory (~18+ files: WebUI + Mojo + Views window + Playwright/Vitest) |
| D | ios-shell + maho-agent + FFI + web-ai + storage + types + android | 40+ files across 9 subsystems |

**Architectural constraint:** `.build.lock` serializes ALL build/test verification. No parallel builds.

---

## User Input Gates — RESOLVED (2026-06-25)

| Gate | Decision | Implication |
|------|----------|-------------|
| **Q1 Scope** | **Ship-All** | B/C/D 모두 `main`에 커밋. C는 gate 실패시에도 fix 시도 후 ship, defer 옵션 비활성. |
| **Q2 FFI Header** | **Canonical + `#include`** | `third_party/maho/maho_ffi.h`를 canonical로 지정. `ios-shell/include/maho_ffi.h`와 `macos-shell/include/maho_ffi.h`는 `#include "third_party/maho/maho_ffi.h"`로 대체 (또는 path-relative include). |
| **Q3 web-ai v2** | **Ship now** | D.6 web-ai v2 + consumer migration 원자 커밋, 이번 통합에 포함. |
| **Q4 Welcome Tests** | **Local-only** | `playwright.config.ts`, `vitest.config.ts` 유지. `test-results/`는 gitignore 추가. CI 워크플로 수정 없음. |
| **Q5 Stale Plans** | **Archive subdirectory** | `.omo/plans/archive/`로 `git mv`. 히스토리 보존. |
| **Q6 Bucket D Priority** | **전부 must-ship** | maho-storage + maho-agent + android-shell + web-ai v2 + ios-shell 전부 이번 통합에 포함. maho-agent는 D.0.2 carry-over 검증 후 ship. |

---

## Pre-Conditions (Wave 0)

Resolve before touching any code. These ARE blocking.

- [ ] **Pre-0.1: Patch `.gitignore` for repo-root screenshots.** Add `/*.png` (or list explicit paths) at the top of root `.gitignore`. Verify Playwright `test-results/` is also gitignored.
  - **Success:** `git status` no longer lists `maho-sidebar-alignment-qa.png`, `pinch-summary-*.png`, `space-ai-config-*.png`.
- [ ] **Pre-0.2: Safety branch.** `git branch backup/pre-integration-2026-06-25`.
  - **Success:** `git branch | grep backup/pre-integration-2026-06-25` returns 1 line.
- [ ] **Pre-0.3: Stash inspection (5-min per stash).** For each of `stash@{0..4}`, run `git stash show -p stash@{N}` and write a 1-line verdict (DROP / RESCUE-to-branch / KEEP).
  - **Success:** Verdict written; if RESCUE: stashed onto a fresh branch `salvage/<topic>`. Then `git stash drop stash@{N}` for all DROP verdicts (in reverse-index order).
- [ ] **Pre-0.4: maho-types HEAD-vs-uncommitted test verification.**
  - Run: `cd maho && cargo test -p maho-types --test shared_contract_test`
  - **Success:** all tests pass. If fail with current working tree: identify which uncommitted hunk causes the regression and document it before proceeding.
- [ ] **Pre-0.5: Answer Q1–Q6** inline above.

---

## Train A — Bucket A: Archive Phase 4 Plan (Wave 1, fast)

[HC1, DEC1]

- [ ] **A.1: Verify `5a3faf9b` deliverable matches plan `import-rust-migration-phase4-callbacks-streaming-2026-06-24.md` Phase 1/2A/2B/2C.**
  - Run: `git show --stat 5a3faf9b -- maho/crates/maho-ffi/src/import.rs maho/crates/maho-ffi/src/import_destination.rs maho-chromium/browser/importer/maho_import_session_bridge.{h,cc}`
  - Check: streaming worker-thread fix present in `import.rs`? Cookie/autofill/favicon thunks present in `maho_import_session_bridge.cc`? ACCEPTANCE.md #1 and #13 marked ✅?
  - **Success:** Every Phase 1/2A/2B/2C deliverable in the plan file matches code in `5a3faf9b`. Any gap → file as separate `phase4-cleanup-2026-06-25.md` follow-up.
- [ ] **A.2 (atomic commit): Archive plan file + update ACCEPTANCE.md if needed.**
  - `mkdir -p .omo/plans/archive/`
  - `git mv .omo/plans/import-rust-migration-phase4-callbacks-streaming-2026-06-24.md .omo/plans/archive/`
  - If ACCEPTANCE gaps found in A.1: edit `maho-chromium/browser/importer/ACCEPTANCE.md` to flip #1, #13 to ✅; else skip.
  - Commit message: `chore(plans): archive Phase 4 import-migration plan (shipped in 5a3faf9b)`
  - **Success:** Plan file moved, commit lands cleanly, no diff drift.

---

## Train B — Bucket B: Settings Finalization (Wave 2, depends on Pre-0.4 + DEC4)

[HC2, HC4, HC5, DEC2, DEC4]

- [ ] **B.1: Lift `maho-types/src/settings.rs` delta into FIRST commit (DEC4).**
  - Confirm with `git diff maho/crates/maho-types/src/settings.rs` it is the `PinnedCloseBehavior::UnloadSwitch → Switch` single-line semantic change.
  - Stage only this file, commit message: `refactor(types): rename PinnedCloseBehavior::UnloadSwitch → Switch`
  - **Verification:** `cargo test -p maho-types` passes; `bun run typecheck` passes (from `maho/`).
- [ ] **B.2: Visual QA / manual smoke (V4).** [maho-settings-style-fixes plan V4]
  - Launch a Maho debug build (`python3 maho-chromium/build/scripts/build_maho.py --skip-rust` if not built since B.1).
  - Open `chrome://maho-settings`, verify: all panes render, "Chromium settings" embed pane shows, no Unicode glyphs visible (✓ × → replaced with Lucide), 3 vendored SVGs load.
  - **Success:** Manual smoke passes; if issues, fix or revert specific hunks before B.3.
- [ ] **B.3 (atomic commit): commit all 12 maho-settings files.**
  - Stage:
    - `maho-chromium/browser/resources/maho_settings/BUILD.gn`
    - `maho-chromium/browser/resources/maho_settings/models.ts`
    - `maho-chromium/browser/resources/maho_settings/react/app.tsx`
    - `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx`
    - `maho-chromium/browser/resources/maho_settings/schema/{domains,panes,setting_schema}.ts`
    - `maho-chromium/browser/resources/maho_settings/settings.{css,html}`
    - DELETION: `maho-chromium/browser/resources/maho_settings/app.ts`
    - DELETION: `maho-chromium/browser/resources/maho_common/react/theme_provider.tsx`
    - NEW: `maho-chromium/browser/resources/maho_settings/icons/*.svg` (3 Lucide files)
  - Commit message: `feat(settings): React migration + Lucide icon system + Chromium-settings embed pane`
  - **Verification:** `lsp_diagnostics` clean on all changed files; `chrome://maho-settings` runtime loads after `build_maho.py --skip-rust`.

---

## Train C — Bucket C: maho_welcome Triage (Wave 3, depends on user gates Q1/Q4)

[HC8, DEC3, RISK3]

- [ ] **C.1: `gn check` gate.**
  - Run: `cd chromium/src && gn check out/Default //maho-chromium/browser/resources/maho_welcome` and `gn check out/Default //maho-chromium/browser/ui/webui/maho_welcome` and `gn check out/Default //maho-chromium/browser/ui/views/welcome`
  - **Success:** All `gn check` commands return exit 0.
  - **Failure:** STOP. Either fix BUILD.gn or defer Bucket C to feature branch.
- [ ] **C.2: Mojo bindings generation gate.**
  - Run: `autoninja -C out/Default maho-chromium/browser/ui/webui/maho_welcome:mojom_js` (or whatever the mojom target is — verify in `BUILD.gn`)
  - **Success:** `out/Default/gen/maho-chromium/browser/ui/webui/maho_welcome/maho_welcome.mojom*.js` exists.
- [ ] **C.3: `autoninja` full build gate.**
  - Run: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target maho_welcome` (or determine the actual target name from BUILD.gn)
  - **Success:** Exit code 0. Build produces a usable artifact.
- [ ] **C.4: Test gates (per Q4 answer).**
  - If Q4=CI: run `cd maho-chromium/browser/resources/maho_welcome && bun run test` (vitest) AND `bun run playwright test` — both must pass.
  - If Q4=local-only: skip; add `test-results/` to gitignore.
- [ ] **C.5 (atomic commit, only if all gates pass): commit all of maho_welcome.**
  - Stage all 3 directories: `maho-chromium/browser/resources/maho_welcome/`, `maho-chromium/browser/ui/webui/maho_welcome/`, `maho-chromium/browser/ui/views/welcome/`.
  - Commit message: `feat(welcome): maho_welcome React WebUI with Playwright + Vitest`
  - **Verification:** Repeat C.1+C.3 post-commit; `chrome://maho-welcome` opens at runtime.
- [ ] **C.5-ALT (if gate fails): defer to feature branch.**
  - `git checkout -b feature/maho-welcome-react`
  - `git add .` for the 3 directories; commit on the branch.
  - Switch back to main; main remains clean.

---

## Train D — Bucket D: Per-Subsystem Decomposition (Wave 3+, depends on Q2/Q3/Q6)

[HC3, HC7, HC9, DEC5, DEC6]

### D.0 — Preconditions
- [ ] **D.0.1: Decide FFI header policy (Q2).** See above.
- [ ] **D.0.2: Audit `maho-agent` hunk vs commit `eb342681`.** [RISK7]
  - Run: `git show eb342681 -- maho/crates/maho-agent/Cargo.toml` and diff against `git diff maho/crates/maho-agent/Cargo.toml`.
  - If hunk overlaps eb342681 → discard hunk (carry-over). If novel → keep.
- [ ] **D.0.3: web-ai consumer migration audit (Q3).** [HC7, RISK4]
  - Grep `web-ai/src/screens/*` and `web-ai/src/hooks/*` for old protocol v1 signatures (e.g., id as number, no BridgeRpcError).
  - **Success:** No v1 references remain.

### D.1 — Storage (one commit)
- [ ] **D.1: Commit `maho-storage` changes.**
  - Stage `maho/crates/maho-storage/src/sqlite.rs` + `maho/crates/maho-storage/tests/storage_test.rs`.
  - Commit message: `feat(storage): expand SQLite backend + tests`
  - **Verification:** `cd maho && cargo test -p maho-storage` passes.

### D.2 — Core (one commit)
- [ ] **D.2: Commit `maho-core` changes.**
  - Stage `maho/crates/maho-core/{src/content_blocker.rs,src/maho_core.rs,benches/core_bench.rs,tests/core_engine_test.rs}`.
  - Commit message: `feat(core): expand content_blocker + core changes`
  - **Verification:** `cd maho && cargo test -p maho-core` passes; `cargo bench -p maho-core --no-run` compiles.

### D.3 — FFI (one commit)
- [ ] **D.3: Commit `maho-ffi/src/lib.rs` audit log + `maho-ffi/Cargo.toml` dep + `maho-bench/Cargo.toml` dep + `maho-jni` test.**
  - Stage:
    - `maho/crates/maho-ffi/Cargo.toml`
    - `maho/crates/maho-ffi/src/lib.rs` (the +5-line `tracing::info!` at line 6211)
    - `maho/crates/maho-bench/Cargo.toml`
    - `maho/crates/maho-jni/tests/maho_bridge_export_coverage.rs`
  - Commit message: `feat(ffi): add BYOK key-access audit log + JNI export coverage`
  - **Verification:** `cd maho && cargo build --workspace --all-targets` succeeds.

### D.4 — Agent (one commit, per Q6/D.0.2)
- [ ] **D.4: Commit (or discard) `maho-agent/Cargo.toml` per D.0.2 verdict.**
  - If novel: commit message `chore(agent): add <dep>`.
  - If carry-over: `git checkout -- maho/crates/maho-agent/Cargo.toml` to discard.

### D.5 — Cargo.lock + relay/Cargo.lock (one commit)
- [ ] **D.5: Commit lockfile updates.**
  - Stage `maho/Cargo.lock` + `maho/relay/Cargo.lock`.
  - Commit message: `chore: update Cargo.lock`
  - **Verification:** `cd maho && cargo metadata > /dev/null` succeeds (no metadata errors).

### D.6 — web-ai protocol v2 (one atomic commit, per Q3)
- [ ] **D.6 (atomic commit): commit all web-ai files together.** [HC7]
  - Stage:
    - `maho/web-ai/package.json` + `maho/web-ai/bun.lock`
    - `maho/web-ai/src/bridge/rpc.ts` + `maho/web-ai/src/bridge/types.ts`
    - `maho/web-ai/src/hooks/use-bridge.ts` + `maho/web-ai/src/hooks/use-streaming.ts`
    - `maho/web-ai/src/screens/chat/chat-screen.tsx`
    - `maho/web-ai/src/screens/conversations/conversation-list-screen.tsx`
    - `maho/web-ai/src/screens/pinch-summary/pinch-summary-screen.tsx`
  - Commit message: `feat(web-ai): bridge protocol v2 (typed RPC + BridgeRpcError + streaming hooks)`
  - **Verification:** `cd maho/web-ai && bun run typecheck` passes; relevant unit tests pass.

### D.7 — maho-webui + website lockfile bumps (one commit)
- [ ] **D.7: Commit `maho-webui` and `website` dep bumps.**
  - Stage `maho/maho-webui/{bun.lock,package.json}` + `website/{bun.lock,package.json}`.
  - Commit message: `chore(deps): bump maho-webui + website deps`
  - **Verification:** `cd maho/maho-webui && bun install --frozen-lockfile && bun run typecheck`; `cd website && bun install --frozen-lockfile && bun run build`.

### D.8 — FFI headers (one commit, per Q2 policy)
- [ ] **D.8: Apply Q2 policy and commit FFI header changes.** [HC3]
  - If Q2=(a) canonical: pick canonical, replace others with `#include`, commit.
  - If Q2=(b) independent: commit all three header diffs with explanatory comment in commit body.
  - If Q2=(c) cbindgen: add cbindgen build dep, regenerate, commit regenerated + Cargo.toml.
  - Commit message: `chore(ffi): <Q2-policy-statement>`
  - **Verification:** Compile each shell — `python3 maho-chromium/build/scripts/build_maho.py --skip-rust` succeeds; `cd maho/ios-shell && make build` succeeds; `cd maho/macos-shell && cargo build` (or equivalent) succeeds.

### D.9 — ios-shell Bridge restructure (one ATOMIC commit) [HC9, RISK5, RISK9]
- [ ] **D.9.1: appium-tests reference audit.** [RISK5]
  - Run: `grep -r "MahoBridge" maho/appium-tests/`
  - **Success:** No stale `MahoBridge` symbol references that would break post-rename.
- [ ] **D.9.2: project.yml + project.pbxproj verification.** [RISK9]
  - Inspect `maho/ios-shell/project.yml` — does it know about the new `Bridge/` group?
  - If yes: regenerate locally via XcodeGen, verify diff matches current `project.pbxproj`.
  - If no: update `project.yml` first, regenerate.
- [ ] **D.9.3 (ATOMIC commit): commit all ios-shell changes together.**
  - Stage:
    - DELETION: `maho/ios-shell/Sources/MahoBridge.swift`
    - NEW: `maho/ios-shell/Bridge/MahoBridge+{AI,Chat,Folders,Tabs}.swift`
    - `maho/ios-shell/Support/WebViewBridgeController.swift` (+523 LOC)
    - `maho/ios-shell/Support/RelayAuthStore.swift`
    - `maho/ios-shell/Sources/{ArchiveView,ContentBlockerView,SettingsView,Models/BrowserState,Views/{ShellTheme,UpdateBannerView}}.swift`
    - `maho/ios-shell/Support/MahoIcon.swift`, `maho/ios-shell/Models/CoreUpdate.swift`
    - `maho/ios-shell/Bridge/ChatSessionRegistry.swift`, `MahoBridge.swift`
    - `maho/ios-shell/include/maho_ffi.h` (if not already in D.8)
    - `maho/ios-shell/Maho.xcodeproj/project.pbxproj`, `maho/ios-shell/project.yml`
    - `maho/ios-shell/Makefile`
  - Commit message: `refactor(ios): split MahoBridge into Bridge/ extensions + WebViewBridgeController`
  - **Verification:** `cd maho/ios-shell && make build` exit 0; `make test` (appium dry-run) exit 0.

### D.10 — android-shell (one commit, per Q6)
- [ ] **D.10: Commit android-shell changes.**
  - Stage `maho/android-shell/{app/build.gradle.kts, app/src/main/java/dev/maho/browser/MahoApplication.kt, bridge/BridgeFolders.kt, support/VoiceSearchButton.kt, ui/theme/BrowserShellTheme.kt, ui/webview/{AIWebViewFragment.kt,WebViewBridge.kt}}`.
  - Commit message: `feat(android): BridgeFolders + VoiceSearchButton + theme adjustments`
  - **Verification:** `cd maho/android-shell && ./gradlew :app:compileDebugKotlin` succeeds.

### D.11 — Misc sidebar/favorites (one commit)
- [ ] **D.11: Commit any remaining hunks.**
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc` etc.
  - Commit message: `fix(sidebar): favorites grid view runtime adjustments`

---

## Verification Matrix (Serial, respects `.build.lock`) [HC6, RISK8]

| Wave | Command | Trigger |
|------|---------|---------|
| V1 | `cd maho && cargo test --workspace` | After D.1, D.2, D.3, D.4, D.5 |
| V2 | `python3 maho-chromium/build/scripts/build_maho.py --skip-rust` | After B.3 |
| V3 | `cd maho/ios-shell && make test` | After D.9 |
| V4 | `cd maho/android-shell && ./gradlew test` | After D.10 |
| V5 | `python3 maho-chromium/build/scripts/build_maho.py` (FULL, no `--skip-rust`) | Final |

Budget: ~4 serial `build_maho.py` cycles (~30 min incremental, ~2 h full). Schedule overnight if needed.

---

## Final Cleanup (Wave 5)

- [ ] **Pre-0.5 archival (deferred non-blocking).** Move stale plans (anything for already-shipped work or older than 2026-05-25) into `.omo/plans/archive/`.
  - Commit message: `chore(plans): archive stale plans (preserve history)`
- [ ] **Push.** `git push origin main` once all V1–V5 pass.

---

## Rollback Strategy

| Scenario | Action |
|----------|--------|
| Single commit broken | `git revert <SHA>` |
| Whole train broken | `git reset --hard backup/pre-integration-2026-06-25` |
| ios-shell rejected | Drop D.9 commit, restore `Sources/MahoBridge.swift` from `backup/` branch |
| C gates fail | Move work to `feature/maho-welcome-react`, leave main clean |
| Any FFI commit broken | `git revert` the D.8 commit; falls back to pre-Q2 state |

---

## Adversarial Provenance

Derived from hyperplan adversarial review (5 members, Round 1 + Round 2 cross-attack) and formalized by the `plan` agent.

- Hard Constraints surviving: 11
- Decisions converged: 9
- Risks with mitigations: 11
- Open Questions surfaced as user gates: 6

Surviving findings per role:
- skeptic: 4/6 (refined)
- validator: 7/8 (refined)
- researcher: 6/7 (evidence-verified)
- architect: 5/7 (refined)
- creative: 6/7 (informs Q1)

---

**Status:** Gates resolved (2026-06-25). Ready for Wave 0 execution.
