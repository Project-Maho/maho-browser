# Maho Boost React Visual Contract

Status: binding React/Tailwind visual contract for `chrome://maho-boost`. React, Tailwind v4, the current WebUI loader, the standalone content-script boundary, and CodeMirror IIFE remain the production architecture. The required outcome is the original compact Boost visual and interaction result, as defined by the normalized Arc references and committed legacy UI anatomy. This is not a redesign, not a rollback instruction, and not permission to reinterpret the UI through generic shadcn/dashboard styling.

Authority order:

1. The Boost Mojo schema, native window controller, interactive tests, and preservation plan are authoritative for behavior, lifecycle, fixed geometry, load order, IDs/classes, and observable state.
2. The normalized reference artifacts under `.omo/evidence/boost-ui-functional-coverage/reference/` are the binding visual authority for rendered pixels. Boost mode compares against `boost-184x582.png`; Code mode compares against `code-452x582.png` after retaining `code-451x582-unpadded.png` as the logical downsample artifact.
3. The raw sources `maho/reference_screenshots/arc-boost.png` and `maho/reference_screenshots/arc-boost-code.png`, plus the committed legacy `boost.html`, `boost.css`, `zen-advanced-color-options.css`, and pre-React `app.ts`, are authoritative for component anatomy, control order, labels, interaction states, and visual state outcomes.
4. Shared Maho theme tokens, `@theme/apply_theme`, `@ui/*` primitives, and `@icons/lucide` are implementation infrastructure. Use them when they preserve the reference result; they do not authorize visual reinterpretation, softened density, generic card layouts, alternate spacing, alternate state colors, or hidden compatibility DOM.

The implementation must preserve the untrusted WebUI and standalone content-script boundaries. It must not change the Mojo schema, generated bindings, native lifecycle, CodeMirror IIFE, or fixed window modes. It must not add a second component library, responsive/mobile layouts, speculative React tooling, hidden compatibility DOM, a rollback to the monolithic legacy UI, or a new design language.

## 0. Todo 1 Binding Reference Packet

Evidence root: `.omo/evidence/boost-ui-functional-coverage/`.

### Deterministic normalization

| Source | Raw dimensions | Raw SHA-256 | PNG signature | Transform | Artifact | Final dimensions | Final SHA-256 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `maho/reference_screenshots/arc-boost.png` | `187x590`, 8-bit RGBA | `bb20e4284110a825e2764ec57a19b00d5ed1ebbbc1143da7992cdcc0abfe8026` | `89504e470d0a1a0a` | Crop WebUI content rectangle `x=3,y=0,w=184,h=582`; no rescale | `.omo/evidence/boost-ui-functional-coverage/reference/boost-184x582.png` | `184x582`, 8-bit RGBA | `a6271d346e1fd91468dff81e2733931d221bea47f96c9bbf368e85c586553c3c` |
| `.omo/evidence/boost-ui-functional-coverage/reference/boost-184x582.png` | `184x582`, 8-bit RGBA | `a6271d346e1fd91468dff81e2733931d221bea47f96c9bbf368e85c586553c3c` | `89504e470d0a1a0a` | Byte-preserving copy for reference-only self diff | `.omo/evidence/boost-ui-functional-coverage/reference/boost-184x582-copy.png` | `184x582`, 8-bit RGBA | `a6271d346e1fd91468dff81e2733931d221bea47f96c9bbf368e85c586553c3c` |
| `maho/reference_screenshots/arc-boost-code.png` | `902x1164`, 8-bit RGBA | `9ba6449c96af4eee10b47d032df97bed1009ee040937502d45a7f80a189777f5` | `89504e470d0a1a0a` | Lanczos downsample exactly `2x` to the logical image; retain before padding | `.omo/evidence/boost-ui-functional-coverage/reference/code-451x582-unpadded.png` | `451x582`, 8-bit RGBA | `76c18dda7f1be4c79b7bf9ce257fca541af67f69e97fe99ddfbdd9551f210c3a` |
| `.omo/evidence/boost-ui-functional-coverage/reference/code-451x582-unpadded.png` | `451x582`, 8-bit RGBA | `76c18dda7f1be4c79b7bf9ce257fca541af67f69e97fe99ddfbdd9551f210c3a` | `89504e470d0a1a0a` | Append one copy of the rightmost `1x582` background column to reach native Code width | `.omo/evidence/boost-ui-functional-coverage/reference/code-452x582.png` | `452x582`, 8-bit RGBA | `0ca2ae31acc01f199ac1ad6d723c7bb15f7e6dd8399c4c71776fe86e41d07673` |

Reference-only QA artifacts:

| Check | Command artifact | Required outcome | Recorded outcome |
| --- | --- | --- | --- |
| Normalized Boost self-diff | `.omo/evidence/boost-ui-functional-coverage/reference/boost-self-diff.json` | `dimensionsMatch=true`, `diffRatio=0`, `similarityScore=100`, `alphaChannelIntact=true` | Satisfied |
| Raw Boost against normalized Boost failure scenario | `.omo/evidence/boost-ui-functional-coverage/reference/boost-raw-vs-normalized-diff.json` | `dimensionsMatch=false` | Satisfied; raw is `187x590`, normalized is `184x582` |
| Normalized Code self-diff | `.omo/evidence/boost-ui-functional-coverage/reference/code-self-diff.json` | `dimensionsMatch=true`, `diffRatio=0`, `similarityScore=100`, `alphaChannelIntact=true` | Satisfied |

### Non-negotiable invariants

| Invariant | Contract |
| --- | --- |
| Production architecture | React/Tailwind stays production. Do not restore the monolithic legacy `boost.html`, `boost.css`, or pre-React `app.ts` as the implementation path. |
| Visual authority | The original legacy UI and normalized screenshots determine the visual and interaction outcome. Shared tokens and primitives are tools for encoding that outcome, not permission to redesign it. |
| Geometry | Boost mode is exactly `184x582`; Code mode is exactly `452x582`. There is no responsive, mobile, tablet, fluid, or alternate desktop layout. |
| Reference comparison | Compare actual Boost captures only to `boost-184x582.png`; compare actual Code captures only to `code-452x582.png`. Raw screenshots are source inputs, not final comparison targets. |
| Code normalization | The Code reference must preserve both `code-451x582-unpadded.png` and `code-452x582.png`; the final 452px artifact is the comparison target, and the unpadded artifact documents the exact 2x Lanczos downsample. |
| Interaction result | Control order, visible labels, menu/popover behavior, rename, color handles, font grid, Size/Case cycles, Zap, Code, picker, inspector, loading, error, disabled, selected, hover, focus-visible, active, and close semantics match the original UI outcome. |
| Live DOM | Every compatibility ID/class attaches to a visible, unique, state-bearing React DOM subtree. Hidden, duplicate, offscreen, inert, or compatibility-only nodes are prohibited. |
| No reinterpretation | No cards-as-layout, generic shadcn dashboard chrome, extra labels, product-wide color remapping, decorative motion, softer density, or redesigned hierarchy may replace the original compact Arc result. |
| Dirty worktree | Preserve every pre-existing dirty hunk. Todo 1 owns only this `DESIGN.md` update plus `.omo/evidence/boost-ui-functional-coverage/`. |
| Verification claim | Todo 1 normalizes reference evidence and binds the contract only. It does not build, run tests, launch Maho, or claim implementation parity. |

### Visual state and capture matrix

Every later implementation or QA lane must enumerate and capture the applicable entries below after its last source edit. A missing state is a failed scenario unless the user explicitly accepts it as out of scope.

| Surface | State or flow | Required capture size | Reference authority | Required evidence |
| --- | --- | --- | --- | --- |
| Boost | Rest/default loaded state | `184x582` | `boost-184x582.png`; legacy title/body anatomy | Fresh actual PNG and `image-diff` JSON against `boost-184x582.png` |
| Boost | Title strip close/name/shuffle hover, focus-visible, active, disabled/loading | `184x582` | Legacy `#zen-boost-head-wrapper`, `#zen-boost-close`, `#zen-boost-name`, `#zen-boost-shuffle` | Rest, hover/focus/active/disabled captures plus AX/focus evidence |
| Boost | Boost action menu open | `184x582` | Legacy name trigger and menu actions Rename/Shuffle/Reset/Import/Export/Delete | Menu-open capture, keyboard navigation evidence, focus return evidence |
| Boost | Inline rename editing, valid, invalid/empty, committing, cancelled | `184x582` | Legacy name area and lifecycle behavior | Rename-state captures and DOM/Mojo/core persistence evidence |
| Boost | Loading, success, warning, and error status | `184x582` | Legacy import/export/action status intent; Section 5 `StatusFeedback` | Stable-geometry captures and live region/alert evidence |
| Boost | Color wheel rest | `184x582` | Normalized wheel geometry and legacy square spectrum/dotted overlay | Fresh actual PNG and `image-diff` hotspot review |
| Boost | Primary handle drag/click/keyboard and focus-visible | `184x582` | Legacy primary dot size, border, color, arc/circle feedback | Drag rest/mid/settled captures and keyboard value evidence |
| Boost | Secondary handle drag/click/keyboard and focus-visible | `184x582` | Legacy secondary dot size, border, color, arc/circle feedback | Drag rest/mid/settled captures and keyboard value evidence |
| Boost | Magic Theme selected/unselected | `184x582` | Legacy `#zen-boost-magic-theme` placement and active visual | Selected/unselected capture plus `aria-pressed` evidence |
| Boost | Smart Invert selected/unselected | `184x582` | Legacy utility row active treatment | Selected/unselected capture plus target-page effect evidence |
| Boost | Advanced controls popover open/focus/disabled | `184x582` | Legacy Controls trigger and advanced panel fields | Open capture, collision/bounds evidence, slider focus evidence |
| Boost | Disable color adjustments selected/unselected | `184x582` | Legacy disabled/grayscale treatment and state semantics | Selected/unselected capture plus target-page removal evidence |
| Boost | Font panel default | `184x582` | Legacy elevated white panel, five-column `Aa` grid, separator, select | Fresh actual PNG and grid geometry evidence |
| Boost | Font selected, hover, focus-visible, disabled/loading, font-select value | `184x582` | Legacy `.zen-boost-font-button-active`, select placement | State captures plus selected programmatic state evidence |
| Boost | Size default and every cycle value | `184x582` | Legacy Size row order and mode visual result | Per-value capture and Mojo/core/target-page state evidence |
| Boost | Case default and every cycle value | `184x582` | Legacy Case row order and mode visual result | Per-value capture and Mojo/core/target-page state evidence |
| Boost | Zap inactive, active, loading enter/exit, persisted count, empty count, error | `184x582` | Legacy full-width Zap row, icon, label, trailing count | State captures and four-boundary Zap evidence |
| Boost | Code button hover/focus/active and mode switch | `184x582` before switch; `452x582` after switch | Legacy full-width Code row and native resize | Before/after captures, mode attribute, resize request, focus transfer evidence |
| Code | Rest/default loaded state | `452x582` | `code-452x582.png`; legacy top bar/editor/bottom bar | Fresh actual PNG and `image-diff` JSON against `code-452x582.png` |
| Code | Back hover, focus-visible, active, disabled/loading | `452x582` | Legacy `#zen-boost-back` in 40px top bar | State captures plus focus return evidence |
| Code | CodeMirror mounted/rest/focused | `452x582` | Legacy `#custom-css-editor`, `.cm-editor`, `.cm-content` | Mounted/focused captures and live editor DOM evidence |
| Code | CodeMirror edited/dirty/saving/persisted/error | `452x582` | Legacy editor behavior plus Mojo/core persistence | Edited-state captures, deterministic seam evidence, persisted reopen evidence |
| Code | Picker inactive/active/selection inserted/Escape exit | `452x582` plus target-page overlay where applicable | Legacy picker button, content-script selector flow | Code capture, target overlay capture, CodeMirror insertion evidence |
| Code | Inspector pending/success/failure | `452x582` | Legacy bottom inspector button | Pending-state capture and Mojo result evidence |
| Cross-surface | Close visible button, native X, `Ctrl/Meta+W`, unload/tab navigation | Current mode size | Legacy close lifecycle and native widget behavior | Commit/discard/delete evidence at editor, Mojo, core reopen, and target-page boundaries |

## 1. Atmosphere & Identity

Boost is a dense, direct site-editing utility. It feels like a small native inspector rather than a settings page, dashboard, or marketing surface: a narrow title strip, a single color workspace, a compact typography grid, short utility controls, and a dedicated Code route. The signature is the color wheel with two manipulable color handles inside a fixed-width operational panel. Every element must earn its limited space through an immediate editing action or an explicit state readout.

Identity rules:

- Preserve the existing compact operational language. Do not introduce cards-as-layout, a sidebar, mobile stacking, dashboard metrics, promotional copy, decorative gradients, glass effects, or ambient motion.
- The title strip, body controls, Code top/bottom bars, font grid, utility rows, and bounded advanced-color popover remain visually distinct through shared tonal surfaces, borders, and restrained elevation.
- The color wheel is a functional color model, not a product-brand gradient. Its spectrum and user-selected colors are data visualization and may remain chromatic; surrounding product chrome must use shared semantic tokens.
- Code mode is a focused editing workspace, not a second application. It shares the same native widget, lifecycle, status, and close semantics as Boost mode.
- Visible copy remains short and action-oriented: Boost name, Size/value, Case/mode, Zap/count, Code, Back, and explicit menu actions.
- The primary users include pointer users dragging color handles, keyboard-only users cycling every control, CodeMirror users editing CSS, and users relying on visible focus, status announcements, reduced motion, or high contrast.

## 2. Color & Token Mapping

Boost must call `applyTheme("auto")` through `@theme/apply_theme` during application initialization. Theme changes are resolved through `html[data-theme]`; Boost must not set `data-theme` directly or define an independent palette.

### Shared semantic mapping

| Boost role | Required shared token / utility | Existing grammar preserved |
| --- | --- | --- |
| Window and main canvas | `--color-background`, `bg-background` | Full fixed widget background |
| Main editor surface | `--color-card`, `bg-card`, `text-card-foreground` | Compact uninterrupted control canvas |
| Title strip and Code bars | `--color-secondary` or `--color-muted`, matching foreground | Tonal bar separated from editor body |
| Default utility control | `--color-secondary`, `text-secondary-foreground` | Legacy light-gray `.mod-button` hierarchy |
| Quiet/disabled surface | `--color-muted`, `text-muted-foreground` | Disabled color workspace, inactive helper text |
| Hover | `--color-surface-hover`, `bg-surface-hover` | Subtle feedback without a new accent color |
| Selected/toggled | `--color-surface-selected`, plus foreground and an explicit icon/text/state | Magic Theme, Smart Invert, Disable, active font, Zap/picker state |
| Primary emphasis when required by an existing shared primitive | `--color-primary`, `text-primary-foreground` | Focused or confirmed action only; not decoration |
| Popover/menu surface | `--color-popover`, `text-popover-foreground` | Advanced controls and Boost action menu |
| Borders/dividers | `--color-border`, `border-border` | Title/bar hairlines, font separator, overlay boundary |
| Input/slider boundary | `--color-input`, `border-input` | Rename field, font select, slider track/thumb boundary |
| Keyboard focus | `--color-ring`, `ring-ring` | Every interactive control and CodeMirror boundary |
| Destructive | `--color-destructive`, `text-destructive` | Delete Boost and destructive failures only |
| Success/status | `--color-success`, `text-success` | Confirmed export/import success where text is present |
| Warning | `--color-warning`, `text-warning` | Recoverable warnings only, never a mode decoration |

Color rules:

- Do not introduce generic product colors or product-wide Boost tokens. The normalized references and legacy CSS values are binding local extraction evidence when shared semantic tokens do not match the original visual outcome.
- Prefer shared semantic tokens when their rendered pixels match the normalized references. If they visibly diverge, preserve the extracted Boost-specific value inside the scoped React/Tailwind implementation and document it as local parity data, not as a new shared product token.
- Legacy raw values for light bars, buttons, borders, active controls, font selection, shadows, and the advanced-color panel are future Boost parity authority. They are not global Maho theme-token authority.
- Legacy orange, orange-red, red, blue, and purple mode colors remain visual evidence for Case and Size states. The implementation must also expose text/value and semantic state, but it must not replace the original visual result with unrelated shared selected styling if the rendered pixels diverge.
- The current Magic Theme fallback accent has no shared semantic token. Until W5 removes the control, preserve the reference visual while keeping `aria-pressed` or equivalent semantics. Do not promote the fallback accent into a product token.
- The color wheel's conic spectrum, computed HSL handle colors, and generated arc gradient are functional color data. They are exempt from product-chrome tokenization, but must be confined to the wheel and its handles.
- CodeMirror's internal syntax colors are owned by the bundled CodeMirror `oneDark` extension. The surrounding Code route uses shared tokens. Do not create a per-Boost syntax palette.
- Selection and disabled state must never be communicated by color, opacity, filter, or grayscale alone.

### State treatment

| State | Contract |
| --- | --- |
| Default | Semantic surface + foreground; no arbitrary tint |
| Hover | `bg-surface-hover`; text/icon remains legible |
| Active/pressed | Shared selected or primary tonal change plus immediate control feedback |
| Selected/toggled | `bg-surface-selected`, explicit glyph/text/value, semantic state attribute |
| Focus-visible | Visible ring using `--color-ring`; never replaced by hover styling |
| Disabled | Native `disabled`/`aria-disabled`, muted foreground, reduced emphasis, no pointer activation |
| Loading | `aria-busy`, stable geometry, text or `Loader2`; no color-only or spinner-only status |
| Error | Inline `Alert` or status using destructive semantics and actionable text |

## 3. Typography

Boost uses the shared system stack from `tokens.css`:

`system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif`

CodeMirror uses its bundled monospace/editor stack. No new font files, brand display font, or per-Boost web font may be added.

| Role | Shared scale intent | Weight / line height | Use |
| --- | --- | --- | --- |
| Boost title | `text-xs` | `font-semibold`, compact single line | Current Boost name in the 40px title strip |
| Utility action | `text-sm` or the smallest shared control size that preserves legibility | `font-medium` | Size, Case, Zap, Code, Back |
| Compact icon tooltip/menu item | `text-xs` | `font-medium` | Icon-only accessible helper text and compact action menu |
| Font preview glyph | `text-xs` rendered in the represented system font | normal/medium | `Aa` preview in the 5-column grid |
| Font select | `text-xs` | normal | Default/system font choice |
| Advanced color label | `text-xs` | `font-semibold`, compact line height | Contrast, Brightness, Original Saturation |
| Status/toast/error | `text-xs` | `font-medium` or `font-semibold` | Import/export/loading/error feedback |
| Code editor | 12px-equivalent editor scale | 1.5 line height | CSS source editing only |

Typography rules:

- Preserve single-line truncation for the Boost title. The name remains capped by the behavioral contract and must not expand the title strip.
- Use sentence case. Case mode affects the target-page typography state, not the editor's own labels.
- Text labels must remain visible for Size/Case/Zap/Code/Back. Icon-only controls require accessible names and may use `Tooltip` as a pointer/visual aid.
- Do not guess type size from generic shared classes. Use the smallest shared type classes when they reproduce the normalized references; otherwise preserve measured Boost-scoped extraction values for the legacy `8pt` title and `10pt` action rhythm so the rendered outcome stays exact.
- CodeMirror text, line numbers, selection, and caret remain controlled by CodeMirror. The React shell must not globally override editor typography.

## 4. Spacing & Layout

### Fixed native modes

- **Boost mode is exactly `184x582`.** Native authority: `kBoostModeWidth = 184`, `kBoostModeHeight = 582`.
- **Code mode is exactly `452x582`.** Native authority: `kCodeModeWidth = 452`, `kCodeModeHeight = 582`.
- The root `html#zenBoostWindow` remains live and owns `editor="boost"` or `editor="code"`. The attribute is the CSS/state hook for mode width and must change with the Mojo `RequestModeResize` call.
- This surface has no mobile, tablet, desktop, or fluid responsive mode. The native widget is non-resizable; layout adapts only between the two exact native modes.
- The WebUI fills the native widget edge to edge and owns clipped overflow. Popovers/menus must use collision handling inside the WebUI viewport.

### Boost mode geometry

- Title strip: fixed 40px height.
- Main control inset: 16px horizontally and at the lower body edge where source-grounded (`p-4` intent).
- Source-grounded compact exceptions use the Tailwind scale when exact: 6px title-to-body top inset (`pt-1.5`), 14px major body gap (`gap-3.5`), 8px utility-row gaps (`gap-2`), and 4px/8px local font-grid rhythm. If Tailwind's scale misses the normalized reference, exact Boost geometry wins and the local extraction value must be documented.
- The color wheel fills the available inner width, stays square, and remains the dominant first workspace.
- The font grid is five equal columns. Font buttons are real single-selection controls with an `Aa` preview and an accessible font name.
- The font select follows the grid and separator in the same compact typography section.
- Size and Case share one equal-width row. Zap and Code remain full-width utility rows.
- The body container is the drag region only in unused space. Every interactive descendant is explicitly no-drag.

### Code mode geometry and load contract

- Code top bar: fixed 40px height and a drag region, with the Back control explicitly no-drag.
- Code bottom bar: fixed 60px height, 10px inset and gap, with picker and inspector controls no-drag.
- CodeMirror workspace: exactly the remaining `582 - 40 - 60 = 482px` content height and the full 452px width.
- The legacy editor spacing intent is retained: 16px vertical scroller inset and compact content/gutter padding. Express shell spacing through the shared scale when exact; otherwise preserve documented Boost-scoped extraction values.
- `codemirror.bundle.js` remains a separately packaged IIFE and loads before the React module. It must assign `window.MahoCodeMirror` before React creates or mounts the editor.
- CodeMirror is mounted into the live `#custom-css-editor` host inside the live legacy `#zen-boost-code-editor` container. `.cm-editor` and `.cm-content` remain real descendants required by tests.
- The EditorView is created once for the relevant editor lifecycle and is not recreated on ordinary React renders. Entering Code mode requests measurement after the native resize. Returning to Boost mode disables picker state before resize.
- CodeMirror descendants, scrollbars, selection, and text editing are no-drag.

### Floating surfaces

- The advanced-color popover opens beneath the Controls trigger, is centered on the trigger when possible, and remains at least 8px from every viewport edge.
- Its width is bounded to `min(260px, calc(100vw - 16px))`; content and sliders shrink to the available width. The legacy fixed 185px track plus 18px padding documents the original control density, but the React result must remain inside the `184x582` widget without clipping or escaping.
- The Boost action menu is anchored to the visible name trigger and remains within the viewport. It must not use arbitrary screen coordinates once shared Radix positioning is available.
- Neither floating surface changes native widget size.

### Live test-facing structure

The React DOM must attach every compatibility ID/class to visible, state-bearing structure in the real component tree. No hidden node, duplicate ID, detached compatibility island, or inert placeholder is permitted.

| Required hook | Live ownership |
| --- | --- |
| `#zenBoostWindow` | Root `html`; live `editor` mode attribute |
| `#zen-boost-editor-root` / `#boost-editor.boost-mode` | Visible Boost route root/main |
| `#color-section` | Visible color workspace section |
| `#color-wheel.color-wheel-container` | Visible, interactive color wheel |
| `#dot-primary` + `#zen-boost-color-picker-dot-primary` | One visible primary-handle component subtree; positioning/state and focus control are both functional |
| `#dot-secondary` + `#zen-boost-color-picker-dot-secondary` | One visible secondary-handle component subtree; positioning/state and focus control are both functional |
| `#typography-section` | Visible font/size/case section |
| `#font-grid` + `#zen-boost-font-grid` | One visible five-column font-selection component subtree |
| `.font-button` | Every real font option button |
| `#case-cycle-btn` + `#zen-boost-case` | One real Case cycle control subtree |
| `#size-cycle-btn` + `#zen-boost-size` | One real Size cycle control subtree |
| `#zap-section.zap-controls` | Visible Zap/Code utility section |
| `#zen-boost-zap` | Real Zap toggle/count control |
| `#zen-boost-code-editor-root` / `#code-editor-container.code-mode` | Visible Code route root/main |
| `#zen-boost-code-editor` / `#custom-css-editor` | Real CodeMirror layout container and mount host |
| `#advanced-color-toggle` + `#zen-boost-controls` | One visible Controls/popover-trigger component subtree |
| `#advanced-color-popup` + `#zen-boost-advanced-color-options-panel` | One visible bounded advanced-color form/popover subtree |
| `#magic-theme-toggle` + `#zen-boost-magic-theme` | One visible Magic Theme toggle subtree until W5 removal |
| `#smart-invert-toggle` + `#zen-boost-invert` | One visible Smart Invert toggle subtree |

## 5. Components & Required States

Use existing shared primitives first. A feature-local composition is allowed only for Boost-specific behavior such as the two-dimensional color wheel or CodeMirror host; it is not automatically a new shared primitive.

### BoostShell

- **Structure:** root mode state, title strip, Boost route, Code route, portal/floating-surface host, polite status region.
- **Variants:** Boost mode; Code mode.
- **States:** boot/loading, ready, closing, fatal load error.
- **Accessibility:** one `main` for the active route; inactive route removed from tab order and accessibility tree; root exposes `aria-busy` during bootstrap.
- **Motion:** no page transition that delays resize or focus. Mode change is immediate; any opacity transition must be removed under reduced motion.

### TitleStrip

- **Structure:** semantic `header`; Close icon `Button`; Boost-name `DropdownMenuTrigger`; Shuffle icon `Button`.
- **Variants:** platform ordering may follow the current macOS/non-macOS visual arrangement without changing DOM reading order.
- **States:** default, hover, active, focus-visible, disabled/loading where an action is pending.
- **Accessibility:** accessible names `Close Boost editor`, current Boost name plus `Boost actions`, and `Shuffle Boost settings`. The current name remains visible and truncated.
- **Motion:** color/opacity feedback only; no decorative title animation.

### BoostActionMenu

- **Structure:** shared `DropdownMenu`, `DropdownMenuContent`, items for Rename, Shuffle, Reset, Import, Export, Delete, and shared separators.
- **Variants:** standard item; destructive Delete item.
- **States:** closed, open, focused item, disabled item, loading action, action error.
- **Accessibility:** native menu keyboard behavior; disabled Rename/Reset/Delete when no change exists; Escape closes and returns focus to the name trigger.
- **Motion:** shared menu enter/exit only; reduced motion settles immediately.

### ColorWheel

- **Structure:** labelled section/group, visible wheel, primary handle, secondary handle, optional arc/radius visualization, Magic Theme control until W5.
- **Variants:** normal; color adjustments disabled; Magic Theme active; dragging primary; dragging secondary.
- **States:** default, hover, focus-visible, dragging/active, disabled, loading/hydrating, error.
- **Accessibility:** the wheel has an accessible group name. Each handle is keyboard focusable with an explicit name (`Primary color`, `Secondary color`) and a text value describing hue and distance/saturation. Arrow keys adjust the current handle; Shift+Arrow uses a larger step; Home resets to center. Pointer dragging remains supported. State is not communicated by handle color alone.
- **Motion:** pointer dragging updates immediately. Non-drag jumps may use a short transform transition; reduced motion and active dragging disable interpolation.

### ColorUtilityRow

- **Structure:** shared icon `Button`s for Smart Invert, Advanced Color Controls, and Disable Color Adjustments.
- **Variants:** toggle action; popover trigger.
- **States:** default, hover, active, focus-visible, pressed/selected, disabled, loading, error.
- **Accessibility:** real buttons with `aria-pressed` for toggle actions and `aria-expanded`/`aria-controls` for the popover trigger. Tooltips supplement but never replace accessible names.
- **Motion:** shared control feedback only.

### AdvancedColorPopover

- **Structure:** shared `Popover`, `PopoverTrigger`, bounded `PopoverContent`; three `Label` + `Slider` groups for Contrast, Brightness, Original Saturation.
- **Variants:** standard only.
- **States:** closed, open, focus-within, disabled while color adjustments are disabled, updating, error.
- **Accessibility:** opens with focus on the first slider; each slider has programmatic label and current value; Arrow keys/Home/End follow the shared Radix slider contract; Escape closes and returns focus to Controls; outside activation closes without losing the invoking context.
- **Motion:** shared popover opacity/transform only; no layout animation.

### FontPicker

- **Structure:** visible surface containing a single-select `ToggleGroup` for common fonts, `Separator`, and shared `Select` for all system fonts.
- **Variants:** common-font grid; full-font select.
- **States:** loading fonts, default/no override, hover, active, focus-visible, selected, disabled, empty system-font result, retrieval error.
- **Accessibility:** group label `Font`; every `Aa` option has the font name as its accessible name and selected state; the Select trigger exposes the current choice. Empty/error states retain the Default option and a compact status message.
- **Motion:** tonal/scale feedback only when motion is allowed. Selection does not animate layout.

### CycleControl

- **Structure:** shared `Button` with persistent accessible name and visible label/value.
- **Variants:** Case (`none`, `uppercase`, `lowercase`, `capitalize`); Size (`100%`, `110%`, `125%`, `150%`, `90%`).
- **States:** default, hover, active, focus-visible, selected/non-default, disabled, loading, error.
- **Accessibility:** `aria-pressed` or `aria-valuetext` communicates non-default state; activation order exactly matches Mojo/test sequences. The visible value, not color, identifies the current mode.
- **Motion:** shared press feedback; no gradient interpolation or decorative mode animation.

### ZapControl

- **Structure:** shared `Button`, Lucide intent icon, visible `Zap` label, trailing count/status.
- **Variants:** inactive with no selectors; active Zap mode; inactive with persisted selectors.
- **States:** default, hover, active, focus-visible, pressed, loading enter/exit, empty count, populated count, error.
- **Accessibility:** `aria-pressed` reports live Zap mode; count is included in the accessible description; entering/exiting state is announced. Empty count does not render a misleading zero-only status.
- **Motion:** no idle animation; state transition is immediate.

### CodeRoute

- **Structure:** draggable top bar with no-drag Back `Button`; CodeMirror host; bottom row with picker toggle and inspector action.
- **Variants:** ready editor; picker active; inspector action pending.
- **States:** CodeMirror loading, mounted, focus-visible, dirty/saving, persisted, empty CSS, editor error, picker active, disabled during close.
- **Accessibility:** activation of Code moves focus into CodeMirror after resize/mount; Back returns to Boost mode and restores focus to the Code button. Picker uses `aria-pressed`; Inspector is an action button. CodeMirror retains its native editing semantics and keyboard shortcuts.
- **Motion:** native resize and editor measurement are not animated by React.

### InlineRename

- **Structure:** shared `Input` replacing the visible name text inside the same title component.
- **States:** editing, valid, unchanged, empty-invalid, committing, error.
- **Accessibility:** label `Rename Boost`; text selected on entry; Enter commits, Escape cancels, blur commits valid changed text; focus returns to the Boost action trigger.
- **Motion:** none.

### StatusFeedback

- **Structure:** shared `Toaster` for transient import/export success; compact `Alert` for persistent errors; `Loader2` plus text for pending operations where needed.
- **States:** loading, success, warning, error.
- **Accessibility:** success uses `role="status"`/polite announcement; failures use `role="alert"`; messages identify the completed or failed action. No runtime success is inferred from click alone.
- **Motion:** toast motion uses opacity/transform and is disabled under reduced motion.

### Lucide intent map

All glyphs come through `@icons/lucide`; no vendored SVG or hand-authored icon path may be added to Boost.

| Existing control intent | Lucide intent |
| --- | --- |
| Close | `X` |
| Shuffle | `RefreshCw` unless a semantically closer shared export is approved |
| Name menu | `ChevronDown` |
| Magic Theme | `Sparkles` until W5 removal |
| Advanced controls | `Sliders` |
| Disable color adjustments | `EyeOff` |
| Code | `Code` |
| Back | `ArrowLeft` |
| Inspector | `ExternalLink` or a separately approved inspector-semantic export |
| Import | `Download` only if copy clearly says Import |
| Export | `ArrowUpRight` or a separately approved upload-semantic export |
| Delete | `Trash2` |
| Loading | `Loader2` |

The current export lacks a clear Zap and eyedropper/picker glyph. A later implementation lane may propose adding the canonical Lucide `Zap` and `Pipette` exports to the shared icon barrel if no existing export is semantically accurate. This is icon-inventory debt, not permission to vendor SVGs or create a new icon primitive.

## 6. Motion & Interaction

Motion serves state recognition only.

| Interaction | Contract |
| --- | --- |
| Hover/focus/press | Shared control transitions; approximately micro-duration behavior, no layout movement |
| Color handle drag | Immediate pointer tracking; no smoothing while dragging |
| Color handle click/jump | Short transform interpolation only when motion is allowed |
| Menu/popover open/close | Shared Radix opacity/transform transition |
| Import confirmation | Optional restrained opacity/vertical-transform sweep; never animate `top`, `left`, width, height, margin, or padding |
| Mode switch | Immediate route visibility, Mojo resize, CodeMirror measure, then focus restoration |
| Toast | Opacity/transform entry and exit with semantic status present throughout |

Interaction rules:

- Every button supports Enter and Space through native button semantics.
- Escape priority in Boost mode: cancel inline rename; close the action menu; close the advanced popover; otherwise request editor close. Escape in CodeMirror remains available to CodeMirror before global close behavior is applied.
- `Ctrl+W`/`Meta+W` requests the same close lifecycle as the visible Close control.
- Closing flushes/ends picker modes, then commits a changed temporary Boost or discards an unchanged one according to the existing lifecycle contract.
- Case cycles exactly `none -> uppercase -> lowercase -> capitalize -> none`.
- Size cycles exactly `100% -> 110% -> 125% -> 150% -> 90% -> 100%`.
- Code activation sets `editor="code"`, shows only the Code route, calls `RequestModeResize(kCode)`, measures CodeMirror after resize, and focuses the editor.
- Back exits picker modes, sets `editor="boost"`, calls `RequestModeResize(kBoost)`, and restores focus to Code.
- Outside activation closes menus/popovers but must not trigger the underlying drag region or lose the next intended control activation.
- `prefers-reduced-motion: reduce` disables non-essential transitions, handle interpolation, toast travel, popover zoom/slide, and import animation. State changes, focus, loading text, and status announcements remain immediate and complete.
- The legacy import animation currently animates `top`; that implementation detail is not carried forward. A future equivalent must use `transform`/`opacity` only or be omitted under reduced motion.

## 7. Depth & Surface

Depth strategy: **mixed tonal shift, hairline borders, and restrained elevation**.

- The Boost canvas is one continuous surface. Do not wrap each control group in a generic card.
- The title strip and Code bars separate from the body through a semantic tonal shift and one border hairline.
- Default controls use a supporting semantic surface. Hover and selected overlays provide interaction depth without new colors.
- The font picker may retain one restrained elevated surface because it groups the grid and select as a single control.
- The advanced-color popover and action menu use the shared popover/menu border and shadow. They are the highest elevation levels in the widget.
- The color wheel may retain one restrained shadow to separate a functional visualization from the compact canvas. Its dotted/blurred legacy overlay is not product-token authority and must not become a general Maho material.
- CodeMirror owns editor-internal depth and selection. The surrounding bars remain product surfaces.
- Do not use `glass`, `glass-strong`, backdrop blur, ambient glow, or layered decorative transparency in the React shell. The legacy color-wheel blur is accepted extraction debt and not a reusable material.
- Z-index values follow component layers: base content, bars, menus/popovers, transient status. Do not reproduce arbitrary `998`, `999`, `1000`, or `2147483647` values.

## 8. Accessibility Constraints & Accepted Debt

### Accessibility constraints

Target: WCAG 2.2 AA behavior within the fixed native widget.

- Normal text contrast must be at least 4.5:1; large text and essential non-text UI boundaries at least 3:1. Verify both shared light and dark token resolutions in the final visual/accessibility lane.
- Every interactive element has a visible focus indicator using the shared ring token. Focus must not be clipped by the fixed widget, color-wheel overflow, CodeMirror, or floating surfaces.
- DOM order and forward tab order in Boost mode are: Close -> Boost actions/name -> Shuffle -> Magic Theme (until W5) -> Primary color -> Secondary color -> Smart Invert -> Advanced Color Controls -> Disable Color Adjustments -> common-font buttons in row-major order -> font Select -> Size -> Case -> Zap -> Code. Disabled controls are skipped by native semantics. Shift+Tab is the reverse.
- Code mode tab order is: Back -> CodeMirror editable content -> Color Picker -> Inspector. Entering Code focuses CodeMirror after mount; returning restores focus to Code.
- The action menu uses standard menu arrow-key navigation, Home/End, Enter/Space activation, Escape close, and focus return.
- The advanced popover exposes three labelled sliders in Contrast -> Brightness -> Original Saturation order and returns focus to Controls on close.
- Color handles are keyboard operable and expose text values. Pointer-only dragging is not sufficient.
- All icon-only controls have stable accessible names. Tooltips and `title` attributes are supplementary only.
- Toggle states use `aria-pressed` or the native Radix state. Popover/menu triggers use `aria-expanded` and ownership relationships.
- The title update, selected font, Case value, Size value, Zap count/mode, picker mode, loading, success, and error states are available to assistive technology without relying on color or animation.
- Transient success is announced through one polite status region. Errors use an assertive alert only when user action is required.
- The root, title strip, sections, forms, menus, and buttons use real semantic HTML. No clickable `div` remains for the name trigger or color utility buttons.
- Light/dark mode is applied through `applyTheme`; forced-colors and increased text size must preserve visible boundaries and not hide labels.
- `prefers-reduced-motion` is honored as defined in Section 6.
- No inert, hidden, offscreen, duplicated, or compatibility-only ID node is allowed. Every required ID/class belongs to the visible component subtree that owns the real behavior/state.

### Drag and no-drag constraints

- `#zen-boost-head-wrapper` is a drag region; Close, name/menu trigger, Shuffle, inline rename input, and all nested interactive descendants are no-drag.
- `#zen-boost-code-top-bar` is a drag region; Back and its descendants are no-drag.
- The Boost body/main unused background is a drag region; every button, input, select, slider, color handle, menu, popover, font control, and nested interactive descendant is no-drag.
- CodeMirror, Code bottom controls, scrollbars, text selection, menus, and portal content are no-drag.
- No transparent drag overlay may cover a focusable or clickable control.

### Accepted debt

| Item | Location | Why accepted for Todo 1 | Owner / exit condition |
| --- | --- | --- | --- |
| Legacy Boost chrome contains raw colors with no exact shared semantic match | `boost.css`, `zen-advanced-color-options.css`, inline styles in committed legacy `app.ts` | This is accepted only as local extraction evidence; visual divergence is not accepted | Later implementation preserves the normalized reference pixels without promoting these values to global Maho tokens |
| Legacy mode gradients encode Case/Size state with arbitrary orange/red/blue values | `.mod-button[mode]` | The colors are accepted as reference visuals, not as new shared tokens | Later implementation matches the state visuals and also exposes text/value plus programmatic state |
| Magic Theme fallback accent has no shared token | current Magic Theme preview | Magic Theme remains until the separately planned removal; its current appearance is reference evidence | Later implementation matches the reference while preserving accessible pressed state; no shared token is created |
| Advanced popover's legacy minimum width can exceed the 184px viewport | `zen-advanced-color-options.css` | Static extraction found a geometry conflict; the required result is a bounded panel that preserves original control order and density inside the fixed widget | Later implementation captures open/focus/disabled popover states at `184x582` without clipping or viewport escape |
| CodeMirror legacy CSS and `oneDark` contain editor-specific colors | CodeMirror host and bundled theme | Syntax/editor colors are third-party editor semantics, not Boost product chrome | Keep `oneDark` authoritative unless a separately approved shared editor theme is created |
| Color wheel spectrum and computed HSL handles are raw chromatic values | wheel visualization | They represent editable user data, not product brand | Keep confined to the functional wheel and verify non-color state cues |
| Shared icon barrel lacks unambiguous Zap and picker glyph exports | `@icons/lucide` | Existing exports do not preserve both meanings accurately | Later implementation may add canonical `Zap`/`Pipette` exports in shared scope; no vendored SVG |
| Existing shared primitives do not provide a two-dimensional color-wheel control | Boost color workspace | The behavior is feature-specific and not proven reusable | Compose locally from semantic controls; propose a shared primitive only after a second consumer exists |
| Existing lifecycle tests have partial TODO-level assertions and Zap can skip | lifecycle/behavior interactive tests | Todo 1 is source/evidence-only and cannot strengthen or run tests | Later coverage lanes establish runnable characterization; no current pass claim is allowed |
| Static Todo 1 review cannot prove focus order, drag hit testing, contrast, clipping, CodeMirror mount, or implementation visual parity | entire surface | Todo 1 is evidence normalization and contract binding only; no build, browser, test suite, or app launch is authorized | Later implementation waves perform interactive tests, real-app QA, and visual review against this contract |

No additional accessibility debt is silently accepted. A later exception must identify affected users, severity, exact location, remediation, owner, and explicit approval.

## 9. Boost Reliability Binding Contract

This section is the Wave 0 contract for Todos 2 through 9. Product behavior remains untouched by Todo 1. Later lanes must implement these requirements exactly and attach fresh, machine-readable evidence after their last relevant source edit.

### Selection and activation state machine

- `selectedBoostId` identifies the record currently being edited. `activeBoostId` is independently nullable and identifies the record applied to the current domain.
- Bootstrap selects the active record when present. If the site is Off and saved records exist, it selects the deterministic saved record without activation. A temporary record is created only when the domain has no saved record.
- Sort saved records as selected record, then ASCII case-insensitive name, then ID. The chooser shows each saved record once.
- Selecting a record while Site Off changes only `selectedBoostId`. Turning Site On activates the selected record. Turning Site Off sets `activeBoostId` to null, removes target CSS, and preserves selection for editing.
- `OnActiveChanged(null)` clears activation only. It must not create a temporary Boost or reactivate the site.

### Chooser and Reset barrier

The existing title and name dropdown begins with the site-level On or Off control, followed by bounded visible Boost radio rows in deterministic order. Each row distinguishes selected editing state from active application state. A separator then precedes Rename, Shuffle, Reset, Import, Export, and Delete, which always act on the selected record. The menu remains inside the fixed `184x582` viewport and preserves keyboard navigation, Escape, and focus return.

Reset is controller-owned and ordered: cancel unsent debounce work, invalidate stale queued updates, await sent mutations, call Reset, then refresh the list, selection, activation, and target CSS from authoritative results. Reset preserves Off state and returns new or reset Boosts to `colorBoostEnabled=true` and `magicTheme=false` without changing existing persisted or imported records until explicit edit or reset.

### Color state and handles

Pointer or keyboard adjustment is an atomic transition to manual color mode: `colorBoostEnabled=true`, `magicTheme=false`, then exactly one intended primary or secondary update. Color Off and Auto remain visible states. Only a true global busy or unavailable condition disables mutation.

Each handle has one pointer-capture owner and one visible, focusable accessible button. That owner releases capture and listeners on pointer cancellation, lost capture, and unmount. Native pointer drag and keyboard Arrow, Shift+Arrow, and Home interactions are all part of the required color matrix.

### Live Lucide and Auto inventory

Every live Boost and Code glyph is inline JSX from `@icons/lucide`, with exactly one SVG per control. The required intent inventory is Close `X`, Shuffle `RefreshCw`, menu `ChevronDown`, Auto `Sparkles`, invert `Eye`, color controls `Sliders` and `EyeOff`, Zap `Zap`, Code `Code`, Back `ArrowLeft`, picker `Pipette`, inspector `ExternalLink`, import `Download`, export `ArrowUpRight`, delete `Trash2`, and loading `Loader2`. Only the matching active CSS mask pseudo-elements may be neutralized. Packaged legacy assets and GN resources remain intact.

The wheel's top-center automatic-theme control is `Sparkles + Auto`. It remains a pressed control with visible `Auto` wording and accessible text `Use automatic theme colors`.

### Zap selector and delivery contract

Zap remains select-then-confirm in the isolated world. It normalizes a nested raw event target to the nearest meaningful semantic ancestor, excludes Maho overlay and document-root nodes, and validates intended-target inclusion and bounded match count before enabling confirmation. Removed, invalid, zero-match, and over-broad candidates provide actionable feedback and never persist.

Accepted committed selector grammar is `compound` or `compound > compound`, where `compound` is an optional lowercase HTML tag followed by one or more stable `#id`, `.class`, `[data-*="value"]`, `[aria-label="value"]`, `[name="value"]`, `[role="value"]`, or `[href="value"]` terms. Values use only escaped-free printable identifier or URL characters. Commas, wildcard selectors, pseudo selectors, `:nth-child`, sibling combinators, unbounded descendants, CSS braces, declarations, backslashes, and page-visible hooks are rejected. Candidate priority is unique ID, stable `data-*`, ARIA/name/role/href attribute, stable class combination, then the bounded parent-child fallback.

The sole temporary-unhide attribute is `maho-zap-unhide`. Core composition uses `:not([maho-zap-unhide])`; no `zen-zap-unhide` compatibility attribute is allowed. Active Zap or picker polling is at most `25 ms`, starts with an immediate drain, has one drain request in flight, runs only while Zap or picker mode is active, and stops on every exit, navigation, frame-destruction, and editor-teardown path.

### Required matrices and evidence

The recognition matrix covers nested span or SVG in a button, unique ID, stable data attribute, stable ARIA/name attribute, repeated cards, removed target before confirmation, invalid selector, zero-match candidate, and over-broad candidate. The color matrix covers new, reset, legacy persisted disabled, imported disabled, Auto-to-manual, Color-Off-to-manual, both native pointer drags, keyboard Arrow and Home, pointer cancellation, and teardown.

Each lane writes its required log, JSON, AX tree, screenshot, XML, and verdict under `.omo/evidence/boost-ui-reliability-plan/`. XML must list every expected testcase exactly once with zero failures and zero skips. Source and resource inputs must not be newer than the build, and screenshots must not predate that build. Integration evidence excludes F1 through F4 receipts. Final evidence with current review F1 requires F2, F3, and F4 APPROVE receipts while excluding only the F1 verdict. Complete final evidence requires F1, F2, F3, and F4 APPROVE receipts.

### Scope and exclusive ownership

No new Mojo schema/API, migration, persistence schema, security-boundary relaxation, Chromium feature flag, or build flag is permitted. No GN expansion. No migration changes existing saved disabled-color Boosts. Todo 1 owns only this contract, its checker and tests, and `.omo/evidence/boost-ui-reliability-plan/**`.

| Todo | Exclusive primary ownership after Todo 1 |
| --- | --- |
| Todo 2 | `maho/crates/maho-core/src/boost_manager.rs` |
| Todo 3 | Boost React state, controller, bridge, mutation queue, actions, app, and actions interactive test |
| Todo 4 | `react/components/title-strip.tsx` and visual accessibility interactive test |
| Todo 5 | Color section, color wheel hook, advanced popover, and behavior interactive test |
| Todo 6 | Standalone Zap selector component, content script, overlay, CSS, and Zap picker interactive test |
| Todo 7 | Zap injection and page handlers plus Zap picker follow-up test |
| Todo 8 | Shared Lucide barrel, Boost icon components, Boost CSS, and visual accessibility follow-up test |
| Todo 9 | Evidence and test manifests only, with source fallout returned to the owning Todo |
