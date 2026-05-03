# Chromium desktop macOS E2E

This harness exercises the Chromium desktop overlay on macOS through Accessibility APIs. It launches the built app bundle, waits for the window to become ready, then runs either the Phase 1 smoke scenarios or the Phase 2 seeded and support contract scenarios. For Chromium overlay setup and build prerequisites, see `docs/chromium-fork-build.md`.

## Harness entrypoint

Run the harness with:

```bash
python3 maho/build/scripts/e2e_chromium_sidebar.py
```

Useful command line flags:

```bash
python3 maho/build/scripts/e2e_chromium_sidebar.py --list
python3 maho/build/scripts/e2e_chromium_sidebar.py --scenario launch_clean_profile
python3 maho/build/scripts/e2e_chromium_sidebar.py --scenario phase2_seeded_favorites_snapshot_assert --scenario phase2_folder_sibling_reorder_blocked
```

`--list` prints every available scenario name and exits. `--scenario` may be repeated. If no scenario is passed, the harness runs every scenario in the order defined by the script.

## Prerequisites

- macOS with Accessibility permission granted to the Terminal or Python process that launches the harness.
- A built app bundle at `/Users/indo/code/project/maho-workspace/chromium/src/out/Release/Maho.app` by default.
- The maho-core prebuilt static library from the maho-chromium build flow, since Chromium is launched from the overlay build output.

If you need a different app bundle location, set `MAHO_APP_PATH`.

## Environment variables

The harness reads these environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `MAHO_APP_PATH` | `/Users/indo/code/project/maho-workspace/chromium/src/out/Release/Maho.app` | Path to the app bundle the harness launches. |
| `MAHO_E2E_ARTIFACT_DIR` | `/tmp/maho-chromium-e2e-spaces` | Root directory for per-scenario artifacts. |
| `MAHO_E2E_KEEP_PROFILE=1` | unset | Keeps the temporary browser profile directory after each scenario. When unset, the profile is removed at the end of the run. |
| `MAHO_E2E_APP_READY_TIMEOUT_MS` | `30000` | Wait time for app readiness and the seeded page polling. |
| `MAHO_E2E_SCENARIO_TIMEOUT_MS` | `60000` | Maximum time allowed for one scenario, including readiness and execution. |
| `MAHO_E2E_SUITE_TIMEOUT_MS` | `600000` | Maximum time allowed for the full suite when multiple scenarios are selected. |

## What the harness does

The harness launches the `Maho` binary inside the app bundle with a temporary `--user-data-dir`, disables first run flows, and opens the default initial URLs from the script. It then waits for macOS Accessibility to expose the main window and known sidebar content before running a scenario.

Artifacts are written under one directory per scenario:

```text
${MAHO_E2E_ARTIFACT_DIR:-/tmp/maho-chromium-e2e-spaces}/<scenario-name>/
```

Each run writes at least:

- `profile_path.txt`
- `launch.log`
- `before.png`
- `after.png`

On failures, the harness also writes the relevant diagnostics, which can include:

- `timeout_reason.txt`
- `ax_tree.txt`
- `buttons.txt`
- `window_list.txt`
- `failure.png`
- `blocked_reason.txt` for the blocked folder sibling reorder scenario

`launch.log` records the exact binary and arguments that were used. `profile_path.txt` records the temporary browser profile path. `before.png` and `after.png` capture the screen around each scenario. Failure artifacts are meant to make it obvious whether the app never became ready, the Accessibility tree changed, or a scenario assertion failed after launch.

## Scenario names

Use `python3 maho/build/scripts/e2e_chromium_sidebar.py --list` to confirm the live list. The current scenarios are grouped below by phase.

### Phase 1 smoke coverage

These scenarios cover the basic desktop harness flow and sidebar behavior:

- `launch_clean_profile`
- `sidebar_populates_real_tabs`
- `open_spaces_surface_via_url_dispatch`
- `switch_tab_from_sidebar`
- `open_spaces_surface_via_footer_action`
- `create_space_entry_opens_surface`

### Phase 2 seeded and support contract coverage

These scenarios assert the seeded test page and the current support contract surface:

- `phase2_seeded_favorites_snapshot_assert`
- `phase2_seeded_folder_snapshot_assert`
- `phase2_pinned_section_support_contract_assert`
- `phase2_folder_support_contract_assert`
- `phase2_tab_reorder_support_contract_assert`
- `phase2_folder_sibling_reorder_blocked`

The last scenario is intentionally a blocked state assertion. It does not claim live drag and drop execution. It verifies that folder sibling reorder remains blocked and not wired in the current product support matrix.

## Common failure outputs

A failed run usually falls into one of these buckets:

- `Build artifact not found: .../Contents/MacOS/Maho`, which means the app bundle was not built at the expected path.
- `Accessibility permission is required. Grant Terminal/Python accessibility access and rerun.`, which means macOS Accessibility permission is missing.
- `App path not found: ...`, which means `MAHO_APP_PATH` points at a bundle that does not exist.
- `Unknown scenarios: ...`, which means one or more scenario names do not match the current script.
- `Suite exceeded timeout of ... ms`, which means the full run hit the suite timeout.
- `Scenario ... exceeded timeout of ... ms`, which means the readiness or scenario work took too long.
- `Maho exited early with code ...`, which means the app process crashed or quit before the scenario could finish.

If you need to keep the temporary profile for inspection, set `MAHO_E2E_KEEP_PROFILE=1` before running the harness.