# Maho AI Compact Panel — Card Removal + Header Consolidation

**Status:** New plan, written 2026-05-23. Builds on the AI panel UX work
already shipped in `.sisyphus/plans/maho-ai-acp-migration-plan.md` and
the persistence/legacy cleanup plan.

## 1. Objective

Two visual changes to `chrome://maho-ai`:

1. **Remove the "card" geometry on `.compact-panel`.** Today the panel
   draws itself as a floating card (border + border-radius + shadow +
   solid background) inside the side-panel host. The user wants the
   panel to extend edge-to-edge, blending into the host. Card chrome is
   removed entirely.
2. **Consolidate the duplicate "Maho AI" branding.** The brand text
   appears twice on the empty state — once in the `<header>` strip
   (`compact-panel__quiet-title`) and once embedded in the welcome
   description ("Start with a prompt below and Maho AI will open …").
   The top header is removed; the only remaining "Maho AI" mention is
   the inline description. The header's two action affordances
   (Start-new-session + OverflowMenu) move to a small floating cluster
   in the top-right corner of the panel.

## 2. Source of truth

This file. Other plans untouched.

## 3. Verified facts

| Fact | Evidence |
|---|---|
| Top header is React-rendered, not host-chrome | `react/features/compact/compact-header.tsx` lines 17-39 — owns `<header class="compact-panel__header">` |
| Brand text "Maho AI" lives in `compact-panel__quiet-title` | same file, line 24 |
| Welcome description embeds "Maho AI" via `compact-stage.tsx` line 7 | "Start with a prompt below and **Maho AI** will open a live session in this thread." |
| Card style is on `.compact-panel` itself | `styles/compact-panel.css:12-25` — border, border-radius, background, box-shadow |
| `.compact-panel::before` exists at line 33 — sibling decoration | one extra check needed in implementation; remove if it is part of the card chrome |
| `compact-panel--developer` modifier reuses the card | line 27-30 — both branches affected by chrome removal, not just `--empty` |

## 4. Non-goals

1. Do **not** restyle the welcome state copy ("Ready when you are" / "Live
   session ready"). Keep the description text as-is — it is the only
   surviving brand reference, intentionally.
2. Do **not** touch developer mode UI (`compact-panel--developer`,
   `developer-workspace.tsx`, etc.). Card removal applies to both
   compact and developer modes; that is intended consolidation, not a
   separate redesign.
3. Do **not** change the composer, conversation thread, thinking bubble,
   or tool-call rendering. Visual scope is the outer panel chrome only.
4. Do **not** introduce new design tokens or colors. Reuse what is
   already in `styles/tokens.css`.
5. Do **not** remove the `Icon symbol="panelSpark"` mark from the
   codebase — only its use in the compact header.

## 5. Phased execution

Each phase is independently shippable. Phase 1 is purely additive-removal
on CSS; Phase 2 touches React + CSS together. Verify after each phase.

### Phase 1 — Card chrome removal

**Goal:** `.compact-panel` no longer renders as a card. The panel is a
flat container that fills its host area.

**File:** `maho-chromium/browser/resources/maho_ai/styles/compact-panel.css`

**Atomic steps:**
1. In the `.compact-panel { … }` rule (around line 12), remove these
   declarations:
   - `border: var(--maho-ai-border-width) solid var(--maho-ai-border);`
   - `border-radius: var(--maho-ai-radius-xl);`
   - `background: var(--maho-ai-bg-panel);`
   - `box-shadow: var(--maho-ai-shadow-shell);`
2. Keep `padding`, `min-height`, `display: flex`, `flex-direction`,
   `width`, `max-width`, `position: relative`, `overflow: hidden`.
3. Inspect `.compact-panel::before { … }` at line 33. If its purpose is
   the card highlight / inner glow / outline that complemented the card
   chrome, delete the rule. If it does something unrelated, leave it.
4. Inspect `.compact-panel--developer` at line 27 — currently overrides
   only `width`/`max-width`/`padding`. No card reset needed because the
   parent rule no longer has card properties.
5. Re-evaluate `padding`. With no card, the previous padding may now
   leave the content too far from the host edge. Adjust if needed —
   prefer halving the existing value rather than zeroing it out, so
   composer/thread don't slam against the side panel sides.

**Verification:**
- Build (`python3 maho-chromium/build/scripts/build_maho.py`) passes.
- Diag instance live: panel renders edge-to-edge, no rounded corners,
  no card outline, no shadow, host background visible.
- DOM inspector: `getComputedStyle(document.querySelector('.compact-panel'))`
  reports `border-width: 0px`, `border-radius: 0px`, `background-color:
  rgba(0,0,0,0)`, `box-shadow: none`.

**Rollback:** revert the CSS change. No React or build changes.

### Phase 2 — Header hide + floating actions

**Goal:** The `<header>` strip with brand + actions is gone. Actions
re-appear as a small floating cluster in the top-right of the panel.
The empty welcome state shows only the body copy with one "Maho AI"
mention inside the description.

**Files:**
- `maho-chromium/browser/resources/maho_ai/react/features/compact/compact-header.tsx`
- `maho-chromium/browser/resources/maho_ai/react/features/compact/conversation-thread.tsx` *or wherever `<CompactHeader>` is rendered today*
- `maho-chromium/browser/resources/maho_ai/styles/compact-panel.css`
- `maho-chromium/browser/resources/maho_ai/styles/thread.css` *if header CSS lives there too*

**Atomic steps:**

1. Locate every site that imports/renders `<CompactHeader>`. Likely
   `react/features/compact/conversation-thread.tsx` or similar. Note
   the call sites.
2. Refactor `compact-header.tsx`:
   - Rename the exported component to `CompactFloatingActions` and
     keep the same prop signature (`hasMessages`, `onSetDeveloperMode`,
     `onStartSession`).
   - Replace the JSX tree:
     ```tsx
     <div className="compact-panel__floating-actions">
       <IconButton
         ariaLabel="Start new session"
         symbol="plus"
         variant="compact-icon-ghost"
         onClick={onStartSession}
       />
       <OverflowMenu
         onNewSession={onStartSession}
         onOpenDeveloperMode={() => onSetDeveloperMode(true)}
       />
     </div>
     ```
   - Remove the `<header>`, `compact-panel__brand*`, `compact-panel__quiet-title`,
     and the `panelSpark` icon.
   - Remove `hasMessages` from the rendered output. The prop can stay in
     the signature for callers that already pass it (avoid a breaking
     refactor of every call site), but the floating cluster doesn't use
     `--empty` styling. Add a one-line comment that the prop is
     reserved for future state-aware action visibility.
3. Update every call site found in step 1: rename `<CompactHeader>` to
   `<CompactFloatingActions>` and keep the props as-is.
4. Add CSS to `styles/compact-panel.css` (or `thread.css` if that is
   where the existing compact-panel chrome lives — pick one place,
   don't split):
   ```css
   .compact-panel__floating-actions {
     position: absolute;
     top: var(--maho-ai-space-2);
     right: var(--maho-ai-space-2);
     display: flex;
     gap: var(--maho-ai-space-1);
     z-index: 2;
     padding: var(--maho-ai-space-1);
     border-radius: var(--maho-ai-radius-md);
     background: var(--maho-ai-bg-overlay-subtle);
     backdrop-filter: blur(6px);
   }
   ```
   - If `--maho-ai-bg-overlay-subtle` does not exist, fall back to a
     low-opacity overlay token from `tokens.css`. Do not add new tokens.
5. Drop dead CSS rules whose only consumers were the deleted classes:
   - `.compact-panel__header`, `.compact-panel__header--empty`,
     `.compact-panel__brand`, `.compact-panel__brand--quiet`,
     `.compact-panel__brand-mark`, `.compact-panel__quiet-title`,
     `.compact-panel__header-actions`, `.compact-panel__header-actions--quiet`.
   - Verify by `grep -rn '<class-name>' maho-chromium/browser/resources/`
     before deleting.
6. Adjust `.compact-thread__row--empty` / welcome top spacing if the
   floating cluster overlaps the welcome title. If overlap occurs, add
   `padding-top` to `.compact-thread__list--empty` so the welcome
   doesn't sit beneath the cluster.
7. After streaming is active (`hasMessages: true`), the cluster needs
   to stay above the conversation thread without obscuring text. Add
   to `.compact-thread__list` (or whichever owns the scroll region) a
   `padding-top` equal to the floating cluster height plus a margin —
   roughly `calc(var(--maho-ai-space-2) * 2 + 28px)`.

**Verification:**
- Build passes.
- Diag instance live:
  - Empty state: only one "Maho AI" mention (in the welcome description).
    No top strip. Floating cluster visible top-right.
  - Active state: floating cluster still top-right, scroll region begins
    below it without text being clipped.
  - Click "Start new session" + open OverflowMenu — both work, no
    z-index issues, dropdown positioning correct.
- `document.querySelectorAll('.compact-panel__quiet-title')` returns 0.
- `document.body.innerText` contains "Maho AI" exactly once when in the
  empty state.

**Rollback:** revert the React + CSS commit. No state migration.

### Phase 3 — Live polish pass (post-build)

**Goal:** Catch the small things only visible in the running app.

**Atomic steps:**
1. Live test on light theme.
2. Live test on dark theme. Check that the floating cluster's
   `backdrop-filter: blur(6px)` blends naturally on both backgrounds.
3. Resize the side panel narrow / wide. Confirm:
   - Floating cluster doesn't collide with composer textarea.
   - Welcome copy still readable / centered.
   - Long thread auto-scrolls without floating cluster jumping.
4. Tab order / focus rings: pressing Tab from composer should still
   reach the floating cluster's actions. Confirm no `tabIndex=-1`
   smuggled in by the refactor.
5. macOS reduced-motion / reduced-transparency settings. If the
   `backdrop-filter` looks broken with reduced transparency, fall back
   to a solid translucent background.
6. If anything in 1–5 looks off, adjust CSS in a follow-up edit. No
   React change should be needed.

**Exit criteria:** Visual review against expectations passes — one
"Maho AI" mention, no card outline, floating cluster usable in both
themes and at both widths.

## 6. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Floating cluster overlaps thread content and blocks clicks | Medium | Medium | Phase 2 step 7: add top padding to scroll region. Verify in Phase 3. |
| Removing `--maho-ai-bg-panel` use cascades into other components | Low | Low | The token is consumed elsewhere only in dev mode shells; check `grep -rn 'bg-panel'` before commit. |
| Card removal makes the panel content slam against host edges in unrelated layouts (e.g., `chrome-untrusted://maho-ai-standalone`) | Low | Low | The standalone harness uses the same panel CSS; verify the dev server still looks reasonable, or fall back to keeping a minimal padding. |
| `<CompactHeader>` rename breaks an unseen call site | Low | Low | grep `<CompactHeader` and `import.*CompactHeader` before commit. Refactor catches everything in one pass. |
| `backdrop-filter: blur(6px)` is GPU-expensive on certain hardware | Low | Low | Acceptable — the cluster is tiny (~96px wide); blur cost is negligible. Skip if profiling later disagrees. |

## 7. Verification strategy

1. **Build gate:** `python3 maho-chromium/build/scripts/build_maho.py` —
   must succeed. The plan changes are CSS + a tiny React rename; no
   risk of GN regen failure.
2. **Static lint:** `lsp_diagnostics` clean on all modified files.
3. **DOM assertions** via CDP on a fresh diag instance:
   - `document.querySelectorAll('.compact-panel__quiet-title').length === 0`
   - `document.querySelectorAll('.compact-panel__floating-actions').length === 1`
   - One "Maho AI" occurrence in `document.body.innerText` on the empty
     state.
4. **Visual regression** (manual): screenshot before/after on both
   themes. Document the diff in the commit body.
5. **Functional regression**: send one prompt end-to-end; verify the
   conversation flow still works (no spurious overlays, no broken
   scroll).

## 8. Recommended execution order

```
Phase 1 — Card removal                  ← lowest risk, single-file CSS
Phase 2 — Header hide + floating actions ← needs build + DOM check
Phase 3 — Live polish                   ← live verification only
```

Phase 1 + 2 together fit in one commit because they share the same
visual goal and the same `compact-panel.css` file. Phase 3 is review,
not implementation, and may produce a follow-up tweak commit.

## 9. Honesty statement

This plan is intentionally small. It is a polish pass, not a redesign.
The two changes are independent and either could ship alone, but
together they realize a single user intent: make the panel feel like a
native side-panel surface (not a card-in-card) while removing
duplicated brand text. No data migration, no schema change, no Rust /
mojom touch.
