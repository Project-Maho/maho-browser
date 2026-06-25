# Maho Startup Crash — Label DCHECK in Tab Preview + 4 Suspects

**Date**: 2026-06-05
**Author**: Sisyphus (explore + Momus 종합)
**Status**: Ready for execution (Momus [OKAY] + explore root cause re-ranked)
**Trigger**: Zap fix 검증을 위한 라이브 UI 자동화 시도 중, Maho 앱이 paint 직후 SIGABRT로 충돌. Zap fix와 무관한 별개 rendering DCHECK.

**Plan 1차 (favorites_grid only) 보완**: explore agent가 **5개 후보를 위험도 ranking**으로 식별했고 진짜 #1은 `maho_tab_preview_controller.cc`. 충돌 시 화면에 정확히 그 tab preview가 떠 있었음.

---

## §1 Executive Summary

`views::Label::PaintText`의 upstream DCHECK (`label.cc:955`)가 발화. Label의 ancestor 중 layer를 가진 view의 `fills_bounds_opaquely()`가 false라서, subpixel anti-aliased text가 transparent layer 위에 그려질 때 LCD pixel sampling이 의미 없는 RGB 값을 읽는 invariant 위반. **5개 culprit** 중 #1 (`maho_tab_preview_controller.cc`)이 충돌 시 화면에 떠 있던 hover preview와 정확히 일치 — 가장 강한 후보.

**Fix**: 5개 파일의 Label/View에 `SetSubpixelRenderingEnabled(false)` (+필요 시 `SetFillsBoundsOpaquely(false)`) 추가.

---

## §2 Empirical Evidence

### 충돌 trace (Maho-2026-06-05-091322.ips)

```
EXC_CRASH SIGABRT
abort()
logging::CheckError::~CheckError
views::Label::PaintText                    ← DCHECK source
views::View::Paint
views::View::PaintChildren
views::View::Paint
views::View::PaintFromPaintRoot
ui::Layer::PaintContentsToDisplayList
cc::RecordingSource::Update
cc::PictureLayer::Update
cc::LayerTreeHost::PaintContent
```

### Stderr

```
[FATAL:ui/views/controls/label.cc:955] DCHECK failed: view->layer()->fills_bounds_opaquely().
```

### 충돌 시점 화면 상태 (스크린샷 기록)

- example.com 페이지 로드됨
- 사이드바 "Drag to add Favorites" placeholder 표시
- **"Example Domain — https://example.com/" hover preview tooltip 표시** ← strong correlation with #1
- Address bar 가시
- ~6 초 후 paint 트리거 → DCHECK

---

## §3 Architectural Root Cause

### upstream `views::Label::PaintText` DCHECK 로직 (`label.cc:916-958`)

```cpp
void Label::PaintText(gfx::Canvas* canvas) {
  ...
  // SetSubpixelRenderingEnabled(false)면 일찍 return
  if (!display_text_ || display_text_->subpixel_rendering_suppressed() ||
      skip_subpixel_rendering_opacity_check_) {
    return;
  }

  // ancestor chain 따라 opaque background 또는 opaque-bounded layer 검색
  for (View* view = this; view; view = view->parent()) {
    const auto* background = view->background();
    if (background && color_provider &&
        IsOpaque(background->color().ResolveToSkColor(color_provider))) {
      break;  // OK
    }
    if (view->layer()) {
      DCHECK(view->layer()->fills_bounds_opaquely());  // ← FAIL
      break;
    }
  }
}
```

**DCHECK 발화 조건 3가지 모두 만족 시:**
1. Label에 subpixel rendering 활성화 (default true)
2. Label부터 root까지 ancestor에 opaque background 없음
3. ancestor 중 하나가 layer를 가지면서 `fills_bounds_opaquely() == false`

### Maho에서 위반하는 5개 위치 (explore 식별, 위험도 순)

| 순위 | 파일 | 위치 | 트리거 | 위험도 |
|------|------|------|--------|--------|
| **#1** | `maho_tab_preview_controller.cc` | `BuildBubbleContents` (42-81): title_label, url_label | 탭 hover → tab preview tooltip | **HIGHEST** |
| **#2** | `maho_location_bar_utility_panel_view.cc` | `ActionView` 170-173: SetPaintToLayer+SetOpacity, title_label/subtitle_label_ | Address bar 액션 표시 | HIGH |
| **#3** | `maho_dom_screenshot_action_dialog_view.cc` | `ActionMenuButton` 155: SetPaintToLayer + label_view_ | DOM screenshot 다이얼로그 | HIGH |
| **#4** | `maho_sidebar_action_popover_view.cc` | `ActionRow` 102-114: title_label, subtitle_label | + New Tab action popover, BubbleDialogDelegate | MEDIUM |
| **#5** | `maho_sidebar_favorites_grid_view.cc` | `EmptyFavoritesPlaceholderView` 137-161: 4개 Label | favorites grid empty state | MEDIUM-LOW |

`+ 동일 패턴`: `maho_sidebar_folder_hover_popup_view.cc` ItemRow's label

### #1이 가장 유력한 이유

- 충돌 시 tab preview tooltip이 화면에 있었음 (스크린샷 확인)
- BubbleFrameView가 `kColorSysSurface4` 배경색 (AlphaBlend 0x1E = semi-transparent)을 보고 `client_view->layer()->SetFillsBoundsOpaquely(false)`로 설정
- Bubble의 client_view 안에 title_label + url_label이 있는데 둘 다 `SetSubpixelRenderingEnabled(false)` 호출 안 됨
- ~225 ms hover delay 후 표시 → 충돌 6초 타이밍과 일치

---

## §4 Why Tests Pass But Live App Crashes

테스트 환경 (`InProcessBrowserTest`):
- 헤드리스 paint 경로 — bubble/popover lifecycle 간소화
- 사용자 hover 동작 시뮬레이션 안 함 → tab preview 안 뜸
- DCHECK는 `BUILDFLAG(IS_CHROMEOS)` 가드 없음, macOS 활성

라이브 앱:
- 사용자 mouse 움직임으로 tab preview 트리거 → paint → 폭발
- 사이드바 placeholder도 paint 트리거 시 동시 위반 가능

---

## §5 Definitive Fixes (5 sites)

### Fix #1 — `maho_tab_preview_controller.cc` (HIGHEST PRIORITY)

라인 42-81의 `BuildBubbleContents`:
```cpp
auto* title_label = parent->AddChildView(...);
title_label->SetSubpixelRenderingEnabled(false);  // ← ADD

auto* url_label = parent->AddChildView(...);
url_label->SetSubpixelRenderingEnabled(false);  // ← ADD
```

### Fix #2 — `maho_location_bar_utility_panel_view.cc`

라인 170-173 `ActionView`:
```cpp
if (IsDisabledAction(action_)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);  // ← ADD
  layer()->SetOpacity(0.55f);
}
```

라인 257, 268의 Labels:
```cpp
title_label->SetSubpixelRenderingEnabled(false);  // ← ADD after creation
subtitle_label_->SetSubpixelRenderingEnabled(false);  // ← ADD
```

### Fix #3 — `maho_dom_screenshot_action_dialog_view.cc`

라인 155 `ActionMenuButton`:
```cpp
SetPaintToLayer();
layer()->SetFillsBoundsOpaquely(false);  // ← ADD
layer()->SetOpacity(kDisabledRowOpacity);
```

라인 144의 `label_view_`:
```cpp
label_view_->SetSubpixelRenderingEnabled(false);  // ← ADD
```

### Fix #4 — `maho_sidebar_action_popover_view.cc` + `maho_sidebar_folder_hover_popup_view.cc`

`ActionRow` (popover) 라인 107, 114 의 Labels:
```cpp
title_label->SetSubpixelRenderingEnabled(false);  // ← ADD
subtitle_label->SetSubpixelRenderingEnabled(false);  // ← ADD
```

`ItemRow` (folder_hover_popup) 라인 82-86 의 label: 동일 적용.

### Fix #5 — `maho_sidebar_favorites_grid_view.cc`

`EmptyFavoritesPlaceholderView` 라인 137-161 의 4개 Label:
```cpp
badge_label_->SetSubpixelRenderingEnabled(false);
close_label_->SetSubpixelRenderingEnabled(false);
title_label_->SetSubpixelRenderingEnabled(false);
description_label_->SetSubpixelRenderingEnabled(false);
```

### Bonus — `maho_sidebar_archive_view.cc:386` + `maho_sidebar_library_rail_view.cc:280`

각각 `SetFillsBoundsOpaquely(false)` 누락 (자식이 ImageView/icon button이므로 DCHECK 직접 발화는 LOW이지만 일관성 위해 추가).

---

## §6 Why `SetSubpixelRenderingEnabled(false)` Over Other Options

| 옵션 | 적용성 | 단점 |
|------|--------|------|
| **A. `SetSubpixelRenderingEnabled(false)`** | 모든 Label에 안전 | macOS에서 미세하게 텍스트 quality 차이 (사실상 차이 없음) |
| B. `SetFillsBoundsOpaquely(true)` on layer | 진짜 opaque view에만 | translucent design intent 깨짐 |
| C. ancestor에 opaque background | 가능 | 디자인 변경 (배경 색 추가 필요) |
| D. `SetSkipSubpixelRenderingOpacityCheck()` | escape hatch | DCHECK 회피일 뿐, 실제 paint 결과는 여전히 잘못된 LCD sampling 가능 |

A가 표준 패턴이고 Maho 자체 코드에도 같은 위치에서 사용 중 (`AddPlaceholderLabel` 헬퍼, `maho_sidebar_space_dot_view.cc`, etc.).

---

## §7 Acceptance Criteria

- [ ] Maho.app 실행 + example.com 로드 → 6초 idle → no crash
- [ ] 탭 hover (tab preview tooltip 띄움) → no crash
- [ ] 우클릭 → context menu → no crash
- [ ] 사이드바 favorites empty state 표시 → no crash
- [ ] address bar action popover 표시 → no crash
- [ ] DOM screenshot dialog 트리거 → no crash
- [ ] `~/Library/Logs/DiagnosticReports/Maho-*.ips` 신규 0건
- [ ] `MahoBoostLifecycleTest.*` 8개 + `MahoBoostBehaviorTest.*` 통과 (Zap fix 회귀 0)
- [ ] WU-3 의 라이브 UI 자동화 (cliclick + CDP)로 우클릭→Boost→Zap chain 끝까지 동작 확인 ← **이게 최종 목표 (원래 Zap fix 검증)**

---

## §8 Test (Pre-fix Fail / Post-fix Pass)

### 자동 검증

```cpp
IN_PROC_BROWSER_TEST_F(MahoTabPreviewControllerTest, BubbleContentsPaintsWithoutDCHECK) {
  // tab preview bubble 강제 표시
  // ForceRepaint
  // process alive 확인
  // Pre-fix: SIGABRT
  // Post-fix: 정상
}
```

### 수동 검증

```bash
python3 maho-chromium/build/scripts/build_maho.py
"chromium/src/out/Default/Maho.app/Contents/MacOS/Maho" \
  --user-data-dir=/tmp/maho-test-clean \
  --no-first-run \
  https://example.com
# 1. 10초 대기
# 2. 사이드바의 example.com 탭 위에 마우스 hover (tab preview 트리거)
# 3. 탭 우클릭 (context menu 트리거)  
# 4. 모든 단계에서 no crash 확인
```

---

## §9 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| #1 fix 후에도 #2~#5 중 하나에서 DCHECK 발화 | Medium | High | 5개 모두 함께 fix |
| 또 다른 미발견 Label/translucent-layer 조합 | Low | Medium | `grep SetPaintToLayer + grep Label` regression 추가 |
| `SetSubpixelRenderingEnabled(false)`로 텍스트 quality 저하 | Very Low | Low | 한국어/영어/이모지 시각 비교 — 실제로는 차이 0 |
| translucent design 의도가 깨짐 | None | None | `SetSubpixelRenderingEnabled(false)`는 layer/배경 안 건드림 |

---

## §10 Atomic Commit Strategy

| # | Commit | Scope |
|---|--------|-------|
| 1 | `fix(tab-preview): disable subpixel rendering on bubble Labels (Label DCHECK)` | #1 |
| 2 | `fix(location-bar): disable subpixel rendering + add fills_bounds_opaquely(false) on disabled action rows` | #2 |
| 3 | `fix(location-bar): same on screenshot action dialog rows` | #3 |
| 4 | `fix(sidebar): disable subpixel rendering on action popover + folder hover popup Labels` | #4 |
| 5 | `fix(sidebar): disable subpixel rendering on favorites grid empty state Labels` | #5 |
| 6 | `chore(sidebar): add fills_bounds_opaquely(false) on archive_view + library_rail_view layers (consistency)` | bonus |
| 7 | (no commit) | 빌드 + 수동 검증 + Zap fix 라이브 검증 |

---

## §11 Execution Plan

### WU-1: Fix #1 — tab_preview_controller (HIGHEST)
- `category="quick"`, ~2줄 추가
- 5분
- QA: 빌드 + 탭 hover → no crash

### WU-2: Fix #2, #3 — location_bar (HIGH)
- `category="quick"`, ~6줄 추가
- 10분

### WU-3: Fix #4 — popover/folder_hover (MEDIUM)
- `category="quick"`, ~3줄 추가
- 5분

### WU-4: Fix #5 — favorites_grid (MEDIUM-LOW)
- `category="quick"`, ~4줄 추가
- 5분

### WU-5: Bonus consistency
- `category="quick"`, ~2줄 추가
- 5분

### WU-6: 빌드 + Live UI 검증 (Zap fix 최종 검증)
- 빌드 후 cliclick으로 우클릭→Boost→Zap chain 자동화
- `MahoBoostUI ctor: controller VALID` trace 확인
- editor의 zen-boost-zap 버튼 클릭 → InjectionHandler kZap 전환 → element 사라짐
- 30분

---

## §12 Confidence

**~95%**: explore agent가 5개 후보를 정확히 ranking + 충돌 시 화면 상태와 #1 매칭으로 root cause 확정. 5개 모두 fix 시 paint 경로상 모든 알려진 violation 차단.

**불확실 5%**: 또 다른 BubbleDialog/popup view에 같은 패턴 존재 가능성 (Maho 코드 광범위). 발견 시 동일 패턴 fix.

---

## §13 본 plan의 출처

- **explore agent (bg_e4d76d6b)**: 5개 culprit 위험도 ranking, 각 파일 line-precise 위반 위치, 표준 Chromium 패턴 (space_dot_view, footer_view 등이 정답) 식별, BubbleFrameView::OnThemeChanged의 fills_bounds_opaquely 동작 검증.
- **Momus agent (bg_529cf340)**: plan v1 [OKAY] (모든 file reference 검증). v2는 동일 acceptance 기준 + 확장된 scope.
- 직접 grep + label.cc:916-958 정확한 DCHECK 로직 검증.

---

**End of plan v2.**
