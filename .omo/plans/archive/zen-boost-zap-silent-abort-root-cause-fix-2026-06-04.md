# Zen Boost — Zap Click Silent Abort: Root Cause + Definitive Fix

**Date**: 2026-06-04
**Author**: Sisyphus (Oracle + Librarian + Explore 종합)
**Status**: Ready for execution
**Predecessors**:
- `.sisyphus/plans/zen-boost-zap-full-parity-2026-06-03.md`
- `.sisyphus/plans/zen-boost-magic-theme-workspace-wiring-2026-06-01.md`

**Trigger**: 사용자가 Boost editor에서 Zap 버튼을 눌러도 무반응. 4번의 build cycle 후에도 silent abort 지속. 사용자 명시적 요구로 Oracle/Librarian/Explore 3개 에이전트 종합 분석.

---

## §1 Executive Summary

**Race condition이 아니다. 결정론적 source-order 버그다.**

`MahoBoostUI` 생성자가 `MahoBoostWindowView` 생성자 *안에서 동기적으로* 실행되는데, `WebContentsUserData` attach 코드는 `MahoBoostWindowView` 생성자가 끝난 *다음 줄*에 있다. 어떤 시점/위치 조정으로도 해결 불가. **WebUIController가 LoadInitialURL 호출 stack 안에서 sync 생성**되기 때문.

**Fix**: `MahoBoostWindowView` 생성자 *안에서*, `LoadInitialURL` 호출 *전에* UserData attach. (코드 ~30줄, source-order로 race-free 보장.)

---

## §2 Empirical Evidence

### Trace log (사용자 latest run, 2026-06-04 19:57:57)

```
19:57:57.850  WARNING:maho_boost_ui.cc:66 [MahoBoost-trace] MahoBoostUI ctor: controller INVALID
19:57:57.859  WARNING:maho_boost_window_controller.cc:269 [MahoBoost-trace] Editor opened for domain=example.com
19:57:58.828  WARNING:maho_boost_page_handler.cc:704 [MahoBoost-trace] PageHandler::EnterZapMode boost_id=65c387c1-8ddc-...
19:57:58.828  WARNING:maho_boost_window_controller.cc:197 [MahoBoost-trace] GetActiveTabForBoost: controller WeakPtr invalid
19:57:58.828  WARNING:maho_boost_page_handler.cc:707 [MahoBoost-trace] EnterZapMode: no active_wc, aborting
[5 more identical EnterZapMode failures from repeated user clicks]
```

### 해석

- `MahoBoostUI ctor: controller INVALID` (line 66 of maho_boost_ui.cc)이 `Editor opened for domain=` (line 269 of maho_boost_window_controller.cc)보다 **9 ms 먼저** 발생.
- `Editor opened`는 `ShowForActiveDomain` 마지막 줄. 즉 `CreateAndShowWidget` 완료 후.
- `MahoBoostUI` ctor는 `CreateAndShowWidget` 안의 어떤 sub-step에서 sync로 실행됨.
- 가능한 sub-step은 `new MahoBoostWindowView(...)` (line 441) — `LoadInitialURL`이 그 ctor body에 있음.
- 9 ms 차이는 thread race가 아니라 단일 스택 안 sequential call의 자연 진행.

---

## §3 Architectural Root Cause

### 실제 실행 순서 (단일 UI thread, 단일 call frame)

```
ShowForActiveDomain
└─ CreateAndShowWidget(domain)
   ├─ [line 441]  new MahoBoostWindowView(profile, url, ...)
   │   └─ ctor body:
   │       ├─ create views::WebView
   │       ├─ web_view->LoadInitialURL(chrome-untrusted://maho-boost-editor/)
   │       │   └─ NavigationController::LoadURL
   │       │       └─ NavigationRequest::CreateWebUIIfNeeded
   │       │           └─ WebUIControllerFactoryRegistry::CreateWebUIControllerForURL
   │       │               └─ MahoBoostUIConfig::CreateWebUIController
   │       │                   └─ new MahoBoostUI(web_ui)
   │       │                       └─ FromWebContents(web_ui->GetWebContents())
   │       │                           ❌ UserData 아직 attach 안 됨 → null
   │       │                           controller_ = empty WeakPtr
   │       │                           LOG "controller INVALID"
   │       └─ web_view_ = AddChildView(...)
   │
   ├─ [line 446]  delegate->GetWebViewWebContents()  ← UserData attach 시도
   │   └─ MahoBoostControllerWebContentsData::CreateForWebContents()
   │       └─ data->controller = weak_factory_.GetWeakPtr()
   │   ❌ TOO LATE — MahoBoostUI 이미 빈 WeakPtr 들고 생성 끝남
   │
   ├─ [line 467]  widget->Init(params)
   ├─ widget->Show() / Activate()
   └─ [line 269]  LOG "Editor opened for domain=..."
```

### 결과 chain
1. `MahoBoostUI::controller_` = empty WeakPtr (영구)
2. `MahoBoostUI::CreatePageHandler`가 빈 WeakPtr을 `MahoBoostPageHandler::controller_`에 복사
3. 사용자 Zap 클릭 → JS → Mojo → `MahoBoostPageHandler::EnterZapMode`
4. `GetActiveTab()` → `GetActiveTabForBoost(controller_)` → `if (!controller)` → null
5. Silent abort, callback.Run(), JS 측은 성공 응답 받음

---

## §4 시도한 fix들이 왜 실패했나

| Fix | 시도 | 실패 원인 |
|-----|------|----------|
| **#1** | `GetPendingControllerStorage = WeakPtr` 셋팅을 `widget->Init` 앞으로 | `LoadInitialURL`은 `widget->Init`보다 더 앞 (`new MahoBoostWindowView` 생성자 안). Init 전에 둬도 WebUI ctor보다 늦음 |
| **#2** | Storage 할당을 `new MahoBoostWindowView` 직전으로 | `static base::NoDestructor<base::WeakPtr>` slot이 default-construct = empty. 게다가 `ConsumePendingControllerForUI`의 `std::move + reset` 패턴은 single-shot, 재진입 불가 |
| **#3** | `WebContentsUserData` 마이그레이션 (현재 코드) | **자료구조는 정답 (per-WebContents keying = race-proof). 위치 틀림.** attach가 line 446에 있는데 WebUI ctor는 line 441 안에서 이미 실행 |

세 fix 모두 *시간 기반*으로 해결 시도했으나, 사실은 **stack frame source-order** 문제.

---

## §5 Definitive Fix (Option A)

### 핵심 아이디어

- 자료구조 (`WebContentsUserData`) **유지**
- attach 위치를 `MahoBoostWindowView` 생성자 **안**으로 이동
- `web_view->GetWebContents()` 명시 호출 → upstream Chromium `views::WebView::GetWebContents()`가 lazy-create (navigation 시작 안 함)
- 그 WebContents에 UserData attach
- **그 다음** `web_view->LoadInitialURL(url)` — navigation 시작, WebUI ctor가 attach된 UserData 읽음

### 변경 사항

#### 5.1 `MahoBoostWindowView` 생성자 시그니처 변경

**Before**:
```cpp
MahoBoostWindowView(Profile* profile,
                    const GURL& url,
                    int initial_width,
                    int initial_height,
                    WindowClosingCallback on_closing)
```

**After**:
```cpp
MahoBoostWindowView(Profile* profile,
                    const GURL& url,
                    int initial_width,
                    int initial_height,
                    WindowClosingCallback on_closing,
                    base::WeakPtr<MahoBoostWindowController> controller_weak)
```

#### 5.2 `MahoBoostWindowView` ctor body 재구성

**Before** (현재):
```cpp
auto web_view = std::make_unique<views::WebView>(profile);
web_view->SetPreferredSize(...);
web_view->LoadInitialURL(url);
web_view_ = AddChildView(std::move(web_view));
```

**After**:
```cpp
auto web_view = std::make_unique<views::WebView>(profile);
web_view->SetPreferredSize(...);

// CRITICAL: force WebContents creation BEFORE LoadInitialURL (which
// triggers WebUIController construction synchronously inside its call
// stack). UserData must be attached BEFORE the WebUIController ctor reads
// it. Calling GetWebContents() lazy-creates the WebContents without
// initiating navigation.
content::WebContents* wc = web_view->GetWebContents();
DCHECK(wc);
MahoBoostControllerWebContentsData::CreateForWebContents(wc);
if (auto* data = MahoBoostControllerWebContentsData::FromWebContents(wc)) {
  data->controller = controller_weak;
}

// Now safe to navigate; MahoBoostUI ctor will find UserData via
// FromWebContents and capture the valid WeakPtr.
web_view->LoadInitialURL(url);
web_view_ = AddChildView(std::move(web_view));
```

#### 5.3 `MahoBoostWindowController::CreateAndShowWidget` 변경

**Before** (현재):
```cpp
auto* delegate = new MahoBoostWindowView(
    profile, url, kBoostModeWidth, kBoostModeHeight,
    base::BindOnce(&MahoBoostWindowController::OnDelegateWindowClosing,
                   weak_factory_.GetWeakPtr()));

if (auto* editor_wc = delegate->GetWebViewWebContents()) {
  MahoBoostControllerWebContentsData::CreateForWebContents(editor_wc);
  if (auto* data =
          MahoBoostControllerWebContentsData::FromWebContents(editor_wc)) {
    data->controller = weak_factory_.GetWeakPtr();
  }
}
```

**After**:
```cpp
auto* delegate = new MahoBoostWindowView(
    profile, url, kBoostModeWidth, kBoostModeHeight,
    base::BindOnce(&MahoBoostWindowController::OnDelegateWindowClosing,
                   weak_factory_.GetWeakPtr()),
    weak_factory_.GetWeakPtr());

// (post-ctor attach block 삭제)
```

#### 5.4 Dead code 삭제

- `GetPendingControllerStorage()` (maho_boost_window_controller.cc:45-49)
- `MahoBoostWindowController::ConsumePendingControllerForUI()` (maho_boost_window_controller.cc:156-162)
- `ConsumePendingBoostControllerForUI()` (maho_boost_window_controller_bridge.h)
- `MahoBoostWindowController::ConsumePendingControllerForUI()` 선언 (maho_boost_window_controller.h)
- `MahoBoostUI::SetController` (이미 dead code, 제거하거나 새 UserData 경로로 wire)

#### 5.5 `delegate->GetWebViewWebContents()` 메서드

이미 존재 (이전 fix 시도에서 추가). 그대로 유지하되 controller 측에서 더 이상 호출 안 함.

---

## §6 Alternatives Considered & Rejected

| Option | 거절 사유 |
|--------|----------|
| **B. URL query param + JS** | C++ WeakPtr을 JS에서 dereference 불가. 실효성 0 |
| **C. Mojo로 controller 후송** | `CreatePageHandler` 도착과 controller 도착 사이 window. 그 사이 Zap 클릭 시 silent abort. 큐잉 복잡도 추가, A와 동일 정답 도달 |
| **D. UUID + global map embedded URL** | global map 관리, controller 죽을 때 cleanup, ID 발급, URL 인코딩. A 대비 4배 코드량, 정답성 동일 |
| **E. BrowserUserData<MahoBoostWindowController>** | Boost editor widget이 top-level (BrowserView에 parent 안 됨). WebUI에서 Browser* 알 방법 없음. 결국 D로 환원 |
| **F. SetController 사후 호출** | timing 가정 다시 들어옴. 또는 page handler 생성 deferred → C와 동일 |
| **G. glic-style `AttachToHost` 패턴** | Chromium canonical 패턴, robust. **하지만** buffering 로직 + DidFinishNavigation observer + 여러 추가 클래스 멤버 필요. A 대비 코드량 5배. 향후 prerender/BFCache 추가 시 더 안전한 마이그레이션 후보 (§9에 deferred로 기록) |

**A가 유일하게 (i) global state 0, (ii) timing 가정 0, (iii) 최소 diff 만족.**

---

## §7 Latent Bugs (Zap fix와 별개로 함께 정리)

### 7.1 [심각] Browser* dangling + map leak

- `GetBrowserControllersMap()` (line 39): `std::map<Browser*, unique_ptr<MahoBoostWindowController>>` — **erase 호출 없음**
- Browser 죽으면 map 엔트리 살아남고, controller의 `raw_ptr<Browser> browser_`가 dangle
- `GetActiveTabForBoost`가 `controller->browser_` dereference (line 200) → **UAF**
- **Fix**: `BrowserListObserver::OnBrowserRemoved`에서 erase, OR `BrowserUserData<MahoBoostWindowController>`로 마이그레이션

### 7.2 Dual teardown path

- `OnDelegateWindowClosing` (line 491) + `OnWidgetDestroying` (line 346) 둘 다 `widget_.reset()` PostTask
- 현재는 idempotent (두 번째 reset은 no-op), 미래 state 추가 시 fragile
- **Fix**: 단일 cleanup helper로 통합

### 7.3 stale `current_zap_boost_id_` on editor close

- editor 빠르게 닫힐 때 page handler가 ExitZapMode JS path 못 타면 `current_zap_boost_id_` stale
- 새 editor의 `OnContentScriptEvent`가 stale state로 동작 가능
- **Fix**: page handler dtor 또는 OnEditorKilled에서 `current_zap_boost_id_.clear()`

### 7.4 GRD partial injection silent failure

- `InjectContentScript`에서 CSS empty면 abort (line 309-313, 좋음)
- JS 리소스 empty면 단지 LOG만 찍고 `ExecuteInIsolatedWorld("")`가 silent skip
- selector_component.js만 비고 zap_overlay.js 정상이면 → `ns.SelectorComponent` undefined runtime error, 사용자한테 무피드백
- **Fix**: 어느 하나라도 empty면 전체 injection abort + `content_script_injected_ = false`로 retry 가능하게

---

## §8 Acceptance Criteria

- [ ] 사용자가 탭 우클릭 → Boost → editor 윈도우 → Zap 버튼 클릭 시 다음 trace가 발생:
  - `MahoBoostUI ctor: controller VALID` (이전: INVALID)
  - `PageHandler::EnterZapMode boost_id=...`
  - **`controller WeakPtr invalid` 사라짐**
  - `[MahoBoost] InjectContentScript: ...` (또는 GRD가 정상이면 verbose log만)
- [ ] target tab에 yellow border + bottom bar (zap overlay) 표시
- [ ] target tab에서 element 클릭 시 → element 사라짐 + zap_selectors에 selector 추가됨
- [ ] editor 닫으면 → target tab의 zap mode kZap → kNone 전환 (기존 검증 완료)
- [ ] §10의 unit test가 pre-fix에서 fail, post-fix에서 pass
- [ ] §7의 잠재 버그 7.1, 7.3, 7.4 함께 해결 (7.2는 deferred 가능)
- [ ] `cargo test --workspace` 통과 (rust 회귀 없음)
- [ ] `MahoBoostLifecycleTest.*` 7개 모두 통과 (회귀 없음)

---

## §9 Deferred Items (의도적)

- **Glic-style `AttachToHost` 패턴 마이그레이션**: 향후 boost editor에 prerender/BFCache 추가 시 또는 서로 다른 controller 인스턴스가 같은 WebContents를 공유하는 시나리오 대비. 현재는 Option A로 충분.
- **`browser_` raw_ptr → BrowserUserData 마이그레이션**: §7.1의 더 robust한 fix. 우선 OnBrowserRemoved erase로 정리하고, 본격 마이그레이션은 별도 plan.
- **Cross-platform (Linux/Windows) 검증**: macOS only.

---

## §10 Test (Pre-fix Fail, Post-fix Pass)

`maho-chromium/browser/ui/views/boost/maho_boost_lifecycle_interactive_uitest.cc`에 추가:

```cpp
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, WebUIControllerHasValidController) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  views::Widget* widget = GetController().GetWidgetForTesting();
  ASSERT_TRUE(widget);

  // Find the editor's WebContents
  views::View* contents = widget->GetContentsView();
  ASSERT_TRUE(contents);
  ASSERT_FALSE(contents->children().empty());
  auto* web_view = views::AsViewClass<views::WebView>(contents->children()[0]);
  ASSERT_TRUE(web_view);
  content::WebContents* editor_wc = web_view->GetWebContents();
  ASSERT_TRUE(editor_wc);

  // Wait for chrome-untrusted://maho-boost-editor/ to commit
  ASSERT_TRUE(content::WaitForLoadStop(editor_wc));

  // Verify MahoBoostUI's WeakPtr to controller is valid
  content::WebUI* web_ui = editor_wc->GetWebUI();
  ASSERT_TRUE(web_ui);
  auto* maho_ui = web_ui->GetController()->GetAs<MahoBoostUI>();
  ASSERT_TRUE(maho_ui);

  // Pre-fix: this returns false (controller WeakPtr invalid)
  // Post-fix: this returns true
  EXPECT_TRUE(maho_ui->controller_for_testing());
}
```

`MahoBoostUI`에 `base::WeakPtr<MahoBoostWindowController> controller_for_testing()` getter 추가 필요 (테스트 전용).

추가 강한 테스트 (full-stack):
- `EnterZapMode` Mojo IPC 직접 호출 후 `MahoBoostInjectionHandler::FromWebContents(active_wc)`가 non-null인지 확인 (handler 생성은 `GetActiveTab()` 통과 시에만 일어남)

---

## §11 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| `views::WebView::GetWebContents()`가 LoadInitialURL 없이 lazy-create 안 함 | Low (~5%) | High | `chromium/src/ui/views/controls/webview/webview.cc`에서 `WebView::GetWebContents()` 1분 확인 — `CreateWebContents` 호출하고 navigation 안 거는지 |
| 향후 prerender/BFCache 추가 시 WebContents replacement | Low | Medium | DidFinishNavigation observer에서 re-attach (§9 deferred) |
| `browser_` dangling이 같은 증상으로 발현 | Medium | Medium | §7.1을 함께 fix. trace에서 `browser_ is null` warning 보면 그쪽 |
| 잠재 버그 7.2-7.4가 production 사용자한테 발현 | Medium | Low~Medium | 함께 fix |

---

## §12 Atomic Commit Strategy

| # | Commit message | Scope |
|---|----------------|-------|
| 1 | `fix(boost): pass controller WeakPtr through MahoBoostWindowView ctor (Option A)` | §5 핵심 fix |
| 2 | `chore(boost): remove dead GetPendingControllerStorage / Consume* path` | §5.4 cleanup |
| 3 | `fix(boost): erase MahoBoostWindowController from map on browser teardown` | §7.1 |
| 4 | `fix(boost): clear current_zap_boost_id_ on editor close` | §7.3 |
| 5 | `fix(boost): abort entire content script injection if any GRD resource empty` | §7.4 |
| 6 | `test(boost): assert MahoBoostUI controller_ WeakPtr is valid post-construction` | §10 |
| 7 | (no commit, verification) | build + manual smoke |

---

## §13 Execution Plan

### WU-1: Option A core fix (§5)
- `category="quick"`, ~30줄 변경
- estimated: 20 min

### WU-2: Dead code cleanup (§5.4)
- `category="quick"`, deletion only
- estimated: 10 min

### WU-3: Latent bug 7.1 (Browser dangling)
- `category="deep"`, BrowserListObserver wiring
- estimated: 30 min

### WU-4: Latent bugs 7.3 + 7.4
- `category="quick"`, defensive code
- estimated: 20 min

### WU-5: Test (§10)
- `category="quick"`, single uitest case
- estimated: 30 min

### WU-6: Build + manual UI smoke (§8)
- 사용자 collaboration: 우클릭→Boost→Zap→element 사라짐
- 또는 cliclick으로 자동화 (이전 시도에서 menu activation 실패 확인 — 우선 사용자 manual로)
- estimated: 30 min

---

## §14 Confidence + Honest Caveats

**Option A 성공 확신도**: ~95%

**5%의 불확실성**: upstream `views::WebView::GetWebContents()`가 LoadInitialURL 없이 lazy-create 보장. Chromium 본 트리에서 `chromium/src/ui/views/controls/webview/webview.cc`의 `GetWebContents()` 구현 1분 확인하면 검증 가능. (Librarian 분석에서 이 점은 unstable이라고 추가 plan 권고)

**Fix 후 깨질 가능성**:
1. prerender/BFCache 추가 시 WebContents replacement → DidFinishNavigation re-attach 필요 (§9)
2. 잠재 버그 7.1 (Browser dangling)이 동일 증상으로 발현 → fix 후 trace에서 `browser_ is null` warning 봐야 정확히 식별

**또 timing-기반 hack인가**: **아니오**. 동일 stack frame, 단일 UI thread, source-order 보장. timing assumption 0개.

---

## §15 본 plan의 출처

- **explore agent (bg_74518250)**: JS Zap click → Mojo → C++ chain 전수 검증, 모든 silent break point 11개 (A-J) 식별, WebContentsUserData lookup의 4가지 실패 모드 분석.
- **librarian agent (bg_40b6ab37)**: Chromium upstream `chromium/src/ui/views/controls/webview/webview.cc`, `chrome/browser/glic/host/*` (canonical AttachToHost 패턴) 검증. WebUIController 생성 timing, web_ui→GetWebContents 동일성 보장 확인.
- **oracle agent (bg_0fd42ae7)**: 두 결과 + 모든 trace + 코드 변경 이력 종합. 결정론적 ordering 진단 + 6개 옵션(A-G) 평가 + 잠재 버그 4건 식별 + 신뢰도 95%.

세 에이전트가 동일한 root cause + 동일한 권장 fix(Option A) 도달 — 100% 수렴.

---

**End of plan.**
