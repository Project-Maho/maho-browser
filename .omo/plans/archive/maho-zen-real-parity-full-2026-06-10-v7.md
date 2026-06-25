# Tier A — Maho Zen Real Parity (v7: Honest Gaps Resolved)

**Provenance**: v7 — direct edit by Sisyphus (Lead) resolving 3 honest gaps the user surfaced after v6: (a) L1-T15 file vs L2-T2 file ambiguity → explicit clarification that maho_workspace_importer.{h,cc} is renamed/moved to importers/workspace_importer.{h,cc} during L2-T2 (same logic, refactored across two layers, NOT two parallel files); (b) MahoMigrationDialog visual parity scope → honest acknowledgment that pixel parity with Firefox MigrationUtils is impossible in Chromium fork, dialog targets "visual flavor parity" using Zen design tokens (color-mix, big-shadow, radius) without claiming visual identity; (c) Skip/Cancel/Close behavior → explicit 9-row behavior matrix covering all dialog edge cases.

**Reference Documents**:
- Failed v1 plan: `.sisyphus/plans/maho-welcome-zen-true-parity-2026-06-10.md`
- Previous Tier A (L0+L1): `.sisyphus/plans/maho-zen-real-parity-tier-a-2026-06-10-v3.md`
- Existing impl: `maho-chromium/browser/importer/`, `maho-chromium/browser/ui/webui/maho_welcome/`, `maho-chromium/browser/resources/maho_welcome/`
- AGENTS.md hierarchy: workspace root + `maho/` + `maho-chromium/`
- Icon system: `docs/icon-system.md`

---

## Out-of-Scope (Deferred to Tier B)

- Mods system (userChrome.css per space)
- Split View
- Glance / floating URL bar
- Compact mode (auto-hide sidebar)
- Container tabs (Firefox-style isolation)
- Custom NTP
- Extension import (kImportExtensions constant defined in L0 but importer deferred)

---

## Existing-Implementation TRIAGE Table

| File | Verdict | Reason |
|------|---------|--------|
| `nss_decrypt_helper.{h,cc}` | REFACTOR-IN-PLACE | Double-strip, SHA-384, PII leak |
| `cookie_parser.{h,cc}` | REFACTOR-IN-PLACE | Ciphertext leak, no v10/v20 prefix, no copy-to-temp |
| `chromium_password_parser.{h,cc}` | REFACTOR-IN-PLACE | sql tag, no sequence check |
| `firefox_password_parser.{h,cc}` | REFACTOR-IN-PLACE | Wrong parser used for Zen, variant routing broken |
| `zen_workspaces_parser.{h,cc}` | KEEP + TEST | Silent error, folder hierarchy dropped |
| `arc_sidebar_parser.{h,cc}` | KEEP + TEST | URL validation missing |
| `mozlz4_decompressor.{h,cc}` | KEEP + TEST | No magic check, no size cap |
| `chromium_history_parser.{h,cc}` | KEEP + TEST | Timestamp edge case |
| `firefox_history_parser.{h,cc}` | KEEP + TEST | PRTime conversion |
| `chromium_autofill_parser.{h,cc}` | KEEP + TEST | Schema version, sql tag |
| `safari_bookmark_parser.{h,cc}` | KEEP + TEST | CFRef leak, recursion depth |
| `favicon_parser.{h,cc}` | KEEP + TEST | No size validation, no copy-to-temp |
| `maho_bookmark_importer.{h,cc}` | REFACTOR-IN-PLACE | No dedup, no sequence check |
| `maho_browser_detector.{h,cc}` | KEEP + TEST | Zen path, multi-profile |
| `maho_workspace_importer.{h,cc}` | REFACTOR-IN-PLACE | Folder hierarchy dropped, no sequence check |
| `maho_welcome_page_handler.{h,cc}` | REWRITE | Fat handler, race conditions, 3-method dance |
| `maho_welcome_ui.{h,cc}` | KEEP | Thin WebUI host, no logic bugs |
| `resources/maho_welcome/store.ts` | REWRITE | Manual constants, no Mojo-generated types |
| `resources/maho_welcome/app.ts` | REWRITE | Missing screens, no per-type progress |
| `resources/maho_welcome/import-data.tsx` | REWRITE | Hardcoded flags, no workspace checkbox |

---

## Layer 0: Protocol Foundation

### L0-T1: Mojo IDL Import Constants
- **Deliverable**: `maho-chromium/components/maho_importer/maho_importer_constants.mojom` with `const uint32 kImportBookmarks = 0x01`, `kImportPasswords = 0x02`, `kImportHistory = 0x04`, `kImportCookies = 0x08`, `kImportAutofill = 0x10`, `kImportWorkspaces = 0x20`, `kImportSidebar = 0x40`, `kImportExtensions = 0x80` — single source generating TS + C++ bindings. Bit-6 = `kImportWorkspaces` (NOT `kImportSidebar`), fixing v1 collision.
- **Files**: `maho-chromium/components/maho_importer/maho_importer_constants.mojom`, `maho-chromium/components/maho_importer/BUILD.gn`
- **Success criteria**: `autoninja -C out/Default maho_importer_mojo_bindings` succeeds; generated `.h` and `.ts` both export identical numeric values; no bit collisions
- **Verification**: `autoninja -C out/Default maho_importer_mojo_bindings && grep -r 'kImportBookmarks' chromium/src/gen/maho/components/`

### L0-T2: INVARIANTS.md + Enforcement
- **Deliverable**: `maho-chromium/browser/importer/INVARIANTS.md` enumerating:
  - INV-1: All NSS decrypt calls wrapped in `DCHECK(!content.empty())` pre-strip
  - INV-2: Threading — importer parsers run on `ImporterThread`; bridge/orchestrator dispatch posts to UI
  - INV-3: `MahoSpaceProfileBridge` callers must hold `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)`
  - INV-4: No parser returns success with zero items (empty-result = error)
  - INV-5: Cookie parser MUST check 3-byte prefix (`v10`/`v20`) before attempting decrypt
  - INV-6: Test gate — `test_data/` fixtures required for build
  - INV-7: `ImportOrchestrator` is the ONLY caller of `OnImportProgress(complete=true)`
  - INV-8: FFI calls to `maho_core_handle_event` MUST be on UI thread
  - INV-9: Plaintext key material zeroed after use via RAII SecureBuffer or `memset_s`
- **Files**: `maho-chromium/browser/importer/INVARIANTS.md`
- **Success criteria**: Each invariant has a corresponding grep-able DCHECK/LOG pattern in source
- **Verification**: `grep -rn 'DCHECK_CALLED_ON_VALID_SEQUENCE' maho-chromium/browser/importer/ | wc -l`

### L0-T3: MahoSpaceProfileBridge Consumer Enumeration
- **Deliverable**: Documented list of all `bridge->` callsites in `maho-chromium/browser/` — classified as SIDEBAR / AI / BOOST / IMPORTER with file:line references appended to INVARIANTS.md §Consumers
- **Files**: `maho-chromium/browser/importer/INVARIANTS.md` (append)
- **Success criteria**: Every `bridge->` callsite in the tree has a DCHECK_CALLED_ON_VALID_SEQUENCE guard
- **Verification**: `grep -rn 'bridge->' maho-chromium/browser/ | grep -v '_test\.' | grep -v INVARIANTS`

### L0-T4: Synthetic Test Fixture Set
- **Deliverable**: Binary/hex fixture files under `maho-chromium/browser/importer/test_data/synthetic/`:
  - `nss_3des_cbc.bin` — PKCS#12 → 3DES-CBC encrypted blob
  - `nss_aes256_cbc_sha256.bin` — AES-256-CBC with SHA-256 HMAC
  - `nss_aes256_cbc_sha384.bin` — AES-256-CBC with SHA-384 HMAC (variant C)
  - `nss_aes256_gcm.bin` — AES-256-GCM blob
  - `oscrypt_v10.bin` — Chromium OSCrypt v10 envelope
  - `cookie_v10_prefix.bin` — cookie blob with `v10` 3-byte prefix
  - `cookie_v20_prefix.bin` — cookie blob with `v20` 3-byte prefix (skip vector)
  - `golden_hashes.json` — SHA-256 of each fixture for regression gate
- **Files**: `maho-chromium/browser/importer/test_data/synthetic/*`, `maho-chromium/browser/importer/test_data/golden_hashes.json`
- **Success criteria**: `verify_golden.py` validates all fixtures match golden hashes
- **Verification**: `python3 maho-chromium/browser/importer/test_data/verify_golden.py`

### L0-T5: Real-Fixture Extraction Tooling
- **Deliverable**: `maho-chromium/browser/importer/test_data/extract_fixtures.py` — reads dev-machine Chrome, Firefox, Zen profiles; extracts encrypted blobs; strips PII (replaces usernames/URLs with synthetic); outputs to `test_data/real/`
- **Files**: `maho-chromium/browser/importer/test_data/extract_fixtures.py`
- **Success criteria**: Script runs without error on macOS dev machine; produces ≥1 fixture per browser type; no PII in output
- **Verification**: `python3 maho-chromium/browser/importer/test_data/extract_fixtures.py --dry-run`

### L0-T6: Codified Test Gate
- **Deliverable**: GN `action` target in `maho-chromium/browser/importer/BUILD.gn` that runs `verify_golden.py` and fails if `test_data/synthetic/` is missing or hash mismatch. Wired as dep of `unit_tests` importer test targets.
- **Files**: `maho-chromium/browser/importer/BUILD.gn`, `maho-chromium/browser/importer/test_data/verify_golden.py`
- **Success criteria**: `autoninja -C out/Default importer_test_gate` fails if any fixture deleted; passes with full set
- **Verification**: `rm chromium/src/maho/browser/importer/test_data/synthetic/nss_3des_cbc.bin && autoninja -C out/Default importer_test_gate; echo $?` (expect nonzero)

### L0-T7: Golden-Hash Assertion Harness (C++)
- **Deliverable**: `importer_fixture_test.cc` — GTest that loads each fixture from `test_data/synthetic/`, computes SHA-256, asserts against `golden_hashes.json`.
- **Files**: `maho-chromium/browser/importer/importer_fixture_test.cc`
- **Success criteria**: `unit_tests --gtest_filter='ImporterFixtureTest.*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ImporterFixtureTest.*'`

---

## Layer 1: Parser Refactor + Tests

### L1-T1: nss_decrypt_helper.{h,cc}
- **TRIAGE**: REFACTOR-IN-PLACE
- **Bugs to fix**:
  1. Double-strip bug: `DecryptNSSBlob()` strips ASN.1 header twice when input already has OID parsed — add length guard before second strip
  2. Variant C SHA-384: Use `kCCPRFHmacAlgSHA384` (CommonCrypto on macOS ≥10.15); v1 falsely fell through to SHA-256
  3. Empty-input DCHECK: `DCHECK(!encrypted_data.empty())` at entry (INV-1)
  4. Plaintext key zeroing: RAII SecureBuffer wraps decrypted key material; `memset_s` on scope exit
  5. PII in logs: Remove hostname from `LOG(WARNING)` messages
- **Test deliverable**: `nss_decrypt_helper_unittest.cc` — parameterized over 4 synthetic NSS fixtures; asserts known plaintext; asserts empty-input returns error; asserts SHA-384 variant produces correct output
- **Success criteria**: `unit_tests --gtest_filter='NSSDecryptHelper*'` passes all variants + error case
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='NSSDecryptHelper*'`

### L1-T2: cookie_parser.{h,cc}
- **TRIAGE**: REFACTOR-IN-PLACE
- **Bugs to fix**:
  1. Ciphertext-as-plaintext: Add explicit decrypt-result check; return error on failure
  2. v10/v20 prefix detection: Read first 3 bytes; `v10` → decrypt via `OSCryptKeyProvider`; `v20` → skip row with warning
  3. NULL value crash: Null-check on `sqlite3_column_blob` return before memcpy
  4. Copy-to-temp: Copy Cookies DB to temp before opening (avoid WAL lock on live browser)
- **Test deliverable**: `cookie_parser_unittest.cc` — valid v10 decrypt, v20 skip-with-warning, corrupt blob error, NULL column handling
- **Success criteria**: `unit_tests --gtest_filter='CookieParser*'` all green; no ciphertext leaks
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='CookieParser*'`

### L1-T3: chromium_password_parser.{h,cc}
- **TRIAGE**: REFACTOR-IN-PLACE
- **Bugs to fix**:
  1. sql::Database tag: Use `"MahoImporterChromiumPasswords"` not empty string
  2. OSCrypt key provider integration: Decrypt via shared `OSCryptKeyProvider` interface
  3. Sequence check: `DCHECK_CALLED_ON_VALID_SEQUENCE` at `Parse()` entry
  4. Plaintext key zeroing: RAII SecureBuffer for decrypted passwords
  5. PII in logs: Remove hostnames from WARNING messages
- **Test deliverable**: `chromium_password_parser_unittest.cc` — valid v10 decrypt, wrong-key error, empty DB returns empty vec
- **Success criteria**: `unit_tests --gtest_filter='ChromiumPasswordParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ChromiumPasswordParser*'`

### L1-T4: firefox_password_parser.{h,cc}
- **TRIAGE**: REFACTOR-IN-PLACE
- **Bugs to fix**:
  1. NSS variant routing: Detect OID in encrypted blob header → dispatch to correct NSSDecryptHelper variant
  2. key4.db vs key3.db: Handle both; error clearly on key3.db with unsupported message
  3. PII in logs: Drop hostnames from all log lines
- **Test deliverable**: `firefox_password_parser_unittest.cc` — parameterized over 4 NSS variants; key3.db returns descriptive error
- **Success criteria**: `unit_tests --gtest_filter='FirefoxPasswordParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='FirefoxPasswordParser*'`

### L1-T5: zen_workspaces_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. JSON parse error propagation: Replace silent `return {}` with `LOG(ERROR)` + structured error
  2. Container ID validation: Validate `userContextId` is positive integer
  3. Folder hierarchy: Parse nested bookmark folders within workspace up to 5 levels; expose in result struct
- **Test deliverable**: `zen_workspaces_parser_unittest.cc` — valid JSON, malformed JSON, missing fields, negative userContextId, nested folders preserved
- **Success criteria**: `unit_tests --gtest_filter='ZenWorkspacesParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ZenWorkspacesParser*'`

### L1-T6: arc_sidebar_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Empty sidebar graceful handling: Return empty vector, not error
  2. URL validation: Reject non-http(s) URLs in sidebar items
- **Test deliverable**: `arc_sidebar_parser_unittest.cc` — valid sidebar, empty sidebar, invalid URLs filtered
- **Success criteria**: `unit_tests --gtest_filter='ArcSidebarParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ArcSidebarParser*'`

### L1-T7: mozlz4_decompressor.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Magic header check: Validate `mozLz40\0` 8-byte magic before decompress
  2. Output size cap: Reject decompressed output > 256MB
- **Test deliverable**: `mozlz4_decompressor_unittest.cc` — valid decompress, wrong magic, oversized rejection
- **Success criteria**: `unit_tests --gtest_filter='MozLz4Decompressor*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MozLz4Decompressor*'`

### L1-T8: chromium_history_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Timestamp conversion: Chrome epoch (1601) → Unix epoch — verify DST boundaries
  2. Duplicate URL dedup: Deduplicate by URL, keep latest visit_time
- **Test deliverable**: `chromium_history_parser_unittest.cc` — timestamp fidelity, dedup, empty DB
- **Success criteria**: `unit_tests --gtest_filter='ChromiumHistoryParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ChromiumHistoryParser*'`

### L1-T9: firefox_history_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. mozlz4 integration: Places DB may be compressed in recovery — detect and decompress
  2. PRTime conversion: Firefox PRTime (μs since epoch) → base::Time
- **Test deliverable**: `firefox_history_parser_unittest.cc` — normal places.sqlite, compressed variant, timestamp accuracy
- **Success criteria**: `unit_tests --gtest_filter='FirefoxHistoryParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='FirefoxHistoryParser*'`

### L1-T10: chromium_autofill_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Schema version check: Handle both old `autofill` table and new `local_addresses`/`local_credit_cards`
  2. sql::Database tag: Use `"MahoImporterChromiumAutofill"`
- **Test deliverable**: `chromium_autofill_parser_unittest.cc` — old schema, new schema, empty DB
- **Success criteria**: `unit_tests --gtest_filter='ChromiumAutofillParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ChromiumAutofillParser*'`

### L1-T11: safari_bookmark_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Binary plist handling: Fix CFRef leak in error path
  2. Nested folder recursion depth: Cap at 32 levels
  3. FDA error distinction: Return `kPermissionDenied` vs `kFileNotFound` (not conflated)
- **Test deliverable**: `safari_bookmark_parser_unittest.cc` — valid plist, deep nesting rejected, corrupt plist, ENOENT vs EPERM error codes
- **Success criteria**: `unit_tests --gtest_filter='SafariBookmarkParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='SafariBookmarkParser*'`

### L1-T12: favicon_parser.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Image size validation: Reject favicons > 1MB or dimensions > 256×256
  2. MIME sniffing: Validate content matches expected image format
  3. Copy-to-temp: Copy Favicons DB before opening
- **Test deliverable**: `favicon_parser_unittest.cc` — valid PNG, oversized rejection, format mismatch
- **Success criteria**: `unit_tests --gtest_filter='FaviconParser*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='FaviconParser*'`

### L1-T13: maho_bookmark_importer.{h,cc}
- **TRIAGE**: REFACTOR-IN-PLACE
- **Bugs to fix**:
  1. Bridge sequence check: `DCHECK_CALLED_ON_VALID_SEQUENCE` on all public methods
  2. Duplicate detection: Deduplicate by (URL + parent_folder) tuple
  3. Progress callback: Post progress to UI thread, not inline on importer thread
  4. Workspace-aware skip: When `WorkspaceImporter` is active for Zen/Arc, skip standalone bookmark file import (prevents double-import)
- **Test deliverable**: `maho_bookmark_importer_unittest.cc` — dedup, progress ordering, empty input, workspace-skip
- **Success criteria**: `unit_tests --gtest_filter='MahoBookmarkImporter*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoBookmarkImporter*'`

### L1-T14: maho_browser_detector.{h,cc}
- **TRIAGE**: KEEP + TEST
- **Bugs to fix**:
  1. Zen detection path: `~/Library/Application Support/zen/` on macOS, `~/.zen/` on Linux
  2. Version parsing: Handle non-semver (Zen date-based) gracefully
  3. Multi-profile enumeration: Return ALL Chrome profiles (not just Default)
- **Test deliverable**: `maho_browser_detector_unittest.cc` — detect Chrome/Firefox/Zen/Arc/Safari; missing browsers; version edge cases; multi-profile
- **Success criteria**: `unit_tests --gtest_filter='MahoBrowserDetector*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoBrowserDetector*'`

### L1-T15: maho_workspace_importer.{h,cc} → renamed to workspace_importer.{h,cc} in L2-T2
- **TRIAGE**: REFACTOR-IN-PLACE → eventually MOVE to `importer/importers/workspace_importer.{h,cc}` as part of L2-T2
- **Scope clarification**: This Layer 1 task fixes the **parsing/mapping logic bugs** in the existing v1 file. Layer 2's L2-T2 then refactors this same file to inherit from `ImporterBase` and renames it to `workspace_importer.{h,cc}` under `importers/`. They are **NOT two separate files** — same logic, same class (renamed), refactored across two layers. L1-T15 fixes correctness bugs; L2-T2 fits it into the orchestrator pattern.
- **Bugs to fix in L1-T15** (correctness only, no architectural change yet):
  1. Bridge sequence check: `DCHECK_CALLED_ON_VALID_SEQUENCE`
  2. Zen workspace → Maho Space mapping: Validate color/icon fields; fallback to defaults
  3. Arc space import: Map Arc space structure to Maho equivalents
  4. Folder hierarchy: Preserve nested folders up to 5 levels from parsed workspace data
- **Test deliverable**: `maho_workspace_importer_unittest.cc` — Zen import, Arc import, missing fields use defaults, 5-level folder nesting preserved, bridge thread assertion
- **Success criteria**: `unit_tests --gtest_filter='MahoWorkspaceImporter*'` passes; L2-T2 absorbs this file at the import-orchestrator integration step
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoWorkspaceImporter*'`

---

## Layer 2: Orchestration Layer Rewrite

### L2-T1: ImportOrchestrator (KeyedService)
- **Deliverable**: `maho-chromium/browser/importer/import_orchestrator.{h,cc}` — UI-thread-owned KeyedService. `DCHECK_CALLED_ON_VALID_SEQUENCE` on every public method. Responsibilities:
  - Accepts bitmask of `kImport*` flags + source browser profile path
  - Instantiates only the `Importer<T>` workers matching active bits (gates by bitmask)
  - Tracks per-importer completion count
  - Fires `OnImportProgress(type, items_imported, error)` per-type to Mojo observer
  - ONLY caller of `OnImportProgress(complete=true)` when ALL importers done (INV-7)
  - Cancellation support: `CancelImport()` signals all active workers
- **Files**: `maho-chromium/browser/importer/import_orchestrator.h`, `maho-chromium/browser/importer/import_orchestrator.cc`, `maho-chromium/browser/importer/import_orchestrator_unittest.cc`
- **Test**: Mock 3 importers (2 succeed, 1 fails) — assert `complete=true` fires once after all 3 report; assert cancellation stops pending; assert bitmask filtering
- **Success criteria**: `unit_tests --gtest_filter='ImportOrchestrator*'` passes; no `complete=true` before all done
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ImportOrchestrator*'`

### L2-T2: ImporterBase + Per-Type Workers
- **Deliverable**: `maho-chromium/browser/importer/importer_base.h` (abstract) + concrete per-data-type implementations under `maho-chromium/browser/importer/importers/`:
  - `bookmark_importer.{h,cc}` — calls BookmarkModel::AddURL; workspace-aware skip logic
  - `history_importer.{h,cc}` — calls HistoryService::AddPagesWithDetails
  - `password_importer.{h,cc}` — calls PasswordStoreInterface::AddLogin
  - `autofill_importer.{h,cc}` — calls AutofillTable::AddFormFieldValues
  - `favicon_importer.{h,cc}` — calls FaviconService::SetFavicons
  - `cookie_importer.{h,cc}` — calls CookieManager::SetCanonicalCookie via StoragePartition
  - `workspace_importer.{h,cc}` — **MOVED from `browser/importer/maho_workspace_importer.{h,cc}` (L1-T15 file)**, refactored to inherit from `ImporterBase`. Calls `MahoSpaceFFIClient` (L2-T3) for space/tab/folder creation. **Same logic as L1-T15, NOT a parallel new class.** Old file path deleted; new path is `importers/workspace_importer.{h,cc}`.
  - `essential_importer.{h,cc}` — calls MahoSpaceFFIClient::FavoriteTab for essential/pinned sites
  - Each worker: runs parsing on ImporterThread, posts results to UI thread, reports completion to orchestrator.
  - BookmarkImporter: skips bookmark file when WorkspaceImporter is active for Zen/Arc (prevents duplicate import).
- **File rename note**: Layer 1's `maho_workspace_importer.{h,cc}` ⇒ Layer 2's `importers/workspace_importer.{h,cc}`. The Layer-1 file path is removed; all callers (page handler, BUILD.gn) updated to new path. Test file similarly renamed.
- **Files**: `maho-chromium/browser/importer/importer_base.h`, `maho-chromium/browser/importer/importers/*.{h,cc}` (8 worker files), `maho-chromium/browser/importer/BUILD.gn` (update sources list, remove old path, add new paths)
- **Test**: Per-worker unit test with mock store. BookmarkImporter skip-when-workspace test. Workspace_importer unit test continues to assert the same Zen/Arc mapping behavior from L1-T15 (regression-safe rename).
- **Success criteria**: Each importer passes its unit test; bookmark dedup verified; old `maho_workspace_importer.{h,cc}` path no longer exists in tree
- **Verification**: `test ! -f maho-chromium/browser/importer/maho_workspace_importer.cc && autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='*Importer*'`

### L2-T3: MahoSpaceFFIClient Wrapper
- **Deliverable**: `maho-chromium/browser/importer/maho_space_ffi_client.{h,cc}` — single C++ caller of `maho_core_handle_event` for space mutations. Contains ALL FFI quirks:
  - Raw vs JSON-quoted `space_id`
  - camelCase for `update_space_config.changes`
  - `char*` lifetime via `maho_string_free`
  - UI-thread DCHECK on every method
  - Methods: `CreateSpace(req) → expected<id, err>`, `CreateTab(space_id, url, title)`, `PinTab(tab_id)`, `FavoriteTab(tab_id)`, `CreateFolder(space_id, parent_id, name)`, `MoveTabToFolder(tab_id, folder_id)`, `UpdateSpaceConfig(space_id, changes_json)`, `ActivateSpace(id)`
  - Folder hierarchy preserved up to 5 levels (Zen limit)
- **Files**: `maho-chromium/browser/importer/maho_space_ffi_client.h`, `maho-chromium/browser/importer/maho_space_ffi_client.cc`, `maho-chromium/browser/importer/maho_space_ffi_client_unittest.cc`
- **Test**: Mock `maho_core_handle_event` → verify correct JSON payloads sent for each method; verify 5-level folder creation; verify `maho_string_free` called; verify UI-thread DCHECK fires on wrong thread
- **Success criteria**: `unit_tests --gtest_filter='MahoSpaceFFIClient*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoSpaceFFIClient*'`

### L2-T4: Page Handler Thinning
- **Deliverable**: Rewrite `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.{h,cc}` — becomes Mojo plumbing ONLY:
  - `StartImport(source, bitmask)` → delegates to `ImportOrchestrator`
  - `GetDetectedBrowsers()` → delegates to `MahoBrowserDetector`
  - `FinishOnboarding()` → delegates to orchestrator
  - Browser-type `switch` at importer entry point: dispatches to correct parser set (Chromium vs Firefox/Zen vs Safari vs Arc)
  - NO import logic in page handler body
- **Files**: `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h`, `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc`
- **Test**: Page handler integration test — verify Mojo calls route correctly to orchestrator mock
- **Success criteria**: Page handler has zero parsing/store logic; `unit_tests --gtest_filter='MahoWelcomePageHandler*'` passes
- **Verification**: `grep -n 'HistoryService\|PasswordStore\|BookmarkModel\|CookieManager' maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc | wc -l` (expect 0)

### L2-T5: FavoriteEssentialSites Non-Blocking
- **Deliverable**: Rewrite essential-site favoriting to use `base::PostTaskAndReplyWithResult`:
  - Thread pool: Prepare FFI payload (serialize tab data)
  - UI thread reply: Call `MahoSpaceFFIClient::FavoriteTab` + `bridge->NotifyChanged()`
  - No direct FFI call from thread pool (INV-8)
  - `NotifyChanged` only on UI thread (fixes v1 crash)
- **Files**: `maho-chromium/browser/importer/importers/essential_importer.cc`
- **Test**: Thread-safety test — run `FavoriteEssentialSites` from wrong thread → DCHECK fires in debug; verify no UI jank via mock timing
- **Success criteria**: No DCHECK violations in threaded test; `NotifyChanged` callsite has UI-thread assertion
- **Verification**: `grep -n 'DCHECK_CALLED_ON_VALID_SEQUENCE' maho-chromium/browser/importer/importers/essential_importer.cc`

### L2-T6: FinishOnboarding Single Method
- **Deliverable**: Replace v1's 3-method dance (`CompleteOnboarding` + `OpenMainBrowserWindow` + `CloseWelcomeTab`) with single `FinishOnboarding()` Mojo method:
  1. Mark profile as onboarded (pref write)
  2. Spawn main browser window (`Browser::Create`)
  3. Show welcome toast via `MahoNotificationOverlay::Show()`
  4. Close welcome tab (NOT the browser — just the tab)
  - Ordering guaranteed: steps 2-4 sequential on UI thread
  - No double-close: guard with `is_finishing_` bool, early-return if already finishing
- **Files**: `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc`, `maho-chromium/components/maho_importer/maho_welcome.mojom` (update interface)
- **Test**: Call `FinishOnboarding` twice rapidly → assert no crash, only one main window spawned, only one toast
- **Success criteria**: No double-close in test; welcome tab closes without closing entire browser
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='*FinishOnboarding*'`

### L2-T7: WorkspaceImporter End-to-End Test
- **Deliverable**: `maho-chromium/browser/importer/workspace_import_e2e_test.cc` — integration test:
  - Input: Zen profile fixture with 3 workspaces, 5 nested folders, 12 essentials
  - Assert: `MahoSpaceFFIClient::CreateSpace` called 3 times with correct names
  - Assert: `CreateFolder` called with parent chain preserving 5-level nesting
  - Assert: `FavoriteTab` called 12 times
  - Assert: `UpdateSpaceConfig` called with theme + icon JSON per space
  - Assert: After import, sidebar model shows 3 space dots
- **Files**: `maho-chromium/browser/importer/workspace_import_e2e_test.cc`, `maho-chromium/browser/importer/test_data/synthetic/zen_3_workspaces.json`
- **Success criteria**: `unit_tests --gtest_filter='WorkspaceImportE2E*'` passes
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='WorkspaceImportE2E*'`

---

## Layer 3: Onboarding Integration

### L3-T1: TS Frontend — Mojo-Generated Import Constants
- **Deliverable**: Generate TS import constants from Mojo IDL and use them in the Migration Dialog (NOT the import-data screen):
  - Import `kImportBookmarks`, `kImportPasswords`, `kImportHistory`, `kImportCookies`, `kImportAutofill`, `kImportFavicons`, `kImportWorkspaces`, `kImportSidebar`, `kImportExtensions` from generated Mojo bindings (`.ts`)
  - Delete every manual `IMPORT_*` TS constant in `store.ts`, `import-data.tsx`, and any other welcome resources
  - Per-data-type checkboxes (one per `kImport*` flag) live in **`migration-dialog.tsx`** (L3-T15), NOT `import-data.tsx`
  - For Zen/Arc sources: "Spaces & Tabs" checkbox in the dialog defaults checked when `servicesSupported & kImportWorkspaces`
  - `servicesSupported` bitmask provided by `GetDetectedBrowsers()` Mojo response
- **Files**: `maho-chromium/browser/resources/maho_welcome/types.ts` (re-export from generated Mojo), import sites updated
- **Success criteria**: Zero manual `IMPORT_*` constants in TS; checkbox state derived from Mojo bitmask; typecheck passes
- **Verification**: `grep -rn 'IMPORT_BOOKMARKS\|IMPORT_PASSWORDS\|IMPORT_HISTORY' maho-chromium/browser/resources/maho_welcome/ | grep -v 'generated'` (expect 0 matches)

### L3-T2: React State Machine Rewrite
- **Deliverable**: Rewrite `maho-chromium/browser/resources/maho_welcome/store.ts`:
  - Consume Mojo-generated `ImportDataType` enum directly
  - `DEFAULT_IMPORT_ITEMS` derived from per-browser `servicesSupported` bitmask
  - State machine: `idle → detecting → detected → importing → progress → done | error`
  - No hardcoded defaults — all derived from detection result
  - Progress state: per-type `Map<ImportDataType, 'pending' | 'active' | 'success' | 'error' | 'skipped'>`
- **Files**: `maho-chromium/browser/resources/maho_welcome/store.ts`
- **Success criteria**: `bun run typecheck` passes from `maho-chromium/browser/resources/maho_welcome/`; no `as any` or `@ts-ignore`
- **Verification**: `grep -n 'as any\|@ts-ignore' maho-chromium/browser/resources/maho_welcome/store.ts` (expect 0)

### L3-T3: Welcome Wizard Pages (Zen-Parity 8 Screens)
- **Deliverable**: 8-screen wizard in `maho-chromium/browser/resources/maho_welcome/`:
  1. **Splash** — Logo animation + "Welcome to Maho" + CTA
  2. **Source Select** — Grid of detected browsers with profile picker (multi-profile for Chrome)
  3. **Import Progress** — Per-data-type progress rows (L3-T5 UI)
  4. **Default Browser** — Set Maho as default + skip button
  5. **Search Engine** — Select from Google/DuckDuckGo/Bing/Ecosia
  6. **Essentials** — Mock browser preview with essential site grid; select which to pin
  7. **Theme** — Light/Dark/System + 12 color swatches (L4-T2 wires backend)
  8. **Completion** — Heart pulse animation + "You're all set" + open Maho CTA
  Each page: auto-advances on timeout OR has explicit skip. Navigation arrows for back.
- **Files**: `maho-chromium/browser/resources/maho_welcome/pages/*.tsx` (8 files), `maho-chromium/browser/resources/maho_welcome/app.ts` (router)
- **Success criteria**: All 8 screens render without error; navigation forward/back works; auto-advance timer fires
- **Verification**: `bun run typecheck` + manual launch of `chrome://welcome` in dev build

### L3-T4: Browser Detector — All Chrome Profiles
- **Deliverable**: Enhance `MahoBrowserDetector` result to enumerate ALL Chrome profiles (Default, Profile 1, Profile 2, ...):
  - Surface profile list in `GetDetectedBrowsers()` Mojo response
  - UI shows profile selector dropdown when >1 profile detected
  - Each profile reports its own `servicesSupported` bitmask
- **Files**: `maho-chromium/browser/importer/maho_browser_detector.{h,cc}` (enhance), `maho-chromium/components/maho_importer/maho_welcome.mojom` (add `profile_list` field)
- **Success criteria**: Detector returns 2+ profiles for Chrome install with multiple profiles; Mojo type compiles
- **Verification**: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoBrowserDetector*MultiProfile*'`

### L3-T5: Per-Data-Type Progress UI
- **Deliverable**: Status row component in `maho-chromium/browser/resources/maho_welcome/components/import-progress-row.tsx`:
  - One row per active import type
  - Icons: spinner (active), ✓ (success), ✗ (error), ⊘ (skipped)
  - Wired to Mojo `OnImportProgress` observer — updates per-type as each `Importer<T>` reports
  - `ImportOrchestrator::OnAllImportersDone` is the only place `complete=true` fires → auto-advances to next page
- **Files**: `maho-chromium/browser/resources/maho_welcome/components/import-progress-row.tsx`, `maho-chromium/browser/resources/maho_welcome/pages/import-progress.tsx`
- **Success criteria**: Progress row updates independently per type; page advances only after all done; error row shows retry button
- **Verification**: `bun run typecheck` + manual test with simulated slow importer

### L3-T6: FDA-Retry Button on Safari Card
- **Deliverable**: Safari import card shows:
  - ENOENT ("File not found — Safari data not present"): disable import, no retry
  - EPERM ("Full Disk Access required"): show retry button that opens `x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles`
  - After FDA granted + retry clicked: re-attempt Safari import
  - Distinction based on error code from parser layer (L1-T11)
- **Files**: `maho-chromium/browser/resources/maho_welcome/components/safari-card.tsx`, `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` (add `OpenFDASettings` Mojo method)
- **Success criteria**: ENOENT → no button; EPERM → button visible; button click opens System Preferences
- **Verification**: `grep -n 'x-apple.systempreferences' maho-chromium/browser/resources/maho_welcome/components/safari-card.tsx`

### L3-T7: End-to-End Manual QA Script
- **Deliverable**: `maho-chromium/browser/importer/test_data/qa_script.md` — executable QA checklist:
  1. Launch fresh profile (`--user-data-dir=/tmp/maho-qa`)
  2. Verify `chrome://welcome` appears automatically
  3. Select Zen as source → profile picker shows workspaces preview
  4. Start import → progress rows animate per-type
  5. After completion → verify `chrome://history` has imported entries
  6. Verify `chrome://password-manager` has imported passwords
  7. Verify sidebar shows imported space dots (3 for test fixture)
  8. Verify folder tree in sidebar (5-level nesting)
  9. Verify essentials in favorites grid
  10. Complete wizard → main browser opens, toast appears, welcome tab closes
- **Files**: `maho-chromium/browser/importer/test_data/qa_script.md`
- **Success criteria**: All 10 steps pass on dev build with Zen installed
- **Verification**: Manual execution of QA script

### L3-T8: Visual Spec — Splash Screen (Zen-parity)
- **Reference**: `.sisyphus/plans/zen-onboarding-reference-2026-06-10.md` §SCREEN 0 + §Window & Stage
- **Deliverable**: `maho-chromium/browser/resources/maho_welcome/pages/splash.tsx` matching Zen exactly:
  - Window 875×560 centered (window.resizeTo equivalent on welcome WebUI)
  - Title: two `<span>` lines, font-size `5rem`, weight `500`, line-height `1.1`, max-width `50%`, `font-feature-settings: "swsh" 1`
  - Junicode-equivalent serif font (bundle `JunicodeVF-Roman.woff2` or fallback to Cormorant/EB Garamond)
  - CTA: icon-only forward arrow button, `position: absolute; bottom: 10%; padding: 1em; min-width: 100px`
  - Copy: `IDS_MAHO_WELCOME_TITLE_LINE1` ("Welcome to") + `IDS_MAHO_WELCOME_TITLE_LINE2` ("a calmer internet")
  - Title spans enter animation: spring `{stiffness:300, damping:20, mass:1.8}`, span 1 at 0.2s, span 2 at 0.8s (0.6s stagger), keyframes `opacity:[0,1], y:[20,0], filter:["blur(2px)","blur(0px)"]`
  - CTA enter: same spring, delay 0.1s after spans complete
  - Exit: spring `ease:[0.755,0.05,0.855,0.06], bounce:0.4, stagger:0.4s`, keyframes `opacity:[1,0], y:[0,-10], filter:["blur(0px)","blur(2px)"]`
- **Files**: `splash.tsx`, `welcome.css` (splash section), `maho_strings.grdp` (copy strings)
- **Success criteria**: Visual diff vs Zen reference screenshot <5% pixel difference; animation timing within ±50ms of Zen values
- **Verification**: Playwright test loads splash, captures, runs `image-diff` against `test_data/zen-screenshots/splash.png`

### L3-T9: Visual Spec — Shell Layout (Zen-parity)
- **Reference**: `.sisyphus/plans/zen-onboarding-reference-2026-06-10.md` §SHELL
- **Deliverable**: Shell container in `app.tsx` matching Zen `#zen-welcome-pages`:
  - Width 60% × height 60% (responsive: 80% at <1400px, 90% at <1200px, 100% at <1000px with `border-radius: 0`)
  - `border-radius: 1em` (16px), `box-shadow: rgba(0,0,0,0.24) 0px 3px 8px`
  - Background `light-dark(#e2e2e2, #101010)` via CSS variable `--maho-branding-bg`
  - 40/60 split: sidebar `padding: 3.8rem`, content background `light-dark(rgba(0,0,0,.1), rgba(255,255,255,.1))`
  - Page transition animations:
    - Enter: sidebar h1+p+p `x:[150%,0]` spring `bounce:0.2 stagger:0.05s`; buttons same with `stagger:0.1s startDelay:0.4s`; content `opacity:[0,1] stagger:0.1s`
    - Exit: reverse `x:[0,-150%]` spring `bounce:0 stagger:0.05s startDelay:0.3s`; content `opacity:[1,0] startDelay:0.3s duration:0.1s`
  - Buttons get `pointer-events: none` until entry animation completes
- **Files**: `app.tsx` (router + transition), `welcome.css` (shell section)
- **Success criteria**: Shell layout matches Zen at all 4 responsive breakpoints; transitions match Zen timing within ±50ms
- **Verification**: Playwright resize test (1500/1300/1100/900px viewport) + transition assertion via animationend events

### L3-T10: Visual Spec — Import + Default Browser Screen (Zen-parity)
- **Reference**: §SCREEN 1 (Zen-exact: import screen has NO per-data-type checkboxes; those live in the separate Migration Dialog L3-T15)
- **Deliverable**: `import-data.tsx` rewrite matching Zen's two-stage Screen 1:
  - **Stage A (initial)** — content area shows nothing (or subtle illustration). Sidebar shows:
    - Headline: "A Fresh Start, Same Bookmarks"
    - Body 1: "Your bookmarks, history, and passwords are like a trail of breadcrumbs through the internet—don't leave them behind!"
    - Body 2: "Easily bring them over from another browser and pick up right where you left off."
    - Primary CTA: "Import now"
    - Secondary CTA: "Skip"
  - **On "Import now" click**: Open `MahoMigrationDialog` (L3-T15) — a separate modal/popup window. Dialog handles source browser selection + per-data-type checkboxes + actual import dispatch.
  - **Stage B (after dialog returns)** — Stage A primary CTA changes from "Import now" to "Next". Default-browser radio pair appears in content area (the same screen, no page transition):
    - Two radio cards SIDE-BY-SIDE (50% width each)
    - Card CSS: `padding: 1.5rem 1.1rem`, `border-radius: 0.6rem`, `gap: 0.8rem`, `border: 2px solid var(--maho-colors-border)`, `background: light-dark(rgba(255,255,255,.7), rgba(0,0,0,.4))`
    - Hover: `box-shadow: 0 3px 8px rgba(0,0,0,.24)`
    - Checked: `box-shadow + border: 2px solid var(--maho-primary-color); scale: 1.03`
    - Transition: `scale 0.1s, box-shadow 0.1s`
    - Card 1: "Set Maho as your default browser"
    - Card 2: "DON'T set Maho as your default browser" (default checked — matches Zen)
  - **No per-data-type checkboxes on this screen** (Zen-exact). All data-type selection is inside the dialog.
- **Files**: `import-data.tsx`, `maho_strings.grdp` (copy strings: headline, body 1, body 2, "Import now", "Skip", "Next", "Set as default", "DON'T set as default")
- **Success criteria**: Visual diff vs Zen Screen 1 (both stages) <5%; clicking "Import now" opens dialog; after dialog close, default-browser radios appear with copy verbatim
- **Verification**: Playwright click flow + visual diff at Stage A and Stage B

### L3-T11: Visual Spec — Search Engine Screen (Zen-parity)
- **Reference**: §SCREEN 2
- **Deliverable**: New `search-engine.tsx`:
  - Layout: `display: grid; grid-template-columns: 1fr 1fr; padding: 5rem`
  - Engine card: `flex-direction: column; font-weight: 600; aspect-ratio: 1/0.7; width: 60%; justify-self: center`
  - 3+ engines: card height 50%
  - 32×32 icon + name label
  - Engine list filtering: exclude any whose name contains "wikipedia" or "ebay"
  - On click: immediately calls `SearchService::SetDefaultSearchEngine` via Mojo (mirroring Zen's immediate `SearchService.setDefault`)
  - Copy: headline "Your Default Search Engine", body "Choose your default search engine. You can always change it later!", CTA "Next"
- **Files**: `search-engine.tsx`, page handler `SetDefaultSearchEngine` Mojo method, `maho_strings.grdp`
- **Success criteria**: Grid renders 4-6 engines correctly; click sets default in Maho prefs; visual diff <5%
- **Verification**: Playwright click + assertion on `search.engines.default` pref

### L3-T12: Visual Spec — Essentials Mock Browser (Zen-parity)
- **Reference**: §SCREEN 3
- **Deliverable**: New `essentials.tsx` with embedded mock browser preview:
  - Mock browser container: `min-width: 70%; min-height: 80%; margin-left: auto; margin-top: auto; border-top-left-radius: 1.2em; border: 1px solid var(--maho-colors-border); border-bottom-width: 0; border-right-width: 0; box-shadow: 0 3px 8px rgba(0,0,0,.24); background: light-dark(rgba(255,255,255,.5), rgba(0,0,0,.2)); padding-right: 20%`
  - Sidebar: `padding: 1.4rem; gap: 1.2rem; background: light-dark(rgba(255,255,255,.7), rgba(0,0,0,.4))`
  - macOS win-button row: `gap: 0.5rem`, three `15×15px` circles with `border-radius: 50%; background: var(--maho-toolbar-element-bg)`
  - Essentials grid: `display: grid; grid-template-columns: repeat(3, 1fr); gap: 1rem`
  - 9 pre-seeded apps: Obsidian, Discord, Trello, Slack, GitHub, Tuta, Notion, Google Calendar, Figma (URLs + favicon SVGs from `maho-chromium/browser/resources/maho_welcome/icons/essentials/{name}.svg`)
  - Tab tile: `--tab-min-height: 6rem; min-width: 5rem; --border-radius-medium: 1rem; transition: transform 0.1s`
  - Selected tile: `transform: scale(1.06)`
  - Tile icon overlay (`::after`): `width: 1.8rem; height: 1.8rem; position: absolute; top:50%; left:50%; transform: translate(-50%,-50%); border-radius: 4px; background: var(--maho-essential-tab-icon)`
  - 2 placeholder bars (`.extra-tab`): `width: 100%; height: 4rem; border-radius: var(--border-radius-large); background: var(--maho-toolbar-element-bg); grid-column: span 3`
  - On wizard finish: selected tabs → `MahoSpaceFFIClient::CreateTab(default_space, url, title)` + `FavoriteTab(tab_id)` (L2-T3 wrapper)
  - Copy: headline "Your Key Tabs, Always Within Reach", body 1 "Keep your most important tabs easily accessible and always at hand, no matter how many you open.", body 2 "Essential tabs are always visible, no matter what workspace you are in.", CTA "Next"
- **Files**: `essentials.tsx`, 9 SVG icons under `resources/maho_welcome/icons/essentials/`, `maho_strings.grdp`
- **Success criteria**: 9 tiles render, click toggles selected scale, finish creates 9 essentials in default Maho space; visual diff <5%
- **Verification**: Playwright click + post-finish assertion via `MahoSpaceFFIClient::GetSpaceFavoriteTabs(default_space).length == selected_count`

### L3-T13: Visual Spec — Theme Screen (Zen-parity, gradient-based)
- **Reference**: §SCREEN 4 + §Theme System Internals + §Theme Picker
- **Deliverable**: Theme screen reuses Maho's existing space-create theme picker (panel `chrome://maho-space-create` already implements gradient generation per `maho/AGENTS.md` references). Specifically:
  - Open the existing Maho theme picker panel via `PanelMultiView::OpenPopup` equivalent on a 1×1 anchor inside the welcome content area, expand anchor to panel rect on `popupshowing`
  - Panel attributes: `noautohide`, `consumeoutsideclicks=false`, `nonnativepopover`
  - Theme JSON schema must match: `{type:"gradient", gradientColors:[{c:[r,g,b], isPrimary, isCustom, algorithm, lightness}, ...], opacity:0.5, texture:0}`
  - Gradient algorithms (`getGradient()` from existing maho-core or new helper):
    - 0 colors → fallback solid (rgba(0,0,0,.4) dark / #131313 dark-opaque / #e9e9e9 light)
    - 1 color → blendWithWhiteOverlay(opacity)
    - 2 colors → background `linear-gradient(-45deg, c2, transparent), linear-gradient(135deg, c1, transparent)`; toolbar `linear-gradient(-45deg, c2, c1)`
    - 3 colors → `linear-gradient(-5deg, c1 10%, transparent 80%) + radial-gradient(circle at 95% 0%, c3, transparent 75%) + radial-gradient(circle at 0% 0%, c2 10%, transparent 70%)`
  - Pre-seeded swatches: 5 pages × 8 colors (light singles → light analogous trios → dark singles → dark analogous trios → grayscale ramp). Exact hex values from Zen dossier.
  - Color harmonies: complementary (180°), singleAnalogous (310°), splitComplementary (150,210°), analogous (50,310°), triadic (120,240°), floating (free)
  - Opacity slider: macOS 0.30-0.90, other 0.25-0.90, default 0.5
  - Texture rotator: 16 dots in circle, mix-blend-mode hard-light grain overlay
  - Selected theme applied via `MahoSpaceFFIClient::UpdateSpaceConfig(default_space, theme_json)` (L2-T3)
  - On exit: panel hides via `animate({opacity:[1,0]})` then `hidePopup()`. Page does NOT fade out content (`dontFadeOut: true`).
  - Copy: headline "Your Workspaces, Your Colors", body "Personalize your browser by giving each workspace its own unique color identity.", CTA "Next"
- **Files**: `theme.tsx`, theme picker integration via Mojo, `maho_strings.grdp`
- **Success criteria**: Default space shows gradient theme matching selected swatch + opacity + texture combination; visual diff vs Zen theme screen <5%
- **Verification**: Playwright click swatch → assert `space.theme` JSON matches expected; visual diff

### L3-T14: Visual Spec — Completion Screen + Heart Animation (Zen-parity)
- **Reference**: §SCREEN 5
- **Deliverable**: Rewrite `completion.tsx`:
  - Two-line headline: `All set?<br/>Let's get rolling!` (Junicode, weight 500, line-height 1.1)
  - Body: `You're all set up and ready to go. Click the button below to start browsing with Maho.`
  - CTA: `Dive in!` — NO secondary/skip button
  - On CTA click: `FinishOnboarding()` Mojo (L2-T6) which orchestrates:
    1. `animate("#welcome-content", {x:[0,"100%"]}, {bounce:0})` — slide right
    2. `animHeart()` — `animate("#welcome-heart", {opacity:[0,1,1,1,0], scale:[0.5,1,1.2,1,1.2]}, {duration:1.5, delay:0.2, bounce:0})` — total 1.7s
    3. `pinRemainingTabs()` (essentials → default space favorites)
    4. `animate("#welcome-pages", {opacity:[1,0]})` — fade overlay
    5. Reveal browser chrome (`#browser > * style.display = ""; opacity = 0`)
    6. `animate("#browser > *:not(background, toast)", {opacity:[0,1]})` — fade chrome in
    7. Show toast `IDS_MAHO_TOAST_WELCOME_FINISHED`
  - Heart element: `#welcome-heart` div, `width:100%; height:100%; opacity:0; background-image: url(chrome://maho-icons/essential-add.svg); background-size: 15%; background-position: center; -moz-context-properties: fill, fill-opacity` equivalent
- **Files**: `completion.tsx`, `welcome.css` (heart animation), `maho_welcome_page_handler.cc` (FinishOnboarding extension), `maho_strings.grdp`
- **Success criteria**: Animation sequence plays in order; heart pulses 1.5s exactly; chrome reveals smoothly; toast appears
- **Verification**: Playwright capture sequence at 0s, 0.4s, 1s, 1.5s, 2s after CTA click → 5 frame diff vs Zen reference

### L3-T15: Maho Migration Dialog (Zen MigrationUtils functional equivalent — visual flavor parity, not pixel parity)
- **Reference**: Zen `MigrationUtils.showMigrationWizard(window, {isStartupMigration:true})` opens Firefox's native MigrationUtils wizard. Maho is Chromium-based and **cannot replicate that wizard pixel-for-pixel** — it's Gecko code with Firefox's XUL/XBL toolkit. We build a functionally equivalent native dialog with **visual flavor parity** (Zen design tokens applied to Chromium Views), not visual identity.
- **Visual parity scope (HONEST)**: Dialog uses Zen's design tokens (`--maho-dialog-background`, `--maho-big-shadow`, border-radius, padding scale, `color-mix()` colors) so it feels like part of the Zen-flavored welcome flow. But the dialog's **structural layout will not match Firefox MigrationUtils visually** — that's an architectural reality we accept, not a plan defect.
- **Deliverable**: Native Views-based dialog `MahoMigrationDialogView : views::DialogDelegateView` (mirrors Chromium's existing import dialog patterns at `chrome/browser/ui/views/importer/`):
  - **Trigger**: L3-T10's "Import now" button calls `OpenMigrationDialog()` Mojo method on the welcome page handler. Page handler instantiates `MahoMigrationDialogView` modal-rooted to the welcome window.
  - **Layout** (top-to-bottom, ~720×540):
    1. Header: "Import from another browser" + close button (X)
    2. Source browser list — vertical card list of detected browsers (Chrome / Firefox / Zen / Arc / Safari / Brave / Edge) with profile picker dropdown when >1 profile detected. Each card uses `2px solid var(--maho-colors-border)` + hover `--maho-big-shadow` + `2px solid var(--maho-primary-color)` on selected.
    3. Per-data-type checkbox group — driven by `kImport*` Mojo enum (L3-T1):
       - ☑ Bookmarks
       - ☑ History
       - ☑ Passwords
       - ☑ Cookies
       - ☑ Autofill form data
       - ☑ Favicons
       - ☑ **Spaces & Tabs** — appears only when `selected_browser.servicesSupported & kImportWorkspaces` (Zen / Arc) — default-checked
       - ☑ Sidebar items
    4. Per-type availability: checkboxes disabled and grayed when `!(selected_browser.servicesSupported & kFlag)` (e.g., Safari: only Bookmarks enabled)
    5. Progress region (visible only during import): one row per active data type with spinner / ✓ / ✗ / ⊘ icons + count text ("Importing 1234 bookmarks…")
    6. Buttons: "Cancel" (left) + "Import" (right, primary, `--maho-primary-color`)
  - **Visual tokens (flavor parity)**:
    - Dialog background: `var(--maho-dialog-background)` = `light-dark(#fafbff, #1c1c1c)` — matches Zen
    - Border-radius: `1em` (16px) on outer dialog, `0.6rem` on cards (matches Zen radio cards)
    - Shadow: `--maho-big-shadow` = `rgba(0,0,0,0.24) 0px 3px 8px`
    - Padding: card `1.5rem 1.1rem`, dialog content `2rem`
    - Selected card scale: `1.03` (matches L3-T10's import radio cards for consistency)
    - Typography: system UI font (Views default), header weight 600 / body weight normal
  - **Behavior matrix** (all edge cases explicit):

    | Trigger | Action | Welcome state after |
    |---|---|---|
    | "Import now" clicked on import screen Stage A | Dialog opens, no browser pre-selected | Welcome flow paused; waits for dialog result |
    | "Skip" clicked on import screen Stage A | Dialog NEVER opens; both Stage A and Stage B skipped; welcome advances to next screen (Search Engine) | Skip → Search Engine |
    | Source browser selected in dialog | Checkboxes update per `servicesSupported` bitmask | Dialog still open |
    | "Cancel" clicked before "Import" was clicked | Dialog closes immediately. **Welcome returns to import screen Stage A** (NOT Stage B). User can click "Import now" again or "Skip". | Stage A |
    | "Cancel" clicked DURING active import | Dispatch `CancelImport()` to orchestrator → orchestrator stops pending workers → dialog stays open showing "Cancelling…" → after orchestrator confirms cancel, dialog closes → **Welcome returns to import screen Stage A** (no partial Stage B; user can retry) | Stage A |
    | "Import" clicked, import succeeds | Progress rows complete, dialog auto-closes after 1s delay → **Welcome advances to Stage B** (default-browser radio cards appear, primary CTA changes "Import now" → "Next") | Stage B |
    | "Import" clicked, import partially fails (some types succeed, some fail) | Per-type rows show ✓ or ✗; "Continue" button replaces "Import". User clicks Continue → dialog closes → **Welcome advances to Stage B** (partial success counts as completion for flow advancement) | Stage B |
    | "Import" clicked, import fully fails (e.g., source browser DB corrupted) | All rows show ✗ + error message + "Retry" + "Cancel" buttons. Retry re-attempts; Cancel returns to Stage A. | Stage A or Stage B based on user action |
    | Window closed via X (top-right) before Import clicked | Equivalent to Cancel-before-Import: returns to Stage A | Stage A |
    | Window closed via X during active import | Equivalent to Cancel-during-import: dispatches CancelImport, returns to Stage A | Stage A |

  - **Per-space import is exposed here** — "Spaces & Tabs" checkbox is the user-visible knob that triggers `WorkspaceImporter` (L2-T2 → L2-T3 `MahoSpaceFFIClient`). Without this checkbox, `kImportWorkspaces` bit is never set and workspace import is unreachable.
  - **Localization**: Header, button labels, checkbox labels, status text, error messages — all `IDS_MAHO_MIGRATION_*` strings via L4-T10. Both `en` and `ko`.
- **Files**:
  - `maho-chromium/browser/ui/views/importer/maho_migration_dialog_view.{h,cc}` — Views-based dialog implementation
  - `maho-chromium/browser/ui/views/importer/maho_migration_dialog_browsertest.cc` — InProcessBrowserTest covering all 9 behavior matrix scenarios
  - `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome.mojom` — add `OpenMigrationDialog() => (MigrationDialogResult)` Mojo method, `MigrationDialogResult { bool was_cancelled; uint32 imported_items_bitmask; }` struct
  - `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` — `OpenMigrationDialog` implementation: instantiate dialog modal-rooted to welcome window, await result, return to TS
  - `maho-chromium/browser/maho_strings.grdp` — `IDS_MAHO_MIGRATION_*` strings (header, 8 checkbox labels, button labels, 5 status messages, error variants)
- **Success criteria**:
  - Dialog renders all detected browsers + per-data-type checkboxes
  - "Spaces & Tabs" checkbox appears for Zen/Arc with default-checked, hidden/disabled for others
  - "Import" click dispatches StartImport with correct bitmask; per-type progress visible; closes on completion
  - All 9 behavior matrix rows produce documented post-state transitions
  - Visual flavor matches Zen welcome (dialog tokens consistent with import-data screen tokens)
- **Verification**:
  - C++ unit test: `MahoMigrationDialogViewTest` — instantiate view, simulate browser detection, click "Spaces & Tabs", click Import, assert `StartImport` called with expected bits
  - Browser test: `MahoMigrationDialogBrowserTest` — covers all 9 behavior matrix rows; assert post-state Stage A vs Stage B transitions correctly
  - Visual flavor diff: dialog screenshot compared against welcome screen — same `--maho-big-shadow`, same border-radius scale, same color tokens (NOT compared against Zen's MigrationUtils — that's an explicit out-of-scope per "visual flavor parity" note above)
  - Playwright/manual: launch fresh profile → wizard → click "Import now" → verify dialog opens with Zen detected → check "Spaces & Tabs" → click Import → verify orchestrator receives correct bitmask

---

## Layer 4: Onboarding Polish

### L4-T1: Animations
- **Deliverable**: CSS/JS animations in `maho-chromium/browser/resources/maho_welcome/welcome.css`:
  - Heart pulse on completion page (CSS `@keyframes pulse`)
  - Slide-out on welcome tab close (300ms ease-out)
  - Fade-in on main browser open (200ms)
  - Page transition: slide-left for forward, slide-right for back (250ms)
  - Single source in `welcome.css` — no inline styles
  - Animation timing tolerance ±50ms (acceptable variance)
- **Files**: `maho-chromium/browser/resources/maho_welcome/welcome.css`
- **Success criteria**: All animations render smoothly at 60fps; no layout shift during transitions
- **Verification**: Manual visual QA on dev build

### L4-T2: Theme Picker
- **Deliverable**: Theme page wired to backend:
  - 3 mode chips: Light / Dark / System
  - 12 color swatches (predefined palette)
  - Selection writes JSON: `{ "scheme": "auto"|"light"|"dark", "accent": "#hex" }`
  - Applied via `MahoSpaceFFIClient::UpdateSpaceConfig` for default space
  - Persisted in profile prefs for new spaces
- **Files**: `maho-chromium/browser/resources/maho_welcome/pages/theme.tsx`, `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` (add `SetTheme` method)
- **Success criteria**: Theme selection persists across restart; default space reflects chosen theme
- **Verification**: Select dark + blue → restart → verify pref file contains theme JSON

### L4-T3: Welcome Toast
- **Deliverable**: `IDS_MAHO_TOAST_WELCOME_FINISHED` string resource actually displayed:
  - Wired via `MahoNotificationOverlay::Show()` from `FinishOnboarding` (L2-T6)
  - Toast appears in main browser window after welcome tab closes
  - Auto-dismisses after 5 seconds
  - No hardcoded string literals — uses `l10n_util::GetStringUTF16(IDS_MAHO_TOAST_WELCOME_FINISHED)`
- **Files**: `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc`, string resource in `maho-chromium/browser/maho_strings.grdp`
- **Success criteria**: Toast appears with localized string; no hardcoded literals in source
- **Verification**: `grep -rn 'IDS_MAHO_TOAST_WELCOME_FINISHED' maho-chromium/browser/ | grep -v 'grdp'` (expect ≥1 usage in .cc)

### L4-T4: Localization
- **Deliverable**: All UI strings use `IDS_*` resource IDs:
  - Toast messages, error messages, FDA explanations, progress status text, page titles, button labels
  - Registered via `apply_chromium_src_overrides.py` string override mechanism
  - Korean (ko) and English (en) translations provided
- **Files**: `maho-chromium/browser/maho_strings.grdp` (add entries), `maho-chromium/browser/resources/maho_welcome/` (consume via Mojo or `loadTimeData`)
- **Success criteria**: Zero hardcoded user-visible strings in TS/C++; `grep -rn '"[A-Z].*"' | grep -v IDS_` finds only non-user-visible strings
- **Verification**: `grep -rn 'loadTimeData\|GetStringUTF16' maho-chromium/browser/resources/maho_welcome/ maho-chromium/browser/ui/webui/maho_welcome/`

### L4-T5: Accessibility
- **Deliverable**: Full keyboard navigation + screen reader support:
  - All wizard pages navigable via Tab/Shift+Tab
  - Focus ring visible on all interactive elements
  - `aria-label` on icon-only buttons
  - `aria-live="polite"` on progress region
  - `role="progressbar"` with `aria-valuenow` on import progress
  - Focus auto-moves to first interactive element on page transition
  - Escape key = skip/close where applicable
- **Files**: `maho-chromium/browser/resources/maho_welcome/pages/*.tsx`, `maho-chromium/browser/resources/maho_welcome/components/*.tsx`
- **Success criteria**: VoiceOver (macOS) can navigate entire wizard without mouse; no focus traps
- **Verification**: Manual VoiceOver walkthrough of all 8 screens

### L4-T6: Final Acceptance Gate — 35-Item Checklist
- **Deliverable**: Automated + manual verification that all 35 acceptance criteria pass (see §Acceptance Checklist below — 11 functional + 5 behavioral + 12 visual + 3 security + 2 test + 2 architecture). Codified in `maho-chromium/browser/importer/ACCEPTANCE.md`.
- **Files**: `maho-chromium/browser/importer/ACCEPTANCE.md`
- **Success criteria**: All 35 items checked off
- **Verification**: Run Final Gate Commands (see below)

### L4-T7: Visual Regression QA Gate (Playwright + image-diff)
- **Reference**: `.sisyphus/plans/zen-onboarding-reference-2026-06-10.md`
- **Deliverable**:
  - Reference screenshots captured from a live Zen install: `test_data/zen-screenshots/{splash,shell,import,search,essentials,theme,completion}.png` (8 images committed under `maho-chromium/browser/resources/maho_welcome/test_data/`)
  - Playwright test `welcome_visual_regression.spec.ts` that loads `chrome://maho-welcome/`, walks through each screen, captures viewport, runs `pixelmatch` (or `image-diff`) vs reference; FAIL if >5% pixel difference
  - GN test target wired so `bun run test:visual` triggers it
  - Headless screenshot capture on CI
- **Files**: `maho-chromium/browser/resources/maho_welcome/test_data/zen-screenshots/*.png`, `maho-chromium/browser/resources/maho_welcome/__tests__/welcome_visual_regression.spec.ts`, `maho-chromium/browser/resources/maho_welcome/BUILD.gn` (test action)
- **Success criteria**: All 8 screen captures within 5% pixel diff vs reference; test passes in CI
- **Verification**: `cd maho-chromium/browser/resources/maho_welcome && bun run test:visual`

### L4-T8: Theme Token System (Zen color-mix() parity)
- **Reference**: §Theme Color System
- **Deliverable**: Add CSS custom properties to `welcome.css` (and global Maho theme stylesheet if applicable) matching Zen's derivation chain exactly:
  - `--maho-branding-dark: #101010`, `--maho-branding-paper: #e2e2e2`
  - `--maho-branding-bg: light-dark(#e2e2e2, #101010)`
  - `--maho-branding-bg-reverse: light-dark(#101010, #e2e2e2)`
  - `--maho-colors-primary: light-dark(color-mix(in srgb, var(--maho-primary-color) 50%, black), color-mix(in srgb, var(--maho-primary-color) 20%, #101010 80%))`
  - `--maho-colors-secondary` and `--maho-colors-tertiary` per dossier formulas
  - `--maho-colors-border` per dossier formula
  - `--maho-dialog-background`, `--maho-urlbar-background`, `--maho-main-browser-background`
  - `--maho-big-shadow: rgba(0,0,0,0.24) 0px 3px 8px`
  - `--maho-border-radius: 7px`, `--button-border-radius: 8px`
  - Light/dark scheme switching via `:root[data-scheme="dark"]` / `:root[data-scheme="light"]`
- **Files**: `welcome.css`, optionally `maho-chromium/browser/resources/common/zen_compat_tokens.css`
- **Success criteria**: All Zen-equivalent CSS variables defined and used by L3-T8 through L3-T14 components
- **Verification**: `grep -c '\\-\\-maho-' maho-chromium/browser/resources/maho_welcome/welcome.css` ≥ 12

### L4-T9: Animation Library Choice + Spring Param Lock
- **Reference**: §Animation System Summary
- **Deliverable**: Decision document + locked physics constants:
  - Choice: **Motion One** (`@motionone/dom`) — Zen's choice, identical spring API. Bundle into welcome resources.
  - Alternative if bundle size objection: Web Animations API + custom spring solver (matches `motion-utils` formula).
  - Locked constants in `welcome.css.constants.ts`:
    - `SPLASH_SPRING = {type:"spring", stiffness:300, damping:20, mass:1.8}`
    - `PAGE_TRANSITION_SPRING = {type:"spring", bounce:0.2}` (enter) / `{bounce:0}` (exit)
    - `SPLASH_EXIT_EASE = [0.755, 0.05, 0.855, 0.06]` (cubic-bezier)
    - `SPLASH_EXIT_BOUNCE = 0.4`
    - Stagger: splash spans 0.6s, sidebar children 0.05s, buttons 0.1s, button startDelay 0.4s
    - Heart pulse `{duration:1.5, delay:0.2, bounce:0}`
    - Theme dot move `{duration:0.4, type:"spring", bounce:0.3}`
- **Files**: `welcome.css.constants.ts`, `BUILD.gn` (motion-one dependency)
- **Success criteria**: All 8 wizard pages use only the locked constants; no inline animation params
- **Verification**: `grep -rn 'stiffness\\|damping\\|bounce' maho-chromium/browser/resources/maho_welcome/pages/ | grep -v constants` (expect 0)

### L4-T10: Copy Localization (en + ko, all from Zen .ftl)
- **Reference**: §SCREEN 0-5 copy tables
- **Deliverable**: All UI strings registered as `IDS_MAHO_WELCOME_*` resources via `apply_chromium_src_overrides.py`. Both `en` and `ko` translations:
  - Screens: `IDS_MAHO_WELCOME_TITLE_LINE1/2`, `IDS_MAHO_WELCOME_IMPORT_HEADLINE/BODY1/BODY2/CTA`, `IDS_MAHO_WELCOME_DEFAULT_BROWSER_SET/DONT_SET`, `IDS_MAHO_WELCOME_SEARCH_HEADLINE/BODY/CTA`, `IDS_MAHO_WELCOME_ESSENTIALS_HEADLINE/BODY1/BODY2/CTA`, `IDS_MAHO_WELCOME_THEME_HEADLINE/BODY/CTA`, `IDS_MAHO_WELCOME_COMPLETION_HEADLINE/BODY/CTA`, `IDS_MAHO_TOAST_WELCOME_FINISHED`
  - Source-of-truth `en` strings copied verbatim from Zen's `zen-welcome.ftl` (substituting "Zen" → "Maho" where appropriate)
  - `ko` translations professionally drafted (or marked `[TODO: ko translation]` for v1)
- **Files**: `maho-chromium/build/scripts/apply_chromium_src_overrides.py` (extend), translation `.xtb` files
- **Success criteria**: Zero hardcoded user-visible strings in TS/C++ welcome code; both `en` and `ko` resolve correctly
- **Verification**: `grep -rn '"[A-Z][a-z]\\+\\s' maho-chromium/browser/resources/maho_welcome/pages/ | grep -v IDS_ | grep -v import` (expect minimal/zero matches)

---

## Tier B Sketch (Post-Tier-A)

- **Mods System**: userChrome.css per space — inject custom CSS into web content via space-scoped content script mechanism. Requires maho-core extension loader changes.
- **Split View**: Side-by-side tab tiling within a space. Requires native Views layout changes + space-aware tab strip model.
- **Glance / Floating URL Bar**: Minimal overlay URL bar that auto-hides. Separate WebUI surface overlaying main content.
- **Compact Mode**: Auto-hide sidebar on narrow windows; sidebar appears on hover or keyboard shortcut.
- **Container Tabs**: Firefox-style tab isolation per container (separate cookie jars). Requires `StoragePartition` per container integration.
- **Custom NTP**: Per-space new tab page with widgets, bookmarks grid, and search. Replaces chrome://newtab with maho://space-home.

---

## v1 Failure Inventory → Task Mapping

| # | v1 Bug | Fix Task(s) | Verification |
|---|--------|-------------|--------------|
| 1 | NSS double-strip | L1-T1 | `NSSDecryptHelper*` test passes |
| 2 | Cookies as ciphertext | L1-T2 | `CookieParser*` no ciphertext in output |
| 3 | Phase 14 unreachable (bit-6 collision) | L0-T1 + L3-T1 + L3-T2 | Mojo IDL has no bit collision; TS imports from generated |
| 4 | FF/Zen passwords use Chromium parser | L1-T4 + L2-T4 | Browser-type switch routes Zen to Firefox parser |
| 5 | NotifyChanged from thread pool | L2-T3 + L2-T5 | DCHECK on UI thread in FFI client |
| 6 | FavoriteEssentialSites blocks UI thread | L2-T5 | PostTaskAndReplyWithResult pattern |
| 7 | Phase 12 race + double-close | L2-T6 | `is_finishing_` guard; test double-call |
| 8 | Import completion never fires complete=true | L2-T1 | Orchestrator is sole `complete=true` caller |
| 9 | NSS Variant C SHA-384 broken | L1-T1 | Parameterized test over variant C fixture |
| 10 | test_data/ empty | L0-T4 + L0-T5 | `verify_golden.py` passes |
| 11 | Cookies + favicons unconditional | L2-T1 | Orchestrator gates by bitmask |
| 12 | IDS_MAHO_TOAST_WELCOME_FINISHED unused | L4-T3 + L4-T4 | grep shows usage in .cc |
| 13 | sql::Database tag copy-paste | L1-T3 + L1-T10 | Unique tags per parser |
| 14 | Cookie/favicon don't copy-to-temp | L1-T2 + L1-T12 | Copy logic in parser |
| 15 | Plaintext keys not zeroed | L1-T1 + L1-T3 | RAII SecureBuffer in code |
| 16 | PII in logs | L1-T1 + L1-T3 + L1-T4 | grep for hostnames in LOG = 0 |
| 17 | Safari FDA conflates ENOENT/EPERM | L1-T11 + L3-T6 | Distinct error codes in test |
| 18 | FDA-retry button missing | L3-T6 | Button renders on EPERM |
| 19 | Workspace folder hierarchy dropped | L1-T15 + L2-T3 + L2-T7 | E2E test asserts 5-level nesting |
| 20 | Bookmarks duplicate-imported with workspaces | L2-T2 (BookmarkImporter skip) | Test: workspace active → bookmarks skipped |

---

## Risk → Mitigation Mapping

| Risk | Mitigation | Task |
|------|-----------|------|
| `kCCPRFHmacAlgSHA384` unavailable on CI macOS | Runtime check + `GTEST_SKIP()` on < 10.15 | L1-T1 |
| Real fixture extraction leaks PII | `extract_fixtures.py` regex-scrubs + `--dry-run` | L0-T5 |
| Golden hash breaks on git line-ending | Fixtures are binary `.bin` + `.gitattributes binary` | L0-T4 |
| OSCrypt key unavailable in test env | Mock `OSCryptKeyProvider` with known test key | L1-T2, L1-T3 |
| sql::Database WAL lock on live DBs | `SQLITE_OPEN_READONLY` + copy-to-temp | L1-T2, L1-T3, L1-T12 |
| Upstream rebase breaks BUILD.gn | All targets in `maho-chromium/` overlay only | L0-T6 |
| FFI `char*` lifetime misuse | `MahoSpaceFFIClient` owns string + calls `maho_string_free` | L2-T3 |
| Welcome wizard renders blank on fresh profile | Smoke test in L3-T7 QA script | L3-T3 |
| Double-close race in FinishOnboarding | `is_finishing_` guard + unit test | L2-T6 |
| Mojo IDL change breaks existing consumers | L0-T1 is first task; all consumers updated in L2-T4 | L0-T1 |
| Thread-pool FFI call crashes | UI-thread DCHECK in `MahoSpaceFFIClient`; `PostTaskAndReply` pattern | L2-T5 |
| Folder nesting > 5 levels in source browser | Cap at 5, flatten remainder into parent | L2-T3 |
| **Junicode-equivalent font unavailable** | Bundle `JunicodeVF-Roman.woff2` as web font in welcome resources OR use Cormorant/EB Garamond fallback with same metrics | L3-T8 |
| **Animation library bundle bloat** | Motion One = 18KB gzip; if too large, fall back to Web Animations API + custom spring solver | L4-T9 |
| **Visual diff false positives from font rendering differences** | Allow 5% pixel tolerance + per-screen baseline regeneration script | L4-T7 |
| **Zen reference screenshots become stale** as Zen updates | `extract_zen_screenshots.py` re-captures from current Zen install; commit timestamp pinned to dossier SHA | L4-T7 |
| **Theme gradient algorithm divergence** if Maho's existing gradient picker has different formulas | L3-T13 explicit task: align Maho's `getGradient()` with Zen's exact 0/1/2/3-color formulas; unit test with known input → expected gradient string | L3-T13 |
| **Color-mix() unsupported in older Chromium** Chromium fork supports it (M111+); verify build target is recent enough | grep `color-mix` in welcome.css after L4-T8; build with current Maho target | L4-T8 |
| **Copy translation drift** between Zen .ftl and Maho .grdp over time | `apply_chromium_src_overrides.py` script regenerates from upstream Zen .ftl source; CI gate diff | L4-T10 |
| **Visual regression test flakiness on CI** (subpixel rendering, GPU differences) | Run on consistent macOS runner with locked GPU profile; 5% tolerance buffer | L4-T7 |

---

## Atomic Commit Strategy


# Layer 0 (sequential, each buildable)
git commit -m "importer(L0): add Mojo IDL constants for import types"          # L0-T1
git commit -m "importer(L0): add INVARIANTS.md + consumer enumeration"         # L0-T2 + L0-T3
git commit -m "importer(L0): add synthetic test fixtures + golden hashes"      # L0-T4
git commit -m "importer(L0): add real-fixture extraction tooling"              # L0-T5
git commit -m "importer(L0): add GN test gate + fixture integrity test"        # L0-T6 + L0-T7

# Layer 1 (one per parser, parallelizable)
git commit -m "importer(L1): fix nss_decrypt_helper double-strip + SHA-384 + key zeroing + tests"
git commit -m "importer(L1): fix cookie_parser v10/v20 + copy-to-temp + tests"
git commit -m "importer(L1): fix chromium_password_parser sql tag + key zeroing + tests"
git commit -m "importer(L1): fix firefox_password_parser variant routing + tests"
git commit -m "importer(L1): add zen_workspaces_parser tests + folder hierarchy"
git commit -m "importer(L1): add arc_sidebar_parser tests + URL validation"
git commit -m "importer(L1): add mozlz4_decompressor tests + magic check"
git commit -m "importer(L1): add chromium_history_parser tests + timestamp fix"
git commit -m "importer(L1): add firefox_history_parser tests + mozlz4"
git commit -m "importer(L1): add chromium_autofill_parser tests + schema version"
git commit -m "importer(L1): fix safari_bookmark_parser FDA error codes + tests"
git commit -m "importer(L1): add favicon_parser tests + copy-to-temp + size cap"
git commit -m "importer(L1): fix maho_bookmark_importer dedup + workspace-skip + tests"
git commit -m "importer(L1): fix maho_browser_detector multi-profile + tests"
git commit -m "importer(L1): fix maho_workspace_importer folder hierarchy + tests"

# Layer 2 (sequential — each depends on prior)
git commit -m "importer(L2): add ImportOrchestrator KeyedService + tests"       # L2-T1
git commit -m "importer(L2): add ImporterBase + per-type workers + tests"       # L2-T2
git commit -m "importer(L2): add MahoSpaceFFIClient wrapper + tests"            # L2-T3
git commit -m "importer(L2): thin page handler — delegate to orchestrator"      # L2-T4
git commit -m "importer(L2): make FavoriteEssentialSites non-blocking"          # L2-T5
git commit -m "importer(L2): FinishOnboarding single method + race guard"       # L2-T6
git commit -m "importer(L2): add workspace import E2E test"                     # L2-T7

# Layer 3 (partially parallelizable)
git commit -m "welcome(L3): rewrite import-data.tsx with Mojo-generated constants"  # L3-T1
git commit -m "welcome(L3): rewrite store.ts — Mojo enum state machine"             # L3-T2
git commit -m "welcome(L3): implement 8-screen wizard (Zen parity)"                 # L3-T3
git commit -m "welcome(L3): browser detector multi-profile UI"                       # L3-T4
git commit -m "welcome(L3): per-data-type progress UI"                               # L3-T5
git commit -m "welcome(L3): Safari FDA-retry button + error distinction"             # L3-T6
git commit -m "welcome(L3): add E2E QA script"                                       # L3-T7

# Layer 4 (sequential polish)
git commit -m "welcome(L4): add page transition + completion animations"      # L4-T1
git commit -m "welcome(L4): wire theme picker to MahoSpaceFFIClient"          # L4-T2
git commit -m "welcome(L4): wire welcome toast via IDS_ resource"             # L4-T3
git commit -m "welcome(L4): localize all strings (en + ko)"                   # L4-T4
git commit -m "welcome(L4): accessibility — keyboard nav + ARIA"              # L4-T5
git commit -m "welcome(L4): add ACCEPTANCE.md + final gate verification"      # L4-T6


Each commit gate: `bun run typecheck && autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='*Importer*:*Welcome*:*Orchestrator*:*FFIClient*'`

---

## Effort Estimate

| Layer | Tasks | Parallel Waves | Estimated Duration |
|-------|-------|---------------|-------------------|
| L0 | 7 | 2 waves | 2 days |
| L1 | 15 | 3 waves of 5 | 4 days |
| L2 | 7 | 2 waves | 3 days |
| L3 | 15 (7 functional + 7 visual spec T8-T14 + 1 dialog T15) | 4 waves | 6 days |
| L4 | 10 (6 polish + T7 visual regression + T8 tokens + T9 anim lock + T10 copy) | 3 waves | 4 days |
| **Total** | **54 tasks** | — | **~19 days** |

---

## Parallel Execution Graph


Wave 1 (L0, no deps):
├── L0-T1: Mojo IDL constants
├── L0-T2: INVARIANTS.md
└── L0-T3: Bridge consumer enumeration

Wave 2 (L0, depends Wave 1):
├── L0-T4: Synthetic fixtures
├── L0-T5: Extraction tooling
├── L0-T6: GN test gate (depends L0-T4)
└── L0-T7: Golden-hash harness (depends L0-T4)

--- L0 GREEN GATE ---

Wave 3 (L1 batch A):
├── L1-T1: nss_decrypt_helper
├── L1-T2: cookie_parser
├── L1-T3: chromium_password_parser
├── L1-T4: firefox_password_parser
└── L1-T5: zen_workspaces_parser

Wave 4 (L1 batch B, parallel with Wave 3):
├── L1-T6: arc_sidebar_parser
├── L1-T7: mozlz4_decompressor
├── L1-T8: chromium_history_parser
├── L1-T9: firefox_history_parser
└── L1-T10: chromium_autofill_parser

Wave 5 (L1 batch C, parallel with Wave 3-4):
├── L1-T11: safari_bookmark_parser
├── L1-T12: favicon_parser
├── L1-T13: maho_bookmark_importer
├── L1-T14: maho_browser_detector
└── L1-T15: maho_workspace_importer

--- L1 GREEN GATE ---

Wave 6 (L2 batch A):
├── L2-T1: ImportOrchestrator
├── L2-T2: ImporterBase + workers
└── L2-T3: MahoSpaceFFIClient

Wave 7 (L2 batch B, depends Wave 6):
├── L2-T4: Page handler thinning
├── L2-T5: FavoriteEssentialSites non-blocking
├── L2-T6: FinishOnboarding single method
└── L2-T7: Workspace E2E test

--- L2 GREEN GATE ---

Wave 8 (L3 batch A):
├── L3-T1: TS Mojo-generated constants
├── L3-T2: Store.ts state machine
└── L3-T4: Browser detector multi-profile UI

Wave 9 (L3 batch B, depends Wave 8):
├── L3-T3: 8-screen wizard
├── L3-T5: Per-type progress UI
├── L3-T6: FDA-retry button
└── L3-T7: QA script

--- L3 GREEN GATE ---

Wave 10 (L4 batch A):
├── L4-T1: Animations
├── L4-T2: Theme picker backend wiring
└── L4-T3: Welcome toast

Wave 11 (L4 batch B, depends Wave 10):
├── L4-T4: Localization
├── L4-T5: Accessibility
└── L4-T6: Final acceptance gate

--- L4 GREEN GATE = TIER A COMPLETE ---

Critical Path: L0-T1 → L0-T4 → L1-T1 → L2-T1 → L2-T4 → L3-T1 → L3-T3 → L4-T4 → L4-T6


---

## 35-Item Acceptance Checklist (Tier A Complete = All 35 ✓)

### Functional (11)
1. ☐ Import from Chrome (Default profile) — bookmarks, history, passwords, cookies, autofill, favicons all present in Maho
2. ☐ Import from Chrome (non-Default profile) — profile selector works
3. ☐ Import from Firefox — NSS-decrypted passwords correct across all 4 variants
4. ☐ Import from Zen — 3 workspaces appear as 3 space dots in sidebar
5. ☐ Import from Zen — 5-level folder nesting preserved in sidebar tree
6. ☐ Import from Zen — 12 essential sites appear in favorites grid
7. ☐ Import from Arc — spaces + sidebar items mapped correctly
8. ☐ Safari import with FDA granted — bookmarks imported
9. ☐ Safari import without FDA — EPERM error + retry button works
10. ☐ FinishOnboarding — main browser opens, toast appears, welcome closes (no double-close)
11. ☐ **Migration Dialog** (L3-T15) — opens on "Import now" click, shows source browsers + per-data-type checkboxes including "Spaces & Tabs" for Zen/Arc, dispatches StartImport with correct bitmask, auto-closes on completion

### Behavioral (5)
12. ☐ `complete=true` fires exactly once per import session
13. ☐ Per-type progress updates independently (not batched)
14. ☐ Cancellation mid-import stops pending workers (Cancel button in Migration Dialog)
15. ☐ No UI thread blocking during import (responsive during 1000-bookmark import)
16. ☐ No PII in any `LOG()` output during import

### Visual Parity (12) — pixel-level vs Zen reference
17. ☐ Window initial size 875×560 centered (L3-T8)
18. ☐ Splash title 5rem/500/Junicode + spans stagger 0.6s + spring (300/20/1.8) — visual diff <5% (L3-T8)
19. ☐ Shell layout 60×60% with 1em radius and `--maho-big-shadow`; responsive at 4 breakpoints (L3-T9)
20. ☐ Page transitions slide x:150% with spring bounce:0.2/0; ±50ms timing (L3-T9)
21. ☐ Import screen — Stage A (CTA + Skip) → click Import opens dialog → Stage B (default-browser radio cards 50% width, scale:1.03 on checked) — copy verbatim (L3-T10 + L3-T15)
22. ☐ Search engine — 2-col grid, 5rem padding, aspect 1/0.7 cards (L3-T11)
23. ☐ Essentials mock — 70×80%, top-left radius 1.2em, 9 apps + 2 placeholders, scale:1.06 selected (L3-T12)
24. ☐ Theme — gradient panel reused, 5×8 swatches, opacity slider, `getGradient()` algorithms match (L3-T13)
25. ☐ Completion — heart pulse `[0,1,1,1,0]/[0.5,1,1.2,1,1.2]` over 1.5s+0.2s delay (L3-T14)
26. ☐ Final sequence — slide right → heart → fade overlay → reveal chrome → toast (L3-T14)
27. ☐ Branding colors derived via `color-mix()` from `--maho-primary-color` (L4-T8)
28. ☐ Visual regression test passes: 8 screen captures within 5% pixel diff vs Zen reference (L4-T7)

### Security (3)
29. ☐ Plaintext keys zeroed after use (SecureBuffer RAII)
30. ☐ No ciphertext exposed as plaintext cookie value
31. ☐ Cookie DB copied to temp (no WAL lock on live browser)

### Test (2)
32. ☐ All unit tests pass: `unit_tests --gtest_filter='*Importer*:*Welcome*:*Orchestrator*:*FFIClient*:*NSSDecrypt*:*CookieParser*:*PasswordParser*:*WorkspaceParser*:*BrowserDetector*:*BookmarkParser*:*HistoryParser*:*AutofillParser*:*FaviconParser*:*MozLz4*:*SidebarParser*:*ImporterFixture*:*MahoMigrationDialog*'`
33. ☐ Golden-hash gate passes: `verify_golden.py` exits 0

### Architecture (2)
34. ☐ Zero manual `IMPORT_*` constants in TS (all from Mojo)
35. ☐ Page handler has zero direct store/parser calls (all via orchestrator); Migration Dialog dispatches via `OpenMigrationDialog` Mojo only

---

## Final Gate Commands

```bash
# 1. Rust workspace check
bun run typecheck && bun run test

# 2. Golden fixtures
python3 maho-chromium/browser/importer/test_data/verify_golden.py

# 3. Build
python3 maho-chromium/build/scripts/build_maho.py --ninja-target unit_tests

# 4. Unit tests (full importer + welcome suite)
./chromium/src/out/Default/unit_tests --gtest_filter='*Importer*:*Welcome*:*Orchestrator*:*FFIClient*:*NSSDecrypt*:*CookieParser*:*PasswordParser*:*WorkspaceParser*:*BrowserDetector*:*BookmarkParser*:*HistoryParser*:*AutofillParser*:*FaviconParser*:*MozLz4*:*SidebarParser*:*ImporterFixture*:*FinishOnboarding*:*WorkspaceImportE2E*'

# 5. TypeScript (welcome frontend)
cd maho-chromium/browser/resources/maho_welcome && npx tsc --noEmit

# 6. Architecture assertions
test "$(grep -rn 'IMPORT_BOOKMARKS\|IMPORT_PASSWORDS\|IMPORT_HISTORY' maho-chromium/browser/resources/maho_welcome/ | grep -cv 'generated')" -eq 0
test "$(grep -cn 'HistoryService\|PasswordStore\|BookmarkModel\|CookieManager' maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc)" -eq 0

# 7. Security assertions
test "$(grep -rn 'LOG.*http\|LOG.*@\|LOG.*\.com' maho-chromium/browser/importer/ | grep -cv '_test\.')" -eq 0

# 8. Build-clean with warnings-as-errors
autoninja -C out/Default chrome 2>&1 | grep -c 'error:' | xargs test 0 -eq


All 8 commands must exit 0. Zero test failures. Zero PII leaks. Zero manual constants.

---

## Open Questions (User-Input Gates Before Tier B)

1. **Cookie scope**: v20 App-Bound cookies are skipped with warning in Tier A. Import them in Tier B or leave permanently unsupported?
2. **Tier B priority order**: Mods / Split View / Glance / Compact / Containers / NTP — which ships first?
3. **Migration vs onboarding framing**: Should welcome flow rebrand as "Switch from Zen" with migration-specific copy?
4. **Real-fixture extraction policy**: Extract from dev machine + sanitize + commit (default), or separate CI-only test environment?
5. **Extension import scope**: `kImportExtensions` defined in L0-T1 but no importer in Tier A. Build in Tier B, or fast-follow after Tier A?
6. **Multi-profile import**: Import from ALL detected profiles simultaneously, or one-at-a-time with user selection? (Current plan: user selects one)


