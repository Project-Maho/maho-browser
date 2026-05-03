# maho-storage

## OVERVIEW

Persistence layer: SQLite (relational), LMDB (fast K/V), Yrs CRDT (sync). 5 files, 240 lines.

## STRUCTURE

| File | Lines | Role |
|------|-------|------|
| `sqlite.rs` | 104 | Relational storage — bookmarks, history, settings |
| `crdt.rs` | 64 | Yrs-based CRDT — cross-device sync for spaces/tabs |
| `lmdb.rs` | 47 | Fast key-value — session state, tab snapshots |
| `error.rs` | 19 | `StorageError` enum via thiserror |
| `lib.rs` | 6 | Re-exports |

## WHERE TO LOOK

| Task | File |
|------|------|
| Persistent user data (bookmarks, history) | `sqlite.rs` |
| Fast ephemeral state (session, cache) | `lmdb.rs` |
| Cross-device sync | `crdt.rs` |
| New error variant | `error.rs` |

## CONVENTIONS

- `thiserror` for all error types
- `tempfile` for test databases
- Each backend is a standalone module — no cross-backend coupling
- Caller (maho-core) decides which backend to use
