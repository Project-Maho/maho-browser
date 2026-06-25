# Maho Welcome — 5 UX/Functional Issues Plan

**Created**: 2026-06-14 ~00:30 JST
**Source**: User-driven QA after restructure + alignment fixes
**Status**: Plan-only. Each issue needs investigation + fix.

---

## Issue 1: Skip vs Back 비주얼 불일치 (FIX)

### 현재 상태
```css
/* 시각적으로 다른 두 버튼 */
.welcome-button {                           /* Back 사용 */
  border: 1px solid var(--color-sys-outline);
  border-radius: 8px;
  padding: 10px 20px;
  background: transparent;
}

.welcome-button--skip {                     /* Skip 사용 */
  border: none;
  opacity: 0.5;
  padding: 0;
}
```

→ **Skip = 단순 dim 텍스트 / Back = 박스 버튼** — 같은 secondary 액션인데 시각 위계 다름

### Fix 옵션

**A. 둘 다 text-only로 (추천)** — 미니멀, 어울리는 패턴
```css
.welcome-button--skip,
.welcome-button--back {
  border: none;
  background: transparent;
  opacity: 0.5;
  padding: 0;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
}

.welcome-button--skip:hover,
.welcome-button--back:hover {
  opacity: 0.8;
}
```

**B. 둘 다 button-style로** — Back처럼 둘 다 박스 버튼

**추천 = A** (좌측 사이드바 nav가 미니멀해야 우측 primary가 부각됨)

**Effort**: 5분 (CSS 1 block)

---

## Issue 2: 서치엔진 선택 피드백 없음 (FIX)

### 현재 상태
`search-engine.tsx:67-70`:
```tsx
<button
  className={`welcome-engine-card${engine.keyword === selectedEngine ? ' is-selected' : ''}`}
  aria-pressed={engine.keyword === selectedEngine}
  onClick={() => onSelect(engine.keyword)}>
```

CSS `.welcome-engine-card.is-selected` 존재함 (border-color, scale, box-shadow).

### 진단 필요
- 클릭 시 `onSelect` 정상 호출되나?
- `selectedEngine` state 업데이트 되나?
- Default selected engine 표시 잘 되나? (스크린샷에서 Google이 indigo border로 selected 표시됨)

### Hypotheses

| H | 가설 | 검증 |
|---|---|---|
| H1 | 선택 state 업데이트 안 됨 (onSelect 동작 X) | CDP click 후 store 상태 점검 |
| H2 | 시각 효과가 너무 미세함 (scale 1.03 + border) | CSS에서 강조 효과 추가 |
| H3 | onSelect는 동작하지만 Mojo `setDefaultSearchEngine` 호출 안 됨 | Mojo trace |

### Fix 후보
- **A**: 진단으로 H1/H2/H3 판정 → 해당 코드 fix
- **B**: 시각 강조 강화 (scale 1.03 → 1.05, border 더 두껍게, checkmark 아이콘 표시)

```css
.welcome-engine-card.is-selected {
  border: 3px solid var(--maho-primary-color);   /* 2 → 3px */
  scale: 1.06;                                    /* 1.03 → 1.06 */
  box-shadow: 0 4px 16px rgba(91, 79, 207, 0.25); /* indigo 그림자 */
}

.welcome-engine-card.is-selected::after {
  content: '✓';
  position: absolute;
  top: 8px;
  right: 8px;
  width: 24px;
  height: 24px;
  background: var(--maho-primary-color);
  color: white;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: bold;
}
```

**Effort**: 10-20분 (진단 + CSS)

---

## Issue 3: Essentials 페이지 의도 불명 (FIX)

### 현재 상태
- 헤딩: "Your Key Tabs, Always Within Reach"
- 본문: "Keep your most important tabs easily accessible..." / "Essential tabs are always visible..."
- 우측: mock browser preview에 9개 앱 아이콘 grid

→ 사용자: "이게 뭘 설정 하는 건지 모르겠음"

### 문제
- "Key tabs" / "essential tabs" 추상적 — 사용자가 핀하는 행위인지, 미리보기인지, 구독 관리인지 불명
- 9개 아이콘 (Obsidian/Discord/Trello/Slack/GitHub/Tuta/Notion/Calendar/Figma)이 왜 이거인지, 클릭해서 뭘 하는지 affordance 없음
- 각 타일에 hover 시 보임? 클릭 시 선택 표시? 모름

### Fix 후보

**A — 카피 명확화 (필수)**:
```
헤딩: "Pin Your Essential Apps" (또는 "Your Always-Available Apps")
본문 1: "Pick the apps you use every day. They'll be pinned to your sidebar so they're always one click away — across every workspace."
본문 2: "(선택사항 — 나중에 사이드바에서 추가/제거 가능)"
```

**B — 시각 affordance 추가**:
- 각 타일에 hover 시 "Pin" 텍스트 또는 + 아이콘 오버레이
- 선택된 타일에 명확한 selected state (체크마크, indigo background tint)
- 사이드바에 "Selected: 0/9" 같은 카운터

**C — interaction model 명확화**:
- 첫 진입 시 모든 타일 선택 상태 (default)
- 사용자가 빼고 싶은 거 클릭해서 해제
- 또는 모두 unselected, 원하는 거만 선택

### 추천
- A (copy fix) — 5분
- B (selected state 강조) — 10분
- C (default selection 정책) — 디자인 결정 필요

**Effort**: 15-30분

---

## Issue 4: Theme Picker 별도 윈도우 vs 통합

### 현재 동작
"Pick a Theme" 클릭 → `MahoSpaceThemePickerDialog::Open()` → **별도 native dialog window** 열림 (production path와 같은 dialog)

→ 사용자: "별도 윈도우로 작동하는게 어색함, 통합 가능한지?"

### 답: **YES, 통합 가능. 하지만 tradeoff 있음.**

#### Option A: D2 그대로 (현재 — separate dialog)
- ✅ ADR 준수 (`chrome://maho-space-create` 단일 production surface)
- ✅ 코드 중복 없음
- ❌ 별도 윈도우 = onboarding 흐름 끊김

#### Option B: 인라인 React 테마 picker (통합)
- 우측 콘텐츠 pane 안에 테마 선택 UI 직접 렌더
- Color swatches, gradient picker, mode switcher를 React 컴포넌트로 재구현
- 통합 = 매끄러운 onboarding
- ❌ ADR ambiguous: "Second production WebUI surface" 명시 금지인데, 이건 production WebUI는 아니지만 onboarding-only React inline picker
- ❌ 코드 중복: chrome://maho-space-create의 React 로직 일부 복사 필요
- ⚠️ Effort: ~4-8 hours (gradient 알고리즘, swatch 데이터, theme JSON spec 재현)

#### Option C: chrome-untrusted:// scheme 마이그레이션 (B1 from 이전 plan)
- chrome://maho-space-create → chrome-untrusted://maho-space-create 스킴 변경
- iframe 직접 embed 가능 (Mojo bindings도 동작)
- ❌ ADR 위배 가능성 ("permanently chrome://maho-space-create")
- Effort: ~2-3hr

#### Option D: Onboarding Theme step 자체 삭제 (E from 이전 plan)
- Theme step 제거 → 사용자가 사이드바 "+" 메뉴에서 나중에 선택
- Effort: ~30분 (step 제거 + page count 조정)
- ❌ Zen-parity 잃음

### 추천: **Option B (인라인 React picker) 또는 Option D (제거)**

Option B 강점:
- 사용자 의도(통합)에 가장 부합
- ADR 텍스트적으로 "production WebUI surface" 별도 신설 아님 (welcome 안에 내장된 React 컴포넌트)
- 단일 onboarding 흐름

Option D 강점:
- 가장 빠름
- 복잡도 제거
- 사이드바 "Choose a Theme"가 실제 production 진입점이므로 onboarding에서 굳이 강제할 필요 X

**선택 필요**: B vs D? (사용자 결정)

**Effort**: B = 4-8hr / D = 30min

---

## Issue 5: Dive in! 크래시 (BLOCKING)

### 흐름
```
Click "Dive in!" 
→ onPrimaryClick (app.tsx)
→ store.complete() OR runCompletionSequence (completion.tsx)
   1. animate #welcome-content slide (500ms)
   2. animate #welcome-heart pulse (1500ms)
   3. favoriteSelectedEssentials()
   4. applyTheme(selectedTheme)
   5. animate #welcome-pages fade (400ms)
   6. store.openMainBrowser()
      → pageHandler.finishOnboarding() [Mojo]
         → MahoWelcomePageHandler::FinishOnboarding (page_handler.cc:405)
            1. is_finishing_ = true
            2. SetBoolean(kWelcomeCompleted, true)
            3. ClearPreviewOverride + UpdateFromCore + RepaintBrowserAfterThemeChange
            4. Browser::Create(profile_, true) + window->Show()
            5. MahoNotificationOverlay::Show (toast)
            6. CloseWelcomeTab() → TabStripModel::CloseWebContentsAt
```

### 의심 지점

| 단계 | 가능한 크래시 원인 |
|---|---|
| 4 (Browser::Create) | profile_ stale pointer, Browser::CreationStatus::kError |
| 5 (NotificationOverlay) | overlay null deref, l10n 문자열 unresolved |
| 6 (CloseWelcomeTab) | App-mode Browser의 TabStripModel 동작 / closing 전체 Browser 트리거 → page_handler 객체 destruct → Mojo callback after delete = use-after-free |

### Hypotheses

| H | 가설 | 진단 |
|---|---|---|
| **H1** | App-mode Browser closing 시 page_handler 객체 destruct → Mojo connection 끊김 → callback 처리 중 use-after-free | LOG 추가 + 크래시 dump 분석 |
| **H2** | `Browser::Create` 새 normal browser 생성 시 maho-core 초기화 충돌 (welcome+main 동시 활성) | LOG before/after Browser::Create |
| **H3** | `applyTheme` Mojo 호출이 RepaintBrowserAfterThemeChange와 race | Theme apply 시점 분리 |
| **H4** | NotificationOverlay null pointer (browser_view 없으면) | bv null check 이미 있음, 다른 deref 점검 |

### 진단 패치

`page_handler.cc:405` FinishOnboarding에 LOG 추가:
```cpp
void MahoWelcomePageHandler::FinishOnboarding() {
  LOG(WARNING) << "[Maho] FinishOnboarding ENTER, is_finishing=" << is_finishing_;
  if (is_finishing_) return;
  is_finishing_ = true;

  LOG(WARNING) << "[Maho] step1: SetPref kWelcomeCompleted";
  profile_->GetPrefs()->SetBoolean(maho::welcome::kWelcomeCompleted, true);

  LOG(WARNING) << "[Maho] step2: theme cleanup";
  if (MahoSpaceThemeState::HasPreviewOverride()) { ... }

  LOG(WARNING) << "[Maho] step3: Browser::Create";
  if (browser_ && profile_) {
    Browser::CreateParams params(profile_, true);
    Browser* main_browser = Browser::Create(params);
    LOG(WARNING) << "[Maho] step3b: main_browser=" << main_browser;
    main_browser->window()->Show();
    LOG(WARNING) << "[Maho] step4: window shown";
    
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(main_browser);
    LOG(WARNING) << "[Maho] step4b: bv=" << bv;
    ...
  }

  LOG(WARNING) << "[Maho] step5: CloseWelcomeTab";
  CloseWelcomeTab();
  LOG(WARNING) << "[Maho] FinishOnboarding EXIT (returned)";
}
```

### Fix 후보 (가설별)

**H1 (use-after-free in CloseWelcomeTab)**:
- `CloseWelcomeTab`을 `PostTask`로 비동기 처리:
  ```cpp
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MahoWelcomePageHandler::CloseWelcomeTab, weak_factory_.GetWeakPtr()));
  ```
- 또는 Mojo 호출 완료 후 next tick에 close

**H2 (Browser::Create 충돌)**:
- maho-core 초기화 점검
- welcome browser와 main browser가 같은 profile 공유하므로 maho-core 인스턴스 검증

**H3 (theme apply race)**:
- Theme apply를 Browser::Create 이전으로 옮김
- Or: animation chain에서 applyTheme 분리

**H4 (NotificationOverlay)**:
- 추가 null check
- IDS 문자열 검증

### Effort
- 진단 LOG: 10분
- 빌드 + 크래시 재현 + 로그 분석: 20분
- Fix 적용: 가설별 15-60분
- 총 ~45-90분

---

## Wave Structure (전체 5개)

```
Wave G-1 (BLOCKING — Dive in 크래시): 약 60-90min
├── G-1a: 진단 LOG 추가 → apply_chromium_src_overrides.py 패치 또는 직접 page_handler.cc
├── G-1b: 빌드 + Maho 실행 + Dive in 클릭 → 크래시 재현 + 로그 수집
├── G-1c: 가설 판정 → 해당 fix 적용
└── G-1d: 빌드 + 검증 (크래시 안 발생 확인)

Wave G-2 (Skip/Back visual): 5min
└── welcome.css에 .welcome-button--back 추가 (Skip과 동일 스타일)

Wave G-3 (Search engine feedback): 20min
├── G-3a: CDP click 진단 — selectedEngine state 업데이트 확인
└── G-3b: 시각 강조 (scale 1.06 + 두꺼운 border + checkmark)

Wave G-4 (Essentials clarity): 15min
├── G-4a: page_handler.cc IDS 카피 수정
└── G-4b: tile selected state CSS 강조

Wave G-5 (Theme picker integration): 사용자 결정 + 실행 30min ~ 8hr
├── 옵션 결정: B (인라인 React) vs D (Theme step 제거) vs A (현상 유지)
└── 선택된 옵션 실행

총 시간: 70min ~ 9hr (크게 G-5 결정에 따라 변동)
```

---

## 사용자 결정 필요

| 결정 | 옵션 |
|---|---|
| **G-2 Skip/Back style** | A (text-only 둘 다) / B (button 둘 다) — 추천: A |
| **G-3 Selected feedback** | 진단 후 fix vs 시각 강조 즉시 vs 둘 다 |
| **G-4 Essentials copy** | 새 카피 채택 vs 기존 유지 |
| **G-5 Theme integration** | A (separate dialog 유지) / B (인라인 React) / D (step 제거) |

---

## Approval Gate

**STATUS: pending user 결정 + GO signal.**

- `GO G-1` (크래시 fix BLOCKING)
- `GO G-2` (Skip/Back visual)
- `GO G-3` (Search feedback)
- `GO G-4` (Essentials)
- `GO G-5: B` 또는 `GO G-5: D` (Theme)
- `GO ALL` (sequential 전체)
- `MODIFY` (plan 수정)
