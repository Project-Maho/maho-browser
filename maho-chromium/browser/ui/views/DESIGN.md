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

## Maho Mini Top Bar

### Layout Contract
* **Reference baseline:** Verify against fresh captures of the recovered Arc references at a 600×760dp Mini window. The default top bar is 46dp tall across the Views top bar, popup content offset/minimum height, and macOS native titlebar override.
* **Three-zone composition:** Reserve 8dp as the outer inset and 60dp for native macOS traffic lights. The left zone is the compact active-Space chip within the integrated chrome surface. The focal zone is the full-window-centered editable hostname. The right zone is the copy icon inside that same surface plus the separate promote control.
* **Integrated chrome surface:** At the 600dp default width, the continuous chrome surface is approximately `x=74dp`, `y=7dp`, `w=302dp`, `h=32dp`, with a `10dp` corner radius. It contains the Space chip, editable address field, and copy control as one bounded surface, not detached pills.
* **Material:** The integrated surface is stable dark plum/purple low-alpha glass with a restrained edge and highlight. This material is independent of the active-Space hue. Only the Space marker uses the active Space color. It is not neutral gray, and it must not become a full-width saturated Space-tinted titlebar.
* **Space chip:** Keep the current Space compact and left-aligned inside the integrated surface, with its label and marker intact. The marker alone may use the active Space color. The chip backing, border, and label remain part of the stable glass material.
* **Centered editable hostname:** The address field remains editable, accepts normal address-field pointer and keyboard interaction, and displays its hostname centered against the full 600dp window width within 1dp. Preserve this full-window centering while allowing the field's internal available width to shrink before it overlaps the Space chip, copy control, or promote control.
* **Copy control:** Place the link-copy icon inside the right end of the integrated chrome surface. It is a distinct compact control within the surface, not a standalone pill between the address field and promote control.
* **Promote control:** Place a separate compact segmented control at approximately `x=382dp`, `y=9dp`, `w=210dp`, `h=28dp`, with an `8dp` corner radius. Its primary segment names the destination, for example `Open in Personal`, and exposes the `⌘O` hint. Its trailing dropdown segment is `30dp` wide with a centered `1×18dp` divider/caret slot, opens the Space picker, and remains visually and hit-test distinct from the primary promote action.
* **Responsive priorities:** Preserve, in order, traffic-light reservation, the active-Space marker and readable identity, an editable centered hostname, the copy control, and the primary promote action. Under horizontal pressure, tighten internal gaps and labels first, then compact the promote segments while retaining its dropdown affordance. Never let controls overlap or replace the hostname with a non-editable label.

### Component and Accessibility Contract
* Space, address, copy, and promote controls use dedicated Mini color IDs: `kMahoColorMiniFrameWash`, `kMahoColorMiniFrameEdge`, `kMahoColorMiniControlBackground`, `kMahoColorMiniControlBorder`, `kMahoColorMiniPrimaryText`, and `kMahoColorMiniSecondaryText`. The Mini frame and integrated surface tokens resolve to the stable dark-plum/purple glass described above, not a neutral-gray palette and not an active-Space tint.
* Mini top-bar paint and control surfaces must not use `kMahoColorTitlebarGlass`, `kMahoColorCardBackground`, or `kMahoColorTitlebarEdge`, because those broad shell tokens may inherit active Space tint.
* Preserve explicit accessible names: the Space chip exposes the current Space name, the editable address field is named `Address`, the copy control is named `Copy link`, the promote primary action is named `Open in <Space name>`, and the trailing selector is named `Choose Space`. The selector remains separately focusable and operable from the primary promote action.
* The hostname and all labels painted over translucent surfaces keep subpixel rendering disabled. Focus indicators must remain visible against the dark glass, and every interaction remains keyboard-operable.
* Completion requires a fresh 600×760dp capture comparison against `maho-mini-reference-arc.png`, `maho-mini-reference-design.png`, `maho-mini-reference-comparison.png`, and `maho-mini-reference-later.png` before visual parity is claimed.

## Sidebar Ellipsis Spark Action Row

### Overview
* The processing feedback is strictly contained within the 42dp New Tab/Tidy/Clear action row, keeping the actual tab list fully visible and allowing live updates.
* Private mode restricts the action row to a "+ New Tab" button only and does not instantiate the processing feedback.

### Motion & Timing Contract
* **Rich Motion Timeline**: 300ms merge, at least 200ms working, 300ms success/failure resolve, and 200ms restore.
* **Fast Path**: Fast Tidy and Clear complete in exactly 1000ms. Single cycle (Clear) resolves success automatically.
* **Slow Path**: Long operations (Tidy) extend only the 2200ms working loop. Resolving captures the current pose and transitions immediately.
* **Reduced Motion**: Static textless pose (dots and centered spark) with buttons hidden. Clear holds 180ms; Tidy waits for external resolve then hides immediately.

### Styling & Accessibility
* **Geometry**: Ellipsis scale factor `1.44`. Dots merge from thirds to rest offsets `[-15.84, 0, 15.84]` with radius easing `4.32` to `3.6`. Working dots settle at `[-14.4, 0, 14.4]`, radius `3.6`. Traversing spark tracks `[-14.4, 14.4]` at y-offset `-8.64`, radius `2.16`.
* **Palette**: Dots map to `ui::kColorSysOnSurfaceSubtle`; spark/check mark map to `kMahoColorAccentBlueBright`.
* **State & Announcements**: Textless states with no visible label. Accessible name/role `kAlert` is announced on start and resolution.
* **Input Blocking**: Button subtree event processing and accessibility exposure are blocked during processing. Keyboard focus is restored to the initiating button upon completion.
