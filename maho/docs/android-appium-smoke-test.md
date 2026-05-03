# Android Appium Smoke Test

A lightweight repo-local smoke runner for the Maho Android shell lives in `appium-tests/`.

For the CI-ready command matrix, see `docs/test-matrix.md`.

## Entry point

- `appium-tests/run-android-smoke.js`
- `bun run test:android`

## What it covers

- App launch
- Home surface visibility
- Bottom chrome visibility
- Search submit to browsing state
- Menu open and settings access
- Tab grid access
- Browse for Me / summary surface when available

## Selector contract used

The runner follows the Android shell tags and state descriptions already present in the app:

- `mainBrowserHomeSurface`
- `homeSearchView`
- `homeSearchBar`
- `homeSearchField`
- `mainBrowserBottomBar`
- `arcBottomBarContainer`
- `arcBottomBarTabsButton`
- `arcBottomBarMoreButton`
- `mainBrowserBrowsingSurface`
- `tabSwitcherShell`
- `tabGrid`
- `summaryShellScrim`
- `summaryShellOverlay`

It also falls back to visible Android labels such as `Skip`, `Settings`, `Browse for Me`, `Tabs`, `Back`, and `General` so the smoke runner can stay useful even when a selector is not directly surfaced as a tag.

## Notes

- No product code changes are required for the smoke runner.
- Start Appium 2 with UiAutomator2 before running the script.
