# Desktop Maho Mail Commercial-Usability Review & Resolution Audit

**Original Review Date:** 2026-09-06
**Audit Correction Date:** 2026-09-08
**Auditor:** Maho Browser Core & Product Engineering Team
**Scope:** `chrome://maho-mail` (React WebUI), `maho/mail-core` (Rust engine & domain storage), `maho-chromium/browser/mail_helper/` (C++ sandbox helper & arbitration), Mail Mojo IPC / handlers, Agent Mail tools (`MahoBrowserMainExtraParts` & `MahoMcpSession`), and Settings / notifications / badges integration.

---

## 1. Executive Summary & Readiness Verdict

A commercial-usability review of the desktop Maho Mail feature set was conducted across 601 participating codebase paths. Confirmed in-scope defect resolutions were implemented and validated against targeted unit and integration suites. However, whole-product commercial readiness cannot be claimed: end-to-end guest VM execution remains blocked, no real scenario matrix or screenshots were produced, and external-provider mail delivery remains unverified.

### 1.1 Historical Verification Results (2026-09-06 Snapshot)
The following test counts reflect historical component-level test runs as of 2026-09-06. These establish unit- and bridge-level regression coverage for specific defects, but do not prove current integrated, end-to-end product readiness:
- **Rust `mail-core` Workspace:** 398 tests passed, 0 failed, 0 ignored (`plan/mail-commercial-review-20260905/evidence/lead-rust-all-green.log`).
- **React WebUI Vitest Suite:** 131 test files, 992 tests passed, 0 failed (`plan/mail-commercial-review-20260905/evidence/webui-all-green.log`). Isolated hook pagination and transport suites pass (e.g., 23 tests across 4 test files in `.omo-task-artifacts/resume-perf-mail-20260908/mail-current-tests.json`).
- **Native Integration Suite (`chromium/src/out/Default`):**
  - `maho_mail_attachment_registry_test`: 16/16 passed (exit 0)
  - `maho_mail_notification_coordinator_test`: 15/15 passed (exit 0)
  - `maho_sidebar_mail_badge_test`: 5/5 passed (exit 0)
  - `maho_mail_read_bridge_test`: 4/4 passed (exit 0)
  - `maho_mail_service_test`: 45 passed
- **Tool Catalog Parity:** 4/4 passed (`test_mail_tool_catalog_parity.py`).
- **TLS Protocol Loopback Fixture:** 5/5 passed (`maho/appium-tests/desktop-mail/tls-mail-protocol.test.js`); guest loopback fixture preparation and teardown pass in isolation (`plan/mail-commercial-review-20260905/evidence/lead-vm-fixture-direct-qa.log`), but do not launch the browser or exercise UI.

### 1.2 Dated Correction (2026-09-08): Runtime E2E Execution BLOCKED
Source inspection of the guest automation runner (`maho/appium-tests/desktop-mail/run-mail-cli-vm.py`)—not a newly captured guest execution receipt—and re-examination of prior Tart VM records establish that desktop runtime verification remains **BLOCKED** and full scenario proof is **UNVERIFIED**:
1. **Source Inspection of Runner Script:** Examination of `run-mail-cli-vm.py` reveals that only initial CLI discovery commands are implemented (`summary["actions"].append(...)`). The `status` field is initialized to `"BLOCKED"` and never promoted. The `screenshots` list is initialized to empty and is never populated anywhere in the runner. The normal `main()` path unconditionally returns code 2 (`return 2`).
2. **Prior Recorded VM Teardown Results:** Prior recorded results from the Tart macOS VM run (`macos_vm_e2e.py`, clone `maho-e2e-55797b76b99f45ce`) recorded `teardown_result.succeeded = true`. These prior records document host/guest resource provisioning and clean teardown; successful teardown does not constitute proof of a passing end-to-end scenario.
3. **Full Scenario Workflows Unverified:** Full lifecycle workflows (account setup, mailbox synchronization, email composition, attachment handling, and Agent panel interaction) lack an executed scenario matrix and visual screenshot receipts. Full scenario proof remains unsupported and UNVERIFIED.
4. **Prior Recorded Host/Runtime Blockers:**
   - Headless macOS keychain non-interactive trust restriction prevents headless installation of loopback TLS root certificates.
   - CLI automation routes for right-panel agent opening and snapshotting remain missing from the CLI catalog.
   - Settings agent snapshot selectors, typed approval flows, and helper shutdown events remain unverified in the guest runtime.

### 1.3 Readiness Verdict Boundaries
- **Local Engine, Storage, WebUI & Native Component Contracts:** Verified at isolated unit/component test boundaries per historical regression suites.
- **Desktop Runtime & Guest VM E2E Workflows:** **BLOCKED / UNVERIFIED**. Source inspection of `run-mail-cli-vm.py` shows only discovery implemented, screenshots never populated, BLOCKED status never promoted, and normal main path returns 2. Prior recorded VM teardown does not prove full scenario completion.
- **External Real-World Mail Delivery:** **UNVERIFIED**. External-provider delivery was not exercised.

---

## 2. Key Confirmed Defect Resolutions

The following defect resolutions remain preserved as historical code fixes verified by targeted component test suites:

### 2.1 WebUI: Attachment Download Native Persistence Race (C-4)
- **Problem:** `EmailBodyView.tsx` previously displayed an instant success toast upon acquiring a one-time capability token, without waiting for the native backend to complete file writing. If writing failed or was delayed, users received false positive confirmation.
- **Fix:** Updated `EmailBodyView.tsx`, `react/api/index.ts`, and `maho_mail.mojom-webui.js.d.ts` to await `api.saveAttachment(capabilityToken)` and surface explicit error toasts if saving fails. Verified by `AttachmentDownload.test.tsx` (3/3 passing).

### 2.2 Agent / MCP: Mail Flag Mutation Dispatch (AG-05)
- **Problem:** `MahoMcpBrowserDelegateImpl` in `maho_browser_main_extra_parts.cc` lacked an implementation for `MailFlag`, causing agent `mail_flag` calls (`mark_read`, `mark_unread`, `toggle_star`) to fail with generic execution errors.
- **Fix:** Implemented `MahoMcpBrowserDelegateImpl::MailFlag` to route flag mutations through `MahoMailService` and validate action strings, returning `mail_invalid_flag_action` for unsupported actions.

### 2.3 Agent / MCP: Safe Approval Metadata & Prompt Re-check (AG-03)
- **Problem:** Internal MCP mail write operations lacked structured transactional context in approval dialogs, and changes to policy during prompt presentation were not checked prior to execution.
- **Fix:** Implemented `BuildSafeMailApprovalMetadata` in `maho_mcp_session.cc` to extract safe transaction fields (`to`, `subject`, `action`, `attachment_count`) while scrubbing bodies and secrets. Enforced post-approval re-authorization check against `AuthorizeMailTool`.

### 2.4 Engine & Storage: Safe IMAP UID Expunge (ST-S03)
- **Problem:** Deleting or moving an email on an IMAP server could trigger an unqualified `EXPUNGE`, inadvertently purging unrelated messages marked `\Deleted` by other clients.
- **Fix:** Replaced blanket `EXPUNGE` with selective UID expunge commands (`UID EXPUNGE <uid>`) supported by RFC 4315, with safe fallback preserving external message states.

### 2.5 Security: Print Sanitization & Remote Image Isolation (C-1 / C-2)
- **Problem:** Generating print HTML previously bypassed DOMPurify and allowed unescaped HTML injection into a new browser window inheriting privileged WebUI context.
- **Fix:** Sanitized all print bodies with DOMPurify, stripped data/script URIs, and preserved tracker-blocking proxies during print formatting.

---

## 3. Success Criteria Verification Matrix

| Success Criteria | Target Requirement | Measured Outcome | Status |
|---|---|---|---|
| **SC1: Shipped Audit Scope** | Complete inventory of all 601 paths into lanes; findings logged; blockers reported | Full lane taxonomy in `path-to-lane.tsv`; 45 gap paths reviewed in `gap-path-review.md`. | **PASSED** |
| **SC2: Client Suites** | WebUI vitest + Rust mail-core test pass; RED/GREEN receipts recorded | Vitest: 131/131 files (992 tests passed). Rust: 398/398 passed. Historical component suites passed. | **PASSED** (Component Level) |
| **SC3: Native Contracts** | Catalog parity pass; narrow Chromium binaries built & verified | Catalog: 4/4 pass. Attachment (16), Notification (15), Badge (5), ReadBridge (4) all pass. | **PASSED** (Component Level) |
| **SC4: VM E2E & Teardown** | Built Maho.app tested in Tart VM; receipts saved; resources cleaned up | Prior recorded results show VM harness provisioned and torn down with 0 leaks (`teardown_result.succeeded = true`). Source inspection of `run-mail-cli-vm.py` shows only discovery implemented, screenshots never populated, BLOCKED never promoted, and normal main returns 2. Full scenario proof was not recorded; successful teardown does not establish a passing scenario. | **BLOCKED / UNVERIFIED** |

---

## 4. Concrete External / Host Blockers Recorded

1. **Guest VM Runner Implementation Status (`run-mail-cli-vm.py`):**
   - Source inspection (not a newly captured guest execution run) shows only discovery commands implemented.
   - Status is initialized to `"BLOCKED"` and never promoted.
   - The screenshots list is initialized to empty and is never populated by the runner.
   - Normal `main()` execution unconditionally returns code 2 (`return 2`).
   - Settings agent snapshot selectors, typed approval flows, and helper shutdown events remain unverified at runtime.
2. **Prior Recorded Headless macOS Keychain Non-Interactive Trust Denial:**
   - Command `/usr/bin/security add-trusted-cert -r trustRoot -k /Users/admin/Library/Keychains/login.keychain-db <cert>` was previously recorded failing with `SecTrustSettingsSetTrustSettings: The authorization was denied since no user interaction was possible.`
   - In headless Tart macOS VMs, modifying root trust settings in system keychains requires either GUI authorization or pre-trusting the CA directly in the base golden image (`maho-e2e-golden`).
3. **CLI Route Availability for Right-Panel Agent:**
   - The CLI automated catalog does not currently expose a direct command to open and snapshot the right-side agent panel without user interface click coordination.
4. **External Real-Account Delivery Distinction:**
   - External-provider delivery was not exercised. Live IMAP/SMTP communication with real external mail providers (e.g. Gmail, Outlook) remains unexecuted and separate from local guest runner blockers.

---

## 5. Conclusion & Evidence Lineage

### 5.1 Evidence File References
- **Historical Component Results:**
  - Rust workspace: `plan/mail-commercial-review-20260905/evidence/lead-rust-all-green.log`
  - WebUI vitest: `plan/mail-commercial-review-20260905/evidence/webui-all-green.log`
  - Unit pagination/transport hooks: `.omo-task-artifacts/resume-perf-mail-20260908/mail-current-tests.json`
  - Fixture QA: `plan/mail-commercial-review-20260905/evidence/lead-vm-fixture-direct-qa.log`
- **Audit Plans & Checkpoints:**
  - `plan/mail-commercial-review-20260905/completion-audit.md`
  - `plan/mail-commercial-review-20260905/lead-vm-fixture-audit.md`
  - `.omo-task-artifacts/resume-perf-mail-20260908/completion-audit.md`
- **Current Runtime Runner:**
  - `maho/appium-tests/desktop-mail/run-mail-cli-vm.py` (source inspection: status `BLOCKED` never promoted, screenshots never populated, unconditional `return 2`)

### 5.2 Updated Verdict
Commercial readiness and full runtime verification cannot be certified at this time. While local engine, storage, WebUI, and native contract unit suites passed historically in isolation, end-to-end desktop execution in the Tart macOS VM remains UNVERIFIED and BLOCKED: source inspection of `run-mail-cli-vm.py` shows only discovery implemented, screenshots never populated, BLOCKED status never promoted, and the normal main path unconditionally returns 2. Prior recorded VM teardown proves resource hygiene, but does not prove functional desktop usability. External-provider delivery was not exercised. Full readiness claims are downgraded to UNVERIFIED and BLOCKED until guest scenarios execute to completion with visual screenshot evidence.
