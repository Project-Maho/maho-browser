# Maho Settings + AI WebUI Migration — 15-Bug Fix Plan

**Date:** 2026-07-02
**Branch:** `main` (HEAD: `fa3445df`)
**Scope:** `maho-chromium/browser/ui/webui/maho_settings/`, `maho-chromium/browser/ui/webui/maho_ai/`, `maho-chromium/browser/resources/maho_settings/`, `maho/relay/server/src/`, `maho/crates/maho-ffi/`

---

## Executive Summary

A static + runtime audit of the settings/AI WebUI migration surfaced 15 defects across the auth flow, billing data pipeline, appearance effects, and traffic annotation compliance. This plan addresses all 15 in dependency-ordered phases (A–G), targeting one atomic commit per phase. Critical path: Phase B (C++ handler fixes) unblocks Phase D (billing wiring). Relay backend changes (Phase A) are parallel-safe with B/C. Bug #11 (3-platform window vibrancy) is the largest single item and warrants a follow-up sub-plan. Total estimated effort: 8–10 focused sessions.

---

## Bug Inventory

| # | Sev | Title | Location | Decided Fix |
|---|-----|-------|----------|-------------|
| 1 | P0 | `GetAccountStatus()` returns `signed_in=false` despite token pref present | `maho_settings_page_handler.cc:2757` | Wipe tokens ONLY on explicit 401-after-refresh. Network errors / 5xx / timeouts must NOT wipe. |
| 2 | P0 | `StoreRelayTokens` stores empty `user_id` and `user_tier` | `maho_settings_page_handler.cc:2476-2481` | `FindInt("id")` + `NumberToString()`. Tier fetched post-login from `/auth/subscription`. |
| 3 | P0 | Four `policy_exception_justification: "Not implemented."` | Settings L73,89,106 + AI L97 | Replace with Chromium policy audit rubric-aligned justifications. |
| 4 | P0 | `AISettings.opencode_runtime_enabled` dead field | `maho_settings.mojom:30` + handler L1621 | DELETE from mojom, handler, and TS types. |
| 5 | P1 | `BillingInfo.has_payment_method` hardcoded `false` | `maho_settings_page_handler.cc:2824` | Parse from relay response `dict.FindBool("has_payment_method")`. |
| 6 | P1 | `AccountStatus.subscription_status` hardcoded `""` | `maho_settings_page_handler.cc:2614,2850` | Store to new pref `kRelaySubscriptionStatus` after login + subscription fetch; read in GetAccountStatus. |
| 7 | P1 | `AccountStatus.subscription_expires_at` hardcoded `0` | `maho_settings_page_handler.cc:2615,2851` | Store to new pref `kRelaySubscriptionExpiresAt`; read in GetAccountStatus. |
| 8 | P1 | Invoices card 100% static | `domain_panes.tsx:1089-1101` | New mojom `GetInvoices()`, relay endpoint `GET /billing/invoices` (LS Orders API proxy), TS data-driven list. |
| 9 | P1 | Billing portal URL hardcoded | `domain_panes.tsx:931` | New mojom `GetBillingPortalUrl()`, relay `POST /billing/portal_session` (LS Customer Portal session). |
| 10 | P1 | `appearance.sidebar_width` slider no effect | `maho_settings_page_handler.cc:1401-1411` | Dual-write: also `prefs_->SetInteger(sidebar_prefs::kSidebarWidth, clamped)`. |
| 11 | P1 | `appearance.window_transparency` toggle no effect | `maho_settings_page_handler.cc:1398-1399` | 3-platform impl: macOS NSVisualEffectView, Windows Mica, Linux blur-behind. Separate sub-plan. |
| 12 | P2 | Theme store DNS failure | `maho_settings_page_handler.cc:1804` | Change error to "Theme store coming soon"; hide Retry on DNS failure. |
| 13 | P2 | SignOut uses `maho_core_sign_in()` FFI | `maho_settings_page_handler.cc:2579` | New `maho_core_dispatch_shell_event` FFI wrapper; reserve `sign_in` for SignIn only. |
| 14 | P2 | Login `device_name` hardcoded `"Maho Desktop"` | `maho_settings_page_handler.cc:2385` | Platform detection via `#if BUILDFLAG(IS_MAC/WIN/LINUX)` + `base::SysInfo`. |
| 15 | P2 | Credits progress bar raw `<div>` | `domain_panes.tsx:1069-1076` | Replace with shadcn `<Progress value={pct} />`. |

---

## Phased Execution Plan

### Phase A — Relay Backend Augmentation

**Goal:** Extend relay responses with fields the C++ handler and TS frontend need.
**Parallel-safe:** No dependency on C++ or TS changes.
**Commit:** `relay: add has_payment_method, /billing/invoices, /billing/portal_session endpoints`

#### A.1: Add `has_payment_method` to `SubscriptionResponse`

**Files:**
- `maho/relay/migration/src/` — new migration: `ALTER TABLE users ADD COLUMN has_payment_method BOOLEAN NOT NULL DEFAULT 0`
- `maho/relay/entity/src/users.rs` — add `pub has_payment_method: bool`
- `maho/relay/server/src/auth/models.rs` — add field to `SubscriptionResponse`
- `maho/relay/server/src/auth/routes.rs` — populate `has_payment_method: user.has_payment_method` in `subscription()`
- `maho/relay/server/src/billing.rs` — set `has_payment_method=true` on successful `subscription_created`/`order_created` webhook

**TDD:**
1. Write test: `GET /auth/subscription` returns `has_payment_method: false` for fresh user.
2. Write test: after simulated `subscription_created` webhook, subscription returns `has_payment_method: true`.
3. Implement migration + model + route changes.

**Verification:**
```bash
cd maho/relay && cargo test
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:18765/auth/subscription | jq .has_payment_method
```

#### A.2: Add `POST /billing/portal_session` endpoint

**Files:**
- `maho/relay/server/src/billing.rs` — add route handler
- `maho/relay/server/src/server.rs` — register `.route("/billing/portal_session", post(...))`
- `maho/relay/entity/src/users.rs` — ensure `lemonsqueezy_customer_id: Option<String>` field exists (add migration if needed)

**Design:** Bearer-authed. Calls LemonSqueezy Customer Portal API (`POST https://api.lemonsqueezy.com/v1/customer-portal-sessions`) with user's LS customer ID. Returns `{"url": "<session_url>"}`. If no LS customer ID stored, returns `{"url": "https://app.lemonsqueezy.com/my-orders"}` as fallback.

**TDD:**
1. Write test: unauthenticated request returns 401.
2. Write test: user without LS customer_id returns fallback URL.
3. (Integration, optional): mock LS API and verify returned URL format.

**Verification:**
```bash
curl -X POST -H "Authorization: Bearer $TOKEN" http://127.0.0.1:18765/billing/portal_session | jq .url
```

#### A.3: Add `GET /billing/invoices` endpoint

**Files:**
- `maho/relay/server/src/billing.rs` — add route handler
- `maho/relay/server/src/auth/models.rs` — add `InvoiceEntry` and `InvoicesResponse` structs

**Design:** Bearer-authed. Proxies LemonSqueezy Orders API (`GET https://api.lemonsqueezy.com/v1/orders?filter[user_email]=<email>`) filtered by user email or LS customer ID. Returns `{"invoices": [{id, created_at, amount_usd, currency, status, pdf_url}]}`. Returns empty array if LS API key not configured or user has no orders.

**Structs:**
```rust
#[derive(Debug, Serialize)]
pub struct InvoiceEntry {
    pub id: String,
    pub created_at: i64,
    pub amount_usd: f64,
    pub currency: String,
    pub status: String,  // "paid", "pending", "refunded"
    pub pdf_url: String,
}

#[derive(Debug, Serialize)]
pub struct InvoicesResponse {
    pub invoices: Vec<InvoiceEntry>,
}
```

**TDD:**
1. Write test: unauthenticated returns 401.
2. Write test: user with no LS config returns `{"invoices": []}`.
3. Write integration test with mock LS API server.

**Verification:**
```bash
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:18765/billing/invoices | jq '.invoices | length'
cd maho/relay && cargo test && cargo clippy -- -D warnings
```

---

### Phase B — C++ Handler Fixes (No Relay Dependency)

**Goal:** Fix auth parsing, remove dead code, clean FFI, add platform detection.
**Commit:** `settings: fix auth token clearing, user_id int parse, remove dead opencode field, add dispatch_shell_event FFI, platform device_name`

#### B.1: Bug #1 — Conditional token clearing on billing refresh failure

**Files:**
- `maho-chromium/browser/ui/webui/maho_auth_utils.h` — update `RefreshCallback` typedef to include `int http_status`
- `maho-chromium/browser/ui/webui/maho_auth_utils.cc` — pass HTTP status code in callback (L183-189)
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L2728-2784 (refresh failure lambda in `OnSubscriptionResponse`)

**Current behavior (L2756-2758):**
```cpp
if (!success || !tokens || !handler->encryptor_.has_value()) {
  maho::auth::ClearRelayTokens(handler->prefs_);
  handler->BroadcastAccountStatus();
  std::move(cb).Run(make_empty());
  return;
}
```

**Fix:** Only clear tokens when the refresh attempt received an explicit HTTP 401 response (meaning the refresh token is truly invalid/expired). Network errors, timeouts, and 5xx responses leave auth state intact.

**Updated RefreshCallback signature:**
```cpp
// maho_auth_utils.h:
using RefreshCallback = base::OnceCallback<void(
    bool success,
    int http_status,  // NEW: 0 for network error, 401 for invalid, etc.
    std::optional<RefreshedTokens> tokens)>;
```

**Updated maho_auth_utils.cc (RefreshAccessToken inner lambda):**
```cpp
if (net_error != net::OK || !response_body) {
  std::move(cb).Run(false, /*http_status=*/0, std::nullopt);
  return;
}
if (http_status != 200) {
  std::move(cb).Run(false, http_status, std::nullopt);
  return;
}
// ... success path:
std::move(cb).Run(true, 200, std::move(tokens));
```

**Updated page_handler.cc (refresh failure lambda):**
```cpp
[](base::WeakPtr<MahoSettingsPageHandler> handler,
   GetBillingInfoCallback cb, bool success, int refresh_http_status,
   std::optional<maho::auth::RefreshedTokens> tokens) {
  // ...
  if (!success || !tokens || !handler->encryptor_.has_value()) {
    // Only wipe auth on explicit 401 (refresh token invalid/expired):
    if (refresh_http_status == 401) {
      maho::auth::ClearRelayTokens(handler->prefs_);
      handler->BroadcastAccountStatus();
    } else {
      DLOG(WARNING) << "Billing refresh failed (HTTP "
                    << refresh_http_status
                    << "); preserving auth state.";
    }
    std::move(cb).Run(make_empty());
    return;
  }
  // ...success continues...
}
```

**TDD:**
1. Write test: login → stop relay → call `GetBillingInfo` → verify `GetAccountStatus` still returns `signed_in=true`.
2. Write test: login → relay returns 401 on refresh → verify tokens ARE cleared and `signed_in=false`.
3. Write test: login → relay returns 500 on refresh → verify tokens are NOT cleared.

**Verification:** CDP script: login → kill relay → navigate to billing → "Connection lost" shown BUT account pane still shows signed-in.

#### B.2: Bug #2 — Fix integer `user_id` parsing + post-login tier fetch

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L2473-2482 (OnLoginResponse account parsing), L2525 (post-login subscription fetch)

**Fix (user_id, replace L2476-2477):**
```cpp
// BEFORE:
if (const std::string* v = account->FindString("id"))
    user_id = *v;

// AFTER:
if (std::optional<int> v = account->FindInt("id"))
    user_id = base::NumberToString(*v);
else if (const std::string* v = account->FindString("id"))
    user_id = *v;  // Fallback for hypothetical future string IDs.
```

**Fix (tier — remove dead FindString, add post-login subscription fetch):**

Remove L2480-2481 (`FindString("tier")` which always returns null since `PublicUser` has no `tier` field).

After `BroadcastAccountStatus()` at L2525, trigger an async subscription fetch to populate tier + subscription prefs:

```cpp
BroadcastAccountStatus();

// Populate tier + subscription prefs from /auth/subscription.
// Fire-and-forget — failure leaves tier empty until user opens Billing pane.
GetBillingInfo(base::BindOnce([](maho_settings::mojom::BillingInfoPtr) {
  // Result persisted to prefs inside OnSubscriptionResponse; discard here.
}));

std::move(callback).Run(true, "");
```

**TDD:**
1. Write unit test: mock login JSON with `"account": {"id": 42, "email": "x@y.com", "display_name": "X"}` → verify `params.user_id == "42"`.
2. Write test: login with local relay → check prefs → `maho.auth.relay.user.id` is `"1"` (not empty).
3. Write test: after login, `GetAccountStatus().tier` is eventually populated (after async subscription fetch).

#### B.3: Bug #4 — Delete dead `opencode_runtime_enabled` field

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` — delete `bool opencode_runtime_enabled;` from `AISettings` struct (L30)
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — delete `settings->opencode_runtime_enabled = false;` (L1621)
- `maho-chromium/browser/resources/maho_settings/react/` — grep and remove any `opencode_runtime_enabled` references in TS types/components

**TDD:** Build succeeds. TS typecheck passes. No references remain (grep confirmation).

#### B.4: Bug #13 — New `maho_core_dispatch_shell_event` FFI

**Files:**
- `maho/crates/maho-ffi/src/lib.rs` — add:
  ```rust
  /// Dispatch an arbitrary ShellEvent to maho-core.
  /// Accepts the same JSON format as maho_core_sign_in but with clearer semantics.
  #[no_mangle]
  pub unsafe extern "C" fn maho_core_dispatch_shell_event(
      ptr: *mut MahoCore,
      event_json: *const c_char,
  ) -> *mut c_char {
      // Delegates to the same internal dispatcher.
      maho_core_sign_in(ptr, event_json)
  }
  ```
- `maho-chromium/third_party/maho/maho_ffi.h` — add declaration:
  ```c
  char* maho_core_dispatch_shell_event(struct MahoCore* ptr, const char* event_json);
  ```
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L2579:
  ```cpp
  // BEFORE:
  char* result = maho_core_sign_in(core, event_json);
  // AFTER:
  char* result = maho_core_dispatch_shell_event(core, event_json);
  ```

**TDD:**
1. `cd maho && bun run test` — FFI crate compiles and existing tests pass.
2. Login + logout cycle works against local relay (verified via CDP).

#### B.5: Bug #14 — Platform-aware device_name and device_type

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L2385-2386

**Fix:**
```cpp
// BEFORE:
body.Set("device_name", "Maho Desktop");
body.Set("device_type", "desktop");

// AFTER:
#if BUILDFLAG(IS_MAC)
  body.Set("device_name", "Maho macOS");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_WIN)
  body.Set("device_name", "Maho Windows");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_LINUX)
  body.Set("device_name", "Maho Linux");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_IOS)
  body.Set("device_name", "Maho iOS");
  body.Set("device_type", "mobile");
#elif BUILDFLAG(IS_ANDROID)
  body.Set("device_name", "Maho Android");
  body.Set("device_type", "mobile");
#else
  body.Set("device_name", "Maho");
  body.Set("device_type", "unknown");
#endif
```

**Verification:** Login on macOS → relay DB shows `devices.name = "Maho macOS"`, `devices.device_type = "desktop"`.

---

### Phase C — Traffic Annotations (Parallel-Safe)

**Goal:** Fix all 4 non-compliant `policy_exception_justification` strings.
**Commit:** `settings+ai: fix traffic_annotation policy_exception_justification compliance`

#### C.1: Bug #3 — Replace placeholder justifications

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L73, 89, 106
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc` — L97

**Fix (L73 — kAuthLoginTrafficAnnotation):**
```
policy_exception_justification:
  "Not controlled by enterprise policy. The user explicitly submits "
  "credentials in the Account settings pane to authenticate with the "
  "Maho relay service. No ambient or background network activity."
```

**Fix (L89 — kAuthLogoutTrafficAnnotation):**
```
policy_exception_justification:
  "Not controlled by enterprise policy. The user explicitly clicks "
  "Sign Out. A single best-effort POST invalidates the server session. "
  "No ambient or background network activity."
```

**Fix (L106 — kSubscriptionTrafficAnnotation):**
```
policy_exception_justification:
  "Not controlled by enterprise policy. Triggered when the user "
  "opens the Billing pane to view plan and credit balance. "
  "No ambient or background network activity."
```

**Fix (AI handler L97 — kCreditBalanceTrafficAnnotation):**
```
policy_exception_justification:
  "Not controlled by enterprise policy. Triggered when the signed-in "
  "user opens the AI panel. Fetches the current PAYG credit balance. "
  "No ambient or background network activity."
```

**TDD:** `grep -r "Not implemented." maho-chromium/browser/ui/webui/maho_settings/ maho-chromium/browser/ui/webui/maho_ai/` returns zero matches.

**Verification:** Build succeeds. Grep confirms zero matches for `"Not implemented."` in policy_exception_justification fields.

---

### Phase D — Billing Data Wiring (Depends on A + B)

**Goal:** Wire relay billing data through mojom to TS frontend for has_payment_method, subscription status, invoices, and portal URL.
**Commit:** `settings: wire billing data (has_payment_method, subscription prefs, invoices, portal URL)`

#### D.1: Bug #5 — `has_payment_method` from relay response

**Depends on:** A.1

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L2824

**Fix:**
```cpp
// BEFORE:
info->has_payment_method = false;  // Backend not implemented yet.

// AFTER:
info->has_payment_method = dict.FindBool("has_payment_method").value_or(false);
```

**Verification:** `curl /auth/subscription | jq .has_payment_method` returns true/false; UI shows correct payment method status.

#### D.2: Bugs #6 & #7 — Persist subscription_status and subscription_expires_at to prefs

**Depends on:** B.1 (so token clearing doesn't race), B.2 (post-login tier fetch)

**Files:**
- `maho-chromium/browser/ui/webui/maho_account_prefs.h` — add two new pref consts
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — multiple sites
- `maho-chromium/browser/ui/webui/maho_auth_utils.cc` — clear new prefs in ClearRelayTokens

**New prefs (`maho_account_prefs.h`):**
```cpp
inline constexpr char kRelaySubscriptionStatus[] =
    "maho.auth.relay.subscription_status";
inline constexpr char kRelaySubscriptionExpiresAt[] =
    "maho.auth.relay.subscription_expires_at";
```

**Register prefs** (in the profile prefs registration function):
```cpp
registry->RegisterStringPref(maho::account_prefs::kRelaySubscriptionStatus, "");
registry->RegisterInt64Pref(maho::account_prefs::kRelaySubscriptionExpiresAt, 0);
```

**OnSubscriptionResponse success path (after L2824):**
```cpp
// Persist subscription fields to prefs for GetAccountStatus:
prefs_->SetString(acct::kRelaySubscriptionStatus, info->subscription_status);
prefs_->SetInt64(acct::kRelaySubscriptionExpiresAt, info->subscription_expires_at);

// Also store tier from subscription response (login response doesn't carry tier):
if (!info->tier.empty()) {
  prefs_->SetString(acct::kRelayUserTier, info->tier);
}

// Re-broadcast so account pane updates without page reload:
BroadcastAccountStatus();
```

**GetAccountStatus (replace L2614-2615):**
```cpp
status->subscription_status = prefs_->GetString(acct::kRelaySubscriptionStatus);
status->subscription_expires_at = prefs_->GetInt64(acct::kRelaySubscriptionExpiresAt);
```

**BroadcastAccountStatus signed-in path (replace L2850-2851):**
```cpp
status->subscription_status = prefs_->GetString(acct::kRelaySubscriptionStatus);
status->subscription_expires_at = prefs_->GetInt64(acct::kRelaySubscriptionExpiresAt);
```

**ClearRelayTokens (maho_auth_utils.cc) — add to existing clear block:**
```cpp
prefs->ClearPref(maho::account_prefs::kRelaySubscriptionStatus);
prefs->ClearPref(maho::account_prefs::kRelaySubscriptionExpiresAt);
```

**TDD:**
1. Login → open Billing pane → `GetAccountStatus` returns non-empty `subscription_status`.
2. Restart browser → `GetAccountStatus` still returns persisted subscription data.
3. Logout → both prefs are cleared, `subscription_status` is empty.

#### D.3: Bug #8 — Invoices mojom + handler + TS

**Depends on:** A.3

**Mojom addition (`maho_settings.mojom`):**
```mojom
struct Invoice {
  string id;
  int64 created_at;
  float amount_usd;
  string currency;
  string status;
  string pdf_url;
};

// In PageHandler interface:
GetInvoices() => (array<Invoice> invoices);
```

**Handler implementation (`maho_settings_page_handler.cc`):**
- New method `GetInvoices(GetInvoicesCallback callback)`
- Fetches `GET /billing/invoices` from relay with bearer token
- Parses JSON array into `Invoice` mojom structs
- Needs new traffic annotation (with proper justification)
- On error/empty: returns empty array

**TS changes (`domain_panes.tsx`):**
Replace static empty state (L1089-1101) with:
```tsx
// Loading state:
{invoicesLoading && <Skeleton className="h-36 w-full" />}

// Data state:
{invoices.length > 0 && (
  <div className="divide-y divide-border/70 rounded-xl border border-border/70">
    {invoices.map(inv => (
      <div key={inv.id} className="flex items-center justify-between gap-4 p-4">
        <div>
          <p className="text-sm font-medium">{formatBillingDate(inv.created_at)}</p>
          <p className="text-xs text-muted-foreground">{inv.status}</p>
        </div>
        <div className="flex items-center gap-3">
          <span className="text-sm font-semibold">{formatUsd(inv.amount_usd)}</span>
          {inv.pdf_url && <a href={inv.pdf_url} target="_blank" rel="noopener">PDF</a>}
        </div>
      </div>
    ))}
  </div>
)}

// Empty state (only when loaded and genuinely empty):
{!invoicesLoading && invoices.length === 0 && (
  <div className="grid min-h-36 place-items-center ...">
    <p>No invoices yet</p>
  </div>
)}
```

**Verification:** CDP script: sign in → navigate to Billing → invoices section renders (either list or empty state, NOT stuck loading).

#### D.4: Bug #9 — Billing portal URL from relay

**Depends on:** A.2

**Mojom addition (`maho_settings.mojom`):**
```mojom
// In PageHandler interface:
GetBillingPortalUrl() => (string url);
```

**Handler implementation (`maho_settings_page_handler.cc`):**
- New method `GetBillingPortalUrl(GetBillingPortalUrlCallback callback)`
- `POST /billing/portal_session` to relay with bearer token
- Parses `{"url": "..."}` from response
- On error: returns empty string (TS handles gracefully)
- Needs new traffic annotation

**TS changes (`domain_panes.tsx` L930-932):**
```tsx
// BEFORE:
const openBillingPortal = () => {
  window.open('https://billing.stripe.com/', '_blank', 'noopener');
};

// AFTER:
const [portalLoading, setPortalLoading] = useState(false);
const openBillingPortal = async () => {
  setPortalLoading(true);
  try {
    const { url } = await handler.getBillingPortalUrl();
    if (url) window.open(url, '_blank', 'noopener');
  } finally {
    setPortalLoading(false);
  }
};
```

Update "Adjust plan" and "Manage" buttons: `disabled={portalLoading}`.

**Verification:** Click "Adjust plan" → new tab opens with LemonSqueezy portal URL (not `billing.stripe.com`).

---

### Phase E — Appearance Side-Effects

**Goal:** Make sidebar_width work immediately; plan window transparency separately.
**Commit (E.1 only):** `settings: bridge sidebar_width to sidebar_prefs for immediate effect`

#### E.1: Bug #10 — Sidebar width dual-write

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L1401-1412
- Add `#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"`
- Add `#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"`

**Fix:**
```cpp
} else if (key == "appearance.sidebar_width") {
    int width = 0;
    if (!base::StringToInt(value, &width)) return true;
    // Clamp to runtime range from layout tokens:
    width = std::clamp(width,
        sidebar_layout::kRuntimeRailWidthMinDp,   // 280
        sidebar_layout::kRuntimeRailWidthMaxDp);   // 400

    // 1. Write to Chromium pref — sidebar container observes via PrefChangeRegistrar:
    prefs_->SetInteger(sidebar_prefs::kSidebarWidth, width);

    // 2. Also update maho-core for cross-device sync:
    base::DictValue settings;
    base::DictValue appearance;
    appearance.Set("sidebarWidth", width);
    settings.Set("appearance", std::move(appearance));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
    handled = true;
}
```

**Also update AppendFfiSettings** (the reverse path that populates the UI slider value):
```cpp
// Read current width from Chromium pref (authoritative for desktop):
int current_width = prefs_->GetInteger(sidebar_prefs::kSidebarWidth);
settings.emplace_back("appearance.sidebar_width", base::NumberToString(current_width));
```

**TDD:**
1. Set slider to 350 → measure sidebar via DOM inspection → width ≈ 350px.
2. Set slider to 280 → sidebar = 280px.
3. Restart browser → sidebar width persists.

**Verification:**
```
CDP: navigate chrome://maho-settings?pane=appearance
     → set sidebar_width=350
     → wait 1s
     → DOM.getBoxModel on sidebar container
     → assert width ∈ [340, 360]
```

#### E.2: Bug #11 — Window transparency (3-platform) — SEPARATE SUB-PLAN

**Status:** Largest single item. Requires `chromium_src/` overrides across 3 platforms, version gating, runtime toggle, and visual QA per platform.

**Recommended:** Create a follow-up plan file at `.opencode/plans/window-transparency-3platform-plan-2026-07-XX.md` covering:

| Platform | Mechanism | Complexity |
|----------|-----------|------------|
| macOS | `NSVisualEffectView` on BrowserView contentView | Medium — well-documented API |
| Windows 11 | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_MAINWINDOW)` (Mica) | Medium — Win11 22H2+ only |
| Linux | Wayland: `KWinBlurManager` protocol; X11: `_KDE_NET_WM_BLUR_BEHIND_REGION` hint | High — best-effort, compositor-dependent |

**Pref:** `"maho.appearance.window_transparency"` (bool, default false).
**Runtime behavior:** Toggle vibrancy/Mica/blur on/off without restart.
**Location:** `maho-chromium/chromium_src/chrome/browser/ui/views/frame/` (Brave-style override).

---

### Phase F — Theme Store Error UX

**Goal:** Improve theme store error messaging without DNS infrastructure.
**Commit:** `settings: improve theme store unavailable messaging`

#### F.1: Bug #12 — Theme store error text + retry behavior

**Files:**
- `maho-chromium/browser/resources/maho_settings/react/` — theme store section in TS

**Fix (TS side):**
- When `success=false` and themes array is empty:
  - Show "Theme store coming soon" message (not "Theme store unavailable")
  - Hide "Retry" button when the failure is a DNS-level error (no cached data)
  - If cached themes exist from prior session, show them with "Offline — showing cached themes" indicator

**Verification:** Block DNS for `themes.maho.app` → open theme store → "Theme store coming soon" shown (no Retry button spinning forever).

---

### Phase G — Cosmetic

**Goal:** Replace raw progress bar with shadcn component.
**Commit:** `settings: replace credits progress div with shadcn Progress component`

#### G.1: Bug #15 — Shadcn `<Progress>` component

**Files:**
- `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx` — L1069-1076

**Fix:**
```tsx
// BEFORE:
<div
  aria-valuemax={100}
  aria-valuemin={0}
  aria-valuenow={progressValue}
  className="h-3 overflow-hidden rounded-full bg-muted/80"
  role="progressbar">
  <div className="h-full rounded-full bg-success transition-all" style={{width: `${progressValue}%`}} />
</div>

// AFTER:
<Progress value={progressValue} className="h-3 [&>div]:bg-success" />
```

Add import at top of file:
```tsx
import { Progress } from '@ui/progress';
```

**Verification:** Visual: credits bar renders correctly with shadcn styling. `bun run typecheck` passes.

---

## Dependency Graph

```
                    ┌─────────────────────────────────────┐
                    │          WAVE 1 (parallel)           │
                    │                                     │
                    │  A.1 ─────────────────────┐        │
                    │  A.2 ─────────────────┐   │        │
                    │  A.3 ─────────────┐   │   │        │
                    │  B.1 ─────────┐   │   │   │        │
                    │  B.2 ─────┐   │   │   │   │        │
                    │  B.3       │   │   │   │   │        │
                    │  B.4       │   │   │   │   │        │
                    │  B.5       │   │   │   │   │        │
                    │  C.1       │   │   │   │   │        │
                    │  E.1       │   │   │   │   │        │
                    │  F.1       │   │   │   │   │        │
                    │  G.1       │   │   │   │   │        │
                    └────────────┼───┼───┼───┼───┼────────┘
                                 │   │   │   │   │
                    ┌────────────▼───▼───▼───▼───▼────────┐
                    │          WAVE 2 (after wave 1)       │
                    │                                     │
                    │  D.2 ← (B.1 + B.2)                  │
                    │  D.3 ← (A.3)                        │
                    │  D.4 ← (A.2)                        │
                    │  D.1 ← (A.1)                        │
                    └─────────────────────────────────────┘
                                     │
                    ┌────────────────▼────────────────────┐
                    │       WAVE 3 (follow-up plan)        │
                    │                                     │
                    │  E.2 (3-platform transparency)       │
                    │  → separate .opencode/plans/ file    │
                    └─────────────────────────────────────┘
```

**Serialization constraints:**
- D.1 requires A.1 completed (relay must return `has_payment_method`)
- D.2 requires B.1 (token-clearing fix) + B.2 (post-login subscription fetch)
- D.3 requires A.3 (relay invoices endpoint)
- D.4 requires A.2 (relay portal session endpoint)
- E.2 deferred to separate plan file
- All Wave 1 items have zero inter-dependencies

---

## Verification Harness

### Setup: Local Relay

```bash
# Terminal 1 — Build and start relay
cd maho/relay && cargo build --release
MAHO_RELAY_SERVICE_TOKEN=devtoken123 \
  ./target/release/maho-relay \
    --port 18765 \
    --open-registration \
    --db-path /tmp/relay-settings-qa.db

# Terminal 2 — Register test user
curl -s -X POST http://127.0.0.1:18765/auth/signup \
  -H "Content-Type: application/json" \
  -d '{
    "email": "qa@maho.local",
    "password": "test12345678",
    "display_name": "QA Tester",
    "device_name": "QA Mac",
    "device_type": "desktop"
  }' | jq .

# Terminal 3 — Launch Maho pointed at local relay
MAHO_RELAY_URL=http://127.0.0.1:18765 \
  chromium/src/out/Default/Maho.app/Contents/MacOS/Maho \
  --remote-debugging-port=9222
```

### CDP Test Script: `test_settings_auth_billing_flow.ts`

Single-file CDP automation. Execute via:
```bash
bunx tsx test_settings_auth_billing_flow.ts
```

**Step 1 — Sign-in (validates Bug #1, #2):**
```
1. Connect to CDP at ws://127.0.0.1:9222
2. Navigate to chrome://maho-settings?pane=account
3. Wait for DOMContentLoaded
4. Fill email input: "qa@maho.local"
5. Fill password input: "test12345678"
6. Click Submit / Sign In button
7. Wait 5000ms
8. ASSERT: Element with text "QA Tester" is visible (display_name rendered)
9. ASSERT: Sign-in form is NOT visible
10. Evaluate: handler.getAccountStatus() → ASSERT signed_in === true
11. Evaluate: handler.getAccountStatus() → ASSERT user_id !== "" (Bug #2)
```

**Step 2 — Billing data (validates Bug #5, #6, #7):**
```
12. Navigate to chrome://maho-settings?pane=billing
13. Wait 3000ms for billing fetch to complete
14. ASSERT: subscription_status badge text !== "No subscription" OR equals a valid status
15. Evaluate: handler.getBillingInfo() → ASSERT has_payment_method is boolean (Bug #5)
16. Evaluate: handler.getAccountStatus() → ASSERT subscription_status !== "" (Bug #6)
17. Evaluate: handler.getAccountStatus() → ASSERT subscription_expires_at > 0 OR subscription_status === "free" (Bug #7)
18. ASSERT: credits section is rendered (progress bar element visible)
```

**Step 3 — Invoices + Portal (validates Bug #8, #9):**
```
19. ASSERT: invoices section is rendered (NOT perpetually loading)
20. ASSERT: "Adjust plan" button href/onclick does NOT contain "billing.stripe.com" (Bug #9)
```

**Step 4 — Sidebar width (validates Bug #10):**
```
21. Navigate to chrome://maho-settings?pane=appearance
22. Evaluate: handler.setSetting("appearance.sidebar_width", "350")
23. Wait 1000ms
24. DOM.getBoxModel on sidebar container element
25. ASSERT: width ∈ [340, 360] (±10px tolerance for DPI scaling)
```

**Step 5 — AccountStatus persistence (regression test for Bug #1):**
```
26. Navigate to chrome://newtab
27. Wait 1000ms
28. Navigate to chrome://maho-settings?pane=account
29. Wait 2000ms
30. ASSERT: account pane still shows "QA Tester" (NOT sign-in form)
31. Evaluate: handler.getAccountStatus() → ASSERT signed_in === true
```

**Step 6 — Sign-out:**
```
32. Click "Sign Out" button
33. Wait 2000ms
34. ASSERT: sign-in form is visible
35. Evaluate: handler.getAccountStatus() → ASSERT signed_in === false
36. ASSERT: user_id === ""
```

**Exit code:** 0 if all 36 assertions pass, 1 on first failure (with descriptive message identifying which assertion failed and actual vs expected values).

---

## Commit Strategy

| Phase | Commit Message | Files Touched | Gate |
|-------|---------------|---------------|------|
| A | `relay: add has_payment_method field, /billing/invoices, /billing/portal_session` | `maho/relay/` (migration, entity, server/src/) | `cd maho/relay && cargo test && cargo clippy -- -D warnings` |
| B | `settings: fix auth token clearing semantics, user_id int parse, remove dead opencode field, add dispatch_shell_event FFI, platform device_name` | `maho-chromium/browser/ui/webui/maho_settings/`, `maho-chromium/browser/ui/webui/maho_auth_utils.*`, `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom`, `maho/crates/maho-ffi/src/lib.rs`, `maho-chromium/third_party/maho/maho_ffi.h` | Build + CDP sign-in/sign-out cycle passes |
| C | `settings+ai: fix traffic_annotation policy_exception_justification compliance` | `maho_settings_page_handler.cc`, `maho_ai_page_handler.cc` (text only) | Build + grep confirms zero "Not implemented." in policy fields |
| D | `settings: wire billing data (has_payment_method, subscription prefs, invoices, portal)` | `maho_settings.mojom`, `maho_settings_page_handler.cc/h`, `maho_account_prefs.h`, `maho_auth_utils.cc`, `domain_panes.tsx` | CDP billing flow (steps 12-20) passes |
| E.1 | `settings: bridge sidebar_width to sidebar_prefs for immediate desktop effect` | `maho_settings_page_handler.cc` | Slider moves sidebar (CDP step 21-25) |
| E.2 | `appearance: implement 3-platform window transparency` | TBD (sub-plan) | Per-platform visual verification |
| F | `settings: improve theme store unavailable messaging` | `domain_panes.tsx` (theme section) | DNS block → "coming soon" shown |
| G | `settings: replace credits progress div with shadcn Progress` | `domain_panes.tsx` | Visual + TS typecheck |

**Merge order:** A, B, C can merge independently in any order. D merges after A+B. E.1, F, G merge independently at any point after their respective Wave 1 items.

---

## Estimated Work

| Phase | Sessions | Risk | Notes |
|-------|----------|------|-------|
| A (Relay) | 1.5 | Low | Additive; LS API integration for invoices/portal adds modest complexity. |
| B (C++ fixes) | 2 | Medium | RefreshCallback signature change ripples through all callers. FFI rename requires Rust rebuild. |
| C (Annotations) | 0.5 | Low | Text-only edits in 2 files. |
| D (Billing wiring) | 2 | Medium | Mojom changes require full Chromium rebuild. Multiple files across 4 layers. |
| E.1 (Sidebar width) | 0.5 | Low | Single pref write + include addition. |
| E.2 (Transparency) | 3+ | High | 3 platforms, chromium_src overrides, version gating. **Separate sub-plan.** |
| F (Theme store UX) | 0.5 | Low | TS string change + conditional rendering. |
| G (Cosmetic) | 0.5 | Low | Single component swap. |

**Total: ~8 sessions** (excluding E.2 which gets its own plan)
**Critical path duration: ~5 sessions** (B → D is the longest serialized chain)
