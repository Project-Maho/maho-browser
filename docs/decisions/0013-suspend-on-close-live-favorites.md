# 13. Suspend-on-Close for Live Favorites

## Status

**Accepted**, 2026-07-12.

---

## Context

Under Favorites Model B (ADR 0012), a favorite is a persistent, URL-addressed entry that may or may not have a live `WebContents` at any instant. "No live tab" is an explicitly valid, resolvable state, not a bug.

This leaves one behavior undefined: when a user closes a favorite that currently *is* live, what happens? Question H3 in `.omo/plans/sidebar-tab-role-remediation-2026-07-12.md` framed the choice as suspend versus delete. Deleting on close would contradict Model B (it would treat "live" as the favorite's only valid form), while doing nothing is not an option because the user asked to close the tab. We need a ratified rule so the Rust core and C++ UI agree on the resulting state.

---

## Decision

We adopt **suspend-on-close** for live favorites:

1. **Closing a live favorite suspends it**: Closing drops the live `WebContents` but keeps the favorite entry. The tab transitions to the Suspended lifecycle state and retains `TabRole::Favorite`. It is never removed from favorites by a close.
2. **Consistent with Model B**: A favorite need not be live. Suspending on close simply moves the entry from the "live" side of Model B to the "no live tab" side, which is already a first-class state (ADR 0012).
3. **Distinct from Pinned close-protection**: Pinned tabs keep their existing close semantics per ADR 0011. Favorites are not "close-protected" in that sense; closing a favorite is allowed, it just demotes the tab to non-live rather than removing it.
4. **Removal is explicit only**: The sole way to remove a favorite is an explicit "Remove from Favorites" role transition (the `ChangeTabRole` / favorite toggle path), never a close.

---

## Implementation Note

The behavior lives in `maho/crates/maho-core/src/maho_core.rs` in the `CloseTab` handler (around lines 3989-3996). For a favorite tab, the handler calls `suspend_tab`, persists, and syncs, then returns without deleting the entry. Cross-device, the suspended state propagates through the normal `Tab` entity sync path, so a favorite suspended on one device shows as suspended elsewhere.

---

## Consequences

- **Matches the Model B mental model**: Closing a favorite maps cleanly onto the already-valid "favorite with no live tab" state.
- **No accidental favorite loss**: A close can never silently drop a favorite from the dock.
- **Coherent cross-device state**: Suspension syncs through the standard Tab path, so devices stay consistent without special-casing.
- **Risk: close may not mean "gone"**: A closed favorite still appears in the favorites dock, which can surprise users who expect close to equal removal. This is mitigated by the clear, explicit "Remove from Favorites" action.
- **Window-scoped close propagation (P1)**: On desktop, favorites are shared, so closing a live favorite in one window suspends it in all windows. This is consistent with the suspend-on-close policy and the shared-favorite model.
