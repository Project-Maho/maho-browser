# Maho Browser Cross-Platform Auto-Update — Implementation Plan

**Created**: 2026-06-14 JST
**Source**: `docs/maho_browser_update_architecture.md` (post-review revision incorporating all High/Medium/Low findings + additional improvements)
**Status**: Plan-only. No code written yet.
**Scope**: New subsystem — `maho/browser/updates/`. Touches `MahoBrowserMainExtraParts`, `BUILD.gn`, prefs registry, and adds three platform delegates (mac / win / linux-multi).

---

## 0. Goals & Non-Goals

### Goals
1. Ship a unified, browser-process-singleton update coordinator (`MahoUpdateManager`) that drives platform-native engines underneath.
2. macOS via Sparkle 2 (EdDSA-only, channel-aware appcast).
3. Windows via MSIX AppInstaller (primary) **or** signed Custom Downloader (fallback) — both paths gated by signature verification.
4. Linux via package-manager handoff with build-time-selected backend (deb/rpm/flatpak/appimage/noop).
5. Channels (stable / beta / dev / canary), staged rollout (HMAC bucket), crash-loop rollback, last-known-good pin.

### Non-Goals
- Server-side appcast generator / update CDN setup (separate workstream).
- Differential / patch updates beyond what Sparkle and zsync give for free.
- In-app payment / license enforcement around updates.
- Auto-update for the Tauri-based Maho Mail (`mail/`) — out of scope; that stack uses Tauri updater.

### Success Criteria (binary)
- [ ] `bun run typecheck` and `bun run test` clean from repo root.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --ninja-target chrome` succeeds on macOS host.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --ninja-target unit_tests` succeeds; new unit tests pass.
- [ ] `MahoUpdateManager::Initialize()` is idempotent across 3 consecutive `PostProfileInit` calls (test-asserted).
- [ ] On macOS dev build, manual "Check for updates" against a fixture appcast over `https://localhost` (test cert) returns `kUpdateAvailable` and signature verification rejects a tampered DMG.
- [ ] Windows custom-downloader code path rejects: (a) hash mismatch, (b) Authenticode failure, (c) manifest signature failure — three separate unit-test fixtures.
- [ ] Linux noop backend builds and links on a Linux dev host with `maho_linux_packaging = "tarball"`.
- [ ] All 13 review findings from the review doc trace to a checked item in §3 below.

---

## 1. Architecture Snapshot (Authoritative)

```
maho/browser/updates/                                  (== maho-chromium/browser/updates/ on disk)
├── maho_update_manager.h            # public API (UI-thread only, SEQUENCE_CHECKER)
├── maho_update_manager.cc           # state machine + observer dispatch
├── maho_update_pref_names.h         # pref keys
├── maho_update_prefs.cc/.h          # RegisterProfilePrefs + RegisterLocalStatePrefs
├── platform_updater_delegate.h      # abstract base (virtual dtor, 5 hooks)
├── rollout_bucket.cc/.h             # HMAC-SHA256 stable bucket (0..99)
├── crash_loop_sentinel.cc/.h        # last-known-good + crash-loop detector
├── maho_update_delegate_mac.mm      # ARC; SPUStandardUpdaterController
├── maho_update_delegate_win.cc      # MSIX path + custom downloader path
├── maho_update_signature_win.cc/.h  # WinVerifyTrust + EdDSA manifest
├── maho_update_delegate_linux_pkg.cc       # deb/rpm banner-only
├── maho_update_delegate_linux_flatpak.cc   # noop banner
├── maho_update_delegate_linux_appimage.cc  # zsync delta
├── maho_update_delegate_linux_noop.cc      # tarball / unknown
└── BUILD.gn                                 (lives at maho-chromium/browser/BUILD.gn — entries, not own file)
```

### Threading contract (re-stated, must hold)
- All public methods on `BrowserThread::UI`. `SEQUENCE_CHECKER` DCHECKs at every entry.
- Network/disk/installer I/O on a single `base::SequencedTaskRunner` from `ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})`.
- `Initialize()` idempotent via `bool initialized_`.
- Observers via `base::ObserverList<MahoUpdateObserver>` where `MahoUpdateObserver : public base::CheckedObserver`.

### State machine
```
kIdle ──CheckForUpdates──▶ kChecking ──┬──▶ kUpToDate ──▶ kIdle
                                       └──▶ kUpdateAvailable ──▶ kDownloading
                                            ──▶ kVerifying ──┬──▶ kReadyToInstall ──ApplyUpdateAndRestart──▶ (relaunch)
                                                             └──▶ kError
                                       └──▶ kError
```
Any transition to `kError` records the cause in a `last_error_` enum and surfaces it via `OnUpdateStateChanged(kError)`.

---

## 2. Phasing

Phases are **strictly sequential** at the merge level. Within a phase, tasks marked `‖` may be parallelized as separate commits/PRs.

### Phase 0 — Pre-flight (1 PR)
- P0.1 Add `maho/build/args.gni` declaring `maho_linux_packaging` (default `"tarball"`). New file; imported by `maho-chromium/browser/BUILD.gn`.
- P0.2 Register prefs scaffolding only (no consumers yet):
  - `maho.update.channel` (string, default `"stable"`) — profile pref.
  - `maho.update.install_id` (string, generated once) — local-state pref.
  - `maho.update.last_known_good_version` (string) — local-state pref.
  - `maho.update.crash_sentinel` (dict `{version, count, started_at}`) — local-state pref.
- P0.3 Add `maho/browser/updates/` empty dir with placeholder `OWNERS` + `README.md` linking the design doc.
- **Verification**: `bun run typecheck`, `bun run test`, build chrome target.

### Phase 1 — Core manager (no platform delegate yet) (1 PR)
- P1.1 Implement `MahoUpdateManager` per §1 contract with a stub `PlatformUpdaterDelegate` (`Noop` impl that returns `kUpToDate` after a posted task).
- P1.2 `RolloutBucket` — pure-stdlib HMAC-SHA256 over `install_id || "rollout"` mod 100. Unit-tested with fixed install_id vectors.
- P1.3 `CrashLoopSentinel` — writes "starting" sentinel on `MahoBrowserMainExtraParts::PreMainMessageLoopRun`, clears it after `kStableRuntime = base::Seconds(60)` via a delayed UI-thread task. If on startup the sentinel is found and `count >= 3` for the *current* version, set `last_error_ = kCrashLoop` and skip update apply.
- P1.4 Hook `Initialize()` from `PostProfileInit` (idempotent).
- P1.5 Unit tests: idempotency (3× Init = 1 effective init), observer add/remove during dispatch, bucket determinism, sentinel state transitions.
- **Verification**: `unit_tests --gtest_filter=MahoUpdate*` pass.

### Phase 2 — macOS / Sparkle 2 (1 PR, ‖ with Phase 3 prep tasks)
- P2.1 Vendor `Sparkle.framework` ≥ 2.x. Decision point: GN-tracked binary (`third_party/sparkle/`) **or** Carthage-managed and copied at build time. **Recommendation: vendor the framework directly into the repo at `maho-chromium/third_party/sparkle/Sparkle.framework/`** and add a `LICENSES/sparkle.txt`. Document SHA-256 of the vendored archive in `third_party/sparkle/README.md`.
- P2.2 `maho_update_delegate_mac.mm` (ARC):
  - Owns `SPUStandardUpdaterController` and a strong `MahoSparkleDelegate <SPUUpdaterDelegate>`.
  - Implements `feedURLStringForUpdater:` returning the per-channel appcast URL.
  - Bridges `updater:didFinishUpdateCycleForUpdateCheck:error:` → C++ state machine.
  - `Check()` calls `[controller_.updater checkForUpdatesInBackground]` for background, `checkForUpdates` for manual.
- P2.3 GN: add `cflags_objcc = [ "-fobjc-arc" ]`, `frameworks = [ "Sparkle.framework" ]`, `framework_dirs = [ rebase_path("//maho-chromium/third_party/sparkle") ]`.
- P2.4 `Info.plist` injection for the Maho app bundle:
  - `SUFeedURL` (default stable channel, runtime-overridable via delegate)
  - `SUPublicEDKey` — EdDSA public key (placeholder for now; staging key)
  - `SUEnableAutomaticChecks = NO` (we drive it ourselves)
  - **No `SUPublicDSAKey`** (DSA is not part of this design).
- P2.5 Test: spin a local HTTPS appcast fixture (using `net::EmbeddedTestServer` with a generated cert) and assert that:
  - Valid EdDSA-signed appcast → `kUpdateAvailable`.
  - Tampered DMG (hash mismatch on the published `sparkle:edSignature`) → `kError` with `last_error_ = kSignatureMismatch`.
- **Verification**: macOS browser_tests pass; manual smoke "Check for updates" menu item.

### Phase 3 — Windows MSIX path (1 PR)
- P3.1 `maho_update_delegate_win.cc`:
  - At init, detect packaging via `Package::Current()` — if it throws or returns null (Win32-installed build), fall back to custom-downloader path (Phase 4).
  - For MSIX: spawn `winrt::init_apartment(apartment_type::single_threaded)` on a dedicated `base::Thread` ("MahoUpdateWinRT"), run the `IAsyncAction CheckMsixUpdateAsync()` coroutine, hop the result to UI.
  - Map `PackageUpdateAvailability::Available|Required` → `kUpdateAvailable`; `NoUpdates` → `kUpToDate`; `Error|Unknown` → `kError`.
- P3.2 GN: `libs = [ "windowsapp.lib", "wintrust.lib", "crypt32.lib" ]` under `is_win`.
- P3.3 Tests: gMock the WinRT call via a thin `IPackageUpdateChecker` interface; assert state transitions on each enum value.
- **Verification**: `unit_tests` pass; `chrome` target links cleanly with the new libs (cross-build dry-run via existing chromium-fork-cross-build pipeline).

### Phase 4 — Windows Custom Downloader + signature verification (1 PR; **security-critical**)
- P4.1 `maho_update_signature_win.{h,cc}`:
  - `bool VerifyAuthenticode(const base::FilePath&, std::string_view expected_signer_cn)` using `WinVerifyTrust(NULL, &WINTRUST_ACTION_GENERIC_VERIFY_V2, &data)` plus `CryptQueryObject` to extract the signer cert and compare CN.
  - `bool VerifyManifestSignature(std::string_view manifest_json, base::span<const uint8_t> ed25519_sig, base::span<const uint8_t> public_key)` using boringssl `ED25519_verify` (already in `//third_party/boringssl`).
  - `bool VerifySha256(const base::FilePath&, std::string_view expected_hex)` streaming via `crypto::SecureHash`.
- P4.2 `maho_update_delegate_win.cc` custom-downloader branch:
  - Fetch `https://update.maho.dev/windows/check?channel=<ch>` via `network::SimpleURLLoader` bound to the system `URLLoaderFactory`.
  - Parse manifest (`base::JSONReader`), extract: `version`, `installer_url`, `installer_sha256`, `installer_signer_cn`, `rollout_bucket`, `manifest_signature`.
  - Apply rollout: skip if `RolloutBucket::Compute(install_id) >= manifest.rollout_bucket`.
  - Download installer to `base::FilePath` in `DIR_TEMP` while streaming SHA-256.
  - **Mandatory verification gates, in this exact order**:
    1. Manifest EdDSA signature against embedded public key. **Fail → `kError(kManifestUnsigned)`**.
    2. Streamed SHA-256 == manifest's `installer_sha256`. **Fail → `kError(kHashMismatch)`**.
    3. `VerifyAuthenticode(installer, signer_cn)`. **Fail → `kError(kAuthenticodeFailed)`**.
  - Only on all-pass: store path + planned launch args; transition to `kReadyToInstall`.
- P4.3 Embed the EdDSA public key as a `kMahoUpdateManifestPublicKey` `constexpr std::array<uint8_t, 32>` in a generated header from `//maho/keys/manifest_pubkey.bin`. Build rule: `action("generate_update_pubkey")` in `maho-chromium/browser/BUILD.gn`.
- P4.4 Installer launch (on browser shutdown only):
  ```cpp
  base::LaunchOptions options;
  options.start_hidden = true;
  base::CommandLine cmd(verified_installer_path_);
  // Toolchain-specific. Maho ships NSIS; see installer/README.
  cmd.AppendArg("/S");
  base::LaunchProcess(cmd, options);
  ```
  Add a `// TODO(maho): if installer toolchain changes, update flags here AND installer/README.md` comment with the table from the design doc.
- P4.5 Tests: 3 negative fixtures (tampered manifest, tampered installer hash, unsigned/wrong-CN installer), 1 positive fixture. All assert exact `last_error_` values.
- **Verification**: `unit_tests` pass; manual run against a self-hosted fixture manifest+installer.

### Phase 5 — Linux backends (1 PR; ‖ internal sub-tasks)
- P5.1 `maho_update_delegate_linux_pkg.cc` — banner-only:
  - HTTPS GET signed JSON `latest-stable.json` (same EdDSA verification as Windows).
  - Compare semver to `chrome::kChromeVersion`-equivalent constant for Maho.
  - On newer: emit observer event with copy: `"A new version of Maho is available. Run: sudo apt update && sudo apt upgrade maho-browser"` (string parameterized for dnf).
  - Does NOT shell out to `apt`/`dnf`. Does NOT install repo file or GPG key — that's the packaging script's job (out of scope here, but tracked in §6).
- P5.2 `maho_update_delegate_linux_flatpak.cc` — pure noop with banner: `"Updates are managed by Flatpak."`
- P5.3 `maho_update_delegate_linux_appimage.cc`:
  - Read `update_information` ELF section from `/proc/self/exe` via `libelf` **or** parse the `AppImage`'s embedded zsync URL via the documented offset (prefer the ELF section approach to avoid taking on `libelf`).
  - Bundle `libappimageupdate` as an optional dep; if not available at build time, fall back to a banner that links the download page. **Default: banner-only**, opt in to in-process delta via `maho_appimage_delta = true`.
- P5.4 `maho_update_delegate_linux_noop.cc` — banner: `"Auto-update is disabled for this build."` for `tarball` / unknown packaging.
- P5.5 GN selection block per design doc §4.
- P5.6 Tests: each backend instantiable on Linux dev host with the matching `maho_linux_packaging` value.
- **Verification**: at minimum `pkg` and `noop` build & link on Linux; `flatpak` and `appimage` build (link without runtime exercise) on Linux.

### Phase 6 — Channels, staged rollout, rollback wiring (1 PR)
- P6.1 `SetChannel(UpdateChannel)` persists `maho.update.channel`, asks the delegate to refresh feed URL, and emits `OnUpdateStateChanged(kIdle)`.
- P6.2 Settings UI surface: add a "Update channel" combobox to `chrome://settings/maho` (or wherever Maho settings live; verify with explore) bound to the pref. **Defer to Phase 7 if settings surface requires non-trivial WebUI work** — note in handoff.
- P6.3 Rollout bucket consulted in:
  - macOS: via `feedURLStringForUpdater:` returning `appcast-stable.xml?b=<bucket>` (server filters).
  - Windows custom: client-side compare against manifest's `rollout_bucket`.
  - MSIX: server-side only (bucket cannot influence WinRT call).
  - Linux pkg: server-side only.
- P6.4 Rollback path:
  - On `kCrashLoop` detection at startup, set `state_ = kError(kCrashLoop)`, write a `local_state` flag `maho.update.rollback_requested = true`.
  - macOS: surface UI directing user to download last-known-good DMG (we do not auto-downgrade on macOS — destructive vs profile).
  - Windows MSIX: invoke `PackageManager::RemovePackageAsync` + reinstall LKG `.msix` from cached path **only if user confirms**.
  - Windows custom: present LKG installer path (cached during last successful update) and request user confirmation.
  - Linux: banner only.
- P6.5 Tests: bucket-throttle (49 vs 50 boundary), crash-loop trip after 3 starts, rollback flag persistence across restart.

### Phase 7 — Observability + UI surface (1 PR)
- P7.1 UMA histograms: `Maho.Update.State`, `Maho.Update.LastError`, `Maho.Update.CheckLatency`, `Maho.Update.RolloutBucket` (capped at 100 buckets to avoid cardinality blow-up).
- P7.2 `chrome://maho-about` (or existing `chrome://settings/help`) wired to manager: state, last check time, channel selector, "Check now" button.
- P7.3 Toolbar/menu hook for "An update is available — Restart" badge when `state == kReadyToInstall`.

---

## 3. Review-Finding Traceability

Every review finding from the prior pass MUST appear here with the phase that addresses it. If a row is unchecked at merge time, the merge is blocked.

| # | Severity | Finding | Phase / Task |
|---|----------|---------|--------------|
| 1 | High | Sparkle 2 API (drop `SUUpdater`) | P2.2 |
| 2 | Med  | EdDSA only, drop DSA | P2.4 (`SUPublicEDKey` only) |
| 3 | High | Windows installer signature verification | P4.1 + P4.2 (3-gate verification) |
| 4 | Low  | `/S` is NSIS-specific | P4.4 (TODO + table) |
| 5 | Low  | Drop Linux cron-job suggestion | P5.1 (banner-only, no cron, no repo writes) |
| 6 | High | Channels / staged rollout / rollback | Phase 6 (entire) |
| 7 | Med  | Threading/sequencing model | §1 contract + P1.1 (`SEQUENCE_CHECKER`) |
| 8 | Low  | MSIX example fully implemented | P3.1 (real `IAsyncAction` coroutine) |
| 9 | Low  | Linux delegate split | P5.1–P5.4 + GN block |
| 10 | Low  | Path convention note | Plan §1 (also reflected in design doc header) |
| 11 | Low  | `MahoUpdateObserver` virtual destructor | §1 (`base::CheckedObserver` + `~Observer() override = default;`) |
| 12 | Low  | `Initialize()` idempotency | P1.1 + P1.5 test |
| 13 | Low  | ARC opt-in for `.mm` | P2.3 (`cflags_objcc = [ "-fobjc-arc" ]`) |

### Additional improvements (from the doc rewrite, also tracked here)
| # | Improvement | Phase / Task |
|---|-------------|--------------|
| A1 | Add `kVerifying` UpdateState | P1.1 (state machine) |
| A2 | WinRT/Authenticode `libs` in GN | P3.2 + P4.2 |
| A3 | Mermaid diagram label fix (`PostBrowserStart` → `PostProfileInit`) | Already in design doc; no code change |
| A4 | `base::CheckedObserver` for safe re-entrant remove | §1 + P1.1 |
| A5 | Crash-loop sentinel | P1.3 + P6.4 |
| A6 | HMAC-based stable rollout bucket | P1.2 + P6.3 |
| A7 | Last-known-good pin | P6.4 |
| A8 | `maho_linux_packaging` GN arg + runtime fallback | P0.1 + P5.5 |
| A9 | Vendored Sparkle with SHA-256-pinned README | P2.1 |
| A10 | Generated `kMahoUpdateManifestPublicKey` header | P4.3 |

---

## 4. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Sparkle 2 ABI drift (vendored framework) | Med | Med | Pin via SHA-256 in `third_party/sparkle/README.md`; bump deliberately. |
| WinRT availability on Win10 LTSC w/o MSIX | Med | High | MSIX path falls back to custom downloader if `Package::Current()` throws. |
| EdDSA private key compromise | Low | Catastrophic | Key lives offline; CI signs via HSM-backed signing service (out of scope here, tracked in §6). |
| `WinVerifyTrust` revocation check hangs offline | Med | Med | Set `dwUIChoice = WTD_UI_NONE`, `fdwRevocationChecks = WTD_REVOKE_NONE` for offline-tolerance, with explicit comment justifying why (the manifest hash + Authenticode chain is the auth boundary, not OCSP). |
| Crash-loop false positives during legitimate slow startup | Med | Med | `kStableRuntime = 60s` is conservative; sentinel only trips after 3 consecutive crashes. |
| Vendor-tracking conflict with upstream Chromium roll | Low | Low | All new files live under `maho-chromium/`; no upstream files modified except `BUILD.gn` patches via existing override mechanism. |
| `winrt::init_apartment` thread interactions with Chromium's COM init | Low | High | Dedicated `base::Thread` with explicit STA init, never share with the UI thread's COM. |

---

## 5. Out-of-Repo Dependencies (tracked, not implemented here)

These block end-to-end functionality but are separate workstreams:
- **Update server / appcast / manifest CDN**: Maho-hosted endpoints producing EdDSA-signed manifests + signed appcasts.
- **EdDSA signing key generation + storage**: HSM-backed signer in CI.
- **Windows installer toolchain** (NSIS recommended): produces Authenticode-signed `.exe` + `.msix`.
- **Linux repo signing keys** + apt/dnf repo hosting: handled in packaging workstream; this plan only consumes the resulting URLs.
- **AppImageUpdate library**: optional, gated by `maho_appimage_delta`.

---

## 6. Verification Plan (per-PR commands)

Run from repo root, in this order, on each PR before request-review:

```bash
# Rust workspace sanity
bun run typecheck
bun run test

# Chromium narrow target — fastest gate
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests

# Run our tests
chromium/src/out/Default/unit_tests --gtest_filter='MahoUpdate*'

# Full chrome target — slower, gates Phase 2/3/4 PRs
python3 maho-chromium/build/scripts/build_maho.py --ninja-target chrome
```

Per-PR exit criteria: all four commands exit 0; new tests in the touched phase pass; existing `MahoUpdate*` tests do not regress.

---

## 7. Rollout Plan for *this* feature itself

(Recursive: how do we ship the auto-updater itself safely?)
1. Phase 0–1 land behind a kill-switch local-state pref `maho.update.enabled` (default `false`).
2. Phase 2 (mac) → enable on internal dev channel only, manual flip per machine.
3. Phase 3+4 (win) → enable on internal dev channel only, manual flip per machine.
4. Phase 5 (linux) → banner-only is safe to enable broadly.
5. Phase 6 wires the user-visible channel selector; flip kill-switch default to `true` only after all platform flows have completed at least one full check-→download-→verify-→apply cycle on a fixture server.

---

## 8. Open Questions (to resolve before Phase 2 starts)

1. **Sparkle vendoring location**: `maho-chromium/third_party/sparkle/` (tracked in maho-chromium repo) vs. `chromium/src/third_party/sparkle/` (gitignored, fetched). Recommendation: the former.
2. **EdDSA pubkey rotation strategy**: single key with manual rotation, or pin a list of N trusted keys? Recommendation: list of N, embed as `std::array<std::array<uint8_t, 32>, N>`.
3. **Windows: do we ship MSIX as primary or keep `.exe` as primary?** Recommendation: ship both, default download CTA on website is `.exe`, MSIX path for Store-equivalent customers. Plan supports either.
4. **Phase 6 settings UI: WebUI page or native Views?** Defer; spike during P6.2.
5. **Channel naming for canary**: align with Chromium's `dev`/`canary` distinction or merge into one `nightly`? Recommendation: keep both, mirror Chromium.
