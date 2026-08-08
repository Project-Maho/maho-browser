# Google Sign-In (Loopback OAuth) Reliability — Root Cause & Regression Guard

**Status:** Fixed and runtime-verified 2026-08-05. This is a **durable contract**, not a
scratch note. Read it before touching the welcome/identity Google sign-in flow.

**Audience:** anyone editing the relay *identity* login (the "Continue with Google" button
on the welcome screen). This is the login that establishes a **relay session** and clears
the login gate. It is **not** the Gmail *mail-account* OAuth (that is a separate loopback in
`mail-core`; see `plan/mail-features-implementation-plan.md`).

---

## 0. TL;DR — what broke and what must never regress

The welcome Google sign-in worked intermittently ("됐다 안됐다"). Four independent defects
each produced a *different* user-visible failure. All four are fixed. **Do not reintroduce
any of them.** The invariants in §3 are load-bearing; the tests in §4 lock them.

| # | Symptom user saw | Root cause | Fix anchor |
|---|---|---|---|
| F1a | "sign-in timed out waiting for redirect" | 300s hard cap; real first-time consent (account picker + password + passkey + consent) exceeds it | `CALLBACK_TIMEOUT = 900s`, `google_identity.rs` |
| F1b | leaked loopback port + listener thread; "already in progress" on retry | listener thread only ended on timeout; no cancel path | per-flow `AtomicBool` cancel registry, `google_identity.rs` |
| F2  | "This site can't be reached" on `127.0.0.1:<port>` after restart | ephemeral `127.0.0.1:0` port bound per flow; a session-restored dead callback tab reloaded a port nothing listens on | success page now self-closes (`window.close()`) so no callback tab is ever restored; `SUCCESS_RESPONSE` |
| F3  | login silently failed even though Google succeeded | popup teardown (`WebContentsDestroyed`) after a successful redirect set `retired_` and dropped the later `OnCoreComplete` | `DidStartNavigation` stops observing once the popup reaches the loopback host; `maho_google_sign_in.cc` |
| F4  | "sign-in already in progress" and neither flow completed | a stale `ActiveFlow` singleton blocked new starts | new start **supersedes** the stale flow via `RetireForSupersede()`; `maho_google_sign_in.cc` |

---

## 1. Flow map (who owns what)

```
welcome UI "Continue with Google"
  └─ MahoWelcomePageHandler::SignInWithGoogleFromWelcome
       └─ maho::auth::StartGoogleSignIn            (maho_google_sign_in.cc)   [C++ / UI thread]
            └─ maho_google_sign_in_start (FFI)     (maho-ffi/src/lib.rs)
                 └─ google_identity::start_google_sign_in                     [Rust / core]
                      ├─ binds a loopback TcpListener on 127.0.0.1:<ephemeral>
                      ├─ builds the Google auth URL (redirect_uri = that loopback)
                      └─ spawns a listener thread: accept → read code → exchange → callback
   token exchange (code -> id_token) is delegated to the relay:
        POST {relay}/auth/oauth/google/exchange   (desktop cannot hold the client secret)
```

- **C++ side** owns the popup/peek WebContents and the `ActiveFlow()` singleton, and reports
  the outcome to the welcome UI via `OnCoreComplete`.
- **Rust side** (`maho-core`) owns the loopback listener, the CSRF `state`, PKCE, and the
  relay token exchange.
- The two are coupled by the CSRF `state` string.

---

## 2. Where the fixes live (verified anchors)

### Rust — `maho/crates/maho-core/src/google_identity.rs`
- `const CALLBACK_TIMEOUT: Duration = Duration::from_secs(900);` — **backstop only.** A flow
  normally ends on redirect or on C++ cancel. The comment above it explains why a short cap
  silently killed real sign-ins; **do not shorten it** to "tidy up."
- `const SUCCESS_RESPONSE` — the 200 page returned on the loopback. It **must** contain
  `<script>window.close()</script>`. That auto-close is what prevents F2 (a restored dead
  callback tab). Do not strip the script.
- `cancels()` / `register_cancel` / `take_cancel` / `pub fn cancel_google_sign_in(state)` —
  per-flow cancellation registry keyed by CSRF `state`.
- `accept_with_timeout(listener, cancel)` — polls both the cancel flag (every
  `ACCEPT_POLL_INTERVAL`) **and** the backstop deadline. Returns `"sign-in cancelled"` on
  cancel. This is what lets a superseded/aborted flow release its port immediately instead
  of leaking it until 900s.
- `start_google_sign_in` registers the cancel flag before spawning the thread and clears it
  (`take_cancel`) when the thread finishes.

### FFI — `maho/crates/maho-ffi/src/lib.rs`
- `maho_google_sign_in_cancel(state)` FFI export wrapping `cancel_google_sign_in`.
  **Currently unwired on the C++ side on purpose** — see §5 (header ownership). The Rust
  cancel infrastructure is complete and tested; the C++ supersede path (F4) achieves
  deterministic teardown without needing this symbol yet.

### C++ — `maho-chromium/browser/ui/webui/maho_google_sign_in.cc`
- `DidStartNavigation(...)` — when the popup's primary main frame navigates to a loopback
  host (`127.0.0.1` / `localhost` / `[::1]`), it calls `Observe(nullptr)`. From that instant
  the Rust listener owns the outcome, so the popup's later teardown must **not** be
  interpreted as a cancellation. **This is the F3 fix. Do not remove the host check or the
  `Observe(nullptr)` call.**
- `WebContentsDestroyed()` — only *now* (i.e. before the loopback redirect) does a popup
  close mean "user cancelled" → `Retire(...)`.
- `RetireForSupersede()` — abandons a stale flow **without** reporting a cancellation error
  to the UI, and resolves its own callback benignly so a stale JS promise cannot hang.
- `StartGoogleSignIn(...)` — if `ActiveFlow()` is set, it calls
  `ActiveFlow()->RetireForSupersede()` and resets it, then installs the new flow. **This is
  the F4 fix. A new start must supersede, never reject with "already in progress."**

---

## 3. Invariants — DO NOT REGRESS

1. **The callback wait is effectively unbounded for a human.** `CALLBACK_TIMEOUT` is a
   backstop measured in *minutes* (900s). Never reduce it to a "reasonable" few-second/one-
   minute value; first-time consent legitimately takes minutes.
2. **The loopback success page must self-close.** Keep `window.close()` in `SUCCESS_RESPONSE`.
   A visible/persistent callback tab can be session-restored later and hit a dead ephemeral
   port ("This site can't be reached").
3. **Reaching the loopback host means the flow succeeded from the popup's POV.** Once the
   popup navigates to `127.0.0.1`/`localhost`/`[::1]`, stop observing it. Popup teardown after
   that point is normal and must not drop the pending `OnCoreComplete`.
4. **A new sign-in supersedes a stale one.** Never fail a fresh start with "already in
   progress." Retire the old flow (benignly) and take over.
5. **Aborted/superseded flows must release their port promptly.** The listener thread polls a
   cancel flag; keep that poll so a dead flow does not hold a loopback port until the backstop.
6. **The desktop never holds the Google client secret.** Code→token exchange stays delegated
   to the relay `/auth/oauth/google/exchange`. (Client-secret provisioning for the exchange is
   a relay/build concern; do not bake a secret into the desktop binary for this identity flow.)

---

## 4. Regression guard (tests)

- **Rust unit tests** (`maho/crates/maho-core/src/google_identity_tests.rs`): cover the cancel
  registry (register/cancel/idempotent-after-finish), the backstop timeout path, and that the
  success response contains the auto-close script. Run:
  ```bash
  cd maho && cargo test -p maho-core --lib google_identity
  ```
  Expected: all green (19 tests as of the fix). Keep these passing.
- **C++ browsertest** (`maho-chromium/browser/ui/webui/maho_google_sign_in_peek_browsertest.cc`):
  covers host selection / peek-vs-popup routing. **Note:** these cases are currently marked
  *"Deferred execution: shared \*browsertests targets currently do not link"* — they do not run
  in CI yet, so they are **not** a live guard for F2/F3/F4. Until that target links, F2/F3/F4
  are protected by the invariants above + a manual runtime check (§6). If you make the
  browsertests link, add cases: (a) popup closes *after* loopback redirect → sign-in still
  succeeds (F3); (b) a second `StartGoogleSignIn` supersedes the first (F4).

---

## 5. Header-ownership caveat (why the cancel FFI is unwired)

`build_maho_core.py` only rewrites the ownership-gated `third_party/maho/maho_ffi.h` when the
build's `ACTIVE_TASK`/`TASK_ID` matches the arc-parity evidence TSV. In a normal build both are
unset ⇒ `owned=False` ⇒ the script logs `Active task None does not own ... maho_ffi.h. Skip
replace.` and leaves the header untouched. Consequences:

- A newly added FFI export (e.g. `maho_google_sign_in_cancel`) will **not** appear in the
  committed `maho_ffi.h`, so C++ cannot call it in an ordinary build.
- Therefore the C++ fixes (F2/F3/F4) were deliberately written to **not** depend on the new
  cancel symbol. The Rust cancel path is ready and tested for when the header is regenerated
  under proper ownership.
- **Do not hand-edit `maho_ffi.h`** to expose the symbol. Regenerate it through the owned
  build path instead.

---

## 6. Manual runtime verification (the check that actually caught the bugs)

Because the browsertests don't link yet, verify a real sign-in end-to-end after any change to
this flow:

1. Build via the official wrapper (never delete `.build.lock`):
   ```bash
   python3 maho-chromium/build/scripts/build_maho.py --skip-lucide-check --no-launch
   ```
2. Launch a **fresh profile** with the welcome gate active (no `--maho-disable-login-gate`,
   because you are testing the login itself) and DevTools open:
   ```bash
   "chromium/src/out/Default/Maho.app/Contents/MacOS/Maho" \
     --user-data-dir=/tmp/maho-login-verify --remote-debugging-port=9779
   ```
   (Relay client-secret provisioning for the exchange is environment/build-specific; use the
   same mechanism the build uses. Never print the secret.)
3. Complete "Continue with Google" once. Confirm, via the profile `Default/Preferences`
   (presence only — never print token values):
   - `maho.auth.relay.access_token.encrypted_b64` present
   - `maho.auth.relay.refresh_token.encrypted_b64` present
   - `maho.auth.relay.user.email` / `display_name` / `oauth_provider=google` present
   and that the welcome wizard advances past the sign-in step (gate cleared).
4. Sanity: the loopback callback tab must **not** linger (F2), a slow consent must **not** time
   out (F1a), and closing the Google popup after redirect must **not** fail the sign-in (F3).

Runtime-verified clean on 2026-08-05 with a fresh profile: relay access+refresh tokens and
user identity persisted, wizard advanced, no timeout / dead-port / dropped-success.

---

## See also
- `docs/operations/local-dev-login.md` — the login **gate** (bypass, local relay, dev skip).
  That doc is about getting *past* the gate for dev; **this** doc is about the Google sign-in
  that *legitimately clears* the gate never regressing.
