# Desktop Grounding Verification Summary

**Date:** 2026-08-28  
**Target:** `maho-platform` & `maho-cli` (Desktop Grounding: Screen Capture + OCR + AX Elements + Find-Text)  
**Environment:** macOS Darwin 25.6.0 (arm64, Apple M4 Max)  
**TCC States Observed:**
- Screen Recording (`kTCCServiceScreenCapture`): **Granted** (`CGPreflightScreenCaptureAccess` returned `true`, live capture succeeded at 3840x1600).
- Accessibility (`AXIsProcessTrusted`): **Granted** (`AXIsProcessTrusted` returned `true`, AX tree traversal succeeded on frontmost app PID 664).

---

## Verdict Table

| # | Criterion | Command | Result | Evidence File |
|---|---|---|---|---|
| 1 | Full Platform Unit Tests | `cargo test -p maho-platform` | **PASS** (49 passed, 0 failed, exit 0) | `01_cargo_test_maho_platform.txt` |
| 2 | Clippy Cleanliness | `cargo clippy -p maho-platform -- -D warnings` / `cargo clippy -p maho-cli -- -D warnings` | **PARTIAL / FAIL (CLI)** (`maho-platform`: PASS exit 0; `maho-cli`: FAIL exit 101 due to pre-existing violations) | `02_clippy.txt` |
| 3 | Cross-Compile Linux & Windows | `cargo check -p maho-platform --target x86_64-unknown-linux-gnu`<br>`cargo check -p maho-platform --target x86_64-pc-windows-gnu` | **PASS** (both rc 0, mac-gated deps isolated) | `03_cross_compile_linux.txt`<br>`03_cross_compile_windows.txt` |
| 4 | Real-Surface CLI Grounding | `target/debug/maho desktop capture` / `ocr` / `find-text` / `elements` | **PASS** (All exit 0, structured JSON emitted, real coordinates verified) | `04_cargo_build_maho_cli.txt`<br>`04_desktop_capture.txt`<br>`04_desktop_ocr.txt`<br>`04_desktop_find_text.txt`<br>`04_desktop_elements.txt` |
| 5 | Fixture Determinism | `cargo test -p maho-platform ocr`<br>`cargo test -p maho-platform find_text` | **PASS** (5/5 OCR tests passed, 5/5 find_text tests passed against `ocr_fixture.png`) | `05_fixture_tests.txt` |
| 6 | Safety Contract Enforcement | Spot-check `DesktopCommands` in `main.rs` + `target/debug/maho desktop click` | **PASS** (Grounding variants have no `--approve`; input commands strictly enforce typed approval denial) | `06_safety_contract.txt` |
| 7 | Privacy Policy Adherence | Live screen capture path in `$TMPDIR` + region-bounded OCR + query-only match output in find-text | **PASS** (Captures land in temp dir, region filters bound text exposure, find-text returns only matched tokens) | `07_privacy_check.txt` |

---

## Detailed Findings & Defects

### Defects & Code Inconsistencies Found

1. **Bare `maho desktop ocr` without `--output` fails with I/O error instead of capturing screen**
   - **Location:** `crates/maho-cli/src/platform.rs:168`
   - **Code:** `let path = output.unwrap_or_default();`
   - **Issue:** When no `--output` path is provided to `maho desktop ocr`, `output.unwrap_or_default()` returns an empty `PathBuf` (`""`). This causes `maho_platform::ocr::ocr_image` to attempt `std::fs::canonicalize("")`, which fails with `PlatformError::Io("No such file or directory (os error 2)")`. In contrast, `find_text` properly defaults to `capture_screen(None)` when no image file is specified.
   - **Recommendation:** If `output` is `None`, `DesktopCommands::Ocr` should either capture the screen via `maho_platform::capture::capture_screen(None)` and OCR that temp image, or document `--output` as required.

2. **Unused CLI arguments in `maho desktop elements` (`--role` and `--output` ignored)**
   - **Location:** `crates/maho-cli/src/platform.rs:173-176`
   - **Code:** `DesktopCommands::Elements { app, role: _, output: _ }`
   - **Issue:** `filter_by_role` was implemented in `maho_platform::ax_tree::filter_by_role`, and `--role` / `--output` are declared on the CLI subcommand, but the dispatch handler discards `role` and `output` using `_` without filtering or file output.

3. **Unused CLI argument in `maho desktop ocr` (`--full` ignored)**
   - **Location:** `crates/maho-cli/src/platform.rs:159`
   - **Code:** `DesktopCommands::Ocr { region, full: _, output }`
   - **Issue:** The `--full` flag accepted by Clap is ignored in the command handler.

4. **Non-macOS Cross-Compilation Warnings**
   - **Location:**
     - `crates/maho-platform/src/ax_tree.rs:3:20`: unused import `DEEP_LINK_ACCESSIBILITY` on non-mac targets.
     - `crates/maho-platform/src/desktop.rs:11:20`: unused import `DEEP_LINK_ACCESSIBILITY` on non-mac targets.
     - `crates/maho-platform/src/ocr.rs:10:15`: function `vision_bbox_to_pixels` is dead code on non-mac targets.

5. **`maho-cli` clippy errors (Pre-existing in codebase)**
   - **Location:** `crates/maho-cli/src/refactor.rs`, `crates/maho-cli/src/browser.rs`, `crates/maho-cli/src/browser_pipe.rs`, `crates/maho-cli/src/vault.rs`, `crates/maho-cli/src/workspace.rs`
   - **Issue:** Fails `-D warnings` due to disallowed methods (`Result::unwrap` in `serde_json::json!` expansion) and un-inlined format args. Note that `crates/maho-platform` and `crates/maho-cli/src/platform.rs` themselves have 0 warnings.

## Post-Verifier Wiring Fixes (lead, 2026-08-28)

Verifier defects D1-D4 fixed by lead after verification:
- D1: `desktop ocr` without --output now captures the live screen to a temp file (was: empty-path Io error)
- D2: `desktop elements` --role now filters via filter_by_role; --output persists result JSON
- D3: `desktop ocr` --full now controls text exposure: live-screen default redacts text (coords only, text_redacted=true per design §7.2); --full or explicit file source includes text
- D4: non-mac cross-target warnings fixed (cfg-gated imports in ax_tree.rs/desktop.rs; vision_bbox_to_pixels mac-gated); strict cross clippy now clean:
  - `cargo clippy -p maho-platform --target x86_64-unknown-linux-gnu -- -D warnings` rc 0
  - `cargo clippy -p maho-platform --target x86_64-pc-windows-gnu -- -D warnings` rc 0
Post-fix gates: cargo test -p maho-platform 49 passed; clippy platform -D warnings clean; real-surface re-verified (see 08_wiring_fixes.txt).
Pre-existing (out of grounding scope): `cargo clippy -p maho-cli` fails in maho-browser-mcp/src/client.rs (Aug 26 code, unwrap-in-json! + collapsible_if) — untouched by this feature.
