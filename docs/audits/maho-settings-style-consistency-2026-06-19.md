# Maho-Chromium Settings Panel — Style Consistency Audit

**Date:** 2026-06-19
**Scope:** `maho-chromium/browser/resources/maho_settings/` (sole live settings surface, registered at `chrome://maho-settings`)
**Method:** 6 parallel `explore` agents (W1) → consolidated evidence → `oracle` synthesis (W4)
**Author:** Sisyphus (ulw mode, hai/anthropic--claude-4.7-opus)
**Decisions captured (user, 2026-06-19):**
- **Q2-a (brand canon):** ✅ `Maho AI` 통일
- **Q2-b (section header casing):** ✅ Sentence case 통일
- **Q1 (`kind: 'embed'` 전략):** ⏸️ **결정 보류** (Oracle 권장: 삭제) — see §6

---

## 1. Executive Verdict

**This is a collection of bespoke surfaces with a primitive-passthrough veneer, not a coherent design system.**

**Score: 4 / 10.**

The settings panel is the most disciplined surface in the WebUI fleet — it has a declarative schema track for 6/13 panes, an internal `--maho-settings-*` token namespace, and broadly consistent BEM. But the patterns of intent are real while the contracts that would enforce them are absent. Five structural deficits cap the ceiling: (a) an unfinished vanilla→React migration, (b) numerical token drift already present across surfaces, (c) zero Lucide icon canon adherence, (d) no i18n infrastructure, (e) eight settings-private components that belong in `maho_common`.

---

## 2. Audit Architecture (what was checked)

| Surface | Path | Status |
|---|---|---|
| **Primary target** | `maho-chromium/browser/resources/maho_settings/` | Audited (full) |
| Shared library | `maho-chromium/browser/resources/maho_common/` | Audited (reference) |
| Sibling surfaces (consistency baseline) | `maho_ai/`, `maho_notes/`, `maho_welcome/`, `maho_easel/`, `maho_boost/`, `maho_space_config/`, `maho_space_create/` | Audited (cross-reference) |
| C++ glue | `maho-chromium/browser/ui/webui/maho_settings/`, `maho-chromium/browser/ui/maho_settings_navigation.{cc,h}` | Audited |
| Mojom IDL | `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` | Audited |
| Upstream chromium_src settings overrides | `chromium_src/chrome/browser/{resources,ui/webui}/settings/` | **Confirmed: do not exist.** Maho has not overridden any upstream Chrome settings resources. Audit scope did NOT need to expand. |

**13 active panes audited:** General, Tabs, Privacy, Notifications, Advanced, Appearance, Theme Store, Profiles, Account & Sync, Passwords, Autofill, Shortcuts, Maho AI, Content Blocker, Air Traffic Control, Extensions.

---

## 3. Five Root Causes (not symptom list)

| # | Root cause | Origin symptoms |
|---|---|---|
| **R1** | **Dual implementation tracks were never reconciled.** Vanilla `app.ts` was the original; `react/` is the live replacement; the migration finished in source but not in `BUILD.gn:ts_files`. | Orphaned 2450-LOC duplicate. String drift between tracks (`…` literal vs `\u2026` escape). `kind:'embed'` rendering path mirrored in both. `ToggleControl` (app.tsx:389-415) ↔ `AsyncToggleButton` (domain_panes.tsx:246-284) duplicated for the same DOM. |
| **R2** | **No design-token contract layer between `maho_common` and surfaces.** Each surface defines its own private namespace; no canonical semantic mapping. | `--maho-settings-space-7=32px` vs `--maho-ai-space-7=28px`. settings.html doesn't link `maho_theme.css` (siblings notes/welcome/space_config do). Hardcoded colors and the 18px radius leaking outside token blocks. welcome's 14 undefined `--color-sys-*` falling back to Google Blue `#1a73e8`. notes' 12 hardcoded `rgba(233,69,96,…)`. |
| **R3** | **`maho_common` is a primitive library, not a design system.** It exposes 4 unstyled passthrough primitives + an unused `MahoThemeProvider`. No compound components, no token export, no icon registry, no voice/i18n contract. | 8 settings-private components with no promotion path: `SelectShell`, `ToggleControl`, `SegmentedControl`, `ManagedSettingRow`, `SectionHeader`, `SliderControl`, `ColorSwatchControl`, `TextFieldShell`. `maho_ai` diverging into CVA+clsx authoring model. `theme_provider.tsx` with 0 consumers. |
| **R4** | **No icon ingestion pipeline; the Lucide canon (`docs/icon-system.md`) is documented but unenforced.** | No `maho_settings/icons/` directory. 11 raw Unicode glyphs (`▾ × 5`, `× × 4`, `→ × 2`) substituting for icons. Non-canonical `font-size: 11px` sizing rule (Lucide canon: 16/20/24px). Sibling `maho_ai/icons/` and `maho_boost/icons/` already comply — settings is the sole non-compliant surface. |
| **R5** | **Strings are first-class code, not data.** No `loadTimeData`, no GRD/GRDP, no JSON catalog. | Brand drift `Maho AI` (nav) vs `AI Configuration` (in-pane header). 14 Title-case bespoke headers vs 6 Sentence-case schema headers. Period-trailing inconsistency at domain_panes.tsx:764. Korean/Japanese localization blocked entirely. |

---

## 4. Evidence Inventory

### 4.1 Architecture / dead code

| Finding | Evidence |
|---|---|
| Orphaned `app.ts` (2450 LOC vanilla DOM) | `maho-chromium/browser/resources/maho_settings/app.ts` (entire file); `BUILD.gn:ts_files` still lists it; zero React imports of its exported functions (`bindLegacySettingsEnvironment`, `renderLegacyPane`, `syncLegacySettings`) |
| Strings already drifting between tracks | `app.ts:337` uses `\u2026` escape; `domain_panes.tsx:372` uses literal `…` — same string, different encodings |
| Orphan `combobox.tsx` | `react/ui/combobox.tsx` not in `BUILD.gn:ts_files`; zero imports; classes `setting-combobox-trigger`, `setting-combobox-panel`, `setting-combobox-command` etc. have NO matching CSS rules in `settings.css` |
| Mojom-shadowing types | `models.ts:43` (`PasswordProviderKind`) and `models.ts:63` (`SavedPassword`) re-declared as local TS interfaces, shadowing types that mojo.ts already re-exports from `maho_settings.mojom-webui.js` |
| Unused `MahoThemeProvider` | `maho_common/react/theme_provider.tsx` exports `MahoThemeProvider` and `useMahoTheme` — 0 consumers project-wide |
| **Latent embed disaster (P0 #2)** | `react/app.tsx:180-204` (`<EmbedPane>`) and `app.ts:242-274` (`createEmbedPane()`) render `chrome://settings/` in `<iframe class="browser-settings-webview">` for any pane with `kind: 'embed'`. Currently no pane uses it. CSP at `maho_settings_ui.cc:32-34` whitelists `chrome://settings` and `chrome://settings/passwords` for `child-src`. Activation = immediate visual-consistency failure. |

### 4.2 Concrete bugs (visible today)

| Bug | File:Line | Effect |
|---|---|---|
| Undefined CSS class on `<select>` | `react/domain_panes.tsx:692` | `<select className="text-field-shell">` — class does not exist in `settings.css` → renders unstyled |
| `setting-combobox-*` classes have no rules | `react/ui/combobox.tsx` (entire) + `settings.css` (search confirms no rules) | If ever wired up, combobox renders completely unstyled |
| 2 description strings missing terminal periods | `react/domain_panes.tsx:764` (`'Custom binding'`, `'Default'`) | Inconsistent with the other 35+ helper texts in the panel |

### 4.3 Design tokens — drift evidence

**Spacing scale numerical drift across surfaces:**

| Step | settings (`--maho-settings-space-N`) | maho_ai (`--maho-ai-space-N`) | common (`--maho-space-*`) |
|---|---|---|---|
| 1 | 4px | 4px | xs: 4px |
| 2 | 8px | 8px | sm: 8px |
| 3 | 12px | 12px | (none) |
| 4 | 16px | 16px | md: 16px |
| 5 | 20px | 20px | (none) |
| 6 | 24px | 24px | lg: 24px |
| 7 | **32px** | **28px** ⚠️ | xl: 32px |
| 8 | **40px** | **32px** ⚠️ | (none) |

**`settings.html` does NOT link `maho_common/maho_theme.css`** despite siblings `notes`/`welcome`/`space_config` doing so via `<link rel="stylesheet" href="chrome://resources/maho_common/maho_theme.css">`.

**15 raw font-size literals in `settings.css`:** 10/12/12.5/13/14/15/16/18/31 px — no font-size token scale exists in settings.

**Hardcoded color literals OUTSIDE the token blocks** (in `settings.css` component rules, not in `:root` / `@media (prefers-color-scheme)`): `#9bb2ff` (dark accent), `#4a6aff` (light accent), `rgba(255,255,255,0.1)`, `rgba(155,178,255,0.11)`, `rgba(116,213,156,0.26)`, `#151a21`, `#f5f6fa`, `#0a0c12`.

**Defined-but-unused token:** `--maho-settings-radius-md: 18px` is declared but `.content-card` uses raw `18px` literal.

**Cross-surface latent bugs (out of immediate audit scope but flagged):**
- `maho_welcome/welcome.css` references 14 undefined `--color-sys-*` tokens with hardcoded fallbacks (`#1a73e8` Google Blue) — clashes with Maho's `--maho-primary: #e94560`
- `maho_notes/notes.css` hardcodes `rgba(233,69,96,…)` 12 times instead of `var(--maho-primary)`

### 4.4 Component reuse rate ~55%

**maho_common consumed by settings:** `Button`, `TextInput`, `TextArea`, `joinClassNames`, `notifyListeners` / `Listener` / `asErrorMessage`, `bindMojoPageHandler`.

**Settings-private re-implementations (NOT promoted to maho_common):**

| Component | File:Line | Notes |
|---|---|---|
| `ToggleControl` | `app.tsx:389-415` | Track + thumb DOM, role="switch" |
| `AsyncToggleButton` | `domain_panes.tsx:246-284` | **Duplicate** of `ToggleControl` with async wrapper |
| `SelectShell` | `domain_panes.tsx:327-348` | `.setting-input-shell--select` + chevron suffix |
| `TextFieldShell` | `domain_panes.tsx:305-325` | `.setting-input-shell` wrapper |
| `ActionButton` | `domain_panes.tsx:210-244` | Async-aware button with busy state |
| `ManagedSettingRow` | `domain_panes.tsx:286-303` | Core row layout |
| `SectionHeader` | `domain_panes.tsx:196-208` | Card header w/ optional action |
| `SegmentedControl` | `app.tsx:505-546` | Tablist segments |
| `SliderControl` | `app.tsx:582-638` | `<input type="range">` + debounce |
| `ColorSwatchControl` | `app.tsx:548-580` | Color swatch grid |

**No `cr_elements` (cr-button, cr-toggle, cr-input, cr-link-row) are used anywhere in the entire Maho WebUI fleet.** Maho has fully seceded from Chromium's WebUI design system. This makes a Maho-internal design system **mandatory**, not optional.

**Class-authoring model split:** settings/notes/welcome/space_create use **prefix-BEM with `.is-*` state modifiers** + `joinClassNames`. `maho_ai` uses **CVA + clsx** (`react/lib/cn.ts`). One-off divergence.

### 4.5 Icons — total Lucide canon non-compliance

`maho_settings/icons/` directory does **not** exist. Zero `<svg>`, zero `<img>` icon refs.

**11 raw Unicode glyphs across React + legacy tracks:**

| Glyph | Codepoint | Role | Refs |
|---|---|---|---|
| `▾` | U+25BE | Dropdown chevron | `app.tsx:500`, `domain_panes.tsx:345`, `combobox.tsx:34`, `app.ts:1129`, `app.ts:2363` |
| `×` | U+00D7 | Close / delete button label | `domain_panes.tsx:448`, `domain_panes.tsx:557`, `app.ts:431`, `app.ts:540` |
| `→` | U+2192 | Data-display arrow ("source → target") | `domain_panes.tsx:554`, `app.ts:506` |

Sibling features `maho_ai/icons/` (15 SVGs) and `maho_boost/icons/` (11 SVGs) already comply. Settings is the only non-compliant surface.

### 4.6 Copy / voice

**Section header casing split (decision: Sentence case 통일):**

| Track | Convention | Count | Example |
|---|---|---|---|
| `react/domain_panes.tsx` (bespoke panes) | Title Case | 14 | `Browser Profiles`, `URL Routing Rules`, `AI Configuration`, `Account & Sync`, `Saved Passwords`, `Connected Devices`, `Saved Addresses`, `Payment Methods`, `Filter Lists`, `Installed Extensions`, `Engine Status`, `Community Themes`, `Keyboard Shortcuts`, `Content Blocker` |
| `schema/panes.ts` LIVE_PANE_SECTION_MANIFEST | Sentence case | 6 | `Daily browsing defaults`, `Browser chrome`, `Safety & tracking`, `Performance & browser behavior`, `Notification prompts`, `Tab lifecycle` |

**Brand drift (decision: `Maho AI` 통일):**
- `panes.ts:195` `navTitle: 'Maho AI'`
- `panes.ts:199` `title: 'Maho AI'`
- `domain_panes.tsx:594` section header `'AI Configuration'` ← drops "Maho"
- `domain_panes.tsx:596,598` loading/error messages `'Loading AI settings…'`, `'Failed to load AI settings.'` ← drops "Maho"
- `app.ts:553,560` legacy duplicates with same drift

**Punctuation:** 35+ helper texts consistently end in `.`. Only 2 violations: `domain_panes.tsx:764` (`'Custom binding'`, `'Default'`).

**Toggle label casing:** 18 Sentence : 3 Title (Title used only for proper nouns like `'Show Home button'`, `'Send "Do Not Track"'`, `'Safe Browsing'`). Broadly consistent.

**Terminology audit (no drift found):**
- "Email" used consistently (no "E-mail")
- "Notification" / "Notifications" used consistently (no "Alert")
- "Tab" / "Window" used for distinct concepts (no overlap)
- "Profile" / "Account" refer to distinct features (Identity domain group)
- "Spaces" / "space" used in both contexts (`Spaces:` label prefix vs `Apply to space` sentence)

---

## 5. Prioritized Fix List

### P0 — Next sprint (visible bugs / dead-code traps / latent disasters)

| # | Issue | File:line refs | Fix | Effort |
|---|---|---|---|---|
| 1 | Orphaned `app.ts` (2450 LOC) still in build | `maho-chromium/browser/resources/maho_settings/app.ts`; `BUILD.gn:ts_files` | Delete `app.ts`; remove from `ts_files`; verify `app_bundle.js` regenerates | M |
| 2 | Latent `kind:'embed'` path drops unstyled `chrome://settings` into iframe | `react/app.tsx:180-204`; `app.ts:242-274`; CSP at `maho_settings_ui.cc:32-34` | **DECISION PENDING (see §6)** — if delete: remove embed branch + CSP entries; if keep: build styled shell | S (delete) / L (style) |
| 3 | Unstyled `<select className="text-field-shell">` (class undefined) | `react/domain_panes.tsx:692` | Replace with existing `SelectShell` | S |
| 4 | Orphan `combobox.tsx` excluded from build, never imported | `react/ui/combobox.tsx` | Delete the file | S |
| 5 | Duplicate type declarations shadow Mojo-generated types | `models.ts:43,63` | Delete local re-declarations; import from `mojo.ts` | S |
| 6 | Missing terminal periods on 2 helper texts | `react/domain_panes.tsx:764` | Add `.` to `'Custom binding'` and `'Default'` | S |
| 7 | `MahoThemeProvider` / `useMahoTheme` exported with 0 consumers | `maho_common/react/theme_provider.tsx` | Delete (or wire — see P2 #23) | S |
| 8 | Duplicate Toggle implementations | `app.tsx:389-415`, `domain_panes.tsx:246-284` | After P0 #1, keep `AsyncToggleButton`, rename to `Toggle`; same component handles sync via no-op promise | M |
| 9 | 11 raw Unicode glyphs as icons; no `/icons/` directory | All Unicode glyph refs in §4.5 | Create `maho_settings/icons/`; vendor Lucide `chevron-down.svg`, `x.svg`, `arrow-right.svg`; render via `<img>` or inline `<svg>`; remove `font-size: 11px` icon rule | M |
| 10 | `settings.html` doesn't load shared theme CSS (siblings do) | `settings.html` | Add `<link rel="stylesheet" href="chrome://resources/maho_common/maho_theme.css">` | S |

### P1 — This quarter (systemic drift)

| # | Issue | File:line refs | Fix | Effort |
|---|---|---|---|---|
| 11 | 8 settings-private components never promoted to `maho_common` | `domain_panes.tsx` (`SelectShell`, `SegmentedControl`, `ManagedSettingRow`, `SectionHeader`, `SliderControl`, `ColorSwatchControl`, `TextFieldShell`, `ActionButton`) | Move each to `maho_common/react/`; settings imports them; siblings inherit | L |
| 12 | Numerical token drift across surfaces | `settings.css:1-25`; `maho_ai/styles/tokens.css`; `maho_common/maho_theme.css` | Pick canonical scale (recommend common as authority); rewrite settings + ai to consume common; delete duplicate definitions | M |
| 13 | 15 raw font-size literals; no font-size token in `--maho-settings-*` | `settings.css` (10/12/12.5/13/14/15/16/18/31px) | Define `--maho-settings-text-{xs..3xl}` mapped to a 7-step scale; replace literals | M |
| 14 | Hardcoded colors leaked outside token blocks | `settings.css` component rules listed in §4.3 | Move all into the two `@media (prefers-color-scheme)` token blocks; reference via `var(--maho-settings-*)` only | M |
| 15 | `.content-card` uses raw `18px` despite token existing | `settings.css` `.content-card` rule | Replace with `var(--maho-settings-radius-md)` | S |
| 16 | **Section header casing split → Sentence case 통일** ✅ DECIDED | `react/domain_panes.tsx` (14 Title-case headers) | Recase all 14 to Sentence: `Browser profiles`, `URL routing rules`, `AI configuration`, `Account & sync`, `Saved passwords`, `Connected devices`, `Saved addresses`, `Payment methods`, `Filter lists`, `Installed extensions`, `Engine status`, `Community themes`, `Keyboard shortcuts`, `Content blocker` | M |
| 17 | **Brand drift `Maho AI` (nav) ↔ `AI Configuration` (in-pane) → `Maho AI` 통일** ✅ DECIDED | `domain_panes.tsx:594` AI pane header; `:596,598` loading/error messages; `app.ts:553,560` legacy duplicates | Recase header to `Maho AI configuration` (Sentence + qualifier). Loading: `Loading Maho AI settings…`. Error: `Failed to load Maho AI settings.` Retry title: `Maho AI settings unavailable` | S |
| 18 | Sibling surfaces hardcode brand color and use undefined tokens | `maho_welcome/welcome.css` (14× `--color-sys-*` Google Blue fallbacks); `maho_notes/notes.css` (12× hardcoded `rgba(233,69,96,…)`) | Replace `--color-sys-*` with Maho tokens; replace rgba literals with `var(--maho-primary)` derivations | M |

### P2 — Backlog (polish / strategic refactor)

| # | Issue | Effort |
|---|---|---|
| 19 | Build shared design-token contract layer (expand `maho_common/maho_theme.css`; per-surface namespaces become aliases only) | XL |
| 20 | i18n infrastructure (GRD/GRDP catalog + `loadTimeData` wiring + extract every hardcoded string) | XL |
| 21 | Canonical Lucide ingestion pipeline (vendoring + dedup + auto-generated TS index) | L |
| 22 | Class-authoring model unification (migrate `maho_ai` from CVA+clsx → BEM+`joinClassNames`) | L |
| 23 | Decide `MahoThemeProvider` strategy (deletion in P0 #7 vs runtime light/dark + accent override design) | M |
| 24 | Document Maho design-system contract in `maho_common/AGENTS.md` | M |

---

## 6. Open Decision

### Q1. `kind: 'embed'` 전략 — **결정 보류 (Oracle 권장: 삭제)**

**컨텍스트.** `schema/panes.ts`의 어떤 pane도 현재 `kind: 'embed'`를 사용하지 않음. 그러나 코드(`app.ts:242-274`, `react/app.tsx:180-204`)와 CSP(`maho_settings_ui.cc:32-34`)는 활성화 한 줄로 `chrome://settings` 업스트림 페이지를 iframe에 그대로 띄울 준비가 되어 있음. 활성화되는 순간 Maho 디자인 0%, 구글 디자인 100%의 페이지가 Maho 설정창 안에 노출됨.

**판단 기준.** "Privacy Sandbox / 인증서 / Sync internals / chrome://settings/passwords 등 업스트림-only 설정을 가까운 미래에 Maho 설정 안에서 보여주고 싶은가?"

**옵션:**
- **삭제 (Oracle 권장)** — embed 분기 코드 제거 + CSP child-src에서 `chrome://settings` / `chrome://settings/passwords` 제거. 잠재 재앙 + 데드 코드 둘 다 척결. Effort: S.
- **보존하고 스타일링** — 정식 설계: styled iframe shell + per-host CSP allowlist + `<EmbedPane>` 컴포넌트 정비. Effort: L.
- **보류 (현 상태 유지)** — 잠재 재앙 알면서 그대로 두기. 권장 안 함.

**다음 결정 시점.** P0 실행 시점에 다시 답변. 기본값으로 진행하려면 **삭제**.

---

## 7. Suggested Sequencing

```
[1] 철거 패스: P0 #1, #2, #4, #5, #7  ← 유령 코드 경로 제거 (모든 후속 작업 unblock)
        ↓
[2] Toggle 통합 + 첫 승격: P0 #8 (다른 7개 컴포넌트 승격의 템플릿)
        ↓ (병렬)
[3] 아이콘 부트스트랩: P0 #9 (per-surface 컨벤션 확립)
        ↓
[4] 토큰 정합 변곡점: P0 #10 + P1 #12~#15 (★ 가장 큰 leverage)
        ↓
[5] 컴포넌트 승격 wave + voice 통일: P1 #11 + #16 + #17 (P0/P1 절반이 의존)
```

**Highest-leverage step = [4] token reconciliation.** Every day of delay bakes drift into newly-added components, making future migration cost grow quadratically.

---

## 8. Risks if Not Fixed (6-month horizon)

| Cluster | What concretely breaks |
|---|---|
| **Architecture** | New contributors edit `app.ts` not knowing it's orphaned → invisible regressions ship. Someone activates `kind:'embed'` for Privacy Sandbox panes (one-line work, CSP already permits) → unstyled stock `chrome://settings` lands in Maho window → ship-blocking incident. Realistic cost: 1 visible regression + 1-2 days cleanup. |
| **Tokens** | 2 new surfaces in 6 months = 2 new `--maho-{surface}-*` namespaces with subtly-different scales. Dark-mode bugs become per-surface hunts. Migration cost grows quadratically. Welcome's Google-Blue fallbacks surface in a screenshot at the worst possible moment. |
| **Components** | Any a11y fix to Toggle (focus ring, `aria-checked`, keyboard) replicated 2-3 times. Each new surface copy-pastes its own Toggle/Select/Row. Realistic audit finding count: 8-12 duplicated a11y bugs by Q4. |
| **Icons** | Glyph rendering varies by OS / installed font. Linux without proper Noto fallbacks renders `▾` as a serif glyph mismatched to surrounding sans UI. macOS minor versions disagree on `▾` weight. The first vendored Lucide SVG that ships makes all remaining Unicode glyphs obvious bugs in the same screenshot. |
| **Copy** | Korean/Japanese localization gated behind XL i18n migration → cannot be slipped into a sprint. Brand identity (`Maho AI` vs `AI`) and voice (Title vs Sentence) drift telegraphs "two teams shipped this" to users. Every new pane added today increases the migration debt. |

---

## 9. Methodology / Reproducibility

**Wave 1 (parallel, ~3min total).** 6 background `explore` agents:
- `bg_4e06a994` — Settings WebUI file inventory
- `bg_a2356e0c` — Design tokens + shared CSS catalogue
- `bg_aa9094fa` — Shared components + reuse rate
- `bg_377185d5` — Icons (Lucide canon adherence)
- `bg_8a9a65c3` — Copy / i18n / casing / terminology
- `bg_7133dbf4` — Mojom + chromium_src overrides scope check

**Wave 4 (sync, ~3min).** Single `oracle` synthesis with consolidated evidence → score, root causes, P0/P1/P2 list, sequencing, risks, open questions.

**Skipped:**
- Wave 3 (deep agent fan-out per concern) — explorer evidence already concern-scoped, redundant audits would re-grep same files.
- Wave 5 (visual-qa screenshot evidence) — would require Chromium build (~30min-1hr); user did not opt-in.

**Notepad (transient):** `/var/folders/zh/.../ulw-settings-style-20260619-161237.....md` (101 lines)

---

## 10. Bonus Findings (LSP diagnostics on source — pre-existing, reinforce audit)

The TypeScript/HTML language servers flagged the following pre-existing issues in audited files. These were not introduced by this audit; they are additional evidence reinforcing the conclusions above and are candidates for inclusion in the P0/P1 fix waves.

| Severity | File:line | Issue | Maps to |
|---|---|---|---|
| ERROR | `settings.html:2` | `<html>` missing `lang` attribute | A11y; not covered above. **Add as P0.** |
| ERROR | `react/app.tsx:191` | `<iframe>` (the embed surface!) missing `title` attribute | **Reinforces P0 #2** — the embed path is not just visually unstyled, it's also a11y-broken. Another reason to delete. |
| ERROR | `react/app.tsx:437` | `<label>` not associated with input | A11y drift; **add as P0**. |
| ERROR | `react/domain_panes.tsx:314` | `<label>` not associated with input | A11y drift; **add as P0**. |
| ERROR | `app.ts:1036, 2091` | `forEach` callback returns a value (anti-pattern) | **Reinforces P0 #1** — `app.ts` already has lint debt. Delete in P0 #1 also resolves. |
| ERROR (cross-surface) | `maho_notes/react/primitives.tsx:82` | `<label>` not associated with input | A11y drift in sibling surface; out of immediate scope but flagged. |

These confirm a broader **a11y drift theme** that the original audit did not surface. Recommend folding three new items into P0:

- **P0 #11 (new):** Add `lang="en"` to `settings.html` (or correct locale once i18n exists).
- **P0 #12 (new):** Fix `<label>` ↔ input associations in `react/app.tsx:437` and `react/domain_panes.tsx:314`. Use `htmlFor` + `id` pairs.
- **P0 #13 (new):** If embed path is kept (per §6 Q1 "보존" outcome), iframe at `react/app.tsx:191` MUST get a meaningful `title=`. If deleted, this resolves automatically.

---

## 11. Glossary

| Term | Meaning in this document |
|---|---|
| **Surface** | A self-contained WebUI feature directory under `maho-chromium/browser/resources/` (e.g., `maho_settings/`, `maho_ai/`). Each gets its own `chrome://...` URL. |
| **Pane** | A single page in the settings panel content area (e.g., "General", "Privacy", "Maho AI"). Defined in `schema/panes.ts:PANE_DEFINITIONS`. |
| **Live pane** | A pane rendered by Maho's React components from declarative schema (`schema/setting_schema.ts`). 6 of 13 panes are live. |
| **Domain pane** | A pane with bespoke React implementation in `react/domain_panes.tsx`. The other 7 panes. |
| **Embed pane** (`kind: 'embed'`) | A pane that would render `chrome://settings/...` in an iframe. Currently 0 panes use this; code path exists in both tracks. |
| **Token drift** | Same conceptual scale step (e.g., "step 7 spacing") having different numerical values across surfaces. |
| **BEM** | Block / Element / Modifier CSS class naming convention: `.preferences-section__title--first`. Used by settings/notes/welcome/space_create. |
| **CVA + clsx** | `class-variance-authority` + `clsx` library combo for typed class composition. Used by `maho_ai` only. |
