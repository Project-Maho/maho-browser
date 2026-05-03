# Appium Tests

Repo-local Appium coverage for the Maho iOS app lives here.

See `docs/test-matrix.md` for the CI order, command inventory, and platform prerequisites.

## Scripts

```bash
bun run test         # main regression runner: run-tests.js
bun run test:ios     # alias for the main regression runner
bun run test:android # Android smoke runner: run-android-smoke.js
bun run test:edge    # crash-prone edge-case coverage
```

## Files

- `run-tests.js` — canonical main regression flow, including the 26-test smoke path
- `run-android-smoke.js` — Android Appium smoke runner for the current shell selectors/state
- `edge-case-tests.js` — malformed input, repeated menu/incognito interaction, and other crash-prone paths
- `screenshots/` — artifacts saved by the suites

## Notes

- The test suite is intentionally repo-local and does not touch product code.
- Update the `APP_PATH` constant in `run-tests.js` if your simulator build lands in a different DerivedData path.
