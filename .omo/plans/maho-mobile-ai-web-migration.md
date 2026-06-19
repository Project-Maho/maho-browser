# Maho Mobile AI Screens — Web-Based Migration Plan

**Created:** 2026-06-19
**Status:** Phase A Complete — 2026-06-19
**Scope:** Migrate 9 AI-related mobile screens from native (Compose/SwiftUI) to unified web bundle

---

## 1. Goal

Write each AI screen ONCE in web technology (TypeScript + React), embed in native shells via WebView on iOS/Android, and reuse the same bundle on Desktop Chromium fork via existing `chrome://maho-ai` WebUI. Achieve UI/UX parity across iOS, Android, and Desktop from a single source.

### Success Criteria
- 7 of 9 AI screens rendered from a single web bundle on all 3 platforms
- Cold start to interactive: <800ms on mid-range Android (Pixel 6a), <500ms on iPhone 13
- Zero native bridge calls that bypass the typed RPC contract
- Bundle size: <80KB gzipped (excluding shared framework runtime)
- All migrated screens pass Appium regression suite on both platforms

---

## 2. Decision Context

### Current State
| Metric | Value |
|--------|-------|
| AI screens | 9 |
| Platforms | 3 (iOS, Android, Desktop) |
| Native implementations | Android: 4,266 LOC across 9 files / iOS: 3,433 LOC across 9 files |
| Desktop WebUI AI surface | Already React-based (`maho-chromium/browser/resources/maho_ai/react/`) |
| Total native LOC for AI | ~7,700 across 18 files (duplicated logic) |

### Cost Projection
- Current cost per new AI feature: ~800 LOC × 2 platforms = ~1,600 LOC native
- At planned W6+ roadmap (22 AI features): 22 × 2 = 44 native implementations
- Web-based cost per feature: ~400 LOC × 1 implementation + ~50 LOC bridge glue = ~450 LOC
- **Break-even point: 4 screens** (we already have 9 — this is already past ROI threshold)

### Already in Repo
| Stack | Location | Notes |
|-------|----------|-------|
| React 19 + Vite | `mail/` | Tauri v2 desktop email client |
| React + Mojo WebUI | `maho-chromium/browser/resources/maho_ai/react/` | Desktop AI panel — **already React** |
| Vanilla TS WebUI | `maho-chromium/browser/resources/maho_settings/` | Settings panel (manual DOM) |
| JNI bridge (167 exports) | `maho/crates/maho-jni/src/lib.rs` | Android ↔ Rust |
| Swift FFI bridge | `maho/ios-shell/Sources/MahoBridge.swift` | iOS ↔ Rust |

---

## 3. Technology Choices

### 3.1 Framework: React 18 (Preact compat layer for mobile bundle)

**Decision:** Use React API surface with Preact as the runtime for mobile bundles.

**Rationale:**
1. Desktop AI surface (`maho-chromium/browser/resources/maho_ai/react/`) is already React — sharing components requires API compatibility
2. Mail app (`mail/src/`) is React 19 — team has React fluency
3. Preact provides React API compatibility at 3KB gzipped vs React's 44KB — critical for WebView cold start on mobile
4. Same `.tsx` source files compile to both full React (desktop, where Chromium bundles React) and Preact (mobile, size-optimized)

**Bundle size budget:**
- Preact runtime: ~3KB gzipped
- Application code (9 screens): ~40KB gzipped estimated
- Shared UI primitives: ~10KB gzipped
- **Total: ~53KB gzipped** (well under 80KB budget)

**Alternatives considered:**
| Framework | Size (gzip) | React compat | Why not |
|-----------|-------------|--------------|---------|
| Lit | 7KB | ❌ No JSX | Would require rewriting existing desktop AI components |
| Solid | 7KB | ❌ Different reactivity | Same rewrite problem; signals != hooks |
| Svelte | 2KB runtime | ❌ No compat | Compiler approach incompatible with existing React codebase |
| Full React 19 | 44KB | ✅ Native | Too heavy for mobile WebView cold start |
| **Preact + compat** | **3KB** | **✅ aliased** | **Winner — size of Lit, compat of React** |

### 3.2 Bundler: Vite

**Decision:** Vite with Preact plugin for development, Rollup (via Vite build) for production.

**Rationale:**
1. Mail app already uses Vite — team knows it
2. Desktop AI WebUI already has `vitest.config.ts` — test infrastructure reuse
3. Output: single ES module bundle + CSS, suitable for embedding
4. Dev server with HMR for WebView development via local network

**Build output:**
```
maho/web-ai/dist/
├── ai-bundle.js       # Single ES module (all screens)
├── ai-bundle.css      # Extracted CSS
└── ai-bundle.js.map   # Source maps (dev only)
```

### 3.3 Communication: Typed RPC over postMessage

**Architecture:** Promise-based typed bridge with JSON-RPC 2.0 semantics.

```
┌──────────────────────────────────────────────────┐
│              Web Bundle (ai-bundle.js)            │
│  React components → useBridge() hook → RPC call  │
└──────────────┬───────────────────────────────────┘
               │ window.webkit.messageHandlers (iOS)
               │ MahoBridgeAndroid.rpc(json) (Android)
               │ chrome.send / Mojo (Desktop)
               ▼
┌──────────────────────────────────────────────────┐
│           Platform Bridge Dispatcher             │
│  iOS: WKScriptMessageHandler → MahoBridge.swift  │
│  Android: @JavascriptInterface → MahoBridge.kt   │
│  Desktop: Mojo PageHandler (existing)            │
└──────────────┬───────────────────────────────────┘
               │ FFI / JNI
               ▼
┌──────────────────────────────────────────────────┐
│              maho-core (Rust)                     │
│  byok_*, chat_*, conversation_*, offline_model_* │
└──────────────────────────────────────────────────┘
```

**Bridge contract (TypeScript):**

```typescript
// web-ai/src/bridge/types.ts

export interface MahoBridge {
  // BYOK
  byokGetProviders(): Promise<string[]>;
  byokGetKey(provider: string): Promise<string | null>;
  byokSetKey(provider: string, key: string): Promise<boolean>;
  byokDeleteKey(provider: string): Promise<boolean>;
  byokValidateKey(provider: string, key: string): Promise<boolean>;

  // Chat Session
  chatSessionNew(apiKey: string, endpoint: string, model: string, systemInstruction: string): Promise<number>; // returns session ptr
  chatSendUserTurn(sessionPtr: number, message: string): Promise<boolean>;
  chatSendImage(sessionPtr: number, mime: string, data: Uint8Array): Promise<boolean>;
  chatSendTextWithImage(sessionPtr: number, text: string, mime: string, data: Uint8Array): Promise<boolean>;
  chatCancel(sessionPtr: number): Promise<void>;
  chatSessionFree(sessionPtr: number): Promise<void>;
  chatSessionPollEvent(sessionPtr: number): Promise<string | null>;

  // Conversations
  createConversation(id: string, title: string | null, spaceId: string | null, model: string | null): Promise<boolean>;
  listConversations(limit?: number): Promise<ConversationSession[]>;
  getConversationMessages(sessionId: string): Promise<ConversationTurn[]>;
  saveConversationMessage(sessionId: string, role: string, content: string, urlContext: string | null): Promise<boolean>;
  deleteConversation(sessionId: string): Promise<boolean>;
  renameConversation(sessionId: string, title: string): Promise<boolean>;

  // Space AI Config
  getSpaceAIConfig(spaceId: string): Promise<SpaceAIConfig | null>;
  setSpaceAIConfig(spaceId: string, config: SpaceAIConfig): Promise<boolean>;

  // Offline Model
  offlineModelInfo(): Promise<OfflineModelInfo | null>;
  offlineModelIsDownloaded(): Promise<boolean>;
  offlineModelDelete(): Promise<boolean>;

  // Native-only actions (delegated to native, not Rust)
  capturePhoto(): Promise<{mime: string; data: Uint8Array} | null>;
  captureScreenshot(): Promise<Uint8Array | null>;
  openSettings(): Promise<void>;
  hapticFeedback(style: 'light' | 'medium' | 'heavy'): Promise<void>;
}
```

**Bridge dispatcher implementation:**

```typescript
// web-ai/src/bridge/rpc.ts
let callId = 0;
const pending = new Map<number, {resolve: Function; reject: Function}>();

export function callBridge<T>(method: string, params: unknown[] = []): Promise<T> {
  return new Promise((resolve, reject) => {
    const id = ++callId;
    pending.set(id, {resolve, reject});

    const message = JSON.stringify({jsonrpc: '2.0', id, method, params});

    if (window.webkit?.messageHandlers?.mahoBridge) {
      // iOS
      window.webkit.messageHandlers.mahoBridge.postMessage(message);
    } else if ((window as any).MahoBridgeAndroid) {
      // Android
      (window as any).MahoBridgeAndroid.rpc(message);
    } else if ((window as any).__mahoChromePageHandler) {
      // Desktop (Mojo) — handled by existing page handler
      (window as any).__mahoChromePageHandler(message);
    }
  });
}

// Called by native to resolve pending promises
(window as any).__mahoBridgeResponse = (responseJson: string) => {
  const {id, result, error} = JSON.parse(responseJson);
  const call = pending.get(id);
  if (!call) return;
  pending.delete(id);
  if (error) call.reject(new Error(error.message));
  else call.resolve(result);
};
```

### 3.4 Asset Embedding

**Strategy:** Ship web bundle with the app binary — no remote loading.

| Platform | Asset Location | Load Method |
|----------|---------------|-------------|
| Android | `app/src/main/assets/web-ai/` | `file:///android_asset/web-ai/index.html` |
| iOS | `Maho.app/web-ai/` | Bundle resource, loaded via `WKWebView.loadFileURL()` |
| Desktop | `chrome://maho-ai/` (existing) | Chromium WebUI resource system (no change) |

**Dev mode:** Local Vite dev server at `http://localhost:5173` — WebView loads from network during development. Controlled by build flag `MAHO_WEB_AI_DEV=1`.

---

## 4. Architecture

```
                    ┌─────────────────────────────────┐
                    │    web-ai/ (new directory)       │
                    │    Single TypeScript source      │
                    ├─────────────────────────────────┤
                    │  src/                            │
                    │  ├── screens/                    │
                    │  │   ├── byok/                   │
                    │  │   ├── chat/                   │
                    │  │   ├── conversation-list/      │
                    │  │   ├── agent/                  │
                    │  │   ├── space-ai-config/        │
                    │  │   ├── offline-model/          │
                    │  │   ├── privacy-dashboard/      │
                    │  │   └── visual-search-results/  │
                    │  ├── bridge/                     │
                    │  │   ├── types.ts                │
                    │  │   ├── rpc.ts                  │
                    │  │   └── platform-detect.ts      │
                    │  ├── ui/                         │
                    │  │   ├── button.tsx              │
                    │  │   ├── input.tsx               │
                    │  │   ├── list-item.tsx           │
                    │  │   ├── markdown.tsx            │
                    │  │   └── ...                     │
                    │  ├── hooks/                      │
                    │  │   ├── use-bridge.ts           │
                    │  │   └── use-streaming.ts        │
                    │  └── app.tsx                     │
                    │  vite.config.ts                  │
                    │  package.json                    │
                    │  tsconfig.json                   │
                    └────────────┬────────────────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              │                  │                   │
              ▼                  ▼                   ▼
    ┌─────────────────┐ ┌──────────────┐  ┌─────────────────────┐
    │  Android Shell   │ │  iOS Shell   │  │  Chromium Desktop   │
    │  WebView +       │ │  WKWebView + │  │  (existing WebUI)   │
    │  @Interface      │ │  MessageHndlr│  │  Mojo PageHandler   │
    └────────┬────────┘ └──────┬───────┘  └──────────┬──────────┘
             │                  │                      │
             ▼                  ▼                      ▼
    ┌─────────────────────────────────────────────────────────────┐
    │                      maho-core (Rust)                        │
    │  byok_manager / chat_session / conversation_store /          │
    │  offline_model / space_ai_config / privacy_dashboard         │
    └─────────────────────────────────────────────────────────────┘
```

### 4.1 Screen Navigation

No SPA router. Each screen is a standalone mount point selected by URL hash:

```
file:///android_asset/web-ai/index.html#byok
file:///android_asset/web-ai/index.html#chat?sessionId=abc&pageUrl=...
file:///android_asset/web-ai/index.html#conversations
file:///android_asset/web-ai/index.html#agent
file:///android_asset/web-ai/index.html#space-ai-config?spaceId=xyz
file:///android_asset/web-ai/index.html#offline-model
file:///android_asset/web-ai/index.html#privacy
file:///android_asset/web-ai/index.html#visual-search-results
```

Native code opens a WebView and sets the hash. No client-side routing library needed — a simple `switch` on `location.hash` in `app.tsx`:

```typescript
// web-ai/src/app.tsx
function App() {
  const screen = useHashRoute();
  switch (screen.name) {
    case 'byok': return <BYOKScreen />;
    case 'chat': return <ChatScreen sessionId={screen.params.sessionId} />;
    case 'conversations': return <ConversationListScreen />;
    case 'agent': return <AgentScreen />;
    case 'space-ai-config': return <SpaceAIConfigScreen spaceId={screen.params.spaceId} />;
    case 'offline-model': return <OfflineModelScreen />;
    case 'privacy': return <PrivacyDashboardScreen />;
    case 'visual-search-results': return <VisualSearchResultsScreen />;
    default: return <ErrorScreen message="Unknown screen" />;
  }
}
```

### 4.2 Per-Screen Migration Assessment

| # | Screen | LOC (Android/iOS) | Migrate? | Rationale | Native APIs Used |
|---|--------|-------------------|----------|-----------|-----------------|
| 1 | BYOK Settings | 589 / 421 | ✅ Web | Pure form UI; keychain access via bridge (`byokSetKey`, `byokGetKey`) | Secure storage (via Rust bridge) |
| 2 | Chat | 1441 / 1341 | ✅ Web | Streaming text + markdown — web's strength; already done on Desktop | Chat session polling, image send |
| 3 | Conversation List | 437 / 461 | ✅ Web | Simple list view + CRUD; no native APIs | `listConversations`, `deleteConversation` |
| 4 | Visual Search | 252 / 202 | ⚠️ Hybrid | Camera capture stays native; result display + image send moves to web | Camera (native) → image bytes (bridge) → chat |
| 5 | Camera Capture | 247 / 232 | ❌ Native | CameraX / AVCaptureSession — requires native hardware access | Camera hardware, permissions |
| 6 | Agent | 318 / 73 | ✅ Web | WebSocket-based UI; web is natural fit | WebSocket (native or web) |
| 7 | Space AI Config | 567 / 434 | ✅ Web | Pure form UI; settings CRUD | `getSpaceAIConfig`, `setSpaceAIConfig` |
| 8 | Offline Model | 143 / 144 | ✅ Web | List + download progress; simple bridge calls | `offlineModelInfo`, `offlineModelDelete` |
| 9 | Privacy Dashboard | 272 / 125 | ✅ Web | Settings display UI | Settings read-only |

**Summary:** 7 screens → full web, 1 → hybrid (native camera + web results), 1 → stays native.

**LOC impact:**
- Removed native code: ~6,700 LOC (7 full screens × 2 platforms + partial Visual Search)
- New web code: ~2,500 LOC (estimated, one implementation)
- Bridge glue (both platforms): ~400 LOC
- **Net reduction: ~3,800 LOC**, plus elimination of future duplication

---

## 5. Migration Sequence

### Phase A — Foundation (Week 1-2)

| Step | Task | Deliverable | Success Criteria |
|------|------|-------------|-----------------|
| A1 | Create `maho/web-ai/` directory with Vite + Preact + TypeScript config | `package.json`, `vite.config.ts`, `tsconfig.json` | `bun run build` produces `dist/ai-bundle.js` |
| A2 | Implement typed bridge contract (`src/bridge/types.ts`, `rpc.ts`) | Bridge interface + RPC dispatcher | TypeScript compiles; mock tests pass |
| A3 | Implement BYOK Settings screen as reference (`src/screens/byok/`) | Fully functional BYOK screen in web | Visual parity with native screenshots |
| A4 | Wire Android bridge: `WebViewBridge.kt` with `@JavascriptInterface` dispatching to existing `MahoBridge` | `app/src/main/.../ui/webview/WebViewBridge.kt` | BYOK screen functional on Android emulator |
| A5 | Wire iOS bridge: `WebViewBridgeController.swift` with `WKScriptMessageHandler` dispatching to existing `MahoBridge` | `ios-shell/Support/WebViewBridgeController.swift` | BYOK screen functional on iOS simulator |
| A6 | Side-by-side comparison: web BYOK vs native BYOK — UX quality gate | Screenshot comparison document | Team approval to proceed |

**Decision gate after A6:** If cold start > 1.5s or UX quality is materially worse, evaluate alternatives before proceeding. Revert criteria defined in Section 9.

### Phase B — Core Chat Screens (Week 3-4)

| Step | Task | Deliverable |
|------|------|-------------|
| B1 | Port Chat screen — streaming, message history, image display | `src/screens/chat/` |
| B2 | Port Conversation List — list, rename, delete | `src/screens/conversation-list/` |
| B3 | Port Space AI Config — form UI | `src/screens/space-ai-config/` |
| B4 | Integration test: all 3 screens on both platforms via Appium | Test results passing |

Chat (B1) is intentionally second — it's the highest-complexity screen and will stress-test the bridge (streaming events, image payloads, session lifecycle). If Chat works well, everything else is simpler.

### Phase C — Remaining Screens + Cleanup (Week 5-6)

| Step | Task | Deliverable |
|------|------|-------------|
| C1 | Port Privacy Dashboard | `src/screens/privacy-dashboard/` |
| C2 | Port Offline Model screen | `src/screens/offline-model/` |
| C3 | Port Agent screen (WebSocket UI) | `src/screens/agent/` |
| C4 | Port Visual Search results view (hybrid: native camera → web results) | `src/screens/visual-search-results/` |
| C5 | Remove deprecated native screen files (behind feature flag for one release) | `legacy/` directory with dead native code |
| C6 | Update `AGENTS.md` files, add `web-ai/AGENTS.md` | Documentation |

### Phase D — Desktop Alignment (Parallel to Phase C)

| Step | Task | Deliverable |
|------|------|-------------|
| D1 | Configure `maho-chromium/browser/resources/maho_ai/` to use shared web bundle | Updated GN build rules (user-approved) |
| D2 | Verify desktop AI panel still works with unified bundle | Existing desktop Vitest + manual QA |
| D3 | Single source of truth: `web-ai/` ships to all 3 platforms | Build scripts produce platform-specific outputs |

---

## 6. Integration with Existing Rust Core

### No Rust Changes Required

The migration does NOT require changes to `maho-core` or `maho-jni`. The web bridge layer calls the exact same JNI/FFI methods that native screens currently call:

**Android path:**
```
Web bundle → postMessage → @JavascriptInterface WebViewBridge.kt
  → MahoBridge.kt (existing companion object)
    → JNI → maho-jni → maho-core
```

**iOS path:**
```
Web bundle → WKScriptMessageHandler → WebViewBridgeController.swift
  → MahoBridge.swift (existing instance)
    → FFI → maho-core
```

The only new code is the thin dispatcher layer (`WebViewBridge.kt` / `WebViewBridgeController.swift`) that translates JSON-RPC to existing method calls.

### Bridge Method Mapping (Android)

| RPC Method | Existing `MahoBridge.kt` Method |
|------------|--------------------------------|
| `byokGetProviders` | `MahoBridge.byokGetProviders()` |
| `byokGetKey` | `MahoBridge.byokGetKey(provider)` |
| `byokSetKey` | `MahoBridge.byokSetKey(provider, key)` |
| `byokDeleteKey` | `MahoBridge.byokDeleteKey(provider)` |
| `byokValidateKey` | `MahoBridge.byokValidateKey(provider, key)` |
| `chatSessionNew` | `MahoBridge.chatSessionNew(apiKey, endpoint, model, systemInstruction)` |
| `chatSendUserTurn` | `MahoBridge.chatSendUserTurn(wrapperPtr, message)` |
| `chatSendImage` | `MahoBridge.chatSendImage(wrapperPtr, mime, data)` |
| `chatCancel` | `MahoBridge.chatCancel(wrapperPtr)` |
| `chatSessionPollEvent` | `MahoBridge.chatSessionPollEvent(wrapperPtr)` |
| `chatSessionFree` | `MahoBridge.chatSessionFree(wrapperPtr)` |
| `listConversations` | `MahoBridge.listConversations(limit)` |
| `getConversationMessages` | `MahoBridge.getConversationMessages(sessionId)` |
| `saveConversationMessage` | `MahoBridge.saveConversationMessage(sessionId, role, content, urlContext)` |
| `createConversation` | `MahoBridge.createConversation(id, title, spaceId, model)` |
| `deleteConversation` | `MahoBridge.deleteConversation(sessionId)` |
| `renameConversation` | `MahoBridge.renameConversation(sessionId, title)` |
| `offlineModelInfo` | `MahoBridge.offlineModelInfo()` |
| `offlineModelIsDownloaded` | `MahoBridge.offlineModelIsDownloaded()` |
| `offlineModelDelete` | `MahoBridge.offlineModelDelete()` |
| `capturePhoto` | Native-only: launches CameraCaptureScreen, returns bytes |
| `captureScreenshot` | Native-only: calls platform screenshot API |

---

## 7. File Layout (Concrete Paths)

```
maho/web-ai/                          # NEW — web AI bundle source
├── package.json
├── vite.config.ts
├── tsconfig.json
├── index.html                        # Entry point (minimal shell)
├── src/
│   ├── app.tsx                       # Hash router + screen switch
│   ├── bridge/
│   │   ├── types.ts                  # MahoBridge interface
│   │   ├── rpc.ts                    # JSON-RPC dispatcher
│   │   └── platform-detect.ts        # iOS/Android/Desktop detection
│   ├── screens/
│   │   ├── byok/
│   │   │   ├── byok-screen.tsx
│   │   │   └── provider-card.tsx
│   │   ├── chat/
│   │   │   ├── chat-screen.tsx
│   │   │   ├── chat-bubble.tsx
│   │   │   ├── chat-input.tsx
│   │   │   └── use-chat-session.ts
│   │   ├── conversation-list/
│   │   │   └── conversation-list-screen.tsx
│   │   ├── agent/
│   │   │   └── agent-screen.tsx
│   │   ├── space-ai-config/
│   │   │   └── space-ai-config-screen.tsx
│   │   ├── offline-model/
│   │   │   └── offline-model-screen.tsx
│   │   ├── privacy-dashboard/
│   │   │   └── privacy-dashboard-screen.tsx
│   │   └── visual-search-results/
│   │       └── visual-search-results-screen.tsx
│   ├── ui/                           # Shared primitives
│   │   ├── button.tsx
│   │   ├── input.tsx
│   │   ├── list-item.tsx
│   │   ├── status-banner.tsx
│   │   ├── markdown.tsx
│   │   └── icon.tsx
│   └── hooks/
│       ├── use-bridge.ts             # Bridge context + typed caller
│       ├── use-streaming.ts          # Chat poll loop as hook
│       └── use-hash-route.ts         # Hash-based navigation
├── tests/
│   ├── bridge.test.ts
│   ├── byok-screen.test.tsx
│   └── chat-screen.test.tsx
└── dist/                             # Build output (gitignored)
    ├── ai-bundle.js
    ├── ai-bundle.css
    └── ai-bundle.js.map

maho/android-shell/app/src/main/
├── assets/web-ai/                    # Copied from dist/ at build time
│   ├── index.html
│   ├── ai-bundle.js
│   └── ai-bundle.css
└── java/dev/maho/browser/ui/webview/
    ├── AIWebViewFragment.kt          # WebView container for AI screens
    └── WebViewBridge.kt              # @JavascriptInterface JSON-RPC dispatcher

maho/ios-shell/
├── web-ai/                           # Copied from dist/ at build time (Xcode resource)
│   ├── index.html
│   ├── ai-bundle.js
│   └── ai-bundle.css
└── Support/
    └── WebViewBridgeController.swift  # WKScriptMessageHandler dispatcher
```

---

## 8. Risks & Mitigations

| # | Risk | Severity | Mitigation |
|---|------|----------|------------|
| 1 | **Cold start latency** — WebView initialization + JS parse time exceeds budget | High | Pre-warm WebView at app startup (background); Preact keeps bundle tiny; measure on target devices in Phase A |
| 2 | **Native UX feel** — Web scrolling/touch behavior feels non-native | High | Use native scroll containers where possible; add `overscroll-behavior: none`; implement haptic bridge calls; test with real users in Phase A gate |
| 3 | **Bridge async race conditions** — Multiple inflight RPC calls with out-of-order responses | Medium | JSON-RPC IDs guarantee correct dispatch; bridge dispatcher is single-threaded on JS side; Rust core methods are stateless (each call is independent) |
| 4 | **App Store rejection** — Apple may reject apps with "too much" WebView content | Low | Apple's policy allows WebView for app content (not a "wrapper app"); Maho is primarily a browser; camera/tabs/navigation remain native; precedent: Arc Search, Brave News |
| 5 | **Chat streaming latency** — Poll-based bridge adds ~50ms per token vs native | Medium | Keep poll interval at 50ms (matching current native); consider WebSocket push from Rust for future optimization |
| 6 | **Image payload size** — Large photos passed through bridge JSON as base64 | Medium | Use binary transfer: Android `@JavascriptInterface` supports `byte[]`; iOS `WKScriptMessageHandlerWithReply` supports typed data; fallback to chunked base64 |
| 7 | **Memory pressure** — WebView + native app competing for RAM on low-end devices | Medium | Limit WebView to single instance; release when AI screen dismissed; set `android:hardwareAccelerated="false"` if needed |
| 8 | **Desktop bundle divergence** — Desktop Mojo bindings differ from mobile postMessage | Low | Bridge abstraction layer (`platform-detect.ts`) handles platform differences; same React components render everywhere |

---

## 9. Effort Estimate

| Phase | Duration | Engineer-Weeks | Assumptions |
|-------|----------|----------------|-------------|
| A — Foundation | 2 weeks | 2 eng-weeks | One engineer, full-time |
| B — Core Chat | 2 weeks | 3 eng-weeks | Chat streaming complexity; 1.5 engineers |
| C — Remaining + cleanup | 2 weeks | 2 eng-weeks | Straightforward ports |
| D — Desktop alignment | 1 week (parallel) | 1 eng-week | Reuse existing Mojo infrastructure |
| **Total** | **6 weeks** | **8 eng-weeks** | |

**Comparison with continuing native:**
- Next 13 planned AI features (W6-W12 roadmap) at native cost: 13 × 1,600 LOC = 20,800 LOC new native code across 2 platforms
- Same features with unified web: 13 × 450 LOC = 5,850 LOC
- **Savings: ~15,000 LOC over the next 6 months, plus ongoing maintenance reduction**

**ROI timeline:** Break-even at feature #4 post-migration. With 13 planned features, this delivers 3.6× return on the 8 eng-week investment.

---

## 10. Testing Strategy

### Unit Tests (Web)
- Framework: Vitest (already used in `maho-chromium/browser/resources/maho_ai/`)
- Location: `maho/web-ai/tests/`
- Coverage target: Bridge contract (100%), screen logic (80%), UI components (60%)
- Mock bridge: `createMockBridge()` for isolated component testing

### Integration Tests (Platform)
- Framework: Appium (existing at `maho/appium-tests/`)
- Scope: Each migrated screen on both Android emulator + iOS simulator
- Key scenarios:
  - BYOK: add key → validate → verify stored → delete
  - Chat: send message → receive streaming response → verify displayed
  - Conversations: create → list → rename → delete
  - Offline model: check status → delete model

### Visual Regression
- Tool: Playwright screenshot comparison (new) or manual screenshot diffing
- Baseline: Current native screenshots
- Threshold: <2% pixel difference

### Performance
- Cold start: Measure time from `loadUrl()` to first meaningful paint
- Budget: <800ms Android (Pixel 6a), <500ms iOS (iPhone 13)
- Interaction latency: <100ms for tap → response
- Streaming: First token visible within 200ms of bridge event

---

## 11. Rollback Plan

### Feature Flag Architecture
```kotlin
// Android: FeatureFlags.kt
object FeatureFlags {
    val USE_WEB_AI_BYOK = BuildConfig.WEB_AI_BYOK_ENABLED  // per-screen flags
    val USE_WEB_AI_CHAT = BuildConfig.WEB_AI_CHAT_ENABLED
    // ... one per screen
}
```

```swift
// iOS: FeatureFlags.swift
enum FeatureFlags {
    static let useWebAIBYOK = Bundle.main.infoDictionary?["WEB_AI_BYOK_ENABLED"] as? Bool ?? false
    // ... one per screen
}
```

### Revert Criteria (any one triggers rollback):
1. Cold start exceeds 2.0 seconds on target devices
2. Crash rate attributable to web screens > 0.5% of sessions
3. Bug count in web screens exceeds equivalent period of native screens by >50%
4. User feedback (beta) overwhelmingly negative on UX quality

### Retention Policy
- Native screen code moved to `legacy/` branch (not deleted from git history)
- Keep for one full release cycle (2 weeks post-migration)
- Feature flags allow per-screen revert without full rollback

---

## 12. Open Questions (Requiring Team Decision)

| # | Question | Recommendation | Impact if Deferred |
|---|----------|---------------|-------------------|
| 1 | Should Agent screen use native WebSocket or web-native `WebSocket` API? | Use web-native `WebSocket` — simpler, no bridge needed for network | Low — either works |
| 2 | Should we pre-warm the WebView at app cold start or lazily on first AI screen open? | Pre-warm — allocate in background during splash | Affects cold start budget |
| 3 | Should desktop AI panel (`chrome://maho-ai`) be refactored to use the same source, or keep separate? | Phase D unifies them; but existing Desktop code has developer-mode features — keep those as desktop-only components | Delay means two React AI codebases temporarily |
| 4 | Dark mode / theming: CSS variables injected by native, or detect from `prefers-color-scheme`? | Native injects CSS variables matching current space theme (mirrors how desktop WebUI does it) | Blocks Phase A if not decided |
| 5 | Voice input: bridge to native `SpeechRecognizer`, or use Web Speech API? | Bridge to native — better accuracy, offline support on both platforms | Blocks Chat screen if deferred |

---

## 13. Recommended Starting Screen (Phase A2)

**BYOK Settings** — for these reasons:

1. **Simplest bridge surface**: Only 5 RPC methods (`byokGetProviders`, `byokGetKey`, `byokSetKey`, `byokDeleteKey`, `byokValidateKey`)
2. **Pure form UI**: No streaming, no real-time updates, no complex state
3. **Existing native is well-contained**: 589 LOC Android / 421 LOC iOS — easy to compare
4. **No native API dependencies**: Secure storage is already behind Rust bridge
5. **Tests existing bridge plumbing**: If BYOK works end-to-end, the bridge contract is proven
6. **Reversible**: Low-stakes screen — if web version is worse, native fallback is trivial

If BYOK validates the approach, Chat (Phase B1) is the real stress test that proves streaming, session management, and image handling work through the web bridge.

---

## Appendix: Commands

```bash
# Development
cd maho/web-ai && bun install
cd maho/web-ai && bun run dev          # Vite dev server on :5173
cd maho/web-ai && bun run build        # Production bundle → dist/
cd maho/web-ai && bun run test         # Vitest
cd maho/web-ai && bun run typecheck    # tsc --noEmit

# Copy to Android assets
cp -r maho/web-ai/dist/* maho/android-shell/app/src/main/assets/web-ai/

# Copy to iOS bundle
cp -r maho/web-ai/dist/* maho/ios-shell/web-ai/

# Full mobile build (includes web bundle copy)
cd maho/android-shell && ./gradlew app:assembleDebug
cd maho/ios-shell && make build-app
```
