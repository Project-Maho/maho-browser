# Maho Browser — Release Key Management

> **Scope**: This document covers the ed25519 signing key used by Sparkle 2
> (macOS) for update signature verification.  The Windows Authenticode cert
> and Linux package-signing GPG key follow separate processes and are not
> covered here.

---

## Overview

Maho Browser's macOS update pipeline uses **Sparkle 2**, which requires
EdDSA (ed25519) signature verification.  Every `.dmg` shipped to users must
be signed by the private half of the key whose **32-byte raw public key** is
embedded in the app bundle's `Info.plist` as `SUPublicEDKey`.

The `SUPublicEDKey` value is committed to the repo inside the branding patch.
The **private key is never committed**.  It lives exclusively in the team's
1Password vault (or KMS) and on the release engineer's air-gapped signing
workstation during release builds.

---

## 1. Key Generation

### Prerequisites

```bash
pip install cryptography   # only needed for --generate / --export-public
```

### Generating a new keypair

**Run from outside the repo** (mandatory — the script refuses to write a
private key inside a git-tracked directory):

```bash
python3 maho-chromium/build/scripts/generate_update_pubkey.py \
    --generate \
    --output-dir ~/maho-release-keys/
```

Sample output:

```
Private key written to : /Users/you/maho-release-keys/maho_update_privkey.pem  (chmod 600, KEEP OFFLINE)
Public key binary      : /Users/you/maho-release-keys/maho_update_pubkey.bin

SUPublicEDKey (paste into Info.plist branding patch):
  <base64-string>

Next steps:
  1. Store maho_update_privkey.pem in 1Password / KMS — never commit it.
  2. Copy maho_update_pubkey.bin to maho-chromium/browser/keys/manifest_pubkey.bin
  3. Add the SUPublicEDKey value to the Info.plist branding patch.
  ...
```

> The private key file is created with `chmod 600`.  Do not print it to
> the terminal or paste it into Slack/email.

### Re-deriving the public key from a stored private key

If the private key was retrieved from 1Password / KMS for signing and you
need to confirm the public key value:

```bash
python3 maho-chromium/build/scripts/generate_update_pubkey.py \
    --export-public ~/maho-release-keys/maho_update_privkey.pem
```

---

## 2. Secure Storage

| Location | Contents | Access |
|---|---|---|
| 1Password "Maho Release Keys" vault | `maho_update_privkey.pem` | Release engineers only (MFA required) |
| KMS (alternative) | Key material, never exported | CI release job service account |
| `maho-chromium/browser/keys/manifest_pubkey.bin` | 32-byte raw public key | Committed to repo, public |
| Info.plist branding patch | `SUPublicEDKey` base64 string | Committed to repo, public |

### 1Password workflow

1. Generate the keypair as above.
2. Open the "Maho Release Keys" vault in 1Password.
3. Create a new "Secure Note" item named `maho-update-ed25519-<YYYY>`.
4. Attach `maho_update_privkey.pem` as a file attachment.
5. Record the `SUPublicEDKey` value in the note body for reference.
6. Delete the local copy of `maho_update_privkey.pem` after upload.

### KMS workflow (alternative)

Use AWS KMS or GCP KMS to store the key material.  The release pipeline signs
using the KMS API; the private key bytes never leave the KMS hardware boundary.

```bash
# Example: sign with AWS KMS (replace with real KMS sign command)
aws kms sign \
  --key-id alias/maho-update-signing \
  --message-type RAW \
  --signing-algorithm ECDSA_SHA_256 \
  --message fileb://release_payload_hash.bin
```

---

## 3. Committing the Public Key

After generating a keypair, the **only** piece that enters the repo is the
32-byte raw public key binary, used in two places:

### 3a. Binary file for the C++ header generator

```bash
# Copy the public binary into the repo.
cp ~/maho-release-keys/maho_update_pubkey.bin \
   maho-chromium/browser/keys/manifest_pubkey.bin

git add maho-chromium/browser/keys/manifest_pubkey.bin
git commit -m "updates: set ed25519 public key for Sparkle 2 verification"
```

The GN build rule (`generate_update_pubkey`) calls:

```bash
python3 maho-chromium/build/scripts/generate_update_pubkey.py \
    --embed-public \
    maho-chromium/browser/keys/manifest_pubkey.bin \
    <gen_dir>/manifest_pubkey.h
```

This emits `kMahoUpdateManifestPublicKey` and
`kMahoUpdateManifestPublicKeyIsPlaceholder` into the generated header.

### 3b. Info.plist `SUPublicEDKey` branding patch

Sparkle 2 reads the ed25519 public key from `SUPublicEDKey` in the app
bundle's `Info.plist`.  This value must be patched into the Chromium branding
overlay before a signed release build.

Locate the branding patch applied by `apply_chromium_src_overrides.py` and
set:

```xml
<key>SUPublicEDKey</key>
<string>PASTE_BASE64_PUBLIC_KEY_HERE</string>
```

The base64 string is the output of `--generate` or `--export-public`.

> **Until this patch is applied, Sparkle will refuse every update with a
> signature-verification error** (error codes 2001/2002 in
> `maho_update_delegate_mac.mm`).  The `kMahoUpdateManifestPublicKeyIsPlaceholder`
> flag and the build-time warning from `generate_update_pubkey.py` exist
> precisely to make this state loud and obvious.

---

## 4. Signing a Release (Private-Key Signing Flow)

Sparkle 2's `generate_appcast` tool (from the Sparkle distribution) signs
the update package and writes the `edSignature` attribute into the appcast XML.

### Prerequisites

```bash
# On the signing workstation:
# Install Sparkle tools (download from https://sparkle-project.org).
export PATH="$PATH:/path/to/Sparkle/bin"

# Retrieve the private key from 1Password.
op read "op://Maho Release Keys/maho-update-ed25519-<YYYY>/maho_update_privkey.pem" \
    > /tmp/maho_update_privkey.pem
chmod 600 /tmp/maho_update_privkey.pem
```

### Sign the DMG

```bash
# Sign the update package with Sparkle's sign_update tool.
sign_update Maho-<version>.dmg /tmp/maho_update_privkey.pem
```

This outputs a line like:

```
sparkle:edSignature="<base64-sig>" sparkle:length="12345678"
```

Paste these attributes into the appcast XML item for this release.

### Shred the private key after signing

```bash
# Overwrite and delete (macOS).
shred -u /tmp/maho_update_privkey.pem 2>/dev/null || \
  (cat /dev/urandom | head -c $(wc -c < /tmp/maho_update_privkey.pem) > /tmp/maho_update_privkey.pem && rm /tmp/maho_update_privkey.pem)
```

---

## 5. Verifying the Setup (--check mode)

Before any release, run the self-test:

```bash
python3 maho-chromium/build/scripts/generate_update_pubkey.py --check
# Expected: "All checks passed." and exit 0.
```

This verifies:
1. The script parses without syntax errors (`ast.parse`).
2. `--embed-public` placeholder and real-key round-trips succeed.
3. The defensive repo-write guard fires correctly (if `cryptography` is installed).
4. `--generate` + `--export-public` produce consistent public key bytes.

---

## 6. Rotation Policy

| Trigger | Action |
|---|---|
| Annual rotation (calendar) | Generate new keypair in December; deploy with first January release |
| Private key compromise suspected | **Immediate** rotation; revoke old key; force-update all channels |
| Release engineer offboarding | Rotate if they had direct access to the private key |
| KMS key rotation (automated) | Ensure new `maho_update_pubkey.bin` + branding patch are deployed before old key expires |

### Rotation procedure

1. Generate a new keypair: `--generate --output-dir ~/maho-release-keys-<YYYY>/`
2. Commit the new `manifest_pubkey.bin` and branding patch `SUPublicEDKey`.
3. Build and ship a **transitional release** signed with the **old** private
   key but containing the **new** public key in `Info.plist`.
   - Users on the old version will accept this update (old key validates the
     delivery), and their app will then trust future updates signed with the
     new key.
4. Sign all subsequent releases with the **new** private key.
5. Archive the old private key in 1Password with label `RETIRED-<date>`.
6. After all users have passed through the transitional release (monitor via
   `Maho.Update.State` / `Maho.Update.RolloutBucket` UMA), the old private key
   can be permanently deleted from 1Password.

> **Do not** delete the old private key before the transitional release has
> reached 100% rollout — users stuck on older versions cannot upgrade without
> it.

---

## 7. Checklist: First Production Rollout

- [ ] `manifest_pubkey.bin` committed (non-zero, 32 bytes)
- [ ] `SUPublicEDKey` set in Info.plist branding patch
- [ ] `kMahoUpdateManifestPublicKeyIsPlaceholder` is `false` in generated header
- [ ] `generate_update_pubkey.py --check` exits 0
- [ ] Appcast signed with `sign_update` and `edSignature` attribute present
- [ ] Test on canary channel: Sparkle finds + accepts the update without error 2001/2002
- [ ] Private key stored in 1Password, local copy shredded

---

## Related Files

| File | Purpose |
|---|---|
| `maho-chromium/build/scripts/generate_update_pubkey.py` | Keygen + header-embed utility (this workflow) |
| `maho-chromium/browser/keys/manifest_pubkey.bin` | 32-byte raw public key (committed, not secret) |
| `maho-chromium/browser/updates/maho_update_delegate_mac.mm` | Sparkle 2 integration; `SUPublicEDKey` wiring note |
| `maho-chromium/browser/updates/rollout_bucket.cc` | HMAC-SHA256 bucket computation (install_id → 0-99) |
| `maho-chromium/browser/updates/maho_update_manager.cc` | State machine; `kSimulateUpdateStateSwitch` |
| `maho-chromium/browser/updates/crash_loop_sentinel.h` | Crash-loop detection (3 consecutive crashes → rollback) |
| `docs/operations/update-rollout-playbook.md` | Staged rollout runbook (10% → 50% → 100%) |
| `docs/maho_browser_update_architecture.md` | Full architecture reference |
