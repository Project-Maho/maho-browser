# Native-vs-Web Boundary Rules and Guidelines

This document outlines the architectural guidelines and boundaries governing when to build features using platform-native UI elements versus embedding them inside the unified web-based AI interface (`web-ai` WebView).

## Overview & Core Rationale

Maho Browser is built on a hybrid architecture combining a high-performance C++ browser overlay (Brave-style) and platform-native shells (macOS AppKit, iOS UIKit/SwiftUI, Android Jetpack Compose) with a shared Rust logic core (`maho-core` and `maho-agent`).

To ensure both maximum performance and velocity, we separate the UI surfaces into two distinct categories:
1. **Platform-Native UI**: Used for core browser chrome, navigation controls, and low-latency interaction loops.
2. **Web-Based WebView UI (`web-ai`)**: Used for the AI chat panel, assistant history, configuration tools, and settings interfaces that require rich cross-platform layout symmetry.

By maintaining a clear division, we guarantee that the browser feels like a first-class citizen on every platform while allowing the AI agent framework to evolve rapidly without requiring duplicate implementations across native platforms.

---

## Architectural Decision Boundary

| Feature Category | Implementation Target | Rationale |
|:---|:---|:---|
| **Core Navigation Chrome** | Native | Tab bars, address bars, search pills, and context menus must respond instantly to touch/keyboard events and conform to system-native accessibility standards. |
| **Tab Grid & Workspace View** | Native | Managing lists of active tabs requires native scroll performance, drag-and-drop mechanics, and tight memory constraints to prevent crashes on mobile. |
| **AI Chat & Prompts** | WebView | Rich rendering of markdown, code highlights, chat bubbles, streaming messages, and attachments are highly layout-dynamic and change frequently based on upstream LLM capabilities. |
| **Space Config & Theme Picker** | WebView | The space creation and theme customization experience (`chrome://maho-space-create`) relies on complex interactive grids, palettes, and animations that are shared between desktop and mobile. |
| **System Settings** | Native | Platform settings (Privacy, Sync setup, permissions) hook directly into native APIs (Keychain, Android Keystore) and must match the OS Settings layout hierarchy. |

---

## Guidelines for New Features

When designing a new feature, engineers must evaluate the boundary criteria before selecting the UI technology stack.

### 1. When to Build Native (SwiftUI / Jetpack Compose / Views C++)
* **System Integration**: The feature requires direct OS permission access, camera/photo selectors, background execution, or deep keychain binding.
* **Micro-interactions**: The feature involves dragging, swiping, or fluid gestures (e.g., pinching tabs or swiping to delete tab cards) where a WebView introduces sub-frame latency.
* **Accessibility (a11y)**: Screen readers (VoiceOver, TalkBack) must navigate the element flawlessly as part of the operating system's default navigation tree.

### 2. When to Build in the `web-ai` WebView (React + Tailwind / Astro)
* **Cross-Platform Layouts**: The UI is identical across macOS, iOS, Android, and Desktop, and contains complex typographic structures (markdown tables, syntax highlighting, charts).
* **AI Tool Orchestration**: The UI reflects live agent activities, displaying streaming thoughts, tool invocation trees, or cost estimates.
* **Rapid Rollouts**: The layout needs to update dynamically based on server-driven config or quick web bundles without requiring an app store update cycle.

---

## Performance & Memory Management

Because WebViews carry significant memory and CPU overhead compared to native components:
1. **Lazy Attachment**: WebView instances must be lazy-loaded only when the user enters the AI assistant pane or creates a space config.
2. **Active Disconnect**: The JS-RPC bridge controller must call `shutdownStreaming()` and release FFI pointers upon view destruction to prevent memory leaks and dangling background polling threads.
3. **No Duplicate Engines**: Never spawn more than one active chat WebView context per active window/profile. Keep a single coordinator pattern.

---

## v3 Agent Runtime Implementation Boundary

The v3 agent runtime (`maho-agent` crate) lives **entirely in the native domain**. WebView surfaces interact with it only through Mojo IPC mediated by the Chromium overlay; they never call the FFI directly.

### Boundary stack

```
┌────────────────────────────────────────────────┐
│  WebView (agent panel)                         │
│    resources/agent/app.ts                      │
└───────────────────┬────────────────────────────┘
                    │ Mojo IPC (no FFI access)
                    ▼
┌────────────────────────────────────────────────┐
│  Chromium Browser Process (C++)                │
│    maho_ai_runtime_router.cc                   │
│    maho_unified_agent_adapter.cc  ◀── always-on (Phase 3, no feature flag)
└───────────────────┬────────────────────────────┘
                    │ C ABI FFI (8-arg create_session)
                    ▼
┌────────────────────────────────────────────────┐
│  Native shells bypass overlay:                 │
│    Android (maho-jni) ────────────────────┐    │
│    iOS    (MahoBridge+Agent.swift) ───────┤    │
│    macOS  (deferred — Chromium fork)      │    │
└───────────────────────────────────────────┴────┘
                    │
                    ▼
┌────────────────────────────────────────────────┐
│  Rust agent runtime (maho-agent)               │
│    SwiftideBackend + MahoOpenAIConfig          │
│    + BYOK SecretString + Permission gating    │
└────────────────────────────────────────────────┘
```

### Phase 3 migration (no feature flag)

Earlier rollout used `kMahoAgentV2` to gate the `MahoUnifiedAgentAdapter` path. As of Phase 3:

- **Flag removed** from `maho_features.{cc,h}`.
- **Router always creates** `MahoUnifiedAgentAdapter` via `RebuildAdapterIfStale`.
- **Legacy OpenCode subprocess path is fully gone** — no subprocess spawn anywhere in the C++ code. The class was renamed from `MahoOpencodeAcpAdapter` to `MahoUnifiedAgentAdapter`; files renamed from `maho_opencode_acp_adapter.{cc,h}` to `maho_unified_agent_adapter.{cc,h}`.
- Desktop adapter passes `allow_insecure_key_storage=false` and wires permission + secure-storage callbacks (default-deny `shell_exec`; secure-storage returns null pending OSCrypt-encrypted-prefs migration → fail-closed).

Native shells (iOS/Android) never used this flag; they always called the FFI directly.

### FFI contract (8-arg create_session)

```c
maho_agent_session* maho_agent_create_session(
    MahoCore* core,                          // 1: core handle
    const char* session_id,                  // 2: UUID string
    const char* workspace_root,              // 3: filesystem scope
    bool allow_insecure_key_storage,         // 4: ALWAYS false on mobile
    MahoAgentPermissionCallback permission_cb,        // 5: tool-call gate
    void* permission_user_data,              // 6: caller-owned context
    MahoAgentSecureStorageCallback secure_storage_cb, // 7: BYOK key lookup
    void* secure_storage_user_data           // 8: caller-owned context
);
```

- Sensitive secret material returned by `secure_storage_cb` uses the `(ptr, len, free_fn)` `MahoAgentSecureKey` triple. Rust zeroizes the buffer before invoking `free_fn`.
- `allow_insecure_key_storage` is hardcoded `false` on both iOS and Android JNI shims — falling back to plaintext settings storage is forbidden on mobile.

### Domain rules (v3 binding)

1. **maho-agent has NO domain dependency on maho-core.** Sessions are short-lived and persist conversation history through `maho-storage` only.
2. **Secrets never traverse Mojo.** BYOK keys are loaded by the C++ adapter directly via `secure_storage_callback` and never enter renderer-process IPC.
3. **Session handles never leave the Browser Process.** WebUI sees agent events as Mojo-validated state, never raw `OpaquePointer`.
4. **Native shells bypass the Chromium overlay entirely.** Mobile and macOS-shell call `maho-agent` FFI directly; the overlay path is desktop-only.
5. **Polling consumer pattern is mandatory on native shells.** See [streaming-event-protocol.md](./streaming-event-protocol.md) for the JSON envelope queue and single-owner-free invariant.

### Cross-references

- [streaming-event-protocol.md](./streaming-event-protocol.md) — event envelope and per-platform consumer pattern
- `maho/crates/maho-agent/` — Rust agent crate
- `maho-chromium/browser/ai/maho_ai_runtime_router.cc` — router (Phase 3 simplified, no flag gates)
- `maho-chromium/browser/ai/maho_unified_agent_adapter.{cc,h}` — adapter (always-on, calls maho-agent FFI directly)
- `.omo/plans/maho-mobile-agent-framework-v3.md` — master plan W0–W3
