# ios-shell

## OVERVIEW
iOS shell target for Maho. Swift/iOS UI layer, XcodeGen-defined, with views split into feature directories.

## STRUCTURE
- `MahoApp.swift` — app entry point
- `Views/` — primary UI surface and feature screens
- `Bridge/` — Swift ↔ Rust FFI bridge
- `Models/` — local iOS-facing models
- `Support/` — shared helpers and app support code
- `Integration/` — integration surfaces
- `Extensions/` — app extensions, excluding `ShareExtension/**`

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Startup flow | `MahoApp.swift` | Chooses onboarding vs browser path |
| Browser UI | `Views/MainBrowserView.swift` | Main shell surface |
| Onboarding | `Views/Onboarding/` | First-run flow |
| Tab grid | `Views/TabGrid/` | Tab-management UI |
| FFI access | `Bridge/` | Swift side of Rust bridge |
| Build wiring | `project.yml` | Target/source layout |

## CONVENTIONS
- Swift 5.9, iOS 17.
- Bridging header: `Maho-Bridging-Header.h`.
- `ShareExtension/**` stays excluded from the main app target.
- Bundle id prefix is `dev.maho`.
- XCTest and UI tests live under `Tests/` and `UITests/`.

## ANTI-PATTERNS
- Do not move shell logic into Rust unless it is truly shared core behavior.
- Do not add unrelated platform code to `Views/` without a clear feature boundary.
- Do not change target/source grouping without updating `project.yml`.

## COMMANDS
```bash
cd ios-shell && make test
cd ios-shell && make build-app
```
