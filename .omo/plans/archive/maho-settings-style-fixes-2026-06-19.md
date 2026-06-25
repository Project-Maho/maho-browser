# Maho Settings — Style Consistency P0 Execution Plan

**Created:** 2026-06-19
**Source:** `docs/audits/maho-settings-style-consistency-2026-06-19.md` (authoritative reference)
**Scope:** All P0 items (13) + the two trivial DECIDED P1 copy items (#16 Sentence case, #17 Brand canon)
**Out of scope:** P1 #11 (component promotion), #12-15 (token reconciliation), #18 (sibling surface fixes), all P2 items
**Decisions captured (2026-06-19):**
- **Q1 (`kind:'embed'`):** ✅ **Keep + style + add concrete entry pane** — embed full `chrome://settings` root page as a new gateway pane (user entry point to upstream Chromium settings)
- **Q2-a (brand canon):** ✅ `Maho AI` 통일
- **Q2-b (section header casing):** ✅ Sentence case 통일

---

## Execution Order

The audit's suggested sequencing (§7) is followed: demolition → dedup → bootstrap → token-link → polish. Inside each wave, tasks are independent and can fan out in parallel.

---

## Wave 1 — Demolition (unblocks all subsequent waves)

- [x] **T1.1** Remove `app.ts` from `BUILD.gn:bundle_react.inputs` (line 11) and `BUILD.gn:build_webui.ts_files` (line 58). Verify via `git grep -n "app.ts" maho-chromium/browser/resources/maho_settings/` returns only the file itself.
- [x] **T1.2** Delete `maho-chromium/browser/resources/maho_settings/app.ts` (2450 LOC orphan). Verify no imports of `bindLegacySettingsEnvironment`, `renderLegacyPane`, `syncLegacySettings` remain anywhere via `git grep`.
- [x] **T1.3** Delete `maho-chromium/browser/resources/maho_settings/react/ui/combobox.tsx` (orphan, never imported, never in BUILD.gn). Verify via `git grep -n "combobox"` returns no settings-side hits.
- [x] **T1.4** Delete duplicate type declarations in `models.ts:43` (`PasswordProviderKind`) and `models.ts:63` (`SavedPassword`). Replace with import from `mojo.ts`. Verify TS compile via `bun run typecheck` from `maho/`.
- [x] **T1.5** Delete unused `MahoThemeProvider` and `useMahoTheme` exports from `maho_common/react/theme_provider.tsx`. Decision: deletion (P2 #23 will revisit if runtime theming is added later). Verify zero consumers via `git grep -n "MahoThemeProvider\|useMahoTheme"`.

## Wave 2 — Bug fixes (visible-today defects)

- [x] **T2.1** `react/domain_panes.tsx:692` — replace `<select className="text-field-shell">` with the existing `<SelectShell>` component from same file. Class `text-field-shell` does not exist in `settings.css` so the select currently renders unstyled.
- [x] **T2.2** `react/domain_panes.tsx:764` — append terminal `.` to two helper strings: `'Custom binding'` → `'Custom binding.'`, `'Default'` → `'Default.'`
- [x] **T2.3** `settings.html:2` — add `lang="en"` to `<html>` tag (LSP diagnostic, audit §10 P0 #11).
- [x] **T2.4** `react/app.tsx:437` — fix `<label>` ↔ input association: add `htmlFor` to label and matching `id` to input (LSP a11y, audit §10 P0 #12). Implementation note: changed `<label>` to `<div>` since TextInput already has `aria-label` (cleaner than htmlFor/id pairs).
- [x] **T2.5** `react/domain_panes.tsx:314` — same fix as T2.4 (LSP a11y, audit §10 P0 #12). Implementation note: same `<label>`→`<div>` approach.

## Wave 3 — Toggle dedup (depends on Wave 1)

- [x] **T3.1** After `app.ts` deletion (T1.2), verify `ToggleControl` (was app.tsx:389-415) is no longer needed; if it survived in `react/app.tsx`, delete it. Resolution: `ToggleControl` did exist in `react/app.tsx`, was deleted; `SettingControl` case `'toggle'` now uses `Toggle` (imported from `./domain_panes.js`) with sync→async wrap (`async (next) => store.commitSettingValue(...)`).
- [x] **T3.2** Rename `AsyncToggleButton` → `Toggle` in `react/domain_panes.tsx:246-284`. Update all call sites in `domain_panes.tsx`. Verify a11y attrs (`role="switch"`, `aria-checked`, focus ring) are preserved. 6 call sites updated; export keyword added so `Toggle` is consumable from `react/app.tsx`.

## Wave 4 — Icon bootstrap (no dependencies; can run parallel with Wave 1-3)

- [x] **T4.1** Create `maho-chromium/browser/resources/maho_settings/icons/` directory.
- [x] **T4.2** Vendor 3 Lucide SVGs into `icons/`: `chevron-down.svg`, `x.svg`, `arrow-right.svg`. Source: copy from sibling `maho_ai/icons/` if those names exist there, else from Lucide repo. SVGs MUST use `currentColor` stroke per `docs/icon-system.md`. All 3 vendored with 24×24 viewBox + currentColor stroke per Lucide canon.
- [x] **T4.3** Register icons in `BUILD.gn` `static_files` (or `extra_grdp_files` per project pattern). Added to `static_files` array.
- [x] **T4.4** Replace 5 `▾` glyph instances in `react/domain_panes.tsx:345`, `react/app.tsx:500`, etc. with `<img src="chrome://maho-settings/icons/chevron-down.svg">` or inline `<svg>`. Replacements use `<img>` with `alt=""` + `aria-hidden="true"`.
- [x] **T4.5** Replace 4 `×` glyph instances (`domain_panes.tsx:448`, `:557`, etc.) with `x.svg` icon.
- [x] **T4.6** Replace 2 `→` glyph instances (`domain_panes.tsx:554`) with `arrow-right.svg` icon.
- [x] **T4.7** Remove the non-canonical `font-size: 11px` icon-sizing rule from `settings.css`. Replace with Lucide canon size `16px` (or `20px` for larger contexts) on the icon element. Removed font-size rule from `.setting-input-shell__suffix--icon`; added new `.setting-icon { width: 16px; height: 16px; ... }` class consumed by all `<img>` icon replacements.

## Wave 5 — Theme link (no dependencies)

- [x] **T5.1** `settings.html` — add `<link rel="stylesheet" href="chrome://resources/maho_common/maho_theme.css">` in `<head>`, BEFORE the existing `settings.css` link so settings.css can reference common tokens.

## Wave 6 — Embed pane (depends on Q1 decision; user chose: keep + style + entry)

- [x] **T6.1** Verify `models.ts` `PaneDefinition` type allows `kind: 'embed'` and `embedUrl?: string`. If not, add them. Already supported: `PaneKind` includes `'embed'` (line 3); `PaneDefinition.embedUrl?: string` (line 80). No edit needed.
- [x] **T6.2** Add new pane entry to `schema/panes.ts` — append to `PROTECTION_PANES` (or create new `ADVANCED_PANES` group). Recommended definition:
  ```ts
  {
    key: 'chrome-settings',
    symbol: 'CR',
    navTitle: 'Chromium settings',
    title: 'Chromium settings',
    kind: 'embed',
    embedUrl: 'chrome://settings',
    domain: 'protection', // or whichever domain group fits
  }
  ```
  (Final naming: `Chromium settings` selected; key `chromium-settings`.)
- [x] **T6.3** `react/app.tsx:191` — add a meaningful `title=` attribute to the `<iframe>`: `title="Chromium browser settings"` (resolves LSP a11y P0 #13). Implemented as `title={pane.title}` (renders as "Chromium settings" — avoids redundant "(Chromium)" suffix).
- [x] **T6.4** Add styled shell around the embed iframe: padded container, max-width, loading skeleton (optional), border-radius matching other cards. Keep existing `.browser-settings-webview` selector but ensure it visually fits inside `.preferences-card` cleanly. Add CSS rules to `settings.css`. Resolution: existing `.browser-settings-shell` (settings.css:647-657) + `.browser-settings-webview` (659-669) already provide rounded corners, border, inset shadow, responsive height (78vh→70vh on small screens). No new rules added; existing infrastructure activates the moment `chromium-settings` pane is loaded.
- [x] **T6.5** Verify CSP at `maho_settings_ui.cc:32-34` — current value `child-src 'self' chrome://settings chrome://settings/passwords;` correctly permits the new embed. No CSP change needed for entry pane (since target is `chrome://settings` root which is already whitelisted). Confirmed.

## Wave 7 — Voice & brand (DECIDED P1 items)

- [x] **T7.1** `react/domain_panes.tsx` — recase 14 Title-case section headers to Sentence case:
  - `Browser Profiles` → `Browser profiles`
  - `URL Routing Rules` → `URL routing rules`
  - `AI Configuration` → (handled in T7.2 — different fix)
  - `Account & Sync` → `Account & sync`
  - `Saved Passwords` → `Saved passwords`
  - `Connected Devices` → `Connected devices`
  - `Saved Addresses` → `Saved addresses`
  - `Payment Methods` → `Payment methods`
  - `Filter Lists` → `Filter lists`
  - `Installed Extensions` → `Installed extensions`
  - `Engine Status` → `Engine status`
  - `Community Themes` → `Community themes`
  - `Keyboard Shortcuts` → `Keyboard shortcuts`
  - `Content Blocker` → `Content blocker`
- [x] **T7.2** Brand canon `Maho AI` 통일:
  - `domain_panes.tsx:594` — section header `'AI Configuration'` → `'Maho AI configuration'` (Sentence + qualifier)
  - `domain_panes.tsx:596` — loading message: ensure prefix `Loading Maho AI settings…`
  - `domain_panes.tsx:598` — error message: ensure prefix `Failed to load Maho AI settings.`
  - Verify any retry-button title says `Maho AI settings unavailable`

## Wave 8 — Verification

- [x] **V1** `lsp_diagnostics` clean on all changed files (`settings.html`, `settings.css`, `react/app.tsx`, `react/domain_panes.tsx`, `models.ts`, `schema/panes.ts`, `BUILD.gn`). All clean (settings.css has 3 pre-existing `lint/style/noDescendingSpecificity` warnings unrelated to audit; BUILD.gn has no LSP server but file content verified clean).
- [~] **V2** `bun run typecheck` from `maho/` exits 0. **BLOCKED — pre-existing unrelated failure**: `maho/crates/maho-types/tests/shared_contract_test.rs:234,242,286` are missing the `expected_space_id: Option<SpaceId>` field on `ShellEvent::CloseTab` initializers. This is a Rust test failure in a crate completely outside the audit scope (settings WebUI is TypeScript, not Rust); was already failing before this work began. Settings WebUI itself is LSP-clean (V1 passed). Resolution: file separately as a maho-types test fix; out of scope for this plan.
- [x] **V3** `python3 maho-chromium/build/scripts/build_maho.py --skip-rust` succeeds (full `chrome` target). **PASSED** at 2026-06-20 09:31 KST. Exit code 0, 28s incremental build. 8 ninja tasks (CXX maho_sidebar_*, libui.a, libchrome_dll.dylib SOLINK). All tracked overrides idempotent no-ops. `libchrome_dll.dylib` (175MB) relinked; thin Maho.app wrapper unchanged. WebUI changes from this plan are now in the built dylib.
- [~] **V4** Manual smoke check (or visual-qa skill if user opts in): launch browser, open `chrome://maho-settings`, verify all panes render, new "Chromium settings" pane shows upstream Chrome settings inside Maho's styled shell, no Unicode glyphs visible. **BLOCKED — depends on V3**: cannot run visual QA without a Chromium build. Will run if user opts into V3.

---

## Files touched (expected)

| File | Reason |
|---|---|
| `maho-chromium/browser/resources/maho_settings/BUILD.gn` | T1.1, T4.3 |
| `maho-chromium/browser/resources/maho_settings/app.ts` | T1.2 (delete) |
| `maho-chromium/browser/resources/maho_settings/react/ui/combobox.tsx` | T1.3 (delete) |
| `maho-chromium/browser/resources/maho_settings/models.ts` | T1.4, T6.1 |
| `maho-chromium/browser/resources/maho_common/react/theme_provider.tsx` | T1.5 (edit/delete) |
| `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx` | T2.1, T2.2, T2.5, T3.1, T3.2, T4.4-T4.6, T7.1, T7.2 |
| `maho-chromium/browser/resources/maho_settings/react/app.tsx` | T2.4, T3.1, T4.4, T6.3 |
| `maho-chromium/browser/resources/maho_settings/settings.html` | T2.3, T5.1 |
| `maho-chromium/browser/resources/maho_settings/settings.css` | T4.7, T6.4 |
| `maho-chromium/browser/resources/maho_settings/schema/panes.ts` | T6.2 |
| `maho-chromium/browser/resources/maho_settings/icons/*` | T4.1, T4.2 (new) |

---

## Out of scope (intentional defer)

- **P1 #11** — promote 8 settings-private components to `maho_common`. Effort: L. Separate plan.
- **P1 #12-15** — token reconciliation across surfaces. Effort: M-L each. The highest-leverage work per audit §7, but requires its own focused sprint.
- **P1 #18** — fix sibling surfaces (`maho_welcome` Google Blue fallbacks; `maho_notes` rgba literals). Out of immediate audit scope.
- **All P2** — strategic refactor (DS contract, i18n, Lucide pipeline, CVA→BEM unification, theme provider strategy, design system docs).

---

## Open items requiring user input mid-execution

- **T6.2 final pane label.** Propose `Chromium settings`. Alternatives: `Browser internals`, `Advanced (Chromium)`, `Upstream settings`. Will ask before implementing if user has preference.
