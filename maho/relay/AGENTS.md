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
│   ├── lib.rs          # Worker entry + Router (all route registrations) + scheduled() retention job + RoomDO
│   ├── auth/           # Signup/login/refresh/verify/bypass + middleware
│   │   ├── routes.rs   # HTTP handlers; service/admin token helpers (require_service_token/require_admin_token/extract_bearer)
│   │   ├── tokens.rs   # Opaque token issue/hash (CSPRNG failures propagate, never 0-fill)
│   │   ├── passwords.rs # Argon2 hashing
│   │   ├── models.rs   # User/session types
│   │   └── middleware.rs # Auth guard extractor
│   ├── billing.rs      # LemonSqueezy webhook (claim-first idempotency) + portal session + invoices + credit routes
│   ├── sync/           # Snapshot upload/download (device-room attribution); WebSocket (/ws)
│   ├── room.rs         # WebSocket room/broadcast; unauth-socket alarm reaper
│   ├── jobs.rs         # Pure handoff-job state machine (bill-once decisions)
│   ├── jobs_routes.rs  # Service-token job routes (create/get/complete/fail/progress)
│   ├── metrics.rs      # Admin funnel metrics (server-derived, aggregate-only)
│   ├── updates.rs      # Config update distribution (ed25519-signed, rollout buckets)
│   ├── db/             # Cloudflare D1 helper wrappers
│   │   ├── mod.rs      # Users/devices/sessions/credits/rooms/messages helpers
│   │   ├── jobs.rs     # handoff_jobs D1 helpers (conditional UPDATE + RETURNING)
│   │   └── config_updates.rs # config_updates D1 helpers
│   ├── config.rs       # Env-var bindings (single source: RelayConfig, VariantMap, tier_ceiling_usd, snapshot cap)
│   ├── errors.rs       # AppError → HTTP response mapping (5xx bodies generalized + error_id)
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
Auth subject column prevents the T3-class bug (a client calling a service-only route): **user** = user access token, **service** = `MAHO_RELAY_SERVICE_TOKEN` (Worker-to-Worker only, never shipped to clients), **admin** = `MAHO_ADMIN_TOKEN`, **none** = unauthenticated, **signature** = ed25519/HMAC.

| Route | Method | Domain | Auth subject |
|-------|--------|--------|--------------|
| `/health` | GET | Health | none |
| `/auth/signup`, `/auth/login`, `/auth/logout`, `/auth/refresh` | POST | Auth | none (credentials in body) |
| `/auth/me`, `/auth/subscription` | GET | Auth | user |
| `/auth/devices/:id` | DELETE | Auth | user |
| `/auth/request_verify`, `/auth/verify` | POST | Auth | user |
| `/auth/introspect`, `/auth/consume_bypass` | POST | Auth | service |
| `/auth/admin/force_verify_email`, `/auth/admin/set_credit_balance` | POST | Admin | admin |
| `/admin/metrics/funnel` | GET | Admin | admin |
| `/billing/lemonsqueezy` | POST | Billing | signature (HMAC) |
| `/billing/portal_session` | POST | Billing | user |
| `/billing/invoices` | GET | Billing | user |
| `/billing/credit_balance` | GET/POST | Billing | service |
| `/billing/consume_credit` | POST | Billing | service |
| `/jobs/create`, `/jobs/complete`, `/jobs/fail`, `/jobs/progress` | POST | Jobs | service |
| `/jobs/:id` | GET | Jobs | service |
| `/sync/snapshot/upload` | POST | Sync | user |
| `/sync/snapshot/latest` | GET | Sync | user |
| `/updates/config` | GET | Updates | none |
| `/updates/config` | POST | Updates | admin (`CONFIG_UPDATE_ADMIN_TOKEN`, signature-verified) |
| `/ws` | WS | Real-time sync | user (first-message auth) |
| `/waitlist` | POST | Marketing | none |

## PRODUCTION DOMAINS
`relay.mahobrowser.com`, `update.mahobrowser.com`, `updates.mahobrowser.com`, `proxy.mahobrowser.com`

## CONVENTIONS
- Runtime: Cloudflare Workers WASM (`worker` crate v0.4). No `std::net`, no `tokio`, no `std::fs`.
- DB: Cloudflare D1 (SQLite dialect) via `worker::d1`. Binding name `MAHO_DB`.
- Errors: All handlers return `AppError`; `errors.rs` maps to HTTP responses. 5xx bodies are generalized to `internal_error` + a random `error_id` (logged); 4xx strings are a client contract — do not change them.
- Auth guard: user routes call `auth::middleware::get_auth_context`; service routes call `auth::routes::require_service_token`; admin routes call `auth::routes::require_admin_token`. All bearer parsing goes through `extract_bearer` (single site).
- Secrets: accessed via `env.secret("KEY_NAME")`, never hardcoded. Admin routes use a dedicated `MAHO_ADMIN_TOKEN` (fail-closed: 500 when unset), NOT the service token.
- Config: `config.rs` (`RelayConfig`) is the single source for env-tunable values; no reading the same env var directly elsewhere, and no declaring config keys nothing reads.
- **D1 concurrency: use a single conditional `UPDATE/INSERT … RETURNING` and judge the effect by whether a row came back — never read-then-write across an `await`.** Money/idempotency paths depend on this (`claim_event`, `try_consume_credit`, `try_complete_job`, snapshot/seq upserts, waitlist).
- D1 row deserialization uses a named `#[derive(Deserialize)]` struct; `first::<(...)>` tuple deserialization is forbidden (it fails at runtime — see T1).
- CSPRNG failures (`getrandom`) must propagate or panic — never `let _ =` (a swallowed error yields predictable zero tokens/salts).
- **No new client → Maho-host outbound calls** (zero-telemetry promise). Funnel/retention is derived server-side from `user_activity_days`, never a client ping.

## ANTI-PATTERNS
- No `tokio::` runtime primitives — Workers runtime is single-threaded WASM
- No standard filesystem I/O
- No adding relay crates to the root `maho/Cargo.toml` workspace
- No mixing admin-only routes with user routes without auth guard
- No in-memory `static` counters for rate limiting — isolate-local, so shared limits are illusory (Workers spins many isolates per colo)
- No leaking internal error detail (SQL, table/column names, serde field paths) in 5xx bodies
- No read-then-write for any state that can be mutated concurrently — use conditional `UPDATE … RETURNING`

## SECRETS & KEYS
- `MAHO_RELAY_SERVICE_TOKEN` — Worker-to-Worker (billing-proxy ↔ relay). Never shipped to clients.
- `MAHO_ADMIN_TOKEN` — gates `/auth/admin/*` and `/admin/metrics/funnel`. Register before deploy (`wrangler secret put`), else those routes fail closed with 500.
- `CONFIG_UPDATE_ADMIN_TOKEN` — separate token for `POST /updates/config` (kept distinct on purpose).
- `ARGON2_PEPPER`, `LS_WEBHOOK_SECRET`, `LEMONSQUEEZY_API_KEY`, `RESEND_API_KEY` — as before.
- `LS_VARIANT_*` (public product IDs, `[env.*.vars]`, not secrets) — must be the real store variant IDs or paid tiers record as free.
- Removed keys (do not reintroduce without an implementation): `MAX_CONNECTIONS_PER_IP`, `MAX_ROOMS_PER_IP`, `*_RATE_LIMIT_PER_IP_PER_MINUTE` (superseded by the edge rate-limit backend).

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
