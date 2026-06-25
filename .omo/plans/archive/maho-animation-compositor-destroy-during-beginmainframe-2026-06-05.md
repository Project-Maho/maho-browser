# Maho Animation Crash — Compositor Destruction During BeginMainFrame

**Date**: 2026-06-05
**Author**: Sisyphus
**Status**: Ready for execution
**Trigger**: `views::Label::PaintText` DCHECK fix 후 라이브 검증 시 새로 노출된 `cc::LayerTreeHost::~LayerTreeHost` DCHECK. 사용자 보고된 Zap 무반응 fix와 무관, 별개의 widget lifecycle 버그.

**선행 plan**: `.sisyphus/plans/maho-startup-label-dcheck-favorites-grid-2026-06-05.md` (Label DCHECK fix 완료, 이후 노출됨)

---

## §1 Executive Summary

`MahoSpacesOverlayController::FinishHide`가 animation OnEnded 콜백 안에서 **synchronously** `widget_.reset()` 호출. 이게 `cc::BeginMainFrame` 호출 stack 안에서 실행 → Widget destruction → Compositor destruction → `cc::LayerTreeHost::~LayerTreeHost`가 `DCHECK(!inside_main_frame_)` 발화 → SIGABRT.

**Fix**: `MahoCommandOverlayController::FinishHideAnimation` (이미 정확한 패턴 사용)와 동일하게 `PostTask` 로 widget destruction을 다음 task iteration으로 deferring.

---

## §2 Empirical Evidence

### Crash trace

```
[FATAL:cc/trees/layer_tree_host.cc:269] Check failed: !inside_main_frame_.
```

### Stack

```
cc::SingleThreadProxy::BeginMainFrame
  ui::Compositor::BeginMainFrame                    ← inside_main_frame_ = true
  ui::CompositorAnimationObserver::Notify
  ui::LayerAnimatorCollection::OnAnimationStep
  ui::LayerAnimator::Step → FinishAnimation
  ui::LayerAnimationSequence::ProgressToEnd
  views::AnimationBuilder::Observer::OnLayerAnimationEnded
  base::OnceCallback::Run                            ← animation OnEnded 콜백 실행
  views::Widget::~Widget                             ← Widget 파괴 (synchronous)
  views::NativeWidgetMac::~NativeWidgetMac
  remote_cocoa::NativeWidgetNSWindowBridge::CloseWindowNow
  ui::Compositor::~Compositor
  cc::LayerTreeHost::~LayerTreeHost                  ← DCHECK FAIL
```

### 충돌 reproduce 시나리오

- 빈 프로필로 Maho 실행 + example.com 로드
- ~6초 후 (or hover 등 paint trigger 시)
- spaces_overlay 또는 동일 패턴의 overlay가 hide animation 종료 → widget destroy → DCHECK

---

## §3 Architectural Root Cause

### upstream Chromium invariant

`cc::LayerTreeHost::~LayerTreeHost()` (chromium/src/cc/trees/layer_tree_host.cc:269):

```cpp
LayerTreeHost::~LayerTreeHost() {
  ...
  CHECK(!inside_main_frame_);  // ← 이 destructor가 BeginMainFrame 안에서 실행되면 안 됨
  ...
}
```

`ui::Compositor` 파괴 시 내부의 `cc::LayerTreeHost` 도 파괴됨. Compositor는 NativeWidget이 가지고 있고 NativeWidget은 Widget이 소유. **Widget 파괴 = 결국 Compositor 파괴**.

`inside_main_frame_` 플래그는 `BeginMainFrame()` 시작/종료에서 set/unset. 그래서 **BeginMainFrame 호출 stack 안에서는 어떤 Widget도 파괴할 수 없음**.

### Maho에서 패턴 위반: `MahoSpacesOverlayController::FinishHide`

`maho_spaces_overlay_controller.cc:261-263`:
```cpp
void MahoSpacesOverlayController::FinishHide() {
  CloseWidget(restore_focus_on_hide_);  // ← synchronous widget_.reset() 호출
}
```

`CloseWidget` (line 228 area):
```cpp
void MahoSpacesOverlayController::CloseWidget(bool restore_focus) {
  weak_factory_.InvalidateWeakPtrs();
  ...
  widget_.reset();   // ← synchronous Widget 파괴
  ...
}
```

이 `FinishHide`가 line 215-225의 `AnimationBuilder().OnEnded(...)` 콜백:
```cpp
views::AnimationBuilder()
    .OnEnded(base::BindOnce(&MahoSpacesOverlayController::FinishHide,
                            weak_factory_.GetWeakPtr()))
    .OnAborted(base::BindOnce(&MahoSpacesOverlayController::FinishHide,
                              weak_factory_.GetWeakPtr()))
    ...
```

OnEnded 콜백은 LayerAnimator가 BeginMainFrame 안에서 호출 → `widget_.reset()` 동기 실행 → DCHECK fail.

### 같은 파일들에서 정답 패턴

**`MahoCommandOverlayController::FinishHideAnimation`** (`maho_command_overlay_controller.cc:160-171`):
```cpp
void MahoCommandOverlayController::FinishHideAnimation() {
  if (!widget_) return;
  widget_->RemoveObserver(this);
  widget_->Hide();
  std::unique_ptr<views::Widget> widget = std::move(widget_);  // ← move out
  is_hiding_ = false;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce([](std::unique_ptr<views::Widget>) {},   // ← deferred 파괴
                     std::move(widget)));
}
```

**`MahoNotificationOverlay::OnDismissTimerFired`** (`maho_notification_overlay.cc:243-251`): 동일 패턴.

`spaces_overlay`가 정답 패턴을 누락한 유일한 케이스.

---

## §4 Why Tests Pass But Live App Crashes

테스트 환경 (`InProcessBrowserTest`):
- 헤드리스 모드에서 animation이 instantaneous로 진행 또는 skip
- `BeginMainFrame` 호출 시점이 다름
- `spaces_overlay` 자동으로 안 띄움

라이브:
- 사용자 어떤 동작 (Cmd+T, space 전환, 그 외 트리거) 으로 spaces_overlay 잠시 표시 → hide animation → BeginMainFrame 안에서 widget destroy → DCHECK

또는 본 crash가 spaces_overlay 외 다른 동일-패턴 코드에서 발생할 가능성 — explore agent 분석 결과 통합 후 추가 후보 식별 필요.

---

## §5 Definitive Fix

### Fix #1 (확실): `MahoSpacesOverlayController::FinishHide`

`maho_spaces_overlay_controller.cc:261-263`:

**Before**:
```cpp
void MahoSpacesOverlayController::FinishHide() {
  CloseWidget(restore_focus_on_hide_);
}
```

**After** (PostTask 패턴 도입, command_overlay와 동일):
```cpp
void MahoSpacesOverlayController::FinishHide() {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoSpacesOverlayController> self) {
            if (self) {
              self->CloseWidget(self->restore_focus_on_hide_);
            }
          },
          weak_factory_.GetWeakPtr()));
}
```

또는 더 간단하게: `CloseWidget` 자체를 PostTask deferred로 만들지 않고 `FinishHide`만 deferring (다른 caller들은 BeginMainFrame stack 밖에 있음).

### Fix #2 (잠재): explore agent 결과 통합

`bg_bf06df69`가 분석 중. AnimationBuilder + OnEnded + Widget destruction 패턴의 다른 후보 발견 시 동일 fix 적용. 현재 본 직접 grep 결과:

| 파일 | OnEnded 콜백 | Widget destruction? | 상태 |
|------|------------|--------------------|------|
| `maho_command_overlay_controller.cc:142` | FinishHideAnimation | ✅ PostTask 사용 | OK |
| `maho_notification_overlay.cc:281` | OnDismissTimerFired | ✅ PostTask 사용 | OK |
| **`maho_spaces_overlay_controller.cc:216`** | **FinishHide → CloseWidget** | ❌ synchronous | **BUG** |
| `maho_sidebar_favorites_grid_view.cc:1608` | inline lambda — view visibility만 | ❌ no widget destroy | OK |

---

## §6 Acceptance Criteria

- [ ] Maho.app 실행 + 6초 idle → no crash
- [ ] spaces_overlay 트리거 (Cmd+T, space switch 등) 후 hide animation → no crash
- [ ] `views::AnimationBuilder` OnEnded 콜백에서 widget 파괴하는 모든 코드가 PostTask 패턴 사용
- [ ] `~/Library/Logs/DiagnosticReports/Maho-*.ips` 신규 0건
- [ ] 회귀 0: `MahoBoostLifecycleTest.*` + `MahoSpacesOverlayController*` 관련 테스트 통과

---

## §7 Test (Pre-fix Fail / Post-fix Pass)

자동:
```cpp
IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayControllerTest, HideAnimationDoesNotCrash) {
  // 1. spaces overlay 표시
  // 2. animation 끝까지 진행
  // 3. process alive 검증
  // Pre-fix: SIGABRT
  // Post-fix: clean
}
```

수동:
```bash
"chromium/src/out/Default/Maho.app/Contents/MacOS/Maho" \
  --user-data-dir=/tmp/maho-anim-test \
  --no-first-run \
  https://example.com
# Cmd+T or trigger spaces_overlay → hide → no crash
```

---

## §8 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| spaces_overlay fix 외에 또 다른 OnEnded → widget destroy 패턴 존재 | Medium | High | explore agent 결과로 보완 |
| PostTask defer 로 인한 visual glitch | Very Low | Low | command_overlay, notification_overlay 둘 다 동일 패턴, 문제 없음 검증됨 |
| `weak_factory_.InvalidateWeakPtrs()`가 CloseWidget 안에서 호출됨 — PostTask 전에 invalidate되면 task 실행 안 됨 | Medium | Medium | WeakPtr 검사를 task 시작 시점에 수행 |

---

## §9 Atomic Commit Strategy

| # | Commit | Scope |
|---|--------|-------|
| 1 | `fix(spaces-overlay): defer widget destruction to next task iteration to avoid Compositor destructor DCHECK during BeginMainFrame` | §5 Fix #1 |
| 2 | (explore 결과 후) 추가 후보 fix | §5 Fix #2 |
| 3 | (no commit) 빌드 + 라이브 검증 + Zap fix 최종 검증 |

---

## §10 Execution Plan

### WU-1: spaces_overlay FinishHide PostTask 마이그레이션
- `category="quick"`, ~10줄 변경
- 추정: 10분
- QA: 빌드 + spaces overlay 트리거 → 정상

### WU-2: explore 결과 통합 (잠재 후보 추가 fix)
- `category="quick"`, 발견 시 fix
- 추정: 15분

### WU-3: 빌드 + 라이브 검증 (드디어 Zap fix 최종 검증)
- 우클릭 → Boost → editor → Zap → element 사라짐 자동화
- `MahoBoostUI ctor: controller VALID` trace 확인 + 페이지 element 실제 사라짐 확인
- 추정: 30분

---

## §11 Confidence

**~90%**: spaces_overlay::FinishHide는 명확한 root cause. command_overlay/notification_overlay와 비교해 패턴 누락 확정. PostTask defer fix는 검증된 표준 패턴.

**불확실 10%**: 실제 충돌이 spaces_overlay에서 일어났는지, 아니면 또 다른 같은-패턴 코드인지는 explore agent 결과 통합 후 확정. 만약 다른 곳이면 §5 Fix #2 적용.

---

## §12 본 plan의 출처

- 직접 grep + 패턴 비교 (command_overlay/notification_overlay correct vs spaces_overlay incorrect)
- upstream `cc/trees/layer_tree_host.cc:269` DCHECK 정확한 위치 확인
- crash stack trace 직접 분석
- explore agent (`bg_bf06df69`) 결과는 도착 시 §5 Fix #2 보완에 사용

---

**End of plan.**
