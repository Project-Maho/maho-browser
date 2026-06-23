# BYOK Runtime Verification Procedures

This document describes manual and automated verification procedures for confirming BYOK (Bring Your Own Key) encryption-at-rest works end-to-end on each supported platform. Supports SOC2 control evidence for **CC6.6** (Encryption at Rest) and **C1.1** (Confidentiality data labeling).

---

## 1. Automated (Rust integration tests)

Run from `maho/` workspace root:

```bash
cargo test -p maho-agent --test integration_tests
```

Tests covering BYOK runtime behavior:

| Test | Asserts |
|---|---|
| `test_secure_storage_callback_invoked` | Callback is called with `"openai"` provider |
| `test_secure_storage_callback_failure_fails_closed` | Agent returns error when callback returns Err |
| `test_secure_storage_callback_provider_routing` | Falls through `openai` → `anthropic` correctly |
| `test_byok_key_access_audit_emitted` | Callback invocation is observable (audit trail) |
| `test_agent_fails_without_byok_key` | `allow_insecure_key_storage=false` + no callback → fail-closed |

**Expected**: all tests PASS. Failure indicates regression in BYOK callback contract.

---

## 2. Android KeyStore AES-256-GCM Round-Trip (Instrumented)

### Prerequisites
- Android device or emulator (API 23+)
- Android Studio + ADB

### Procedure
1. Connect device: `adb devices`
2. Install debug build: `cd maho/android-shell && ./gradlew installDebug`
3. Run instrumented test:
   ```bash
   ./gradlew app:connectedDebugAndroidTest --tests "*MahoBridgeSecureStorageTest*"
   ```

### What to verify
- `SecureStorageHelper.encrypt(plaintext)` returns non-empty base64 string
- `SecureStorageHelper.decrypt(encrypted)` returns original plaintext
- Key is stored under alias `MahoBYOKSecureKey` in AndroidKeyStore (verify via `keystore-explorer` or programmatic check)
- KeyGenParameterSpec uses:
  - `KEY_ALGORITHM_AES`
  - `BLOCK_MODE_GCM`
  - `ENCRYPTION_PADDING_NONE`
  - `setKeySize(256)` — confirms AES-256, not default 128
- Decryption fails after `KEY_ALIAS` deletion (proves key bound to AndroidKeyStore, not in-memory)

### Manual sanity check
```kotlin
val plaintext = "test-byok-key-12345"
val encrypted = SecureStorageHelper.encrypt(plaintext)
val roundtripped = SecureStorageHelper.decrypt(encrypted)
assert(roundtripped == plaintext)
```

---

## 3. iOS Keychain ACL Verification (XCTest)

### Prerequisites
- macOS + Xcode + iOS Simulator
- iOS deployment target ≥ 15

### Procedure
1. `cd maho/ios-shell && make test`
2. Run BYOK-specific test target (or via Xcode UI: Maho > Tests > BYOKKeychainTests)

### What to verify
- `BYOKKeychain.set(provider: "openai", key: "x")` returns `true`
- `BYOKKeychain.get(provider: "openai")` returns the same key
- `BYOKKeychain.getData(provider: "openai")` returns equivalent `Data`
- Keychain entry attributes:
  - `kSecClass = kSecClassGenericPassword`
  - `kSecAttrService = "dev.maho.browser.byok"`
  - `kSecAttrAccessible = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`
- After device lock + first-unlock cycle, key remains accessible
- After device wipe / re-install, key is gone (per `ThisDeviceOnly` flag)
- `BYOKKeychain.delete(provider:)` returns `true` and subsequent `get()` returns `nil`

### Manual sanity check
```swift
let success = BYOKKeychain.set(provider: "openai", key: "sk-test-123")
assert(success == true)
let retrieved = BYOKKeychain.get(provider: "openai")
assert(retrieved == "sk-test-123")
let data = BYOKKeychain.getData(provider: "openai")
assert(data != nil && String(data: data!, encoding: .utf8) == "sk-test-123")
_ = BYOKKeychain.delete(provider: "openai")
assert(BYOKKeychain.get(provider: "openai") == nil)
```

---

## 4. Desktop OSCrypt Round-Trip (Chromium)

### Prerequisites
- Chromium build complete (`python3 maho-chromium/build/scripts/build_maho.py`)
- Maho.app installed

### Procedure
1. Launch Maho.app
2. Open `chrome://maho-settings/ai`
3. **Once Desktop BYOK settings UI is implemented (G9 write side):** enter test key, save
4. Inspect prefs file directly:
   - macOS: `~/Library/Application Support/Maho/Default/Preferences`
   - Windows: `%LOCALAPPDATA%\Maho\User Data\Default\Preferences`
   - Linux: `~/.config/maho/Default/Preferences`

### What to verify
- Pref `maho.ai.byok.openai.encrypted_b64` is set to a non-empty base64 string
- The decoded base64 is NOT the plaintext key (verify: base64-decode the pref value and confirm it's NOT human-readable)
- OSCrypt-decoded value matches the entered key
- After clearing the key via Settings UI, the pref is empty string or absent
- Crash dump produced during an active session contains `maho-byok-key-status: active` annotation (verify via `crashpad_database_util`)

### Programmatic verification (Browser Process console)
Open DevTools on `chrome://maho-settings`:
```javascript
chrome.send('getPref', ['maho.ai.byok.openai.encrypted_b64']);
// Returned value should be a base64 string, NOT the original plaintext
```

---

## 5. Negative Cases (verify protection mechanisms work)

### 5.1 Plaintext leak prevention
Run agent session with mock LLM (no real API call), then:
- Grep `/var/log/maho/audit.json` (or platform equivalent) for the test key value — must return ZERO matches
- Grep process memory dump for the test key value — should match ONLY during active HTTP request, not after session end
- Inspect `tracing::warn` deny logs — must contain `tool` name, NEVER `args` content

### 5.2 Fail-closed when storage unavailable
- Android: revoke KeyStore permission (impossible on real device; simulate by deleting key alias)
- iOS: revoke Keychain (impossible; simulate by failing `SecItemCopyMatching`)
- Desktop: empty `maho.ai.byok.*` prefs

Expected: Agent returns `AgentError::PermissionDenied` or `ExecutionError("Secure storage callback is missing")` — never proceeds with plaintext fallback.

### 5.3 Audit log contains expected events
After running a session, audit JSON log should contain entries for:
- `event = "session_start"` with `session_id`
- `event = "byok_key_access"` with `provider`
- `event = "session_end"` with `session_id` + `duration_ms`
- (If permission denied): `tool = "..."` warn with NO `args` field

---

## 6. Verification Cadence

| Cadence | Procedure | Owner |
|---|---|---|
| Pre-release | All sections 1-5 must pass before any release tagged `*-rc.*` | Release engineer |
| Quarterly | Manual round-trip on each platform (sections 2-4) | vCISO |
| On dependency upgrade | Re-run section 1 after any update to secrecy, zeroize, swiftide, async-openai, async-anthropic | Eng |
| Post-incident | After any BYOK-related incident per incident-response-byok.md, re-run all sections | vCISO |

---

## 7. Evidence Capture for SOC2 Audit

For Big 4 audit walkthrough:
1. Screen recording of all 5 sections passing on each platform
2. Audit log JSON sample (sanitized — keys redacted)
3. Crash dump sample showing annotation
4. Signed sign-off from vCISO that procedure executed within the audit period
