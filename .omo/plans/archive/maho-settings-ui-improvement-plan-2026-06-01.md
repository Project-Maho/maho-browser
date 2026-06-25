# Maho Settings Page — UI Improvement Plan

**Created:** 2026-06-01
**Status:** Approved / Ready to execute (4 phases)
**Scope:** `maho-chromium/browser/resources/maho_settings/`
**Domain:** visual-engineering
**Estimated effort:** ~7 dev-hours (4 phases) + ~30-60 min builds per phase
**User decisions baked in:**
- Save flow: **Option B — blur-to-save (auto-save)** with transient ✓ Saved indicator
- Combobox dependency: **Radix UI** (`@radix-ui/react-popover` + `cmdk` for command/search)
- Phase 4 (pane-specific polish): **included**
- Phasing: ship phase-by-phase after approval (this plan IS the approval gate)

---

## 0 — Background

User reported three concrete UI issues in the Settings page (specifically the Maho AI pane), then asked to expand into a holistic settings page UI improvement plan:

1. Provider dropdown looks identical to a text field — users can't tell it opens a list
2. Save button design is unrefined and pushes the text input too narrow on the left
3. Toggle switches show redundant "On"/"Off" text labels

A code audit reveals deeper design-system inconsistencies behind these symptoms — they're not isolated bugs but consequences of a shallow component layer where one CSS class (`setting-switch`) is overloaded across distinct controls.

The current state has 67 CSS classes in a single `settings.css` file (847 lines), 1994 lines of `domain_panes.tsx` rendering all panes, and a partial design token system (`--maho-settings-space-*`, `--maho-settings-radius-*`) that is applied inconsistently.

---

## 1 — Current State Audit

### 1.1 Component Inventory

| Component | Source | Current CSS class | Purpose |
|---|---|---|---|
| `ActionButton` | `domain_panes.tsx:426` | `setting-switch` | Save / Remove / generic action buttons |
| `AsyncToggleButton` | `domain_panes.tsx:462` | `setting-switch` + `is-on` | Real on/off toggle (e.g., Session Persistence) |
| `TextFieldShell` | `domain_panes.tsx:521` | `setting-input-shell` | Wraps `<input>` with border + padding |
| `AiSelectRow` | `domain_panes.tsx:900` | `setting-input-shell setting-input-shell--select` | Wraps native `<select>` (just patched) |
| `AiFieldRow` | `domain_panes.tsx:878` | (uses TextFieldShell + ActionButton) | Text input + inline Save |

### 1.2 Issue Catalog (User-Reported + Discovered)

#### MEDIUM — User #1 — Dropdown affordance unclear (`AiSelectRow`)
- After my recent fix, the select uses `.setting-input-shell--select` with a 12px chevron `::after`. Visually still reads as a text field because:
  - Same border, same background, same min-height as text inputs
  - Chevron is small (12px) and same color as muted text
  - No hover state distinct from text input focus
- **Symptom**: User can't distinguish Provider field from Base URL/API Key/Model fields

#### MEDIUM — User #2 — Save button visual + layout
- `ActionButton` reuses `setting-switch` CSS (line 441) — a class designed for toggle switches with track+thumb. The button gets an opaque flat look that doesn't communicate "primary action".
- After CSS fix flex: `1 1 0` on input shell + 8px gap + button: input shrinks more than necessary because the button's `setting-switch` styling is `width: 100%` for switches. Even with the gap, the button takes too little space (no min-width) and the input swallows leftover.
- **Symptom**: Text input shrinks to ~75% of available width, Save button still cramped and looks like an inline tag

#### LOW — User #3 — Toggle "On"/"Off" text labels (`AsyncToggleButton`)
- `<span className="setting-switch__status">{enabled ? 'On' : 'Off'}</span>` at line 497 — visually redundant since the toggle thumb position already communicates state.
- Apple HIG, Material 3, and most modern design systems omit these labels.
- **Symptom**: Visual noise next to a self-explanatory control

#### MEDIUM — Discovered #4 — `ActionButton` ↔ `AsyncToggleButton` class collision
- Both use `setting-switch` as the root class. `AsyncToggleButton` adds `is-on` modifier. `ActionButton` doesn't.
- Result: Save buttons inherit toggle CSS (flex layout, justify-content: flex-end, gap: 12px for track+thumb) — none of which makes sense for a Save button.
- This is the root cause of #2 — Save needs its own component class, not a switch class.

#### LOW — Discovered #5 — Inconsistent input row layouts
- AI pane: input + inline Save button (auto-saves on commit)
- Other panes (Privacy, Browsing): toggle switches, segmented controls, single inputs
- The "input + Save" pattern is unique to AI pane and isn't visually distinguished — looks crammed compared to other pane rows.

#### LOW — Discovered #6 — Description text always rendered, even when empty
- `ManagedSettingRow description="" title={label}` (line 893) renders an empty `<p className="setting-row__description"></p>` element.
- Empty paragraphs occupy vertical space (line-height) → visual gap between title and control inconsistent across rows.

#### LOW — Discovered #7 — Native `<option>` styling can't be perfected
- We added `option { background: #1c1f29; color: #...; }` but macOS native dropdowns ignore option-level CSS and render system style.
- For 137 providers, native select with no search is poor UX. Solution requires custom Combobox component (Phase 3 in this plan).

#### INFO — Discovered #8 — `setting-row__description` always renders
- The element exists even with empty string content. Should conditionally render OR collapse with CSS `:empty { display: none; }`.

#### INFO — Discovered #9 — Design tokens partially adopted
- `--maho-settings-space-*` exists (1, 2, 3, 4, 5, 6, 7, 8) but many CSS rules use raw values: `padding: 0 12px`, `gap: 16px`, `min-height: 44px`, etc.
- `--maho-settings-radius-*` (sm: 12, md: 18, lg: 24, pill: 999) — used in some places, raw values elsewhere.
- Migration to tokens would make subsequent design refinements cheaper.

---

## 2 — Design Principles

These principles guide the redesign. Every component decision must satisfy at least one.

1. **Affordance > Aesthetics** — Visual difference between controls (input, dropdown, button, toggle) must be immediate. Same shape ≠ same role.
2. **Consistent vertical rhythm** — Every row has predictable height; sub-elements align across rows.
3. **One class, one purpose** — `setting-switch` is for toggles only. Buttons get `setting-button`. Dropdowns get `setting-dropdown`.
4. **Tokens over magic numbers** — Spacing, radius, color, transition use CSS variables.
5. **Native fallback acceptable in Phase 1** — Native `<select>` is OK for now; custom Combobox is Phase 3 enhancement.
6. **No noise** — Empty descriptions, redundant on/off labels, low-information chrome get removed.

---

## 3 — Phase 1 — Component-Level Fixes (Quick Wins, ~2h)

Targets the three user-reported issues + the `ActionButton` class collision. Self-contained CSS + minimal TSX changes. No mojom / build-system impact.

### 3.1 Fix `AsyncToggleButton` — Remove On/Off label

**File**: `domain_panes.tsx:462-499`

```diff
   return (
     <Button
       aria-checked={enabled}
       aria-label={label}
       className={joinClassNames('setting-switch', enabled ? 'is-on' : undefined)}
       ...>
       <span className="setting-switch__track">
         <span className="setting-switch__thumb" />
       </span>
-      <span className="setting-switch__status">{enabled ? 'On' : 'Off'}</span>
     </Button>
   );
```

Accessibility preserved: `aria-checked={enabled}` + `role="switch"` already conveys state to assistive tech.

**File**: `settings.css` — keep `.setting-switch__status` rule but mark unused (or delete after grep confirms zero callers).

**Acceptance**:
- [ ] Toggle row shows track + thumb only, no "On"/"Off" text
- [ ] VoiceOver announces state correctly via `aria-checked`

### 3.2 Decouple `ActionButton` from `setting-switch`

**File**: `domain_panes.tsx:426-460`

```diff
 function ActionButton({...}) {
   return (
     <Button
       ...
-      className={joinClassNames('setting-switch', className)}
+      className={joinClassNames('setting-button', className)}
       ...>
       {label}
     </Button>
   );
 }
```

**File**: `settings.css` — add new `.setting-button` rule:

```css
.setting-button {
  align-items: center;
  appearance: none;
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.10);
  border-radius: var(--maho-settings-radius-sm);
  color: var(--maho-settings-text-primary);
  cursor: pointer;
  display: inline-flex;
  flex: 0 0 auto;
  font: inherit;
  font-weight: 500;
  justify-content: center;
  min-height: 36px;
  min-width: 64px;
  padding: 0 var(--maho-settings-space-3);
  transition: background var(--maho-settings-transition),
              border-color var(--maho-settings-transition);
}

.setting-button:hover:not(:disabled) {
  background: rgba(255, 255, 255, 0.12);
  border-color: rgba(255, 255, 255, 0.16);
}

.setting-button:active:not(:disabled) {
  background: rgba(255, 255, 255, 0.06);
}

.setting-button:disabled {
  cursor: default;
  opacity: 0.42;
}

.setting-button--primary {
  background: var(--maho-settings-accent);
  border-color: transparent;
  color: #0a0c14;
  font-weight: 600;
}

.setting-button--primary:hover:not(:disabled) {
  background: rgba(155, 178, 255, 0.92);
}
```

**Acceptance**:
- [ ] Save button has distinct chip/button shape, not a switch
- [ ] Hover/active states clear
- [ ] `setting-switch` no longer applies to Save buttons (grep confirms only `AsyncToggleButton` uses it)

### 3.3 Strengthen dropdown affordance

**File**: `settings.css` — modify `.setting-input-shell--select`:

```css
.setting-input-shell--select {
  background: linear-gradient(180deg,
      rgba(255, 255, 255, 0.04),
      rgba(255, 255, 255, 0.02)),
      var(--maho-settings-input);
  cursor: pointer;
  padding-right: 38px;
  position: relative;
}

.setting-input-shell--select::before {
  background: rgba(255, 255, 255, 0.04);
  border-left: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 0 12px 12px 0;
  bottom: 0;
  content: '';
  pointer-events: none;
  position: absolute;
  right: 0;
  top: 0;
  width: 32px;
}

.setting-input-shell--select::after {
  /* keep existing chevron, but make it 14px and accent color */
  width: 14px;
  height: 14px;
  background-size: 14px 14px;
  color: var(--maho-settings-text-primary);
}

.setting-input-shell--select:hover {
  border-color: rgba(255, 255, 255, 0.18);
}
```

The `::before` pseudo creates a distinct chevron well on the right edge. This visually differentiates select from text input at a glance even at small sizes.

**Acceptance**:
- [ ] Dropdown's right edge has a visible "well" with chevron — clearly clickable area
- [ ] Hover highlights the dropdown distinctly from input fields

### 3.4 Fix input/button row balance

**File**: `settings.css`

```diff
 .setting-row__control {
   align-items: center;
   display: flex;
-  gap: 8px;
+  gap: var(--maho-settings-space-2);
   justify-content: stretch;
   min-width: 0;
   width: 100%;
 }
```

Already 8px (= space-2) but switch to token. Real fix is in 3.2 — `setting-button` has `flex: 0 0 auto` + `min-width: 64px` so input gets the rest.

**Acceptance**:
- [ ] Text input takes available width (no longer 75%)
- [ ] Save button readable, not truncated, not minimum-content

### 3.5 Drop empty description rendering

**File**: `domain_panes.tsx:502` — `ManagedSettingRow`

```diff
-      <div className="setting-row__copy">
-        <h4 className="setting-row__title">{title}</h4>
-        <p className="setting-row__description">{description}</p>
-      </div>
+      <div className="setting-row__copy">
+        <h4 className="setting-row__title">{title}</h4>
+        {description ? (
+          <p className="setting-row__description">{description}</p>
+        ) : null}
+      </div>
```

**Acceptance**:
- [ ] Rows without descriptions have tighter title→control spacing
- [ ] DOM no longer has empty `<p>` elements

### 3.6 Phase 1 verification

```bash
cd maho-chromium/browser/resources/maho_settings && bunx tsc --noEmit ...
python3 maho-chromium/build/scripts/build_maho.py
```

Manual QA: open `chrome://maho-settings/?pane=maho-ai`:
- [ ] Provider dropdown visually distinct (chevron well + hover)
- [ ] Save buttons rendered with new `setting-button` style (not switch)
- [ ] Session Persistence toggle has no "On"/"Off" text
- [ ] Description-less rows are tighter

---

## 4 — Phase 2 — Blur-to-Save + Token Hardening (~2h)

### 4.1 Auto-save (blur-to-commit) flow

User decision: **Option B**. Eliminate explicit Save buttons in the AI pane; commit on blur with a transient ✓ Saved indicator.

**File**: `domain_panes.tsx` — replace `AiFieldRow` (line 878):

```tsx
function AiFieldRow(
    {initialValue, label, onCommit, placeholder, type = 'text'}: {
      initialValue: string;
      label: string;
      onCommit: (value: string) => Promise<void>;
      placeholder?: string;
      type?: string;
    }) {
  const [draft, setDraft] = useState(initialValue);
  const [savedAt, setSavedAt] = useState<number | null>(null);
  const [isSaving, setIsSaving] = useState(false);
  const mountedRef = useMountedRef();

  useEffect(() => {
    setDraft(initialValue);
  }, [initialValue]);

  const commitIfDirty = useCallback(async () => {
    if (draft === initialValue || isSaving) {
      return;
    }
    setIsSaving(true);
    try {
      await onCommit(draft);
      if (mountedRef.current) {
        setSavedAt(Date.now());
      }
    } finally {
      if (mountedRef.current) {
        setIsSaving(false);
      }
    }
  }, [draft, initialValue, isSaving, onCommit, mountedRef]);

  // Auto-fade ✓ Saved after 2.4s.
  useEffect(() => {
    if (savedAt == null) return;
    const timer = setTimeout(() => {
      if (mountedRef.current) setSavedAt(null);
    }, 2400);
    return () => clearTimeout(timer);
  }, [savedAt, mountedRef]);

  const showSavedIndicator = savedAt != null;

  return (
    <ManagedSettingRow description="" title={label}>
      <TextFieldShell
        ariaLabel={label}
        placeholder={placeholder ?? label}
        type={type}
        value={draft}
        onChange={setDraft}
        onBlur={() => { void commitIfDirty(); }}
        onKeyDown={e => {
          if (e.key === 'Enter') {
            e.currentTarget.blur();
          } else if (e.key === 'Escape') {
            setDraft(initialValue);
            e.currentTarget.blur();
          }
        }}
      />
      <span
        className={joinClassNames(
            'setting-saved-indicator',
            showSavedIndicator && 'setting-saved-indicator--visible',
            isSaving && 'setting-saved-indicator--saving')}
        aria-live="polite">
        {isSaving ? 'Saving…' : showSavedIndicator ? '✓ Saved' : ''}
      </span>
    </ManagedSettingRow>
  );
}
```

`TextFieldShell` extended to accept `onBlur` + `onKeyDown` props (forward to native `<input>`).

The `setting-button` from Phase 1 remains for genuine button actions (e.g., "Remove rule", "Add space") in other panes — only the AI pane drops inline Save.

### 4.2 New CSS for saved indicator

**File**: `settings.css`

```css
.setting-saved-indicator {
  align-items: center;
  color: var(--maho-settings-text-muted);
  display: inline-flex;
  flex: 0 0 auto;
  font-size: 12px;
  font-weight: 500;
  min-width: 56px;
  opacity: 0;
  transition: opacity 200ms ease;
}

.setting-saved-indicator--visible {
  color: var(--maho-settings-success, #6ed8a8);
  opacity: 1;
}

.setting-saved-indicator--saving {
  color: var(--maho-settings-text-muted);
  opacity: 1;
}
```

Add `--maho-settings-success: #6ed8a8;` token if not already present.

### 4.3 Confirmation for sensitive fields

API Key is the most sensitive auto-saved field. Add a per-row `requireConfirm` prop on `AiFieldRow` (default false). When true, blur-to-save triggers a small inline confirmation chip "Save changes?" with [Save] [Cancel] buttons.

```tsx
<AiFieldRow
  label="API Key"
  initialValue={state.data.apiKey}
  onCommit={value => commit(async () => handler.setAIApiKey(value))}
  placeholder="Paste your API key"
  type="password"
  requireConfirm   // ← only this field uses explicit confirm
/>
```

Implementation: when `requireConfirm` and dirty, blur shows confirm chip instead of auto-committing. Provides safety net for the most cost-sensitive field.

### 4.4 CSS token migration

Replace remaining magic numbers throughout `settings.css`. New tokens:

```css
--maho-settings-control-height: 44px;
--maho-settings-control-height-sm: 36px;
--maho-settings-button-radius: 12px;
--maho-settings-input-radius: 12px;
--maho-settings-bg-button: rgba(255, 255, 255, 0.08);
--maho-settings-bg-button-hover: rgba(255, 255, 255, 0.12);
--maho-settings-bg-button-primary: var(--maho-settings-accent);
--maho-settings-success: #6ed8a8;
```

Migrate via grep + replace:
- `padding: 12px 0;` → `padding: var(--maho-settings-space-3) 0;`
- `gap: 16px;` → `gap: var(--maho-settings-space-4);`
- `min-height: 44px;` → `min-height: var(--maho-settings-control-height);`
- `border-radius: 12px;` → `border-radius: var(--maho-settings-input-radius);`

### 4.5 Consistent vertical rhythm

```css
.setting-row {
  align-items: center;
  background: transparent;
  border-top: 1px solid var(--maho-settings-border);
  display: grid;
  gap: var(--maho-settings-space-4);
  grid-template-columns: minmax(0, 1fr) minmax(280px, 360px);
  min-height: 56px;
  padding: var(--maho-settings-space-3) 0;
}
```

Locked min-height aligns rows in panes with mixed control sizes (text input vs toggle vs segmented).

### 4.6 Phase 2 acceptance

- [ ] AI pane fields auto-save on blur — no Save buttons (except API Key with confirm chip)
- [ ] ✓ Saved indicator appears after commit, fades after 2.4s
- [ ] Saving… indicator appears while RPC in flight
- [ ] Enter key in input blurs/commits; Escape reverts to initialValue
- [ ] API Key field shows "Save changes?" chip on blur if dirty
- [ ] All raw spacing/radius values in `settings.css` migrated to tokens (grep `padding: \d+px` returns near-zero matches)
- [ ] Settings rows have consistent 56px min-height

---

## 5 — Phase 3 — Searchable Combobox via Radix UI (~2h)

Native `<select>` with 137 options is unusable. Build a searchable Combobox using Radix UI primitives (already in `maho_ai/` via `@radix-ui/react-popover`).

### 5.1 Dependency setup

User decision: **Radix UI**. Add new dependencies to `maho_settings`:

```bash
# In maho_settings react package — pin versions matching maho_ai's existing Radix version.
# Add to maho_settings/react/package.json (or similar manifest):
#   @radix-ui/react-popover: ^1.x  (already used in maho_ai)
#   cmdk: ^1.x                      (Combobox/command palette primitive)
```

Verify `maho_ai` already vendored these; if so, no new download — just add to `maho_settings` deps.

### 5.2 Component spec

New file: `maho-chromium/browser/resources/maho_settings/react/ui/combobox.tsx`

```tsx
import * as Popover from '@radix-ui/react-popover';
import {Command} from 'cmdk';
import {useState} from 'react';

export interface ComboboxOption {
  value: string;
  label: string;
}

export interface ComboboxProps {
  ariaLabel: string;
  options: ComboboxOption[];
  placeholder?: string;
  value: string;
  onCommit: (value: string) => void | Promise<void>;
}

export function Combobox(
    {ariaLabel, options, placeholder, value, onCommit}: ComboboxProps) {
  const [open, setOpen] = useState(false);
  const selected = options.find(o => o.value === value);
  const triggerLabel = selected?.label ?? value ?? placeholder ?? '';

  return (
    <Popover.Root open={open} onOpenChange={setOpen}>
      <Popover.Trigger asChild>
        <button
          aria-label={ariaLabel}
          className="setting-combobox-trigger"
          type="button">
          <span className="setting-combobox-trigger__label">
            {triggerLabel}
          </span>
          <span className="setting-combobox-trigger__chevron" aria-hidden>▾</span>
        </button>
      </Popover.Trigger>
      <Popover.Portal>
        <Popover.Content
          align="start"
          className="setting-combobox-panel"
          sideOffset={4}
          collisionPadding={12}>
          <Command className="setting-combobox-command" loop>
            <Command.Input
              autoFocus
              className="setting-combobox-search"
              placeholder="Search…"
            />
            <Command.List className="setting-combobox-list">
              <Command.Empty className="setting-combobox-empty">
                No matches
              </Command.Empty>
              {options.map(o => (
                <Command.Item
                  key={o.value}
                  value={`${o.label} ${o.value}`}
                  className="setting-combobox-item"
                  onSelect={() => {
                    void onCommit(o.value);
                    setOpen(false);
                  }}>
                  {o.label}
                </Command.Item>
              ))}
            </Command.List>
          </Command>
        </Popover.Content>
      </Popover.Portal>
    </Popover.Root>
  );
}
```

### 5.3 Replace `AiSelectRow` to use Combobox

```tsx
function AiSelectRow(
    {initialValue, label, onCommit, options}: { ... }) {
  return (
    <ManagedSettingRow description="" title={label}>
      <Combobox
        ariaLabel={label}
        options={options}
        placeholder="Select…"
        value={initialValue}
        onCommit={async value => { await onCommit(value); }}
      />
    </ManagedSettingRow>
  );
}
```

The unknown-value preservation logic (Phase 1's `(custom)` suffix) is no longer needed — the Combobox displays whatever string `value` is, even if not in options list.

### 5.4 CSS

Add to `settings.css`:

```css
.setting-combobox-trigger {
  align-items: center;
  appearance: none;
  background: var(--maho-settings-input);
  border: 1px solid rgba(255, 255, 255, 0.09);
  border-radius: var(--maho-settings-input-radius);
  color: var(--maho-settings-text-primary);
  cursor: pointer;
  display: inline-flex;
  flex: 1 1 0;
  font: inherit;
  justify-content: space-between;
  min-height: var(--maho-settings-control-height);
  min-width: 0;
  padding: 0 var(--maho-settings-space-3);
  text-align: left;
  transition: background var(--maho-settings-transition),
              border-color var(--maho-settings-transition);
}

.setting-combobox-trigger:hover {
  background: var(--maho-settings-input-focus);
  border-color: rgba(255, 255, 255, 0.18);
}

.setting-combobox-trigger[data-state='open'] {
  border-color: var(--maho-settings-accent);
  box-shadow: 0 0 0 1px rgba(145, 169, 255, 0.3);
}

.setting-combobox-trigger__label {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.setting-combobox-trigger__chevron {
  color: var(--maho-settings-text-muted);
  flex: 0 0 auto;
  font-size: 14px;
  margin-left: var(--maho-settings-space-2);
}

.setting-combobox-panel {
  background: rgba(28, 32, 42, 0.96);
  backdrop-filter: blur(50px) saturate(180%);
  border: 1px solid rgba(255, 255, 255, 0.10);
  border-radius: var(--maho-settings-radius-md);
  box-shadow: 0 16px 48px rgba(0, 0, 0, 0.32);
  display: flex;
  flex-direction: column;
  max-height: 320px;
  min-width: var(--radix-popover-trigger-width);
  overflow: hidden;
  z-index: 100;
}

.setting-combobox-search {
  appearance: none;
  background: transparent;
  border: none;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  color: var(--maho-settings-text-primary);
  font: inherit;
  outline: none;
  padding: var(--maho-settings-space-3);
}

.setting-combobox-list {
  max-height: 260px;
  overflow-y: auto;
  padding: var(--maho-settings-space-1);
}

.setting-combobox-item {
  align-items: center;
  border-radius: 8px;
  color: var(--maho-settings-text-secondary);
  cursor: pointer;
  display: flex;
  font-size: 13px;
  min-height: 32px;
  padding: 0 var(--maho-settings-space-3);
  transition: background var(--maho-settings-transition);
}

.setting-combobox-item[data-selected='true'],
.setting-combobox-item:hover {
  background: rgba(255, 255, 255, 0.08);
  color: var(--maho-settings-text-primary);
}

.setting-combobox-empty {
  color: var(--maho-settings-text-muted);
  font-size: 13px;
  padding: var(--maho-settings-space-4);
  text-align: center;
}
```

### 5.5 Backend adjustments

- `GetAIProviderList` already returns full list with display_name — no backend change
- Filtering done client-side via `cmdk` (137 items × ~30 chars = trivial, no virtualization needed)

### 5.6 Acceptance

- [ ] Provider field opens custom Radix popover (not native macOS dropdown)
- [ ] Type to filter; cmdk handles fuzzy matching by `label` + `value`
- [ ] Arrow keys navigate items, Enter commits, Escape closes
- [ ] Selected provider's display_name shown in trigger
- [ ] Triggers focus-style same as text input focus (consistent affordance)
- [ ] No native dropdown (`<select>`) anywhere in AI pane
- [ ] List virtualized only if >200 items (137 doesn't need it)

---

## 6 — Phase 4 — Pane-Specific Polish (~1h)

User decision: **included**. Final polish pass after Phases 1-3 land.

### 6.1 Maho AI pane

#### 6.1.1 Field grouping

Group related fields with subtle separators:

```
┌─ Provider & Model ────────────────────────────────────────┐
│  Provider     [GitHub Copilot                ▼]           │
│  Model        [gpt-5.5                          ]         │
│  Base URL     [(provider default)               ]         │
└────────────────────────────────────────────────────────────┘

┌─ Authentication ──────────────────────────────────────────┐
│  API Key      [●●●●●●●●                         ]  [Save]│
└────────────────────────────────────────────────────────────┘

┌─ Behavior ─────────────────────────────────────────────────┐
│  Approval Policy     [prompt                    ]         │
│  Session Persistence  ───●                                 │
└────────────────────────────────────────────────────────────┘
```

Implementation: introduce `<SectionDivider title="…">` component that renders a small uppercase label + thin separator line above a row group.

```tsx
<SectionDivider title="Provider & Model" />
<AiSelectRow label="Provider" .../>
<AiFieldRow label="Model" .../>
<AiFieldRow label="Base URL" .../>

<SectionDivider title="Authentication" />
<AiFieldRow label="API Key" requireConfirm .../>

<SectionDivider title="Behavior" />
<AiFieldRow label="Approval Policy" .../>
<AiToggleRow label="Session Persistence" .../>
```

CSS:
```css
.setting-section-divider {
  color: var(--maho-settings-text-muted);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  margin-top: var(--maho-settings-space-5);
  padding-bottom: var(--maho-settings-space-2);
  text-transform: uppercase;
}

.setting-section-divider:first-child {
  margin-top: 0;
}
```

#### 6.1.2 Connection status pill

Show OpenCode connection status above field groups:

```tsx
<div className="setting-status-pill" data-state={connectionState}>
  <span className="setting-status-pill__dot" />
  <span className="setting-status-pill__label">
    {connectionState === 'connected' ? 'Connected to OpenCode' : ...}
  </span>
</div>
```

States: `connected` (green dot), `connecting` (amber pulsing), `disconnected` (red), `error` (red + icon).

Wire to `MahoAiRuntimeAdapter::GetActiveAdapter` via existing `GetConnectionState` Mojo RPC (already exists in maho_ai mojom — needs cross-WebUI proxy or duplicate RPC in maho_settings).

**Out of scope for Phase 4**: full cross-WebUI plumbing. Phase 4 just ships the static `<SectionDivider>` grouping + a placeholder pill that reads from `prefs_->GetBoolean(ai::kOpenCodeRuntimeEnabled)`. Real connection state in a follow-up.

### 6.2 Other panes audit

#### 6.2.1 Toggle audits
Grep all `AsyncToggleButton` usage:
- Maho AI pane (already covered Phase 1)
- Privacy pane (Tracking Protection, etc.)
- Browsing pane
- Identity pane

Confirm none have other text labels that should be removed (e.g., "Enabled / Disabled" beside toggle).

#### 6.2.2 Sidebar active state

Current `.sidebar-nav-button.is-active`:
```css
.sidebar-nav-button.is-active {
  background: rgba(255, 255, 255, 0.04);
  color: var(--maho-settings-text-primary);
}
```

Subtle. Strengthen:
```css
.sidebar-nav-button.is-active {
  background: var(--maho-settings-accent-bg, rgba(155, 178, 255, 0.10));
  border-radius: var(--maho-settings-sidebar-item-radius);
  color: var(--maho-settings-text-primary);
}

.sidebar-nav-button.is-active::before {
  background: var(--maho-settings-accent);
  border-radius: 2px;
  bottom: 8px;
  content: '';
  left: 0;
  position: absolute;
  top: 8px;
  width: 3px;
}
```

Adds a left-edge accent bar — consistent with macOS System Settings active state.

#### 6.2.3 Domain group headers

Sidebar nav currently lists all panes flat. The schema defines domains (browsing, look-and-feel, identity, productivity). Render as collapsible group headers:

```
PRODUCTIVITY
  Maho AI
  Shortcuts
  ATC Rules

LOOK & FEEL
  Theme
  Sidebar

IDENTITY
  Profile
  Saved Passwords
```

Already has `.sidebar-domain-label` CSS — verify it's applied.

### 6.3 Phase 4 acceptance

- [ ] Maho AI pane has 3 section dividers (Provider & Model / Authentication / Behavior)
- [ ] Static connection pill rendered (real connection state deferred)
- [ ] No remaining "On"/"Off" or "Enabled"/"Disabled" text labels on any toggle
- [ ] Sidebar active state has accent bar on left edge
- [ ] Domain headers visible in sidebar (if not already)

---

## 7 — Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Phase 1 CSS changes affect non-AI panes (other panes use `ActionButton`) | Grep all `ActionButton` callers; rename root class only after audit |
| Token migration regresses existing rules with raw values | Add tokens additively; don't remove existing rules until visually verified |
| Custom Combobox accessibility regressions vs native select | Use Radix (handles a11y) OR test with VoiceOver before ship |
| Save flow change (Phase 2) breaks existing AI pane behavior | Feature flag the new flow; fallback to current inline Save |
| Build cascade rebuilds (we hit this earlier) | Keep CSS changes only when possible; React component changes minimal |

---

## 8 — Phase Sequencing & Time

| Phase | Task | Effort | Dependency |
|---|---|---|---|
| 1 | Component-level fixes (3 user issues + ActionButton decoupling + empty desc) | ~2h | None |
| 2 | Layout (input+save flow) + token migration | ~2h | Phase 1 |
| 3 | Searchable Combobox for providers | ~2h | Phase 1 (button styles); independent of Phase 2 |
| 4 | Pane-specific polish | ~1h | Phases 1-3 |
| **Total** | | **~7h dev** + 30-60 min builds | |

Phases 1 and 3 can ship independently. Phase 2 should follow Phase 1.

---

## 9 — Acceptance (overall)

- [ ] Phase 1 user-reported issues all addressed: dropdown affordance, save button design, no on/off labels
- [ ] No `setting-switch` class on non-toggle elements
- [ ] All CSS uses tokens (post Phase 2)
- [ ] AI pane fields auto-save on blur with ✓ Saved indicator (Phase 2)
- [ ] Provider selection is searchable with Radix Combobox (Phase 3)
- [ ] AI pane has section dividers + connection pill (Phase 4)
- [ ] Sidebar active state has accent bar (Phase 4)
- [ ] Build green: `python3 maho-chromium/build/scripts/build_maho.py`
- [ ] No regressions in other panes (manual QA pass through Privacy / Browsing / Identity / Productivity / Look & Feel)

---

**End of plan.**
