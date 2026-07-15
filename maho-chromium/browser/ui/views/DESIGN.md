# Desktop Native Views — Incognito delta

## Observed Arc
Help Center article mappings used as reference for parity:
* Article 22354398337687: https://resources.arc.net/api/v2/help_center/en-us/articles/22354398337687.json (Retrieved: 2026-07-13)
* Article 20498463803799: https://resources.arc.net/api/v2/help_center/en-us/articles/20498463803799.json (Retrieved: 2026-07-13)
* Article 20498377604887: https://resources.arc.net/api/v2/help_center/en-us/articles/20498377604887.json (Retrieved: 2026-07-13)
* Article 19434259167767: https://resources.arc.net/api/v2/help_center/en-us/articles/19434259167767.json (Retrieved: 2026-07-13)
* Article 19400407903767: https://resources.arc.net/api/v2/help_center/en-us/articles/19400407903767.json (Retrieved: 2026-07-13)

Official private shell visual asset details:
* URL: https://resources.arc.net/hc/article_attachments/22368596458007
* SHA-256: ccd2cd923451537dcec608649882e976b7b09afe278535155dfb97e4909d9610
* Type: RGBA PNG
* Dimensions: 454×344

## Approved Maho default

### Color and UI Tokens
The following semantic tokens are approved for the private Visual shell to ensure parity and contrast:
* **Shell (Sidebar Outer/Base Frame):** `#111214`
* **Surface (Sidebar Inner/Main Panel):** `#202124`
* **Elevated / Search (Input backings/Popups):** `#2F3033`
* **Primary Text:** `#ECEDEF`
* **Secondary Text:** `#A7ABB1`
* **Border:** `#858A91`
* **Hover State Background:** `#303134`
* **Selected State Background:** `#3C4043`
* **Focus Outline/Accent:** `#8AB4F8`

Accessibility guidelines:
* Text contrast must be `>=4.5:1` relative to backing colors.
* Essential borders and focus indicators must have contrast `>=3:1`.
* Selected items must render a 2dp leading bar.

### Accessibility and Keyboard Navigation Contract
* **Initial Focus:** Initial focus after an Incognito page commits is WebContents.
* **Expanded Tab Order:** Expanded forward Tab order is `Sidebar Toggle → Back → Forward → Reload → Search or Enter URL → New Tab → tab rows in TabStripModel order → WebContents`; disabled Back/Forward are skipped, and reverse Shift+Tab is the exact reverse of the resulting enabled sequence.
* **Collapsed Tab Order:** Collapsed order is `Expand Sidebar → New Tab → tab rows in strip order → WebContents`.
* **Fullscreen Behavior:** Fullscreen-hidden exposes only WebContents and no Incognito identity, while fullscreen-revealed restores the expanded rail, visible identity, and expanded order without moving current focus.
* **Identity Visibility:** Identity is visible in expanded, collapsed, and fullscreen-revealed states only.
* **Escape Key Behavior:**
  * Escape from Command Palette returns focus to the invoking Search field.
  * Escape from any other Sidebar control returns focus to the previously focused WebContents.
  * Escape in WebContents remains unchanged.
* **Close Tab Behavior:** Closing the focused tab selects/focuses the Chromium-chosen successor row, or New Tab if none.
* **Motion & CJK Layout:**
  * Reduced motion requests must lead to zero-duration animations/transitions.
  * CJK names (`비공개 탭 테스트`, `プライベートタブ`, `隐私标签页`) must be painted without truncation or clipping.
  * Decorative Glasses elements must be explicitly ignored by assistive technology (AX screen readers).

### Windows Zen Frame Contract
* **Standard App Menu:** Windows Arc-layout windows expose a visible top-left ellipsis control in `MahoSidebarTopBarView` using `maho_lucide_icons::kEllipsisIcon`, accessible name `Menu`, and the existing top-bar control-button token sizing. Activating it opens the full standard Chromium `AppMenu` anchored to the visible ellipsis bounds.
* **Keyboard Route:** `Alt+E` / `Alt+F` (`BrowserView::ShowAppMenu`) route to the same visible ellipsis menu in Maho Arc layout. Non-Arc, PWA, macOS, and Linux menu behavior remains Chromium-default.
* **Focus and Escape:** The menu must use Chromium's normal menu focus model: focus enters the app menu on open, `Esc` closes it, and focus restoration follows the underlying Views menu runner. The ellipsis hit rect is excluded from draggable caption hit testing.
* **Caption Controls at Rest:** Windows native minimize/maximize/close controls are hidden and non-hittable at rest. No full-size invisible caption overlay may intercept content or sidebar clicks.
* **Top-Right Reveal Trigger:** A small top-right screen strip reveals native caption controls instantly. Leaving both the trigger and revealed caption region schedules hide after approximately `500ms`; when animation duration scaling is zero, hide is immediate.
* **No Reveal Animation:** Reveal/hide changes are visibility and hit-test state only. Do not animate layout dimensions or opacity for the Zen caption controls.
* **System Menu:** `Alt+Space` and system-menu hit testing remain native Windows behavior; the top-left app-menu ellipsis is not a replacement for the OS system menu.
* **Snap Layout:** Revealed maximize/restore must return `HTMAXBUTTON` from non-client hit testing so Windows 11 Snap Layout works. Minimize and close return their native hit-test codes.
* **State Reset:** Window bounds, maximize/restore, fullscreen, removal, and destruction reset the caption controls to hidden and stop any hover monitor/timer.
* **Platform Scope:** macOS traffic lights and Linux frame behavior are unchanged.
