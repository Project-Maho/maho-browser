# Tier A — Maho Zen Parity Correctness Fixes

**Provenance**: Plan derived from hyperplan adversarial review (5 members: skeptic / validator / researcher / architect / creative; 2 rounds completed; Round 3 skipped due to peer-message visibility degradation) and formalized by the `plan` agent on 2026-06-10. Tier B (feature-surface expansion: Mods / Split View / Glance / Compact / Containers / NTP) is intentionally excluded — separate plan to follow after Tier A acceptance.

**Reference Documents**:
- Failed v1 plan: `.sisyphus/plans/maho-welcome-zen-true-parity-2026-06-10.md`
- Existing impl: `maho-chromium/browser/importer/`, `maho-chromium/browser/ui/webui/maho_welcome/`, `maho-chromium/browser/resources/maho_welcome/`
- AGENTS.md hierarchy: workspace root + `maho/` + `maho-chromium/`

---

## Layer 0: Protocol Foundation

### L0-T1: Mojo IDL Import Constants
- Deliverable: `maho-chromium/components/maho_importer/maho_importer_constants.mojom` with `const uint32 kImportBookmarks`, `kImportPasswords`, `kImportHistory`, `kImportCookies`, `kImportAutofill`, `kImportWorkspaces`, `kImportSidebar`, `kImportExtensions` — single source generating TS + C++ bindings
- Files: `maho-chromium/components/maho_importer/maho_importer_constants.mojom`, `maho-chromium/components/maho_importer/BUILD.gn`
- Success criteria: `autoninja -C out/Default maho_importer_mojo_bindings` succeeds; generated `.h` and `.ts` both export identical numeric values
- Verification command: `autoninja -C out/Default maho_importer_mojo_bindings && grep -r 'kImportBookmarks' chromium/src/gen/maho/components/`

### L0-T2: INVARIANTS.md + Enforcement
- Deliverable: `maho-chromium/browser/importer/INVARIANTS.md` enumerating:
  - INV-1: All NSS decrypt calls wrapped in `DCHECK(!content.empty())` pre-strip
  - INV-2: Threading — importer parsers run on `ImporterThread`; bridge dispatch posts to UI
  - INV-3: `MahoSpaceProfileBridge` callers must hold `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)`
  - INV-4: No parser returns success with zero items (empty-result = error)
  - INV-5: Cookie parser MUST check 3-byte prefix (`v10`/`v20`) before attempting decrypt
  - INV-6: Test gate — `test_data/` fixtures required for build
- Files: `maho-chromium/browser/importer/INVARIANTS.md`
- Success criteria: Each invariant has a corresponding `grep`-able DCHECK/LOG pattern in source
- Verification command: `grep -rn 'DCHECK_CALLED_ON_VALID_SEQUENCE' maho-chromium/browser/importer/ | wc -l` (must match consumer count)

### L0-T3: MahoSpaceProfileBridge Consumer Enumeration
- Deliverable: Documented list of all `bridge->` callsites in `maho-chromium/browser/` — classified as SIDEBAR / AI / BOOST / IMPORTER with file:line references appended to INVARIANTS.md §Consumers
- Files: `maho-chromium/browser/importer/INVARIANTS.md` (append)
- Success criteria: Every `bridge->` callsite in the tree has a DCHECK_CALLED_ON_VALID_SEQUENCE guard
- Verification command: `grep -rn 'bridge->' maho-chromium/browser/ | grep -v '_test\.' | grep -v INVARIANTS`

### L0-T4: Synthetic Test Fixture Set
- Deliverable: Binary/hex fixture files under `maho-chromium/browser/importer/test_data/synthetic/`:
  - `nss_3des_cbc.bin` — PKCS#12 → 3DES-CBC encrypted blob
  - `nss_aes256_cbc_sha256.bin` — AES-256-CBC with SHA-256 HMAC
  - `nss_aes256_cbc_sha384.bin` — AES-256-CBC with SHA-384 HMAC (variant C)
  - `nss_aes256_gcm.bin` — AES-256-GCM blob
  - `oscrypt_v10.bin` — Chromium OSCrypt v10 envelope
  - `cookie_v10_prefix.bin` — cookie blob with `v10` 3-byte prefix
  - `cookie_v20_prefix.bin` — cookie blob with `v20` 3-byte prefix (skip vector)
  - `golden_hashes.json` — SHA-256 of each fixture for regression gate
- Files: `maho-chromium/browser/importer/test_data/synthetic/*`, `maho-chromium/browser/importer/test_data/golden_hashes.json`
- Success criteria: `python3 -c "import json,hashlib; ..."` validates all fixtures match golden hashes
- Verification command: `python3 maho-chromium/browser/importer/test_data/verify_golden.py`

### L0-T5: Real-Fixture Extraction Tooling
- Deliverable: `maho-chromium/browser/importer/test_data/extract_fixtures.py` — reads dev-machine Chrome (`~/.config/google-chrome/`), Firefox (`~/.mozilla/firefox/`), Zen profiles; extracts encrypted blobs; strips PII (replaces usernames/URLs with synthetic); outputs to `test_data/real/`
- Files: `maho-chromium/browser/importer/test_data/extract_fixtures.py`
- Success criteria: Script runs without error on macOS dev machine; produces ≥1 fixture per browser type; no PII in output (grep for email/URL patterns = 0)
- Verification command: `python3 maho-chromium/browser/importer/test_data/extract_fixtures.py --dry-run`

### L0-T6: Codified Test Gate
- Deliverable: GN `action` target in `maho-chromium/browser/importer/BUILD.gn` that runs `verify_golden.py` and fails if `test_data/synthetic/` is missing or hash mismatch. Wired as dep of `unit_tests` importer test targets.
- Files: `maho-chromium/browser/importer/BUILD.gn`, `maho-chromium/browser/importer/test_data/verify_golden.py`
- Success criteria: `autoninja -C out/Default importer_test_gate` fails if any fixture deleted; passes with full set
- Verification command: `rm chromium/src/maho/browser/importer/test_data/synthetic/nss_3des_cbc.bin && autoninja -C out/Default importer_test_gate; echo $?` (expect nonzero)

### L0-T7: Golden-Hash Assertion Harness (C++)
- Deliverable: `importer_fixture_test.cc` — GTest that loads each fixture from `test_data/synthetic/`, computes SHA-256, asserts against `golden_hashes.json`. Ensures fixture integrity across git operations.
- Files: `maho-chromium/browser/importer/importer_fixture_test.cc`
- Success criteria: `unit_tests --gtest_filter='ImporterFixtureTest.*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ImporterFixtureTest.*'`

---

## Layer 1: Parser Refactor + Tests

### L1-T1: nss_decrypt_helper.{h,cc}
- TRIAGE: REFACTOR-IN-PLACE
- Bugs to fix:
  1. **Double-strip bug**: `DecryptNSSBlob()` strips ASN.1 header twice when input already has OID parsed — add length guard before second strip
  2. **Variant C SHA-384**: Use `kCCPRFHmacAlgSHA384` (CommonCrypto exposes it on macOS ≥10.15); v1 falsely claimed unavailable and fell through to SHA-256
  3. **Empty-input DCHECK**: Add `DCHECK(!encrypted_data.empty())` at entry (INV-1)
- Test deliverable: `nss_decrypt_helper_unittest.cc` — parameterized test over all 4 synthetic NSS fixtures; asserts known plaintext output; asserts empty-input returns error
- Success criteria: `unit_tests --gtest_filter='NSSDecryptHelper*'` passes all 4 variants + error case
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='NSSDecryptHelper*'`

### L1-T2: cookie_parser.{h,cc}
- TRIAGE: REFACTOR-IN-PLACE
- Bugs to fix:
  1. **Ciphertext-as-plaintext**: Parser returned raw ciphertext bytes as cookie value when decrypt failed silently — add explicit decrypt-result check; return error on failure
  2. **v10/v20 prefix detection**: Read first 3 bytes; `v10` → decrypt via `OSCryptKeyProvider`; `v20` → `LOG(WARNING) << "v20 App-Bound not supported"` + skip row
  3. **NULL value crash**: Add null-check on `sqlite3_column_blob` return before memcpy
- Test deliverable: `cookie_parser_unittest.cc` — tests: valid v10 decrypt, v20 skip-with-warning, corrupt blob error, NULL column handling
- Success criteria: `unit_tests --gtest_filter='CookieParser*'` all green; no ciphertext leaks in output
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='CookieParser*'`

### L1-T3: chromium_password_parser.{h,cc}
- TRIAGE: REFACTOR-IN-PLACE
- Bugs to fix:
  1. **sql::Database tag**: `sql::Database` constructor requires unique histogram tag — use `"MahoImporterChromiumPasswords"` not empty string
  2. **OSCrypt key provider integration**: Decrypt via shared `OSCryptKeyProvider` interface (same as cookie_parser)
  3. **Sequence check**: Add `DCHECK_CALLED_ON_VALID_SEQUENCE` at `Parse()` entry
- Test deliverable: `chromium_password_parser_unittest.cc` — tests: valid v10 decrypt, wrong-key error, empty DB returns empty vec (not error)
- Success criteria: `unit_tests --gtest_filter='ChromiumPasswordParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ChromiumPasswordParser*'`

### L1-T4: firefox_password_parser.{h,cc}
- TRIAGE: REFACTOR-IN-PLACE
- Bugs to fix:
  1. **NSS variant routing**: Detect OID in encrypted blob header → dispatch to correct NSSDecryptHelper variant (3DES / AES-CBC-SHA256 / AES-CBC-SHA384 / AES-GCM)
  2. **key4.db vs key3.db**: Handle both legacy key3.db (Berkeley DB) and key4.db (SQLite) — error clearly on key3.db with unsupported message
- Test deliverable: `firefox_password_parser_unittest.cc` — parameterized over 4 NSS variants; assert correct routing; assert key3.db returns descriptive error
- Success criteria: `unit_tests --gtest_filter='FirefoxPasswordParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='FirefoxPasswordParser*'`

### L1-T5: zen_workspaces_parser.{h,cc}
- TRIAGE: KEEP (verify correctness)
- Bugs to fix:
  1. **JSON parse error propagation**: Replace silent `return {}` with `LOG(ERROR)` + structured error
  2. **Container ID validation**: Validate `userContextId` is positive integer
- Test deliverable: `zen_workspaces_parser_unittest.cc` — valid JSON, malformed JSON, missing fields, negative userContextId
- Success criteria: `unit_tests --gtest_filter='ZenWorkspacesParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ZenWorkspacesParser*'`

### L1-T6: arc_sidebar_parser.{h,cc}
- TRIAGE: KEEP (verify correctness)
- Bugs to fix:
  1. **Empty sidebar graceful handling**: Return empty vector, not error, when sidebar section absent
  2. **URL validation**: Reject non-http(s) URLs in sidebar items
- Test deliverable: `arc_sidebar_parser_unittest.cc` — valid sidebar, empty sidebar, invalid URLs filtered
- Success criteria: `unit_tests --gtest_filter='ArcSidebarParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ArcSidebarParser*'`

### L1-T7: mozlz4_decompressor.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **Magic header check**: Validate `mozLz40\0` 8-byte magic before attempting decompress — return error not crash
  2. **Output size cap**: Reject decompressed output > 256MB (DoS guard)
- Test deliverable: `mozlz4_decompressor_unittest.cc` — valid decompress, wrong magic, oversized output rejection
- Success criteria: `unit_tests --gtest_filter='MozLz4Decompressor*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MozLz4Decompressor*'`

### L1-T8: chromium_history_parser.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **Timestamp conversion**: Chrome epoch (Jan 1 1601) → Unix epoch conversion — verify no off-by-one on DST boundaries
  2. **Duplicate URL dedup**: Deduplicate by URL, keep latest visit_time
- Test deliverable: `chromium_history_parser_unittest.cc` — timestamp conversion fidelity, dedup behavior, empty DB
- Success criteria: `unit_tests --gtest_filter='ChromiumHistoryParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ChromiumHistoryParser*'`

### L1-T9: firefox_history_parser.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **mozlz4 integration**: Places DB may be mozlz4-compressed in recovery files — detect and decompress
  2. **PRTime conversion**: Firefox PRTime (microseconds since epoch) → base::Time
- Test deliverable: `firefox_history_parser_unittest.cc` — normal places.sqlite, compressed variant, timestamp accuracy
- Success criteria: `unit_tests --gtest_filter='FirefoxHistoryParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='FirefoxHistoryParser*'`

### L1-T10: chromium_autofill_parser.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **Schema version check**: Handle both old `autofill` table and new `local_addresses`/`local_credit_cards` schema
  2. **sql::Database tag**: Use `"MahoImporterChromiumAutofill"`
- Test deliverable: `chromium_autofill_parser_unittest.cc` — old schema, new schema, empty DB
- Success criteria: `unit_tests --gtest_filter='ChromiumAutofillParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='ChromiumAutofillParser*'`

### L1-T11: safari_bookmark_parser.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **Binary plist handling**: Ensure `CFPropertyListCreateWithData` error path doesn't leak CFRef
  2. **Nested folder recursion depth**: Cap at 32 levels (stack overflow guard)
- Test deliverable: `safari_bookmark_parser_unittest.cc` — valid plist, deeply nested (33 levels rejected), corrupt plist error
- Success criteria: `unit_tests --gtest_filter='SafariBookmarkParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='SafariBookmarkParser*'`

### L1-T12: favicon_parser.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **Image size validation**: Reject favicons > 1MB or dimensions > 256×256
  2. **MIME sniffing**: Validate actual content matches expected image format
- Test deliverable: `favicon_parser_unittest.cc` — valid PNG, oversized rejection, format mismatch
- Success criteria: `unit_tests --gtest_filter='FaviconParser*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='FaviconParser*'`

### L1-T13: maho_bookmark_importer.{h,cc}
- TRIAGE: REFACTOR-IN-PLACE
- Bugs to fix:
  1. **Bridge sequence check**: Add `DCHECK_CALLED_ON_VALID_SEQUENCE` on all public methods
  2. **Duplicate bookmark detection**: Deduplicate by (URL + parent_folder) tuple
  3. **Progress callback**: Post progress to UI thread, not inline on importer thread
- Test deliverable: `maho_bookmark_importer_unittest.cc` — import dedup, progress ordering, empty input
- Success criteria: `unit_tests --gtest_filter='MahoBookmarkImporter*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoBookmarkImporter*'`

### L1-T14: maho_browser_detector.{h,cc}
- TRIAGE: KEEP
- Bugs to fix:
  1. **Zen detection path**: Verify `~/.zen/` profile path detection on Linux, `~/Library/Application Support/zen/` on macOS
  2. **Version parsing**: Handle non-semver version strings gracefully (Zen uses date-based)
- Test deliverable: `maho_browser_detector_unittest.cc` — detect Chrome/Firefox/Zen/Arc/Safari; handle missing browsers; version parsing edge cases
- Success criteria: `unit_tests --gtest_filter='MahoBrowserDetector*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoBrowserDetector*'`

### L1-T15: maho_workspace_importer.{h,cc}
- TRIAGE: REFACTOR-IN-PLACE
- Bugs to fix:
  1. **Bridge sequence check**: `DCHECK_CALLED_ON_VALID_SEQUENCE`
  2. **Zen workspace → Maho Space mapping**: Validate color/icon fields; fallback to defaults not crash
  3. **Arc space import**: Map Arc space structure to Maho equivalents
- Test deliverable: `maho_workspace_importer_unittest.cc` — Zen import, Arc import, missing optional fields use defaults, bridge thread assertion
- Success criteria: `unit_tests --gtest_filter='MahoWorkspaceImporter*'` passes
- Verification command: `autoninja -C out/Default unit_tests && ./chromium/src/out/Default/unit_tests --gtest_filter='MahoWorkspaceImporter*'`

---

## Risk → Mitigation Mapping

| Risk | Mitigation | Task |
|------|-----------|------|
| `kCCPRFHmacAlgSHA384` unavailable on CI macOS version | Runtime check + skip with `GTEST_SKIP()` on < 10.15 | L1-T1 |
| Real fixture extraction leaks PII | `extract_fixtures.py` regex-scrubs emails/domains + `--dry-run` mode | L0-T5 |
| Golden hash breaks on git line-ending normalization | Fixtures are binary (`.bin`), committed with `.gitattributes binary` | L0-T4 |
| OSCrypt key not available in test environment | Mock `OSCryptKeyProvider` with known test key in fixtures | L1-T2, L1-T3 |
| sql::Database locks (WAL mode) on copied profile DBs | Open in `SQLITE_OPEN_READONLY` mode in parsers | L1-T3, L1-T8, L1-T10 |
| Chromium upstream rebase breaks BUILD.gn additions | Keep all new targets in `maho-chromium/browser/importer/BUILD.gn` (overlay, not upstream) | L0-T6 |
| Test fixture set grows stale vs. browser updates | Golden-hash gate catches drift; `extract_fixtures.py` is re-runnable | L0-T5, L0-T6 |

---

## Atomic Commit Strategy

```
# Layer 0 commits (sequential, each buildable)
git commit -m "importer: add Mojo IDL constants for import types"          # L0-T1
git commit -m "importer: add INVARIANTS.md with enforcement contracts"     # L0-T2 + L0-T3
git commit -m "importer: add synthetic test fixtures + golden hashes"      # L0-T4
git commit -m "importer: add real-fixture extraction tooling"              # L0-T5
git commit -m "importer: add GN test gate + fixture integrity test"        # L0-T6 + L0-T7

# Layer 1 commits (one per parser, can be parallel branches merged sequentially)
git commit -m "importer: fix nss_decrypt_helper double-strip + SHA-384 + tests"
git commit -m "importer: fix cookie_parser v10/v20 prefix + ciphertext leak + tests"
git commit -m "importer: fix chromium_password_parser sql tag + tests"
git commit -m "importer: fix firefox_password_parser variant routing + tests"
git commit -m "importer: add zen_workspaces_parser tests + error propagation"
git commit -m "importer: add arc_sidebar_parser tests + URL validation"
git commit -m "importer: add mozlz4_decompressor tests + magic check"
git commit -m "importer: add chromium_history_parser tests + timestamp fix"
git commit -m "importer: add firefox_history_parser tests + mozlz4 integration"
git commit -m "importer: add chromium_autofill_parser tests + schema version"
git commit -m "importer: add safari_bookmark_parser tests + recursion cap"
git commit -m "importer: add favicon_parser tests + size validation"
git commit -m "importer: fix maho_bookmark_importer sequence + dedup + tests"
git commit -m "importer: add maho_browser_detector tests + Zen path"
git commit -m "importer: fix maho_workspace_importer sequence + mapping + tests"
```

Each commit: `bun run typecheck && bun run test && autoninja -C out/Default unit_tests` must pass.

---

## Effort Estimate

| Layer | Tasks | Parallel Waves | Estimated Duration |
|-------|-------|---------------|-------------------|
| L0 | 7 tasks | 2 waves (T1-T3 parallel, T4-T7 parallel after) | 2 days |
| L1 | 15 parsers | 3 waves of 5 (parsers are independent) | 4 days |
| **Total** | 22 tasks | — | **6 days** |

---

## Parallel Execution Graph

```
Wave 1 (L0, no deps):
├── L0-T1: Mojo IDL constants
├── L0-T2: INVARIANTS.md
└── L0-T3: Bridge consumer enumeration

Wave 2 (L0, depends on Wave 1 for invariant definitions):
├── L0-T4: Synthetic fixtures
├── L0-T5: Extraction tooling
├── L0-T6: GN test gate (depends L0-T4)
└── L0-T7: Golden-hash harness (depends L0-T4)

Wave 3 (L1 batch A, depends on L0 complete):
├── L1-T1: nss_decrypt_helper
├── L1-T2: cookie_parser
├── L1-T3: chromium_password_parser
├── L1-T4: firefox_password_parser
└── L1-T5: zen_workspaces_parser

Wave 4 (L1 batch B, independent of Wave 3):
├── L1-T6: arc_sidebar_parser
├── L1-T7: mozlz4_decompressor
├── L1-T8: chromium_history_parser
├── L1-T9: firefox_history_parser
└── L1-T10: chromium_autofill_parser

Wave 5 (L1 batch C, independent of Wave 4):
├── L1-T11: safari_bookmark_parser
├── L1-T12: favicon_parser
├── L1-T13: maho_bookmark_importer
├── L1-T14: maho_browser_detector
└── L1-T15: maho_workspace_importer

Critical Path: L0-T1 → L0-T4 → L0-T6 → L1-T1 → (done)
```

---

## Final Gate

All of the following must pass on the completed branch:

```bash
bun run typecheck
bun run test
python3 maho-chromium/browser/importer/test_data/verify_golden.py
autoninja -C out/Default unit_tests
./chromium/src/out/Default/unit_tests --gtest_filter='*Importer*:*NSSDecrypt*:*CookieParser*:*PasswordParser*:*WorkspaceParser*:*BrowserDetector*:*BookmarkParser*:*HistoryParser*:*AutofillParser*:*FaviconParser*:*MozLz4*:*SidebarParser*:*ImporterFixture*'
```

Zero test failures. Zero `LOG(ERROR)` regressions in existing tests. Build-clean with `-Werror`.

---

## Open Questions (User-Input Gates Before Tier B)

1. **Cookie scope in Tier A** — INCLUDED per L1-T2 above. Reaffirm or relax (v1 also failed here).
2. **Tier B priority order** — Mods / Split View / Glance / Compact / Containers / NTP. Which ships next after Tier A acceptance?
3. **Migration vs onboarding framing** — should welcome flow rebrand as "Switch from Zen" with migration copy?
4. **Real-fixture extraction policy** — extract from dev machine + sanitize+commit (L0-T5 default), or separate test environment?
