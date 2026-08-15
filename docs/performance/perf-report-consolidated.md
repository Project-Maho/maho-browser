# Maho 전 플랫폼 통합 성능 마스터 보고서

> 작성일: 2026-08-15  
> 범위: Desktop(Rust backend, Chromium C++ overlay, WebUI), Mobile(iOS, Android), shared `web-ai`  
> 성격: 기존 성능 보고서 통합 및 교차 시스템 아키텍처 리뷰  
> 변경 범위: 문서만 작성하며 소스 코드는 수정하지 않습니다.

## 1. 요약

Maho의 핵심 성능 문제는 플랫폼별 독립 병목보다 **초기화·스토리지·네이티브/웹 경계·공유 JavaScript가 하나의 critical path로 연결되는 구조**에 있습니다.

가장 중요한 결론은 다음과 같습니다.

1. **Desktop P0:** Chromium browser UI thread의 `PostBrowserStart()`가 `maho_desktop_core_initialize()`를 동기 호출하므로 Rust의 profile/storage/DB/SQLCipher/schema/migration 비용이 browser cold start와 UI 응답성으로 직접 전파될 수 있습니다. 해결은 단순 thread 이동이 아니라 최소 availability와 feature readiness를 분리하는 상태 모델이어야 합니다.
2. **Mobile P0:** iOS/Android launch에서 asset stat/hash/copy/provisioning 같은 비-UI I/O가 main thread에 남아 있으면 first frame과 WebView/WKWebView 부팅이 늦어집니다. 이 비용 뒤에는 shared `web-ai` load/parse/evaluate/render가 직렬로 이어지므로 네이티브와 Web startup을 함께 최적화해야 합니다.
3. **공통 P0:** shared `web-ai`는 iOS·Android뿐 아니라 Desktop `chrome://maho-ai`에도 사용됩니다. 초기 shell에 불필요한 feature를 route/feature 단위 dynamic import로 분리하면 세 플랫폼의 초기 JS 비용을 동시에 줄일 수 있습니다. 다만 mobile local asset 환경의 chunk version/offline/path 일관성을 함께 설계해야 합니다.
4. **Rust P1:** mail sync의 높은 concurrency는 SQLite single-writer contention을 늘려 background throughput과 foreground latency를 동시에 악화시킬 수 있습니다. bounded concurrency, fetch/commit 분리, transaction batching이 우선 후보입니다.
5. **Desktop WebUI P1:** `maho_settings`는 C++ notification debounce와 renderer의 `requestAnimationFrame` coalescing을 이미 사용합니다. 따라서 추가 throttling보다 변경 때마다 broad/full settings snapshot을 재구성·전송하는 프로토콜을 initial snapshot + delta/revision으로 바꾸는 편이 더 구조적인 개선입니다.
6. 최적화 순서는 **공통 측정 → startup critical path 절단 → shared bundle 축소 → DB/IPC/render dataflow 개선 → 회귀 방지 자동화**를 권장합니다.

---

## 2. 검토 범위와 근거

### 2.1 기존 보고서

`docs/performance/`에는 통합 시점 기준 다음 두 보고서가 존재했습니다.

- `docs/performance/perf-report-rust-backend.md`
- `docs/performance/perf-report-mobile.md`

Rust 보고서는 `maho-rust-core`, Desktop bridge, Chromium startup 연결, Tokio/SQLite/SQLCipher/mail/cache/search를 분석합니다. Mobile 보고서는 `maho-ios`, `maho-android`, `maho-shared/web-ai`를 분석하며 shared `web-ai`가 Desktop에서도 사용됨을 포함합니다.

### 2.2 Chromium/WebUI 보완 검토

전용 Chromium C++/WebUI 보고서는 없었으므로 다음 소스를 읽기 전용으로 표본 검증했습니다.

- `maho-chromium/browser/maho_browser_main_extra_parts.cc`
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- `maho-chromium/browser/resources/maho_settings/react/store.ts`
- `maho-chromium/browser/resources/maho_settings/react/app.tsx`
- `maho-chromium/browser/resources/*/bundle_react.mjs`

확인된 사항은 다음과 같습니다.

- `PostBrowserStart()` 경로와 Desktop Rust Core 초기화가 직접 연결됩니다.
- settings C++ handler는 notification을 coalesce하지만 flush 시 broad settings snapshot을 다시 생성해 전달합니다.
- renderer store는 rapid Mojo push를 RAF 단위로 coalesce합니다.
- 여러 React WebUI가 surface별 single-entry/single-output esbuild 패턴을 사용하며, 확인한 build script에는 명시적 `splitting: true`가 없습니다.

Desktop WebUI의 실제 bundle bytes·IPC bytes·React commit time은 이번 문서 통합에서 정량 측정하지 않았으므로 관련 항목은 measurement gate를 둡니다.

### 2.3 근거 등급

| 등급 | 의미 | 예시 |
|---|---|---|
| A | 기존 보고서와 소스 흐름에서 직접 확인된 구조 | Chromium UI startup → sync Rust init, mobile launch asset work |
| B | 소스상 비용 가능성이 높고 profiler 검증이 필요한 구조 | broad settings snapshot, 단일 WebUI bundle의 실제 비용 |
| C | 일반적인 최적화 후보이나 현재 Maho 근거가 약함 | 무차별 memoization, 소규모 allocation 제거 |

---

## 3. 전 플랫폼 critical path

### 3.1 Desktop startup

`Chromium startup → BrowserMainExtraParts → PostBrowserStart() → Rust FFI init → profile/storage/DB readiness → browser/service readiness → UI usable`

Rust 내부 cold I/O가 Rust만의 문제가 아니라 browser-process startup 지연으로 확대되는 경로입니다.

### 3.2 Mobile cold start

`process launch → native main-thread init → asset check/copy → WKWebView/WebView → local document → web-ai JS load/parse/eval → React render → first interaction ready`

앞 단계만 개선하면 다음 단계가 새 병목으로 나타날 수 있으므로 end-to-end funnel로 측정해야 합니다.

### 3.3 Runtime dataflow

`network/mail sync → async concurrency → SQLite/SQLCipher → cache/search → FFI/Mojo/native-web bridge → frontend store → render/layout/paint`

높은 concurrency, DB lock, broad payload, broad frontend invalidation이 결합되면 작은 비용이 계층마다 증폭될 수 있습니다.

---

## 4. 플랫폼별 종합 리뷰

### 4.1 Desktop Rust backend

#### P0 — UI-thread 동기 Core 초기화

`PostBrowserStart()`에서 Rust Core initialization이 동기 호출되므로 storage/profile/DB open, SQLCipher, schema/migration 성격의 작업이 cold path에 들어오면 browser startup이 함께 늦어집니다.

**권장안**

- 최소 `availability`와 실제 `feature readiness`를 분리합니다.
- `Uninitialized / Initializing / Ready / Failed / ShuttingDown` 성격의 명시적 상태를 둡니다.
- readiness 이전 호출의 queue/fail/fallback 동작을 API별로 정의합니다.
- initialization cancellation과 shutdown ordering을 함께 설계합니다.

**리스크:** 기존 호출자가 "initialize 반환 = 모든 기능 준비"를 가정했을 수 있어 race가 생길 수 있으며, migration 실패가 늦게 드러날 수 있습니다. 단순 background thread 이동만으로 처리하면 안 됩니다.

#### P1 — Mail sync concurrency와 SQLite writer contention

네트워크 fetch 병렬화는 유리하지만 SQLite write는 직렬화되는 구간이 강합니다. 과도한 account/folder worker는 lock queue, transaction tail latency, Tokio pressure, foreground query 경쟁을 증가시킬 수 있습니다.

**권장안:** bounded concurrency, network fetch와 DB commit 단계 분리, batch transaction, foreground/background QoS를 검토합니다. 적정 worker 수는 benchmark로 결정해야 합니다.

#### P1 — Connection/transaction lifecycle

encrypted DB connection을 짧은 operation마다 새로 열면 setup/pragma/cipher/mutex 비용이 반복될 수 있습니다.

**권장안:** connection reuse 또는 작은 application-owned pool을 실험하되 read/write workload를 구분하고 transaction duration과 lock wait을 함께 측정합니다. pool을 크게 만드는 것은 SQLite write contention을 악화시킬 수 있습니다.

#### P1 — Cache/search query amplification

대규모 mailbox에서 `OFFSET` pagination과 반복 `COUNT`는 tail latency를 키울 수 있습니다.

**권장안:** stable sort key + unique ID의 keyset/seek pagination, 실제 `EXPLAIN QUERY PLAN` 기반 index, 불필요한 exact count 회피를 검토합니다.

#### P2 — FFI allocation/copy와 blocking work

많은 소형 FFI 호출과 JSON/string/`Vec` 변환은 hot path에서 heap churn을 만들 수 있습니다. blocking disk/SQLite가 async worker를 점유할 가능성도 있습니다.

**권장안:** profile로 확인된 hot path만 batch API로 바꾸고, 실제 blocking workload만 적절한 blocking pool로 격리합니다.

### 4.2 Desktop Chromium C++ overlay

Rust startup 비용은 C++ lifecycle에서 호출되므로 Rust 팀만의 문제가 아닙니다. 다음 contract를 Chromium/Rust 공동으로 정의해야 합니다.

- first window 이전에 반드시 필요한 최소 Core 상태
- 각 browser service의 readiness 구독 방식
- 초기화 실패와 retry/fallback
- profile destruction 및 shutdown ordering

`PostBrowserStart()` 주변의 다른 bootstrap 작업도 trace해야 하지만, 이번 검토에서 wall time이 측정되지 않은 항목은 즉시 병목으로 단정하지 않습니다.

### 4.3 Desktop WebUI / Mojo

#### P1 — `maho_settings` full snapshot protocol

C++ debounce와 renderer RAF coalescing이 이미 있어 burst frequency는 어느 정도 제어됩니다. 그러나 각 flush에서 broad snapshot을 재구성·serialize·IPC 전송하고 renderer state를 크게 갱신한다면 비용은 남습니다.

**권장 구조**

- 초기 hydrate: full snapshot 1회
- 일반 변경: typed delta `{key, value, revision}` 성격
- revision mismatch: full resync
- 고빈도 상태: 필요 시 narrow channel

**리스크:** multi-pref atomicity, out-of-order/missed event, C++/Mojo/TS 동시 변경이 필요합니다. 이미 coalescing이 있으므로 IPC bytes와 render profile로 개선 폭을 검증해야 합니다.

#### P2 — React WebUI code splitting

여러 `bundle_react.mjs`가 single-entry/single-output 패턴을 사용하지만 생성 bundle의 실제 비용은 아직 측정되지 않았습니다.

**권장안:** esbuild metafile, compressed/uncompressed size, DevTools Coverage, parse/evaluate trace를 먼저 수집하고 실제 heavy route/pane만 dynamic import합니다. shared `web-ai`보다 근거가 약하므로 P2로 둡니다.

### 4.4 iOS shell

#### P0 — Main-thread cold-start I/O

WKWebView/UIKit object는 main thread 제약이 있지만 asset copy, file stat/hash/version scan, preprocessing은 그렇지 않습니다.

**권장안**

- build/asset manifest stamp로 unchanged launch fast path를 만듭니다.
- 실제 staging/hash/copy는 background queue에서 수행합니다.
- versioned directory에 새 asset set을 준비한 뒤 verify 후 atomic switch합니다.
- 이전 valid generation을 fallback으로 유지합니다.

**리스크:** partial asset, stale chunk, update 도중 종료, deferred work가 first interaction으로 이동하는 문제입니다.

#### P1 — WKWebView bridge

반복 `evaluateJavaScript`와 많은 작은 native↔JS message는 serialization/callback 비용을 만듭니다. 의미상 함께 처리 가능한 update를 batch하고 polling보다 event-driven push를 사용하되, streaming 체감 latency를 해치지 않도록 frame/짧은 time window 기준으로 조정합니다.

### 4.5 Android shell

#### P0 — Asset provisioning + WebView boot

launch마다 asset directory를 광범위하게 stat/hash/copy하면 main-thread I/O 뒤에 WebView boot와 JS 초기화가 직렬로 누적됩니다.

**권장안:** iOS와 동일한 manifest/version contract, no-change fast path, background staging, atomic activation을 사용합니다. WebView 인스턴스 자체는 플랫폼 thread constraint를 지켜야 합니다.

#### P1/P2 — WebView bridge와 Compose host state

bridge message batching/coalescing, `AndroidView` 주변의 좁고 안정적인 state, Macrobenchmark/Perfetto/FrameTiming, 필요 시 Baseline Profile을 권장합니다. bridge contract를 바꿀 때는 `addJavascriptInterface` 등 security/compatibility도 함께 검토합니다.

### 4.6 Shared `web-ai`

#### P0 — Initial module graph / code splitting

shared `web-ai`는 모바일과 Desktop AI가 같은 초기 JS 비용을 공유하는 공통 레버리지 지점입니다.

**권장안**

- initial shell에는 navigation, minimal store, core bridge, 현재 대화 최소 renderer만 유지합니다.
- image, memory, web search, writing, history/settings 등 heavy/infrequent feature는 dynamic import합니다.
- 실제 사용 가능성이 높은 chunk만 idle prefetch합니다.
- mobile asset manifest가 chunk graph/version을 함께 관리하도록 합니다.

**트레이드오프:** initial parse/eval과 heap은 줄지만 첫 feature 진입 latency, chunk file 수, local URL resolution, stale cache, offline failure 위험이 커집니다. 따라서 code splitting과 mobile asset versioning은 하나의 프로그램으로 추진해야 합니다.

#### P1 — Store/render fan-out

넓은 global subscription은 작은 state update를 큰 tree reconciliation로 확장할 수 있습니다. selector를 좁히고 React Profiler에서 commit cost가 큰 subtree부터 memoization/partitioning을 적용합니다.

#### P1 — Streaming update cadence

토큰 단위 또는 지나치게 작은 delta마다 render하면 frame budget을 침범할 수 있습니다. network receive와 UI commit cadence를 분리하고 `requestAnimationFrame` 또는 짧은 buffer window로 합칩니다. final status/event는 별도 우선순위로 즉시 반영할 수 있습니다.

#### P1 — Long conversation/history

긴 세션에서는 DOM, rich content, media가 누적됩니다. virtualization/windowing, offscreen lazy mount, stable key, history pagination, media lifecycle 관리가 필요합니다.

#### P2 — Persistence/serialization

큰 state를 자주 JSON serialize하는 경로가 profile로 확인되면 write batching/debounce와 structured storage를 검토합니다.

---

## 5. 시스템 간 연계 영향

### 5.1 Rust Core → Chromium UI thread

Rust DB/profile init의 지연은 Rust subsystem 내부에 머물지 않고 startup hook 반환을 늦춰 first visible/interactive milestone에 전파될 수 있습니다. 따라서 이 작업의 owner는 Rust Core와 Chromium lifecycle의 공동 owner여야 합니다.

### 5.2 Mobile native I/O → WebView → JS parse

asset I/O를 제거하면 WebView boot가, WebView를 개선하면 `web-ai` parse/eval이 다음 dominant bottleneck이 될 수 있습니다. 각 단계 숫자보다 `process launch → first frame → document commit → web shell render → first interaction ready` 전체 funnel을 사용해야 합니다.

### 5.3 Code splitting ↔ Mobile asset provisioning

Web runtime 관점에서는 initial bundle을 분리하는 것이 유리하지만 file 수 증가는 asset stat/hash/copy 비용을 늘릴 수 있습니다. 해결은 splitting 포기가 아니라 manifest digest가 동일하면 file walk를 생략하는 differential activation입니다.

### 5.4 Async concurrency ↔ SQLite foreground latency

worker 증가가 pending writes와 lock queue를 키우면 background sync가 foreground search/cache latency를 악화시킬 수 있습니다. 최적 concurrency는 CPU core 수보다 network latency, DB service time, foreground QoS를 기준으로 정해야 합니다.

### 5.5 FFI/Mojo payload ↔ React invalidation

backend broad object reconstruction → serialization/IPC → frontend object update → store invalidation → render가 한 update에 연결됩니다. `maho_settings`는 rate coalescing이 이미 있으므로 다음 최적화 축은 **payload와 subscription granularity**입니다.

### 5.6 Deferral ↔ Readiness correctness

동기 초기화를 비동기로 바꾸면 "반환 시 invariant 보장"이라는 단순한 contract를 잃습니다. readiness owner, early call behavior, failure/retry, shutdown cancellation을 명문화하지 않으면 startup 숫자를 위해 correctness를 희생하게 됩니다.

---

## 6. 전사 통합 우선순위 매트릭스

| Priority | 항목 | 플랫폼 | Impact | Effort | Risk | 근거 |
|---|---|---|---|---|---|---|
| **P0** | UI-thread sync Rust Core init 분리 + readiness | Desktop Rust/Chromium | 매우 높음 | 중~높음 | 높음 | A |
| **P0** | launch asset stat/hash/copy main-thread 제거 + manifest fast path | iOS/Android | 매우 높음 | 중간 | 중~높음 | A |
| **P0** | shared `web-ai` minimal shell + feature splitting | iOS/Android/Desktop AI | 매우 높음 | 중~높음 | 중~높음 | A/B |
| **P1** | mail bounded concurrency + fetch/commit 분리 + transaction batching | Rust | 높음 | 중간 | 중간 | A |
| **P1** | SQLite reuse/small pool + transaction tuning | Rust | 높음 | 중~높음 | 중~높음 | A/B |
| **P1** | keyset pagination/query/index 개선 | Rust data | 높음 | 중간 | 중간 | A/B |
| **P1** | native↔web bridge batching/coalescing | iOS/Android | 중~높음 | 중간 | 중간 | A/B |
| **P1** | settings initial snapshot + delta/revision | Chromium/WebUI | 중~높음 | 중간 | 중간 | B |
| **P1** | web-ai selector/streaming/list 최적화 | 공통 Web AI | 높음 | 중간 | 중간 | A/B |
| **P2** | Desktop 개별 WebUI route/pane splitting | Desktop WebUI | 중간 | 중간 | 낮~중 | B |
| **P2** | traced blocking Rust work 격리 | Rust | 중간 | 낮~중 | 중간 | B |
| **P2** | FFI hot-path batching/copy 축소 | Desktop bridge | 중간 | 중~높음 | 중간 | B |
| **P2** | Android Baseline Profile/Compose 안정화 | Android | 중간 | 낮~중 | 낮 | B |
| **P2** | large JS persistence batching | Shared Web | 중간 | 중간 | 중간 | B/C |
| **P3** | profile 근거 없는 micro-allocation/memoization | 전 플랫폼 | 낮음 | 낮~중 | 낮 | C |
| **P3** | 성능 효과가 입증되지 않은 stylistic cleanup | 전 플랫폼 | 낮음 | 낮음 | 낮 | C |

---

## 7. 핵심 수정안의 실현 가능성과 리스크

| 수정안 | 실현 가능성 | 핵심 이득 | 주요 트레이드오프/리스크 | 안전 장치 |
|---|---|---|---|---|
| Rust async readiness | 중~높음 | Desktop cold-start critical path 절단 | readiness race, failure/shutdown ordering | state machine, cancellation, integration test |
| Mobile manifest fast path | 높음 | unchanged launch file walk 제거 | stale/partial asset | versioned staging, verify, atomic switch, rollback |
| web-ai code splitting | 높음 | 3플랫폼 initial JS/heap 감소 | first-use latency, chunk/path/cache 문제 | manifest, offline test, selective prefetch |
| Mail bounded concurrency | 높음 | DB lock/scheduler pressure 감소 | sync throughput을 너무 낮출 수 있음 | workload별 benchmark |
| Transaction batching | 높음 | commit/lock 횟수 감소 | 큰 transaction은 lock hold 증가 | batch-size sweep, transaction p95 |
| SQLite reuse/pool | 중간 | setup/cipher 반복 감소 | writer contention 악화 가능 | 작은 pool, read/write 분리 |
| Settings delta protocol | 중~높음 | IPC/state invalidation 감소 | missed/out-of-order event | revision + full resync |
| Bridge batching | 높음 | call/serialization 수 감소 | ordering/latency | message class 분리, short window |
| Streaming batching | 높음 | jank/commit 빈도 감소 | 체감 streaming 지연 | frame-aware buffer, latency metric |
| Long-list virtualization | 중간 | DOM/heap/scroll 개선 | dynamic height/focus/accessibility | UX 회귀 테스트 |
| Desktop WebUI splitting | 중간 | heavy surface 초기 비용 감소 | first-use/chunk 관리 | profile 후 대상 한정 |
| FFI batching | 중간 | cross-language overhead 감소 | API churn | hot path만 적용 |

---

## 8. 단계별 최적화 로드맵

### Phase 0 — Baseline과 관측성

**Desktop**
- Chromium trace에서 `PostBrowserStart()`, Rust init, 각 Maho service bootstrap, first window visible/interactive를 분리합니다.
- Rust `tracing`으로 storage/DB/migration span을 연결합니다.

**Rust/DB**
- DB open, query/transaction p50/p95, lock/wait proxy, rows scanned, `EXPLAIN QUERY PLAN`, worker concurrency/queue depth를 수집합니다.

**iOS**
- `os_signpost`/Instruments로 process launch → first frame → asset ready → WKWebView document → web shell ready를 연결합니다.

**Android**
- Macrobenchmark `StartupTimingMetric`, Perfetto, FrameTimingMetric으로 동일 funnel을 측정합니다.

**Shared Web**
- bundle metafile/bytes/chunk count, DevTools Coverage, parse/evaluate long task, React commit, DOM/heap을 측정합니다.

**Exit gate:** cold/warm p50/p95를 재현할 수 있고 startup waterfall에서 각 계층 시간이 분리되어야 합니다.

### Phase 1 — Startup critical path 절단

1. Desktop Rust readiness state machine과 minimal sync init을 도입합니다.
2. iOS/Android asset manifest fast path, background staging, atomic activation을 적용합니다.
3. shared `web-ai`에서 독립 heavy feature 하나로 dynamic chunk pilot을 수행하고 Desktop/iOS/Android offline/update/rollback을 검증한 뒤 확대합니다.

**Exit gate:** end-to-end cold-start가 개선되고 readiness race, broken chunk, stale/partial asset이 없어야 하며, 비용을 first interaction 이후로 단순 이동시키지 않았음을 확인해야 합니다.

### Phase 2 — Storage/IPC/renderer dataflow

- mail bounded concurrency, fetch/commit 분리, transaction batching을 적용·튜닝합니다.
- connection reuse/small pool과 keyset pagination을 representative dataset에서 검증합니다.
- settings payload/frequency를 측정한 뒤 snapshot + delta/revision protocol을 pilot합니다.
- mobile bridge에서 ordering-safe event만 frame/짧은 window 기준으로 batch합니다.
- web-ai selector 축소, streaming cadence 조절, long-list virtualization을 적용합니다.

**Exit gate:** background sync 중 foreground p95가 악화되지 않고, IPC bytes/render commit과 long task가 실제로 감소해야 합니다.

### Phase 3 — Profile-guided 확장과 회귀 방지

- 실제 heavy하다고 확인된 Desktop settings/mail/welcome 등 surface에만 route/pane splitting을 확대합니다.
- hot FFI call만 batch API로 합치고 traced blocking work만 blocking pool로 격리합니다.
- Android Baseline Profile 등 release 최적화를 적용합니다.
- CI/release에 startup benchmark, DB dataset benchmark, web-ai bundle budget, long-task/frame metric, asset integrity, readiness/shutdown race test를 추가합니다.

**Exit gate:** 각 budget이 안정된 baseline 분포를 근거로 정의되고 PR/release에서 regression을 탐지할 수 있어야 합니다.

---

## 9. 공통 KPI

### Desktop startup
- process start → first window visible
- process start → first interaction ready
- `PostBrowserStart()` wall time
- sync Rust init wall time
- Rust Core ready timestamp
- DB open/migration wall time

### Mobile startup
- cold/warm startup p50/p95
- launch → first native frame
- asset provisioning wall time 및 main-thread portion
- WebView/WKWebView create → document commit
- document commit → web shell render
- web shell → first interaction ready

### Shared Web
- initial JS bytes/chunk count
- parse/evaluate duration
- long-task frequency/duration
- React commit duration
- streaming commit frequency
- long conversation frame time
- DOM/heap trend

### Backend/DB
- query/transaction p50/p95/p99
- lock/wait proxy
- rows scanned/returned
- mail sync throughput
- background sync 중 foreground query latency
- active/pending worker count

### IPC/bridge
- messages/sec, bytes/sec
- average/max payload
- coalesced redundant updates
- state change → visible UI latency

---

## 10. 리스크 레지스터

| Risk | 관련 변경 | 심각도 | 완화책 |
|---|---|---|---|
| Core readiness race | Desktop async init | 높음 | explicit state, consumer audit, integration test |
| shutdown callback/use-after-free | Desktop async init | 높음 | cancellation ownership, generation/weak handle |
| migration failure 지연 | deferred init | 높음 | failed state, telemetry, recovery policy |
| SQLite contention 악화 | pool/concurrency 변경 | 높음 | write 제한, lock/transaction profile |
| sync throughput 저하 | concurrency 제한 | 중간 | workload별 benchmark |
| stale/partial asset | mobile manifest | 높음 | versioned staging, atomic activation, fallback |
| dynamic chunk load failure | web-ai split | 높음 | manifest integrity, offline/local URL test |
| first-use latency 증가 | code splitting | 중간 | selective preload/prefetch |
| settings state mismatch | delta protocol | 높음 | revision, resync, full snapshot fallback |
| bridge ordering 오류 | batching | 중~높음 | message class/sequence 분리 |
| streaming 체감 지연 | render batching | 중간 | frame-aware small buffer |
| virtualization UX 회귀 | long list | 중간 | focus/selection/accessibility test |
| 비용의 단순 지연 이동 | 모든 deferral | 중~높음 | end-to-end milestone 검증 |

---

## 11. 피해야 할 접근

- **Rust init 전체를 무조건 background thread로 옮기기:** readiness와 shutdown contract가 없으면 correctness race를 만듭니다.
- **SQLite connection 수를 무조건 늘리기:** single-writer contention 때문에 역효과가 날 수 있습니다.
- **모든 React component에 `memo` 추가하기:** 실제 subscription/prop 문제를 가릴 수 있습니다.
- **JS를 최대한 잘게 chunking하기:** file count, local asset activation, first-use latency가 악화될 수 있습니다.
- **`maho_settings`에 debounce만 추가하기:** 이미 C++ debounce와 RAF coalescing이 있으므로 payload granularity가 다음 후보입니다.
- **microbenchmark만으로 완료 판단하기:** end-to-end first interaction과 foreground latency를 함께 봐야 합니다.

---

## 12. 권장 구현 순서와 Ownership

먼저 공통 trace와 readiness/asset contract를 설계한 뒤 다음 작업을 병렬화하는 편이 좋습니다.

| Workstream | 주 소유 영역 | 공동 검토 영역 |
|---|---|---|
| Desktop Startup | Chromium C++ + Rust Core | profile/storage, browser lifecycle |
| Rust Storage/Sync | Rust backend | mail UX/foreground priority |
| Mobile Startup | iOS + Android shell | shared web asset pipeline |
| Shared Web AI | `maho-shared/web-ai` | Desktop Chromium, iOS, Android |
| Desktop WebUI Dataflow | Chromium Mojo + WebUI TS | preference/state owners |
| Performance Guardrail | 공통 infra/release | 각 플랫폼 owner |

권장 순서는 다음과 같습니다.

1. 공통 trace/benchmark baseline 고정
2. Desktop sync Rust init을 readiness 기반으로 분리
3. iOS/Android asset fast path + background staging
4. shared `web-ai` minimal shell + feature chunks, mobile manifest와 동시 배포
5. mail sync concurrency + SQLite transaction 최적화
6. settings Mojo snapshot + delta/revision 전환
7. native/web bridge와 streaming update cadence coalescing
8. long conversation/history virtualization 및 selector granularity 개선
9. 실제 profile에서 비용이 확인된 Desktop WebUI/FFI P2 항목만 확대
10. startup/bundle/DB/frame regression을 CI/release guardrail로 고정

---

## 13. 결론

Maho 전 플랫폼에서 가장 큰 ROI는 micro-tuning보다 **공통 critical path를 끊는 아키텍처 변경**에 있습니다.

Desktop에서는 Chromium UI thread와 Rust Core initialization의 결합을 풀고, Mobile에서는 launch asset provisioning을 main thread에서 제거해야 합니다. 그 위에 세 플랫폼이 공유하는 `web-ai` initial module graph를 줄이면 Desktop/iOS/Android 모두에서 공통 이득을 얻을 수 있습니다.

다음 단계에서는 concurrency 자체를 높이기보다 SQLite의 serialization 특성에 맞춰 mail sync와 transaction을 제어하고, broad FFI/Mojo/state update를 더 좁은 dataflow로 전환하는 것이 중요합니다. 특히 `maho_settings`처럼 이미 debounce/RAF coalescing이 존재하는 곳에서는 같은 방어책을 중복하기보다 payload와 protocol을 개선해야 합니다.

따라서 **Phase 0 관측성 → P0 startup/bundle → P1 storage/dataflow → P2 profile-guided 확장 → CI guardrail** 순서를 최종 권고합니다.

---

## Appendix A. 기존 보고서 매핑

| 기존 보고서 | 주요 영역 | 통합 반영 |
|---|---|---|
| `perf-report-rust-backend.md` | sync Rust init / Chromium startup | 4.1, 4.2, 5.1, P0 |
| `perf-report-rust-backend.md` | mail concurrency / SQLite | 4.1, 5.4, P1 |
| `perf-report-rust-backend.md` | connection/query/FFI/blocking | 4.1, P1/P2, Phase 2/3 |
| `perf-report-mobile.md` | iOS/Android cold start | 4.4, 4.5, P0 |
| `perf-report-mobile.md` | native↔JS bridge | 4.4, 4.5, P1 |
| `perf-report-mobile.md` | shared web-ai module graph | 4.6, 5.3, P0 |
| `perf-report-mobile.md` | store/streaming/list/persistence | 4.6, P1/P2 |

## Appendix B. 추가 Desktop 읽기 전용 검토

| 소스 | 확인 사항 | 판단 |
|---|---|---|
| `maho_browser_main_extra_parts.cc` | `PostBrowserStart()`와 Rust init 연결 | Desktop startup P0 유지 |
| `maho_settings_page_handler.cc` | notification coalescing + broad snapshot 경로 | snapshot→delta P1 후보 |
| `maho_settings/react/store.ts` | renderer RAF coalescing 존재 | 중복 throttling 권고 회피 |
| `maho_settings/react/app.tsx` | settings 초기 module graph 확인 | bundle profiling 필요 |
| `browser/resources/*/bundle_react.mjs` | single-entry/single-output, 명시적 splitting 부재 | 측정 전 P2로 제한 |

이 보완 검토는 정량 profiler를 대체하지 않습니다. Desktop WebUI bundle size, IPC bytes, React commit time은 Phase 0에서 계측한 뒤 우선순위를 재조정해야 합니다.
