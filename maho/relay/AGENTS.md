# PROJECT KNOWLEDGE BASE — maho/relay

**Generated:** 2026-07-14
**Commit:** 4fd48142
**Branch:** main

## OVERVIEW
Standalone Cloudflare Workers relay server compiled to WASM via `worker-build`. Serves as auth authority, sync backend, billing webhook processor, and update distribution endpoint for Maho products. Separate Cargo workspace — do NOT add to `maho/Cargo.toml` members (libsqlite3-sys conflict).

## STRUCTURE
```
relay/
├── src/
│   ├── lib.rs          # Worker entry + Router (all route registrations)
│   ├── auth/           # Signup/login/refresh/verify/bypass + middleware
│   │   ├── routes.rs   # HTTP handlers
│   │   ├── tokens.rs   # JWT issue/verify/revoke
│   │   ├── passwords.rs # Argon2 hashing
│   │   ├── models.rs   # User/session types
│   │   └── middleware.rs # Auth guard extractor
│   ├── billing.rs      # LemonSqueezy webhook + portal session + invoices
│   ├── sync/           # Snapshot upload/download; WebSocket (/ws)
│   ├── room.rs         # WebSocket room/broadcast management
│   ├── db/             # Cloudflare D1 helper wrappers
│   ├── config.rs       # Env-var bindings (secrets, D1 binding name)
│   ├── errors.rs       # AppError → HTTP response mapping
│   └── waitlist.rs     # POST /waitlist handler
├── migrations/         # Numbered SQL migration files (D1 schema)
└── wrangler.toml       # Cloudflare deployment config (routes, D1, env)
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Add a new route | `src/lib.rs` router chain | Wire handler there first |
| Auth logic | `src/auth/routes.rs`, `src/auth/tokens.rs` | JWT + Argon2 |
| Protect a route | `src/auth/middleware.rs` | Auth guard extractor |
| Billing events | `src/billing.rs` | LemonSqueezy webhook path |
| Sync protocol | `src/sync/` | Snapshot upload/download; WS in `room.rs` |
| Schema change | `migrations/` | New numbered `.sql` file |
| Secrets/bindings | `wrangler.toml` + `src/config.rs` | D1 binding = `MAHO_DB` |

## API SURFACE
| Route | Method | Domain |
|-------|--------|--------|
| `/auth/signup`, `/auth/login`, `/auth/logout`, `/auth/refresh` | POST | Auth |
| `/auth/me`, `/auth/subscription` | GET | Auth |
| `/auth/devices/:id` | DELETE | Auth |
| `/auth/introspect`, `/auth/request_verify`, `/auth/verify`, `/auth/consume_bypass` | POST | Auth |
| `/auth/admin/force_verify_email`, `/auth/admin/set_credit_balance` | POST | Admin |
| `/billing/lemonsqueezy`, `/billing/portal_session` | POST | Billing |
| `/billing/invoices` | GET | Billing |
| `/sync/snapshot/upload` | POST | Sync |
| `/sync/snapshot/latest` | GET | Sync |
| `/ws` | WS | Real-time sync |
| `/waitlist` | POST | Marketing |

## PRODUCTION DOMAINS
`relay.mahobrowser.com`, `sync.mahobrowser.com`, `update.mahobrowser.com`, `updates.mahobrowser.com`, `proxy.mahobrowser.com`

## CONVENTIONS
- Runtime: Cloudflare Workers WASM (`worker` crate v0.4). No `std::net`, no `tokio`, no `std::fs`.
- DB: Cloudflare D1 (SQLite dialect) via `worker::d1`. Binding name `MAHO_DB`.
- Errors: All handlers return `AppError`; `errors.rs` maps to HTTP responses.
- Auth guard: handlers requiring auth call `auth::middleware::require_auth(&req, &env)`.
- Secrets: accessed via `env.secret("KEY_NAME")`, never hardcoded.

## ANTI-PATTERNS
- No `tokio::` runtime primitives — Workers runtime is single-threaded WASM
- No standard filesystem I/O
- No adding relay crates to the root `maho/Cargo.toml` workspace
- No mixing admin-only routes with user routes without auth guard

## COMMANDS
```bash
# From maho/relay/
cargo test                              # unit tests (native target)
cargo clippy -- -D warnings
worker-build --release                  # build WASM output to build/
wrangler dev                            # local dev server
wrangler deploy --env production        # deploy to production
```

## NOTES
- `wrangler.toml` has `account_id` hardcoded — do not rotate without updating this.
- Migrations are in `migrations/` as numbered SQL files; apply via `wrangler d1 migrations apply`.
- `build/` directory contains WASM output — gitignored except `.gitignore`, `package.json`.
