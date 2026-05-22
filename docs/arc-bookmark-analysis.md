# Arc Bookmark Analysis

## Arc model

Arc desktop explicitly does not have bookmarks. Its organizational primitives are:

- **Pinned Tabs** ... persistent tabs that survive session restarts, pinned per-space
- **Favorites** ... a top-level row of pins visible across all spaces
- **Spaces** ... workspaces that partition the sidebar into contexts
- **Folders** ... grouping within a space's sidebar
- **Library / Archive** ... where closed tabs go; searchable, auto-archived over time

The sidebar *is* the bookmark system. There's no separate bookmark manager, no bookmark bar, no hierarchical folder tree in the Chrome sense. Users pin what matters; everything else flows through the archive.

## Maho current state

Maho has real bookmark infrastructure on the backend:

- `maho-core` bookmark manager with CRUD operations
- `maho-ffi` bridge exposing bookmark operations to the C++ layer
- `maho-storage` persistence for bookmark data
- `maho-types` bookmark contract types

However, there's limited first-class desktop UI for bookmarks. No dedicated bookmark manager panel or library view ships in the Chromium overlay today.

## Key comparison

| Concept | Arc | Maho today |
|---------|-----|------------|
| Persistent pages | Pinned tabs + Favorites | Backend bookmark store, minimal UI |
| Organization | Spaces, folders, sidebar | Not yet implemented at UX layer |
| Classic bookmarks | Rejected by design | Backend exists, UI underbuilt |
| Import from Chrome | Converted to pins on import | Supported at data layer |

## Corrected conclusion

If Arc parity is the goal, building a separate bookmark manager or bookmark library panel is over-scoped and directionally wrong. Arc succeeded by *eliminating* the bookmark concept, not by improving it.

The right direction for Maho:

1. Treat the existing bookmark backend as a **compatibility and import substrate**, not a first-class user-facing feature.
2. Build UX around **pinned tabs, favorites, and sidebar organization** (spaces, folders, archive).
3. Classic bookmark data serves import (Chrome, Firefox, Safari) and sync interop, but doesn't need its own manager UI.

## Remaining work

- Design and implement pinned tabs UX (per-space persistence, visual treatment)
- Add favorites row (cross-space pins)
- Build spaces/folders as sidebar organization
- Implement archive behavior for closed tabs
- Wire bookmark import to create pins rather than populating a hidden bookmark tree
- Decide whether to expose a "show all bookmarks" escape hatch for power users migrating large collections
