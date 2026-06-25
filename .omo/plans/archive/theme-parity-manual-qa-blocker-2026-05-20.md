# Theme Parity Manual QA — Current Blocker (2026-05-20)

## Scope

Honest record of the current manual QA attempt for the theme-parity flow:
`Create space` footer button → `New Space` popover row → `MahoSidebarCreateSpaceView` → theme button → shared `MahoSpaceConfigDialog`. This note closes the active QA todo by stating exactly where the run halted and why.

## What was attempted

Drove an isolated Maho build through native macOS automation, aiming to reach the create-space chooser and confirm the shared-config handoff visually.

## What succeeded

- **Process launch.** Isolated Maho instance came up cleanly on `--remote-debugging-port=9446`.
  - PID: `11741`
  - Profile dir: `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-space-theme-rerun2-B0jGTA`
- **DevTools reachability.** Both endpoints responded over loopback:
  - `http://127.0.0.1:9446/json/version`
  - `http://127.0.0.1:9446/json/list`
  - Targets exposed were limited to `about:blank` plus the top-chrome target. No DevTools surface for the sidebar internals.
- **Window discovery.** Quartz window list resolved a single visible window for PID `11741`:
  - Window ID `16296`, title `about:blank`, frame `{X=22, Y=52, Width=1200, Height=1452}`.
- **Visual capture.** Exact-window screenshot saved at:
  `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-space-theme-rerun2-window.png`
  - OCR confirmed the empty-state UI was on screen: `Drag to add Favorites`, `Favorites keep your most used sites`, `+ New Tab`, `No tabs open`. So the sidebar rendered, the build is alive, and the empty space is what the user would see.

## What failed

- **Footer-zone clicks did not surface the popover.** Repeated clicks in the expected `Create space` plus-button band produced no `New Space / New Folder / New Tab` popover and added no new Maho windows beyond the existing layer-0 window set. Coordinates tried (absolute screen px):
  - `(302,1470)`, `(322,1470)`, `(342,1470)`, `(362,1470)`
  - `(302,1490)`, `(322,1490)`, `(342,1490)`, `(362,1490)`
- **Accessibility tree did not expose the sidebar.** Earlier AX inspection of PID `11741` only returned the standard window controls (traffic-light buttons). Internal Chromium sidebar controls were not exposed by accessibility name, so the footer button and popover rows can't be hit by AX action either.

## Current blocker (narrow)

Native macOS automation cannot yet target the footer `+` button and its `New Space / New Folder / New Tab` popover reliably enough to produce end-to-end manual proof of the chooser → shared-config handoff. The build runs, the window renders the empty-state, DevTools is up, and screenshots come through. What's missing is a stable target for the footer control: blind-coordinate clicking in the footer band hasn't triggered the popover, and AX doesn't expose those Chromium-internal nodes by name on this build.

App launch and runtime are no longer the blocker. Target acquisition is.

## Known intended flow (for reference, from tests/code)

`Create space` footer button → `New Space` popover row → `MahoSidebarCreateSpaceView` → theme button → shared `MahoSpaceConfigDialog`.

This is the path the manual QA needs to walk; it has not yet been reached on this run.
