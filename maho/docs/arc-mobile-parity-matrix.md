# Arc mobile parity evidence matrix

**Snapshot:** 2026-08-09

This is a source-and-verification inventory for
`.omo/plans/mobile-parity-parallel-2026-08-08.md`. It documents the work
present in the tree and the evidence retained for it; it is not a blanket claim
that every mobile workflow has parity.

## Status vocabulary

- **source-verified**: current HEAD source contains the stated structure. This
  does not establish device or simulator behavior.
- **test-verified**: focused automated test or build evidence exists in
  addition to the cited source.
- **runtime-verified**: focused simulator interaction evidence exists for the
  stated path.
- **unverified**: this work did not establish the stated runtime behavior.
- **source audit**: an absence checked with a repository search. An absence has
  no positive source line, so the command is recorded instead of inventing one.

## Delivered convergence and cleanup

| Area | Status | Evidence | Scope and limits |
| --- | --- | --- | --- |
| Shared web-ai router | source-verified | `web-ai/src/app.tsx:38-48`; `web-ai/src/screens/conversations/conversation-list-screen.tsx:220-225,286-294` | A `switch (route.name)` has concrete cases for `byok`, `agent`, `chat`, and `conversations`. Conversation creation or row selection invokes the supplied chat-navigation callback; the router callback sets `#chat?sessionId=...`. This is source structure, not a mobile runtime claim. |
| iOS BYOK host | source-verified | `ios-shell/Views/Settings/SettingsRootView.swift:54-68`; `ios-shell/Views/Settings/BYOKWebView.swift:16-22` | Settings links to a WKWebView host that requests the shared `byok` route. Runtime navigation was not captured by this task. |
| Android BYOK host | source-verified | `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserOverlay.kt:56-96`; `android-shell/app/src/main/java/dev/maho/browser/ui/webview/BYOKWebOverlay.kt:21-28` | The API-keys overlay creates a web-ai host for `byok`. Runtime navigation was not captured by this task. |
| iOS conversations host | source-verified | `ios-shell/Views/MainBrowserView.swift:179-184,321-338`; `ios-shell/Views/MainBrowserView+Sheets.swift:116-121`; `ios-shell/Views/ConversationsWebView.swift:16-22` | The shell can present a WKWebView host that requests the shared `conversations` route. |
| Android conversations host | source-verified | `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserOverlay.kt:92-96`; `android-shell/app/src/main/java/dev/maho/browser/ui/webview/ConversationsWebOverlay.kt:21-28` | The conversations overlay creates a web-ai host for `conversations`. |
| Chat route from conversations | test-verified | `web-ai/src/app.tsx:45-48`; `web-ai/src/screens/conversations/conversation-list-screen.tsx:220-225,286-294`; `.omo/evidence/task-3-mobile-parity-parallel-2026-08-08.web-ai.txt` | Source wires conversation actions to the `chat` hash route; retained web-ai test/build evidence passed. End-to-end streaming chat on both mobile hosts remains unverified. |
| Removed native BYOK, conversations, and chat screens | source audit | `git grep -E 'BYOKSettingsView\|BYOKSettingsScreen\|ConversationListView\|ConversationListScreen\|ChatView\|ChatScreen' HEAD -- maho/ios-shell maho/android-shell` returned no matches | Removed symbols have no valid `file:line` anchor. The audit establishes only source absence at HEAD. |
| iOS Archive presentation and restore | runtime-verified | `ios-shell/Views/MainBrowserView.swift:185-191,321-338`; `ios-shell/Views/MainBrowserView+Sheets.swift:88-90`; `ios-shell/Sources/ArchiveView.swift:21-25,58-64`; `ios-shell/Sources/Models/BrowserState.swift:114-124` | The shell presents the sole `ArchiveView`; it loads the active space's archive and restores through typed `ShellEvent`. The focused simulator test and screenshots are listed below. |
| iOS Archive regression coverage | test-verified | `ios-shell/UITests/ConversationE2ETests.swift:14-78`; `ios-shell/Tests/CodableRoundTripTests.swift:58-110` | The UI test seeds a bridge-backed archived-tab fixture, restores it, and asserts the active tab. Codable tests cover archive/restore wire fields and archived-tab decoding. |
| Android Reader renderer extraction | test-verified | `android-shell/app/src/main/java/dev/maho/browser/support/ReaderRenderer.kt:7-74`; `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserReader.kt:85-97`; `android-shell/app/src/test/java/dev/maho/browser/support/ReaderRendererTest.kt:11-77` | Reader HTML generation is a pure Kotlin function used by the native Reader path and covered by JVM tests. This does not prove on-device rendering. |
| Android bridge domain split | source-verified | `android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt:875-885,905-978`; `android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeAgent.kt:10-39`; `android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeAi.kt:6-15`; `android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeChat.kt:5-45`; `android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeConversations.kt:8-39` | `MahoBridge` delegates AI, agent, chat, and conversation facade methods to domain objects while retaining native declarations. No runtime behavior is inferred from the split. |
| iOS ShellEvent split | test-verified | `ios-shell/Models/ShellEvent.swift:1-180`; `ios-shell/Models/ShellEvent+AccountLibrary.swift:3-70`; `ios-shell/Models/ShellEvent+Browser.swift:3-149`; `ios-shell/Models/ShellEvent+CommandBar.swift:3-161`; `ios-shell/Models/ShellEvent+TabsSpaces.swift:3-134`; `ios-shell/Tests/CodableRoundTripTests.swift:58-110` | Codable implementation is organized across domain extensions; focused archive wire-contract tests passed. |
| Android bottom-bar split | source-verified | `android-shell/app/src/main/java/dev/maho/browser/ui/ArcBottomBar.kt:334-347,491-527`; `android-shell/app/src/main/java/dev/maho/browser/ui/ArcBottomBarBrowserSection.kt:11-29`; `android-shell/app/src/main/java/dev/maho/browser/ui/ArcBottomBarPageSection.kt:11-18`; `android-shell/app/src/main/java/dev/maho/browser/ui/ArcBottomBarSpacesSection.kt:9-14` | The entry composable delegates spaces, page, and browser menu content to focused sibling composables. |
| iOS MainBrowserView split | test-verified | `ios-shell/Views/MainBrowserView.swift:8-22,321-339`; `ios-shell/Views/MainBrowserView+Sheets.swift:3-44,47-123`; `ios-shell/Views/MainBrowserPresentation.swift:3-44`; `ios-shell/Tests/BridgeLifecycleTests.swift:86-100` | `ShellRoute` and presentation state remain in the host; the sheet modifier receives explicit bindings and callbacks. Focused shell-route and Archive regressions passed. |
| Android MainBrowserScreen split | test-verified | `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserScreen.kt:65-1066`; `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserShellRoute.kt:3-6`; `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserAccessibility.kt:5-24`; `android-shell/app/src/main/java/dev/maho/browser/ui/theme/BrowserShellTheme.kt:17-110`; `.omo/evidence/task-10-mobile-parity-parallel-2026-08-08.loc-repair.txt` | Route type and accessibility mapping are extracted, theme primitives live in the theme file, and `isIncognito` remains local Compose state. The retained evidence records 998 pure LOC, passing unit tests, and a passing debug assembly. |
| Removed cluster suggestion UI and flag | source audit | `git grep -E 'clusterTabsEnabled\|TabGroupSuggestion' HEAD -- maho/ios-shell maho/android-shell` returned no matches | Removed symbols have no valid `file:line` anchor. The audit establishes only source absence at HEAD. |

## Focused verification evidence

| Check | Result | Evidence and limit |
| --- | --- | --- |
| iOS Archive wire contracts | test-verified | `ios-shell/Tests/CodableRoundTripTests.swift:58-110`; `.omo/evidence/task-6-mobile-parity-parallel-2026-08-08.fixture-qa.txt` records 25 passing focused tests. |
| iOS Archive menu, populated list, and restore | runtime-verified | `ios-shell/UITests/ConversationE2ETests.swift:21-78`; the retained signed simulator run passed and produced `.omo/evidence/task-6-archive-menu-verified.png`, `.omo/evidence/task-6-archive-populated-verified.png`, and `.omo/evidence/task-6-archive-restored-verified.png`. |
| iOS sheet decomposition regression | test-verified | `ios-shell/Tests/BridgeLifecycleTests.swift:86-100`; `ios-shell/UITests/ConversationE2ETests.swift:21-78`; `.omo/evidence/task-11-mobile-parity-parallel-2026-08-08.regression.txt` records 9 passing unit tests and 1 passing Archive UI test. |
| Android Reader renderer | test-verified | `android-shell/app/src/test/java/dev/maho/browser/support/ReaderRendererTest.kt:11-77`; `.omo/evidence/task-5-mobile-parity-parallel-2026-08-08.txt` records `app:testDebugUnitTest` as successful. |
| Android MainBrowserScreen LOC repair | test-verified | `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserScreen.kt:1-1066`; `.omo/evidence/task-10-mobile-parity-parallel-2026-08-08.loc-repair.txt` records 998 pure LOC plus successful unit-test and debug-assembly commands. |
| web-ai routes and screens | test-verified | `web-ai/src/__tests__/app-routing.test.tsx`; `web-ai/tests/byok-screen.test.tsx:19-150`; `web-ai/src/screens/conversations/__tests__/conversation-list.test.tsx`; `.omo/evidence/task-3-mobile-parity-parallel-2026-08-08.web-ai.txt` records 137 passing tests, 4 skipped tests, and a successful build. Skipped tests are not treated as evidence. |

## Runtime gaps retained for final QA

| Scenario | Status | Evidence limit |
| --- | --- | --- |
| BYOK UI navigation on both mobile runtime surfaces | unverified | Host and route source plus web-ai tests/build are available; no successful end-to-end capture on both native hosts is cited. |
| Conversations list and in-host streaming chat on both runtime surfaces | unverified | Router, hosts, and web-ai tests are source/test-verified; an end-to-end streaming conversation was not captured on both platforms. |
| Android Archive, Reader, normal, and incognito runtime screenshots | unverified | JVM and build evidence exists, but no successful Android runtime capture is cited. |
| Full mobile product parity outside this plan's convergence scope | unverified | This plan did not validate every launch, sync, spaces, history, deep-link, share, widget, or accessibility workflow. |

## Guardrails

- No rollback feature flag for the deleted native AI screens was found in
  source files by
  `git grep -i rollback HEAD -- 'maho/web-ai/src/**' 'maho/ios-shell/**/*.swift' 'maho/android-shell/**/*.kt'`.
- BYOK, conversation, chat, and agent RPC dispatch code remains in
  `ios-shell/Support/WebViewBridgeController.swift:160-181` and
  `android-shell/app/src/main/java/dev/maho/browser/ui/webview/WebViewBridge.kt:52-68`;
  this is source evidence, not proof that every method succeeds at runtime.
- Reader rendering remains native per platform. The web-ai router placeholder
  cases at `web-ai/src/app.tsx:50-58` contain no Reader route or renderer.
- `isIncognito` stays local shell state at
  `ios-shell/Views/MainBrowserView.swift:35` and
  `android-shell/app/src/main/java/dev/maho/browser/ui/MainBrowserScreen.kt:84`.

Final F1-F4 verification remains the authority for declaring the plan complete.
