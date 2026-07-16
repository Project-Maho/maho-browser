# Local Dev Login — Bypassing the Maho Login Gate

**Audience:** developers running the Maho desktop browser (Chromium fork) locally.
**Problem this solves:** a fresh Maho profile shows the login/welcome wall and, in
some flows, refuses to open a browser window until a valid relay session exists.
This runbook explains the gate, how to bypass it for local development, how to run
a local relay server, and the local test account that was created for this purpose.

---

## 1. How the login gate works

At profile init, Maho decides whether to gate the whole browser:

- `maho-chromium/browser/maho_browser_main_extra_parts.cc:2032`
  ```cpp
  const bool needs_login_gate = !maho::auth::HasValidRelaySession(prefs);
  prefs->SetBoolean(maho::welcome::kLoginGateActive, needs_login_gate);
  if (needs_login_gate) maho::MahoWelcomeWindow::Show(profile);
  ```
- `HasValidRelaySession` (`maho-chromium/browser/ui/webui/maho_auth_utils.cc:282`)
  returns `true` when the profile prefs hold a **non-expired access token**, or a
  non-expired **refresh token**. The relevant prefs:
  `kRelayAccessTokenEncryptedB64`, `kRelayRefreshTokenEncryptedB64`,
  `kRelayAccessTokenExpiresAt`, `kRelayRefreshTokenExpiresAt`.
- While `maho.auth.login_gate_active` (`maho_welcome_prefs.h:11`) is `true`, the
  `chromium_src` override of `startup_browser_creator_impl.cc` suppresses startup
  browser creation and session restore, so no browser window appears.
- The gate is **recomputed on every launch** from `HasValidRelaySession`. A token
  that expires re-activates the gate on the next start.

There is **no hardcoded/baked-in dev account or password.** A session is purely
token-based, and tokens come from the relay auth server.

---

## 2. Two ways to develop past the gate

### A. Dev Skip (no account, per-session)

The welcome UI exposes a **Dev Skip** button *only in local-dev builds*:

- `MahoWelcomePageHandler::DevSkipOnboarding()`
  (`maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc:511`)
  runs **only when the `MAHO_RELAY_URL` env var is set** — otherwise it logs
  `"DevSkipOnboarding ignored: not a local-dev build."` and returns.
- The React welcome app renders the button when `snapshot.isLocalDev` is true:
  `browser/resources/maho_welcome/react/app.tsx:227,287,814` →
  `store.ts:396 devSkipOnboarding()`.
- Clicking it calls `DoFinishOnboarding()`, which sets
  `kLoginGateActive = false`, `kWelcomeCompleted = true`, and reveals the browser.

Dev Skip does **not** create tokens, so it only clears the gate for the current
session; the next launch re-gates unless you actually log in (approach B).

**Launch:**
```bash
MAHO_RELAY_URL=http://localhost:8787 \
  "chromium/src/out/Default/Maho.app/Contents/MacOS/Maho"
```
The binary is `Maho.app` (not `Chromium.app`). With `MAHO_RELAY_URL` set, the
welcome screen shows the **Dev Skip** button — click it to enter the browser.

### B. Real local login (persistent)

Run a local relay (section 3), then sign up / log in through the welcome flow with
the test account (section 4). Real tokens get stored in the profile prefs and
`HasValidRelaySession` stays true until they expire, so the gate does not
re-appear on subsequent launches.

`GetRelayBaseUrl()` (`maho_auth_utils.cc:303`) reads `MAHO_RELAY_URL`, falling back
to the production `sync.mahobrowser.com`. So `MAHO_RELAY_URL` both enables local-dev
mode **and** points auth calls at your local relay.

---

## 3. Running the local relay server

The relay is a standalone Cloudflare Workers project at `maho/relay/` (see
`maho/relay/AGENTS.md`). It runs fully locally via wrangler + miniflare — no
Cloudflare account or network is needed.

### Prerequisites (already present on this machine)
- `wrangler` (v4.62), `worker-build`, Rust `wasm32-unknown-unknown` target, `bun`/`node`.
- `maho/relay/.dev.vars` — holds server secrets (`ARGON2_PEPPER`,
  `MAHO_RELAY_SERVICE_TOKEN`, `RESEND_API_KEY`, `LEMONSQUEEZY_API_KEY`,
  `LS_WEBHOOK_SECRET`). **Do not commit or print these values.** `ARGON2_PEPPER`
  is required for signup/login (password hashing).

### Important: use the `staging` env for local dev
`wrangler.toml` puts the D1 database, vars, and Durable Object bindings **only**
under `[env.production]` and `[env.staging]` — the default env has none, so plain
`wrangler dev` fails. Use `--env staging`, which sets `OPEN_REGISTRATION=true`,
`MIN_PASSWORD_LENGTH=8`, and dev-friendly rate limits, and binds a **local**
miniflare D1 (persisted under `maho/relay/.wrangler/state/v3/d1`).

### Steps
```bash
cd maho/relay

# 1. Apply migrations to the LOCAL D1 (idempotent; --local = miniflare, not remote)
wrangler d1 migrations apply maho-relay-db-staging --env staging --local

# 2. Start the local dev server (loads .dev.vars secrets automatically)
wrangler dev --env staging --port 8787
# → "Ready on http://localhost:8787"
```

Notes:
- `wrangler dev` defaults to **local** simulation (miniflare); it does NOT touch
  the remote Cloudflare D1 unless you pass `--remote`.
- The local D1 state persists across restarts, so accounts you create survive a
  server restart.
- A benign warning about `compatibility_date` falling back to the runtime's latest
  is expected and harmless.

**In this environment the server is currently running** (started in a tmux session
named `maho-relay`; logs also tee'd to `/tmp/maho-relay-dev.log`). The relay itself
is a normal OS process tree (`wrangler dev` → `workerd`) listening on
`127.0.0.1:8787`, independent of which shell/tmux launched it.
```bash
tail -f /tmp/maho-relay-dev.log            # view logs
lsof -iTCP:8787 -sTCP:LISTEN -n -P         # confirm it is listening
# stop it (reliable, socket-agnostic):
pkill -f 'wrangler dev --env staging'
# or, if attached to the same tmux server that launched it:
tmux kill-session -t maho-relay
```

### Relay auth API (for reference)
`POST /auth/signup`, `POST /auth/login`, `POST /auth/refresh`, `GET /auth/me`
(see `maho/relay/AGENTS.md` for the full surface).

---

## 4. Local test account

A local-dev test account was created against the local relay (local miniflare D1
only — **not** a real/remote account). It persists in
`maho/relay/.wrangler/state/v3/d1`.

| Field | Value |
|-------|-------|
| Email | `dev@maho.local` |
| Password | `MahoDev!2026` |
| Display name | `Maho Dev` |
| Env | `staging` (local) |

How it was created + verified (both returned `HTTP 200` with an `AuthResponse`
containing `access_token` / `refresh_token`):
```bash
# signup
curl -s -X POST http://localhost:8787/auth/signup \
  -H "Content-Type: application/json" \
  -d '{"email":"dev@maho.local","password":"MahoDev!2026","display_name":"Maho Dev","device_name":"Local Dev Mac","device_type":"desktop"}'

# login (verify)
curl -s -X POST http://localhost:8787/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email":"dev@maho.local","password":"MahoDev!2026","device_name":"Local Dev Mac","device_type":"desktop"}'
```

To recreate it after wiping local D1 state, re-run the migrations + signup above.
`OPEN_REGISTRATION=true` on staging allows re-signup; if the email already exists
you get `409 email already exists` (log in instead).

---

## 5. Putting it together — launch the browser logged in

```bash
# 1. relay running locally (section 3), on :8787
# 2. launch Maho pointed at the local relay
MAHO_RELAY_URL=http://localhost:8787 \
  "chromium/src/out/Default/Maho.app/Contents/MacOS/Maho"
```
Then either:
- Click **Dev Skip** on the welcome screen (per-session, no login), or
- Log in with `dev@maho.local` / `MahoDev!2026` (persistent — no gate on next launch).

---

## 6. browser_tests / interactive_ui_tests (InProcessBrowserTest)

The Dev Skip path is a Mojo call from the welcome WebUI and is **not** reachable in
`InProcessBrowserTest`, so gtest binaries (`browser_tests`, `interactive_ui_tests`)
still hit the gate: with no relay session, `login_gate_active` is set at startup and
`RestoreOrCreateBrowser` suppresses window creation → `browser()` is null →
`startup_browser_creator_impl.cc:270 "No browser window found for startup"` → SIGSEGV,
**harness-wide** (even untouched upstream tests like `BrowserTest.Title` crash).

`MAHO_RELAY_URL` does **not** help here (no welcome UI to click). To run these tests
you must remove the gate at the harness level — e.g. seed the profile prefs in the
test fixture's `SetUpOnMainThread` / local-state setup so the gate never activates:
- set `maho.auth.login_gate_active = false`, or
- seed a valid relay session (`kRelayAccessTokenEncryptedB64` + a future
  `kRelayAccessTokenExpiresAt`).

This is a test-infra change to the base browser-test fixture and is intentionally
**not** applied by default; treat it as the known path for enabling Maho
`InProcessBrowserTest` runtime locally or in CI. Pure unit tests
(`unit_tests`: `MahoTabContextMenuTest.*`, `SidebarSplitGroupRenderTest.*`) do not
launch a browser and are unaffected by the gate.

---

## 7. Key source references

| Concern | Location |
|---------|----------|
| Gate decision at startup | `maho-chromium/browser/maho_browser_main_extra_parts.cc:2032` |
| Session validity check | `maho-chromium/browser/ui/webui/maho_auth_utils.cc:282` (`HasValidRelaySession`) |
| `MAHO_RELAY_URL` base-url read | `maho-chromium/browser/ui/webui/maho_auth_utils.cc:303` (`GetRelayBaseUrl`) |
| `login_gate_active` pref | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_prefs.h:11` |
| Dev Skip handler (needs `MAHO_RELAY_URL`) | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc:511` |
| Dev Skip button (isLocalDev) | `maho-chromium/browser/resources/maho_welcome/react/app.tsx:227,287,814` |
| Startup suppression override | `maho-chromium/chromium_src/chrome/browser/ui/startup/startup_browser_creator_impl.cc` |
| Relay server | `maho/relay/` (`AGENTS.md`, `wrangler.toml`, `src/auth/routes.rs`) |
