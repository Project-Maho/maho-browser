# PROJECT KNOWLEDGE BASE — browser/importer

**Generated:** 2026-06-15

## OVERVIEW
Browser data import subsystem. Imports bookmarks, history, passwords, cookies, autofill, favicons, and spaces/tabs from Chrome, Firefox (with NSS decrypt), Safari, Arc, and Zen. ~30 files + INVARIANTS.md. Read INVARIANTS.md before modifying.

## STRUCTURE
```
browser/importer/
├── INVARIANTS.md           # Safety invariants — READ FIRST
├── ACCEPTANCE.md           # Tier A acceptance checklist (~35 criteria)
├── import_orchestrator.cc  # Top-level coordinator, ONLY caller of OnImportProgress(complete=true)
├── importer_base.h         # Base interface
├── maho_browser_detector.cc # Profile enumeration for Chrome/Firefox/Safari
├── importers/              # Per-source importers (subdirectory)
├── *_parser.cc/h           # Stateless data parsers: arc, chromium, firefox, safari, cookie, favicon, nss, ...
├── maho_space_ffi_client.cc # FFI bridge to maho-core for space/folder creation
└── test_data/              # Fixture data for unit tests
```

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add new browser source | New `*_parser.cc/h` + register in `maho_browser_detector.cc` |
| Import orchestration flow | `import_orchestrator.cc` |
| Firefox password decrypt | `nss_decrypt_helper.cc` (NSS 3/4 SHA-1/SHA-384 paths) |
| Zen workspace import | `zen_workspaces_parser.cc` + `maho_space_ffi_client.cc` |
| Arc sidebar import | `arc_sidebar_parser.cc` |
| Welcome flow onboarding import | `maho_space_ffi_client.cc::FavoriteEssentialSites` (standalone path, no orchestrator) |
| Cookie decryption | `cookie_parser.cc` — checks `v10`/`v20` prefix (INV-5) |

## INVARIANTS (summary — read INVARIANTS.md for full spec)
- Parsers run on `ImporterThread`; bridge/orchestrator posts to UI thread (INV-2)
- FFI calls to `maho_core_handle_event` MUST be on UI thread (INV-8)
- `ImportOrchestrator` is the ONLY caller of `OnImportProgress(complete=true)` (INV-7)
- Cookie parser MUST check 3-byte prefix before decrypt (INV-5)
- No parser returns success with zero items (INV-4)
- Plaintext key material zeroed after use via RAII SecureBuffer or `memset_s` (INV-9)

## ANTI-PATTERNS
- No direct FFI calls from parsers — go through orchestrator/bridge
- No calling `OnImportProgress(complete=true)` outside `ImportOrchestrator`
- No bypassing NSS path detection in `nss_decrypt_helper.cc`
- No modifying `test_data/` fixtures without updating matching unit tests
