# Data Classification Policy

## 1. Purpose & Scope

To establish classification standards for the Maho Browser workspaces, repositories, and running instances to satisfy SOC2 Confidentiality criteria (specifically **C1.1**). This policy governs all assets processed or held locally or in transit by the Maho Browser, the FFI bridge, and native shells.

---

## 2. Classification Levels & Control Requirements

All data objects are classified into one of the three levels below:

### Level 3: Restricted (Confidential / Secret)
Data whose unauthorized disclosure would result in severe financial, reputational, or legal damage to the user or organization.
- **Examples:** BYOK API Keys (OpenAI, Anthropic), Saved Logins & Passwords, Sync cryptographic seed phrases.
- **Control Requirements:**
  - Mandatory encryption-at-rest via OS-specific secure storage subsystems (Keychain on iOS, KeyStore on Android, OSCrypt on macOS/Windows/Linux).
  - Explicit heap scrub using memory zeroization before memory reuse.
  - Plaintext values must **never** be logged to standard debug or system trace channels.

### Level 2: Private (Internal Use Only)
User-specific context and metadata that must remain confidential but does not constitute raw secrets.
- **Examples:** Chat Conversation History, workspace environment parameters, file system path queries.
- **Control Requirements:**
  - Access restricted to the running application sequence; gated via `PermissionGatedTool` default-deny routines.
  - Stored within user-profile directory permissions (restricted to the host user context).

### Level 1: Public
Information intended or approved for public distribution.
- **Examples:** Open-source project files, default configuration values, public documentation.

---

## 3. Labeling and Enforcement

1. **Codebase Annotations:**
   - Any function, struct, or variable storing or handling Level 3 (Restricted) data must be clearly documented with safety and zeroization comments.
   - Example:
     ```rust
     // SAFETY: ZeroizedString contains BYOK plaintext, exclude from crash dumps
     pub struct ZeroizedString(secrecy::SecretString);
     ```

2. **Automated Enforcement:**
   - Git hook or CI step searching code for plaintext key definitions or forbidden keywords.
   - Pre-commit scanning via regex prevent check-ins containing Level 3 elements.

---

## 4. Mis-classification Examples (Negative Cases)

To guide engineering and QA teams, the following examples illustrate common mis-classification violations that will trigger SOC2 compliance audits:

1. **Incorrect Logging (Violating CC7.1):**
   - *Scenario:* Logging `ToolCall` arguments containing user API keys or personal chat content inside audit logs.
   - *Correction:* The audit log target `maho_agent::audit` strictly excludes the arguments parameter on allows. Only tool name is written.
2. **Plaintext Preference Storage (Violating CC6.6):**
   - *Scenario:* Writing an OpenAI API key directly to a plaintext preference key (e.g. `maho.ai.api_key`) instead of using the base64-encoded OSCrypt key (`maho.ai.byok.openai.encrypted_b64`).
   - *Correction:* Plaintext preference fields must only store Level 1 or Level 2 default configuration values.
3. **Soft Deletion of Databases:**
   - *Scenario:* Marking a chat session as `deleted = 1` inside local SQLite database instead of performing a hard purge.
   - *Correction:* Cascading hard deletes are enforced to wipe records.

---

## 5. Audit and Verification Examples

Compliance auditors will periodically query the log targets to verify compliance:
- Run: `grep -E "api_key|token" /var/log/maho_audit.json` to verify no Restricted data leaked to audit streams.
- Review storage outputs: Run database checks asserting that no rows containing plaintext keys are stored in `settings` SQLite tables.
