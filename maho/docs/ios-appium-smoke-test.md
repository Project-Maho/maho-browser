# iOS Appium Smoke Test

A lightweight, repo-local smoke test for the Maho iOS app using Appium and the iOS Simulator.

For the CI-ready command matrix, see `docs/test-matrix.md`.

## Current Layout

The Appium suite lives in `appium-tests/` and is split into two entry points:

- `appium-tests/run-tests.js` — the canonical main regression runner
- `appium-tests/edge-case-tests.js` — crash-prone edge-case coverage

The local package manifest in `appium-tests/package.json` provides the runnable scripts.

## Prerequisites

- macOS with Xcode installed
- iOS Simulator available (current suite defaults to iPhone 17 Pro, iOS 26.4)
- Appium 2.x installed globally or locally: `bunx appium`
- Appium XCUITest driver installed: `bunx appium driver install xcuitest`
- Maho iOS app built for simulator

## Build the App for Simulator

Build the simulator app with the repo’s current iOS target/configuration, then point Appium at the resulting `.app` bundle used by `appium-tests/run-tests.js`.

The main runner currently expects the simulator build at:

```
/Users/indo/Library/Developer/Xcode/DerivedData/Maho-cncqahqhgqnsbaetovgsgoskvekd/Build/Products/Debug-iphonesimulator/Maho.app
```

If your local derived data path differs, update the `APP_PATH` constant inside `appium-tests/run-tests.js` before running the suite.

## Install Appium Dependencies

```bash
cd appium-tests
bun install
```

This installs `webdriverio` locally so the smoke scripts can run without a global module.

## Run the Smoke Test

1. Start Appium server:
```bash
bunx appium
```

2. In another terminal, run the main regression runner:
```bash
cd appium-tests
bun run test
```

3. Run the edge-case suite when you want the crash-prone paths:
```bash
cd appium-tests
bun run test:edge
```

## What the Smoke Test Covers

- Launches the app in the iOS Simulator
- Verifies the bottom-anchored home surface and browsing chrome
- Exercises the explicit submit path, menu interactions, incognito toggle, settings navigation, tab grid, and Browse for Me flows
- Captures screenshots for key states in `appium-tests/screenshots/`
- Keeps `run-tests.js` as the main canonical regression pass and `edge-case-tests.js` as the crash-path pass

## Accessibility Identifiers Used

The smoke test relies on the following accessibility identifiers being present in the iOS app:

- `arcAddressPill`
- `homeSearchField`
- `homeSearchBar`
- `homeSearchSubmitButton`
- `arcBottomBar`
- `mainBrowserBottomBar`
- `arcMenuButton`
- `arcTabsButton`
- `tab grid` and settings accessibility identifiers surfaced by the current iOS views
- `Browse for Me` and `Private` menu items used by the regression suite

## Troubleshooting

- **Simulator not found**: Make sure the simulator name matches an installed simulator. List available simulators with: `xcrun simctl list devices`.
- **App path incorrect**: Update the `APP_PATH` constant in `appium-tests/run-tests.js` to match your local build output.
- **WebDriverAgent timeout**: First run may take a while as Appium builds and installs WebDriverAgent. Subsequent runs are faster.
- **Accessibility identifiers missing**: If a test step fails with "element not found", verify the identifier exists in the iOS view.

## Limitations

- This is a smoke test, not a comprehensive UI test suite.
- `run-tests.js` is the canonical main pass; `edge-case-tests.js` is supplemental regression coverage.
- It assumes the app launches to the main browser view with the bottom bar visible after onboarding is dismissed.
- Sync status and complex settings are still out of scope for this minimal smoke test.
