# Maho Browser — Update Rollout Playbook

> **Version**: 1.0  **Date**: 2026-06-17  
> **Audience**: Release engineers, on-call SREs  
> **Related**: `docs/operations/release-key-management.md`,
> `docs/maho_browser_update_architecture.md`

---

## Overview

This runbook describes how to execute a staged Sparkle 2 update rollout for
Maho Browser, monitor it, respond to crash-loop events, and roll back if
needed.  The infrastructure described here is live as of 2026-06-17:

| Component | File | Status |
|---|---|---|
| Rollout bucket HMAC computation | `browser/updates/rollout_bucket.cc` | ✅ Live |
| Update state machine | `browser/updates/maho_update_manager.cc` | ✅ Live |
| Crash-loop sentinel (≥3 crashes → rollback) | `browser/updates/crash_loop_sentinel.cc` | ✅ Live |
| Kill-switch pref (`maho.update.enabled`) | `maho_update_manager.cc` §Initialize | ✅ Live |
| `--maho-simulate-update-state` debug flag | `maho_update_manager.cc` | ✅ Live |
| macOS Sparkle 2 delegate | `browser/updates/maho_update_delegate_mac.mm` | 🟡 Wired + framework embedded (A.4, 2026-07-11); pending signed-release live-update test |
| **SUPublicEDKey (ed25519 public key)** | Info.plist branding patch | ⛔ Blocker — see §1.1 |

**The single remaining blocker** for end-to-end signed rollouts is the
`SUPublicEDKey` value.  Follow `docs/operations/release-key-management.md`
to generate the keypair, then come back to this playbook.

---

## 1. Pre-Rollout Checklist

### 1.1  Ed25519 key wired

- [ ] `manifest_pubkey.bin` is non-zero in the repo
- [ ] `SUPublicEDKey` set in Info.plist branding patch
- [ ] `kMahoUpdateManifestPublicKeyIsPlaceholder` is `false`
- [ ] `python3 maho-chromium/build/scripts/generate_update_pubkey.py --check` exits 0

### 1.2  Build

- [ ] Release DMG built and notarized (`xcrun notarytool`)
- [ ] DMG signed with `sign_update` (Sparkle CLI): `edSignature` attribute present in appcast XML
- [ ] Appcast XML checksums and sizes match the real artifact

### 1.3  Canary smoke-test

Before ANY staged rollout, deploy to **canary** (bucket 0-0, i.e. internal
team machines) and verify:

```bash
# On a canary machine, launch with the simulate flag to pre-verify the UI:
/Applications/Maho.app/Contents/MacOS/Maho \
  --maho-simulate-update-state=update_available

# Then, with the real appcast pointed at the canary feed:
defaults write com.maho.browser SUFeedURL "https://updates.maho.app/updates/macos/appcast?channel=canary&b=0"
# Restart app and verify:
# 1. Sparkle finds the update (no error 2001/2002 = key is wired correctly)
# 2. Update downloads and installs
# 3. App relaunches to new version
```

- [ ] Canary update accepted (no signature error)
- [ ] No crash in first 60 seconds post-launch (CrashLoopSentinel stable timer)
- [ ] UMA: `Maho.Update.State` shows `kUpToDate` after successful install

---

## 2. Staged Rollout — Three Phases

Rollout uses the HMAC-SHA256 bucket system in `rollout_bucket.cc`.  Each
install gets a persistent `install_id` (UUID stored in local_state prefs).
`RolloutBucket::Compute(install_id)` returns a deterministic integer in
[0, 99].  The appcast server gates delivery by the `?b=<bucket>` query
parameter.

### Phase 1 — 10% (buckets 0-9)

**Duration**: minimum 24 hours of stable operation.

**Deploy**:
```bash
# Update appcast server config to serve the new version for buckets 0-9.
# Example server-side config (actual implementation depends on relay infra):
maho-release-cli set-rollout \
  --version 1.x.y \
  --channel stable \
  --bucket-max 9
```

**Monitor** (see §4 for full metrics list):
- `Maho.Update.State` histogram: watch for spike in `kError`
- `Maho.Update.LastError`: any `kSignatureMismatch` (2001/2002) = key mismatch → **stop rollout**
- `Maho.Update.CheckLatency`: P95 should be < 5 s
- Crash reports: compare crash rate (crashes/active_users) to baseline

**Go / No-Go criteria for Phase 2**:
- Crash rate ≤ baseline + 0.5%
- `kSignatureMismatch` errors = 0
- `Maho.Update.State` kError rate ≤ 2% of checks
- No CrashLoopSentinel trips on the new version

### Phase 2 — 50% (buckets 0-49)

**Duration**: minimum 24 hours.

```bash
maho-release-cli set-rollout \
  --version 1.x.y \
  --channel stable \
  --bucket-max 49
```

**Additional monitoring**:
- Channel distribution: canary (0-0), dev (0-4), beta (0-14), stable (0-99)
- Watch for any increase in `kCrashLoop` errors vs Phase 1

**Go / No-Go criteria for Phase 3**: same thresholds as Phase 1.

### Phase 3 — 100% (buckets 0-99)

```bash
maho-release-cli set-rollout \
  --version 1.x.y \
  --channel stable \
  --bucket-max 99
```

**Post-rollout**: monitor for 48 hours.  After 100% rollout with no issues,
mark the release stable.

---

## 3. Crash-Loop Response

`CrashLoopSentinel::CheckAndMarkStart` trips when the same version crashes
**3 or more consecutive times** within the startup window (before the 60-second
stable timer fires).  When tripped:

1. `MahoUpdateManager` transitions to `UpdateState::kError` with
   `UpdateError::kCrashLoop`.
2. `maho.update.rollback_requested` pref is set to `true`.
3. Auto-update is disabled for that install for the lifetime of that version.

### Detecting a crash-loop event in production

```
# UMA signals:
Maho.Update.LastError  → kCrashLoop spike
Maho.Update.State      → kError spike

# Check logs (macOS):
log show --predicate 'process == "Maho"' --last 10m | grep "crash loop"
# Expected line: "Maho: Auto-update disabled due to crash loop."
```

### Immediate rollback procedure

```bash
# Step 1: Stop the rollout immediately.
maho-release-cli set-rollout \
  --version 1.x.y \
  --channel stable \
  --bucket-max 0        # serve to no new installs

# Step 2: Revert the appcast to the previous stable version.
maho-release-cli revert-rollout \
  --channel stable \
  --version <previous_stable>

# Step 3: Force a "last-known-good" appcast entry pointing back to
#         the previous DMG with its original edSignature.
# (The appcast server must serve the previous version's entry for all buckets.)

# Step 4: Verify the previous version's appcast is live.
curl -s "https://updates.maho.app/updates/macos/appcast?channel=stable&b=50" \
  | grep "<enclosure" | grep "version="
# Confirm version = previous_stable

# Step 5: Notify users via in-app banner (set kill-switch to trigger banner).
# Set maho.update.enabled = false for the bad version via MDM/policy if available.
```

### Post-incident

1. Pull crash reports from the affected version.
2. Identify root cause.
3. Fix and validate on canary before reopening the rollout.
4. Update incident log in `docs/operations/incident-log.md` (create if absent).

---

## 4. Telemetry Monitoring Reference

All UMA histograms are opt-in (user consent required).  The following signals
are available once telemetry is active:

| Histogram | Type | What to watch |
|---|---|---|
| `Maho.Update.State` | Enum | `kError` spike → investigate `LastError` |
| `Maho.Update.LastError` | Enum | `kSignatureMismatch` → key mismatch; `kCrashLoop` → crash-loop tripped |
| `Maho.Update.CheckLatency` | Time | P95 > 10s = server latency issue |
| `Maho.Update.RolloutBucket` | Linear 0-100 | Should be uniform; large deviation = install_id skew |

### Debug simulation (no telemetry needed)

```bash
# Test each update UI state visually without a live relay:
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=idle
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=checking
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=update_available
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=downloading
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=ready_to_install
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=up_to_date
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=error_generic
/Applications/Maho.app/Contents/MacOS/Maho --maho-simulate-update-state=error_crash_loop
```

---

## 5. Channel Migration

Maho uses four update channels with the following default distribution:

| Channel | Bucket range | Audience |
|---|---|---|
| canary | 0 (team-internal) | Maho team + power testers |
| dev | 0-4 (~5%) | Developer users |
| beta | 0-14 (~15%) | Active beta users |
| stable | 0-99 (100%) | General users |

Channel is stored in the `maho.update.channel` pref and passed to Sparkle
as `?channel=<name>` in the feed URL (see `feedURLStringForUpdater:` in
`maho_update_delegate_mac.mm`).

### Migrating a user to a different channel

```bash
# Via settings UI: Settings → About → Update Channel dropdown (when implemented).

# Via command-line (for testing):
defaults write com.maho.browser mahoUpdateChannel "beta"
# Restart app — MahoUpdateManager reads the pref on next Initialize().
```

### Migrating a release cohort

To move a population from beta → stable (e.g., after a beta period ends):

```bash
# Update the appcast server to serve the stable version on the beta feed.
maho-release-cli promote-channel \
  --from beta \
  --to stable \
  --version 1.x.y
```

---

## 6. Linux Package Manager Guidance

Linux updates are delivered through the system package manager, not Sparkle.
The update delegate for Linux (`maho_update_delegate_linux.cc`) handles
notification-only behavior; the system handles actual delivery.

### In-app banner

When an update is available on Linux, Maho displays an in-app notification
banner (not a Sparkle-style modal).  The banner text:

> "A new version of Maho is available. Update using your package manager:
> `sudo apt update && sudo apt upgrade maho`"

### APT / dpkg

```bash
# Add Maho's APT repository (one-time setup, included in install instructions):
echo "deb [signed-by=/usr/share/keyrings/maho-archive-keyring.gpg] \
  https://packages.maho.app/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/maho.list

# Update:
sudo apt update && sudo apt upgrade maho
```

### RPM / dnf

```bash
# Add Maho's RPM repository:
sudo dnf config-manager --add-repo https://packages.maho.app/rpm/maho.repo

# Update:
sudo dnf upgrade maho
```

### Flatpak

```bash
# Flatpak updates happen via Flathub or the Maho Flatpak remote:
flatpak update com.maho.Browser
```

### AppImage

AppImage users self-update by downloading the new AppImage from the releases
page.  The in-app banner links directly to the download URL.

---

## 7. Emergency Kill-Switch

To disable auto-update for all users immediately (e.g., discovered critical
bug in the update payload):

### Server-side

```bash
# Set bucket-max to -1 on the appcast server (serves empty feed / no update).
maho-release-cli set-rollout \
  --version 1.x.y \
  --channel stable \
  --bucket-max -1
```

### Client-side (via policy / MDM)

The `maho.update.enabled` pref (type: boolean) acts as a per-client kill-switch.
Set via managed policy (macOS MDM, Group Policy, or admin pref file):

```xml
<!-- /Library/Preferences/com.maho.browser.plist -->
<key>MahoUpdateEnabled</key>
<false/>
```

When this pref is `false`, `MahoUpdateManager::Initialize()` returns early and
no delegate is created.  No update checks run, no UMA is recorded.

---

## 8. Rollback Verification Checklist

After triggering a rollback:

- [ ] Appcast serving previous version for all channels
- [ ] `maho-release-cli list-rollout --channel stable` shows correct version
- [ ] Spot-check: `curl` appcast URL returns previous version's entry
- [ ] Crash rate returning to baseline (check 30 min after rollback)
- [ ] Incident log entry created
- [ ] Engineering notified via incident channel

---

## 9. Contacts & Escalation

| Role | Responsibility |
|---|---|
| Release Engineer (on-call) | Execute rollout, monitor, rollback if needed |
| Engineering lead | Approve > 10% rollout gates |
| Infrastructure SRE | Appcast server, relay, CDN |
| Security | Immediate escalation if `kSignatureMismatch` errors spike |

---

## Appendix A — Rollout Command Reference

```bash
# Query current rollout state
maho-release-cli list-rollout --channel stable

# Advance to 10%
maho-release-cli set-rollout --version <v> --channel stable --bucket-max 9

# Advance to 50%
maho-release-cli set-rollout --version <v> --channel stable --bucket-max 49

# Full rollout
maho-release-cli set-rollout --version <v> --channel stable --bucket-max 99

# Emergency stop (new installs only; existing downloads proceed)
maho-release-cli set-rollout --version <v> --channel stable --bucket-max -1

# Full rollback to previous version
maho-release-cli revert-rollout --channel stable --version <prev_version>
```

> **Note**: `maho-release-cli` is a placeholder name for the release tooling
> that wraps the appcast server's admin API.  Replace with the actual CLI
> command once the release infrastructure (relay/appcast server) is deployed.

---

## Appendix B — Bucket Distribution Verification

```python
# Quick sanity check: given N random UUIDs, verify ~uniform bucket distribution.
import uuid, hashlib, struct

def bucket(install_id: str) -> int:
    import hmac
    h = hmac.new(install_id.encode(), b"rollout", hashlib.sha256).digest()
    value = struct.unpack(">Q", h[:8])[0]
    return value % 100

samples = [bucket(str(uuid.uuid4())) for _ in range(10000)]
from collections import Counter
c = Counter(b // 10 for b in samples)  # count by decile
for decile, count in sorted(c.items()):
    print(f"  {decile*10:3d}-{decile*10+9:3d}: {count:5d}  ({count/100:.1f}%)")
```

Expected output: each decile ~10% ± 1%.
