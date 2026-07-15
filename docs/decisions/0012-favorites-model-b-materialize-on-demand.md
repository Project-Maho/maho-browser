# 12. Favorites Model B Materialize-on-Demand Invariant

## Status

**Accepted**, 2026-07-12.

---

## Context

In our B-architecture migration, we previously pursued an "always-live" stance for favorites (as defined in `.omo/plans/favorites-b-arch-migration-2026-07-08.md` steps T5.1 / T5.3 / T5.5). However, this Model A approach led to recurring bugs, including blank tiles, ghost favorites after deletion, Cmd+N / click no-ops, and dual highlights. 

We need a clear, unified invariant to govern the relationship between a favorite entry and a live tab, ensuring that "favorite with no live tab" is treated as a valid, resolvable state.

---

## Decision

We commit fully to Model B:

1. **Model B Definition**: A favorite is a persistent, URL-addressed entry (stable `TabId` + url + title + favicon + order) that may or may not have a live `WebContents` at any instant.
2. **Materialize-on-Demand**: A favorite is resolved to a live `WebContents` through exactly one chokepoint. If the `TabId` maps to an existing live tab, focus it. Otherwise, materialize it by navigating to the entry's URL and rebinding the *same* stable `TabId` (never minting a new ID). "No live tab" is a valid state, never a bug.
3. **Atomic Deletion**: Deleting a favorite removes the entry from favorite metadata and space `root_order`, and atomically closes/detaches any live `WebContents`.
4. **Favicon Persistence**: The favicon is a property of the entry, not the live tab. It is captured when a live tab reports one, persisted on `tab.favicon`, and rendered from the entry when no live tab exists (with an async `FaviconService`-by-URL fallback).

This decision explicitly supersedes `.omo/plans/favorites-b-arch-migration-2026-07-08.md` steps T5.1, T5.3, and T5.5.

---

## Consequences

- **Robust Lifecycle**: Favorites survive browser restarts and tab suspensions without requiring eager re-opening of all favorites.
- **No Ghosts**: Atomic deletion prevents mismatch between metadata lists and active tab states.
- **Persistent Visuals**: Renders favicons correctly from entry metadata even when the backing tab is suspended or closed.
