# MAHO-CORE KNOWLEDGE BASE

**Scope:** `crates/maho-core/` — shared domain library.

## OVERVIEW
Rust library housing the domain models, database layer, and service logic shared by the Tauri app, CLI, and MCP server. No Tauri dependency.

## STRUCTURE
```
maho-core/src/
├── lib.rs          # Crate entry, re-exports
├── error.rs        # Domain error types (thiserror)
├── db_path.rs      # Cross-platform DB path resolution
├── db/             # SQLCipher init, schema migrations
├── models/         # Domain types (account, email, folder, attachment, ai, search, saved_search)
└── services/       # Business logic (account, email, folder, ai, search, retry, offline_queue)
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| DB initialization | `db/mod.rs` | Called by `src-tauri/src/lib.rs` setup |
| Schema migrations | `db/migrations.rs` | Versioned SQL migrations |
| Models | `models/` | `account`, `email`, `folder`, `attachment`, `ai`, `search`, `saved_search` |
| Services | `services/` | `account`, `email`, `folder`, `ai`, `search`, `retry`, `saved_search`, `offline_queue` |
| DB path helper | `db_path.rs` | Platform-specific database path resolution |

## CONVENTIONS
- No `tauri::` imports — this crate must remain framework-agnostic.
- `rusqlite` accessed via workspace dependency (SQLCipher enabled).
- Error types use `thiserror` with `From` impls for `rusqlite::Error`.
- Services take `&Connection` as parameter — no state management.
- Benchmarks live in `benches/` using Criterion harness.
- The crate is intentionally flat: models are data-only, services are stateless free functions, and DB code stays in `db/`.

## ANTI-PATTERNS
- No `unwrap()`/`expect()` in non-test code — propagate errors with `?`.
- No `use crate::db::*` glob imports — explicit imports preferred.
- No Tauri command handler patterns in services — keep pure domain logic.

## NOTES
- `db_path.rs` handles platform-appropriate data directory resolution.
- Service layer follows a stateless "module" pattern (not trait-based services).
- Schema migrations are sequential integers in `migrations.rs`.
- The existing top-level AGENTS file is enough for navigation here; no deeper `models/`, `services/`, or `db/` AGENTS are needed.
