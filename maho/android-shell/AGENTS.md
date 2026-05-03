# android-shell

## OVERVIEW
Android shell project. Gradle-based native app boundary for the Maho browser on Android.

## STRUCTURE
- `build.gradle.kts` — build logic
- `settings.gradle.kts` — project wiring
- `gradle/` — wrapper support
- app sources and tests live under `app/`

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Build setup | `build.gradle.kts` | Module and dependency wiring |
| Project wiring | `settings.gradle.kts` | Included modules |
| JVM tests | `app/src/test/` | Fast unit tests |
| Instrumentation tests | `app/src/androidTest/` | Device/emulator coverage |

## CONVENTIONS
- Follow the Gradle/Kotlin build already in place.
- Keep Android shell changes local to `android-shell/`.
- Use the test matrix in `docs/test-matrix.md` for canonical command ordering.

## ANTI-PATTERNS
- Do not treat Android shell work as part of the Swift shells.
- Do not change test ordering without updating the docs.
- Do not bury app-specific logic outside the Android shell tree.

## COMMANDS
```bash
cd android-shell && ./gradlew app:testDebugUnitTest
cd android-shell && ./gradlew app:connectedDebugAndroidTest
```
