# PROJECT KNOWLEDGE BASE — maho/web-ai

## OVERVIEW

The shared Preact AI bundle is mounted by iOS and Android hosts and contains a
desktop bridge path. Its hash router uses `switch (route.name)` in `src/app.tsx`
to select the supported AI screens:

- `#byok` — API-key settings
- `#conversations` — saved conversation list
- `#chat?sessionId=<id>` — conversation chat, reached from the list
- `#agent` — browser agent
- `#onboarding` — onboarding

The iOS and Android source trees mount `#byok` and `#conversations`; chat route
selection is wired by `src/app.tsx` and the conversation-list callbacks. The
former native BYOK, conversations, and chat symbols are absent at HEAD, as
checked with:

```bash
git grep -E 'BYOKSettingsView|BYOKSettingsScreen|ConversationListView|ConversationListScreen|ChatView|ChatScreen' HEAD -- maho/ios-shell maho/android-shell
```

Do not recreate those native screens or add a rollback flag.

## STRUCTURE

```
web-ai/
├── src/
│   ├── app.tsx                    # switch-based hash router; supplies chat navigation callback
│   ├── bridge/                    # Typed JSON-RPC contract and platform detection
│   ├── hooks/                     # Bridge, route, and streaming hooks
│   ├── screens/
│   │   ├── byok/                  # Shared API-key settings
│   │   ├── conversations/         # Shared saved-conversation list
│   │   ├── chat/                  # Shared chat screen
│   │   ├── agent/                 # Agent screen
│   │   └── onboarding/            # Onboarding screen
│   └── ui/                        # Lightweight shared primitives and icons
├── tests/                         # Bridge and screen integration tests
└── scripts/copy-to-platforms.ts   # Copies built assets into mobile hosts
```

## HOSTS AND ROUTING

| Surface | BYOK host | Conversations host | Chat path |
| --- | --- | --- | --- |
| iOS | `../ios-shell/Views/Settings/BYOKWebView.swift` | `../ios-shell/Views/ConversationsWebView.swift` | `src/app.tsx` supplies a callback that sets `#chat?sessionId=...`; list actions invoke it in `src/screens/conversations/conversation-list-screen.tsx` |
| Android | `../android-shell/.../ui/webview/BYOKWebOverlay.kt` | `../android-shell/.../ui/webview/ConversationsWebOverlay.kt` | Same bundle callback and list-action path as iOS |

## CONVENTIONS

- Preact only: use `preact` and `preact/hooks`, never React or `react-dom`.
- Keep the bundle dependency-light. Use `src/ui/icon.tsx`; do not add
  `lucide-react`.
- Add a router case in `src/app.tsx` whenever adding a new real screen.
- The typed `MahoBridge` methods return promises. Wire both native RPC
  dispatchers when adding a cross-platform capability.
- Tests use `@testing-library/preact` and semantic queries.
- Reader stays natively rendered on iOS and Android. Do not add Reader
  rendering code to this bundle.

## NATIVE BRIDGE AND HOST FILES

- iOS RPC dispatch: `../ios-shell/Support/WebViewBridgeController.swift`
- iOS web-ai hosts: `../ios-shell/Views/Settings/BYOKWebView.swift`,
  `../ios-shell/Views/ConversationsWebView.swift`, and `../ios-shell/Views/AgentWebView.swift`
- Android RPC dispatch: `../android-shell/app/src/main/java/dev/maho/browser/ui/webview/WebViewBridge.kt`
- Android host factory: `../android-shell/app/src/main/java/dev/maho/browser/ui/webview/AiWebViewFactory.kt`
- Android web-ai overlays: `BYOKWebOverlay.kt`, `ConversationsWebOverlay.kt`,
  and `WebAgentOverlay.kt` in that directory

## COMMANDS

```bash
cd maho/web-ai
bun run test
bun run build                 # typechecks, builds, and copies platform assets
bun run typecheck
```
