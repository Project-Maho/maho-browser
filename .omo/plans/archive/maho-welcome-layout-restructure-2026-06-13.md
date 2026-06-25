# Maho Welcome Wizard — Layout Restructure Plan

**Created**: 2026-06-13 ~11:15 JST
**Source**: User-driven layout decision after direct PNG inspection of current implementation
**Trigger**: User identified that Stage B has duplicate advance paths (Next in sidebar + radio cards on content pane) — broader analysis revealed this is a wizard-wide issue
**Status**: Plan-only. No execution until user GO signal per wave.

---

## Executive Summary

The current wizard shell renders all controls (Primary, Back, Skip, page indicator) in the **left sidebar** with `justify-content: space-between` causing a ~50% empty middle gap. The right content pane contains decorative graphics and selection elements that compete with sidebar controls for primary-action attention.

**Restructure rule (user-decided)**:
- **Left sidebar (40%)**: Title + body (top); Page indicator + Back + Skip (bottom). Information + soft-exit only.
- **Right content (60%)**: Page content (top body); Primary action button (bottom-right). Interaction + advance.

This single restructure resolves V1 (sidebar empty middle), V3 (Stage B duplicate primary), and partially addresses V6 (Completion bare pane gets actions anchor).

---

## Visual Layout Spec (target)

```
┌──────────────────┬───────────────────────────────────┐
│ 좌측 사이드바 40%  │ 우측 콘텐츠 60%                      │
│ ─────────────── │ ─────────────────────────────────  │
│ ┌─ Top (flex 1) ┐│ ┌─ Body (flex 1) ───────────────┐│
│ │ <h1>          ││ │                                ││
│ │ <p>           ││ │ [페이지별 콘텐츠]              ││
│ │ <p>           ││ │                                ││
│ │ (grows)       ││ │                                ││
│ └───────────────┘│ │                                ││
│                  │ │                                ││
│ ┌─ Bottom ──────┐│ └────────────────────────────────┘│
│ │ ●●○○○○○ dots ││                                    │
│ │ [Skip]        ││ ┌─ Actions ─────────────────────┐│
│ │ [Back]        ││ │                  [Primary] ▶ ││
│ └───────────────┘│ └────────────────────────────────┘│
└──────────────────┴───────────────────────────────────┘
```

**Structural principles**:
- Sidebar `flex-direction: column`, NO `space-between` (use `flex: 1` on top, natural sizing for bottom)
- Content `flex-direction: column`, body grows + actions row anchored at bottom-right
- Skip stays accessible at all times (left bottom)
- Back hidden on first navigable screen (Stage A) and always present elsewhere
- Page indicator (`StepIndicator`) at left bottom above buttons
- Primary action label varies per screen, alignment is bottom-right
- Splash (S1) is **exempt** — no shell, no sidebar/content split

---

## Per-Screen Primary Action Mapping

| Screen | currentPage value | Primary label (right bottom) | Secondary (left bottom) | Notes |
|---|---|---|---|---|
| **Splash** | `WelcomePage.Splash` | _(exempt — no shell)_ | _(exempt)_ | Standalone hero with → CTA. No restructure applied. |
| **Stage A** | `SourceSelect, importStage='A'` | "Import now" | Skip only (no Back) | Primary triggers OS migration dialog |
| **Stage B** | `SourceSelect, importStage='B'` | "Next" | Skip + Back | After dialog resolves; ring+dot can be removed (V3 follow-up) |
| **ImportProgress** | `WelcomePage.ImportProgress` | _(transient — auto-advances)_ | _(disabled)_ | Spinner, no user input |
| **Search Engine** | `WelcomePage.SearchEngine` | "Next" | Skip + Back | |
| **Essentials** | `WelcomePage.Essentials` | "Next" | Skip + Back | |
| **Space Theme** | `WelcomePage.SpaceTheme` | "Next" | Skip + Back | "Pick a Theme" button stays inside content body (separate from wizard primary) |
| **Completion** | `WelcomePage.Completion` | "Dive in!" | Back only (no Skip) | Last screen — no skip. |

**Skip onClick behavior** (preserved from current):
```tsx
if (snapshot.currentPage === WelcomePage.SourceSelect) {
  store.skipImport();   // jumps over ImportProgress → SearchEngine
} else {
  store.nextPage();     // advance one
}
```

**Primary onClick behavior** (preserved):
```tsx
// Stage A: opens OS migration dialog (Mojo)
// All others: store.nextPage()
// Completion: store.complete() → openMainBrowser
```

---

## File-by-File Changes

### File 1: `maho-chromium/browser/resources/maho_welcome/react/app.tsx`

**Current shell structure (lines 162-213)**:
```tsx
<div ref={shellRef} className="welcome-shell welcome-wizard">
  <div className="welcome-shell__sidebar welcome-sidebar">
    <div ref={sidebarContentRef} className="welcome-shell__sidebar-content welcome-sidebar__content">
      {renderSidebar(...)}
    </div>
    <div ref={sidebarButtonsRef} className="welcome-shell__sidebar-buttons welcome-sidebar__buttons">
      <button primary>{primaryLabel}</button>      ← MOVE TO RIGHT
      {canGoBack && <button>Back</button>}          ← stays left bottom
      {!isCompletionPage && <button skip>Skip</button>}  ← stays left bottom
      <StepIndicator />                              ← stays left bottom
    </div>
  </div>
  <div ref={contentRef} className="welcome-shell__content welcome-content">
    <div ref={pageTransitionRef} className="welcome-page-transition">
      {renderContent(...)}
    </div>
  </div>
</div>
```

**New shell structure**:
```tsx
<div ref={shellRef} className="welcome-shell welcome-wizard">
  {/* SIDEBAR — left 40% */}
  <div className="welcome-shell__sidebar welcome-sidebar">
    {/* Top: title + body, grows */}
    <div ref={sidebarContentRef} className="welcome-shell__sidebar-content welcome-sidebar__content">
      {renderSidebar(...)}
    </div>
    {/* Bottom: nav (dots + skip + back) */}
    <div
      ref={sidebarNavRef}
      className="welcome-shell__sidebar-nav welcome-sidebar__nav"
      style={{pointerEvents: buttonsInteractive ? 'auto' : 'none'}}>
      <StepIndicator current={currentStep} total={WIZARD_STEPS} />
      {!isCompletionPage ? (
        <button
          type="button"
          className="welcome-button welcome-button--skip"
          onClick={onSkipClick}>
          {store.getString('IDS_MAHO_WELCOME_CTA_SKIP')}
        </button>
      ) : null}
      {canGoBack ? (
        <button
          type="button"
          className="welcome-button welcome-button--back"
          onClick={() => store.prevPage()}>
          {store.getString('IDS_MAHO_WELCOME_CTA_BACK')}
        </button>
      ) : null}
    </div>
  </div>

  {/* CONTENT — right 60% */}
  <div ref={contentRef} className="welcome-shell__content welcome-content">
    {/* Top: page body, grows */}
    <div className="welcome-shell__content-body">
      <div
        ref={pageTransitionRef}
        key={pageForRender}
        className={`welcome-page-transition welcome-page-transition--${snapshot.direction}`}>
        {renderContent(...)}
      </div>
    </div>
    {/* Bottom: actions row (primary right-aligned) */}
    <div
      ref={contentActionsRef}
      className="welcome-shell__content-actions welcome-content__actions"
      style={{pointerEvents: buttonsInteractive ? 'auto' : 'none'}}>
      <button
        type="button"
        className="welcome-button welcome-button--primary"
        onClick={onPrimaryClick}>
        {primaryLabel}
      </button>
    </div>
  </div>
</div>
```

**Required ref additions** (top of WelcomeApp component):
```tsx
const sidebarNavRef = useRef<HTMLDivElement>(null);
const contentActionsRef = useRef<HTMLDivElement>(null);
```

**Required animation references update**: The existing `runEntryAnimation`/`runExitAnimation` (around lines 380-500) animate `sidebarButtonsRef.current`. Replace with `sidebarNavRef.current` and add `contentActionsRef.current` to the same animation timeline so both bottom zones fade/slide in together.

**Refactor: `onSkipClick` helper** to consolidate the conditional:
```tsx
const onSkipClick = useCallback(() => {
  if (snapshot.currentPage === WelcomePage.SourceSelect) {
    store.skipImport();
  } else {
    store.nextPage();
  }
}, [snapshot.currentPage, store]);
```

---

### File 2: `maho-chromium/browser/resources/maho_welcome/welcome.css`

**Replace lines 98-129 (sidebar layout)**:

Current:
```css
.welcome-shell__sidebar,
.welcome-sidebar {
  width: 40%;
  display: flex;
  flex-direction: column;
  justify-content: space-between;   /* ← V1 BUG: causes 50% empty middle */
  padding: var(--maho-shell-sidebar-padding);
  box-sizing: border-box;
}

.welcome-shell__sidebar-content h1, .welcome-sidebar__content h1 { ... }
.welcome-shell__sidebar-content p, .welcome-sidebar__content p { ... }

.welcome-shell__sidebar-buttons,
.welcome-sidebar__buttons {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
```

New:
```css
.welcome-shell__sidebar,
.welcome-sidebar {
  width: 40%;
  display: flex;
  flex-direction: column;
  /* No justify-content — let flex children control spacing via flex: 1 */
  padding: var(--maho-shell-sidebar-padding);
  box-sizing: border-box;
}

.welcome-shell__sidebar-content,
.welcome-sidebar__content {
  flex: 1;                /* takes available space, pushes nav to bottom naturally */
  display: flex;
  flex-direction: column;
  /* h1 + p stay at top of this region */
}

/* h1 + p rules unchanged (lines 108-122) */

/* RENAME: __sidebar-buttons → __sidebar-nav */
.welcome-shell__sidebar-nav,
.welcome-sidebar__nav {
  display: flex;
  flex-direction: column;
  gap: 8px;
  flex-shrink: 0;          /* never shrinks below natural height */
}

/* StepIndicator inside sidebar-nav: place at top of nav cluster */
.welcome-shell__sidebar-nav .welcome-steps,
.welcome-sidebar__nav .welcome-steps {
  margin-bottom: 1rem;
  align-self: flex-start;  /* dots align left, not stretching across nav width */
}
```

**Replace lines 131-142 (content layout)**:

Current:
```css
.welcome-shell__content,
.welcome-content {
  width: 60%;
  background: var(--maho-shell-content-bg);
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  gap: var(--maho-shell-content-gap);
  position: relative;
  overflow: hidden;
}
```

New:
```css
.welcome-shell__content,
.welcome-content {
  width: 60%;
  background: var(--maho-shell-content-bg);
  display: flex;
  flex-direction: column;
  /* No justify-content / align-items — let body+actions own their sub-layouts */
  position: relative;
  overflow: hidden;
  box-sizing: border-box;
}

.welcome-shell__content-body {
  flex: 1;                            /* fills space above actions row */
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;             /* page content centered in body region */
  gap: var(--maho-shell-content-gap);
  width: 100%;
  padding: var(--maho-shell-sidebar-padding);  /* match sidebar padding */
  box-sizing: border-box;
  overflow: hidden;
}

.welcome-shell__content-actions,
.welcome-content__actions {
  flex-shrink: 0;
  display: flex;
  justify-content: flex-end;           /* primary right-aligned */
  align-items: center;
  padding: 1rem var(--maho-shell-sidebar-padding);
  gap: 0.75rem;
  box-sizing: border-box;
  /* No background — inherits content pane background */
}

/* Primary button sizing for content-actions placement (override default sidebar styling) */
.welcome-shell__content-actions .welcome-button--primary,
.welcome-content__actions .welcome-button--primary {
  min-width: 140px;
  padding: 0.6rem 1.5rem;
}
```

**Update primary button color (V1-from-original / brand color fix)**:

Current (around line 264-268):
```css
.welcome-button--primary {
  background: var(--color-sys-primary, #1a73e8);
  color: var(--color-sys-on-primary, #fff);
}
```

New:
```css
.welcome-button--primary {
  background: var(--maho-primary-color);
  color: #fff;
}
```

**Removed/deprecated**:
- `.welcome-shell__sidebar-buttons` and `.welcome-sidebar__buttons` rules — replaced by `__sidebar-nav` and `__content-actions`. Delete the old class block at lines 124-129.

---

### File 3: `maho-chromium/browser/resources/maho_welcome/react/pages/welcome-splash.tsx`

**No structural changes**. Splash exempt from restructure.

Already has its own self-contained `<div.welcome-splash>` with `<h1>` + standalone CTA button. The wizard shell is bypassed for this screen (rendered directly via app.tsx Splash branch).

---

### Files 4-7: Per-page content components

Pages: `import-data.tsx`, `search-engine.tsx`, `essentials.tsx`, `space-theme.tsx`, `completion.tsx`.

**No JSX structural changes required**. Each page's `*Content` component returns its inner content as before. The wizard shell now wraps it in `__content-body` automatically (per app.tsx restructure).

**One exception — completion.tsx**: `CompletionContent` already renders heart + text. Should remain as-is. The Primary "Dive in!" button is now in shell's `__content-actions` (auto-applied), not inside CompletionContent.

**Verify**: each `*Content` component's outer `<div>` does NOT have explicit `width: 100%; height: 100%; flex` rules that fight against the new content-body wrapper. If any do, let them stretch naturally inside content-body.

---

## V1-V9 Cross-Reference

| ID | Issue | Restructure resolves? | Remaining work |
|---|---|---|---|
| **V1** | Sidebar 50% empty middle | ✅ **AUTO-RESOLVED** | None — `flex: 1` on content + natural nav sizing fixes this |
| **V2** | Stage A circles empty | ⏸ KEEP per user | None — V2 OFF the fix list |
| **V3** | Stage B ring+dot disconnected from cards | ⚠️ **PARTIALLY RESOLVED** | Primary moves to right bottom → Stage B's content pane is now: ring+dot (top) + cards (middle) + Primary "Next" (bottom-right). User can re-evaluate ring+dot necessity after seeing new layout. |
| **V4** | Search Engine 5-card grid asymmetric | ❌ STILL NEEDED | `grid-template-columns: repeat(3, 1fr)` + `:nth-child(5) { grid-column: 2 }` for centered last card |
| **V5** | Space Theme dashed empty box | ❌ STILL NEEDED | Remove `border: 2px dashed`, hide empty preview OR replace with icon hint |
| **V6** | Completion right pane bare | ⚠️ **PARTIALLY RESOLVED** | Right pane now has actions row (Dive in! at bottom right) anchoring layout. Heart visibility decision (`opacity: 0` → `0.12`) still applies for static state. |
| **V7** | Shell too small at fullscreen | ❌ STILL NEEDED | `width: 60% × 60%` → `clamp(760px, 72vw, 1100px) × clamp(520px, 72vh, 780px)` |
| **V8** | Splash heading too small at fullscreen | ❌ STILL NEEDED | `font-size: 5rem` → `clamp(3.5rem, 5vw + 1rem, 8rem)` |
| **V9** | Stage A circles size fixed at 80px | ❌ STILL NEEDED | `clamp(80px, 10vw, 160px)` for circles + similar for arrow |

**After restructure: 3 of 9 issues auto-resolved. Remaining 5 still need separate fixes (V4, V5, V7, V8, V9).**

---

## Wave Structure

```
Wave R0 (PRE — git tracking):
└── git add maho-chromium/browser/resources/maho_welcome/
    git add maho-chromium/browser/ui/webui/maho_welcome/
    (these are currently untracked per `git status`; need tracking before commits)
   ETA: ~1min

Wave R1 (CORE — shell restructure):
├── R1a: app.tsx — split sidebar (top content + bottom nav) + content (top body + bottom actions)
├── R1b: welcome.css — replace .welcome-sidebar layout rules + add new content-actions/-body classes + remove old sidebar-buttons
├── R1c: app.tsx — refactor onSkipClick + onPrimaryClick handlers consolidated
├── R1d: app.tsx — update animation refs (sidebarNavRef, contentActionsRef) in entry/exit animations
└── R1e: build chrome target — verify clean
   ETA: ~45min

Wave R2 (CAPTURE + VERIFY restructure):
├── R2a: launch Maho fresh profile + capture wave-restructure/ at 1280×720, 1920×1080, 2560×1440
├── R2b: look_at + Read tool direct PNG inspection of all 7 screens at 1280×720
├── R2c: per-screen acceptance verification (see Acceptance Criteria below)
└── R2d: pkill Maho + cleanup
   ETA: ~20min

Wave R3 (REMAINING V FIXES — based on restructure result):
├── V4: Search Engine grid `repeat(3, 1fr)` + 5th centered
├── V5: Space Theme dashed box removal
├── V7: Shell sizing clamp()
├── V8: Splash heading clamp()
├── V9: Stage A circles clamp()
└── V6: Completion heart opacity decision (after seeing new pane layout)
   ETA: ~60min total

Wave R4 (FINAL):
├── Re-capture all 7 at all 4 resolutions
├── Direct PNG inspection (Read tool, no look_at agent for visual verdicts)
└── REPORT.md update + git status cleanup
   ETA: ~15min

TOTAL ETA: ~2.5hr (sequential)
```

---

## Acceptance Criteria (per screen, after Wave R1+R2)

For each acceptance: capture PNG → use **Read tool to view directly** (NOT `look_at` agent) → verify points binary.

### Splash (S1)
- ✅ Unchanged from current (no restructure applied)
- ✅ Heading "Welcome to / a calmer internet" centered serif
- ✅ → arrow CTA visible at bottom

### Stage A (S2)
- ✅ Left sidebar: title at top, body paragraphs, then ~50% bottom area now contains [page dots, Skip] (no more empty void)
- ✅ Right content body: existing two circles + arrow graphic (V2 KEEP)
- ✅ Right content actions row: "Import now" button bottom-right
- ✅ NO duplicate primary on left (only Skip + dots)

### Stage B (S3)
- ✅ Left sidebar: "Set as default browser" + body + bottom area [dots, Skip, Back]
- ✅ Right content body: ring+dot + 2 cards (radio selection)
- ✅ Right content actions: "Next" button bottom-right
- ✅ Click "Next" → advance to SearchEngine
- ✅ Click "Skip" → also advance (since not on Stage A)
- ⚠️ V3 follow-up: ring+dot may become unnecessary; user re-evaluates

### Search Engine (S4)
- ✅ Left sidebar: title + body + bottom area [dots, Skip, Back]
- ✅ Right content body: 5 search engine cards (V4 fix needed for grid)
- ✅ Right content actions: "Next" button bottom-right

### Essentials (S5)
- ✅ Left sidebar: title + body + bottom area [dots, Skip, Back]
- ✅ Right content body: 9 essential icons + mock browser (already polished)
- ✅ Right content actions: "Next" button bottom-right

### Space Theme (S6)
- ✅ Left sidebar: title + body + bottom area [dots, Skip, Back]
- ✅ Right content body: theme preview swatch + "Pick a Theme" button (existing)
- ✅ Right content actions: "Next" button bottom-right
- ⚠️ Two right-pane buttons now: "Pick a Theme" (inside content body) + "Next" (in actions row). Visual hierarchy must distinguish them — "Pick a Theme" is a content action, "Next" is wizard advance.

### Completion (S7)
- ✅ Left sidebar: title + body + bottom area [dots, Back] (no Skip on completion)
- ✅ Right content body: heart + text (heart at `opacity: 0.12` per V6 follow-up)
- ✅ Right content actions: "Dive in!" button bottom-right
- ✅ Click "Dive in!" → triggers `runCompletionSequence` → opens main browser

---

## Verification Protocol

**Per Wave R2 step**:
1. Build `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome`
2. Launch Maho with `--remote-debugging-port=9333 --remote-allow-origins=*`
3. Capture all 7 screens with `python3 /tmp/maho-screenshots/capture.py --output-dir /tmp/maho-screenshots/wave-restructure/`
4. **For each screen**: use **`Read tool` on the PNG** (NOT `look_at` agent — that gave inconsistent verdicts in prior sessions)
5. Verify acceptance points binary
6. Capture sizes at 1280×720, 1920×1080, 2560×1440 — compare to wave-defer baseline (sizes should change measurably for V1/V3 resolved screens)

**Anti-patterns from prior sessions to avoid**:
- ❌ Trusting `look_at` agent verdicts without direct PNG inspection
- ❌ Comparing only file size deltas without visual verification
- ❌ Capturing only at 1280×720 (use multiple resolutions)
- ❌ Declaring complete based on "code looks right"

---

## Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Animation refs (`sidebarButtonsRef`) lookups fail after rename | MEDIUM | Build fail | Update all `sidebarButtonsRef.current` references in app.tsx animation code to `sidebarNavRef.current` + add `contentActionsRef.current` |
| New CSS class names conflict with existing | LOW | Visual breakage | New class names use `_-nav` and `__content-actions` suffixes — unique |
| Page content components fight against new content-body wrapper | LOW | Layout breakage | Verify each page's outer div has no `width: 100%; height: 100%; flex` overrides; if any, remove |
| Primary button styling differs in new content-actions context vs old sidebar | LOW | Subtle visual difference | New CSS rule `.welcome-shell__content-actions .welcome-button--primary` overrides for proper sizing |
| Skip behavior on Stage A breaks (currently triggers `skipImport`) | LOW | Functional regression | `onSkipClick` helper preserves the conditional — verify tested path |
| Page transition animations fight against new layout | MEDIUM | Animation jank | Update entry/exit animation targets to include both `sidebarNavRef` and `contentActionsRef`; test transitions |
| Untracked git state means changes can't be diff'd cleanly | MEDIUM | Code review difficulty | Wave R0 git-add untracked welcome dirs first |

---

## Implementation Notes

### Why this restructure beats prior plan v4 layout

Plan v4 placed all controls in sidebar with `space-between` layout. This was inherited from the original Zen-inspired design but produced:
- Visual clutter in sidebar (4 buttons + dots in cramped vertical stack)
- Empty middle in tall sidebars
- Duplication when content pane also has interactive elements (Stage B cards)

The new restructure separates concerns:
- **Sidebar = info zone** (passive: title, body, page indicator) + **soft exit** (Skip, Back)
- **Content = action zone** (active: page widgets, primary advance)

This is closer to standard wizard UX patterns (e.g., macOS installers, modern web onboarding flows) where primary advance is bottom-right of the content area.

### Why right-bottom-aligned Primary

- Conventional "next" placement in LTR UIs (form wizards, dialogs)
- Eye flow: title (top-left) → body (left) → content (right top) → action (bottom-right). Z-pattern.
- Distance from secondary controls (Skip/Back) = reduced misclick

### Skip stays accessible

User feedback principle: Skip should always be one click away regardless of state. Bottom-left placement keeps it visible without competing with primary attention.

### Splash exemption rationale

Splash is a marketing hero, not a wizard step. Has no Skip/Back semantics. Restructure rule does not apply.

---

## Cross-References

- Source plan: `.sisyphus/plans/maho-welcome-7-bug-visual-fix-2026-06-12.md` (V1-V9 fix list, plan v4 base)
- Defer plan: `.sisyphus/plans/maho-welcome-functional-gaps-defer-2026-06-12.md` (DG-1~15 functional gaps)
- Theme picker ADR: `.sisyphus/plans/theme-picker-architecture-decision.md` (D2 dialog reuse — not affected by restructure since dialog is separate WebUI)
- Capture script: `/tmp/maho-screenshots/capture.py` (486 lines, reusable)
- Reference baseline: `/tmp/maho-screenshots/wave-defer/` (current implementation, pre-restructure)

---

## Approval Gate

**STATUS: pending user GO signal per wave.**

User options:
- `GO R0` — git-add untracked welcome dirs first
- `GO R1` — execute Core restructure (app.tsx + welcome.css)
- `GO R2` — execute Capture + Verify
- `GO R3` — execute remaining V4/V5/V7/V8/V9 fixes (after R2 verification)
- `GO R4` — final capture + report
- `GO ALL` — sequential R0 → R1 → R2 → R3 → R4 (~2.5hr total)
- `MODIFY` — request plan changes

**No execution starts until user GO signal received.** Plan-only deliverable as of 2026-06-13 ~11:15 JST.
