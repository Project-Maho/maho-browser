# android-shell

## OVERVIEW

Android shell project. Compose owns native browser chrome and Android system
integration; web-ai owns the converged BYOK, conversations, and chat UI.

## STRUCTURE

- `app/src/main/java/dev/maho/browser/MahoBridge.kt` — JNI declarations and
  public facade
- `app/src/main/java/dev/maho/browser/bridge/` — domain delegates; AI, agent,
  chat, and conversations live in `BridgeAi.kt`, `BridgeAgent.kt`,
  `BridgeChat.kt`, and `BridgeConversations.kt`
- `app/src/main/java/dev/maho/browser/ui/MainBrowserScreen.kt` — native shell
  state and composition; keep it below 1,000 pure LOC
- `app/src/main/java/dev/maho/browser/ui/MainBrowserShellRoute.kt` — shell route
  state type
- `app/src/main/java/dev/maho/browser/ui/MainBrowserAccessibility.kt` — shell
  accessibility descriptions
- `app/src/main/java/dev/maho/browser/ui/theme/BrowserShellTheme.kt` — shell
  color and metric decisions
- `app/src/main/java/dev/maho/browser/ui/MainBrowserReader.kt` — Reader host
- `app/src/main/java/dev/maho/browser/support/ReaderRenderer.kt` — pure Reader
  HTML renderer
- `app/src/main/java/dev/maho/browser/ui/webview/` — shared web-ai hosts and
  RPC bridge, including `BYOKWebOverlay.kt` and `ConversationsWebOverlay.kt`
- `app/src/main/java/dev/maho/browser/ui/ArcBottomBar*.kt` — entry composable
  plus browser, page, and spaces sections

## CONVENTIONS

- Keep `external fun native*` declarations in `MahoBridge.kt`; domain wrappers
  delegate to `bridge/` modules.
- BYOK and conversations use `AiWebViewFactory.createHost`; web-ai source wires
  conversation actions to the chat hash route. Do not recreate the deleted
  native AI screen types; run the plan F1 retired-screen audit against this
  directory.
- Reader remains native. Keep HTML generation in `ReaderRenderer.kt` and cover
  it with `ReaderRendererTest.kt`; do not move it into web-ai.
- `isIncognito` remains local Compose state in `MainBrowserScreen`.
- The deleted tab-clustering flag and suggestion UI must not return; run the
  plan F1 cluster-removal audit against this directory.
- Keep `MainBrowserScreen.kt`, `MahoBridge.kt`, and `ArcBottomBar.kt` below
  1,000 pure LOC.

## WHERE TO LOOK

| Task | Location |
| --- | --- |
| Native shell and overlays | `ui/MainBrowserScreen.kt`, `ui/MainBrowserOverlay.kt` |
| Web-ai host factory and RPC | `ui/webview/AiWebViewFactory.kt`, `ui/webview/WebViewBridge.kt` |
| Reader rendering | `support/ReaderRenderer.kt`, `ui/MainBrowserReader.kt` |
| Bridge domain wrapper | `bridge/Bridge*.kt` |
| Bottom-bar sections | `ui/ArcBottomBar*.kt` |
| JVM tests | `app/src/test/` |
| Instrumentation tests | `app/src/androidTest/` |

## COMMANDS

```bash
cd maho/android-shell
./gradlew app:testDebugUnitTest
./gradlew app:assembleDebug
./gradlew app:connectedDebugAndroidTest
```
