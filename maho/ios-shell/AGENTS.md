# ios-shell

## OVERVIEW

iOS shell target for Maho. SwiftUI owns shell state and native platform
integration; web-ai owns the converged BYOK, conversations, and chat UI.
Project membership is generated from `project.yml` with XcodeGen.

## STRUCTURE

- `MahoApp.swift` — app entry point and onboarding choice
- `Views/MainBrowserView.swift` — shell state, routing, actions, and bottom bar
- `Views/MainBrowserView+Sheets.swift` — explicit-binding sheet and full-screen
  presentations; keep state ownership in `MainBrowserView`
- `Views/MainBrowserPresentation.swift` — display-only URL, tint, and desktop
  user-agent helpers
- `Views/Settings/BYOKWebView.swift` — shared `#byok` web-ai host
- `Views/ConversationsWebView.swift` — shared `#conversations` web-ai host
- `Views/AgentWebView.swift` — shared `#agent` web-ai host
- `Sources/ArchiveView.swift` — the sole Archive view definition
- `Sources/Models/BrowserState.swift` — archive loading and typed restore event
- `Models/ShellEvent*.swift` — domain-split shell-event Codable contract
- `Bridge/` — Swift-to-Rust FFI bridge extensions
- `Tests/` and `UITests/` — unit and simulator regression tests

## WHERE TO LOOK

| Task | Location |
| --- | --- |
| Browser shell state and bottom-bar actions | `Views/MainBrowserView.swift` |
| Sheet behavior | `Views/MainBrowserView+Sheets.swift` |
| Archive presentation and restore | `Sources/ArchiveView.swift`, `Sources/Models/BrowserState.swift` |
| BYOK / conversations / agent web-ai hosts | `Views/Settings/BYOKWebView.swift`, `Views/ConversationsWebView.swift`, `Views/AgentWebView.swift` |
| Typed shell events | `Models/ShellEvent.swift` plus `ShellEvent+*.swift` |
| FFI access | `Bridge/` |
| Build wiring | `project.yml` |

## CONVENTIONS

- Swift 5.9 and iOS 17.
- Keep private state in `MainBrowserView`; pass explicit bindings and closures
  into presentation modifiers rather than recreating ownership elsewhere.
- Archive restore must use typed `ShellEvent.restoreArchivedTab`, not raw JSON.
- There must be exactly one `struct ArchiveView`; audit with
  `git grep -n 'struct ArchiveView' HEAD -- maho/ios-shell`.
- BYOK, conversations, and chat are web-ai surfaces. Do not reintroduce native
  `BYOKSettingsView`, `ConversationListView`, or `ChatView` implementations;
  audit deleted symbols with
  `git grep -E 'BYOKSettingsView|ConversationListView|ChatView' HEAD -- maho/ios-shell`.
- `isIncognito` remains local shell state.
- `ShareExtension/**` stays excluded from the main app target.

## VERIFICATION

The focused plan evidence used a signed simulator invocation. Signing and a
currently available simulator destination are environment-specific; unsigned
startup has previously failed during keychain-backed initialization.

```bash
cd maho/ios-shell
xcodebuild test -project Maho.xcodeproj -scheme Maho \
  -destination 'platform=iOS Simulator,id=109DA58D-9E08-4DB3-8156-8DB6DAA950B5' \
  -allowProvisioningUpdates CODE_SIGN_STYLE=Automatic DEVELOPMENT_TEAM=5DUM8WPB4C
```

Focused Archive regression coverage is in `UITests/ConversationE2ETests.swift`
and `Tests/CodableRoundTripTests.swift`.
