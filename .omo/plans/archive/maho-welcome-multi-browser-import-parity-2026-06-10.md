# Maho Welcome — Multi-browser Import Parity Remediation Plan

**Date**: 2026-06-10
**Goal**: Make the onboarding wizard's Import page actually deliver real, working multi-browser import that matches Zen/Arc behavior. End-to-end: detection → click → bookmarks land in `BookmarkModel`.

---

## 1. Current State Audit (verified)

### What works
- Wizard reaches the Import page on first run.
- Mojo plumbing for `getAvailableBrowsers` / `startImport` / `OnImportProgress` is in place.
- Maho-native importer module exists at `maho-chromium/browser/importer/` (detector + Chromium JSON parser + Firefox/Zen places.sqlite parser + orchestrator).
- Build passes (`build_maho.py --skip-rust` exits 0).
- Safari card appears on the Import page (FDA-required marker is honored — clicking it shows the FDA message).

### What does not work (verified at runtime)
1. **Zen is not detected** even though `~/Library/Application Support/zen/profiles.ini` exists and `Profiles/8ubsu3zz.Default (release)/places.sqlite` is on disk.
   - Root cause: `ResolveDefaultGeckoProfile` only checks `[Profile*] Default=1` but Zen's `profiles.ini` marks Profile1 as Default=1 (which has no `places.sqlite`); the truly-active profile is recorded under `[Install*] Default=<path>` which the parser ignores.
2. **Arc is not detected** because the user has never bookmarked anything in Arc; `~/Library/Application Support/Arc/User Data/Default/Bookmarks` does not exist. Detector skips browsers whose Bookmarks file is missing.
3. **End-to-end import path was never verified.** The "Import complete" toast is just a UI message — there is zero evidence that Maho's `BookmarkModel` actually receives any imported bookmarks. `chrome://bookmarks` was never opened to confirm.
4. **Safari TCC path is untested.** Safari card shows up but clicking import always returns the FDA message. Whether the message is the correct UX (vs. attempting and gracefully failing) is unverified.

### Unknowns
- Whether `BookmarkModelFactory::GetForBrowserContext(profile)->loaded()` returns true at the moment of import in app-mode windows (welcome runs in an app-mode browser, not a normal browser).
- Whether the orchestrator's `BookmarkModel::AddURL/AddFolder` calls actually persist (could be writing to a transient model).
- Whether `PostTaskAndReplyWithResult` weak-ptr capture on `MahoBookmarkImporter` survives the page close (impacts whether import completes if user clicks Next mid-import).

---

## 2. Acceptance Criteria (binary, observable)

1. On first run with Zen installed, the Import page shows a "Zen" card with at least the Bookmarks checkbox enabled.
2. On first run with Arc installed (even without bookmarks), the Import page shows an "Arc" card. If no bookmarks data exists, the card is visible but the Import button is disabled with a clear status ("Nothing to import").
3. Clicking Import on the Zen card causes the actual Zen bookmark tree (read from `places.sqlite`) to be added to Maho's `BookmarkModel`. After completion, `chrome://bookmarks/` shows an "Imported from Zen" folder containing the parsed bookmark hierarchy.
4. Same end-to-end check passes for Chrome/Brave/Edge/Vivaldi/Opera (any installed with bookmarks).
5. Safari card behavior: clicking Import shows the FDA-required message and does not attempt to read the protected file.
6. Welcome window does not crash if user clicks Next mid-import (graceful cancel).
7. No new browser windows open during import (everything stays inside the app-mode welcome window).

---

## 3. Implementation Tasks (ordered, each with a verification step)

### Task A — Fix Gecko profile detection (Zen, Firefox)
**File**: `maho-chromium/browser/importer/maho_browser_detector.cc`
**Change**: Rewrite `ResolveDefaultGeckoProfile` so it
- Scans every `[Install*]` section and records each `Default=<path>` value as an "install-active" candidate (relative to the browser data dir).
- Scans every `[Profile*]` section, recording each profile path and whether the section has `Default=1`.
- Returns the first candidate whose `places.sqlite` exists, in priority order: install-active candidates → profile-default candidates → all profile candidates.

**Verification**: Build, run with fresh profile, click splash, confirm Zen card appears in the DOM via remote-debugging snapshot. Status: pending fix that was started in the prior turn but aborted by the user.

### Task B — Show installed-but-empty Chromium browsers
**File**: `maho-chromium/browser/importer/maho_browser_detector.cc`
**Change**: When the per-browser `Default/` directory exists but the `Bookmarks` file does not, still emit a `DetectedBrowser` with `services_supported = 0`. This makes the card visible but renders the Import button disabled (frontend already handles `services_supported == 0` correctly).
**Caveat**: Need to confirm with the user whether they prefer hiding empty browsers or showing them disabled. Current plan defaults to "show disabled" because user explicitly asked to see Arc.

**Verification**: Arc card appears in DOM with all checkboxes disabled and Import button disabled (no bookmarks data on this host). Visible to the user.

### Task C — Verify orchestrator actually writes to the live `BookmarkModel`
**Files**: `maho_bookmark_importer.cc` (existing), `maho_welcome_page_handler.cc` (existing)
**Change**: Inspect the call chain. Confirm that
- `BookmarkModelFactory::GetForBrowserContext(profile)` returns a non-null model in the welcome page's profile context.
- `model->loaded()` is true when our reply runs (welcome happens after profile init; should be loaded, but verify).
- If the model is not loaded yet, register a one-shot observer that performs the write when `BookmarkModelLoaded` fires, instead of bailing with the "Bookmark storage not ready" message.

**Verification**: Open `chrome://bookmarks/` in a separate tab after import; the imported folder must appear with the right children. If the model is unloaded at write time, the test fails and we need the observer fallback.

### Task D — End-to-end QA: Zen import
1. Build (`build_maho.py --skip-rust`).
2. Launch with a clean `--user-data-dir=/tmp/maho-zen-import-qa`.
3. Click splash arrow.
4. Take screenshot, confirm Zen card visible.
5. Click Import on Zen card.
6. Wait for "Import complete" status.
7. Open new tab → `chrome://bookmarks/`.
8. Confirm "Imported from Zen" folder exists with non-zero children.
9. Spot-check a couple of bookmark URLs match what Zen has.

**Pass condition**: Folder exists + at least one URL is correct.
**Fail condition**: Folder missing, empty folder, or stale URLs. If fail, root-cause and iterate before moving on.

### Task E — End-to-end QA: Arc behavior
1. Same flow as D up to step 4.
2. Confirm Arc card visible (no bookmarks). Status: Import button disabled, message "No bookmarks to import" or similar.
3. Confirm clicking the disabled button does nothing.

**Pass condition**: Card visible, button disabled, no crash. (Real Arc bookmark import is untestable on this host because the user has none. We'll defer that QA until a host with Arc bookmarks is available — but the code path is the same as Chrome/Brave/etc., so unit-test the parser separately if needed.)

### Task F — Safari FDA path verification
1. Click Import on Safari card.
2. Confirm status text is "Grant Full Disk Access in System Settings to import Safari." or equivalent.
3. Confirm no file read attempt was made (no crash, no error in stderr).

**Pass condition**: Specific FDA message + clean stderr.

### Task G — Mid-import cancel safety
1. Trigger import on Zen.
2. Immediately click Next (advance the wizard) before the import completes.
3. Confirm: no crash, no use-after-free, the welcome window proceeds normally.

**Pass condition**: Maho stays alive and responsive.

### Task H — Final sweep
- Run `bun run typecheck && bun run test` from `maho-chromium/browser/resources/maho_welcome/`.
- Run the full build once more to confirm no regression.
- Read every changed file end to end as a final review.

---

## 4. Verification Plan (tooling)

- **DOM inspection** via remote-debugging WS to `chrome://maho-welcome/`: query `.welcome-import-card` names, `services_supported` mask, button states.
- **Bookmark inspection** via second tab navigation to `chrome://bookmarks/`, executing `bookmarks.getTree()` through the chrome.bookmarks DevTools extension API or via DevTools's bookmark sidebar.
- **Stderr log** at `/tmp/maho-stderr.log` for runtime errors.
- **Screenshot** for each step using `screencapture` to provide visual evidence.

Each task ends with explicit evidence (screenshot or DOM JSON dump). No task is marked done without that evidence.

---

## 5. Risks and Open Questions

1. `chrome://bookmarks/` may itself be a separate WebUI that requires its own routing in our overlay. If it's broken in our build, we need an alternative way to inspect the live `BookmarkModel` (e.g., DevTools `chrome.bookmarks` API in another extension page, or LSP-driven C++ test).
2. The welcome browser is app-mode. The page handler creates the importer with the welcome browser's profile. If the imported bookmarks land in a profile that the user's "real" Maho window cannot see, the import is invisible. Need to confirm the welcome browser shares the default profile with regular browsing.
3. Mid-import cancel: `WeakPtr` capture should handle handler destruction cleanly, but the `BookmarkModel` is owned by the profile (long-lived), so writes can race with profile teardown only at app exit. Likely safe but worth a quick stress test.
4. macOS sandbox: dev build is unsandboxed but ad-hoc signed with no entitlements. `~/Library/Application Support/...` reads work without entitlements. Only `~/Library/Safari` requires FDA. No other paths in our detector touch protected dirs.

---

## 6. Out of Scope

- History import (only bookmarks for v1; history is a follow-up).
- Password import (requires Keychain access; deferred entirely).
- Safari import via FDA grant; we ship the message, not the actual import.
- Adding new Chromium-based browsers beyond the listed seven (Chrome, Arc, Brave, Edge, Vivaldi, Opera, plus Firefox/Zen).
- Production code-signing entitlements (`com.apple.security.files.all`) — separate release-build concern.

---

## 7. Definition of Done

All seven tasks (A–H) finish with their pass condition met, screenshots captured, no skipped step. The PR description for the eventual commit includes the screenshots and the bookmarks dump for at least Zen, plus a short note on Arc/Safari behavior on this host.

If any task fails, do not declare done. Iterate.
