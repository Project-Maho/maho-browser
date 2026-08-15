# Chromium desktop macOS E2E

The approved desktop direction is an isolated macOS VM run. The checked-in CLI
exposes `doctor`, `prepare`, `list`, and `run` around a strict manifest. The
historical Accessibility sidebar runner is unavailable, so its commands are not
supported entrypoints. `doctor`, `prepare`, and `list` have live receipts in
the workspace runbook; the clean-guest `run all` proof is still pending.

For the canonical sidebar drag-and-drop capability matrix, current product
gaps, and testing strategy, see `docs/chromium-sidebar-dnd-reference.md`.

The workspace-level `docs/operations/macos-vm-e2e.md` is the normative runbook
for VM lifecycle, privacy, artifacts, and failure handling.

## Approved CLI today

Run from the workspace root:

```bash
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py doctor \
  --state-dir /Volumes/maho-build/maho-vm-state \
  --identity ~/.cache/maho-macos-vm/id_ed25519
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py prepare \
  --state-dir /Volumes/maho-build/maho-vm-state \
  --identity ~/.cache/maho-macos-vm/id_ed25519
python3 maho-chromium/build/scripts/macos_vm_e2e.py list --json
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py run \
  --suite all \
  --golden maho-e2e-golden \
  --identity ~/.cache/maho-macos-vm/id_ed25519 \
  --state-dir /Volumes/maho-build/maho-vm-state
```

This workstation keeps Tart's images and the CLI's ownership state on the
external `/Volumes/maho-build` volume. Tart inherits `TART_HOME` from the
environment, while `--state-dir` (or `MAHO_MACOS_VM_STATE`) governs Maho's own
ownership state. `list` reads only the checked-in manifest, so it takes
neither. The SSH identity stays at `~/.cache/maho-macos-vm/id_ed25519`.

`doctor` reports host readiness, `prepare` creates or reuses the owned golden
image, `list` validates `build/scripts/macos_vm_e2e_manifest.json` and lists:

- suites: `interactive-smoke`, `ai-mock`, and `all`;
- AI scenarios: `ai_panel_renders`, `ai_real_turn`,
  `ai_mock_general_turn`, `ai_mock_quick_action_context`,
  `ai_mock_server_error`, and `ai_mock_key_cleared_no_request`.

`run` accepts `interactive-smoke`, `ai-mock`, or `all`. A successful `doctor`,
`prepare`, or `list` invocation is not E2E proof, and the accepted live `run
all` receipt remains pending in the workspace-level runbook.

Two argument-level behaviors are confirmed here: `--help` exits `0`, and `run
--suite <unknown>` is rejected against the manifest choices with exit `2`
before any VM is created.

## Run contract

The shipped `run` command clones a disposable VM from `maho-e2e-golden`, builds
on the host only in the dedicated host output `chromium/src/out/VM-E2E`, shares
the workspace read-only so the guest sees it at `/Volumes/My Shared
Files/maho-workspace` and remounts it read-only at `/Users/Shared/maho-workspace`,
falls back only to a declared runtime-deps mirror at that same guest path when
necessary, uses a fresh browser profile per suite, binds CDP and the mock LLM
to loopback, exports proof, and deletes the run VM. The `all` suite expands to
`interactive-smoke` followed by `ai-mock`.

The AI suite must run the existing AI panel harness in mock mode. Even
`ai_real_turn` uses the in-guest mock endpoint under this contract. Real
provider credentials and a host Maho profile are forbidden in the VM.

## Privacy, artifacts, and failures

- Never copy host profiles, cookies, keychains, `.env` files, SSH material, API
  keys, or provider tokens into the golden image or disposable guest.
- Never export a browser profile, VM disk, keychain, raw environment dump, or
  source-profile preferences as an artifact.
- Required proof is the validated manifest, a machine-readable run summary,
  provisioning/build/suite logs, gtest results, per-scenario AI results,
  bounded failure diagnostics, and teardown status.
- `passed` means every expanded suite passed, required proof reached the host,
  and teardown completed. Assertion failures are `failed`; build, launch,
  timeout, export, and cleanup failures are `infra_error`; an unstarted suite is
  `not_run` and never a pass.
- Preserve the first causal failure, collect diagnostics on a best-effort
  bounded basis, and do not retry automatically.

Live proof remains pending until one fresh VM runs `all` once without retries,
exports a privacy-reviewed proof package, tears down successfully, and has its
receipt attached to the workspace-level runbook.

Mobile Appium remains a separate iOS/Android simulator, emulator, or device
layer. It is not part of this desktop VM suite.
