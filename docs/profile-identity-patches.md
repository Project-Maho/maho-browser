# Profile Identity Patches — macOS

**Date:** 2026-05-11
**Status:** Applied and verified

## Problem

Every launch of Maho Browser on macOS showed the error dialog:

> Something went wrong when opening your profile. Some features may be unavailable.

The error string is `IDS_COULDNT_OPEN_PROFILE_ERROR` (in `generated_resources.grd`).
All six trigger paths converge on `ShowProfileErrorDialog()`:

1. **WebData DB** — `sql::INIT_FAILURE` from WebDatabase
2. **History DB** — `sql::INIT_FAILURE` from HistoryDatabase
3. **Preferences JSON** — corrupt or unreadable `Preferences` file
4. **Profile creation** — `ProfileManager::OnProfileCreationStarted` failure
5. **Web App DB** — web app database init failure
6. **Prefs loading** — `ProfileImpl` prefs load failure

## Root Causes

### 1. Wrong user data directory

Without `CrProductDirName` in the app plist, non-Google-branded Chromium
defaults the user data dir to `~/Library/Application Support/Chromium/`.
If vanilla Chromium is installed on the same machine, Maho and Chromium
fight over the same profile directory — locked databases, conflicting
schemas, and permission errors.

### 2. Shared Keychain entry

Chromium stores its encryption key in macOS Keychain under the service
name `"Chromium Safe Storage"` with account `"Chromium"`. The Keychain
ACL is bound to the code signature of the binary that created the entry.

When Maho (a different binary) tries to read this entry, macOS may:
- Prompt the user for permission (confusing — "Chromium" asking?)
- Silently deny access (ad-hoc signed builds)

A denied Keychain read means `KeychainPassword::GetPassword()` returns
empty, `OSCrypt::Init()` cannot decrypt existing databases, and every
encrypted DB open triggers `sql::INIT_FAILURE` → profile error dialog.

### 3. Ad-hoc code signing instability

During development, each rebuild produces a different binary hash.
Without consistent code signing, macOS Keychain treats each rebuild as a
different application and revokes access to previously created Keychain
items — even to Maho's own `"Maho Safe Storage"` entry.

## Patches Applied

### Patch 1: `CrProductDirName=Maho` in `app-Info.plist`

**File:** `chromium/src/chrome/app/app-Info.plist`
**Applied via:** `build/scripts/apply_chromium_src_overrides.py`
**Doc reference:** `chromium_src/chrome/app/app-Info.plist`

Adds `<key>CrProductDirName</key><string>Maho</string>` before
`CFBundleExecutable`. This is the Chromium-native mechanism
(`chrome::GetDefaultUserDataDirectory()` reads this plist key) to set
the user data directory to `~/Library/Application Support/Maho/`.

**Effect:** Complete profile isolation from Chromium. Maho creates and
owns its own `Default/` profile, databases, and preferences.

### Patch 2: Maho-specific Keychain names

**File:** `chromium/src/components/os_crypt/common/keychain_password_mac.mm`
**Applied via:** `build/scripts/apply_chromium_src_overrides.py`
**Doc reference:** `chromium_src/components/os_crypt/common/keychain_password_mac.mm`

Changes the `#else` (non-Google-branded) block:
```
"Chromium Safe Storage" → "Maho Safe Storage"
"Chromium"              → "Maho"
```

**Effect:** Maho uses its own Keychain entry for the encryption key,
independent of Chromium. No Keychain ACL conflicts.

### Patch 3: Post-build code signing in `build_maho.py`

**File:** `maho-chromium/build/scripts/build_maho.py`

Added `find_codesign_identity()` and `codesign_app()` functions.
After every successful Chromium build, the script automatically:
1. Finds a valid signing identity from `security find-identity`
2. Signs `Maho.app` with `codesign --force --deep --sign <identity>`

CLI flags added:
- `--skip-codesign` — skip the step entirely
- `--codesign-identity "..."` — override auto-detection

**Effect:** Consistent code signature across rebuilds. macOS Keychain
ACLs persist because the signing identity (team ID + certificate) stays
the same, even when the binary hash changes.

## How It All Fits Together

```
Build completes
  → codesign signs Maho.app (Patch 3)
    → App launches
      → Reads CrProductDirName=Maho from plist (Patch 1)
        → Opens ~/Library/Application Support/Maho/Default/
          → OSCrypt reads "Maho Safe Storage" from Keychain (Patch 2)
            → Keychain ACL matches signed identity (Patch 3)
              → Encryption key retrieved successfully
                → All databases decrypt and open
                  → No profile error dialog
```

## Prevention Guidelines

### When rebasing on upstream Chromium

1. **Check `keychain_password_mac.mm`** — if the file structure changes
   around the `kDefaultServiceName`/`kDefaultAccountName` constants,
   update the anchor string in `apply_chromium_src_overrides.py`.

2. **Check `app-Info.plist`** — if the plist key ordering around
   `CFBundleExecutable` changes, update the anchor string.

3. **Run `apply_chromium_src_overrides.py`** after every `gclient sync`.
   The script is idempotent and reports `already:` for patches that are
   still in place.

### When changing build infrastructure

- **Never remove the codesign step** from `build_maho.py` without
  replacing it with another consistent signing mechanism.
- **Never use ad-hoc signing** (`codesign -s -`) for dev builds that
  need Keychain persistence across rebuilds.
- If the signing certificate expires, `find_codesign_identity()` will
  return `None` and print a warning. Install a new Apple Development
  certificate to restore the flow.

### Debugging profile errors in the future

If the profile error dialog reappears:

1. **Check Keychain Access.app** — search for `"Maho Safe Storage"`.
   If missing, the encryption key was never created (first launch issue).
   If present but locked, check the ACL (Get Info → Access Control).

2. **Check user data directory** — `ls ~/Library/Application Support/Maho/`.
   If it doesn't exist, `CrProductDirName` patch may have been lost.

3. **Check code signing** — `codesign -dvvv Maho.app`. Verify the
   `Authority` and `TeamIdentifier` fields match expectations.

4. **Check for zombie processes** — `ps aux | grep -i maho`. A zombie
   Maho process holding DB locks will prevent a new instance from
   opening databases. Kill the zombie or reboot.

5. **Console.app logs** — filter by `Maho` process. Keychain denial
   errors appear as `SecKeychainItemCopyContent` failures.

## Files Reference

| File | Role |
|------|------|
| `maho-chromium/build/scripts/build_maho.py` | Post-build codesign logic |
| `maho-chromium/build/scripts/apply_chromium_src_overrides.py` | Patch application script |
| `maho-chromium/chromium_src/chrome/app/app-Info.plist` | Doc reference for plist patch |
| `maho-chromium/chromium_src/components/os_crypt/common/keychain_password_mac.mm` | Doc reference for Keychain patch |
| `chromium/src/chrome/app/app-Info.plist` | Actual patched plist (gitignored) |
| `chromium/src/components/os_crypt/common/keychain_password_mac.mm` | Actual patched source (gitignored) |
