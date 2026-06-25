# Maho Welcome — Functional Gaps & Deferred Items

**Created**: 2026-06-12 ~23:30 JST
**Source**: Visual QA verification of plan v4 implementation (`.sisyphus/plans/maho-welcome-7-bug-visual-fix-2026-06-12.md`)
**Capture set**: `/tmp/maho-screenshots/wave-final/` (7 PNGs, 1280×720)
**Build state**: `ninja: no work to do.` (clean)
**Implementation review**: 9.5/10 — plan v4 fully implemented, 1 visual layout bug + 6 functional gaps identified

---

## Executive Summary

Plan v4 implementation in another session matched specification on 6 of 7 wizard screens. Visual QA via PNG `look_at` revealed:

- **Visual PASS**: 6/7 screens (Splash, Migration Stage A, Default Browser Stage B, Search Engine, Essentials, Space Theme)
- **Visual FAIL**: 1/7 screens (Completion — heart icon and title/tagline overlap)
- **Functional gaps unverified**: 6 items requiring runtime/E2E testing or follow-up cleanup

This document defers fixes for the visual bug + 6 functional gaps to follow-up implementation.

---

## Visual QA Results (Wave-Final Capture)

| Screen | File | Size | Verdict | Notes |
|--------|------|------|---------|-------|
| 1 Splash | screen-1-splash.png | 26447B | ✅ PASS | "Welcome to / a calmer internet" 2-line serif + → arrow CTA |
| 2 Migration Stage A | screen-2-import-stage-a.png | 44448B | ✅ PASS | Sidebar h1 + 2 paragraphs + Import/Skip; right pane: translucent circle + ➔ + indigo Maho circle |
| 3 Default Browser Stage B | screen-3-default-browser.png | 34942B | ✅ PASS | Sidebar title + body; right pane: ring+dot anchor above 2 side-by-side cards (DON'T set indigo-bordered) |
| 4 Search Engine | screen-4-search-engine.png | 40093B | ✅ PASS | 5 distinct brand SVGs: Google (multicolor G), Naver (green N), Bing (blue b), Daum (black D), Cốc Cốc (green+orange) |
| 5 Essentials | screen-5-essentials.png | 47363B | ✅ PASS* | 9 colored SVG icons + mock browser preview. *Minor: window traffic-light dots are gray (not red/yellow/green) — design intent unclear |
| 6 Space Theme | screen-6-space-theme.png | 29082B | ✅ PASS | "Choose your theme" + body (i18n resolved); right pane: dashed empty preview + indigo "Pick a Theme" button |
| 7 Completion | screen-7-completion.png | 33810B | ❌ **FAIL** | Heart icon and "You're all set" + tagline OVERLAP — heart in background, text on top, conflicting visual |

---

## Visual Bug — Wave Defer-1 (Required)

### DG-1: Completion screen heart/text overlap

**Severity**: HIGH (blocks visual acceptance for screen 7)

**File**: `maho-chromium/browser/resources/maho_welcome/welcome.css`

**Symptom** (`look_at` evidence): "Heart icon and title/tagline overlap. Heart fills the right pane (position: absolute; inset: 0) AND welcome-completion-pane also (position: absolute; inset: 0). Both center their content → text rendered on top of heart graphic. Stacking: heart (back) → text (front)."

**Root cause**: Both `.welcome-heart` and `.welcome-completion-pane` use `position: absolute; inset: 0` with flex centering. They occupy the same area.

**Two fix options**:

#### Option A (animation-aware): Heart hidden until pulse animation
```css
.welcome-heart {
  /* ... existing rules ... */
  opacity: 0;  /* Hidden by default; animation controls visibility */
}
```
Justification: `runCompletionSequence` (`completion.tsx:39-53`) animates heart `opacity: [0, 1, 1, 1, 0]` with `fill: forwards`. The animation BOTH starts at 0 AND ends at 0, so CSS default `opacity: 0` is consistent with desired before/after states. Heart only visible during 1.5s pulse.

**Pre-click state**: title + tagline visible, heart invisible
**Click "Dive in!"**: content slides right, heart pulses (visible 1.5s), everything fades, browser opens

#### Option B (layout split): Heart and content stacked vertically
```css
.welcome-completion-stage {
  /* keep position relative */
  flex-direction: column;
  gap: 1.5rem;
}
.welcome-heart {
  /* remove position: absolute; inset: 0 */
  position: relative;
  width: 30%;
  aspect-ratio: 1 / 1;
  /* keep flex centering */
}
.welcome-completion-pane {
  /* remove position: absolute; inset: 0 */
  position: relative;
}
```
Justification: heart is hero element above text. Both visible simultaneously. Animation re-design needed (translateX no longer overlay-friendly).

**Recommendation**: **Option A** — preserves existing animation flow, minimal change, matches likely design intent (heart appears DURING transition, not before).

**Acceptance criteria**: re-capture screen-7 PNG, `look_at` reports "title + tagline visible in upper portion (or vertical center) of right pane. Heart NOT visible in static screenshot (will pulse during animation only)."

**Effort**: 1 line CSS + rebuild + re-capture + look_at = ~10min

---

## Functional Gaps — Wave Defer-2 (Functional Validation)

The plan v4 implementation passed STATIC visual review but the following dynamic/functional behaviors are unverified. Each requires a separate validation pass with the running browser.

### DG-2: D2 dialog open flow end-to-end (Wave E2 acceptance)

**Status**: Static button visible (PASS), runtime click → dialog open → theme selected → wizard advances UNVERIFIED

**Missing evidence**:
1. Click `.welcome-theme-picker-button` via CDP
2. Verify native Views dialog (`MahoSpaceThemePickerDialog`) opens at top-level (NOT in subframe)
3. Pick a theme + click Done in dialog
4. Verify dialog closes
5. Verify React state updates (selectedTheme set, preview swatch shows new color)
6. Verify wizard "Next" button enables
7. Verify Mojo callback invoked with committed_theme_json (not cancelled path)

**Test plan**:
- CDP click button → wait 500ms → check for native dialog window (separate Maho window class via `osascript` or accessibility API)
- Or: capture screenshot of full screen (not just maho-welcome page) to see if dialog window appears
- Or: instrument `MahoSpaceThemePickerDialog::Open` to log dispatch + result, verify in `/tmp/maho-vr.log`

**Effort**: ~30min (test harness + manual run + verification)

---

### DG-3: Completion sequence animation flow (Wave E2 + heart fix)

**Status**: Code path exists in `completion.tsx::runCompletionSequence` — UNVERIFIED runtime

**Missing evidence**:
1. Static state (before click): correct (after DG-1 fix)
2. Click "Dive in!" → `#welcome-content` slides translateX 0% → 100% (500ms cubic-bezier)
3. `#welcome-heart` pulses opacity [0→1→1→1→0] + scale [0.5→1→1.2→1→1.2] over 1.5s
4. `store.favoriteSelectedEssentials()` called (Mojo trace)
5. `store.applyTheme()` called if theme selected
6. `#welcome-pages` fades opacity 1 → 0 over 400ms
7. `store.openMainBrowser()` called (browser window opens)

**Test plan**:
- CDP record: `Animation.enable` → click Dive in → capture animation events
- Verify each animation start/end via `Animation.animationStarted`/`animationCanceled`
- Final state: maho-welcome window closes, normal browser window opens

**Effort**: ~45min (animation event listener test harness)

---

### DG-4: Native theme picker dialog regression (ADR canonical path)

**Status**: D2 fix added new entry point to dialog. Existing entry point (sidebar "+" → "Choose a Theme") MUST still work.

**Missing evidence**:
1. Open Maho normally (not welcome wizard)
2. Click sidebar "+" → "Choose a Theme" row
3. Verify `MahoSidebarCreateSpaceView::OnThemePressed()` → `MahoSpaceThemePickerDialog::Open(...)` opens dialog
4. Verify dialog renders chrome://maho-space-create at top-level
5. Pick theme + commit
6. Verify theme applied to existing space

**Test plan**:
- macOS GUI automation (System Events / accessibility)
- Or: manual verification

**Effort**: ~15min manual or ~1hr automated

---

### DG-5: Search engine clicking applies default

**Status**: 5 brand SVGs render. Click → set default UNVERIFIED.

**Missing evidence**:
1. Click any of the 5 cards
2. Verify `engine.iconUrl`-equivalent state propagates to React store
3. Verify `pageHandler.setDefaultSearchEngine(keyword)` called via Mojo
4. Verify `TemplateURLService::SetUserSelectedDefaultSearchProvider` succeeds
5. Verify after wizard exit, browser address bar uses selected engine

**Test plan**:
- CDP click + Mojo trace
- Or: complete wizard with Naver selected, then verify `chrome://settings/searchEngines` shows Naver as default

**Effort**: ~20min

---

### DG-6: Essentials selection persists post-onboarding

**Status**: 9 SVG tiles render. Click → favorite UNVERIFIED.

**Missing evidence**:
1. Click 3-5 essential tiles
2. Verify `store.favoriteSelectedEssentials()` populates `pageHandler.favoriteEssentialSites(urls)`
3. After completing wizard, verify Maho's essentials sidebar shows selected sites
4. Verify pinned state survives browser restart

**Effort**: ~25min

---

### DG-7: Mock browser window dots — design intent

**Status**: Visible (3 gray dots), but spec/Zen reference suggests Apple colored (red/yellow/green)

**Question**: Is monochrome gray the intentional design for Maho's identity? OR a missing CSS color?

**Resolution path**:
- Check Zen reference screenshots in `test_data/zen-screenshots/essentials.png`
- Compare visually
- If Zen shows colored dots → fix CSS
- If Zen shows gray → mark as intentional, document

**Effort**: ~10min check + maybe ~5min CSS fix

---

### DG-8: Search engine page handler dead code cleanup

**Status**: Code review revealed dead code at `maho_welcome_page_handler.cc:282-289`

**Issue**: Line 251 filters out `if (!turl->prepopulate_id()) continue;` — non-prepopulated engines never reach the favicon2 fallback at lines 282-289. The fallback is unreachable.

**Decision needed**:
- **Option A**: Remove dead code, document intent ("welcome wizard intentionally shows only prepopulated engines")
- **Option B**: Remove the line 251 filter, allow non-prepopulated engines + favicon2 fallback

**Recommendation**: Option A — UX rationale (curated engine list for first-run) is reasonable; dead code is technical debt.

**Effort**: ~10min cleanup + comment

---

### DG-9: Git tracking — maho_welcome directories untracked

**Status**: `git status` shows `?? maho-chromium/browser/resources/maho_welcome/` and `?? maho-chromium/browser/ui/webui/maho_welcome/`

**Cause unclear**: Possibly recent restructuring in another session. Files exist + build clean, but not in version control.

**Action**:
```bash
git add maho-chromium/browser/resources/maho_welcome/
git add maho-chromium/browser/ui/webui/maho_welcome/
git status  # verify clean
```

**Effort**: ~2min (assuming no .gitignore conflict)

**Risk**: If `.gitignore` excludes these (unlikely but possible), need to add `!` exception.

---

## Wave Defer Schedule

```
Wave Defer-1 (BLOCKING visual acceptance):
└── DG-1: S7 heart/text overlap fix (Option A: opacity:0 on .welcome-heart)
   ETA: ~10min

Wave Defer-2 (Functional validation):
├── DG-2: D2 dialog flow E2E test (~30min)
├── DG-3: Completion animation flow E2E test (~45min)
├── DG-4: Native dialog regression check (~15min manual)
├── DG-5: Search engine click → default applied (~20min)
├── DG-6: Essentials click → favorite persisted (~25min)
   ETA: ~2.5hr total

Wave Defer-3 (Cleanup):
├── DG-7: Window dots design intent resolution (~10min)
├── DG-8: Dead code cleanup in page_handler.cc (~10min)
├── DG-9: Git add untracked welcome directories (~2min)
   ETA: ~25min total

Total deferred work: ~3hr
```

---

## Out of Scope (Not Tracked Here)

The following are explicitly NOT functional gaps:

- ✅ All 25 IDS strings localized (Korean + English) — verified in code review
- ✅ Brave Leo SVG vendoring with proper SPDX/provenance — verified
- ✅ LICENSES/brave-leo-icons.txt exists — verified
- ✅ BUILD.gn deps clean (favicon_source, theme_picker_dialog) — verified
- ✅ Mojom + handler.h consistent (OpenThemePickerDialog declared/implemented) — verified
- ✅ apply_chromium_src_overrides.py NOT modified — INTENTIONAL (strings served via Mojo, not grd)
- ✅ Build state clean — verified

---

## Cross-References

- Source plan: `.sisyphus/plans/maho-welcome-7-bug-visual-fix-2026-06-12.md` (1533 lines, v4 FINAL)
- Code review session findings: this session 2026-06-12 23:00-23:30
- Capture artifacts: `/tmp/maho-screenshots/wave-final/` (7 PNGs)
- Notepad: `/var/folders/zh/.../ulw-vqa-final-20260612-231249.XXXXXX.md.QKqOpA02pa`
- Theme Picker ADR: `.sisyphus/plans/theme-picker-architecture-decision.md` (D2 strategy compliance verified)

---

## Approval Gate

**STATUS: pending user direction.**

User options:
- `GO Defer-1` — fix S7 heart/text overlap (10min, blocking visual acceptance)
- `GO Defer-2` — full functional validation suite (~2.5hr, comprehensive E2E)
- `GO Defer-3` — cleanup tasks (~25min)
- `GO ALL` — sequential Defer-1 → Defer-2 → Defer-3
- `WAIT` / `MODIFY` — additional planning before execution

**No execution starts until user GO signal received.**

---

# Wave Defer-4 ADDENDUM — Fullscreen Rendering Issues (Discovered 2026-06-13 ~08:30)

**Trigger**: User question "실제로 전체화면에서 제대로 렌더링 되는거 맞아?"
**Method**: Re-captured wizard at 1440×900, 1920×1080, 2560×1440 viewports (vs original 1280×720)
**Display reference**: Mac M-series 3456×2234 Retina = logical 1728×1117

## Display vs Capture Resolution Comparison

| Resolution | CSS breakpoint | shell width | Captures dir |
|---|---|---|---|
| 1280×720 | `@media (max-width: 1400px)` | 80% | `/tmp/maho-screenshots/wave-defer/` |
| 1440×900 | none → default 60% | 60% | `/tmp/maho-screenshots/wave-1440x900/` |
| 1920×1080 | none → default 60% | 60% | `/tmp/maho-screenshots/wave-1920x1080/` |
| 2560×1440 | none → default 60% | 60% | `/tmp/maho-screenshots/wave-2560x1440/` |

**Original 1280×720 capture set was at the wrong breakpoint (80% shell). Real users see 60% shell.**

## DG-10: Splash heading scaling at fullscreen (HIGH)

**Symptom**: At 1920×1080, "Welcome to / a calmer internet" heading occupies ~25% of screen width (490×155 px in 1920×1080 viewport). Splash screens typically need 50-70% width for visual impact.

**Root cause**: `.welcome-splash__title { font-size: 5rem }` is fixed (80px). Doesn't scale with viewport.

**Fix candidates**:
- **A**: Use `clamp()` for responsive scaling: `font-size: clamp(3rem, 8vw, 8rem)`
- **B**: Add `@media (min-width: 1600px)` to bump font-size to 7-8rem
- **C**: Convert to viewport-relative units: `font-size: 6vw` with min/max clamping

**Recommendation**: Option A (`clamp`) — works across all resolutions naturally.

**Effort**: ~5min (1 CSS line) + verify

---

## DG-11: Stage A graphic circles too small at fullscreen (HIGH)

**Symptom**: At 2560×1440, two 80px circles in a ~700×680 px right pane = 5-7% area. Looks like floating dots in vast gray space.

**Root cause**: `.welcome-import-graphic__circle { width: 80px; height: 80px }` is fixed. Should scale with pane.

**Fix candidates**:
- **A**: Use `min(15%, 120px)` width with aspect-ratio
- **B**: `clamp(80px, 12vw, 200px)` for circles + `clamp(2rem, 4vw, 4rem)` for arrow
- **C**: Use SVG with `width: 60%` of container instead of fixed pixels

**Recommendation**: Option B (clamp) — consistent with DG-10 approach.

**Effort**: ~10min (multiple CSS rules)

---

## DG-12: Search engine grid asymmetry (5 items in 2×3) (MEDIUM)

**Symptom**: 5 search engines in 2-column grid → 3 rows where last row has 1 card + 1 empty slot. Visually unbalanced.

**Fix candidates**:
- **A**: Force `.welcome-engines:has(card:nth-of-type(5):last-child)` selector → use `grid-template-columns: repeat(5, 1fr)` (single row)
- **B**: 3-column grid → row 1 (3 cards) + row 2 (2 cards centered via `justify-content: center` on last row)
- **C**: Asymmetric flex with explicit positioning

**Recommendation**: Option A for ≥1400px, fall back to 2-col for <1400px (responsive).

**Effort**: ~15min (CSS + breakpoint testing)

---

## DG-13: Wizard shell max-width too conservative (MEDIUM)

**Symptom**: At 2560×1440 with 60% shell width = 1536×864, but auditor estimated actual rendering at ~1200×720 (47%×50%). Possible explicit max-width or sub-element constraint capping the shell.

**Action**:
- Read `.welcome-shell` and `.welcome-wizard` CSS rules in full
- If max-width/max-height present, evaluate raising for >1400px breakpoint
- Add `@media (min-width: 1600px) { .welcome-shell { width: 70%; height: 70%; max-width: 1600px } }`

**Effort**: ~15min (audit + adjust)

---

## DG-14: Completion right pane empty after DG-1 fix (DESIGN DECISION)

**Symptom**: DG-1 fix (`.welcome-heart { opacity: 0 }`) hides heart in static state, but at fullscreen the right pane is now PURE TEXT in vast empty gray. Visual void where there was overlap before.

**Tradeoff**:
- Before DG-1: heart visible BUT overlaps text (broken)
- After DG-1: text visible BUT pane half-empty (functional but bare)

**Better fix candidates**:
- **A**: Position heart in upper portion of right pane, text in lower portion (vertical stack via flex-column with gap) — both visible, no overlap
- **B**: Use heart as a watermark — `opacity: 0.08` (very subtle background) + text on top
- **C**: Use heart as small decorative element above text (max-width 30%, position relative not absolute)
- **D**: Accept current state — minimal text-only right pane

**Recommendation**: Option C — heart as decorative hero (small, above text), preserves visual identity AND text legibility AND animation flow (when animation runs, heart can scale up to fill).

**Effort**: ~20min (CSS layout rework + animation timing review)

---

## DG-15: Sidebar vertical balance — text top, buttons bottom, big gap (LOW)

**Symptom**: At fullscreen, sidebar text (h1 + body) sits at top, buttons at bottom, ~50% empty space between. Looks unbalanced.

**Note**: This is `flex-direction: column; justify-content: space-between` behavior — INTENTIONAL on small screens, but feels excessive on tall sidebars.

**Fix candidates**:
- **A**: Add `justify-content: center; gap: 3rem` for tall viewports (`@media (min-height: 900px)`)
- **B**: Add visual filler element (e.g., decorative line, icon, or progress visualization)
- **C**: Accept — design choice

**Recommendation**: Option A — only adjust at tall viewports.

**Effort**: ~10min

---

## Wave Defer-4 Schedule

```
DG-10: Splash heading clamp() scaling          ~5min
DG-11: Stage A circles + arrow scaling          ~10min
DG-12: Search engine 5-card grid responsive     ~15min
DG-13: Shell max-width audit + adjust           ~15min
DG-14: Completion pane heart+text layout        ~20min
DG-15: Sidebar vertical balance @ tall viewport  ~10min
─────────────────────────────────────────────────
Total Defer-4 effort:                          ~75min
```

## Verification gate for Defer-4

After applying fixes, re-capture at:
- 1280×720 (sanity — no regression at small)
- 1920×1080 (mid)
- 2560×1440 (large)
- 3456×2234 (native Retina if possible — won't fit in current capture script setDeviceMetricsOverride)

`look_at` per resolution: balance ratio, scale appropriateness, content density.

Pass criteria: All 4 resolutions render with NO "광활한 빈 공간" / "wallflower" feel / asymmetric grid. Wizard shell appears proportional to viewport.

---

## Updated Defer Status (2026-06-13 morning state)

| ID | Status | Notes |
|---|---|---|
| DG-1 | ✅ DONE | `.welcome-heart { opacity: 0 }` — heart hidden in static, BUT created DG-14 (empty pane) |
| DG-2 | ⏸ Trust | User-claimed completed (E2E validation) |
| DG-3 | ⏸ Trust | User-claimed completed (E2E validation) |
| DG-4 | ⏸ Trust | User-claimed completed (E2E validation) |
| DG-5 | ⏸ Trust | User-claimed completed (E2E validation) |
| DG-6 | ⏸ Trust | User-claimed completed (E2E validation) |
| DG-7 | ✅ DONE | macOS traffic-light dots #FF5F56/#FFBD2E/#27C93F applied |
| DG-8 | ✅ DONE | Comment clarified — fallback covers other prepopulated engines (Yandex etc.) |
| DG-9 | ❌ STILL OPEN | `?? maho-chromium/browser/{resources,ui/webui}/maho_welcome/` untracked |
| DG-10 | 🆕 NEW | Splash heading too small at fullscreen |
| DG-11 | 🆕 NEW | Stage A circles too small at fullscreen |
| DG-12 | 🆕 NEW | Search engine 5-card 2×3 asymmetric grid |
| DG-13 | 🆕 NEW | Wizard shell max-width conservative at fullscreen |
| DG-14 | 🆕 NEW | Completion right pane empty (DG-1 side effect) |
| DG-15 | 🆕 NEW | Sidebar vertical imbalance at tall viewports |
