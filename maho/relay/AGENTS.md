# PROJECT KNOWLEDGE BASE — relay

**Generated:** 2026-05-15

## OVERVIEW
Standalone Rust workspace for the Maho sync relay server. It accepts authenticated WebSocket traffic, stores encrypted blobs in SQLite WAL, and never decrypts payloads server-side.

## STRUCTURE
- `server/` — axum server, WebSocket handler, auth routes, room state, config, tests
- `entity/` — SeaORM entities and model definitions
- `migration/` — schema migrations for SQLite

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Server runtime | `server/src/main.rs` | startup, WAL setup, pruning, `server::run()` |
| WebSocket behavior | `server/src/server.rs`, `server/src/handler.rs` | upgrade flow, auth, message forwarding |
| HTTP auth | `server/src/auth/` | signup/login/logout/refresh/device routes |
| Waitlist signups | `server/src/waitlist.rs` | Public `POST /waitlist` for marketing site (no auth); IP-hashed, 5/min rate limit, idempotent dedupe |
| Persistence | `server/src/storage.rs`, `entity/` | SeaORM reads/writes and models |
| Room lifecycle | `server/src/room.rs` | in-memory peer and room tracking |
| Schema changes | `migration/` | SQLite migrations only |

## CONVENTIONS
- Keep the relay isolated from the main `maho/` Cargo workspace.
- Use SQLite WAL mode and SeaORM migrations for schema changes.
- Treat room messages as opaque blobs; the server stores and forwards them without interpretation.
- Keep auth defaults production-safe: `require_auth=true`, `open_registration=false`.

## ANTI-PATTERNS (THIS PROJECT)
- No adding `relay/` crates to the root `maho/Cargo.toml` workspace members.
- No decrypting or inspecting payload contents on the server.
- No bypassing auth/registration defaults without an explicit provisioning flow.
- No schema changes without matching `migration/` updates.

## COMMANDS
```bash
cd relay && cargo test
cd relay && cargo clippy --all-targets --all-features -- -D warnings
cd relay && cargo build --release
```

## NOTES
- The relay has its own `Cargo.toml` and `Cargo.lock`; work from `maho/relay/`, not the root workspace.
- The workspace exists separately because of the `libsqlite3-sys` dependency conflict.
- See `README.md` for protocol, deployment, and configuration details.
