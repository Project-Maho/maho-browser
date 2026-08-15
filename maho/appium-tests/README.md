# Appium Tests

Repo-local Appium coverage for the Maho iOS app lives here.

See `docs/test-matrix.md` for the CI order, command inventory, and platform prerequisites.

## Scripts

```bash
bun run test         # main regression runner: run-tests.js
bun run test:ios     # alias for the main regression runner
bun run test:android # Android smoke runner: run-android-smoke.js
bun run test:edge         # crash-prone edge-case coverage
bun run test:desktop-mail # deterministic host Mail product-contract checks
```

## Files

- `run-tests.js` — canonical main regression flow, including the 26-test smoke path
- `run-android-smoke.js` — Android Appium smoke runner for the current shell selectors/state
- `edge-case-tests.js` — malformed input, repeated menu/incognito interaction, and other crash-prone paths
- `desktop-mail/` — isolated loopback IMAP/SMTP transport fixture plus host-runnable checks that import Mail/Settings product modules and validate native/Rust machine contracts; uses exact reply barriers, no sleeps, and no external network
- `screenshots/` — artifacts saved by the suites

## Notes

- The test suite is intentionally repo-local and does not touch product code.
- Update the `APP_PATH` constant in `run-tests.js` if your simulator build lands in a different DerivedData path.
