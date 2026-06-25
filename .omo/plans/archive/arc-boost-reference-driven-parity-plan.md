# Arc Boost Reference-Driven Parity Plan

**Created:** 2026-05-12  
**Revised:** 2026-05-12 (post-Oracle architecture review)  
**Status:** Execution-ready implementation plan  
**Reference baseline:** Arc Browser Boost UI (screenshots + external CSS example evidence)  
**Architecture review:** Oracle-verified. Native panel hosting confirmed, target-tab plumbing corrected.

---

## 0. Goal

Arc Browser의 Boost 기능과 UI/UX 패리티를 달성한다. 현재 Maho의 Boost는 CSS-only 관리자(chrome://maho-boost)로 동작하며, Arc이 제공하는 핵심 기능 대부분이 미구현 상태이다.

### Core Architectural Insight

**Arc Boost는 CSS/JS authoring system이다.** No-code UI state가 injected CSS/JS로 컴파일된다. 사용자가 보는 것은 에디터 컨트롤이고, 브라우저가 주입하는 것은 flat CSS/JS output이다.

외부 Arc boost CSS 예시가 이를 증명한다: 실제 Boost output은 selector override, hide/reposition/recolor/transition 패턴에 의존하는 flat CSS이며, `!important` 사용이 빈번하다. CSS filter 함수만이 아닌, 구체적 selector에 대한 직접적 스타일 override가 핵심이다.

이 플랜은 다음을 구현 가능한 수준으로 정의한다:

1. `BoostEditorState` source-of-truth 모델 (no-code UI state → CSS/JS 컴파일)
2. Site Control anchored compact panel (primary Arc-like UX)
3. JavaScript Boost 편집 및 실행 경로
4. No-code 색상 에디터 (color wheel + advanced sliders)
5. No-code 폰트 에디터 (preset grid + size/case controls)
6. Zap (요소 숨기기)
7. chrome://maho-boost as library/manager surface

---

## 1. Current State (코드베이스 근거)

### 1.1 Rust 모델 (`maho-types/src/boost.rs`)

- `Boost` struct은 이미 `custom_js: Option<String>`, `color_theme: Option<ColorOverride>`, `font_override: Option<FontOverride>` 필드를 보유
- `ColorOverride`는 background/text/links 세 색상만 지원 (Arc의 editor state 모델과 다름)
- `FontOverride`는 font_family/font_size/line_height 지원

### 1.2 BoostManager (`maho-core/src/boost_manager.rs`)

- `globally_enabled: bool`, `js_enabled: bool` 인메모리 플래그 존재
- **이 플래그들은 persist 되지 않음** — 앱 재시작 시 기본값(true)으로 리셋
- `generate_boost_injection_css()` — CSS만 생성, JS 미포함
- `apply_color_override()` — 단순 rgba 치환만 수행

### 1.3 FFI (`maho-ffi/src/lib.rs`)

- `maho_core_get_boosts_for_url` — CSS 전용 조회
- `maho_core_update_boost_css` — CSS만 업데이트
- JS 업데이트/조회/실행 관련 FFI 함수 없음
- editor state 업데이트 FFI 없음

### 1.4 Mojo IDL (`maho_boost.mojom`)

- `BoostEntry`에 `custom_css`만 노출, `custom_js`/editor state 미노출
- `UpdateBoostCss()` 단일 메서드만 존재
- editor state 업데이트 메서드 없음

### 1.5 Injection Handler (`browser/net/maho_boost_injection_handler.cc`)

- `InjectBoostCss()` — CSS를 `<style>` 태그로 주입
- JS 실행 경로 없음
- Zap(요소 숨기기) 경로 없음
- `BuildBoostCssScript()`는 isolated world에서 실행하지만 CSS 삽입만 담당

### 1.6 WebUI (`browser/resources/maho_boost/app.ts`)

- ~~전체 UI를 `root.innerHTML = ...` 로 렌더링~~ **Corrected:** Current `app.ts` does NOT use `innerHTML`. It uses DOM API methods. No Trusted Types violation exists in the current code.
- CSS-only 편집기 (textarea + save)
- 글로벌 관리자 뷰 (사이트별 패널 아님)
- 템플릿 선택 + 빈 boost 생성만 가능

### 1.7 Site Control (`browser/ui/site_control/maho_page_info_ui.cc`)

- Shield(content blocker) 진입점만 존재
- **Boost 진입점 없음** — page info에서 현재 사이트의 Boost로 진입 불가

### 1.8 없는 것 요약

| 기능 | 상태 |
|------|------|
| JS Boost 편집/실행 | 미구현 |
| No-code color editor (wheel + sliders) | 미구현 |
| No-code font editor (preset grid) | 미구현 |
| Zap (요소 숨기기) | 미구현 |
| Site Control anchored editor panel | 미구현 (현재는 전체 페이지 WebUI) |
| Site Control → Boost 진입 | 미구현 |
| Per-site panel flow | 미구현 |
| Editor state → CSS/JS compilation | 미구현 |
| globally_enabled/js_enabled 영속화 | 미구현 |
| Trusted Types 호환 렌더링 | ~~미구현~~ **해결됨**: 현재 app.ts는 innerHTML 미사용 |

---

## 2. Architecture: Editor State → CSS/JS Compilation

### 2.1 Core Principle

Arc Boost는 사용자에게 no-code 에디터를 제공하고, 그 에디터 상태를 CSS/JS로 컴파일하여 페이지에 주입한다. Code 탭은 사용자가 직접 raw CSS/JS를 작성하는 통로이다.

```
┌──────────────────────────────────────────────────────┐
│  BoostEditorState (source of truth, persisted)       │
├──────────────────────────────────────────────────────┤
│  color_editor: ColorEditorState                      │
│  typography_editor: TypographyEditorState             │
│  zap_rules: Vec<ZapRule>                             │
│  custom_css: Option<String>   ← Code tab, raw       │
│  custom_js: Option<String>    ← Code tab, raw       │
└──────────────────────────────────────────────────────┘
            │
            ▼  compile()
┌──────────────────────────────────────────────────────┐
│  Compiled Output (injected into page)                │
├──────────────────────────────────────────────────────┤
│  1. generated_css  (from color + typo + zap states)  │
│  2. custom_css     (user-authored, as-is)            │
│  3. custom_js      (user-authored, as-is)            │
└──────────────────────────────────────────────────────┘
```

No-code controls → generate CSS via compilation.  
Code tab edits → raw CSS/JS authored by the user, injected directly.

### 2.2 Evidence: External Arc Boost CSS

실제 Arc boost output을 분석하면:
- Flat CSS: selector 기반 override (`header { display: none !important; }`)
- `!important` 빈번 사용
- Hide/reposition/recolor/transition 패턴
- CSS filter 함수는 color adjustment의 일부일 뿐, 전체 모델이 아님
- 즉, Arc은 editor state를 구체적 CSS rule set으로 컴파일하는 시스템

### 2.3 BoostEditorState Sub-states

```rust
pub struct BoostEditorState {
    pub color_editor: ColorEditorState,
    pub typography_editor: TypographyEditorState,
    pub zap_rules: Vec<ZapRule>,
    pub custom_css: Option<String>,  // Code tab: user-authored CSS
    pub custom_js: Option<String>,   // Code tab: user-authored JS
}

pub struct ColorEditorState {
    /// Primary color from 2-handle color wheel (hue + position)
    pub hue: Option<f64>,           // 0.0..360.0
    pub wheel_position: Option<(f64, f64)>,  // normalized x,y within wheel
    /// Advanced controls (anchored popover)
    pub contrast: f64,              // -1.0 ~ 1.0, 0.0 = no change
    pub brightness: f64,            // -1.0 ~ 1.0, 0.0 = no change
    pub original_saturation: f64,   // 0.0 ~ 2.0, 1.0 = original
}

pub struct TypographyEditorState {
    pub font_preset: Option<String>,   // from preset grid
    pub size_adjustment: f64,          // multiplier, 1.0 = no change
    pub text_case: TextCase,           // Normal, Upper, Lower, Capitalize
}

pub enum TextCase {
    Normal,
    Uppercase,
    Lowercase,
    Capitalize,
}

pub struct ZapRule {
    pub selector: String,
    pub label: Option<String>,  // human-readable description
}
```

### 2.4 CSS/JS Compilation Pipeline

`BoostManager::compile_editor_state(state: &BoostEditorState) -> CompiledBoost`:

1. **Color CSS generation**: color_editor state → CSS rules
   - Wheel color → background/foreground overrides on body/elements
   - Advanced sliders → `html { filter: contrast() brightness() saturate(); }` (근사치)
2. **Typography CSS generation**: typography_editor state → CSS rules
   - Font preset → `body, p, h1, ... { font-family: X !important; }`
   - Size → `html { font-size: Ypx !important; }`
   - Case → `body { text-transform: Z !important; }`
3. **Zap CSS generation**: zap_rules → `{selector} { display: none !important; }`
4. **Custom CSS**: pass-through (user-authored)
5. **Custom JS**: pass-through (user-authored, separate injection)

The compiled CSS is concatenated in order and injected as a single `<style>`. JS is injected separately.

---

## 3. Image-Grounded UI Details

### 3.1 Main Editor Panel (스크린샷 근거)

메인 에디터는 Site Control에서 열리는 anchored compact panel이다:

```
┌─────────────────────────────────────┐
│  [Site name]              [•••]     │  ← 헤더
├─────────────────────────────────────┤
│                                     │
│   ┌───────────────────────────┐     │
│   │    2-handle color wheel   │     │  ← rainbow border로 둘러싸인
│   │    area (원형/반원 영역)    │     │     color picker 영역
│   │                           │     │
│   └───────────────────────────┘     │
│                                     │
│   [ Action1 ] [ Action2 ] [ Action3]│  ← 3-button action row
│                                     │
├─────────────────────────────────────┤
│   Font Preset Grid                  │  ← 폰트 프리셋 그리드
│   ┌────┐ ┌────┐ ┌────┐ ┌────┐     │     (타일/카드 형태)
│   │ Aa │ │ Aa │ │ Aa │ │ Aa │     │
│   └────┘ └────┘ └────┘ └────┘     │
│   ┌────┐ ┌────┐                    │
│   │ Aa │ │ Aa │                    │
│   └────┘ └────┘                    │
├─────────────────────────────────────┤
│  [Size] [Case] [Zap] [Code]        │  ← action buttons row
└─────────────────────────────────────┘
```

- **2-handle color wheel**: rainbow gradient border가 있는 원형 또는 반원 영역. 두 핸들로 색상/위치 선택.
- **3-button action row**: color wheel 아래 위치. 정확한 기능은 미확인 (reset/apply/advanced 추정).
- **Font preset grid**: 카드/타일 형태로 폰트 프리셋 표시. "Aa" 샘플 텍스트로 미리보기.
- **Action buttons**: Size (글자 크기), Case (대소문자), Zap (요소 숨기기), Code (raw CSS/JS 에디터)

### 3.2 Advanced Color Controls (스크린샷 근거)

Color wheel의 3-button action row 또는 별도 진입점에서 열리는 **anchored popover**로, 정확히 **세 개의 슬라이더**를 포함한다:

```
┌───────────────────────────────┐
│  Colors                    ▼  │  ← 접힘/펼침 헤더
├───────────────────────────────┤
│  Contrast         ───●──────  │  ← 수평 슬라이더
│  Brightness       ────●─────  │  ← 수평 슬라이더
│  Original Saturation ──●────  │  ← 수평 슬라이더
└───────────────────────────────┘
```

- 슬라이더는 -100 ~ +100 범위로 추정 (정확한 수치 미확인, 근사치)
- **Contrast**: 페이지 전체 요소의 명암 대비 조정
- **Brightness**: 페이지 전체 밝기 조정
- **Original Saturation**: 페이지 원본 색상의 채도 유지/조정

**구현 근사치 주의사항:**
- 정확한 색상 수학(color math)은 Arc 내부 구현이 비공개이므로 근사 구현
- Advanced sliders는 CSS `filter: contrast() brightness() saturate()` 조합으로 시각적 패리티 달성 가능
- Color wheel output은 보다 복잡: selector-specific color override CSS를 생성할 가능성이 높음
- 정확한 wheel → CSS 매핑은 구현 시 튜닝 필요

### 3.3 Zap Mode (스크린샷 근거)

Zap 모드 활성화 시 페이지에 다음 시각적 변화가 적용된다:

```
┌─────────────────────────────────────────────────┐
│                                                 │
│      [페이지 전체가 어둡게 dimmed]                │
│                                                 │
│      ┌─────────────────────┐                    │
│      │  타겟 요소가 노란색  │ ← 하이라이트       │
│      │  으로 강조 표시됨    │                    │
│      └─────────────────────┘                    │
│                                                 │
├─────────────────────────────────────────────────┤
│  Click an element to hide it           [Done]   │  ← 하단 full-width 안내 바
└─────────────────────────────────────────────────┘
```

- **페이지 dimming**: 반투명 어두운 오버레이 (pointer-events: none, 타겟만 통과)
- **타겟 하이라이트**: 마우스 호버 요소에 노란색(yellow) 강조 박스
- **하단 안내 바**: full-width, 좌측에 "Click an element to hide it" 텍스트, 우측에 "Done" 버튼
- 요소 클릭 시 해당 selector에 대한 `display: none !important` CSS rule을 ZapRule로 저장 후 컴파일

**구현 근사치 주의사항:**
- Arc의 "related elements" 자동 감지 서브플로우(예: 같은 클래스의 반복 요소를 함께 숨기기)는 스크린샷에서 확인 불가. 초기 구현에서는 단일 요소만 숨기고, related-elements 감지는 후속 개선으로 분류.
- Undo 서브플로우 (숨긴 요소 복원)의 정확한 UI는 미확인. 초기 구현에서는 에디터 패널의 Zap rules 목록에서 개별 삭제로 대체.

---

## 4. Target Parity (Arc Reference)

### 4.1 Arc Boost 핵심 기능 (구현 대상)

1. **Site Control anchored compact panel**: 현재 사이트의 Boost를 편집하는 anchored panel. Primary UX surface.
2. **Editor state model**: No-code UI state → CSS/JS compilation.
3. **CSS editor (Code tab)**: raw CSS 편집 (현재 있음, UI 개선 필요)
4. **JS editor (Code tab)**: raw JavaScript 코드 편집 + 실행
5. **No-code color editor**: 2-handle color wheel + advanced Contrast/Brightness/Saturation sliders
6. **No-code font editor**: 프리셋 그리드 + Size/Case 컨트롤
7. **Zap**: 페이지 요소를 클릭으로 숨기기, dimming + yellow highlight + bottom bar + Done
8. **Site Control entry point**: page info에서 현재 사이트 Boost 패널 진입
9. **chrome://maho-boost**: library/manager surface (전체 Boost 목록, 설정, 글로벌 토글)

### 4.2 Arc Boost에서 제외하는 것 (deprecated)

- **Share/Gallery**: Arc에서 이미 deprecated. 구현하지 않음.
- **Boost Store/Community**: 외부 공유 마켓. 범위 외.

---

## 5. Scope

### 5.1 In-scope

| # | 기능 | 설명 |
|---|------|------|
| 1 | BoostEditorState model | Rust struct으로 정의, 서브 state 포함. Compilation pipeline 구현. |
| 2 | Site Control anchored panel | Primary UX. Per-site Boost 에디터. Compact floating panel. |
| 3 | JS Boost editing (Code tab) | Mojo + injection handler에서 JS 편집/실행 지원 |
| 4 | No-code color editor | Color wheel + advanced sliders → compiled CSS |
| 5 | No-code font editor | Preset grid + Size/Case → compiled CSS |
| 6 | Zap | Dimming + yellow highlight + bottom bar → ZapRule → compiled CSS |
| 7 | Site Control entry point | Page info에서 현재 사이트 Boost 패널 진입 |
| 8 | chrome://maho-boost library | 전체 Boost 목록, 글로벌 설정, 관리 |
| 9 | Global/JS 플래그 영속화 | BoostManager 플래그를 storage에 저장 |
| 10 | ~~Trusted Types 수정~~ | **Not needed**: 현재 `app.ts`는 innerHTML 미사용. No violation exists. |

### 5.2 Out-of-scope

| 제외 항목 | 사유 |
|-----------|------|
| Share/Gallery | Arc에서 deprecated |
| Boost Store | 커뮤니티 마켓은 범위 외 |
| Related-elements auto-detect in Zap | 스크린샷 미확인, 후속 개선 |
| Exact Arc color math | 비공개 구현, 근사치로 대체 |
| Exact Arc font list | 실용적 소수 프리셋으로 시작 |
| Exact Zap undo subflow | UI 미확인, 목록 삭제로 대체 |

### 5.3 Approximations (명시적 표기)

다음 항목은 Arc 정확한 구현을 재현할 수 없으므로 근사 구현한다:

1. **Color wheel → CSS mapping**: Arc의 2-handle wheel이 정확히 어떤 CSS를 생성하는지 비공개. 우리 구현은 hue/saturation 기반 background-color override + filter 조합으로 근사.
2. **Advanced slider math**: `filter: contrast() brightness() saturate()` 조합은 시각적 근사치.
3. **Font preset list**: Arc 정확한 목록 비공개. 실용적 소수로 시작, 데이터 드리븐 확장 가능.
4. **Zap related-elements**: 자동 감지 알고리즘 미공개. 단일 요소만 초기 지원.
5. **Zap undo subflow**: 정확한 UI 미확인. Editor panel에서 rule 삭제로 대체.

### 5.4 Font Presets 참고

Arc의 정확한 폰트 목록은 비공개이므로, 실용적인 소수 프리셋으로 시작한다:

```
- System Default (-apple-system, BlinkMacSystemFont, sans-serif)
- Inter
- Georgia (serif)
- Merriweather (serif)
- SF Mono / Menlo (monospace)
- Fira Code (monospace)
```

프리셋은 향후 확장 가능하도록 데이터 드리븐으로 구현한다 (하드코드 X).

---

## 6. File Map

### 6.1 Rust layer (maho/)

| 파일 | 변경 내용 |
|------|-----------|
| `crates/maho-types/src/boost.rs` | `BoostEditorState`, `ColorEditorState`, `TypographyEditorState`, `TextCase`, `ZapRule` 추가. 기존 `ColorOverride`는 deprecated marker 추가 후 유지 (migration path). |
| `crates/maho-core/src/boost_manager.rs` | `compile_editor_state()` 메서드 추가: editor state → CSS/JS 생성. Zap rule → CSS 컴파일. Color state → CSS 컴파일. Typography state → CSS 컴파일. 플래그 영속화. |
| `crates/maho-core/src/maho_core.rs` | 새 메서드: `update_boost_editor_state_persisted`, `update_boost_custom_css_persisted`, `update_boost_custom_js_persisted`, `add_zap_rule_persisted`, `remove_zap_rule_persisted`, `compile_and_get_boost_output` |
| `crates/maho-storage/src/sqlite.rs` | `boost_editor_state` JSON blob 저장 (editor_state column). `global_enabled`/`js_enabled` 설정 저장/복원. |
| `crates/maho-ffi/src/lib.rs` | 새 FFI: `maho_core_update_boost_editor_state`, `maho_core_update_boost_custom_css`, `maho_core_update_boost_custom_js`, `maho_core_get_compiled_boost_for_url`, `maho_core_add_zap_rule`, `maho_core_remove_zap_rule` |

### 6.2 C++ bridge (`maho-chromium/third_party/maho/`)

| 파일 | 변경 내용 |
|------|-----------|
| `maho_bridge.h` | 새 함수 선언 추가 (editor state, compiled output, zap) |
| `maho_bridge.cc` | 새 FFI 함수 호출 래핑 |
| `maho_ffi.h` | 새 extern "C" 선언 |

### 6.3 Injection Handler (`maho-chromium/browser/net/`)

| 파일 | 변경 내용 |
|------|-----------|
| `maho_boost_injection_handler.h` | `InjectBoostJs()` 메서드 추가, `InjectCompiledBoost()` (CSS + JS 통합), Zap overlay injection 메서드 |
| `maho_boost_injection_handler.cc` | Compiled CSS injection, JS injection 경로 추가, Zap mode content script injection |

### 6.4 Mojo IDL (`maho-chromium/browser/ui/webui/maho_boost/`)

| 파일 | 변경 내용 |
|------|-----------|
| `maho_boost.mojom` | `BoostEntry`에 `editor_state`, `custom_css`, `custom_js`, `compiled_css` 추가. 새 메서드: `UpdateEditorState`, `UpdateCustomCss`, `UpdateCustomJs`, `AddZapRule`, `RemoveZapRule`, `GetBoostsForCurrentUrl`, `EnterZapMode`, `ExitZapMode`, `CompileAndPreview` |
| `maho_boost_page_handler.cc` | 새 Mojo 메서드 구현 |
| `maho_boost_page_handler.h` | 새 메서드 선언 |

### 6.5 WebUI Frontend (`maho-chromium/browser/resources/maho_boost/`)

| 파일 | 변경 내용 |
|------|-----------|
| `app.ts` | Library manager view 유지. Continue using the existing DOM API rendering pattern; no new Trusted Types work is needed. |
| `boost.css` | Panel 스타일, color wheel 스타일, font grid 스타일, Zap UI 스타일 |
| `views/editor_panel.ts` | 새 파일: Site Control anchored compact panel. Color wheel + 3-button row + font grid + action buttons (Size/Case/Zap/Code). |
| `views/color_editor.ts` | 새 파일: 2-handle color wheel (rainbow border), 3-button action row. Advanced popover trigger. |
| `views/color_advanced.ts` | 새 파일: Anchored popover with Contrast/Brightness/Original Saturation sliders. |
| `views/typography_editor.ts` | 새 파일: Font preset grid (Aa tiles) + Size stepper + Case toggle. |
| `views/code_editor.ts` | 새 파일: CSS/JS 탭 에디터 (Code button에서 진입). Raw authoring. |
| `views/zap_controls.ts` | 새 파일: Zap 모드 진입 + Zap rules 목록 관리 |

### 6.6 Native Panel Hosting (Primary UX, Views layer)

The primary Boost editor panel is a native Views-hosted bubble, following repo-proven patterns.

| 파일 | 변경 내용 |
|------|-----------|
| `browser/ui/site_control/maho_page_info_ui.cc` | Boost 섹션/진입 버튼 추가. 클릭 시 coordinator를 통해 native panel 열기. |
| `browser/ui/site_control/maho_page_info_ui.h` | `ShowBoostPanel()` 메서드 추가 |
| `browser/ui/views/location_bar/maho_boost_bubble_coordinator.h` | 새 파일: Coordinator that owns the bubble lifecycle. Holds `content::WebContents* target_contents_` (the active tab being boosted). Passes target into the page handler on creation. Pattern: `maho_location_bar_utility_bubble_coordinator.*` |
| `browser/ui/views/location_bar/maho_boost_bubble_coordinator.cc` | 새 파일: Creates/shows/hides the bubble. Observes target tab destruction (`WebContentsObserver`) to close panel if tab navigates away or closes. |
| `browser/ui/views/maho_boost_panel_view.h` | 새 파일: `LocationBarBubbleDelegateView` subclass. Hosts `WebUIContentsWrapperT<MahoBoostUI>`. Sets preferred size (320x480 initial). Pattern: `maho_shield_bubble_view.*` |
| `browser/ui/views/maho_boost_panel_view.cc` | 새 파일: Constructs WebUI wrapper, configures auto-resize bounds (320x400 min, 320x640 max), anchors to Site Control button. Overrides `OnWidgetActivationChanged()` for Zap-mode focus semantics (see Phase 6). |
| `browser/ui/webui/maho_boost/maho_boost_page_handler.h` | Add `SetTargetWebContents(content::WebContents*)` called by coordinator; removes reliance on `?url=` for primary path. |
| `browser/ui/webui/maho_boost/maho_boost_page_handler.cc` | Store `target_contents_` raw_ptr. Use it for injection calls instead of scanning tab strip. Fallback to URL-based lookup only when `target_contents_` is null (standalone `chrome://maho-boost` tab). |

### 6.7 Visual reference (스타일 참고용, 수정 대상 아님)

| 파일 | 참고 내용 |
|------|-----------|
| `browser/resources/maho_ai/maho_ai.css` | Anchored panel 스타일링 패턴 참고 |
| `browser/resources/maho_ai/views/cards.ts` | Card-based UI 컴포넌트 패턴 참고 |
| `browser/resources/maho_space_config/space_config.css` | Slider/control 스타일링 패턴 참고 |

### 6.8 기타 진입점

| 파일 | 변경 내용 |
|------|-----------|
| `browser/ui/views/command/maho_command_action_handler.cc` | "Edit Boost" / "Zap Element" 커맨드 추가 |

---

## 7. Phased Implementation Plan

### Phase 1: Foundation — Editor State Model + FFI/Mojo Pipeline + Trusted Types

**목표:** BoostEditorState model 정의, compilation pipeline 구현, 데이터가 FFI → Mojo → WebUI까지 흐르도록 한다.

**작업:**

1. `maho-types/src/boost.rs`에 `BoostEditorState` 및 sub-state structs 추가:
   ```rust
   pub struct BoostEditorState {
       pub color_editor: ColorEditorState,
       pub typography_editor: TypographyEditorState,
       pub zap_rules: Vec<ZapRule>,
       pub custom_css: Option<String>,
       pub custom_js: Option<String>,
   }
   // + ColorEditorState, TypographyEditorState, TextCase, ZapRule
   ```
2. `Boost` struct에 `editor_state: Option<BoostEditorState>` 추가
3. `BoostManager`에 `compile_editor_state()` 구현:
   - ColorEditorState → CSS rules (filter + selector overrides)
   - TypographyEditorState → CSS rules (font-family, font-size, text-transform)
   - ZapRules → CSS rules (`{selector} { display: none !important; }`)
   - 모든 generated CSS를 단일 string으로 concat
4. `maho-storage`에 editor_state JSON serialization/deserialization 추가
5. 새 FFI 함수 추가: `update_editor_state`, `get_compiled_boost`, `update_custom_css`, `update_custom_js`, `add_zap_rule`, `remove_zap_rule`
6. `globally_enabled`/`js_enabled` 영속화 (settings 테이블에 key-value로 저장)
7. Mojo IDL 확장 (BoostEntry에 editor_state/compiled output, 새 메서드)
8. Page handler 구현
9. ~~`app.ts` innerHTML → DOM API 전환~~ **Not needed**: current code already uses DOM API.

**게이트:** `bun run test` + `bun run typecheck` 통과. Mojo 바인딩 빌드 성공. compile_editor_state() unit test 통과.

### Phase 2: JS Injection Path

**목표:** JavaScript Boost가 실제로 페이지에서 실행되도록 한다.

**작업:**

1. `MahoBoostInjectionHandler`에 `InjectBoostJs()` 추가
2. `InjectCompiledBoost()` 통합 메서드: compiled CSS 주입 후 custom_js 주입
3. `DidFinishNavigation()`에서 CSS + JS 모두 주입 (CSS 먼저, JS는 동일 isolated world)
4. JS는 `js_enabled` 플래그가 true일 때만 실행
5. FFI에서 `maho_core_get_compiled_boost_for_url(core, url)` → CSS + JS 반환
6. Bridge 함수 추가

**게이트:** 테스트 페이지에서 JS Boost 실행 확인. console.log 출력 검증.

### Phase 3: Native Anchored Panel + Target Tab Plumbing

**목표:** Arc 스타일 per-site Boost 에디터 패널을 native Views bubble로 구현. Site Control에서 열리는 anchored compact panel이 primary UX. Coordinator가 target WebContents를 직접 page handler에 주입한다.

**작업:**

1. `maho_boost_bubble_coordinator.cc/h` 구현:
   - Owns bubble lifecycle (show/hide/toggle)
   - Captures `content::WebContents* target_contents_` from the active tab at show-time
   - Passes target_contents into `MahoBoostPageHandler::SetTargetWebContents()` after WebUI is ready
   - Observes target tab via `WebContentsObserver`: close panel on tab destruction or cross-origin navigation
   - Entry points: Site Control button, shield bubble "Edit Boost" action, command palette, sidebar coordinator
2. `maho_boost_panel_view.cc/h` 구현:
   - Subclass `LocationBarBubbleDelegateView`
   - Embed `WebUIContentsWrapperT<MahoBoostUI>` with auto-resize enabled
   - Set preferred initial size: 320x480px to prevent layout jank during WebUI load
   - Auto-resize bounds: min 320x400, max 320x640
   - Anchor to Site Control button in location bar
   - Override `GetAnchorView()` for proper attachment
3. `MahoBoostPageHandler` target plumbing:
   - Add `SetTargetWebContents(content::WebContents*)` method
   - Store as `raw_ptr<content::WebContents> target_contents_`
   - All injection/query methods use `target_contents_` when non-null
   - Fallback: if `target_contents_` is null (standalone chrome://maho-boost tab), use URL-based tab-strip lookup (existing `GetTargetWebContents()` path)
4. Editor panel layout (`views/editor_panel.ts`):
   - Color wheel area (placeholder, Phase 4에서 구현)
   - 3-button action row (placeholder)
   - Font preset grid (placeholder, Phase 5에서 구현)
   - Action buttons row: Size, Case, Zap, Code
5. Code button → code editor view (`views/code_editor.ts`):
   - CSS/JS 탭
   - Raw text editing (textarea)
   - Save → `UpdateCustomCss` / `UpdateCustomJs` Mojo call → recompile + reinject via target_contents
6. `chrome://maho-boost` (library surface) 유지: 전체 Boost 목록, 글로벌 토글, 삭제/관리. This path uses `?url=` query param for site context.

**Hosting semantics (critical):**
- **Primary path**: Native `views::Widget` bubble. Coordinator supplies target `content::WebContents*` directly. No URL parsing or tab scanning needed.
- **Secondary path**: Full-tab `chrome://maho-boost`. Uses `?url=` query param + tab-strip scan. Acceptable here because no coordinator exists.

**게이트:** Site Control에서 패널 열림/닫힘. Code 탭에서 CSS 편집 → 저장 → target tab에 즉시 반영. Tab 닫힘 시 panel 자동 닫힘.

### Phase 4: No-Code Color Editor

**목표:** 2-handle color wheel + advanced sliders 구현. Editor state 변경 → CSS 컴파일 → 실시간 주입.

**작업:**

1. `views/color_editor.ts` 구현:
   - 2-handle color wheel with rainbow gradient border
   - Canvas 또는 SVG 기반 원형 picker
   - Handle 드래그 → `ColorEditorState.hue` / `wheel_position` 업데이트
   - 3-button action row (reset, advanced toggle, apply 등. 정확한 기능은 구현 시 결정)
2. `views/color_advanced.ts` 구현:
   - Anchored popover (color_editor.ts에서 trigger)
   - 세 개의 수평 range input: Contrast, Brightness, Original Saturation
   - 값 변경 → `ColorEditorState` 업데이트 → Mojo → compile → reinject
3. CSS compilation for color state:
   ```
   // Advanced sliders → filter
   html { filter: contrast(X) brightness(Y) saturate(Z); }
   
   // Wheel color → selector overrides (근사 구현)
   body { background-color: hsl(H, S%, L%) !important; }
   // ... additional element-specific overrides
   ```
4. Debounce(200ms) 후 injection handler에 재주입 요청

**근사치 주의:** Color wheel의 정확한 출력 형태(어떤 selector에 어떤 color를 적용하는지)는 Arc 비공개. 초기 구현은 body background + filter 조합으로 시작하고, 피드백에 따라 selector 범위를 확장한다.

**게이트:** Wheel 조작 → 페이지 색상 변화. Sliders 조작 → filter 적용. 값 저장 후 새로고침 시 유지.

### Phase 5: No-Code Typography Editor

**목표:** Font preset grid + Size/Case 컨트롤 구현.

**작업:**

1. `views/typography_editor.ts` 구현:
   - Font preset grid: 카드/타일 UI, 각 타일에 "Aa" 샘플을 해당 폰트로 렌더링
   - 선택 시 `TypographyEditorState.font_preset` 업데이트
   - Size control: stepper 또는 slider (0.8x ~ 1.5x multiplier)
   - Case control: Normal / UPPER / lower / Capitalize 토글
2. CSS compilation for typography state:
   ```css
   body, p, h1, h2, h3, h4, h5, h6, span, li, td, th, a {
     font-family: "Inter", sans-serif !important;
   }
   html { font-size: 18px !important; }
   body { text-transform: uppercase !important; }
   ```
3. 초기 프리셋 (소수 실용 세트):
   - System Default
   - Inter
   - Georgia
   - Merriweather
   - SF Mono / Menlo
   - Fira Code
4. Preset data는 JSON/config으로 관리 (향후 확장 용이)

**근사치 주의:** Arc의 정확한 폰트 목록은 비공개. 실용적 소수 프리셋으로 시작하며, 데이터 드리븐으로 추후 확장 가능하게 구현한다.

**게이트:** 폰트 타일 선택 → 페이지 즉시 반영. Size/Case 변경 → 즉시 반영. 저장 후 재방문 시 유지.

### Phase 6: Zap Mode

**목표:** Dimming + yellow highlight + bottom instruction bar + Done button으로 요소를 클릭 숨기기. Panel remains open and usable during Zap interaction with the target page.

**작업:**

1. Zap mode 진입/퇴장 Mojo 메서드 구현 (`EnterZapMode`, `ExitZapMode`)
2. Zap content script 작성 (injection handler에서 주입):
   - 진입 시: 페이지 dimming overlay 추가
   - Mouseover 시 노란색(yellow) 하이라이트
   - 하단 full-width 안내 바 삽입: "Click an element to hide it" + [Done] 버튼
   - 요소 클릭 시: CSS selector 추출 → Mojo로 전송 → `BoostEditorState.zap_rules`에 추가 → recompile → reinject
   - Done 클릭 시: overlay 제거, 안내 바 제거, Zap mode 종료
3. Zap rule compilation: `{selector} { display: none !important; }`
4. Zap rules 관리 UI (editor panel 내 Zap view): 숨긴 요소 목록 + 개별 삭제(복원)
5. CSS selector 추출 전략:
   - 우선순위: `#id` > `[data-*]` > `.class:nth-child(n)` > tag path
   - 가능한 한 specific하되 fragile하지 않은 selector 생성

**Panel Lifecycle/Focus During Zap Mode (critical):**

During Zap mode the user must interact with the target page (hover, click elements) while the Boost panel remains visible and responsive. Standard bubble behavior (`close_on_deactivate_`) would dismiss the panel the moment the user clicks into the target page.

Required focus handling:
- When Zap mode is active, disable `close_on_deactivate_` on the `MahoBoostPanelView` widget (or set `set_close_on_deactivate(false)`)
- The panel must remain pinned/visible while the user interacts with the target page content
- Re-enable normal close-on-deactivate behavior when Zap mode exits (Done button or Escape key)
- Panel `OnWidgetActivationChanged()` override: during Zap mode, suppress the normal `Widget::Close()` path. Log the deactivation but do not close.
- The bottom instruction bar lives in the target page (injected content script), NOT in the panel WebUI. This allows the user to click Done without switching window focus.
- Escape key handler: both in the panel WebUI and in the injected content script should exit Zap mode and restore normal panel close behavior.

Implementation pattern:
```cpp
// In MahoBoostPanelView:
void SetZapModeActive(bool active) {
  zap_mode_active_ = active;
  GetWidget()->SetCanAppearInTaskbar(!active);  // optional
  // Suppress close-on-deactivate during zap
  set_close_on_deactivate(!active);
}
```

**Zap 시각 스펙 (스크린샷 기반):**
- Dimming: `position: fixed; inset: 0; background: rgba(0,0,0,0.5); z-index: 999998; pointer-events: none;`
- Target highlight: `outline: 3px solid yellow; background: rgba(255,255,0,0.15);` (pointer-events 통과)
- Bottom bar: `position: fixed; bottom: 0; left: 0; right: 0; z-index: 999999; background: #1a1a1a; color: white; padding: 12px 20px; display: flex; justify-content: space-between; align-items: center;`
- Done 버튼: 우측 정렬, 밝은 색상

**근사치 주의:** Arc의 "related elements" 자동 감지 및 Undo 서브플로우는 스크린샷에서 확인 불가. 초기 구현은 단일 요소 숨기기만 지원하고, related-elements 감지는 후속 개선으로 분류한다.

**게이트:** Zap mode 진입 → dim + highlight → 클릭 → 요소 사라짐 → Done → 정상 복귀. Panel stays open throughout Zap interaction. 재방문 시 숨김 유지.

### Phase 7: Site Control Entry Point + Command Palette

**목표:** Page info 및 커맨드에서 Boost 패널로 진입.

**작업:**

1. `maho_page_info_ui.cc`에 Boost 진입 섹션 추가:
   - 현재 사이트에 Boost가 있으면: "N Boosts active" + "Edit Boosts" 버튼
   - 없으면: "Create Boost for this site" 버튼
2. 버튼 클릭 → anchored compact panel 열기 (Phase 3)
3. `maho_command_action_handler.cc`에 커맨드 추가:
   - "Edit Boost for this site" → Boost panel 열기
   - "Zap Element" → Zap mode 진입
   - "Toggle Boosts" → globally_enabled 토글

**게이트:** Page info bubble에서 Boost 섹션 표시. 커맨드 팔레트에서 Boost 액션 실행.

---

## 8. Risks

| 리스크 | 영향 | 완화 |
|--------|------|------|
| Isolated world에서 JS 실행 시 페이지 DOM 접근 제한 | JS Boost가 페이지와 상호작용 못 할 수 있음 | main world 실행 옵션 제공 (보안 경고와 함께) |
| Zap selector가 동적 페이지에서 fragile | 재방문 시 숨김 실패 | 여러 selector 전략 조합, fallback 로직 |
| Color wheel → CSS mapping 정확도 부족 | Arc과 시각적 차이 | Iterative tuning, user-facing "approximate" disclaimer |
| Editor state JSON schema 변경 시 migration | 기존 저장된 boost 로드 실패 | Versioned schema + migration logic in deserialization |
| Native panel close-on-deactivate kills Zap UX | User clicks target page → panel closes → Zap mode broken | `set_close_on_deactivate(false)` during Zap mode; restore on exit. See Phase 6 focus handling. |
| Panel preferred size vs WebUI load race | Bubble flashes at 0x0 or wrong size before WebUI renders | Set `SetPreferredSize(320, 480)` at creation; use `EnableAutoResize()` with min/max bounds. See 10.6. |
| Target WebContents lifetime vs panel lifetime | Target tab closes while panel open → dangling pointer | Coordinator observes target via `WebContentsObserver`; closes panel on `WebContentsDestroyed()` |
| Anchored panel WebUI bubble 크기/위치 이슈 | UX 저하 | 기존 maho_location_bar_utility_panel_view 패턴 참조, 크기 제약 설정 |
| 폰트 프리셋의 웹폰트 로딩 | FOUT/FOIT, 로딩 지연 | system font 우선, 웹폰트는 lazy load |
| Tab-strip scan fallback in standalone mode may pick wrong tab | Wrong site boosted in multi-window scenarios | Standalone mode shows explicit site URL in UI; user confirms target. Primary path (native panel) avoids this entirely. |

---

## 9. Verification

### Phase별 게이트 (위 각 Phase 참고)

### 통합 검증

1. **Native panel hosting**: Site Control click → native bubble appears anchored to location bar → WebUI content loads at preferred size without jank → panel is NOT a tab in the tab strip
2. **Target tab plumbing**: Coordinator passes target WebContents directly → page handler uses it for injection → no URL parsing on primary path
3. **Editor state flow**: Panel 열기 → color wheel 조작 → compile → CSS 주입 via target_contents → 페이지 반영 → 저장 → 재방문 시 유지
4. **Code tab flow**: Code 버튼 → CSS/JS textarea 편집 → 저장 → custom CSS/JS 주입 → 재방문 유지
5. **JS flow**: Code tab JS 편집 → 저장 → 페이지 새로고침 → JS 실행 확인
6. **Color flow**: Wheel 조작 + sliders → 실시간 반영 → 저장 → 재방문 유지
7. **Typography flow**: Preset 선택 + Size/Case → 즉시 반영 → 저장 → 재방문 유지
8. **Zap flow**: Zap 진입 → panel stays open → dim + highlight → 클릭 target page → panel not dismissed → 요소 사라짐 → Done → panel resumes normal close behavior → 재방문 시 숨김 유지
9. **Panel lifecycle**: Target tab closes → panel auto-closes. Tab navigates cross-origin → panel auto-closes.
10. **Global toggle**: 글로벌 비활성화 → 모든 Boost 미적용 → 재활성화 → 복원
11. **Compilation correctness**: editor state 변경 → compiled CSS에 올바른 rules 포함 확인
12. **Standalone fallback**: Open `chrome://maho-boost` in a tab → library view works → `?url=` param resolves to correct tab via scan

### Build 검증

```bash
bun run typecheck   # Rust workspace
bun run test        # Rust workspace
python3 maho-chromium/build/scripts/build_maho.py  # Full Chromium build
```

---

## 10. Implementation Notes

### 10.1 Editor State Persistence

`BoostEditorState`는 JSON으로 serialize되어 storage에 저장된다:
- `maho-storage`의 boosts 테이블에 `editor_state TEXT` column 추가
- Serde JSON serialization/deserialization
- Schema versioning: `"_version": 1` field 포함, migration 로직 in deserialize

### 10.2 Compilation Pipeline Details

`BoostManager::compile_editor_state()` 구현:

```rust
fn compile_editor_state(state: &BoostEditorState) -> CompiledBoost {
    let mut css_parts: Vec<String> = Vec::new();
    
    // 1. Color editor → CSS
    if let Some(ref color) = state.color_editor.as_active() {
        css_parts.push(compile_color_state(color));
    }
    
    // 2. Typography editor → CSS
    if let Some(ref typo) = state.typography_editor.as_active() {
        css_parts.push(compile_typography_state(typo));
    }
    
    // 3. Zap rules → CSS
    for rule in &state.zap_rules {
        css_parts.push(format!("{} {{ display: none !important; }}", rule.selector));
    }
    
    // 4. Custom CSS (pass-through)
    if let Some(ref custom) = state.custom_css {
        css_parts.push(custom.clone());
    }
    
    CompiledBoost {
        css: css_parts.join("\n"),
        js: state.custom_js.clone(),
    }
}
```

### 10.3 No-Code vs Code: Clear Separation

- **No-code controls** (color wheel, sliders, font grid, Size, Case, Zap): 사용자가 UI를 조작하면 `BoostEditorState`가 업데이트되고, compilation pipeline이 CSS를 생성.
- **Code tab** (CSS/JS textarea): 사용자가 직접 raw CSS/JS를 작성. `custom_css`/`custom_js` 필드에 저장. 컴파일 없이 as-is로 주입.
- Generated CSS와 custom CSS는 순서대로 concat되어 주입. Custom CSS는 마지막이므로 generated rules을 override 가능.

### 10.4 Injection 순서

1. Generated CSS from color_editor state (filter + selector overrides)
2. Generated CSS from typography_editor state (font, size, case)
3. Generated CSS from zap_rules (`display: none !important`)
4. Custom CSS (user-authored via Code tab, as-is)
5. Custom JS (user-authored via Code tab, separate `<script>` execution)

### 10.5 Zap Selector 추출 알고리즘

```
1. element.id가 있으면 → `#${id}`
2. 고유한 data-* attribute가 있으면 → `[data-xxx="value"]`
3. 고유한 class 조합이 있으면 → `.class1.class2`
4. 위 모두 실패 시 → nth-child path 생성
   예: `body > div:nth-child(2) > section > article:nth-child(3)`
```

### 10.6 Hosting Architecture (Oracle-Reviewed)

**Primary UX: Native Chromium Views-hosted anchored panel (NOT a browser tab)**

The primary Boost editor surface is a native `views::Widget` anchored bubble/panel attached to the Site Control area, with embedded WebUI content. This follows the `LocationBarBubbleDelegateView` / `BubbleDialogDelegate` / `WebUIContentsWrapperT<MahoBoostUI>` pattern already proven in the repo (shield bubble, AI side panel, utility panel).

Key characteristics:
- Native Chromium Views hosting, not a tab in the tab strip
- Anchored to the Site Control button in the location bar
- Embeds `chrome://maho-boost` WebUI content via `WebUIContentsWrapperT<MahoBoostUI>`
- The native coordinator passes the actual target `content::WebContents*` directly into the Boost page handler at panel creation time
- **No `?url=` query param or tab-strip scanning as the primary targeting mechanism.** The coordinator holds a reference to the active tab's WebContents and injects it into the handler. This avoids race conditions from tab switching, URL navigation, and multi-window ambiguity.
- The `?url=` / `GetTargetWebContents()` tab-strip scan path remains ONLY as a fallback for the standalone full-tab `chrome://maho-boost` library/manager surface (secondary UX), where no native coordinator exists to supply the WebContents pointer.

Repo-proven host references to follow:
- `browser/ui/views/location_bar/maho_location_bar_utility_panel_view.*` (anchored panel pattern)
- `browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.*` (coordinator lifecycle)
- `browser/ui/views/shields/maho_shield_bubble_view.*` (bubble with WebUI content)
- `browser/ui/views/side_panel/maho_ai_side_panel_coordinator.cc` (WebUI wrapper integration)

**Preferred panel sizing (layout jank prevention):**
- Width: 320px fixed (matches Arc compact editor width)
- Height: set a preferred minimum of 480px at creation; let WebUI content request final height via `ResizeDueToAutoResize()` once loaded
- Set `SetPreferredSize()` on the `WebUIContentsWrapperT` so the bubble doesn't flash at 0x0 during WebUI initialization
- The WebView's `EnableAutoResize()` bounds: min 320x400, max 320x640

**Secondary UX: chrome://maho-boost (full-tab library/manager)**
- 전체 Boost 목록 조회
- 글로벌 on/off 토글
- JS 실행 on/off 토글
- Boost 삭제/이름 변경
- 전체 페이지 WebUI (기존 패턴 유지)
- In this context only, `?url=` query param + tab-strip scan is acceptable for determining target site context (no native coordinator available)

### 10.7 Global/JS 플래그 영속화

`maho-storage`의 settings 테이블에 key-value로 저장:
- `boost_globally_enabled` → bool
- `boost_js_enabled` → bool

`MahoCore` 초기화 시 로드, 변경 시 즉시 persist.

### 10.8 Trusted Types Status

**Resolved.** Investigation confirms the current `app.ts` does NOT use `innerHTML`. The original audit note was stale. No Trusted Types violation exists in the Boost WebUI code. New panel/editor TypeScript files should continue using DOM API methods (`createElement`, `appendChild`, `textContent`) to maintain compliance.

---

## 11. Dependencies Between Phases

```
Phase 1 (Foundation: Model + Pipeline) ─── 필수 선행 ───┬─── Phase 2 (JS Injection)
                                                         ├─── Phase 3 (Anchored Panel)
                                                         ├─── Phase 4 (Color Editor)
                                                         └─── Phase 5 (Typography Editor)

Phase 2 ─── Phase 3에 Code tab JS 기능 추가
Phase 3 ─── Phase 4, 5, 6의 호스팅 컨테이너 제공
Phase 6 (Zap) ─── Phase 1 이후 독립 진행 가능 (injection handler 확장)
Phase 7 (Site Control entry) ─── Phase 3 이후 (panel이 있어야 진입점 의미)
```

**안전한 병렬 트랙 (Phase 1 완료 후):**
- Track A: Phase 2 (JS) + Phase 3 (Panel UI)
- Track B: Phase 4 (Color) — editor state model은 Phase 1에서 완료, UI만 남음
- Track C: Phase 6 (Zap) — injection handler 확장은 독립적

**직렬 게이트:**
- Phase 1 → 모든 후속 Phase의 선행 조건
- Phase 3 → Phase 7의 선행 조건 (panel 존재 필요)
