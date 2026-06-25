# Browser Import: C++ → Rust Migration Plan

**Created**: 2026-06-16
**Status**: Plan-only. Awaits sequencing & explicit GO from user.
**Owner**: TBD
**Estimate**: Phase 1 = 1 week, Phase 2 = 1 week, Phase 3 = 2-3 weeks
**Driver issue**: Onboarding Arc space import surfaces ownership ambiguity — half logic in C++ (`browser/importer/`), half in Rust (`maho-core` mutation). Current split forces every parser ↔ state hop across FFI, blocks Rust-only unit testing, and adds threading rules (INV-2) that wouldn't exist if parsers also lived on the Rust side.

---

## 1. Goal

Migrate **all browser import logic** (parsing + orchestration) from `maho-chromium/browser/importer/` C++ into a new Rust crate, exposing a **single async FFI entry point** to the Chromium-side WebUI page handler.

**Non-goals**:
- Rewriting the WebUI (`maho_welcome` React app stays as-is)
- Changing the welcome onboarding UX flow
- Touching `maho-core` storage internals beyond what the import path needs
- Migrating `maho_space_profile_bridge` (separate concern)

---

## 2. Motivation

| Problem (today) | Resolved by Rust migration |
|---|---|
| Parser results cross FFI twice (C++ parse → C++ struct → FFI to Rust → maho-core mutate). | Single Rust call: parse → mutate → return progress. |
| Test coverage requires Chromium browser_test harness (slow, flaky, macOS-only for keychain). | `cargo test` per parser; fixture-driven; <1s. |
| `INV-2` (parsers on `ImporterThread`, FFI on UI thread) is a footgun unique to C++ split. | Rust async runtime owns its own scheduling. |
| Adding new browser source = touch 4-5 C++ files + GN + tests. | Add 1 Rust module + register in dispatch table. |
| Arc parser bugs (e.g. `sidebar.plist` schema drift) currently surface as silent FFI no-ops. | Rust `Result<_, ImportError>` propagates structured errors over Mojo. |
| C++ `ImportOrchestrator` reuses Chromium `base::ThreadPool` — couples import to browser-process lifetime. | Rust import runs on its own runtime; clean shutdown semantics. |

**Counter-argument** (preserved for review): the C++ side already has 30 working files with NSS + Keychain integrations. Migration is not free. Phase 3 (NSS/Keychain) is real engineering work, not a port. **This plan accepts that cost in exchange for the architectural payoff.**

---

## 3. Current Architecture (Inventory)

### C++ files (30, all in `maho-chromium/browser/importer/`)

| File | Role | Phase target |
|---|---|---|
| `import_orchestrator.cc/h` | Top-level coordinator. Dispatches to per-type importers. INV-7 owner. | Phase 2 |
| `importer_base.h` | Base interface for `BookmarkImporter`, `HistoryImporter`, etc. | Phase 2 |
| `maho_browser_detector.cc/h` | Enumerates installed browsers + profiles. | Phase 1 |
| `maho_space_ffi_client.cc/h` | FFI bridge to `maho-core` for space/folder/tab CRUD. | Phase 2 (becomes internal Rust calls) |
| `arc_sidebar_parser.cc/h` | Parses Arc `sidebar.plist`. | **Phase 1 (priority — drove this plan)** |
| `chromium_history_parser.cc/h` | Reads Chromium `History` SQLite. | Phase 1 |
| `chromium_bookmark_parser.cc/h` | Reads Chromium `Bookmarks` JSON. | Phase 1 |
| `chromium_password_parser.cc/h` | Reads Chromium `Login Data` SQLite + Keychain decrypt. | **Phase 3 (Keychain)** |
| `chromium_autofill_parser.cc/h` | Reads `Web Data` SQLite. | Phase 1 |
| `cookie_parser.cc/h` | Reads `Cookies` SQLite + AES-GCM `v10/v20` decrypt. | Phase 1 (AES-GCM is straightforward) |
| `favicon_parser.cc/h` | Reads `Favicons` SQLite. | Phase 1 |
| `firefox_bookmark_parser.cc/h` | Reads Firefox `places.sqlite`. | Phase 1 |
| `firefox_history_parser.cc/h` | Same DB, history table. | Phase 1 |
| `firefox_password_parser.cc/h` | Reads `logins.json` + `key4.db`. | **Phase 3 (NSS)** |
| `firefox_cookie_parser.cc/h` | Reads `cookies.sqlite`. | Phase 1 |
| `firefox_autofill_parser.cc/h` | Reads `formhistory.sqlite`. | Phase 1 |
| `nss_decrypt_helper.cc/h` | Loads `libnss3.dylib`, calls `PK11_PBEDecryptedKey`. | **Phase 3 (NSS)** |
| `safari_bookmark_parser.cc/h` | Reads `Bookmarks.plist`. | Phase 1 |
| `safari_history_parser.cc/h` | Reads `History.db`. | Phase 1 |
| `zen_workspaces_parser.cc/h` | Reads Zen workspace JSON. | Phase 1 |
| `importers/bookmark_importer.cc/h` | Type-specific worker; transforms parsed → space/folder/tab via FFI. | Phase 2 |
| `importers/history_importer.cc/h` | Same pattern. | Phase 2 |
| `importers/password_importer.cc/h` | Routes to Chromium- or Firefox- specific decrypt. | Phase 3 |
| `importers/cookie_importer.cc/h` | Inserts into Maho cookie store. | Phase 2 |
| `importers/autofill_importer.cc/h` | Inserts into autofill DB. | Phase 2 |
| `importers/workspace_importer.cc/h` | Maps Zen/Arc workspaces → Maho spaces via FFI. | Phase 2 (this is the Arc path) |
| `importers/favicon_importer.cc/h` | Bulk favicon insert. | Phase 2 |
| `importers/essential_importer.cc/h` | Welcome flow `FavoriteEssentialSites` (no orchestrator path). | Phase 2 |
| `INVARIANTS.md` | INV-1..INV-9. | Updated per phase. |
| `ACCEPTANCE.md` | ~35 Tier-A criteria. | Re-validated per phase. |

### Existing Rust crate to extend
- `maho/crates/maho-core/` — current home of `MahoSpaceFFIClient` callees (`CreateSpace`, `CreateTab`, etc.)

### Proposed new Rust crates
- `maho/crates/maho-import/` — parsers + orchestrator (this plan's main deliverable)
- `maho/crates/maho-import-ffi/` — thin extern "C" surface for the C++ side to call

---

## 4. Target Architecture

### Layer split (post-migration)

```
┌─────────────────────────────────────────────────────────────┐
│  maho_welcome WebUI (TS/React)                              │
│  store.startImport(browser_index, items)                    │
└────────────────────────┬────────────────────────────────────┘
                         │ Mojo IPC
┌────────────────────────▼────────────────────────────────────┐
│  maho_welcome_page_handler.cc (C++)                         │
│  StartImport() → maho_import_start_async(...)               │
│  poll progress channel via base::PostTask                   │
└────────────────────────┬────────────────────────────────────┘
                         │ extern "C" FFI (single entry)
┌────────────────────────▼────────────────────────────────────┐
│  maho-import-ffi (Rust)                                     │
│  pub extern "C" fn maho_import_start(...)                   │
│  pub extern "C" fn maho_import_poll_progress(...)           │
│  pub extern "C" fn maho_import_cancel(...)                  │
└────────────────────────┬────────────────────────────────────┘
                         │ Rust calls
┌────────────────────────▼────────────────────────────────────┐
│  maho-import (Rust)                                         │
│  ├── detect/ — browser+profile enumeration                  │
│  ├── parsers/                                               │
│  │   ├── arc/sidebar.rs                                     │
│  │   ├── chromium/{history,bookmarks,cookies,autofill}.rs   │
│  │   ├── firefox/{places,cookies,formhistory}.rs            │
│  │   ├── safari/{bookmarks,history}.rs                      │
│  │   └── zen/workspaces.rs                                  │
│  ├── decrypt/                                               │
│  │   ├── chromium_v10_v20.rs (AES-GCM, pure Rust)           │
│  │   ├── firefox_nss.rs (Phase 3)                           │
│  │   └── macos_keychain.rs (Phase 3)                        │
│  ├── orchestrator.rs — workflow + progress channel          │
│  └── lib.rs — public API: ImportRequest, ImportResult       │
└────────────────────────┬────────────────────────────────────┘
                         │ direct Rust calls (no FFI)
┌────────────────────────▼────────────────────────────────────┐
│  maho-core (Rust) — existing                                │
│  Spaces, Tabs, Folders, Favorites, History, Cookies LMDB    │
└─────────────────────────────────────────────────────────────┘
```

### FFI surface (final state, end of Phase 3)

```c
// maho-chromium/third_party/maho/maho_import_ffi.h

typedef struct MahoImportHandle MahoImportHandle;

typedef enum MahoImportItemBitmask {
    MAHO_IMPORT_BOOKMARKS  = 1 << 0,
    MAHO_IMPORT_HISTORY    = 1 << 1,
    MAHO_IMPORT_PASSWORDS  = 1 << 2,
    MAHO_IMPORT_COOKIES    = 1 << 3,
    MAHO_IMPORT_AUTOFILL   = 1 << 4,
    MAHO_IMPORT_FAVICONS   = 1 << 5,
    MAHO_IMPORT_WORKSPACES = 1 << 6,
} MahoImportItemBitmask;

typedef enum MahoImportProgressKind {
    MAHO_IMPORT_PROGRESS_UPDATE   = 0,  // statusMessage updated
    MAHO_IMPORT_PROGRESS_COMPLETE = 1,  // success
    MAHO_IMPORT_PROGRESS_ERROR    = 2,  // failure (errorCode + message)
} MahoImportProgressKind;

// Returns array of available browsers (JSON-encoded for simplicity).
// Caller must free via maho_string_free.
const char* maho_import_detect_browsers(void);

// Starts import. Returns opaque handle. Items_bitmask = OR of MahoImportItemBitmask.
// |callback| is invoked from a background thread with each progress event.
// Caller MUST be prepared for callback after maho_import_start returns.
MahoImportHandle* maho_import_start(
    int32_t browser_index,
    uint32_t items_bitmask,
    void (*callback)(MahoImportProgressKind kind,
                     int32_t imported_count,
                     const char* status_message,
                     void* user_data),
    void* user_data);

// Cancels in-flight import. Safe to call from any thread.
void maho_import_cancel(MahoImportHandle* handle);

// Frees handle. Must be called after MAHO_IMPORT_PROGRESS_COMPLETE or _ERROR.
void maho_import_free(MahoImportHandle* handle);
```

**FFI surface size: 4 functions** (down from current ~25 across `maho_space_ffi_client` + `import_orchestrator` indirection).

---

## 5. Phased Migration

### Phase 1 — Parsers (1 week, low risk)

**Scope**: Move all read-only parsers to Rust. C++ orchestrator keeps running, calls Rust to parse, gets `ParsedX` structs back as serialized JSON, continues into existing C++ `importers/*_importer.cc` workers.

**Files to create**:
- `maho/crates/maho-import/Cargo.toml`
- `maho/crates/maho-import/src/lib.rs`
- `maho/crates/maho-import/src/detect.rs`
- `maho/crates/maho-import/src/parsers/arc/sidebar.rs` ← prove out the architecture here first
- `maho/crates/maho-import/src/parsers/chromium/{history,bookmarks,cookies,autofill,favicons}.rs`
- `maho/crates/maho-import/src/parsers/firefox/{places,cookies,formhistory}.rs`
- `maho/crates/maho-import/src/parsers/safari/{bookmarks,history}.rs`
- `maho/crates/maho-import/src/parsers/zen/workspaces.rs`
- `maho/crates/maho-import/src/decrypt/chromium_v10_v20.rs`
- `maho/crates/maho-import-ffi/Cargo.toml`
- `maho/crates/maho-import-ffi/src/lib.rs` — Phase 1 exposes `maho_import_parse_*` only (no orchestrator yet)
- `maho-chromium/browser/importer/maho_import_rust_bridge.cc/h` — C++ shim that calls Rust parse, deserializes JSON

**Files to delete (Phase 1 end)**:
- `arc_sidebar_parser.cc/h`
- `chromium_history_parser.cc/h`, `chromium_bookmark_parser.cc/h`, `chromium_autofill_parser.cc/h`
- `cookie_parser.cc/h`, `favicon_parser.cc/h`
- `firefox_bookmark_parser.cc/h`, `firefox_history_parser.cc/h`, `firefox_cookie_parser.cc/h`, `firefox_autofill_parser.cc/h`
- `safari_bookmark_parser.cc/h`, `safari_history_parser.cc/h`
- `zen_workspaces_parser.cc/h`
- `maho_browser_detector.cc/h`

**Files unchanged**:
- All `importers/*_importer.cc/h` (they still consume parsed structs)
- `import_orchestrator.cc/h`
- `maho_space_ffi_client.cc/h`
- Password-related: `nss_decrypt_helper.cc/h`, `firefox_password_parser.cc/h`, `chromium_password_parser.cc/h` ← Phase 3

**Rust dependencies** (add to `maho/crates/maho-import/Cargo.toml`):
```toml
plist = "1.7"        # Arc sidebar.plist, Safari Bookmarks.plist
rusqlite = { version = "0.31", features = ["bundled"] }  # Chromium/Firefox SQLite reads
serde = { version = "1", features = ["derive"] }
serde_json = "1"
aes-gcm = "0.10"     # Cookie v10/v20 decrypt
base64 = "0.22"
walkdir = "2.5"      # profile enumeration
dirs = "5.0"         # ~/Library/Application Support resolution
thiserror = "1"
```

**Testing strategy**:
- Each parser has `#[test]` with fixtures in `tests/fixtures/{arc,chromium,firefox,safari,zen}/`
- Reuse existing C++ `test_data/` fixtures (they're already extracted plist/SQLite blobs)
- Cross-validate: Rust parser output vs C++ parser output for same fixture (interim test during migration)

**Risk**: Low. Parsers are pure functions over file inputs.

**Acceptance gate** (before Phase 2):
- [ ] All 13 Phase-1 parsers ported with parity tests
- [ ] Existing browser_test suite (`browser/importer/import_orchestrator_browsertest.cc`) green using Rust-backed parsers
- [ ] Welcome onboarding Arc import works end-to-end with real Arc data on a test machine
- [ ] No new INVARIANTS introduced; INV-2 (thread split) still applies because C++ orchestrator still in charge

---

### Phase 2 — Orchestrator (1 week, medium risk)

**Scope**: Move the orchestration layer (worker dispatch, progress reporting, INV-7 single-completion-call enforcement) into Rust. C++ side becomes a thin Mojo→FFI shim.

**Files to create**:
- `maho/crates/maho-import/src/orchestrator.rs` — picks workers from items_bitmask, runs them, aggregates progress
- `maho/crates/maho-import/src/workers/{bookmark,history,cookie,autofill,favicon,workspace,essential}.rs`
- `maho/crates/maho-import/src/progress.rs` — `tokio::sync::mpsc` channel or equivalent
- `maho/crates/maho-import-ffi/src/{progress_callback,lifecycle}.rs`

**Files to delete**:
- `import_orchestrator.cc/h`
- `importer_base.h`
- All of `importers/*_importer.cc/h` (7 files)
- `maho_space_ffi_client.cc/h` ← becomes internal Rust calls into `maho-core`

**Files modified**:
- `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc`
  - `StartImport()` calls `maho_import_start(...)` directly
  - Progress callback bridges via `base::SequencedTaskRunner` to UI thread, fires `OnImportProgress` mojo
- `maho-chromium/browser/importer/BUILD.gn` — strip out C++ orchestrator targets, keep only Phase-3 password files

**Maho-core integration**:
- `maho-import` adds `maho-core` as a path dep in `Cargo.toml`
- Direct `maho_core::CreateSpace(...)` calls (no FFI overhead)
- Threading: Rust controls its own runtime; `maho-core` mutation calls remain main-thread-safe (existing requirement) — `maho-import` posts to maho-core's expected thread context via internal channel

**INVARIANTS rewrite** (new `INVARIANTS-rust.md` in `maho/crates/maho-import/`):
- INV-1 (preserved): No parser returns success with zero items.
- INV-2 (REMOVED): No more cross-thread parser/UI rules; Rust runtime handles it.
- INV-3 (preserved, generalized): Parsers MUST NOT panic; return `Result<_, ImportError>`.
- INV-4 (preserved): No bypassing decrypt prefix checks.
- INV-5 (NEW): Progress callback fires from Rust runtime thread; C++ side MUST `PostTask` to UI before touching browser state.
- INV-6 (preserved): Plaintext key material zeroed via `zeroize::Zeroize` (replaces C++ RAII SecureBuffer).

**Risk**: Medium. Orchestration touches threading + lifecycle. Cancellation semantics must match current C++ behavior (e.g. mid-import skip in welcome flow). Mojo callback bridging is the main complexity.

**Acceptance gate**:
- [ ] All `importers/*_importer.cc` removed
- [ ] Welcome `startImport` Mojo path goes WebUI → page_handler.cc → Rust FFI (single hop)
- [ ] Cancel works (user clicks Skip mid-import)
- [ ] `maho-core` mutation order unchanged (verified via LMDB inspection on test profile)
- [ ] All 35 ACCEPTANCE.md Tier-A criteria still pass

---

### Phase 3 — NSS + Keychain (2-3 weeks, high risk)

**Scope**: The two hard parts — Firefox password decrypt (NSS) and Chromium password decrypt (macOS Keychain).

#### 3a — Chromium passwords (macOS Keychain) — 1 week

**Approach**: Use `security-framework` crate.

**Files to create**:
- `maho/crates/maho-import/src/decrypt/macos_keychain.rs`
- `maho/crates/maho-import/src/parsers/chromium/passwords.rs`

**Files to delete**:
- `chromium_password_parser.cc/h`
- `chromium_password_parser_unittest.cc`
- `importers/password_importer.cc/h` (Chromium branch)

**Cargo additions**:
```toml
[target.'cfg(target_os = "macos")'.dependencies]
security-framework = "2.11"
```

**Risk**: macOS-only. Need to handle:
- Keychain access prompts (user permission UI flow)
- "Arc Safe Storage", "Brave Safe Storage", "Chrome Safe Storage" service name lookups
- Key derivation: PBKDF2-HMAC-SHA1, iterations=1003, salt="saltysalt" (Chromium convention)
- AES-128-CBC decrypt of stored password blobs

**Acceptance**:
- [ ] Real Chrome / Brave / Arc passwords decrypt successfully on test machine
- [ ] Keychain access denied → graceful `ImportError::KeychainDenied` propagation, UI shows error toast
- [ ] No password material logged or written to disk (verify via instrumentation)

#### 3b — Firefox NSS — 1-2 weeks

**Approach decision**: Choose between (a) `bindgen` to `libnss3.dylib` or (b) pure-Rust PBKDF2+3DES.

**Recommendation**: **Pure-Rust** because:
- Avoids macOS dynamic library dispatch complexity (current `nss_decrypt_helper.cc` does dlopen + dlsym ceremony)
- NSS algorithms are stable and documented (PKCS#11 PBE-SHA1-3DES-CBC for older profiles, PBE-SHA256-AES256 for newer)
- Pure-Rust gives reproducible behavior across NSS 3 / NSS 4 versions

**Files to create**:
- `maho/crates/maho-import/src/decrypt/firefox_nss.rs`
- `maho/crates/maho-import/src/parsers/firefox/passwords.rs`
- `maho/crates/maho-import/src/decrypt/asn1.rs` — minimal ASN.1 BER decoder for NSS key blob structure

**Files to delete**:
- `nss_decrypt_helper.cc/h`
- `firefox_password_parser.cc/h`
- `importers/password_importer.cc/h` (Firefox branch)

**Cargo additions**:
```toml
sha1 = "0.10"
sha2 = "0.10"
pbkdf2 = "0.12"
des = "0.8"          # 3DES-CBC for NSS 3
aes = "0.8"          # AES-256-CBC for NSS 4
cbc = "0.1"
hmac = "0.12"
```

**Risk**: HIGH.
- NSS key DB schema (`key4.db` SQLite + `key3.db` Berkeley DB) is undocumented. Rely on Mozilla source code + Brave's existing pure-Rust impl as reference.
- ASN.1 parsing of `metaData` and `nssPrivate` blobs is fiddly.
- Test fixtures with real Firefox profiles (multiple NSS versions) are needed.

**Acceptance**:
- [ ] Real Firefox profiles (NSS 3.x and NSS 4.x) decrypt successfully
- [ ] Wrong master password → `ImportError::FirefoxMasterPasswordRequired` propagates, UI shows password prompt
- [ ] No NSS C library dependency in shipping binary (verified via `otool -L`)

---

## 6. Strangler Pattern (How phases coexist)

During Phase 1 and Phase 2 the import system runs in **mixed mode**:

- C++ orchestrator calls `maho_import_parse_arc(...)` (Rust) for parsing
- Result returned as JSON (serde) → C++ deserializes into existing `ParsedWorkspace` struct
- C++ `WorkspaceImporter` continues using `MahoSpaceFFIClient` (still C++)

This means:
- Each parser can ship independently (deploy Phase 1 in 1-2 PR increments)
- Rollback is just "switch the include from `arc_sidebar_parser.h` to `maho_import_rust_bridge.h`"
- Bisect works (parser-by-parser regression detection)

**Cutover** to Phase 2 happens when:
- All parsers are Rust
- C++ orchestrator becomes thin enough to be worth deleting
- We're confident in Rust runtime threading model

---

## 7. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Rust port misses edge case in Chromium SQLite schema (e.g. corrupted DBs, schema migrations) | Med | Med | Phase 1 cross-validation tests against C++ parsers using shared fixtures; ship behind a flag for 1 release; instrumentation logs both paths in canary. |
| `rusqlite` differs from Chromium's bundled SQLite in obscure pragmas | Low | Low | Pin `rusqlite` to bundled-feature; same upstream SQLite. |
| FFI progress callback bridging causes UI thread starvation | Med | High | Use `base::SequencedTaskRunner::PostTask` with `BindOnce`; rate-limit progress events to ≤10/sec on Rust side. |
| Cancellation race: user clicks Skip while Rust orchestrator is mid-CreateSpace | Med | Med | `tokio::select!` between work and cancel signal; never abort mid-LMDB-write (atomic boundaries). |
| NSS pure-Rust port breaks on older Firefox profiles (NSS < 3.30) | High in Phase 3 | High | Fallback to "skip Firefox passwords with warning" in v1; iterate. Brave's open-source impl as reference. |
| macOS Keychain prompt blocks Maho main thread | Med | High | All Keychain calls on dedicated tokio task; never on Rust main runtime. |
| `maho-import` Rust crate adds 5-10MB to binary (statically linked deps) | Low | Low | Already accept that for `maho-core`; same trade-off. |
| Test infrastructure: no Chromium browser_test for Rust crate | Med | Med | Add Rust integration tests + keep one E2E browser_test that exercises Mojo → FFI → Rust path. |
| Dirty-state collision with current `maho-chromium` working tree (314 modified files) | High | High | **Sequence after current dirty state is resolved.** Do not start migration until tree is clean. |

---

## 8. Acceptance Criteria (overall)

After all 3 phases complete, the following must hold:

### Functional
- [ ] All 7 import types (bookmarks, history, passwords, cookies, autofill, favicons, workspaces) work for Chrome, Brave, Arc, Edge, Vivaldi, Opera, Firefox, Safari, Zen
- [ ] Welcome onboarding Arc import end-to-end success on macOS test profile
- [ ] Welcome `startImport` mojo flow: WebUI → page_handler → Rust FFI → maho-core (single FFI hop)
- [ ] All 35 ACCEPTANCE.md Tier-A criteria pass

### Non-functional
- [ ] No regression in import time (median, p95) measured against pre-migration baseline
- [ ] `cargo test -p maho-import` runs in <10s
- [ ] No NSS dynamic library loaded by shipping binary (`otool -L Maho` clean)
- [ ] Zero new `INVARIANTS.md` items in C++ side; Rust-side `INVARIANTS-rust.md` has full coverage
- [ ] FFI surface area: 4 extern "C" functions (per Section 4)

### Codebase
- [ ] `maho-chromium/browser/importer/` reduced to 0-2 files (Mojo bridge only)
- [ ] All parsing fixtures live in `maho/crates/maho-import/tests/fixtures/`
- [ ] No `INV-2` (cross-thread parser rule) — replaced by Rust runtime ownership

---

## 9. Sequencing & Dependencies

```
[Welcome onboarding work — current sprint, 2026-06-XX]
   ↓
[Resolve maho-chromium dirty state (314 files)]   ← BLOCKER for migration
   ↓
[Phase 1: Parsers]                          1 week
   ↓
[Phase 2: Orchestrator]                     1 week
   ↓
[Phase 3a: Chromium Keychain]               1 week
   ↓
[Phase 3b: Firefox NSS]                     1-2 weeks
   ↓
[Final cleanup + INVARIANTS.md rewrite]     2-3 days
```

**Total wall-clock**: 4-6 weeks (assuming one engineer focused).

**Parallelizable**: Phase 3a and 3b can run in parallel after Phase 2 lands.

---

## 10. Rollback Strategy

Each phase is independently revertable:

- **Phase 1 rollback**: Revert `maho_import_rust_bridge.cc/h` shim to call original C++ parsers (kept in git history). Single commit.
- **Phase 2 rollback**: Restore C++ orchestrator from git. Re-add `BUILD.gn` deps. ~1-day operation.
- **Phase 3 rollback**: Keep C++ NSS / Keychain helpers as fallback path behind a `MahoUseRustImportPasswords` flag. Default flip in v+1 release.

---

## 11. Open Questions (for user decision before starting)

1. **Crate scope**: Single `maho-import` crate or split into `maho-import-{parsers,orchestrator,decrypt}`?
   - **Recommendation**: Single crate, internal modules. Premature splitting adds Cargo overhead without testability gain.

2. **Async runtime**: Tokio or `async-std` or maho-core's existing runtime?
   - **Recommendation**: Reuse maho-core's runtime if it has one; otherwise add Tokio (most ecosystem-supported).

3. **Cancellation API surface**: Single `cancel(handle)` or per-worker cancel?
   - **Recommendation**: Single. Per-worker cancel adds complexity for a use case (cancel one type but not others) we don't have.

4. **Progress event granularity**: Per-item ("Imported bookmark 47/200") or per-type ("Bookmarks done")?
   - **Recommendation**: Per-type initially. Per-item is a UI win but adds Rust→C++ callback overhead. Revisit after Phase 2 lands.

5. **Phase 3 decrypt**: Pure-Rust NSS or `bindgen` to libnss3?
   - **Recommendation**: Pure-Rust (per Section 5 Phase 3b).

6. **Test fixtures**: Reuse existing `test_data/` or extract fresh?
   - **Recommendation**: Reuse. They're already verified to match real-world data. Move them to `maho/crates/maho-import/tests/fixtures/`.

---

## 12. Out of Scope

Explicitly NOT in this plan:

- Migrating `maho_space_profile_bridge` (different concern; not import-related)
- Rewriting WebUI (`maho_welcome` React app stays)
- Changing onboarding UX flow
- Adding new browser sources (do that in a separate plan after migration is done)
- Changing `maho-core` LMDB schema
- Migrating bookmark/history/cookie storage out of Chromium native (still uses Chromium's `BookmarkModel`, `HistoryService`, `CookieMonster` for non-import write paths)

---

## 13. Status

**STATUS**: Plan-only. No implementation work begun. Awaits explicit user GO + clean working tree.

Triggers to proceed:
- `GO PHASE 1` — start parser migration, beginning with Arc (the issue that drove this plan)
- `GO ALL` — sequential 1 → 2 → 3 (4-6 week commitment)
- `MODIFY` — revise plan based on user feedback
- `DEFER` — file plan, stay focused on welcome onboarding work

---

## 14. Related Documents

- `maho-chromium/browser/importer/INVARIANTS.md` — current C++ invariants
- `maho-chromium/browser/importer/ACCEPTANCE.md` — Tier-A acceptance checklist (~35 items)
- `maho-chromium/browser/importer/AGENTS.md` — current importer subsystem guide
- `.sisyphus/plans/maho-welcome-5-issues-2026-06-14.md` — welcome onboarding plan that surfaced this rearchitecture need
- `maho/AGENTS.md` — Rust workspace conventions
- `maho-chromium/AGENTS.md` — Chromium overlay conventions
