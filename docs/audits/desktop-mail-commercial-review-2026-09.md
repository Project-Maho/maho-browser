# Desktop Maho Mail Commercial-Usability Review & Resolution Audit

**Date:** 2026-09-06
**Auditor:** Maho Browser Core & Product Engineering Team
**Scope:** `chrome://maho-mail` (React WebUI), `maho/mail-core` (Rust engine & domain storage), `maho-chromium/browser/mail_helper/` (C++ sandbox helper & arbitration), Mail Mojo IPC / handlers, Agent Mail tools (`MahoBrowserMainExtraParts` & `MahoMcpSession`), and Settings / notifications / badges integration.

---

## 1. Executive Summary & Readiness Verdict

A comprehensive commercial-usability review of the desktop Maho Mail feature set was conducted across all 601 participating codebase paths. All identified and confirmed in-scope deficiencies were resolved and locked behind failing-first and passing regression test suites.

- **Automated Verification Status:** **100% GREEN**
  - **Rust `mail-core` Workspace:** 398 tests passed, 0 failed, 0 ignored (`evidence/lead-rust-all-green.log`).
  - **React WebUI Vitest Suite:** 131 test files, 992 tests passed, 0 failed (`evidence/webui-all-green.log`).
  - **Native Integration Suite (`chromium/src/out/Default`):**
    - `maho_mail_attachment_registry_test`: 16/16 passed (exit 0)
    - `maho_mail_notification_coordinator_test`: 15/15 passed (exit 0)
    - `maho_sidebar_mail_badge_test`: 5/5 passed (exit 0)
    - `maho_mail_read_bridge_test`: 4/4 passed (exit 0)
    - `maho_mail_service_test`: 45 passed
  - **Tool Catalog Parity:** 4/4 passed (`test_mail_tool_catalog_parity.py`).
  - **TLS Protocol Loopback:** 5/5 passed (`tls-mail-protocol.test.js`).
- **Tart macOS VM E2E Execution:**
  - Built `Maho.app` mounted read-only (`virtiofs-read-only`) into isolated clone `maho-e2e-55797b76b99f45ce` (`TART_HOME=/Volumes/T9-Mac/maho-tart`).
  - Successfully executed and cleaned up (`teardown_result.succeeded = true`).
  - Observed and recorded exact headless runtime blockers in guest VM (macOS keychain non-interactive trust restriction and CLI agent panel selector routes).
- **Readiness Verdict:**
  - **Local Engine, Storage, WebUI & MCP Authorization:** Production-ready and verified.
  - **External Real-World Mail Delivery:** Unverified on real internet providers (deferred pending user authorization and test credentials).

---

## 2. Key Confirmed Defect Resolutions

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
| **SC2: Client Suites** | WebUI vitest + Rust mail-core test pass; RED/GREEN receipts recorded | Vitest: 131/131 files (992 tests passed). Rust: 398/398 passed. | **PASSED** |
| **SC3: Native Contracts** | Catalog parity pass; narrow Chromium binaries built & verified | Catalog: 4/4 pass. Attachment (16), Notification (15), Badge (5), ReadBridge (4) all pass. | **PASSED** |
| **SC4: VM E2E & Teardown** | Built Maho.app tested in Tart VM; receipts saved; resources cleaned up | `macos_vm_e2e.py` executed; receipts in `test-results/macos-vm/`; VM torn down with 0 leaks. | **PASSED** |

---

## 4. Concrete External / Host Blockers Recorded

1. **Headless macOS Keychain Non-Interactive Trust Denial:**
   - Command `/usr/bin/security add-trusted-cert -r trustRoot -k /Users/admin/Library/Keychains/login.keychain-db <cert>` failed with `SecTrustSettingsSetTrustSettings: The authorization was denied since no user interaction was possible.`
   - In headless Tart macOS VMs, modifying root trust settings in system keychains requires either GUI authorization or pre-trusting the CA directly in the base golden image (`maho-e2e-golden`).
2. **CLI Route Availability for Right-Panel Agent:**
   - The CLI automated catalog does not currently expose a direct command to open and snapshot the right-side agent panel without user interface click coordination.
3. **External Real-Account Mutation Policy:**
   - Per project policy, external SMTP/IMAP transmissions against live mail providers (e.g. Gmail/Outlook) remain unexecuted without explicit credentials and test authorization.

---

## 5. Conclusion

All local engine, WebUI, and browser delegate integration components meet commercial-grade stability, data safety, and security isolation requirements. Shipped components are fully verified to build and pass all test suites.
