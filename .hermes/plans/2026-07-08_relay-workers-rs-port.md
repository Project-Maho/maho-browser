# Maho Relay — CF Workers Port Plan (Plan B)

> **Status**: READY for delegation. All architectural decisions locked 2026-07-08.
> **Scope**: Port `maho/relay/` (4,388 LOC Rust: axum + tokio + SeaORM + SQLite) to Cloudflare Workers (workers-rs + D1 + Durable Objects). Also update 12+ client-side constant strings across `maho/`, `maho-chromium/`, `mail/`, `.github/workflows/`.
> **Effort estimate**: **3–5 days fulltime** for an experienced Rust + Workers dev, or ~10 focused delegated sessions (one per Phase 2A–2K).
> **Related**: `.hermes/plans/2026-07-08_maho-website-launch-prep-v2.md` (Plan A — website. Run Plan A first or in parallel.)
> **Prerequisites confirmed**: Workers Paid $5/mo subscribed 2026-07-08.

---

## TL;DR

Replace the traditional Rust server (axum + tokio + SeaORM + SQLite) with a Cloudflare Workers deployment (workers-rs + D1 + Durable Objects). Preserve the full endpoint contract from `maho/relay/README.md` (HTTP auth, `POST /waitlist`, `POST /billing/*` webhook, WebSocket `/ws` sync). Deploy to `relay.mahobrowser.com` + related subdomains via Workers Custom Domains.

Also mechanically update 12+ client-side files that hardcode the fictional `sync.maho.app` etc. to the real `sync.mahobrowser.com`. These are Phase 2J — pure string replacement, agy-friendly.

---

## Delegation Contract — READ FIRST

This plan is designed for multiple delegated sessions (one per Phase 2A–2K, or grouped). Each phase is executable independently once its prerequisites are met.

Each delegated session:

1. Reads this plan file as sole source of truth
2. Confirms prerequisite Phases (per Phase's `Prerequisites` block) are complete before starting
3. Executes tasks; commits to `main` directly (no PR)
4. **Does NOT push** — operator pushes after review
5. Uses `CLOUDFLARE_API_TOKEN` env var for CF-side operations (D1 create, DO deployment, Custom Domain binding)
6. Escalates to operator only if a task fails after 3 retries or hits an architecture question not covered in this plan

### CF token scope requirements

The delegated session's `CLOUDFLARE_API_TOKEN` must have:
- `Account: Cloudflare Workers Scripts: Edit`
- `Account: D1: Edit`
- `Account: Durable Objects: Edit` (usually included with Workers)
- `Zone: DNS: Edit` (for Custom Domain bindings on `mahobrowser.com` zone)
- `Zone: Zone Settings: Edit`

Operator should ensure token has these scopes before delegating. Verify via:
```bash
curl -s -H "Authorization: Bearer $CLOUDFLARE_API_TOKEN" \
  "https://api.cloudflare.com/client/v4/accounts/b1c1bfc44b1313b6f51553095ccb2eb4/workers/scripts" \
  | python3 -c "import sys,json; print('workers ok:', json.load(sys.stdin).get('success'))"
```

### Wrangler CLI

`bunx wrangler` is available at v4.62+ (already used in Day 1 ops for the website). No additional install needed.

---

## Locked Architecture Decisions

| ID | Decision | Rationale |
|---|---|---|
| **A1** | Runtime: **workers-rs** (Rust) | Preserve business logic + type safety from existing Rust code. Team is Rust-fluent. |
| **A2** | Datastore: **D1** for relational tables (users, devices, sessions, waitlist_signups, credits, snapshots, room_meta, messages, etc.) | Natural fit for existing SQLite schema. Free tier: 5GB + 5B rows read/mo. |
| **A3** | WebSocket + room state: **Durable Objects** (one class `RoomDO`, one instance per `room_id`) | Only CF-native way to maintain per-room state + WebSocket connections. Workers Paid required (subscribed 2026-07-08). |
| **A4** | ORM: **none** — raw SQL via `worker::D1Database::prepare(...).bind(...).run/first/all()` | D1 has no SeaORM adapter. Raw SQL is idiomatic for D1. |
| **A5** | Migrations: **wrangler d1 migrations** (raw SQL files under `migrations/`) | Convert 10 existing sea-orm-migration files to raw SQL. Version-controlled in repo. |
| **A6** | Auth model: **preserve current** — password + session/refresh token in D1 (not stateless JWT) | Minimizes semantic change. Existing clients (browser, mail app) already expect this contract per README. |
| **A7** | Password hashing: **argon2** crate compiled to WASM | Pure-Rust `argon2` crate compiles for `wasm32-unknown-unknown`. Tuned params (`m_cost=19456, t_cost=2, p_cost=1`) to fit Workers 30s CPU budget. |
| **A8** | HTTP outbound (LemonSqueezy webhook verify, etc.): **`worker::Fetch`** | Replaces `reqwest`. Cleaner Workers integration. |
| **A9** | Deployment layout: single Worker binary at `relay.mahobrowser.com` handles ALL HTTP + `/ws` upgrade | Simpler than multi-worker. Sync/update/proxy subdomains bind to same worker via multiple Custom Domains (workers routing header discrimination). |
| **A10** | Subdomains binding: `relay`, `sync`, `update`, `updates`, `proxy` — all point to same Worker | Same worker handles routing internally. `update` and `updates` normalize to one canonical (server serves both). `proxy` may split later; initially same. |
| **A11** | Test strategy: **Miniflare v3** for local dev, **integration tests** hitting deployed staging worker | Unit tests via `wasm-bindgen-test` where possible; integration via curl + wscat against `relay-staging.mahobrowser.com`. |
| **A12** | Workspace layout: **flatten** `maho/relay/` from 3-crate workspace to single Cargo crate targeting `wasm32-unknown-unknown` | Cargo Workers projects don't support multi-crate workspaces cleanly. Merge `entity/` + `migration/` + `server/` into single crate structure. Business logic modules preserved under `src/`. |
| **A13** | Environment / secrets: **wrangler.toml `[vars]` + wrangler secrets** for API keys (Resend, LemonSqueezy signing secret) | Standard Workers pattern. No `.env` file at runtime. |
| **A14** | Rate limiting: preserve current in-Worker implementation (fixed-window per-IP) for signup/login/waitlist. Use `worker::caches` or DO for state. WebSocket connection rate limit moves into `RoomDO`. | CF has built-in Rate Limiting Rules but they're zone-level; per-endpoint fixed-window is easier in code. |
| **A15** | Retention job (hourly maintenance): **Cron Trigger** in wrangler.toml (`crons = ["0 * * * *"]`) invokes `scheduled()` handler. | Replaces `main.rs` hourly loop. |
| **A16** | CORS: manual headers in each response, allowlist `https://mahobrowser.com` + `https://maho-website.pages.dev` (staging). | Replaces `tower-http::cors`. |
| **A17** | Logging: `worker::console_log!()` + `tracing` if compat. Logs land in `wrangler tail` and CF dashboard Logs. | Replaces `tracing-subscriber` stdout. |
| **A18** | Deploy target hostnames: `relay.mahobrowser.com` (prod), `relay-staging.mahobrowser.com` (staging environment). | Wrangler environments pattern. |

---

## Preserved Contracts (from `maho/relay/README.md`)

The Workers port MUST preserve these endpoint contracts. Deviations are architectural failures.

### HTTP Auth (all under `/auth`)
- `POST /auth/signup` — body: `{email, password, display_name, device_name, device_type}`; response: `{access_token, refresh_token, user, device}` or `{error: string}` with appropriate HTTP status
- `POST /auth/login` — body: `{email, password, device_name?, device_type?}`
- `POST /auth/logout` — body: `{refresh_token}`; deletes session
- `POST /auth/refresh` — body: `{refresh_token}`; returns new access + refresh
- `GET /auth/me` — requires `Authorization: Bearer <access_token>`; returns `{user, device}`
- `DELETE /auth/devices/:id` — requires auth; removes a device + its sessions

### WebSocket (`GET /ws`, upgrade)
- Client sends within 5s of connect: `{"type":"auth","key":"<room_id>","token":"<access_token>","since_seq":<n>}` (or without `token` if `require_auth=false`)
- Server responds `{"type":"auth_ok"}` on success, `{"type":"error","msg":"..."}` on failure (then close)
- If `since_seq` present: server streams `{"type":"catchup","seq":N,"blob":"<b64>"}` messages, then `{"type":"catchup_done"}`
- After auth_ok, client sends text/binary frames up to 1 MB. Server stores, assigns seq, broadcasts to other peers in room, sends `{"type":"ack","seq":N}` to sender.

### Waitlist (`POST /waitlist`)
- Public (no auth). Body: `{email, source, metadata}`. Response: `{ok: bool, already_signed_up?: bool}` or `{ok: false, error: string}`.
- Rate limit: 5/min per SHA-256(IP+salt). Idempotent dedupe on email.

### Billing (`/billing/webhook` etc.)
- LemonSqueezy webhook signature verification (HMAC-SHA256 with signing secret).
- Body: LS event JSON. Actions: create/update user's subscription tier, credit balance, has_payment_method flag.

### Sync HTTP (`/sync/*`)
- `POST /sync/snapshot/upload` — auth required; body: opaque blob; stores in `snapshots` table
- `GET /sync/snapshot/latest?device_id=X` — returns latest snapshot for device

### Health
- `GET /health` → `"ok"` (200)

### Auth policy invariants
- Access tokens: 1 hour expiry
- Refresh tokens: 30 day expiry
- Failed-login lockout: 10 attempts → 15 min lock
- Signup rate limit: 3/min per IP (default; env-configurable)
- Login rate limit: 10/min per IP (default; env-configurable)
- Min password length: 12 (default; env-configurable)

---

## Current State Reference

### Source tree
```
maho/relay/
├── Cargo.toml              # Workspace root; members: server, entity, migration
├── clippy.toml
├── AGENTS.md
├── README.md
├── server/                 # 4,097 LOC
│   ├── src/
│   │   ├── main.rs         # (66)   startup, WAL setup, hourly maintenance
│   │   ├── lib.rs          # (9)
│   │   ├── server.rs       # (151)  axum Router + AppState + rate limiter + WS upgrade
│   │   ├── handler.rs      # (496)  WS handler: auth flow, message loop, broadcast
│   │   ├── config.rs       # (220)  CLI + env config parsing
│   │   ├── room.rs         # (200)  RoomManager: in-memory DashMap of peer sets
│   │   ├── storage.rs      # (915)  SeaORM persistence layer: messages, sessions, credits, etc.
│   │   ├── waitlist.rs     # (177)  POST /waitlist handler
│   │   ├── billing.rs      # (669)  LemonSqueezy webhook + credit management
│   │   ├── auth/
│   │   │   ├── mod.rs      # (5)
│   │   │   ├── routes.rs   # (808)  signup/login/logout/refresh/me/devices
│   │   │   ├── middleware.rs # (63) Authorization header extraction
│   │   │   ├── models.rs   # (276)  Request/response types
│   │   │   ├── passwords.rs# (19)   argon2 wrapper
│   │   │   └── tokens.rs   # (44)   token generation
│   │   └── sync/
│   │       ├── mod.rs      # (2)
│   │       ├── routes.rs   # (209)  snapshot upload/download
│   │       └── models.rs   # (59)   types
│   └── tests/
│       ├── auth_routes.rs         # HTTP auth tests
│       ├── ws_require_auth.rs     # require_auth WebSocket tests
│       ├── integration.rs         # WebSocket protocol tests
│       ├── e2e_sync.rs            # E2E sync simulations
│       └── common/mod.rs          # Test helpers
├── entity/                 # 143 LOC (SeaORM entities)
│   └── src/
│       ├── lib.rs
│       ├── users.rs, devices.rs, sessions.rs
│       ├── message.rs, room_meta.rs
│       ├── snapshots.rs, waitlist_signups.rs
│       ├── credits.rs, consumed_bypass_tokens.rs, processed_events.rs
└── migration/              # 900 LOC (sea-orm-migration)
    └── src/
        ├── lib.rs
        └── m20260424_000001_create_tables.rs        (101 LOC)
        └── m20260502_000002_create_auth_tables.rs   (242 LOC)
        └── m20260627_000003_add_billing.rs          (140 LOC)
        └── m20260627_000004_add_email_verification.rs (49 LOC)
        └── m20260628_000005_add_consumed_bypass_tokens.rs (56 LOC)
        └── m20260628_000006_add_payg_credits.rs     (87 LOC)
        └── m20260702_000007_add_has_payment_method.rs (48 LOC)
        └── m20260706_000008_add_waitlist_signups.rs (98 LOC)
        └── m20260707_000009_add_subscription_event_at.rs (33 LOC)
        └── m20260707_000010_create_snapshots.rs     (46 LOC)
```

Total: **5,531 LOC** including tests.

### Router topology (from `server/src/server.rs`)
```
Router::new()
    .route("/health", get(health))
    .nest("/auth", auth::routes::router())        # signup/login/logout/refresh/me/devices
    .nest("/sync", sync::routes::router())        # snapshot endpoints
    .nest("/billing", billing::router())          # LemonSqueezy webhook
    .nest("/waitlist", waitlist::router())        # POST /waitlist
    .route("/ws", get(ws_upgrade))                # WebSocket upgrade
```

### Not currently deployed anywhere
- User confirmed 2026-07-08: relay runs locally only, no production instance
- No data migration needed — start with empty D1 database
- No user allowlist coordination — no existing installs pointing at `sync.maho.app`

---

## Deployment Target Architecture

```
                        ┌────────────────────────────────┐
                        │  mahobrowser.com CF zone       │
                        │  (already provisioned Day 1)   │
                        └────────────────────────────────┘
                                       │
                                       │ Custom Domain
                                       ▼
        ┌────────────────────────────────────────────────────────┐
        │  maho-relay Worker (workers-rs)                        │
        │                                                        │
        │  Bindings:                                             │
        │  - D1: MAHO_DB (all tables)                            │
        │  - DO: ROOM (class RoomDO — one instance per room_id)  │
        │  - env: LS_WEBHOOK_SECRET, ARGON2_PEPPER, ...          │
        │  - secrets: RESEND_API_KEY (later, for Phase 2 outbound│
        │             email)                                     │
        │                                                        │
        │  Routes (worker::Router):                              │
        │    /health              → 200 "ok"                     │
        │    /auth/signup         → handler                      │
        │    /auth/login          → handler                      │
        │    /auth/logout         → handler                      │
        │    /auth/refresh        → handler                      │
        │    /auth/me             → handler (auth middleware)    │
        │    /auth/devices/:id    → handler (auth middleware)    │
        │    /sync/snapshot/*     → handler (auth middleware)    │
        │    /waitlist            → handler (no auth)            │
        │    /billing/webhook     → handler (LS signature verify)│
        │    /ws                  → WS upgrade → forward to      │
        │                          ROOM Durable Object namespace │
        │                                                        │
        │  Scheduled:                                            │
        │    "0 * * * *"          → hourly maintenance           │
        │                          (retention prune, session gc) │
        └────────────────────────────────────────────────────────┘

Custom Domain bindings (all → same Worker):
  relay.mahobrowser.com    (canonical marketing docs use this for /waitlist)
  sync.mahobrowser.com     (WebSocket sync, /ws)
  update.mahobrowser.com   (auto-update manifest for browser)
  updates.mahobrowser.com  (alias)
  proxy.mahobrowser.com    (managed AI proxy — later phase; initially same worker with 501)
```

**Note on `proxy.mahobrowser.com`**: The AI proxy to OpenRouter is described in blog docs but the current `maho/relay/` code doesn't implement it. Phase 2 stubs the endpoint with 501 Not Implemented; actual proxy logic ships in a later plan when Pro/Max managed AI backend goes live.

---

# Phase 2A — Bootstrap Workers Project

**Prerequisites**: none. Runs first.

**Deliverable**: A minimal deployable Worker skeleton at `maho/relay/` with:
- `Cargo.toml` targeting `wasm32-unknown-unknown` via `worker` crate
- `wrangler.toml` with production + staging environments
- `src/lib.rs` with `#[event(fetch)]` and `#[event(scheduled)]` entry points
- `/health` returns `"ok"`
- Empty D1 database created
- Custom Domain bound to a staging URL first
- Successfully `wrangler deploy` produces a live URL

### Task 2A.1 — Backup existing tree

```bash
cd /Users/indo/code/project/maho-workspace
# Tag current state as legacy reference
git tag pre-relay-workers-port maho/relay/

# Move old tree aside for reference (do NOT delete — kept for logic reference during port)
mv maho/relay maho/relay-legacy
mkdir maho/relay
```

Commit:
```bash
git add -A
git commit -m "chore(relay): stash legacy relay under maho/relay-legacy for port reference

Legacy axum/tokio/SeaORM implementation preserved read-only during the
CF Workers port (Plan B). Delete maho/relay-legacy at Phase 2K exit."
```

### Task 2A.2 — Initialize workers-rs scaffold

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay

# Initialize Rust crate targeting Workers
cat > Cargo.toml <<'EOF'
[package]
name = "maho-relay-worker"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib", "rlib"]

[dependencies]
worker = { version = "0.4", features = ["d1", "http"] }
worker-macros = "0.4"

serde = { version = "1", features = ["derive"] }
serde_json = "1"

# Preserved from legacy
argon2 = "0.5"
sha2 = "0.10"
hmac = "0.12"
subtle = "2.5"
uuid = { version = "1", features = ["serde", "v4", "js"] }
chrono = { version = "0.4", features = ["serde", "wasmbind"] }
base64 = "0.22"

# WASM support
wasm-bindgen = "0.2"
wasm-bindgen-futures = "0.4"
console_error_panic_hook = "0.1"
getrandom = { version = "0.2", features = ["js"] }

[dev-dependencies]
wasm-bindgen-test = "0.3"

[profile.release]
opt-level = "z"
lto = true
codegen-units = 1
EOF

# .gitignore for build artifacts
cat > .gitignore <<'EOF'
/target
/build
/dist
/.wrangler
node_modules
*.wasm
EOF

# Minimal lib.rs — /health endpoint only for smoke test
mkdir -p src
cat > src/lib.rs <<'EOF'
use worker::*;

#[event(fetch)]
pub async fn fetch(req: Request, env: Env, _ctx: Context) -> Result<Response> {
    console_error_panic_hook::set_once();

    let router = Router::new();

    router
        .get("/health", |_, _| Response::ok("ok"))
        .get_async("/", |_, _| async { Response::ok("maho-relay worker") })
        .run(req, env)
        .await
}
EOF
```

### Task 2A.3 — `wrangler.toml`

```bash
cat > wrangler.toml <<'EOF'
name = "maho-relay"
main = "build/worker/shim.mjs"
compatibility_date = "2026-07-08"
account_id = "b1c1bfc44b1313b6f51553095ccb2eb4"

[build]
command = "cargo install -q worker-build 2>/dev/null; worker-build --release"

# ---- Production ----
[env.production]
routes = [
  { pattern = "relay.mahobrowser.com/*", zone_name = "mahobrowser.com" },
  { pattern = "sync.mahobrowser.com/*", zone_name = "mahobrowser.com" },
  { pattern = "update.mahobrowser.com/*", zone_name = "mahobrowser.com" },
  { pattern = "updates.mahobrowser.com/*", zone_name = "mahobrowser.com" },
  { pattern = "proxy.mahobrowser.com/*", zone_name = "mahobrowser.com" },
]

[[env.production.d1_databases]]
binding = "MAHO_DB"
database_name = "maho-relay-db"
database_id = "TO_BE_FILLED_AFTER_D1_CREATE"
migrations_dir = "migrations"

[[env.production.durable_objects.bindings]]
name = "ROOM"
class_name = "RoomDO"

[env.production.vars]
REQUIRE_AUTH = "true"
OPEN_REGISTRATION = "false"
SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE = "3"
LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE = "10"
MIN_PASSWORD_LENGTH = "12"
MAX_ROOM_SIZE_MB = "50"
MAX_MESSAGE_SIZE_BYTES = "1048576"
MAX_CONNECTIONS_PER_ROOM = "5"
MAX_CONNECTIONS_PER_IP = "20"
MAX_ROOMS_PER_IP = "10"
RETENTION_DAYS = "30"

# Secrets set via `wrangler secret put`:
#   LS_WEBHOOK_SECRET
#   ARGON2_PEPPER
#   RESEND_API_KEY (later)

[[env.production.migrations]]
tag = "v1"
new_sqlite_classes = ["RoomDO"]

# ---- Staging ----
[env.staging]
routes = [
  { pattern = "relay-staging.mahobrowser.com/*", zone_name = "mahobrowser.com" },
]

[[env.staging.d1_databases]]
binding = "MAHO_DB"
database_name = "maho-relay-db-staging"
database_id = "TO_BE_FILLED_AFTER_STAGING_D1_CREATE"
migrations_dir = "migrations"

[[env.staging.durable_objects.bindings]]
name = "ROOM"
class_name = "RoomDO"

[env.staging.vars]
REQUIRE_AUTH = "true"
OPEN_REGISTRATION = "true"          # staging allows registration for testing
SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE = "60"
LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE = "60"
MIN_PASSWORD_LENGTH = "8"
MAX_ROOM_SIZE_MB = "50"
MAX_MESSAGE_SIZE_BYTES = "1048576"
MAX_CONNECTIONS_PER_ROOM = "5"
MAX_CONNECTIONS_PER_IP = "20"
MAX_ROOMS_PER_IP = "10"
RETENTION_DAYS = "7"

[[env.staging.migrations]]
tag = "v1"
new_sqlite_classes = ["RoomDO"]

# ---- Scheduled (both environments) ----
[triggers]
crons = ["0 * * * *"]   # hourly maintenance
EOF
```

### Task 2A.4 — Create D1 databases

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay

# Production database
bunx wrangler d1 create maho-relay-db
# Copy the "database_id" from output into wrangler.toml [env.production.d1_databases].database_id

# Staging database
bunx wrangler d1 create maho-relay-db-staging
# Copy database_id into [env.staging.d1_databases].database_id
```

Manually update `wrangler.toml` with the two `database_id` values. Delegated session should be prepared to prompt operator for these outputs (since D1 create requires interaction/CF API confirmation).

### Task 2A.5 — Add RoomDO stub

Add to `src/lib.rs`:
```rust
use worker::*;

#[durable_object]
pub struct RoomDO {
    state: State,
    env: Env,
}

#[durable_object]
impl DurableObject for RoomDO {
    fn new(state: State, env: Env) -> Self {
        Self { state, env }
    }

    async fn fetch(&mut self, _req: Request) -> Result<Response> {
        Response::ok("RoomDO stub — not implemented yet")
    }
}
```

### Task 2A.6 — Set secrets (staging first)

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay

# Generate a random pepper (32 bytes hex)
PEPPER=$(openssl rand -hex 32)
echo "$PEPPER" | bunx wrangler secret put ARGON2_PEPPER --env staging
# For production, use a different pepper:
openssl rand -hex 32 | bunx wrangler secret put ARGON2_PEPPER --env production

# LemonSqueezy webhook secret: operator provides
# echo "<LS_signing_secret>" | bunx wrangler secret put LS_WEBHOOK_SECRET --env staging
# echo "<LS_signing_secret>" | bunx wrangler secret put LS_WEBHOOK_SECRET --env production
```

**Operator input required**: LemonSqueezy webhook signing secret from LS Dashboard → Settings → Webhooks. Delegated session pauses here and asks.

### Task 2A.7 — Deploy staging + smoke test

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay

# Deploy to staging
bunx wrangler deploy --env staging

# Verify
curl -sI https://relay-staging.mahobrowser.com/health
# Expect: HTTP/2 200

curl -s https://relay-staging.mahobrowser.com/health
# Expect: ok
```

Custom Domain binding for `relay-staging.mahobrowser.com` should auto-provision (CF handles DNS + TLS since the zone is on the same account).

If `relay-staging.mahobrowser.com` DNS doesn't resolve immediately, wait up to 3 minutes for CF to provision, then re-test.

### Task 2A.8 — Commit

```bash
cd /Users/indo/code/project/maho-workspace
git add -A maho/relay/
git commit -m "feat(relay): bootstrap workers-rs skeleton with D1 + Durable Objects

Phase 2A of relay port. Minimal Worker deployed to
relay-staging.mahobrowser.com serving /health. D1 databases created
(production + staging). RoomDO class registered but not yet
implemented. Cron trigger scaffolded for hourly maintenance.

Legacy relay code preserved read-only under maho/relay-legacy/."
```

### Phase 2A exit criteria
- [ ] `maho/relay/Cargo.toml`, `wrangler.toml`, `src/lib.rs` created
- [ ] D1 databases created (production + staging), IDs filled in wrangler.toml
- [ ] `wrangler deploy --env staging` succeeds
- [ ] `curl https://relay-staging.mahobrowser.com/health` → 200 "ok"
- [ ] Secrets (ARGON2_PEPPER, LS_WEBHOOK_SECRET) set for both environments
- [ ] Committed to main

---

# Phase 2B — D1 Schema Migrations

**Prerequisites**: Phase 2A complete.

**Deliverable**: All 10 sea-orm migration files converted to raw SQL under `maho/relay/migrations/`, applied to both staging and production D1 databases.

### Reference

Legacy migrations (read-only, do not modify):
- `maho/relay-legacy/migration/src/m20260424_000001_create_tables.rs` — messages, room_meta
- `maho/relay-legacy/migration/src/m20260502_000002_create_auth_tables.rs` — users, devices, sessions
- `maho/relay-legacy/migration/src/m20260627_000003_add_billing.rs` — subscription fields, credits table
- `maho/relay-legacy/migration/src/m20260627_000004_add_email_verification.rs`
- `maho/relay-legacy/migration/src/m20260628_000005_add_consumed_bypass_tokens.rs`
- `maho/relay-legacy/migration/src/m20260628_000006_add_payg_credits.rs`
- `maho/relay-legacy/migration/src/m20260702_000007_add_has_payment_method.rs`
- `maho/relay-legacy/migration/src/m20260706_000008_add_waitlist_signups.rs`
- `maho/relay-legacy/migration/src/m20260707_000009_add_subscription_event_at.rs`
- `maho/relay-legacy/migration/src/m20260707_000010_create_snapshots.rs`

### Task 2B.1 — Convert each migration to raw SQL

For each of the 10 files, produce a corresponding `maho/relay/migrations/00XX_<name>.sql` with equivalent DDL.

Naming convention: **sequential 4-digit prefix** + descriptive name:
- `0001_create_tables.sql`
- `0002_create_auth_tables.sql`
- `0003_add_billing.sql`
- `0004_add_email_verification.sql`
- `0005_add_consumed_bypass_tokens.sql`
- `0006_add_payg_credits.sql`
- `0007_add_has_payment_method.sql`
- `0008_add_waitlist_signups.sql`
- `0009_add_subscription_event_at.sql`
- `0010_create_snapshots.sql`

**Delegation approach**: Each migration file is a candidate for a separate agy task. Prompt template:

─── begin sample agy prompt for 0001 ───
```
CONTEXT: Convert legacy sea-orm-migration Rust file to raw SQL for D1. Legacy file location: /Users/indo/code/project/maho-workspace/maho/relay-legacy/migration/src/m20260424_000001_create_tables.rs

TASK: Read the legacy file. Extract every CREATE TABLE / CREATE INDEX / etc. statement encoded via sea_query DSL. Emit equivalent raw SQL (SQLite dialect, compatible with Cloudflare D1) into /Users/indo/code/project/maho-workspace/maho/relay/migrations/0001_create_tables.sql.

CRITICAL:
- Preserve exact table and column names.
- Preserve column types (map sea_query types to SQLite: Integer, Text, Blob, Real, ... ). Use `INTEGER PRIMARY KEY` for auto-increment ids.
- Preserve constraints (NOT NULL, UNIQUE, DEFAULT).
- Preserve indexes (both PRIMARY KEY and secondary UNIQUE/INDEX).
- Emit each statement terminated by `;` on its own line.
- Add `-- Migration: <filename>` header comment.
- No IF NOT EXISTS clauses (D1 migrations track state internally).

DO NOT TOUCH:
- Any other file
- Legacy files under maho/relay-legacy/

VERIFY:
- File created at /Users/indo/code/project/maho-workspace/maho/relay/migrations/0001_create_tables.sql
- Valid SQLite SQL (parseable by `sqlite3 :memory: < 0001_create_tables.sql`)
- Every CREATE TABLE in the legacy file appears as CREATE TABLE in the SQL
```
─── end ───

Repeat for each of the 10 files with adjusted target filename and source path.

### Task 2B.2 — Apply migrations to staging

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay

# Verify migrations directory has 10 files
ls migrations/*.sql | wc -l   # expect 10

# Apply to staging D1
bunx wrangler d1 migrations apply maho-relay-db-staging --env staging

# List tables
bunx wrangler d1 execute maho-relay-db-staging --env staging --command "SELECT name FROM sqlite_master WHERE type='table';"
# Expect: messages, room_meta, users, devices, sessions, waitlist_signups, credits, consumed_bypass_tokens, processed_events, snapshots (may also include _cf_KV internal tables)
```

### Task 2B.3 — Verify schema matches legacy

For each table listed above, compare column set with legacy sea-orm entity file:

```bash
for tbl in messages room_meta users devices sessions waitlist_signups credits consumed_bypass_tokens processed_events snapshots; do
  echo "=== $tbl ==="
  bunx wrangler d1 execute maho-relay-db-staging --env staging \
    --command "SELECT sql FROM sqlite_master WHERE type='table' AND name='$tbl';"
done
```

Compare column names, types, constraints against `maho/relay-legacy/entity/src/<tbl>.rs`.

### Task 2B.4 — Apply to production

Only after staging validated:
```bash
bunx wrangler d1 migrations apply maho-relay-db --env production
```

### Task 2B.5 — Commit

```bash
git add maho/relay/migrations/
git commit -m "feat(relay): D1 schema migrations for Workers port

10 SQL migration files converted from legacy sea-orm-migration Rust
files. Applied to staging and production D1 databases via wrangler
d1 migrations apply. Table schemas match legacy entity definitions
verbatim (column names, types, constraints, indexes preserved).

Ref: Phase 2B of relay port plan."
```

### Phase 2B exit criteria
- [ ] 10 SQL migration files under `maho/relay/migrations/`
- [ ] Applied to staging D1 successfully
- [ ] Applied to production D1 successfully
- [ ] `wrangler d1 execute --command "SELECT name FROM sqlite_master WHERE type='table';"` shows all 10 expected tables
- [ ] Schema DDL matches legacy entity definitions
- [ ] Committed

---

# Phase 2C — Auth Module Port

**Prerequisites**: Phase 2A + 2B complete.

**Deliverable**: All 6 auth HTTP endpoints (`signup`, `login`, `logout`, `refresh`, `me`, `devices/:id`) working end-to-end on staging.

### Files to create

```
maho/relay/src/
├── lib.rs                    # entry + router (extended)
├── auth/
│   ├── mod.rs                # module exports + Router mount
│   ├── routes.rs             # 6 endpoint handlers
│   ├── middleware.rs         # Authorization header extract + verify
│   ├── models.rs             # request/response types (from legacy)
│   ├── passwords.rs          # argon2 hash + verify (from legacy, adapted)
│   ├── tokens.rs             # access/refresh token generation
│   └── errors.rs             # AuthError → HTTP response mapping
├── db/
│   ├── mod.rs
│   ├── users.rs              # D1 queries: insert_user, find_user_by_email, ...
│   ├── devices.rs
│   └── sessions.rs
├── config.rs                 # env var extraction into RelayConfig struct
└── errors.rs                 # top-level error type
```

### Delegation approach

Each file is a separate agy task. Legacy Rust code in `maho/relay-legacy/server/src/auth/*.rs` provides the reference logic. Delegated session must:

1. Read legacy file
2. Adapt to workers-rs idioms:
   - `axum::extract::State` → `env.d1("MAHO_DB")`
   - `sea_orm::ActiveModel::save` → `worker::D1PreparedStatement::bind(...).run().await`
   - `tokio::spawn` → don't; Workers is single-threaded per request
   - `argon2::Argon2::hash_password` → same crate, tuned params
3. Preserve endpoint contract (request body shape, response shape, HTTP status codes)
4. Preserve error semantics (rate limit → 429, invalid password → 401, etc.)

Sample agy brief for `passwords.rs`:

─── begin ───
```
CONTEXT: Port password hashing from legacy Rust axum server to workers-rs. Legacy: /Users/indo/code/project/maho-workspace/maho/relay-legacy/server/src/auth/passwords.rs (19 lines, argon2 wrapper).

TASK: Create /Users/indo/code/project/maho-workspace/maho/relay/src/auth/passwords.rs with equivalent function signatures:

    pub fn hash_password(password: &str, pepper: &[u8]) -> Result<String, PasswordError>
    pub fn verify_password(password: &str, hash: &str, pepper: &[u8]) -> Result<bool, PasswordError>

Use `argon2::Argon2` with params tuned for Workers CPU budget:
    let params = argon2::Params::new(
        19456,  // m_cost (memory KiB)
        2,      // t_cost (iterations)
        1,      // p_cost (parallelism, 1 for wasm)
        None,   // output_len
    ).map_err(PasswordError::from)?;
    let argon2 = argon2::Argon2::new(argon2::Algorithm::Argon2id, argon2::Version::V0x13, params);

Password combines with pepper via HMAC-SHA256 before hashing:
    let peppered = hmac::Hmac::<sha2::Sha256>::new_from_slice(pepper)?.chain_update(password.as_bytes()).finalize().into_bytes();
    let salt = argon2::password_hash::SaltString::generate(...);
    argon2.hash_password(&peppered, &salt)...

Define PasswordError enum with variants: InvalidHash, HashFailed, ConfigError.

DO NOT TOUCH:
- Any other file
- Legacy files

VERIFY:
- File compiles as part of `cargo build --target wasm32-unknown-unknown --release`
- Roundtrip test: hash a password, verify with same pepper → true; verify with different pepper → false
```
─── end ───

Similar per-file briefs for `tokens.rs`, `middleware.rs`, `models.rs`, `routes.rs`, `db/users.rs`, `db/devices.rs`, `db/sessions.rs`.

### Task 2C.1 – 2C.8 — Port each auth file

Fire in dependency order:
1. **2C.1** `errors.rs` (top-level error type)
2. **2C.2** `config.rs` (env var extraction)
3. **2C.3** `auth/passwords.rs`
4. **2C.4** `auth/tokens.rs`
5. **2C.5** `auth/models.rs`
6. **2C.6** `db/users.rs`, `db/devices.rs`, `db/sessions.rs` (parallel)
7. **2C.7** `auth/middleware.rs`
8. **2C.8** `auth/routes.rs` (the big one, ~800 legacy lines → likely ~600 workers-rs lines)
9. **2C.9** wire into `lib.rs` router

### Task 2C.10 — Integration smoke test

After each subtask, compile:
```bash
cd /Users/indo/code/project/maho-workspace/maho/relay
cargo check --target wasm32-unknown-unknown
```

After all subtasks done, deploy staging and hit endpoints:
```bash
bunx wrangler deploy --env staging

BASE=https://relay-staging.mahobrowser.com

# Signup
curl -sX POST $BASE/auth/signup -H 'Content-Type: application/json' -d '{
  "email":"test1@example.com",
  "password":"correct horse battery staple",
  "display_name":"Test",
  "device_name":"CI Laptop",
  "device_type":"desktop"
}'
# Expect: 200 with {access_token, refresh_token, user, device}

# Login
curl -sX POST $BASE/auth/login -H 'Content-Type: application/json' -d '{
  "email":"test1@example.com",
  "password":"correct horse battery staple",
  "device_name":"CI Laptop",
  "device_type":"desktop"
}'
# Expect: 200 with {access_token, refresh_token, user, device}

# /me with token
TOKEN="<paste access_token>"
curl -sH "Authorization: Bearer $TOKEN" $BASE/auth/me
# Expect: 200 with {user, device}
```

### Task 2C.11 — Commit

```bash
git add maho/relay/src/
git commit -m "feat(relay): port auth module to workers-rs + D1

Phase 2C: signup/login/logout/refresh/me/device endpoints migrated
from axum + SeaORM to worker::Router + D1PreparedStatement. Argon2
password hashing preserved with WASM-tuned params (m_cost=19456).
Access/refresh token model preserved. Rate limits per README.

Contract-tested against relay-staging.mahobrowser.com."
```

### Phase 2C exit criteria
- [ ] All 6 auth endpoints respond 200 on staging with valid inputs
- [ ] Invalid password → 401
- [ ] Duplicate email signup → 409
- [ ] Rate limit exceeded → 429
- [ ] `/auth/me` with expired token → 401
- [ ] `cargo check --target wasm32-unknown-unknown` clean
- [ ] Committed

---

# Phase 2D — Waitlist Endpoint

**Prerequisites**: Phase 2A + 2B + 2C partial (config module needs to exist).

**Deliverable**: `POST /waitlist` working on staging with rate limit + idempotent dedupe.

### Task 2D.1 — Port waitlist.rs

Reference: `maho/relay-legacy/server/src/waitlist.rs` (177 LOC).

Create `maho/relay/src/waitlist.rs`:
- Extract IP from `req.headers().get("cf-connecting-ip")` (CF-specific header, always present in Workers)
- Rate limit: 5/min per SHA-256(IP + salt). Store window state in D1 or Durable Object (use a `RateLimitDO` if wanting DO; simpler: D1 table `rate_limit_windows` with (key, minute, count))
- Idempotent: if email already in `waitlist_signups`, return `{ok: true, already_signed_up: true}` without insert
- Validate email format (basic regex or `email_address` crate)
- Body validation: `metadata` size ≤ 10 KB

### Task 2D.2 — Wire into router in `lib.rs`

```rust
router
    // ... existing routes
    .post_async("/waitlist", crate::waitlist::handler)
```

### Task 2D.3 — Smoke test

```bash
curl -sX POST https://relay-staging.mahobrowser.com/waitlist \
  -H 'Content-Type: application/json' \
  -d '{"email":"waitlist-test@example.com","source":"phase2d","metadata":{"platform":"macos"}}'
# Expect: {"ok":true}

# Repeat same email
curl -sX POST https://relay-staging.mahobrowser.com/waitlist \
  -H 'Content-Type: application/json' \
  -d '{"email":"waitlist-test@example.com","source":"phase2d"}'
# Expect: {"ok":true,"already_signed_up":true}

# Verify in D1
bunx wrangler d1 execute maho-relay-db-staging --env staging \
  --command "SELECT email, source FROM waitlist_signups;"
```

### Task 2D.4 — Commit

```bash
git commit -m "feat(relay): port POST /waitlist to workers-rs

Phase 2D. IP-hashed rate limit (5/min), idempotent email dedupe,
metadata size validation. Contract-tested against staging."
```

### Phase 2D exit criteria
- [ ] `POST /waitlist` with new email → 200 `{ok:true}`
- [ ] `POST /waitlist` with duplicate email → 200 `{ok:true, already_signed_up:true}`
- [ ] `POST /waitlist` with invalid email → 400 `{error:"invalid_email"}`
- [ ] Rate limit: 6th request within 1 min from same IP → 429
- [ ] D1 `waitlist_signups` table has expected rows
- [ ] Committed

---

# Phase 2E — Billing Webhook Endpoint

**Prerequisites**: Phase 2A + 2B + 2C partial.

**Deliverable**: `POST /billing/webhook` verifying LemonSqueezy HMAC signature and processing subscription/credit events into D1.

### Task 2E.1 — Port billing.rs

Reference: `maho/relay-legacy/server/src/billing.rs` (669 LOC — the largest legacy module).

Key operations:
- Verify `X-Signature` header via HMAC-SHA256 with `env.secret("LS_WEBHOOK_SECRET").await?`
- Parse LS event JSON: `event_name` (order_created, subscription_created, subscription_updated, subscription_cancelled, subscription_payment_success, etc.)
- Idempotent processing: check `processed_events` table before applying (use `event.id` as unique key)
- Update `users.subscription_tier`, `users.has_payment_method`, `users.subscription_event_at`
- For PAYG order events: increment `credits.balance`, insert `credits.transactions` row

Port strategy:
- Preserve enum for event types
- Replace `reqwest` (rarely used in billing.rs) with `worker::Fetch` if any outbound HTTP is needed
- All DB access via `env.d1("MAHO_DB")?.prepare(...).bind(...).run().await`

### Task 2E.2 — Wire into router

```rust
router.post_async("/billing/webhook", crate::billing::webhook_handler)
```

### Task 2E.3 — Smoke test

LemonSqueezy has a "Send test event" button in their Dashboard. Point staging webhook URL to `https://relay-staging.mahobrowser.com/billing/webhook`, send a test event, verify:

```bash
# Check D1 processed_events table
bunx wrangler d1 execute maho-relay-db-staging --env staging \
  --command "SELECT event_id, event_name, processed_at FROM processed_events ORDER BY processed_at DESC LIMIT 5;"
```

Also test signature verification: send a curl with modified body but same X-Signature → expect 401 with error `invalid_signature`.

### Task 2E.4 — Commit

```bash
git commit -m "feat(relay): port billing webhook (LemonSqueezy) to workers-rs

Phase 2E. HMAC-SHA256 signature verification via worker::Env secret.
Idempotent event processing (processed_events dedupe key). Updates
user subscription tier + credit balance in D1. Ports all 669 LOC
of legacy billing.rs to workers-rs."
```

### Phase 2E exit criteria
- [ ] Valid LS test event → 200, updates D1
- [ ] Modified body with old signature → 401
- [ ] Duplicate event_id → 200 (idempotent), no double-processing
- [ ] Committed

---

# Phase 2F — Sync HTTP Routes (Snapshot Upload/Download)

**Prerequisites**: Phase 2A + 2B + 2C.

**Deliverable**: `POST /sync/snapshot/upload` + `GET /sync/snapshot/latest` working with auth.

### Task 2F.1 — Port sync/routes.rs

Reference: `maho/relay-legacy/server/src/sync/routes.rs` (209 LOC).

Endpoints:
- `POST /sync/snapshot/upload` — auth required (Bearer). Body: opaque bytes (encrypted snapshot blob). Stores in `snapshots` (id, user_id, device_id, blob, created_at). Size limit: 10 MB.
- `GET /sync/snapshot/latest?device_id=<id>` — auth required. Returns latest snapshot for that device_id belonging to the authenticated user.

Auth middleware from Phase 2C reused.

### Task 2F.2 — Smoke test

```bash
# Log in first to get token
TOKEN=$(curl -sX POST https://relay-staging.mahobrowser.com/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"test1@example.com","password":"correct horse battery staple"}' \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])")

DEVICE_ID=$(curl -sH "Authorization: Bearer $TOKEN" https://relay-staging.mahobrowser.com/auth/me \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['device']['id'])")

# Upload
echo -n "test snapshot data" > /tmp/snap.bin
curl -sX POST "https://relay-staging.mahobrowser.com/sync/snapshot/upload?device_id=$DEVICE_ID" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @/tmp/snap.bin
# Expect: 200 with {ok:true, snapshot_id:"..."}

# Download
curl -sH "Authorization: Bearer $TOKEN" \
  "https://relay-staging.mahobrowser.com/sync/snapshot/latest?device_id=$DEVICE_ID" \
  -o /tmp/downloaded.bin
diff /tmp/snap.bin /tmp/downloaded.bin
# Expect: no output (identical)
```

### Task 2F.3 — Commit

```bash
git commit -m "feat(relay): port sync HTTP snapshot endpoints to workers-rs

Phase 2F. Snapshot upload/latest with auth. Encrypted blobs treated
as opaque bytes. 10MB per-upload limit. D1 snapshots table."
```

### Phase 2F exit criteria
- [ ] Snapshot upload+download roundtrip byte-for-byte identical
- [ ] Unauthenticated request → 401
- [ ] Cross-user device_id request → 403
- [ ] Committed

---

# Phase 2G — Room Durable Object + WebSocket

**Prerequisites**: Phase 2A + 2B + 2C complete. Most complex phase.

**Deliverable**: `GET /ws` upgrade → Durable Object per room handling WebSocket protocol per README.md WebSocket Protocol section.

### Architecture

```
Client ──ws──► maho-relay Worker fetch handler ──► namespace.get(room_id).fetch(ws-upgrade req)
                                                          │
                                                          ▼
                                              RoomDO instance for room_id
                                                          │
                                                          ├── Hibernation API: accepts WS connection
                                                          ├── State: peer sockets, seq counter, message log
                                                          ├── D1: persist messages to `messages` table
                                                          ├── Broadcast to other peers via ws.send()
```

### Task 2G.1 — RoomDO class scaffold

Extend `src/lib.rs` RoomDO stub with real implementation:

```rust
use worker::*;

#[durable_object]
pub struct RoomDO {
    state: State,
    env: Env,
    // In-memory: room_id, peer sockets (indexed by device_id or connection id), max_size
    peers: std::collections::HashMap<String, WebSocket>,
    next_seq: u64,
    room_size_bytes: u64,
}

#[durable_object]
impl DurableObject for RoomDO {
    fn new(state: State, env: Env) -> Self {
        Self { state, env, peers: Default::default(), next_seq: 0, room_size_bytes: 0 }
    }

    async fn fetch(&mut self, req: Request) -> Result<Response> {
        // Load persisted next_seq + room_size from storage if first call
        // Accept WebSocket upgrade
        // Enter auth flow (read auth message within 5s)
        // On auth_ok: register peer, send catchup if since_seq present
        // Main loop: receive messages, persist to D1, broadcast, ack
        // On disconnect: unregister peer, persist state
    }
}
```

Use `state.accept_web_socket_with_tags(...)` from workers-rs Hibernation API to avoid keeping the DO alive while WS is idle.

### Task 2G.2 — Auth flow in RoomDO

Port from `maho/relay-legacy/server/src/handler.rs` (496 LOC).

Key steps:
1. On new WS connection, set `state.set_websocket_auto_response(...)` timeout to 5s
2. Wait for first message; parse `{"type":"auth","key":"<room_id>","token":"<access_token>","since_seq":N}`
3. Validate `key` matches the DO's room_id
4. If `token` provided (require_auth=true mode):
   - Verify token via `sessions` D1 table
   - Verify authenticated device owns this room_id (`devices.room_id = key`)
5. Send `{"type":"auth_ok"}` or `{"type":"error","msg":"..."}` (close on error)

### Task 2G.3 — Catchup logic

If `since_seq` present in auth message:
- Query D1: `SELECT seq, blob FROM messages WHERE room_id = ? AND seq > ? ORDER BY seq ASC`
- Stream each as `{"type":"catchup","seq":N,"blob":"<b64>"}`
- Finish with `{"type":"catchup_done"}`

### Task 2G.4 — Message loop

For each incoming WS frame from an authenticated peer:
1. Enforce message size ≤ 1MB
2. Enforce room storage ≤ 50 MB (`room_size_bytes + msg.len() ≤ 50MB * 1024 * 1024`); if exceeded, evict oldest messages until fits
3. Assign next seq (`self.next_seq += 1`)
4. Persist to D1 `messages` (room_id, seq, blob, created_at)
5. Update D1 `room_meta.next_seq`, `room_meta.last_activity`
6. Broadcast to all other peers in `self.peers`
7. Send `{"type":"ack","seq":N}` to sender

### Task 2G.5 — Cleanup on disconnect

On WS close:
- Remove peer from `self.peers`
- Persist final state to DO storage
- If `self.peers.is_empty()` → DO can hibernate (state remains in storage)

### Task 2G.6 — WebSocket route wiring in fetch handler

In `src/lib.rs`:

```rust
router.get_async("/ws", |req, ctx| async move {
    let key = req.headers().get("X-Room-Key")?.unwrap_or_default();
    // Alternative: extract from first message inside DO; for now, use header or path

    let namespace = ctx.env.durable_object("ROOM")?;
    let stub = namespace.id_from_name(&key)?.get_stub()?;
    stub.fetch_with_request(req).await
});
```

### Task 2G.7 — Smoke test with `wscat`

```bash
# Ensure a user + device exists (from Phase 2C signup)
TOKEN="<access_token>"
DEVICE_ROOM_ID="<devices.room_id from D1>"

# WS connect
bunx wscat -c "wss://sync-staging.mahobrowser.com/ws"

# Once connected, send auth message
> {"type":"auth","key":"<DEVICE_ROOM_ID>","token":"<TOKEN>","since_seq":0}
< {"type":"auth_ok"}
< {"type":"catchup_done"}   # if no prior messages

# Send a message
> hello world (text frame)
< {"type":"ack","seq":1}

# Open a second wscat with same key, verify broadcast
```

### Task 2G.8 — Commit

```bash
git commit -m "feat(relay): port WebSocket sync relay to Durable Objects

Phase 2G — most complex. Room state + peer tracking + broadcast via
Durable Objects. Hibernation API for idle rooms. Auth flow, catchup,
per-message rate limit, storage cap all preserved from legacy
handler.rs. Contract-tested via wscat against staging."
```

### Phase 2G exit criteria
- [ ] Two wscat clients connect to same room, one sends → other receives
- [ ] Client reconnect with `since_seq=N` receives catchup for messages after N
- [ ] Wrong token → connection closes with `error`
- [ ] Wrong room_id (device doesn't own) → connection closes
- [ ] Message size > 1MB → connection closes or error
- [ ] Committed

---

# Phase 2H — Testing

**Prerequisites**: Phase 2A – 2G complete.

**Deliverable**: A test harness (Miniflare local + integration curl+wscat) that exercises all endpoints. Not necessarily 100% coverage; the goal is regression protection for future refactors.

### Task 2H.1 — Miniflare local dev config

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay

# Miniflare comes bundled with wrangler dev
bunx wrangler dev --env staging --local
# Serves on http://localhost:8787
```

### Task 2H.2 — Integration test script

Create `maho/relay/tests/integration.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

BASE="${BASE:-https://relay-staging.mahobrowser.com}"

pass=0; fail=0
check() { if [ "$1" = "$2" ]; then echo "PASS $3"; pass=$((pass+1)); else echo "FAIL $3 (got $1 expected $2)"; fail=$((fail+1)); fi; }

# /health
RES=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/health")
check "$RES" "200" "GET /health"

# /auth/signup
BODY='{"email":"itest+'$(date +%s)'@example.com","password":"correct horse battery staple","display_name":"itest","device_name":"ci","device_type":"desktop"}'
RES=$(curl -sX POST "$BASE/auth/signup" -H 'Content-Type: application/json' -d "$BODY")
TOKEN=$(echo "$RES" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('access_token',''))")
[ -n "$TOKEN" ] && { echo "PASS POST /auth/signup returned token"; pass=$((pass+1)); } || { echo "FAIL POST /auth/signup"; fail=$((fail+1)); }

# /auth/me
RES=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: Bearer $TOKEN" "$BASE/auth/me")
check "$RES" "200" "GET /auth/me"

# /waitlist
BODY='{"email":"waitlist+'$(date +%s)'@example.com","source":"itest"}'
RES=$(curl -sX POST "$BASE/waitlist" -H 'Content-Type: application/json' -d "$BODY")
OK=$(echo "$RES" | python3 -c "import sys,json; print(json.load(sys.stdin).get('ok'))")
check "$OK" "True" "POST /waitlist"

# ... more tests ...

echo "----"
echo "$pass passed, $fail failed"
[ "$fail" = "0" ] && exit 0 || exit 1
```

### Task 2H.3 — WebSocket integration test (Python websockets)

Create `maho/relay/tests/ws_test.py`:

```python
import asyncio, json, os, sys
import websockets

BASE_WS = os.environ.get("BASE_WS", "wss://sync-staging.mahobrowser.com/ws")
TOKEN = os.environ.get("TOKEN")
KEY = os.environ.get("ROOM_KEY")

async def main():
    async with websockets.connect(BASE_WS) as ws:
        await ws.send(json.dumps({"type":"auth","key":KEY,"token":TOKEN,"since_seq":0}))
        auth_ok = json.loads(await ws.recv())
        assert auth_ok.get("type") == "auth_ok", f"expected auth_ok, got {auth_ok}"
        # Consume any catchup messages
        while True:
            msg = json.loads(await ws.recv())
            if msg.get("type") == "catchup_done":
                break
        # Send a test message
        await ws.send("hello from ws_test.py")
        ack = json.loads(await ws.recv())
        assert ack.get("type") == "ack" and ack.get("seq") >= 1
        print("PASS WebSocket send+ack")

asyncio.run(main())
```

### Task 2H.4 — Commit

```bash
git add maho/relay/tests/
git commit -m "test(relay): integration test harness for workers deploy

Phase 2H. Shell script covering all HTTP endpoints + Python
websockets script covering WS protocol. Run against staging or
prod (via BASE env var)."
```

### Phase 2H exit criteria
- [ ] `bash tests/integration.sh` passes against staging
- [ ] `TOKEN=... ROOM_KEY=... python tests/ws_test.py` passes against staging
- [ ] `wrangler dev --local` starts without error
- [ ] Committed

---

# Phase 2I — Production Deploy

**Prerequisites**: Phase 2A – 2H complete. All staging tests passing.

**Deliverable**: Live production worker at `relay.mahobrowser.com` + related subdomains, serving all endpoints, passing full smoke suite.

### Task 2I.1 — Deploy

```bash
cd /Users/indo/code/project/maho-workspace/maho/relay
bunx wrangler deploy --env production
```

CF auto-provisions DNS records + TLS for each Custom Domain in wrangler.toml `routes` (relay.mahobrowser.com, sync., update., updates., proxy.).

### Task 2I.2 — Smoke test production

Same tests as staging but with `BASE=https://relay.mahobrowser.com`:
```bash
BASE=https://relay.mahobrowser.com bash tests/integration.sh

TOKEN=... ROOM_KEY=... python tests/ws_test.py   # BASE_WS=wss://sync.mahobrowser.com/ws
```

### Task 2I.3 — Configure LemonSqueezy production webhook URL

LS Dashboard → Settings → Webhooks → Update production webhook URL to `https://relay.mahobrowser.com/billing/webhook`. Send test event, verify 200.

### Task 2I.4 — Commit

```bash
git commit -m "feat(relay): deploy production worker to *.mahobrowser.com

Phase 2I. All subdomains (relay/sync/update/updates/proxy) bound to
maho-relay production worker. LS webhook re-pointed to production.
Full smoke suite passes."
```

### Phase 2I exit criteria
- [ ] `curl -sI https://relay.mahobrowser.com/health` → 200
- [ ] `curl -sI https://sync.mahobrowser.com/health` → 200 (or equivalent alive check)
- [ ] Integration test suite passes against production
- [ ] LS test event lands in production D1 `processed_events`
- [ ] Committed

---

# Phase 2J — Client-Side Constant Updates

**Prerequisites**: Phase 2I complete (production endpoints live).

**Deliverable**: Every hardcoded `sync.maho.app` / `update.maho.app` / `proxy.maho.app` / `updates.maho.app` in client-side code updated to the corresponding `*.mahobrowser.com` hostname.

### Files to update (12+ occurrences)

Per Day 1 review + client-side grep:

| File | Change |
|---|---|
| `maho/crates/maho-core/src/sync_models.rs:81` | `"wss://sync.maho.app"` → `"wss://sync.mahobrowser.com"` |
| `maho/crates/maho-core/src/maho_core.rs:6776` | Update sync reference |
| `maho/crates/maho-core/src/sharing_manager.rs:33` | Share-link base → `https://mahobrowser.com` |
| `maho/crates/maho-core/src/extension_bridge_sync.rs` | (grep + update) |
| `maho/ios-shell/Support/RelayAPIClient.swift:152` | Default base URL |
| `maho/macos-shell/Sources/Sync/SyncManager.swift:19` | Sync server URL |
| `maho/macos-shell/Sources/Settings/AccountSyncSettingsView.swift` | UI display |
| `maho/macos-shell/Tests/RelaySyncTests.swift` | Test fixtures |
| `maho/android-shell/app/src/main/…` | (grep for `maho\.app`) |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc:67` | `kSettingsSyncRelayUrl` |
| `.github/workflows/deploy-website.yml` | Deploy target |
| `mail/src/utils/analytics.ts:1` | Plausible domain |
| `mail/src-tauri/src/commands/ai.rs` | HTTP-Referer |
| `website/src/content/docs/blog/verify-maho-locally.mdx` | Hostname mentions (already partial in Plan A Task 1.6h; complete here) |
| `website/src/content/docs/blog/zero-telemetry-by-default.mdx` | Hostname mentions |

### Task 2J.1 – 2J.14 — Per-file agy tasks

Each file is a single-file mechanical replacement. Template agy prompt:

─── begin ───
```
CONTEXT: Client-side hardcoded relay backend hostname. Legacy relay used the fictional "sync.maho.app" etc.; production is now live at "sync.mahobrowser.com" (deployed Phase 2I). Update the constants.

TASK: In /Users/indo/code/project/maho-workspace/<FILE-PATH>, replace:
- Every "sync.maho.app" → "sync.mahobrowser.com"
- Every "wss://sync.maho.app" → "wss://sync.mahobrowser.com"
- Every "update.maho.app" → "update.mahobrowser.com"
- Every "updates.maho.app" → "updates.mahobrowser.com"
- Every "proxy.maho.app" → "proxy.mahobrowser.com"
- Every "https://maho.app" (if referring to relay endpoints — verify context; if it's a marketing URL, it should already be corrected in Plan A) → "https://mahobrowser.com"

DO NOT TOUCH:
- Any other file
- hello@maho.app or security@maho.app email addresses (those were handled in Plan A Task 1.6)
- Any relay-legacy content (that tree is being deleted in Phase 2K)

VERIFY:
- `grep -c 'maho\.app' <FILE-PATH>` → 0
- File compiles / lints in its respective toolchain
```
─── end ───

Repeat per file. Independent — all can run in parallel.

### Task 2J.15 — Verify each subtree builds

```bash
# Rust workspace
cd /Users/indo/code/project/maho-workspace
bun run typecheck

# macOS shell
cd macos-shell && make test

# iOS shell
cd ../ios-shell && make test

# Android shell
cd ../android-shell && ./gradlew app:testDebugUnitTest

# Mail app
cd ../mail && bun run test && cargo test

# Chromium fork
# Full chrome target rebuild is expensive; only rebuild if this affects a live component
# For now: cargo check the resources and unit_tests target
python3 /Users/indo/code/project/maho-workspace/maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests
```

### Task 2J.16 — Commit (per-subtree for cleaner history)

```bash
# Rust core
git add maho/crates/
git commit -m "feat(core): migrate sync/share URLs to mahobrowser.com

Phase 2J of relay port. Sync WebSocket, share-link base, and
extension bridge sync endpoints now point at the deployed CF Workers
relay at wss://sync.mahobrowser.com."

# Native shells
git add maho/ios-shell/ maho/macos-shell/ maho/android-shell/
git commit -m "feat(shells): migrate relay hostnames to mahobrowser.com"

# Chromium
git add maho-chromium/
git commit -m "feat(chromium): migrate settings sync relay URL constant"

# Mail
git add mail/
git commit -m "feat(mail): migrate Plausible + HTTP-Referer to mahobrowser.com"

# Website blog docs
git add website/src/content/docs/blog/
git commit -m "docs(blog): update backend endpoint hostnames to mahobrowser.com

Ref Plan B Phase 2J. Aligns verify-maho-locally.mdx and
zero-telemetry-by-default.mdx with the deployed CF Workers relay."

# Workflows
git add .github/workflows/
git commit -m "ci(workflows): update deploy target URLs to mahobrowser.com"
```

### Phase 2J exit criteria
- [ ] `grep -rn 'maho\.app' maho/ maho-chromium/ mail/ .github/workflows/ | grep -v relay-legacy` → 0
- [ ] All subtrees typecheck/build clean
- [ ] Committed (5–7 commits per subtree)

---

# Phase 2K — Cleanup + Final Verification

**Prerequisites**: Phase 2A – 2J complete.

**Deliverable**: Legacy tree deleted, docs updated, full end-to-end sync verified.

### Task 2K.1 — Delete legacy tree

```bash
cd /Users/indo/code/project/maho-workspace
git rm -r maho/relay-legacy
git commit -m "chore(relay): remove maho/relay-legacy after Workers port complete

Phase 2K. Legacy axum/tokio/SeaORM tree served as read-only reference
during Phase 2A-2J. All logic ported to workers-rs at maho/relay/.
Delete via git rm to preserve history."
```

### Task 2K.2 — Update README + AGENTS

`maho/relay/README.md` — rewrite for the workers-rs deploy:
- Deployment: `bunx wrangler deploy --env production`
- Local dev: `bunx wrangler dev --env staging --local`
- Test: `bash tests/integration.sh`
- No nginx section (Workers handles TLS)
- No `--port`, `--db-path` CLI options (env vars only via wrangler.toml)

`maho/relay/AGENTS.md` — update:
- Structure: single crate, workers-rs, D1, DO
- Anti-pattern: "No adding relay/ crates to root Cargo.toml workspace members" — REMOVE (may no longer apply since we're a WASM crate; verify)
- Commands: `wrangler` instead of `cargo build --release`

### Task 2K.3 — End-to-end sync test with real Maho browser build

1. Build Maho browser with updated `sync.mahobrowser.com` constant (Phase 2J)
2. Sign up a fresh user via Maho browser UI (or `/auth/signup` curl)
3. Toggle sync ON in Settings
4. Add a bookmark
5. Sign in on a second device (or fresh profile)
6. Verify bookmark appears

If bookmark syncs, Phase 2 = complete.

### Task 2K.4 — Final commit + tag

```bash
git tag post-relay-workers-port
git commit --allow-empty -m "chore(relay): Phase 2 complete — Workers port shipped

- workers-rs skeleton (2A)
- D1 schema migrations (2B)
- Auth module port (2C)
- Waitlist endpoint (2D)
- LemonSqueezy billing webhook (2E)
- Sync HTTP snapshot endpoints (2F)
- WebSocket relay via Durable Objects (2G)
- Test harness (2H)
- Production deploy to *.mahobrowser.com (2I)
- Client-side constant updates (2J)
- Legacy tree cleanup (2K)

End-to-end sync verified through real browser build."
```

### Phase 2K exit criteria
- [ ] `maho/relay-legacy/` deleted from tree
- [ ] `maho/relay/README.md` + `AGENTS.md` reflect workers-rs deploy
- [ ] E2E sync test between two Maho browser instances passes
- [ ] Tagged `post-relay-workers-port`

---

# Overall Exit Criteria (Plan B Complete)

- [ ] `curl https://relay.mahobrowser.com/health` → 200 "ok"
- [ ] All HTTP auth endpoints functional (signup/login/logout/refresh/me/devices)
- [ ] Waitlist endpoint accepting signups
- [ ] Billing webhook receiving + processing LS events
- [ ] Snapshot upload/download roundtrip works
- [ ] WebSocket sync via wss://sync.mahobrowser.com/ws works
- [ ] All 12+ client-side constants point at mahobrowser.com
- [ ] `grep -rn 'sync\.maho\.app\|update\.maho\.app\|proxy\.maho\.app\|updates\.maho\.app' maho/ maho-chromium/ mail/ .github/workflows/` → 0 (excluding blog historical references)
- [ ] `maho/relay-legacy/` gone
- [ ] End-to-end sync test between two devices passes
- [ ] Wall clock: 3–5 days spread across ~10 delegated sessions

---

# Risks + Rollback

| Risk | Impact | Mitigation |
|---|---|---|
| Argon2 params too heavy → 30s Workers CPU limit exceeded | Signup/login times out | Task 2A.6 pepper generated; Task 2C.3 params tuned to `m_cost=19456`. Benchmark first signup. If TLE, reduce to `m_cost=9216`. |
| D1 write throughput lower than SQLite native | Signup burst rate-limited by D1 | D1 free tier: 100k writes/day, then paid. Monitor via CF dashboard. Cap signup rate at 3/min per IP (already in config). |
| DO Hibernation API subtle bugs | Room state lost, WS drops silently | Extensive Phase 2G testing with 2+ wscat clients + reconnect after 10 min idle. |
| LemonSqueezy webhook signature mismatch (format change) | Billing events silently dropped | Task 2E signature verify test. Set up LS Dashboard alerts for failed webhook deliveries. |
| Workers-rs argon2 crate WASM compat issue | Won't compile | Fallback: use `password-hash` + `argon2-alt` or reimplement via `subtle` + Web Crypto. |
| Client-side clients (browser, mobile) cache old hostname | Sync fails silently after Phase 2I flip | Not a real risk — no existing production clients (user confirmed relay was local-only). Fresh builds only. |
| Phase 2G takes longer than estimated (DO learning curve) | Delegated session stalls | Split 2G into 2G.1 (skeleton), 2G.2 (auth flow), 2G.3 (catchup), 2G.4 (broadcast). Each ~1 sitting. |

### Rollback

Before Phase 2A: `git tag pre-relay-workers-port` (Task 2A.1 already does this).

Between phases: each phase commits are additive. Reverting Phase 2X = `git revert <phase-commits>` + `wrangler deploy` a prior version.

If production deploy at Phase 2I breaks: 
- `wrangler rollback` (rolls to previous deployment)
- Or `wrangler deploy --env production` a prior git-checkout

D1 data corruption: `wrangler d1 backup restore` (D1 has automatic 24h retention).

Full nuke: `git reset --hard pre-relay-workers-port` + `wrangler d1 delete maho-relay-db` + start over.

---

# Non-Code Followups (Operator)

1. **LemonSqueezy webhook secret**: provide via `wrangler secret put LS_WEBHOOK_SECRET --env production` when Phase 2E starts. Get value from LS Dashboard → Settings → Webhooks → Signing Secret.
2. **Resend API key** (later; Phase 3+ for outbound email): `wrangler secret put RESEND_API_KEY --env production`. Not needed for Phase 2 core.
3. **LS webhook URL update**: LS Dashboard → Settings → Webhooks → update URL to `https://relay.mahobrowser.com/billing/webhook`. Do at Phase 2I.3.
4. **Monitor CF dashboard** post-deploy: Workers Analytics, D1 usage, DO usage. First 24h are the risky window.
5. **DMARC upgrade** (per Plan A calendar): unrelated to Plan B but scheduled for 2026-07-22.

---

# Sign-off

**Author**: Sisyphus (2026-07-08)
**Status**: **READY for delegation**. All architectural decisions locked. Prerequisites (Workers Paid $5/mo) confirmed 2026-07-08.
**Effort**: 3–5 days fulltime, ~10 delegated sessions (one per phase 2A-2K).
**Scope**: `maho/relay/` complete rewrite + client-side constants across `maho/`, `maho-chromium/`, `mail/`, `.github/workflows/`, `website/src/content/docs/blog/`.

**Next action** (delegated session for Phase 2A):
```bash
cd /Users/indo/code/project/maho-workspace/maho/relay
# Read this plan file's Phase 2A section
# Follow tasks 2A.1 → 2A.8 sequentially
```

**Related plans**:
- `.hermes/plans/2026-07-08_maho-website-launch-prep-v2.md` (Plan A — website; run first or in parallel)
- `.omo/plans/maho-website-improvement-plan.md` (design-language, orthogonal)
