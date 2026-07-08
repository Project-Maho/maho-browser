# 11. Decoupled Favorites Close-Protection Policy

## Status

**Accepted**, 2026-07-08.

---

## Context

Under A-architecture, favorite and pinned tabs were tightly coupled to the close lifecycle. This resulted in complex, error-prone custom close behaviors, confirmation dialogs, or custom URL reset logic. 

Under B-architecture, we treat favorite and pinned states as dynamic roles mapped onto normal tab models. This requires a explicit decision on how closing, unloading, and removing favorite/pinned tabs should behave to prevent state discrepancies between the Rust core and C++ UI.

---

## Decision

We decouple the favorites close lifecycle to allow normal close and unload operations to happen natively:

1. **Closing a Pinned or Favorite Tab**: Clicking the close/minus button on a favorite or pinned tab closes/destroys the tab normally. We do not block the user with confirmation dialogs or preserve the tab in a ghost state.
2. **Removing a Favorite Tab via Context Menu**: Selecting "Remove" from the favorites context menu does NOT close the tab. Instead, it transitions its role back to normal (dispatching a `"change_tab_role"` event with a `"normal"` target role). The tab remains open and transitions back to the normal tab list.
3. **Tab Unload / Suspension**: Pinned and favorite tabs can be unloaded or suspended (e.g. under memory pressure or manually) in the same manner as normal tabs, without affecting their pinned/favorite status in the sidebar.

---

## Consequences

- **Simplified State Model**: No custom close-protection tracking or complex edge cases where closed tabs remain as phantom views.
- **Improved UX Consistency**: Tab closure behaves consistently across all sections of the sidebar (normal, pinned, favorites).
- **FFI Stability**: Prevents production regression risks by eliminating the need to handle complex, asynchronous close-confirmation loops across the C++ / Rust FFI boundary.
