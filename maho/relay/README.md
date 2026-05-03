# Maho Relay Server

Store-and-forward WebSocket relay for cross-device sync. Persists encrypted blobs per room, never decrypts. Clients handle E2E encryption (AES-256-GCM + Argon2id).

## Architecture

```
Client A ──ws──┐                  ┌──ws── Client B
               ├── Relay Server ──┤
Client C ──ws──┘   (SQLite WAL)   └──ws── Client D
```

- **Room-based**: Clients join rooms by `room_id` (32-char hex, derived from seed via Argon2id)
- **Store-and-forward**: Messages persist to SQLite, catch up on reconnect via `since_seq`
- **Opaque blobs**: Server stores and forwards encrypted payloads without interpretation
- **RAII cleanup**: Connection and room state are cleaned up automatically on disconnect

## Quick Start

```bash
# Build
cd relay && cargo build --release

# Run with production-safe defaults (auth required, registration closed)
./target/release/maho-relay

# With options
./target/release/maho-relay --port 9090 --db-path /data/relay.db --open-registration
```

By default the relay now starts in a production-safe mode:
- WebSocket sync requires authenticated access tokens (`require_auth = true`)
- New account registration is closed (`open_registration = false`)

For first-run provisioning, explicitly enable registration during setup, create the initial account(s), then disable it again.

## Configuration

| Parameter | CLI Flag | Env Var | Default | Description |
|-----------|----------|---------|---------|-------------|
| Port | `--port` | `MAHO_RELAY_PORT` | 8080 | Server listen port |
| DB Path | `--db-path` | `MAHO_RELAY_DB_PATH` | `./relay.db` | SQLite database file |
| Max Room Size | — | `MAHO_RELAY_MAX_ROOM_SIZE_MB` | 50 | Per-room storage limit, MB |
| Max Message Size | — | — | 1 MB | Single message size limit |
| Rate Limit | — | — | 100/min | Messages per minute per connection |
| Connections/Room | — | — | 5 | Max concurrent peers per room |
| Connections/IP | — | — | 20 | Max concurrent connections per IP |
| Rooms/IP | — | — | 10 | Max rooms per IP |
| Retention | — | — | 30 days | Message retention period |
| Auth Timeout | — | — | 5s | Time to send auth after connect |
| Require Auth | `--require-auth` / `--no-require-auth` | `MAHO_RELAY_REQUIRE_AUTH` | `true` | Require authenticated WebSocket sync |
| Open Registration | `--open-registration` / `--closed-registration` | `MAHO_RELAY_OPEN_REGISTRATION` | `false` | Allow unauthenticated signup |
| Signup Rate Limit | `--signup-rate-limit-per-ip-per-minute` | `MAHO_RELAY_SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE` | `3` | Max signup attempts per IP per minute |
| Login Rate Limit | `--login-rate-limit-per-ip-per-minute` | `MAHO_RELAY_LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE` | `10` | Max login attempts per IP per minute |
| Min Password Length | `--min-password-length` | `MAHO_RELAY_MIN_PASSWORD_LENGTH` | `12` | Minimum password length for signup/login |

Env vars override CLI flags.

## WebSocket Protocol

### Endpoint
`GET /ws` , WebSocket upgrade

### Health Check
`GET /health` , returns `ok`

### HTTP Auth Endpoints
- `POST /auth/signup`
- `POST /auth/login`
- `POST /auth/logout`
- `POST /auth/refresh`
- `GET /auth/me`
- `DELETE /auth/devices/:id`

### Auth Flow
1. Client connects to `/ws`
2. Client sends auth within 5s.
   - Production/default mode (`require_auth = true`):
     ```json
     {"type":"auth","key":"<32-char-hex-room-id>","token":"<access_token>","since_seq":0}
     ```
   - Migration/legacy mode (`require_auth = false`):
     ```json
     {"type":"auth","key":"<32-char-hex-room-id>","since_seq":0}
     ```
3. Server validates the room key and, when auth is required, validates the access token and checks that the authenticated device owns that `room_id`
4. Server responds with `{"type":"auth_ok"}`
5. If `since_seq` is present, server sends catchup messages, then `{"type":"catchup_done"}`

### Auth Policy
- Access tokens expire after 1 hour
- Refresh tokens expire after 30 days
- Failed login lockout: 10 attempts → 15 minute lock
- Signup and login are rate limited per IP using fixed 1-minute windows
- Passwords must satisfy the configured minimum length (default 12)

### Signup Example
```json
{
  "email": "alice@example.com",
  "password": "correct horse battery staple",
  "display_name": "Alice",
  "device_name": "Alice Laptop",
  "device_type": "desktop"
}
```

### Refresh Example
```json
{
  "refresh_token": "<refresh_token>"
}
```

### Signup/Login Error Format
```json
{"error":"password must be at least 12 characters"}
```

Rate limiting returns HTTP `429 Too Many Requests`.

### Message Flow
- **Send**: Client sends a text or binary WebSocket frame, up to 1 MB
- **Store**: Server persists the blob and assigns a monotonic sequence number
- **Broadcast**: Server forwards the blob to other peers in the room
- **Ack**: Server sends `{"type":"ack","seq":N}` to the sender

### Catchup Format
```json
{"type":"catchup","seq":1,"blob":"<base64-encoded-original-bytes>"}
{"type":"catchup","seq":2,"blob":"<base64-encoded-original-bytes>"}
{"type":"catchup_done"}
```

### Error Format
```json
{"type":"error","msg":"description"}
```

### Rate Limits
- 100 messages per minute per connection, fixed window
- 1 MB max message size
- 5 connections per room, 20 per IP, 10 rooms per IP
- 50 MB storage per room

## Database

SQLite with WAL mode. Schema managed by sea-orm-migration.

**Tables:**
- `messages` , `(id, room_id, seq, blob, created_at)` with a unique index on `(room_id, seq)`
- `room_meta` , `(room_id, next_seq, last_activity, total_bytes)`
- `users` , `(id, email, password_hash, display_name, created_at, locked_until, failed_login_attempts, last_failed_login_at)`
- `devices` , `(id, user_id, name, device_type, room_id, created_at, last_seen)`
- `sessions` , `(id, user_id, device_id, token_hash, refresh_hash, expires_at, refresh_expires_at, last_used_at, created_at)`

An hourly maintenance task:
- deletes messages older than the retention period
- deletes sessions whose refresh window has expired

## Testing

```bash
# All tests
cargo test

# Lint check (same as CI/local verification)
cargo clippy --all-targets --all-features -- -D warnings

# Release build
cargo build --release
```

## Workspace Structure

```
relay/
├── Cargo.toml          # Workspace root
├── server/             # Main server crate (binary)
│   ├── src/
│   │   ├── main.rs     # Entry point, WAL setup, hourly maintenance
│   │   ├── server.rs   # axum router, WebSocket upgrade
│   │   ├── handler.rs  # WebSocket handler, auth, message processing
│   │   ├── auth/       # HTTP auth routes, middleware, models, token/password helpers
│   │   ├── storage.rs  # SeaORM persistence layer
│   │   ├── room.rs     # In-memory room and peer management
│   │   └── config.rs   # CLI and env configuration
│   └── tests/
│       ├── auth_routes.rs  # HTTP auth tests
│       ├── ws_require_auth.rs # require_auth websocket tests
│       ├── integration.rs  # websocket protocol tests
│       ├── e2e_sync.rs     # E2E sync simulations
│       └── common/mod.rs   # Test helpers
├── entity/             # SeaORM entity definitions
│   └── src/
│       ├── message.rs
│       ├── room_meta.rs
│       ├── users.rs
│       ├── devices.rs
│       └── sessions.rs
└── migration/          # sea-orm-migration schema
    └── src/
        ├── m20260424_000001_create_tables.rs
        └── m20260502_000002_create_auth_tables.rs
```

> This is a standalone workspace, separate from the main Maho Cargo workspace. Do not add these crates to the root `Cargo.toml`; they use a different `libsqlite3-sys` version.

## Deployment

Run behind a reverse proxy such as nginx or caddy with TLS termination:

Recommended production posture:
- leave registration closed after initial provisioning
- keep `require_auth=true`
- terminate TLS at the proxy
- forward the real client IP (`X-Real-IP` / equivalent) so auth route rate limits apply meaningfully

```nginx
location /ws {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header X-Real-IP $remote_addr;
    proxy_read_timeout 3600s;
}

location /health {
    proxy_pass http://127.0.0.1:8080;
}
```
