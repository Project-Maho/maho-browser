# PROJECT KNOWLEDGE BASE — browser/mail_helper

**Generated:** 2026-08-30

## OVERVIEW
Mail helper subsystem: profile-keyed browser service (`MahoMailService`), out-of-process helper process (`maho_mail_helper`), Mojo IPC interface (`maho_mail_helper.mojom`), attachment registry, unread badge calculation, and notification coordinator.

## STRUCTURE
```
browser/mail_helper/
├── BUILD.gn                                      # GN build configuration for service and helper binary
├── maho_mail_service.h/.cc                       # Profile-keyed KeyedService managing accounts and IPC
├── maho_mail_service_factory.h/.cc               # KeyedServiceFactory instantiating MahoMailService
├── maho_mail_helper_launcher.h/.cc               # Process lifecycle supervisor spawning and monitoring helper
├── maho_mail_helper_main.cc                      # Out-of-process helper binary main entry point
├── maho_mail_helper.mojom                        # Mojo IPC contract between browser process and helper
├── maho_mail_attachment_registry.h/.cc           # Capability-token registry for downloaded attachments
├── maho_mail_notification_coordinator.h/.cc      # OS notification coordinator & UIDVALIDITY watermark tracker
├── maho_mail_badge.h/.cc                         # Unread count calculations for dock and sidebar badges
├── maho_mail_dock_badge_mac.h/.mm                # macOS native dock badge implementation
├── maho_mail_notification_permission.h/.cc       # Platform notification permission manager
├── maho_mail_notification_permission_mac.mm      # macOS notification permission bridge
├── maho_mail_notification_permission_win.cc      # Windows notification permission bridge
├── maho_mail_read_bridge.h/.cc                   # Thread-safe JSON reader bridge to Rust mail-core callbacks
├── maho_mail_arbitration_service.h/.cc           # Mail sync arbitration service
├── maho_mail_helper_sandbox_linux.h/.cc          # Linux seccomp sandbox policy for helper executable
├── maho_mail_helper_version.h                    # Mail helper IPC protocol version definitions
└── *_unittest.cc / *_test_main.cc                # Subsystem unit and sandbox test suites
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Account management & IPC dispatch | `maho_mail_service.cc` | Profile KeyedService; manages active connections |
| Helper process lifecycle | `maho_mail_helper_launcher.cc` | Process spawning, health monitoring, restart backoff |
| Out-of-process helper daemon | `maho_mail_helper_main.cc` | Hosts IMAP/SMTP protocols, SQLCipher storage, and MIME engines |
| Attachment access & tokens | `maho_mail_attachment_registry.cc` | Capability-based token issuance and file access control |
| Notification watermark tracking | `maho_mail_notification_coordinator.cc` | Persists watermarks across UIDVALIDITY epochs |
| Unread badge updates | `maho_mail_badge.cc`, `maho_mail_dock_badge_mac.mm` | Drives dock and sidebar badge numbers |
| Rust mail-core callback bridge | `maho_mail_read_bridge.cc` | Marshals async JSON responses safely to UI sequence |

## CONVENTIONS
- **Out-of-Process Quarantining**: Heavy mail protocol operations (IMAP, SMTP, OAuth, MIME decoding) and cryptographic key storage execute strictly in the isolated `maho_mail_helper` process.
- **Thread Safety**: UI sequence methods must include `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)`. Asynchronous IPC callbacks must use `base::WeakPtr` to avoid use-after-free upon profile teardown.
- **Profile Eligibility & Lifecycle**: `MahoMailService` is instantiated via `MahoMailServiceFactory` only for eligible regular user profiles when `maho.mail.enabled` is active. Off-The-Record (OTR), Guest, and Incognito profiles are never initialized.
- **Capability Tokens for Attachments**: Attachment payloads are mediated through `MahoMailAttachmentRegistry` using transient capability tokens; direct arbitrary file paths are never exposed to WebUI.
- **UIDVALIDITY Epoch Safety**: `MahoMailNotificationCoordinator` must validate UIDVALIDITY epochs before posting notifications or recording watermarks to prevent duplicate or stale alerts.

## ANTI-PATTERNS
- **No Direct Mail Protocols on UI Sequence**: Never run IMAP/SMTP networking or MIME decoding in the main browser process.
- **No Cleartext Credentials in Logs or Memory**: Passphrases, tokens, and encrypted envelopes must remain redacted and zeroed after transmission.
- **No Mail Services for OTR Profiles**: Never instantiate or bind `MahoMailService` in Incognito or Guest profiles.
- **No Unsupervised Helper Spawning**: Helper processes must only be launched via `MahoMailHelperLauncher` to maintain crash detection and restart throttling.

## COMMANDS
```bash
# Canonical locked build for Mail Service and helper targets (run from repo root)
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target maho_mail_service --ninja-target maho_mail_launcher --ninja-target maho_mail_helper

# Build narrow test targets via locked wrapper
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target maho_mail_service_test --ninja-target maho_mail_attachment_registry_test --ninja-target maho_mail_notification_coordinator_test --ninja-target maho_mail_read_bridge_test

# Run subsystem unit test binaries (after locked build)
./chromium/src/out/Default/maho_mail_service_test
./chromium/src/out/Default/maho_mail_attachment_registry_test
./chromium/src/out/Default/maho_mail_notification_coordinator_test
./chromium/src/out/Default/maho_mail_read_bridge_test

# Run Mail contract and source verification scripts
python3 maho-chromium/build/scripts/test_mail_tool_catalog_parity.py
python3 maho-chromium/build/scripts/test_mail_notification_coordinator_source.py
python3 maho-chromium/build/scripts/test_mail_service_badge_source.py
```
