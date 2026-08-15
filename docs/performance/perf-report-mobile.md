# Maho Browser 모바일 셸 및 shared web-ai 성능 정적 분석 보고서

## 1. Executive summary

본 보고서는 `maho/ios-shell`, `maho/android-shell`, `maho/web-ai`를 대상으로 수행한 **정적(read-only) 성능 분석** 결과입니다. iOS/Android 앱의 cold start, WebView/WKWebView 구성 및 브리지, JNI/JavaScript 상호운용, UI-thread 작업, WebView 수명과 메모리, 이미지 처리, 그리고 shared Preact UI의 번들 로딩·스트리밍·리스트 렌더링 경로를 검토했습니다.

이번 분석에서 가장 우선적으로 수정할 항목은 다음과 같습니다.

1. **iOS cold start 경로가 첫 화면 구성 전에 Keychain/파일 시스템/core 생성/state load를 동기 실행합니다.** 첫 프레임 전에 FFI 및 저장소 I/O가 직렬화되어 있습니다.
2. **Android cold start도 `Application.onCreate()`에서 AndroidKeyStore/SharedPreferences/JNI core 생성/state load를 동기 실행합니다.** 특히 SQLCipher key 생성 경로의 `SharedPreferences.commit()`은 동기 디스크 쓰기입니다.
3. **shared web-ai가 하나의 IIFE `ai-bundle.js`로 강제 인라인되어 route-level code splitting이 불가능합니다.** `app.tsx`도 모든 화면을 정적으로 import하므로 BYOK와 같은 작은 화면을 열어도 chat/agent/conversations 코드까지 함께 파싱·평가합니다. 동일 bundle을 사용하는 Desktop에도 그대로 영향을 줍니다.
4. **Chat streaming이 이벤트마다 전체 message 배열을 탐색/복사하고 전체 message list를 다시 렌더링합니다.** 긴 대화에서 토큰 스트리밍 중 작업량이 대화 길이에 따라 증가합니다.
5. **Android content blocker 활성 시 `shouldInterceptRequest()`마다 전체 Settings JSON을 JNI에서 가져와 역직렬화한 뒤 다시 JNI filter query를 실행합니다.** 서브리소스 수에 비례하여 동기 request-interception 비용이 증가합니다.

추가로 50 ms 고정 polling, streamed image의 반복 base64 문자열 복사, conversation list 무가상화, iOS sync의 `@MainActor` 저장소 작업, navigation마다 screenshot/favicon 처리, Android lifecycle의 동기 save/archive, WebView bridge session 정리 누락 위험을 확인했습니다.

> **분석 제약:** 요청 조건에 따라 build, Gradle, Xcode, simulator/emulator, runtime profiler는 실행하지 않았습니다. 따라서 아래 심각도는 실제 wall-clock 측정치가 아니라 코드 경로, 호출 빈도, thread affinity, 데이터 크기 증가 특성에 근거한 정적 우선순위입니다. 소스 코드는 수정하지 않았으며 본 보고서 파일만 생성합니다.

## 2. 범위와 판정 기준

### 분석 범위

- **iOS ownership:** `maho/ios-shell`
- **Android ownership:** `maho/android-shell`
- **shared-web-ai ownership:** `maho/web-ai`
- shared-web-ai는 iOS/Android host뿐 아니라 Desktop의 `chrome://maho-ai` 경로에서도 사용하는 공용 UI입니다 (`maho/AGENTS.md:91`). 따라서 shared-web-ai의 번들/렌더링 개선은 Desktop에도 적용됩니다.

### 심각도 기준

| Severity | 기준 |
|---|---|
| **High** | 첫 프레임/페이지 로드/스트리밍 hot path를 직접 차단하거나, 호출 수·대화 길이·리소스 수에 따라 비용이 크게 증가하는 구조입니다. |
| **Medium** | 반복 navigation/overlay/sync/이미지 처리에서 UI jank, IPC overhead, memory churn 또는 배터리 비용을 만들 가능성이 높습니다. |
| **Low** | 현재 코드만으로 사용자 체감 영향이 제한적으로 보이며, 우선순위가 낮은 미세 최적화입니다. |

## 3. Severity table

| # | Severity | Ownership | 영역 | 요약 |
|---:|:---:|---|---|---|
| 1 | **High** | iOS | App startup | 첫 프레임 전에 Keychain/파일/core/state load 동기 실행 |
| 2 | **High** | Android | App startup | `Application.onCreate()`에서 KeyStore/commit/JNI/state load 동기 실행 |
| 3 | **High** | shared-web-ai | Bundle/load | 단일 IIFE + `inlineDynamicImports` + 모든 screen 정적 import |
| 4 | **High** | shared-web-ai | Rendering | token/thinking event마다 O(N) message update + 전체 list rerender |
| 5 | **High** | Android | Networking/WebView | 모든 intercepted resource마다 Settings JSON decode + JNI filter query |
| 6 | **Medium** | shared-web-ai | JS/native IPC | chat/agent의 50 ms polling으로 지속적인 bridge 왕복 |
| 7 | **Medium** | shared-web-ai | Image/memory | image chunk마다 base64 전체 복사 + 완전한 data URL 재생성 |
| 8 | **Medium** | shared-web-ai | List/scroll | conversations 최대 100건 일괄 렌더 + 반복 filter/includes |
| 9 | **Medium** | iOS | Sync/UI thread | Sync transport 전체가 `@MainActor`, pull/apply 저장소 루프도 main actor |
| 10 | **Medium** | iOS | WebView creation | browser WKWebView 생성마다 약 350 KB Readability source 동기 읽기/문자열 구성 |
| 11 | **Medium** | iOS | Navigation/image | 모든 navigation finish마다 screenshot + favicon fetch/decode/re-encode |
| 12 | **Medium** | iOS | JS/native IPC | tool/image delta는 token과 달리 batch 없이 `evaluateJavaScript` 호출 |
| 13 | **Medium** | Android | Lifecycle/UI thread | `onResume` auto-archive, `onPause` save-state를 UI thread에서 동기 JNI 호출 |
| 14 | **Medium** | Android | Memory/lifetime | AI WebView destroy 시 bridge coroutine/session registry 정리 API가 없음 |

**합계:** High 5건, Medium 9건, Low 0건입니다. Low 항목을 억지로 분리하기보다 사용자 체감 가능성이 있는 High/Medium 경로에 우선순위를 집중했습니다.

---

## 4. Prioritized findings

### P0-1. iOS: 첫 프레임 전에 저장소/Keychain/core 초기화를 동기 실행합니다

- **Severity:** High
- **Ownership:** iOS
- **영향 영역:** cold start, first frame, main thread

**Evidence**

- `maho/ios-shell/MahoApp.swift:17-31` — `MahoApp.init()`에서 storage path 계산 후 `provisionSqlcipherKey`, `initialize`, `loadState`, sync session 구성을 연속 호출합니다. `provisionSqlcipherKey` 호출은 `:25`에서 확인됩니다.
- `maho/ios-shell/Bridge/MahoBridge.swift:58-126` — SQLCipher key provisioning 경로가 `FileManager`, Keychain 조회/저장, directory/file 검사를 동기 수행합니다.
- `maho/ios-shell/Bridge/MahoBridge.swift:138-158` — `initialize(storagePath:)`가 `maho_core_new_with_storage` FFI를 동기 호출합니다.
- `maho/ios-shell/Bridge/MahoBridge+Lifecycle.swift:13-19` — `loadState()`가 core FFI를 동기 호출합니다.

**문제**

SwiftUI `App.init()`은 UI가 화면에 나오기 전 실행됩니다. 이 위치에서 Keychain, 파일 시스템, database/core 생성과 state load가 직렬로 완료되어야 이후 단계로 진행할 수 있습니다. 저장소 크기, Keychain 상태, DB recovery 등에 따라 cold-start tail latency가 커질 수 있습니다.

**Suggested fix**

1. first frame에 반드시 필요한 최소 bootstrap state만 동기로 남기고 core open/state hydration을 별도 startup coordinator로 이동합니다.
2. splash/root shell을 먼저 표시한 뒤 `Task.detached` 또는 전용 actor/queue에서 storage/core 초기화를 수행하고, 준비 완료 시 observable state를 publish합니다.
3. Keychain key는 한 번 읽은 뒤 process lifetime 동안 메모리에 보관하고, directory/resource-value setup은 최초 설치 또는 migration 시점으로 제한합니다.
4. core API가 thread-safe하지 않다면 전용 serial actor를 만들어 FFI 순서를 보장하되 MainActor와 분리합니다.

**수정 후 검증 지표**

- process start → first frame
- process start → interactive root shell
- Keychain provision/core open/loadState 각각의 signpost duration

---

### P0-2. Android: `Application.onCreate()`가 KeyStore/동기 commit/JNI/state load를 직렬 실행합니다

- **Severity:** High
- **Ownership:** Android
- **영향 영역:** cold start, time-to-first-activity

**Evidence**

- `maho/android-shell/app/src/main/java/dev/maho/browser/MahoApplication.kt:8-18` — `onCreate()`에서 directory 생성, `provisionSqlcipherKey()`, `initialize()`, `loadState()`, `SyncManager.initialize()`를 순서대로 실행합니다. key provisioning은 `:15`입니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeAi.kt:22-53` — `AndroidKeyStore` load/key 생성과 `Cipher` 준비가 동기 경로에 있습니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeAi.kt:165-187` — SQLCipher key가 없으면 암호화 후 `SharedPreferences.edit().putString(...).commit()`으로 동기 디스크 commit을 수행합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt:11-13` — 첫 bridge 접근 시 `System.loadLibrary("maho_jni")`가 발생합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt:27-30,48-61` — native core 생성과 state lifecycle API가 직접 JNI를 호출합니다.

**문제**

`Application.onCreate()`는 첫 Activity가 그려지기 전에 main thread에서 실행됩니다. KeyStore 초기화, 동기 preferences commit, native library load, DB/core open, state hydration을 모두 이 구간에 넣으면 cold-start가 저장소와 보안 provider 비용에 묶입니다.

**Suggested fix**

1. native library load와 최소 key availability 확인 외의 heavy initialization을 startup coroutine으로 분리합니다.
2. UI가 필요로 하는 snapshot만 빠르게 로드하고 DB/core hydration은 `Dispatchers.IO` 전용 dispatcher에서 처리합니다.
3. key 최초 생성 시 `.commit()` 대신 비동기 `.apply()`가 안전한지 검토하거나, 반드시 durable-before-core-open이어야 한다면 해당 경로만 background thread에서 완료한 뒤 core를 엽니다.
4. AndroidX Startup을 사용하더라도 initializer 자체에서 block하지 말고 dependency 순서만 정의하며 실제 I/O는 비동기로 유지합니다.

**수정 후 검증 지표**

- cold start TTID/TTFD
- `Application.onCreate` duration
- KeyStore/key provision, native load, core create, state load 각각의 trace slice

---

### P0-3. shared-web-ai: 모든 화면이 하나의 IIFE bundle로 강제되어 route-level code splitting이 불가능합니다

- **Severity:** High
- **Ownership:** shared-web-ai
- **영향 영역:** bundle parse/evaluate, WebView first paint, iOS/Android/Desktop 공통

**Evidence**

- `maho/web-ai/vite.config.ts:15-35` — build output을 `format: 'iife'`로 만들고 `inlineDynamicImports: true`를 설정하며 entry를 `ai-bundle.js` 하나로 고정합니다.
- `maho/web-ai/src/app.tsx:10-15` — Agent, Settings, Chat, Conversations, Onboarding screen을 모두 정적으로 import합니다.
- `maho/web-ai/src/app.tsx:35-58` — 실제 화면 선택은 hash route `switch` 단계에서 뒤늦게 이루어집니다.
- `maho/AGENTS.md:91` — 동일 `web-ai`가 iOS WKWebView, Android WebView, Desktop `chrome://maho-ai`에서 사용됨을 명시합니다.

**문제**

BYOK처럼 작은 surface를 열어도 chat, agent, conversations 등의 코드가 하나의 JavaScript 파일에 포함되어 네트워크/asset read 이후 전부 parse/evaluate 대상이 됩니다. 이는 WebView를 새로 만드는 모바일 overlay에서 특히 손해가 크며 Desktop AI panel도 동일한 초기 bundle 비용을 부담합니다.

**Suggested fix**

1. route 단위 entry chunk를 분리합니다: `byok`, `conversations/chat`, `agent`, `onboarding`.
2. ES module을 안정적으로 제공할 수 있는 Android/Desktop은 dynamic `import()`를 사용합니다.
3. iOS `file://` 제약 때문에 module chunk 로딩이 불안정하다면, tiny bootstrap + 화면별 classic IIFE entry 여러 개를 생성하여 native host가 route에 맞는 entry만 로드하도록 구성하거나 `WKURLSchemeHandler` 기반 local scheme으로 module loading을 지원합니다.
4. bundle budget을 CI에 추가하여 entry별 minified/gzip size regression을 검출합니다.

**수정 후 검증 지표**

- route별 transferred/local-read bytes
- JS parse/evaluate time
- WebView navigation start → first contentful paint
- Desktop `chrome://maho-ai` panel open latency

---

### P0-4. shared-web-ai: Chat streaming event마다 O(N) message update와 전체 list render가 발생합니다

- **Severity:** High
- **Ownership:** shared-web-ai
- **영향 영역:** streaming UI, long conversation, scroll/jank, iOS/Android/Desktop 공통

**Evidence**

- `maho/web-ai/src/screens/chat/chat-screen.tsx:371-407` — streaming reducer가 token/thinking event마다 message state를 새로 만듭니다.
- `maho/web-ai/src/screens/chat/chat-screen.tsx:1044-1084` — 렌더 시 `state.messages.map(...)`으로 전체 message list를 다시 순회합니다.
- `maho/web-ai/src/screens/chat/chat-screen.tsx:1464-1482` — assistant token append가 streaming assistant를 찾은 뒤 `messages.map(...)`으로 전체 배열을 복사하며 한 항목을 갱신합니다.

**문제**

스트리밍 event 수를 E, 기존 message 수를 N이라고 하면 현재 update 경로의 배열 탐색/복사는 대략 O(E×N)으로 증가합니다. Preact diff가 DOM write를 줄여도 VNode 생성과 component 계산 자체는 반복됩니다. 대화가 길수록 token cadence에서 main JS thread 부담이 커지고 scroll frame과 경쟁합니다.

**Suggested fix**

1. streaming 중인 마지막 assistant message를 별도 `liveMessage` state/signal로 분리하고, 완료 시 immutable history에 한 번 합칩니다.
2. token/thinking delta는 buffer에 모아 `requestAnimationFrame` 또는 16–33 ms cadence로 flush합니다.
3. streaming message index를 직접 보관하여 매 event마다 `find`/`map`하지 않도록 합니다.
4. 완료된 긴 history에는 windowing/virtualization을 적용하고 message row를 memoize합니다.

**수정 후 검증 지표**

- token stream 중 JS long task count
- 50/100/500 message history에서 average frame time
- event당 reducer/render duration

---

### P0-5. Android: 모든 서브리소스 interception에서 Settings JSON decode와 JNI filter query를 반복합니다

- **Severity:** High
- **Ownership:** Android
- **영향 영역:** page resource loading, content blocking, JNI overhead

**Evidence**

- `maho/android-shell/app/src/main/java/dev/maho/browser/ui/WebViewController.kt:99-124` — `shouldInterceptRequest()`마다 `BridgeSettings.getSettingsTyped()`를 호출하여 blocker flag를 얻고, 활성화되어 있으면 `queryFilterDecision`을 수행합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeSettings.kt:19-26` — `getSettingsTyped()`는 `MahoBridge.getSettings()` 결과 JSON 전체를 `decodeFromString<Settings>()`로 매번 역직렬화합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt:713-717` — 각 filter 판정은 `nativeQueryFilterDecision(...)` JNI 호출입니다.

**문제**

`shouldInterceptRequest()`는 document, script, image, font, XHR 등 많은 resource에서 호출됩니다. blocker enable 여부 하나를 알기 위해 매번 full settings를 JNI → JSON string → Kotlin object로 변환하고, 이후 다시 JNI로 필터 판정을 넘깁니다. hot page에서는 resource count에 비례해 allocation/JNI 비용이 증가하고 interception 반환 지연이 실제 resource dispatch에 영향을 줄 수 있습니다.

**Suggested fix**

1. privacy/content-blocker enable flag를 settings change event 시 atomic/volatile snapshot으로 갱신하고 request path에서는 JSON decode를 제거합니다.
2. filter engine pointer/config snapshot을 native 측에 고정하고 JNI API를 allocation-minimized fast path로 만듭니다.
3. 가능하면 source host whitelist도 immutable snapshot/cache로 유지하고 request마다 preference/storage를 읽지 않습니다.
4. 동일 URL/first-party tuple의 짧은 TTL decision cache를 검토하되 dynamic rule update 시 generation으로 invalidation합니다.

**수정 후 검증 지표**

- 100/500 resource page에서 `shouldInterceptRequest` total CPU time
- request당 JNI calls 및 allocations
- blocker on/off page load delta

---

### P1-1. shared-web-ai: chat/agent가 50 ms 고정 polling으로 native bridge를 지속 호출합니다

- **Severity:** Medium
- **Ownership:** shared-web-ai
- **영향 영역:** JS/native IPC, CPU wakeups, battery, iOS/Android/Desktop 공통

**Evidence**

- `maho/web-ai/src/hooks/use-streaming.ts:12` — `POLL_INTERVAL_MS = 50`입니다.
- `maho/web-ai/src/hooks/use-streaming.ts:127-163` — active chat session 동안 `bridge.chatPollEvents(sessionId)` 후 50 ms timeout으로 다음 poll을 예약합니다. event가 없어도 반복합니다.
- `maho/web-ai/src/hooks/use-agent-session.ts:20,149-188` — agent도 같은 50 ms cadence로 `bridge.agentPollEvent(handle)`를 반복하고 empty queue에도 polling을 계속합니다.
- `maho/web-ai/src/hooks/use-agent-session.ts:11-14` — 주석상 Android와 iOS 모두 native event queue를 poll하는 구조입니다.

**문제**

50 ms는 session당 초당 최대 약 20회의 JS↔native 요청을 만듭니다. 각 poll이 값이 없어도 Android에서는 JavaScript interface/JSON/coroutine 경로, iOS에서는 WK bridge/JSON 경로를 통과합니다. 사용자 응답이 길거나 tool 작업이 지연되면 CPU wakeup과 IPC가 누적됩니다.

**Suggested fix**

1. native → JS push event 또는 long-poll 방식으로 전환하여 empty poll을 제거합니다.
2. push가 어렵다면 empty 결과 시 50→100→200 ms adaptive backoff를 적용하고 event 수신 시 즉시 50 ms로 복귀합니다.
3. page hidden/background 시 polling을 중지합니다.
4. chat은 한 poll에서 가능한 event를 batch drain하여 bridge crossing당 payload를 늘리고 crossing 횟수를 줄입니다.

---

### P1-2. shared-web-ai: streamed image가 chunk마다 누적 base64와 data URL 전체를 재생성합니다

- **Severity:** Medium
- **Ownership:** shared-web-ai
- **영향 영역:** memory churn, GC, image streaming, iOS/Android/Desktop 공통

**Evidence**

- `maho/web-ai/src/screens/chat/chat-screen.tsx:1541-1559` — image delta마다 assistant message와 image array를 새로 생성합니다.
- `maho/web-ai/src/screens/chat/chat-screen.tsx:1561-1580` — `existing.rawBase64 + delta.b64Chunk`로 누적 문자열을 새로 만들고, 매 chunk마다 `data:${mime};base64,${nextBase64}` 전체 URL을 재생성합니다.

**문제**

base64 문자열은 이미지가 커질수록 매 append에서 기존 전체 문자열 복사를 유발할 수 있습니다. 동시에 동일 데이터가 `rawBase64`와 `src` data URL에 중복 표현되며 이전 문자열은 GC 대상이 됩니다. 큰 생성 이미지에서 peak memory와 JS GC pause가 커질 수 있습니다.

**Suggested fix**

1. chunk를 배열/byte buffer로 축적하고 일정 cadence 또는 완료 시 한 번 합칩니다.
2. 가능하면 native bridge가 binary/blob-friendly transport를 제공하도록 하고 JS에서는 `Blob` + `URL.createObjectURL()`을 사용합니다.
3. incremental preview가 필요하면 낮은 빈도로만 preview URL을 갱신합니다.
4. 이전 object URL은 `URL.revokeObjectURL()`로 명시 정리합니다.

---

### P1-3. shared-web-ai: Conversations 화면이 최대 100건을 일괄 렌더링하고 그룹 계산을 반복합니다

- **Severity:** Medium
- **Ownership:** shared-web-ai
- **영향 영역:** list/scroll, input responsiveness, mobile WebView

**Evidence**

- `maho/web-ai/src/screens/conversations/conversation-list-screen.tsx:236-251` — `conversationList`에 `limit: 100`을 요청하여 한 번에 state에 적재합니다.
- `maho/web-ai/src/screens/conversations/conversation-list-screen.tsx:465-498` — 모든 group과 conversation row를 `.map()`으로 DOM에 렌더링합니다.
- `maho/web-ai/src/screens/conversations/conversation-list-screen.tsx:503-507` — 프로젝트별로 전체 conversations를 다시 `.filter()`하고 unassigned도 별도 filter합니다.
- `maho/web-ai/src/screens/conversations/conversation-list-screen.tsx:465-498` — 각 row의 선택 여부도 `selectedIds.includes(...)`로 조회하며 rename/input state 변화 시 부모 화면이 다시 계산됩니다.

**문제**

현재 최대치가 100이라 즉시 catastrophic하지는 않지만 모바일 WebView에서 100개 row + menu/selection state는 충분히 layout/style/VDOM 비용을 만들 수 있습니다. 프로젝트 수 P, 대화 수 N에서 그룹화가 O(P×N)이고 row별 includes는 선택 배열 크기에 비례합니다.

**Suggested fix**

1. cursor pagination 또는 incremental loading으로 initial rows를 줄입니다.
2. 50~100개 이상에서는 virtual list/windowing을 적용합니다.
3. `useMemo`에서 한 번 순회하여 `Map<projectId, conversations[]>`를 만들고 O(N+P)로 그룹화합니다.
4. `selectedIds`를 `Set`으로 변환하고 row component를 memoize하며 rename draft state를 해당 row에 국소화합니다.

---

### P1-4. iOS: sync transport의 storage/apply loop가 `@MainActor`에 묶여 있습니다

- **Severity:** Medium
- **Ownership:** iOS
- **영향 영역:** UI thread, background/foreground sync, storage FFI

**Evidence**

- `maho/ios-shell/Sources/Sync/SyncTransport.swift:58` — `SyncTransport` 자체가 `@MainActor`입니다.
- `maho/ios-shell/Sources/Sync/SyncTransport.swift:162-184` — sync cycle task도 명시적으로 `@MainActor`에서 실행됩니다.
- `maho/ios-shell/Sources/Sync/SyncTransport.swift:247-295` — outbox lease/ack, cursor read, 최대 여러 pull page 처리와 각 envelope apply가 같은 actor 경로에서 반복됩니다. `pullAndApply`는 `:277`에서 시작합니다.
- `maho/ios-shell/Sources/Sync/SyncManager.swift:5,21-64` — manager 역시 `@MainActor`이며 session configure/start/syncNow를 관리합니다.

**문제**

HTTP await 자체는 main thread를 계속 점유하지 않지만, await 사이의 core/storage FFI 호출과 per-envelope apply가 MainActor에서 수행됩니다. 대량 catch-up 또는 많은 outbox ack 시 UI event 처리와 경쟁할 수 있습니다.

**Suggested fix**

1. Sync transport를 별도 actor로 이동하고 UI-visible status publish만 MainActor로 hop합니다.
2. core/storage가 serial access를 요구하면 dedicated CoreActor를 두고 sync와 UI가 같은 actor를 공유하되 MainActor에서는 분리합니다.
3. 여러 envelope를 native core에 한 번에 적용하는 batch API를 추가하여 FFI crossing과 actor hop을 줄입니다.

---

### P1-5. iOS: browser WKWebView 생성마다 약 350 KB Readability source를 동기 읽고 합성합니다

- **Severity:** Medium
- **Ownership:** iOS
- **영향 영역:** tab/WebView creation, UI thread, memory allocation

**Evidence**

- `maho/ios-shell/Views/WebViewContainer.swift:27` — WebView configuration 생성 중 `installReadabilityUserScript`를 호출합니다.
- `maho/ios-shell/Views/WebViewContainer.swift:84-129` — 코드 주석이 약 350 KB Readability source임을 명시하고, `String(contentsOf:encoding:)`로 bundle file을 읽어 wrapper JavaScript 문자열과 결합한 뒤 `WKUserScript`를 생성합니다. helper 선언은 `:89`입니다.

**문제**

`makeUIView` 경로에서 반복적인 bundle file read와 큰 Swift String 생성이 일어납니다. 탭을 만들 때마다 동일 source를 다시 읽고 wrapper를 재구성할 필요가 없습니다.

**Suggested fix**

1. process-wide lazy static으로 Readability source 또는 완성된 script source를 한 번만 읽어 캐시합니다.
2. 가능하면 build 단계에서 wrapper까지 포함한 하나의 resource로 생성하여 runtime 문자열 interpolation/concatenation을 제거합니다.
3. 실제 reader 기능을 요청하기 전까지 주입이 불필요하다면 on-demand injection으로 전환합니다.

---

### P1-6. iOS: 모든 navigation finish마다 screenshot과 favicon 작업을 시작합니다

- **Severity:** Medium
- **Ownership:** iOS
- **영향 영역:** post-navigation jank, image CPU/memory/network

**Evidence**

- `maho/ios-shell/Views/WebViewCoordinator.swift:147-160` — `didFinish`에서 tab preview screenshot을 시작하고 바로 favicon capture도 호출합니다. screenshot 호출은 `:155`, favicon 호출은 `:159`입니다.
- `maho/ios-shell/Support/ScreenshotHelper.swift:13-31` — `@MainActor` helper가 `takeSnapshot` 후 JPEG 0.8로 인코딩하며 `jpegData` 호출은 `:26`입니다.
- `maho/ios-shell/Views/WebViewCoordinator.swift:281-303` — favicon URL을 JavaScript로 구한 뒤 `URLSession.shared.data(from:)`, `UIImage(data:)`, `pngData()`를 거쳐 core에 전달합니다.

**문제**

페이지 load 완료 직후에는 사용자 입력과 page-side post-load 작업이 집중됩니다. 이때 viewport snapshot 인코딩과 favicon fetch/decode/re-encode를 매 navigation마다 수행하면 CPU/메모리 spike가 겹칠 수 있습니다. favicon이 동일한 origin에서 반복되어도 cache/unchanged check가 코드상 보이지 않습니다.

**Suggested fix**

1. tab-grid preview가 필요할 때 lazy capture하거나 navigation 완료 후 idle/debounce 시점으로 미룹니다.
2. snapshot 크기를 tab-card 목표 크기에 맞게 downscale하고 encoding은 가능한 범위에서 main actor 밖으로 이동합니다.
3. favicon은 URL/origin별 cache와 unchanged URL skip을 적용합니다.
4. `CGImageSourceCreateThumbnailAtIndex` 등으로 decode 단계부터 작은 target size로 downsample하고 byte/dimension 상한을 둡니다.

---

### P1-7. iOS: tool/image delta는 token과 달리 batch 없이 `evaluateJavaScript`를 호출합니다

- **Severity:** Medium
- **Ownership:** iOS
- **영향 영역:** WKWebView IPC, main queue scheduling

**Evidence**

- `maho/ios-shell/Support/WebViewBridgeController.swift:107-114` — `pushToolDelta`가 delta 하나마다 JavaScript string을 만들고 main queue에서 `evaluateJavaScript`를 호출합니다.
- `maho/ios-shell/Support/WebViewBridgeController.swift:115-122` — `pushImageDelta`도 동일하게 delta당 호출합니다.
- `maho/ios-shell/Support/WebViewBridgeController.swift:1529-1595` — 반면 token path의 `StreamSession`은 token을 누적하여 threshold/timeout 기준으로 batch dispatch합니다.

**문제**

이미지나 tool argument가 여러 delta로 나뉘면 각각 WKWebView process boundary와 main queue를 통과합니다. token path에 이미 batching 설계가 있으므로 event 종류에 따라 bridge 효율이 일관되지 않습니다.

**Suggested fix**

1. tool/image delta도 `StreamSession`과 유사한 per-session queue에 넣어 16–60 ms 단위로 batch합니다.
2. JavaScript string injection 대신 가능한 환경에서는 structured reply/message API를 사용해 escaping/serialization 비용을 줄입니다.
3. image binary는 text/base64 chunk 횟수를 줄이는 별도 transport를 우선 검토합니다.

---

### P1-8. Android: foreground/background lifecycle에서 archive/save JNI를 UI thread에서 동기 호출합니다

- **Severity:** Medium
- **Ownership:** Android
- **영향 영역:** resume latency, pause jank, lifecycle stalls

**Evidence**

- `maho/android-shell/app/src/main/java/dev/maho/browser/MainActivity.kt:84-91` — `onResume()`에서 foreground resume helper를 통해 `MahoBridge.autoArchiveConversations()`를 실행합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/MainActivity.kt:93-96` — `onPause()`에서 `MahoBridge.saveState()`를 직접 호출합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt:48-61` — 이 API들은 native core JNI 함수로 바로 전달됩니다.

**문제**

Activity lifecycle callback은 main thread에서 실행됩니다. archive가 DB scan/update를 하거나 saveState가 serialization/fsync를 포함하면 app 전환과 frame scheduling을 막을 수 있습니다.

**Suggested fix**

1. lifecycle callback에서는 lightweight enqueue만 하고 실제 storage 작업은 `lifecycleScope` + `Dispatchers.IO` 또는 core 전용 serial dispatcher에서 수행합니다.
2. pause/save가 process death 안전성 때문에 즉시 durable해야 한다면 incremental state journal을 사용하여 pause 시점의 작업량을 작게 유지합니다.
3. auto-archive는 last-run timestamp를 두어 매 resume마다 실행하지 않고 정책상 필요한 간격으로 제한합니다.

---

### P1-9. Android: AI WebView destroy가 bridge coroutine/session handle을 명시적으로 정리하지 않습니다

- **Severity:** Medium
- **Ownership:** Android
- **영향 영역:** memory footprint, native session lifetime, overlay reopen

**Evidence**

- `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/AiWebViewFactory.kt:21-26` — `AiWebViewHost.destroy()`는 `webView.destroy()`만 호출합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/WebViewBridge.kt:42-45` — bridge가 자체 `CoroutineScope(SupervisorJob() + Dispatchers.Main)`과 agent/chat `HandleRegistry`를 소유합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/WebViewBridge.kt:55-85` — RPC 작업은 이 scope에서 launch되고 JSON parse/dispatch를 진행합니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/HandleRegistry.kt:14-28` — registry는 register/resolve/unregister만 있고 전체 clear/dispose 경로가 없습니다.
- `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/ConversationsWebOverlay.kt:23-31` — overlay dispose 시 host의 `destroy()`만 호출합니다.

**문제**

WebView를 닫을 때 이미 실행 중인 coroutine, pending RPC, registry에 남은 native session handle을 host lifecycle과 함께 취소/해제하는 계약이 없습니다. JavaScript가 모든 session을 정상 종료한다는 가정이 깨지는 navigation/overlay close/error 경로에서는 native resource가 더 오래 유지될 수 있습니다.

**Suggested fix**

1. `WebViewBridge.close()`를 추가하여 scope를 cancel하고 pending request를 종료합니다.
2. registry가 보유한 handle을 snapshot/clear할 수 있게 하고, 각 native agent/chat session free API를 호출합니다.
3. `AiWebViewHost.destroy()`에서 JS interface 제거 → bridge close → load `about:blank`/WebView cleanup → `destroy()` 순서를 명확히 합니다.
4. overlay를 반복 open/close하는 instrumentation test에서 WebView/native handle count가 baseline으로 복귀하는지 검증합니다.

---

## 5. Cross-platform 우선 적용 순서

### Phase 0 — startup과 hot path

1. Android `Application.onCreate()`의 KeyStore/core/state heavy work를 main thread에서 분리합니다.
2. iOS `MahoApp.init()`의 core/state heavy work를 first frame 이후로 이동합니다.
3. web-ai bundle을 route별로 분리하고 작은 bootstrap만 공통으로 남깁니다.
4. chat streaming state를 `liveMessage` + frame-coalesced update 구조로 변경합니다.
5. Android request interception에서 full settings JSON decode를 제거합니다.

### Phase 1 — IPC와 memory churn

1. 50 ms fixed polling을 push/long-poll 또는 adaptive backoff로 교체합니다.
2. streamed image를 base64 concatenation 대신 buffer/blob 기반으로 변경합니다.
3. iOS tool/image delta도 batch합니다.
4. Android AI WebView bridge에 lifecycle-aware `close()`를 추가합니다.

### Phase 2 — navigation/list smoothness

1. iOS sync storage work를 MainActor에서 분리합니다.
2. iOS screenshot/favicon을 lazy/debounce/downsample/cache합니다.
3. Conversations를 paginate/window하고 group/selection 계산을 memoize합니다.
4. iOS Readability script를 process-wide cache 또는 on-demand injection으로 전환합니다.
5. Android auto-archive/save-state를 lifecycle main thread에서 분리합니다.

## 6. 권장 성능 회귀 지표

구현 변경 후에는 다음 지표를 플랫폼별 CI/성능 테스트에 추가하는 것이 좋습니다.

| 플랫폼 | 권장 지표 |
|---|---|
| iOS | cold start → first frame, core open/loadState signpost, navigation → input-ready, sync 중 main-thread stall, tab preview capture CPU/peak memory |
| Android | TTID/TTFD, `Application.onCreate` duration, blocker ON page의 request interception CPU/JNI count, resume/pause callback duration, overlay 반복 open/close 후 WebView/native handle count |
| shared-web-ai | route별 bundle bytes, JS parse/evaluate time, WebView route first paint, token stream frame time/long tasks, 100/500-message render cost, generated-image stream peak heap/GC pause |
| Desktop | `chrome://maho-ai` first open latency, route별 JS parse/evaluate time, long-chat streaming frame time |

## 7. 결론

현재 모바일 성능 리스크는 개별 micro-optimization보다 **초기화 경로의 동기 I/O/FFI**, **shared bundle의 단일화**, **streaming hot path의 반복 전체 배열 처리**, **Android request interception의 per-resource JSON/JNI 비용**에 집중되어 있습니다. 이 네 축을 먼저 수정하면 startup, AI overlay open, long-chat streaming, 일반 페이지 로드에서 가장 큰 개선 가능성이 있습니다.

본 분석은 소스 변경이나 runtime 실행 없이 수행했습니다. 따라서 다음 구현 단계에서는 위 우선순위대로 instrumentation을 추가하여 정적 예상과 실제 wall-clock/CPU/memory 비용을 대조하는 것을 권장합니다.
