# Arc "Maho Mini" Parity - Behavior Research Brief

This document outlines the behavior specification and design invariants for Maho Browser's "Maho Mini" parity implementation, based on documented and observed behavior in Arc Browser.

## 1. Process & Window Architecture
- **Window Type:** Maho Mini is NOT a separate process. It is a lightweight, floating browser window belonging to the same browser process.
- **Visuals:** It uses a single popup style window frame (`Browser::TYPE_POPUP`) without toolbars, bookmarks bars, or tab strips.
- **Window Sizing:** Default size is `400x500` centered on the parent window (if launched from Maho) or the primary display (if launched globally/externally).
- **Z-Order:** It maintains a floating always-on-top window level (`ui::ZOrderLevel::kFloatingWindow`) so it stays visible while the user references other applications.

## 2. Dismiss-on-Blur (Ephemeral Focus)
- **Rule:** The Maho Mini window is ephemeral. When it loses focus (blurs) to another application or window, it automatically closes.
- **Exceptions:**
  - Standard menus, tooltips, or permission prompts spawned by the Maho Mini window itself must not trigger dismissal.
  - While dragging/selecting text or interacting with sub-dialogs, focus changes within the window are ignored.
- **Timing:** Dismissal must happen immediately on window blur (`views::WidgetObserver::OnWidgetActivationChanged` where active is false).

## 3. Single-Instance Reuse Semantics
- **Rule:** There must be at most one active Maho Mini window per profile.
- **Action:** If a new URL is routed to Maho Mini while a Maho Mini window is already open:
  - Do NOT open a new window.
  - Focus/Activate the existing Maho Mini window.
  - Update the active WebContents of the existing window to load the new target URL.
  - Center/reposition the existing window if needed.

## 4. Promotion & Archival Lifecycle
- **Promote-to-Tab Gesture:** The user can promote the active page in the Maho Mini window to a standard tabbed window using:
  - Command: `PromoteToTab` action or clicking the "Open in Space/Tab" button.
  - Keyboard: `⌘O` or `⌘T` or a dedicated button.
- **Promotion Behavior:**
  - The WebContents of the popup is detached.
  - It is inserted as a new tab into the last active regular tabbed browser window under the same profile.
  - **Fallback:** If no tabbed browser window exists for the profile, a new one is created.
  - The Maho Mini window is closed.
- **Auto-Archive Policy:**
  - If the Maho Mini window is dismissed (closed via blur or close button) without being promoted, its tab is discarded/archived (its session state is not restored, and it does not pollute the user's active spaces or tabs).
