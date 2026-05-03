# Test matrix and command inventory

This is the CI-friendly order of the repo's real test layers after Wave 1 and Wave 2.

## Recommended run order

1. Rust targeted tests
2. Relay server tests
3. iOS XCTest
4. Android JVM unit tests
5. Android instrumentation tests
6. Chromium desktop macOS E2E
7. Appium iOS regression
8. Appium edge cases
9. Android Appium smoke

## Command matrix

| Layer | Kind | Command | When to use | Prerequisites |
| --- | --- | --- | --- | --- |
| Rust targeted tests | Unit and integration | `cargo test -p maho-types`<br>`cargo test -p maho-core`<br>`cargo test -p maho-storage`<br>`cargo test -p maho-extension`<br>`cargo test -p maho-ffi`<br>`cargo test -p maho-bench` | Fast crate-level checks. Run the crate that matches the code you changed, or fan out across the touched crates. | Rust toolchain and repo dependencies. No mobile simulator or emulator needed. |
| Relay server tests | Unit, integration, E2E | `cd relay && cargo test`<br>`cd relay && cargo clippy -- -D warnings` | Sync relay server validation. Run when relay server code changes. Covers storage, room management, auth flow, message exchange, catchup, and E2E sync simulation. | Rust toolchain. No mobile simulator needed. Separate workspace from main Cargo. |
| iOS XCTest | Unit and integration | `cd ios-shell && make test` | Native iOS shell validation. Use for shell logic, view wiring, and UIKit or SwiftUI test coverage. | macOS, Xcode, iOS Simulator runtime. The Makefile runs `xcodebuild test` with the Maho scheme. |
| Android JVM tests | Unit | `cd android-shell && ./gradlew app:testDebugUnitTest` | JVM test layer for Android shell code. Use for fast logic checks in `src/test`. | JDK 17, Android Gradle Plugin, Android SDK. |
| Android instrumentation tests | Integration | `cd android-shell && ./gradlew app:connectedDebugAndroidTest` | Device or emulator coverage for `src/androidTest`. Use after JVM tests pass. | Android emulator or device, Android SDK, UiAutomator2-capable test device. |
| Chromium desktop macOS E2E | Desktop E2E, accessibility-driven | `python3 maho/build/scripts/e2e_chromium_sidebar.py`<br>`python3 maho/build/scripts/e2e_chromium_sidebar.py --list`<br>`python3 maho/build/scripts/e2e_chromium_sidebar.py --scenario launch_clean_profile`<br>`python3 maho/build/scripts/e2e_chromium_sidebar.py --scenario phase2_seeded_favorites_snapshot_assert --scenario phase2_folder_sibling_reorder_blocked` | Chromium desktop overlay smoke and seeded support-contract coverage on macOS. Use when you need to validate the sidebar, the spaces surface, seeded snapshot assertions, or the current blocked folder sibling reorder contract. See `docs/chromium-fork-build.md` for the setup and build flow. | macOS, Accessibility permission for the Terminal or Python process, built `Maho.app` bundle at `/Users/indo/code/project/maho-workspace/chromium/src/out/Release/Maho.app` by default, and the maho-core prebuilt static library produced by the maho-chromium build flow. |
| Appium iOS regression | Smoke and integration | `cd appium-tests && bun run test`<br>`cd appium-tests && bun run test:ios` | Main repo-local iOS Appium pass. `test:ios` is the same runner as `bun run test`. | macOS, Xcode, iOS Simulator, Appium 2.x, XCUITest driver, built simulator app at the path in `appium-tests/run-tests.js`. |
| Appium edge cases | Edge | `cd appium-tests && bun run test:edge` | Crash-prone and malformed-input paths. Run after the main iOS regression pass. | Same as the iOS Appium regression runner. |
| Android Appium smoke | Smoke | `cd appium-tests && bun run test:android` | Short Android Appium smoke for launch, home surface, menu, tab grid, and Browse for Me handling. | Android emulator or device, Appium 2.x, UiAutomator2, installed Android build. |

## Notes

- Appium commands live in `appium-tests/package.json`, and contributors should invoke them with `bun run`.
- The iOS Appium runner is the canonical regression pass. The edge script is separate on purpose.
- Android JVM tests are the fast guardrail. Use instrumentation and Appium after that when UI coverage matters.
- Chromium desktop macOS E2E is a local accessibility harness for the Chromium overlay. It is separate from Appium and should be run against the built desktop bundle, not a simulator app. See `docs/chromium-fork-build.md` for the build flow and bundle path.
