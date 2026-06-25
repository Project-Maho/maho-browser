# Maho Auto-Update — Browser ↔ Relay End-to-End Wiring Plan

**Created**: 2026-06-15 JST
**Source**: Follow-up to `.omo/plans/maho-browser-auto-update-implementation-plan-2026-06-14.md`
**Predecessor work** (already merged in same workspace, on disk):
- Browser side update subsystem (`maho-chromium/browser/updates/`): code, tests, kill-switch — green
- Relay side update endpoints (`maho/relay/server/src/updates/`, `/updates/*` routes, `maho-update-tool` binary): code, tests, smoke-tested end-to-end — green
**Status**: Plan-only. No code written for this plan yet.
**Scope**: Close the dev-loop between browser and relay so that an end-to-end "Check for updates" cycle works on a developer's local machine, then ship the operator-side checklist for production.

---

## 0. Goal

A developer running `python3 maho-chromium/build/scripts/build_maho.py` and `cd maho/relay && cargo run --release` on the same host can:

1. Trigger a check from inside the running Maho.app bundle.
2. See the browser hit `http://localhost:18080/updates/...` (NOT `https://update.maho.dev`).
3. Verify the EdDSA-signed envelope with the matching public key embedded at build time.
4. On a fixture release: transition `kIdle → kChecking → kUpdateAvailable` (or `kReadyToInstall` after download+verify).
5. On an unrelated tampered fixture: transition to `kError` with the correct `UpdateError` code.

### Non-goals
- Production CDN / DNS / TLS for `update.maho.dev` (handled by ops, separate from this plan).
- HSM-backed signing service (current file-backed `update-key.bin` is enough for dev; production hardening is its own workstream).
- EV code-signing certificate procurement (Windows, separate procurement).
- Phase 7 user-visible settings UI ("Check for updates" button, channel selector, restart badge) — sketched here as Phase F4 but explicit *implementation* deferred.

### Success criteria (binary)
- [ ] `bun run typecheck` and `bun run test` clean from repo root.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --ninja-target chrome` succeeds with the new override mechanism in place.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests` and the Maho update tests pass (no regressions in `MahoUpdateManagerTest.*` or `SignatureTest.*`).
- [ ] `cd maho/relay && cargo test --workspace` clean.
- [ ] `cd maho/relay && cargo clippy --all-targets --all-features -- -D warnings` clean.
- [ ] On macOS dev host: a fresh local build, run with `MAHO_UPDATE_SERVER_OVERRIDE=http://localhost:18080` and `maho.update.enabled=true`, against a relay seeded with `maho-update-tool sign-release ... --version 9.9.9.9`, transitions `kIdle → kChecking → kUpdateAvailable` and surfaces a Sparkle dialog (manual smoke test, captured in plan §4 verification).
- [ ] Tampering test: replacing the public key on the browser side with a non-matching one, the same flow ends at `kError(kSignatureMismatch)` (Mac) or `kError(kManifestUnsigned)` (Win/Linux).
- [ ] No code path on the browser side reads `https://update.maho.dev` if `MAHO_UPDATE_SERVER_OVERRIDE` env var is set. (Direct grep test in the plan.)

---

## 1. Architecture Snapshot — How the Pieces Fit

```
                                    ┌────────────────────────────────────────┐
                                    │  Operator workflow (offline / CI)      │
                                    │                                        │
                                    │  maho-update-tool keygen               │
                                    │     → private.bin  (relay-side)        │
                                    │     → public.bin   (browser-side)      │
                                    │     → SUPublicEDKey base64 (Info.plist)│
                                    │                                        │
                                    │  maho-update-tool sign-release ...     │
                                    │     → manifest.json + binary           │
                                    └────────────────────────────────────────┘
                                                     │
                                                     ▼
        ┌─────────────────────────────────────────────────────────────────┐
        │                   maho-relay (axum + SeaORM)                   │
        │   GET /updates/windows/check?channel=stable                     │
        │   GET /updates/macos/appcast?channel=stable                     │
        │   GET /updates/linux/check?channel=stable                       │
        │   GET /updates/{platform}/{channel}/{filename}                  │
        └─────────────────────────────────────────────────────────────────┘
                                                     ▲
                                                     │ HTTPS in prod / HTTP in dev
                                                     │
        ┌─────────────────────────────────────────────────────────────────┐
        │              Maho.app (chromium overlay + maho/browser/updates) │
        │                                                                 │
        │   delegate_mac.mm   ──► Sparkle ──► /updates/macos/appcast      │
        │   delegate_win.cc   ──► /updates/windows/check + binary         │
        │   delegate_linux_pkg.cc ──► /updates/linux/check                │
        │                                                                 │
        │   Base URL: BuildBaseUrl(env, pref) ── default                  │
        │                                       https://update.maho.dev  │
        │                                                                 │
        │   manifest_pubkey.bin (Win/Linux envelope)                      │
        │   Info.plist:SUPublicEDKey  (Sparkle DMG)                       │
        └─────────────────────────────────────────────────────────────────┘
```

### Single source of truth for keys (re-asserted)

The plan reuses the **one Ed25519 keypair** for all three platforms, since both Sparkle and the cross-platform envelope verifier are RFC 8032 ed25519. So `keygen` produces:

| Output | Path / Sink | Consumer |
|---|---|---|
| Private 32-byte seed | `<relay-host>/var/lib/maho-relay/update-key.bin` (chmod 600) | `maho-relay --update-private-key-path` |
| Public 32-byte raw bytes | `maho-chromium/browser/keys/manifest_pubkey.bin` | `generate_update_pubkey.py` → `kMahoUpdateManifestPublicKey` (Win/Linux) |
| Public key base64 (printed by tool) | `apply_chromium_src_overrides.py` → `chrome/app/app-Info.plist` `SUPublicEDKey` | Sparkle (Mac) |

---

## 2. Why a New Plan Is Needed

The 2026-06-14 plan ended its open questions with #1–#5 unresolved. This plan resolves the ones that block dev-loop and explicitly defers the rest.

| 2026-06-14 plan §8 question | This plan resolves? |
|---|---|
| Sparkle vendoring location | Already resolved during impl: `maho-chromium/third_party/sparkle/` |
| EdDSA pubkey rotation: single vs N-key list | **Decided here**: single key for now; a list-of-N is added as Phase F5 (deferred) |
| Windows `.exe` vs MSIX as primary | Out of scope here; both code paths exist, packaging chooses |
| Phase 6 settings UI: WebUI vs native Views | Out of scope here; sketched in F4 (deferred) |
| Channel naming: keep `dev` + `canary` separate | Already resolved in code; no change |

The work this plan adds is **strictly the wiring that lets browser and relay talk to each other**, plus the operational checklist that makes the public key story coherent.

---

## 3. Phasing

Phases F1–F3 land in this order, are merge-blocking for "actually shipping a dev cycle". F4–F6 are explicit deferrals. Within a phase, tasks marked `‖` may be done in parallel.

### Phase F1 — Browser-side base URL override (1 PR)

**Goal**: Replace the hardcoded `https://update.maho.dev` with a function that resolves to `env(MAHO_UPDATE_SERVER_OVERRIDE) || pref(maho.update.server_url) || compile-time default`.

**Why all three layers**:
- Compile-time default → production behavior, no env/pref needed
- Pref override → for stable per-profile dev configurations
- Env var override → for one-shot test runs without touching prefs

**Tasks**:
- F1.1 Add `kMahoUpdateServerUrl` profile pref to `maho_update_pref_names.h` + `maho_update_prefs.cc::RegisterProfilePrefs`. Default empty string. Do **not** add to `apply_chromium_src_overrides.py` (already calls `RegisterProfilePrefs` which picks it up).
- F1.2 Add `maho/browser/updates/maho_update_server_url.{h,cc}`:
  - `std::string ResolveUpdateServerBaseUrl(const Profile* profile)` returns the resolved URL with no trailing slash. Order: env, pref, default.
  - Compile-time default exposed as `constexpr char kMahoUpdateServerDefaultUrl[] = "https://update.maho.dev"`.
  - Hard-block `file://` and any non-`http(s)://` scheme; fall back to default + `LOG(ERROR)`.
- F1.3 Wire it through three call sites:
  - `delegate_mac.mm::feedURLStringForUpdater:` — change from hardcoded `https://update.maho.dev/macos/appcast-{channel}.xml?b={bucket}` to `{base}/updates/macos/appcast?channel={channel}&b={bucket}`.
  - `delegate_win.cc::CheckCustomDownloaderUpdate` — change from hardcoded `https://update.maho.dev/windows/check?channel={ch}` to `{base}/updates/windows/check?channel={ch}`.
  - `delegate_linux_pkg.cc::Check` — change from hardcoded `https://update.maho.dev/linux/{channel}/latest.json` to `{base}/updates/linux/check?channel={channel}`.
- F1.4 Add `MAHO_UPDATE_SERVER_OVERRIDE` env var support. Read once during `MahoUpdateManager::Initialize`, cache in `manager.url_base_`. Expose as `MahoUpdateManager::GetServerBaseUrl()` so delegates pull from one source.
- F1.5 BUILD.gn: add `maho_update_server_url.cc/h` to the `maho_updates` source_set. Update `MahoUpdateManager` ctor signature if needed.
- F1.6 Tests:
  - `MahoUpdateManagerTest.UrlOverride_EnvVarBeatsPref` — set env, set pref, env wins.
  - `MahoUpdateManagerTest.UrlOverride_PrefBeatsDefault` — env unset, pref set.
  - `MahoUpdateManagerTest.UrlOverride_RejectsFileScheme` — falls back to default.
  - `MahoUpdateManagerTest.UrlOverride_StripTrailingSlash` — `http://x:1/` → `http://x:1`.
- F1.7 Doc: add to `docs/maho_browser_update_architecture.md` § channel/URL section.

**Verification** (Phase F1):
```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests
chromium/src/out/Default/unit_tests --gtest_filter='MahoUpdateManagerTest.UrlOverride_*' --test-launcher-jobs=1
```
All four URL override tests pass.

### Phase F2 — Replace public-key placeholders (1 PR; ‖ with F1.x once F1 lands)

**Goal**: Generate one real Ed25519 keypair, push the public half into both the browser-side embedded array and the macOS Info.plist patch.

**Tasks**:
- F2.1 Author script: `maho-chromium/build/scripts/install_dev_update_key.sh` (committed, NOT a one-shot CLI). Inputs: path to a private key file + path to a public key file produced by `maho-update-tool keygen`. The script:
  - Copies the public key into `maho-chromium/browser/keys/manifest_pubkey.bin`
  - Computes base64 of the public key
  - In-place edits `maho-chromium/build/scripts/apply_chromium_src_overrides.py` to replace the literal `MAHO_UPDATE_SUPUBLICEDKEY_PLACEHOLDER` with the base64 string
  - Refuses to run if the input key is all zeros
  - Prints next-steps banner ("rebuild Maho.app, restart relay")
- F2.2 The script does **not** commit any of the changes. The dev runs it once, then commits manually to a private branch (since the public key is per-environment / per-cohort).
- F2.3 Add `--check-no-placeholder` mode to `apply_chromium_src_overrides.py` that exits non-zero if the literal `MAHO_UPDATE_SUPUBLICEDKEY_PLACEHOLDER` still appears anywhere in the patch dictionary. Wire it into `build_maho.py --release` as a hard gate (not enabled for dev builds).
- F2.4 Update `maho-chromium/browser/keys/README.md` "DO NOT SHIP" section to point at this exact script.
- F2.5 Update `maho/relay/README.md` operator workflow to reference the script by name (so the keygen step is one-stop).

**Verification** (Phase F2):
```bash
# Dev path
maho-chromium/build/scripts/install_dev_update_key.sh \
    --private /tmp/maho-keys/private.bin \
    --public /tmp/maho-keys/public.bin

# manifest_pubkey.bin no longer all-zeros
xxd maho-chromium/browser/keys/manifest_pubkey.bin | grep -qv '^[^:]*: 0000 0000 0000 0000 0000 0000 0000 0000  ................$' \
    || { echo "ERROR: still placeholder"; exit 1; }

# apply_chromium_src_overrides.py no longer contains the placeholder string
! grep -q MAHO_UPDATE_SUPUBLICEDKEY_PLACEHOLDER maho-chromium/build/scripts/apply_chromium_src_overrides.py \
    || { echo "ERROR: placeholder still present"; exit 1; }

# Release build's pre-flight check would reject placeholder
python3 maho-chromium/build/scripts/apply_chromium_src_overrides.py --check-no-placeholder
```

### Phase F3 — End-to-end smoke harness (1 PR)

**Goal**: One script + one set of fixtures that exercise the full loop, callable from CI later.

**Tasks**:
- F3.1 Add `maho-chromium/build/scripts/update_e2e_smoke.sh`:
  - Stand up a temp relay on port 18080 with `--no-require-auth` and the dev key from F2
  - `maho-update-tool sign-release` for each of `windows`, `macos`, `linux` against fixture binaries
  - Returns the relay PID so the caller can `kill` it
- F3.2 Add a Python harness `maho-chromium/build/scripts/update_e2e_check.py`:
  - Calls F3.1's script
  - Hits each `/updates/*` endpoint via `urllib`
  - Verifies the Ed25519 envelope using `cryptography` lib (mirroring the smoke test we ran during impl)
  - Asserts tampered payload → reject
  - Cleans up
- F3.3 Document this in `maho/relay/README.md` as "End-to-End Smoke Harness" (replace the inline `Local dev: end-to-end loop` section's hand-coded recipe).
- F3.4 Optional follow-up (not in this PR): `bun run test:update-e2e` proxy from the workspace root.

**Verification** (Phase F3):
```bash
maho-chromium/build/scripts/update_e2e_smoke.sh
python3 maho-chromium/build/scripts/update_e2e_check.py
# expected: "All endpoints OK (4 valid, 1 tamper rejected)"
```

### Phase F4 (deferred) — Phase 7 settings UI surface

Not in this plan's merge scope. Tracked here only so the dependency graph is explicit:

- F4.1 `chrome://settings/help` (or new `chrome://maho-settings#updates`) wires "Check for updates" button + state badge.
- F4.2 Channel selector → writes `maho.update.channel` pref.
- F4.3 "An update is available — Restart" toolbar/menu badge when `state == kReadyToInstall`.

Depends on: F1 + F2 (so the buttons aren't dead). Doesn't depend on F3.

### Phase F5 (deferred) — Public key rotation

Not in this plan's merge scope.

- F5.1 Change `manifest_pubkey.bin` shape from `[u8; 32]` to a concatenation of N keys (`u32 count` followed by N×32 bytes).
- F5.2 `generate_update_pubkey.py` emits `kMahoUpdateManifestPublicKeys` as `std::array<std::array<uint8_t, 32>, N>`.
- F5.3 `VerifyManifestSignature` returns true if any of the N keys verifies the signature.
- F5.4 Same expansion for Sparkle: `SUPublicEDKey` becomes a comma-separated list (Sparkle 2 supports multiple keys via separate plist entries — verify exact mechanism in Sparkle 2.6 docs before scoping).

### Phase F6 (deferred) — Production hardening

Not in this plan's merge scope. Captured here as the "what we know we'll need" list:

| Item | Type |
|---|---|
| `update.maho.dev` DNS + TLS termination (caddy/nginx in front of relay) | Ops |
| HSM/KMS-backed `UpdateSigner` impl behind the existing `from_path` interface | Ops + small server PR |
| Windows EV Code Signing certificate + `signtool.exe` integration in CI | Procurement + CI |
| Apple Developer ID notarization of `.dmg` (likely already exists for Maho) | CI |
| Automated rollout-bucket bump cadence + emergency stop button | Ops + admin endpoint |

---

## 4. Verification Plan (per-PR commands)

Run from repo root, in this order, on each PR before request-review:

```bash
# Rust workspaces (browser + relay)
bun run typecheck
bun run test
cd maho/relay && cargo test --workspace
cd maho/relay && cargo clippy --all-targets --all-features -- -D warnings

# Chromium narrow target
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests
chromium/src/out/Default/unit_tests \
    --gtest_filter='MahoUpdateManagerTest.*:SignatureTest.*' \
    --test-launcher-jobs=1

# After F3 lands:
maho-chromium/build/scripts/update_e2e_smoke.sh
python3 maho-chromium/build/scripts/update_e2e_check.py
```

Per-PR exit criteria: all five exit 0; new tests in the touched phase pass; existing `MahoUpdate*`, `Signature*`, and relay `updates` tests do not regress.

---

## 5. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Forgetting to run F2 install script before release build | High | Critical (placeholder shipped) | F2.3's `--check-no-placeholder` is a hard gate in `--release` mode |
| Pref/env URL override accidentally enabled in release builds | Med | High (telemetry leak / wrong server) | F1's resolver logs WARNING when override is non-default; UMA `Maho.Update.ServerUrlSource` enum to detect post-ship |
| Single key compromise → full update fleet breach | Low | Catastrophic | Phase F5 (key list) is the planned mitigation; for now key lives offline + chmod 600 |
| Browser caches old base URL across pref changes | Low | Med | `MahoUpdateManager` reads URL once per `Initialize`; SetChannel rebuilds; no other caching |
| Mac patch in `apply_chromium_src_overrides.py` hard-edited by F2 script — patch reapplication breaks | Med | Med | F2 script edits idempotently; if both `MAHO_UPDATE_SUPUBLICEDKEY_PLACEHOLDER` and the real value are absent, exit cleanly |

---

## 6. Open Questions (to resolve before Phase F1 starts)

1. **Pref scope: profile vs local-state for `maho.update.server_url`?** Current proposal: profile (per-profile override is more useful for dev; local-state was only used for `install_id` and crash sentinel which are device-wide). Recommend profile.
2. **Should env var override be silently ignored in non-DCHECK builds?** Current proposal: log + apply unconditionally; production builds shouldn't have the env var set anyway, and shipping the override knob is consistent with Brave/Chrome dev patterns.
3. **Which exact channel does the Maho stable build self-identify as?** This affects `delegate_mac.mm`'s `feedURLStringForUpdater:` if no channel pref exists yet. Plan: default `kMahoUpdateChannel` to `"stable"` (already done).
4. **F3 fixture binaries: real DMG/EXE or short scratch files?** Real binaries trigger Sparkle's full validation path (good); scratch files are faster (good for CI). Recommend: scratch in F3.1 by default, real-binary mode opt-in via env flag.

---

## 7. Out-of-Repo Dependencies

(For F6, separately tracked.)

- **Update CDN / DNS / TLS**: needs `update.maho.dev` registered, A record, cert, reverse proxy in front of `maho-relay` (caddy/nginx pattern from existing relay deployment).
- **HSM / KMS**: production replacement for file-backed `update-key.bin`. The `UpdateSigner` trait is already shaped for this (`from_path` is the only async constructor; an `from_kms(arn: &str)` is a drop-in).
- **EV cert + Apple Developer ID**: out of dev-loop scope; needed before any production `.exe` or `.dmg` ships.

---

## 8. File-by-File Summary (what F1+F2+F3 will touch)

```
# Phase F1
maho-chromium/browser/updates/maho_update_pref_names.h           (+1 const)
maho-chromium/browser/updates/maho_update_prefs.cc               (+1 RegisterStringPref)
maho-chromium/browser/updates/maho_update_server_url.{cc,h}      新규
maho-chromium/browser/updates/maho_update_manager.{cc,h}         (cache base url; expose getter)
maho-chromium/browser/updates/maho_update_delegate_mac.mm        (use base url)
maho-chromium/browser/updates/maho_update_delegate_win.cc        (use base url)
maho-chromium/browser/updates/maho_update_delegate_linux_pkg.cc  (use base url + URL shape change)
maho-chromium/browser/updates/maho_update_manager_unittest.cc    (+4 URL override tests)
maho-chromium/browser/BUILD.gn                                    (+source files)
docs/maho_browser_update_architecture.md                         (+ section)

# Phase F2
maho-chromium/build/scripts/install_dev_update_key.sh            신규
maho-chromium/build/scripts/apply_chromium_src_overrides.py      (+ --check-no-placeholder)
maho-chromium/browser/keys/README.md                             (+ script ref)
maho/relay/README.md                                             (+ script ref)

# Phase F3
maho-chromium/build/scripts/update_e2e_smoke.sh                  신규
maho-chromium/build/scripts/update_e2e_check.py                  신규
maho/relay/README.md                                             (replace inline recipe)
```

**Total**: 3 PRs, ~14 files touched, no new external deps.

---

## 9. Self-Rollout Plan (recursive)

This feature gates itself the same way the browser update subsystem itself does: behind the `maho.update.enabled` kill-switch (still `false` by default).

1. **F1+F2 land** → URL override works, but kill-switch is still off, so no traffic flows. Safe.
2. **F3 lands** → smoke harness validates the loop on a dev machine, again kill-switch off in normal builds.
3. **Internal dev cohort flips `maho.update.enabled=true` manually** → real check runs against `update.maho.dev` (staging). Feedback informs F6 work.
4. **F6 production hardening completes** → kill-switch default flips to `true` for stable channel, staged via the same `rollout_bucket` mechanism the manifest already supports.

No part of F1–F3 changes the kill-switch default. Any flip is a separate, deliberate PR.
