# Import Migration Phase 4: Destination Callbacks + Streaming Events

**Created**: 2026-06-24
**Status**: Plan-only. Awaits explicit GO from user.
**Owner**: Sisyphus (this session)
**Estimate**: 14–22h (~2 working days; +1d if Chromium API drift)
**Predecessor**: `.sisyphus/plans/import-rust-migration-2026-06-16.md` (Phases 1+2+3a complete; Phase 3b NSS-3 complete in this session)

---

## 1. Goal

Close the two remaining functional gaps in the Rust-side import system:

1. **Part A — Destination callbacks (~12–18h)**: Wire `MahoCoreDestination`'s 3 stubbed methods (`add_cookie`, `add_autofill`, `add_favicon`) through C++ callbacks back to Chromium's `CookieManager`, `WebDataService`, and `FaviconService`. Today these return `false`, silently dropping data.
2. **Part B — Streaming progress (~2–3h)**: Fix orchestrator so progress events emit during import, not in a batch at the end. Currently a Zen import of 12.7k history shows 0 events for ~10s then everything at once.

**Non-goals**:
- History / Bookmarks / Passwords routing change — those currently go to `maho.db` (Maho's own storage). The user's Maho UI surfaces them. No need to also write to Chromium HistoryService / BookmarkModel / PasswordStore in this plan.
- Bookmarks `services_supported` bit activation for Chrome — separate concern.
- Visual regression tests (#28 in ACCEPTANCE.md) — separate concern.

---

## 2. Architecture Decisions

### 2.1 FFI strategy

Evaluated 4 options; chose **Option D — typed callback pointers passed at `orchestrator_start`**:

| Option | Pros | Cons | Decision |
|---|---|---|---|
| A: per-service FFI exports (`maho_import_set_cookie_cb` …) | explicit | FFI surface grows 5→8 | ❌ |
| B: generic `data_callback(type_id, json)` | minimal surface | JSON marshaling overhead per item (12k cookies) | ❌ |
| C: extend progress callback with payload | zero new FFI | conflates progress vs data | ❌ |
| **D: pass typed function pointers at `orchestrator_start`** | no new exports; type-safe; no JSON | function-ptr signatures in header | ✅ |

**FFI export count stays at 5**: `detect_browsers`, `orchestrator_start` (extended), `orchestrator_cancel`, `orchestrator_free`, `string_free`. Callback `typedef`s are not export symbols.

### 2.2 Threading model

- `MahoImportSessionBridge` is constructed on UI thread by `MahoWelcomePageHandler` (and `MahoMigrationDialogView`).
- Bridge captures `Profile*` and `scoped_refptr<base::SequencedTaskRunner> ui_task_runner_` at construction.
- Rust orchestrator thread invokes static thunks.
- Thunks **immediately copy primitives + strings**, then `PostTask` to UI thread.
- Chromium service calls always run on UI thread (using captured `Profile*`).
- `weak_factory_` guards: if bridge is destroyed before the `PostTask` fires, the call is a no-op.

### 2.3 Data marshaling

- Pass primitives (C strings + i64 + bool + i32) for cookies / autofill.
- Pass `(*const u8, usize)` byte slice for favicon PNGs (single heap copy on C++ side).
- No JSON anywhere in this hot path.

### 2.4 Sync vs async callback semantics

- **Fire-and-forget**: Rust orchestrator does not wait for C++ persistence to complete. After invoking the callback, it immediately moves to the next entry.
- Progress count reports "callbacks issued", not "items persisted" — acceptable because chromium-side persistence rarely fails (and when it does, it's logged but the user wouldn't be able to act on partial-success info anyway).
- Eventual consistency: by the time Maho's import-complete UI shows, all `PostTask`s have queued but may not have flushed. This is fine — Chromium service queues are durable.

---

## 3. Phase 1 — Streaming Events Fix (2–3h)

### 3.1 Current code (`maho/crates/maho-ffi/src/import.rs:559–578`)

```rust
.spawn(move || {
    let mut orchestrator = Orchestrator::new(destination);
    let (tx, rx) = mpsc::channel();
    let _ = orchestrator.start_import(&browser, items_bitmask, tx);  // BLOCKS
    for progress in rx.try_iter() {                                  // drains AFTER
        ctx.forward_progress(&progress);
    }
})
```

**Root cause**: `start_import` runs synchronously on the same thread that drains the channel. `tx` is only dropped after `start_import` returns, so `try_iter()` finds events only at the end. For 12.7k history, that's ~10s of UX blackout.

### 3.2 Fix

Spawn a nested **import worker thread**; let the outer thread block on `rx.recv()`:

```rust
.spawn(move || {
    let (tx, rx) = mpsc::channel();
    let cancel_for_worker = cancel_clone.clone();
    let browser_for_worker = browser.clone();
    let essentials_for_worker = essentials.clone();

    let worker = thread::Builder::new()
        .name("maho-import-worker".into())
        .spawn(move || {
            let mut orchestrator = Orchestrator::new(destination);
            if !essentials_for_worker.is_empty() {
                orchestrator.set_selected_essentials(essentials_for_worker);
            }
            let _ = orchestrator.start_import(&browser_for_worker, items_bitmask, tx);
            // tx drops here, closing channel and signaling main thread to stop
        }).ok();

    while let Ok(progress) = rx.recv() {
        if cancel_clone.load(Ordering::SeqCst) {
            ctx.invoke(3, 0, 0, ptr::null());
            break;
        }
        ctx.forward_progress(&progress);
    }
    if let Some(h) = worker { let _ = h.join(); }
})
```

### 3.3 Verification

- `cargo test -p maho-import --lib` → 140/140 pass (no API change)
- Zen E2E (12.7k history): events emit progressively (observe via JS listener timestamps)
- Arc E2E (≤300 entries): unchanged behavior

**Edge cases handled**:
- Cancellation: cancel flag check is in the `recv()` loop; break exits, joins worker.
- Worker panic: `tx` drops → `rx.recv()` returns `Err` → loop exits cleanly.
- Worker spawn fails: `worker = None` → outer thread immediately gets `Err` on `recv`, exits.

### 3.4 Files modified

- `maho/crates/maho-ffi/src/import.rs` (1 function)

---

## 4. Phase 2 — Destination Callbacks (12–18h)

### 4.1 Phase 2A — FFI signature extension (4–6h)

#### 4.1.1 New callback typedefs (`maho/crates/maho-ffi/src/import.rs`)

```rust
pub type MahoImportCookieCallback = unsafe extern "C" fn(
    host: *const c_char,
    name: *const c_char,
    value: *const c_char,
    path: *const c_char,
    expires: i64,
    is_secure: bool,
    is_httponly: bool,
    same_site: i32,
    user_data: *mut c_void,
);

pub type MahoImportAutofillCallback = unsafe extern "C" fn(
    field_name: *const c_char,
    value: *const c_char,
    times_used: i32,
    first_used: i64,
    last_used: i64,
    user_data: *mut c_void,
);

pub type MahoImportFaviconCallback = unsafe extern "C" fn(
    url: *const c_char,
    png_bytes: *const u8,
    png_len: usize,
    user_data: *mut c_void,
);
```

#### 4.1.2 `maho_import_orchestrator_start` extended

```rust
pub unsafe extern "C" fn maho_import_orchestrator_start(
    core_ptr: *mut MahoCore,
    browser_json: *const c_char,
    items_bitmask: u32,
    essentials_json: *const c_char,
    progress_cb: MahoImportProgressCallback,
    cookie_cb: Option<MahoImportCookieCallback>,        // NEW
    autofill_cb: Option<MahoImportAutofillCallback>,    // NEW
    favicon_cb: Option<MahoImportFaviconCallback>,      // NEW
    user_data: *mut c_void,
) -> *mut MahoImportSession
```

**Impact**: this is an FFI-breaking signature change. The C++ bridge call site must be updated atomically. (No other callers — bridge is the only consumer.)

#### 4.1.3 `MahoCoreDestination` extended (`maho-ffi/src/import_destination.rs`)

```rust
pub struct MahoCoreDestination {
    core_ptr: *mut MahoCore,
    cookie_cb: Option<MahoImportCookieCallback>,
    autofill_cb: Option<MahoImportAutofillCallback>,
    favicon_cb: Option<MahoImportFaviconCallback>,
    user_data: *mut c_void,
}

unsafe impl Send for MahoCoreDestination {}
unsafe impl Sync for MahoCoreDestination {}

impl MahoCoreDestination {
    pub fn new_with_callbacks(
        core_ptr: *mut MahoCore,
        cookie_cb: Option<MahoImportCookieCallback>,
        autofill_cb: Option<MahoImportAutofillCallback>,
        favicon_cb: Option<MahoImportFaviconCallback>,
        user_data: *mut c_void,
    ) -> Self { /* … */ }
}
```

Each stubbed method becomes:

```rust
fn add_cookie(&self, host: &str, name: &str, value: &str, path: &str,
              expires: i64, is_secure: bool, is_httponly: bool, same_site: i32) -> bool {
    let Some(cb) = self.cookie_cb else { return false; };
    let (Some(hc), Some(nc), Some(vc), Some(pc)) = (
        CString::new(host).ok(),
        CString::new(name).ok(),
        CString::new(value).ok(),
        CString::new(path).ok(),
    ) else { return false; };
    unsafe {
        cb(hc.as_ptr(), nc.as_ptr(), vc.as_ptr(), pc.as_ptr(),
           expires, is_secure, is_httponly, same_site, self.user_data);
    }
    true
}
```

Equivalent shapes for `add_autofill` and `add_favicon`.

#### 4.1.4 Verification

- `cargo build -p maho-ffi`
- `cargo test -p maho-import --lib` → 140/140 (no test code changes)
- `nm -gU libmaho_ffi.a | grep -E '_maho_import_' | sort -u | wc -l` → still **5**

#### 4.1.5 Files modified (Phase 2A)

- `maho/crates/maho-ffi/src/import.rs`
- `maho/crates/maho-ffi/src/import_destination.rs`

### 4.2 Phase 2B — C++ bridge thunks + Chromium service routing (6–10h)

#### 4.2.1 `MahoImportSessionBridge.h` additions

```cpp
class MahoImportSessionBridge {
 public:
  MahoImportSessionBridge(
      base::WeakPtr<MahoWelcomePageHandler> handler,
      scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
      Profile* profile);  // NEW

  // … existing …

 private:
  // Captured at construction
  raw_ptr<Profile> profile_;
  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;

  // Rust-side static thunks (invoked on orchestrator thread)
  static void OnCookieFromRust(const char* host, const char* name,
                               const char* value, const char* path,
                               int64_t expires, bool is_secure, bool is_httponly,
                               int32_t same_site, void* user_data);
  static void OnAutofillFromRust(const char* field_name, const char* value,
                                 int32_t times_used, int64_t first_used,
                                 int64_t last_used, void* user_data);
  static void OnFaviconFromRust(const char* url, const uint8_t* png_bytes,
                                size_t png_len, void* user_data);

  // UI-thread handlers (called via PostTask)
  void HandleCookie(std::string host, std::string name, std::string value,
                    std::string path, int64_t expires, bool is_secure,
                    bool is_httponly, int32_t same_site);
  void HandleAutofill(std::u16string field_name, std::u16string value,
                      int32_t times_used, int64_t first_used, int64_t last_used);
  void HandleFavicon(GURL url, std::vector<unsigned char> png_bytes);

  base::WeakPtrFactory<MahoImportSessionBridge> weak_factory_{this};
};
```

#### 4.2.2 `MahoImportSessionBridge.cc` — thunk pattern (example: cookies)

```cpp
// static
void MahoImportSessionBridge::OnCookieFromRust(
    const char* host, const char* name, const char* value, const char* path,
    int64_t expires, bool is_secure, bool is_httponly, int32_t same_site,
    void* user_data) {
  auto* self = static_cast<MahoImportSessionBridge*>(user_data);
  // Copy strings while still on Rust thread (Rust owns buffers transiently)
  std::string host_s(host ? host : "");
  std::string name_s(name ? name : "");
  std::string value_s(value ? value : "");
  std::string path_s(path ? path : "");
  self->ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoImportSessionBridge::HandleCookie,
                     self->weak_factory_.GetWeakPtr(),
                     std::move(host_s), std::move(name_s),
                     std::move(value_s), std::move(path_s),
                     expires, is_secure, is_httponly, same_site));
}

void MahoImportSessionBridge::HandleCookie(/* fields */) {
  if (!profile_) return;
  GURL source_url("https://" + host);
  auto cookie = net::CanonicalCookie::CreateSanitizedCookie(
      source_url, name, value, host, path,
      /*creation*/ base::Time::Now(),
      /*expiration*/ base::Time::FromTimeT(expires),
      /*last_access*/ base::Time::Now(),
      is_secure, is_httponly,
      static_cast<net::CookieSameSite>(same_site),
      net::COOKIE_PRIORITY_DEFAULT,
      /*partition_key*/ std::nullopt,
      /*status*/ nullptr);
  if (!cookie) return;

  auto* cm = profile_->GetDefaultStoragePartition()
                 ->GetCookieManagerForBrowserProcess();
  cm->SetCanonicalCookie(*cookie, source_url,
                         net::CookieOptions::MakeAllInclusive(),
                         base::DoNothing());
}
```

#### 4.2.3 Autofill handler

```cpp
void MahoImportSessionBridge::HandleAutofill(
    std::u16string field_name, std::u16string value,
    int32_t times_used, int64_t first_used, int64_t last_used) {
  if (!profile_) return;
  scoped_refptr<autofill::AutofillWebDataService> svc =
      WebDataServiceFactory::GetAutofillWebDataForProfile(
          profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!svc) return;

  autofill::FormFieldData f;
  f.set_name(field_name);
  f.set_value(value);
  std::vector<autofill::FormFieldData> fields{f};
  svc->AddFormFields(fields);
}
```

If `AutofillWebDataService::AddFormFields` is not present in Chromium 149, fallback to direct `AutofillTable::AddFormFieldValues` via `WebDatabase` access (DB sequence task, not UI).

#### 4.2.4 Favicon handler

```cpp
void MahoImportSessionBridge::HandleFavicon(
    GURL url, std::vector<unsigned char> png_bytes) {
  if (!profile_ || !url.is_valid()) return;
  SkBitmap bitmap;
  if (!gfx::PNGCodec::Decode(png_bytes.data(), png_bytes.size(), &bitmap)) {
    return;
  }
  gfx::Image image = gfx::Image::CreateFrom1xBitmap(bitmap);

  favicon::FaviconService* svc = FaviconServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!svc) return;

  svc->SetFavicons(/*page_urls=*/{url}, /*icon_url=*/url,
                   favicon_base::IconType::kFavicon, image);
}
```

#### 4.2.5 `MahoImportSessionBridge::Start()` update

The call site now passes 3 thunks + `this` as user_data to `maho_import_orchestrator_start`:

```cpp
session_ = maho_import_orchestrator_start(
    core, browser_json.c_str(), items_bitmask, essentials_json.c_str(),
    &MahoImportSessionBridge::OnProgressFromRust,   // existing
    &MahoImportSessionBridge::OnCookieFromRust,     // NEW
    &MahoImportSessionBridge::OnAutofillFromRust,   // NEW
    &MahoImportSessionBridge::OnFaviconFromRust,    // NEW
    this);
```

#### 4.2.6 `MahoWelcomePageHandler` + `MahoMigrationDialogView` updates

Both bridge construction call sites must pass `profile_` (already accessible) in addition to existing args:

```cpp
import_session_bridge_ = std::make_unique<MahoImportSessionBridge>(
    weak_factory_.GetWeakPtr(),
    base::SequencedTaskRunner::GetCurrentDefault(),
    Profile::FromBrowserContext(web_contents_->GetBrowserContext()));
```

#### 4.2.7 BUILD.gn dependency additions

```gn
deps = [
  # … existing …
  "//chrome/browser/favicon",
  "//chrome/browser/web_data_service_factory",
  "//components/autofill/core/browser",
  "//components/autofill/core/common",
  "//components/favicon/core",
  "//components/favicon_base",
  "//net",
  "//services/network/public/mojom",
  "//ui/gfx/codec",
]
```

Some of these may already be transitively pulled in; verify on build failure.

#### 4.2.8 Files modified (Phase 2B)

- `maho-chromium/browser/importer/maho_import_session_bridge.h`
- `maho-chromium/browser/importer/maho_import_session_bridge.cc`
- `maho-chromium/browser/importer/BUILD.gn`
- `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` (bridge construction args)
- `maho-chromium/browser/ui/views/importer/maho_migration_dialog_view.cc` (bridge construction args)

### 4.3 Phase 2C — E2E verification (2–3h)

#### 4.3.1 Verification matrix

| Browser | Verification |
|---|---|
| Chrome (idx 0, mask 93) | Cookies SQLite (`Default/Cookies`) count > 0; Web Data autofill table > 0; Favicons SQLite > 0; existing history (98) preserved |
| Arc (idx 1, mask 125) | Cookies > 0; Workspace data (8 spaces / 24 folders / 23 with tabs) preserved; existing history (237) preserved |
| Zen (idx 5, mask 119) | Cookies (~378) > 0; Workspaces (3) preserved; History (12.7k) preserved; **streaming events visible during import** |
| Opera | (skipped — empty profile on test machine) |
| Safari | (skipped — FDA permission not granted) |

#### 4.3.2 Streaming-events check

JS-side assertion: at least 3 distinct event timestamps > 100ms apart during Zen import (proves events arrive during, not after).

#### 4.3.3 Regression check

- `cargo test -p maho-import --lib` → 140/140
- Cargo full check: `cargo check --workspace` (catches Cargo.toml-level breakage from parallel session)

#### 4.3.4 ACCEPTANCE.md updates

- #1 (Chrome import all types): ⚠️ → ✅ (after callback verification)
- #4 (Zen workspaces): already ✅ (from prior session in this conversation)
- #13 (per-type progress independent, not batched): ⚠️ → ✅
- "Known Issues" section: drop bullets 1 (destination stubs) and 3 (event batching)

---

## 5. Risk Analysis

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `CookieManager::SetCanonicalCookie` signature drift in Chromium 149 | Medium | Medium | Check header on first build failure; signature stable across last 10 releases |
| `AutofillWebDataService::AddFormFields` method missing | Medium | Medium | Fallback to `AutofillTable::AddFormFieldValues` via `WebDatabase` direct access |
| `Profile*` lifetime — destroyed before PostTask fires | Low | High | `weak_factory_` makes handler no-op if bridge destroyed; bridge tied to page-handler lifetime |
| Cookie `expires=0` (session) rejected by Chromium | Low | Low | Chromium accepts; just treats as session cookie |
| PNG decode failure on weird Chrome favicon formats | Low | Low | Skip on decode failure — favicon absence is acceptable |
| BUILD.gn new deps cause circular dependency | Low | Medium | Build immediately; trim deps if cycle |
| Parallel session's `Cargo.toml` dirty state collides | Medium | Low | We modify `src/*.rs` only; no Cargo.toml touch |
| `MahoCoreDestination::Send`/`Sync` constraint violated by callback pointers | Low | Medium | Already marked `unsafe impl Send + Sync`; raw fn pointers are `Send` |
| Streaming fix subtle race (worker spawn fails) | Low | Low | Handle `worker = None` path — outer recv exits immediately |

---

## 6. Timeline Estimate

| Phase | Work | Hours |
|---|---|---|
| 1 | Streaming events fix | 2–3 |
| 2A | FFI typedefs + signature + Rust callback dispatch | 4–6 |
| 2B | C++ bridge thunks + Chromium service handlers | 6–10 |
| 2C | E2E verification + ACCEPTANCE.md update | 2–3 |
| **Total** | | **14–22** |

Worst-case (Chromium API surprises): +1 day.

---

## 7. Files Touched Summary

### Rust (4 files)

- `maho/crates/maho-ffi/src/import.rs` — streaming fix, callback typedefs, signature change
- `maho/crates/maho-ffi/src/import_destination.rs` — struct extension, 3 stub method implementations
- (no other Rust changes — parser/worker layer unchanged)

### C++ (5 files)

- `maho-chromium/browser/importer/maho_import_session_bridge.h`
- `maho-chromium/browser/importer/maho_import_session_bridge.cc`
- `maho-chromium/browser/importer/BUILD.gn`
- `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc`
- `maho-chromium/browser/ui/views/importer/maho_migration_dialog_view.cc`

### Documentation (1 file)

- `maho-chromium/browser/importer/ACCEPTANCE.md` — mark #1, #13 ✅; trim known-issues

### Total: 10 files modified, 0 deleted, 0 created

---

## 8. Acceptance Criteria for This Plan

This plan is "done" when:

- [ ] `cargo test -p maho-import --lib` → 140/140 (unchanged)
- [ ] FFI symbol count remains 5: `nm -gU libmaho_ffi.a | grep -oE '_maho_import_[a-z_]+' | sort -u | wc -l` == 5
- [ ] Chromium build exit 0
- [ ] Zen E2E: events visible during import (not batched), all data types in correct SQLite stores
- [ ] Chrome E2E: cookies SQLite > 0, autofill table > 0, favicons SQLite > 0
- [ ] Arc E2E: workspace data unchanged + cookies > 0
- [ ] ACCEPTANCE.md #1 and #13 marked ✅

---

## 9. Open Questions (resolve before coding)

1. **History/bookmarks/passwords routing change**? Default: NO (maho-core owns these, Maho's UI surfaces them).
2. **Chrome's `services_supported` missing bookmarks bit (2)**? Default: out of scope for this plan; separate fix.
3. **Cookie session policy** (`expires=0`)? Default: pass through, let Chromium decide.
4. **Sync vs async callbacks**? Default: fire-and-forget.

If user disagrees with any default → modify before Phase 2A starts.

---

## 10. Rollback Strategy

- **Phase 1** (streaming fix): revert `src/import.rs` to single-thread `try_iter` pattern. 1 commit.
- **Phase 2A** (Rust callbacks): callbacks are `Option<…>` — passing `None` from C++ side reverts to current `false`-stub behavior without code revert.
- **Phase 2B** (C++ bridges): pass `nullptr` for callback args at `Start()` call site → behavior reverts to today's "data silently dropped". Code stays in place but inactive.
- Each phase is independently revertable without breaking the others.

---

## 11. Status

**STATUS**: Plan-only. No implementation work begun. Awaits user GO.

Triggers to proceed:
- `GO PHASE 1` — start with streaming fix only (2–3h)
- `GO PHASE 2` — start with destination callbacks only (12–18h)
- `GO ALL` — sequential 1 → 2A → 2B → 2C (14–22h)
- `MODIFY` — adjust plan per feedback
- `DEFER` — file plan, no coding now
