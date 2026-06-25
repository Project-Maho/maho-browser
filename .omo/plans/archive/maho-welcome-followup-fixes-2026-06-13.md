# Maho Welcome — 3 Follow-up Fixes (Restructure 후속)

**Created**: 2026-06-13 ~14:55 JST
**Source**: User code review of restructure implementation discovered 3 minor issues
**Reference**: `.sisyphus/plans/maho-welcome-layout-restructure-2026-06-13.md` (restructure plan v1, 11/11 spec items implemented + browser/profile selector 추가 기능)
**Status**: Plan-only. Each fix has separate complexity/risk profile.

---

## Issue 1: Welcome 페이지 자동 안 열림 (회귀)

### Symptom
Maho 첫 실행 시 `chrome://maho-welcome/` 자동 navigate 안 됨. 수동으로 `curl -X PUT http://localhost:9333/json/new?chrome://maho-welcome/` 으로만 열림.

### Code state (verified)
**Patch는 정상 적용됨** — `chromium/src/chrome/browser/ui/startup/startup_browser_creator_impl.cc:733-754`:
```cpp
// Maho welcome: on first run, open onboarding as a chromeless popup.
if (tabs.empty()) {
  PrefService* prefs = profile_->GetPrefs();
  if (prefs && !prefs->GetBoolean("maho.welcome.completed")) {
    gfx::Rect screen_rect =
        display::Screen::Get()->GetPrimaryDisplay().work_area();
    Browser::CreateParams app_params =
        Browser::CreateParams::CreateForApp(
            "maho-welcome", true,
            screen_rect, profile_, true);
    app_params.creation_source = Browser::CreationSource::kStartupCreator;
    Browser* welcome_browser = Browser::Create(app_params);
    chrome::AddTabAt(welcome_browser,
                     GURL("chrome://maho-welcome/"), -1, true);
    welcome_browser->window()->Show();
    return welcome_browser;
  }
}
```

조건 2개:
1. `tabs.empty()` — startup tabs 비어 있어야 함
2. `!prefs->GetBoolean("maho.welcome.completed")` — pref가 false (첫 실행)

### Hypotheses (3가지)

| H | 가설 | 검증 방법 |
|---|---|---|
| **H1** | `tabs.empty()` 가 false → maho-core LMDB가 fresh profile에서도 default tab 생성 | `LOG` 로 tabs.size() 출력 |
| **H2** | `--no-first-run` 플래그가 이 codepath skip / 영향 | 플래그 빼고 launch 검증 |
| **H3** | pref가 어딘가에서 자동 set 됨 | `kWelcomeCompleted` 호출처 grep + log |

### Fix Wave R1 (진단 → fix)

**R1a — 진단 로깅 추가** (`apply_chromium_src_overrides.py`에 patch 추가):

`startup_browser_creator_impl.cc:733` 위치의 기존 patch 수정. 현재 raw 조건만 있음. 진단 로그 추가:
```cpp
// Maho welcome: on first run, open onboarding as a chromeless popup.
if (tabs.empty()) {
  PrefService* prefs = profile_->GetPrefs();
  bool completed = prefs ? prefs->GetBoolean("maho.welcome.completed") : true;
  LOG(WARNING) << "[MahoWelcome] tabs.empty() entered, completed=" << completed
               << ", profile=" << (profile_ ? profile_->GetPath().value() : "null");
  if (prefs && !completed) {
    LOG(WARNING) << "[MahoWelcome] OPENING welcome popup";
    // ... existing block
  }
} else {
  LOG(WARNING) << "[MahoWelcome] tabs NOT empty, count=" << tabs.size();
  for (size_t i = 0; i < std::min<size_t>(tabs.size(), 5); ++i) {
    LOG(WARNING) << "[MahoWelcome]   tab[" << i << "].url=" << tabs[i].url;
  }
}
```

**R1b — 빌드 + launch + 로그 분석**:
```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome
# Launch WITHOUT --no-first-run (H2 검증)
"chromium/src/out/Default/Maho.app/Contents/MacOS/Maho" \
  --user-data-dir=/tmp/maho-test-h2 \
  --remote-debugging-port=9333 \
  --enable-logging --v=1 \
  2>&1 | grep MahoWelcome
```

**R1c — Hypothesis별 fix**:

| H 판정 | Fix |
|---|---|
| H1 (tabs not empty, contains LMDB-restored tab) | 조건 완화: `tabs.empty() \|\| (tabs.size() == 1 && tabs[0].url.IsEmpty())` 또는 `kWelcomeCompleted` 체크를 우선 |
| H2 (`--no-first-run` 영향) | `is_first_run` 체크 추가 OR welcome 트리거를 다른 callsite로 (예: `BrowserListObserver::OnBrowserAdded`) |
| H3 (pref 자동 set 됨) | `kWelcomeCompleted` write 호출처 검사 + 잘못된 set 제거 |

### Acceptance criteria

`pkill Maho; rm -rf /tmp/test-profile; mkdir -p /tmp/test-profile` 후 launch:
```bash
"chromium/src/out/Default/Maho.app/Contents/MacOS/Maho" \
  --user-data-dir=/tmp/test-profile \
  --remote-debugging-port=9333
```
→ `curl -s http://localhost:9333/json | jq '.[].url'` 결과에 `chrome://maho-welcome/` 자동 등장.

### Effort
- 진단 로깅: 10min
- 빌드 + 검증: 15min
- Fix 적용: 15-30min (가설별)
- 총 ~45-60min

---

## Issue 2: S6 Theme picker 두 인디고 버튼 visual hierarchy

### Symptom
Space Theme 화면 우측에 두 indigo 버튼:
- 가운데: "Pick a Theme" (`welcome-theme-picker-button`)
- 우측 하단: "Next" (`welcome-button--primary`)

둘 다 `var(--maho-primary-color)` solid indigo → 어느 게 진행 버튼인지 모호.

### Root cause
`welcome.css:1141-1152`:
```css
.welcome-theme-picker-button {
  background: var(--maho-primary-color);  /* primary와 동일 */
  color: white;
  border: none;
  /* ... */
}
```

### Fix — Secondary (outline) style

**File**: `welcome.css`
**Line**: ~1141-1156 (replace existing rule)

```css
/* BEFORE */
.welcome-theme-picker-button {
  padding: 0.75rem 1.5rem;
  background: var(--maho-primary-color);
  color: white;
  border: none;
  border-radius: var(--maho-button-border-radius);
  font-weight: 500;
  cursor: pointer;
  font-family: inherit;
  font-size: 1rem;
  transition: opacity 150ms;
}

.welcome-theme-picker-button:hover {
  opacity: 0.9;
}

/* AFTER */
.welcome-theme-picker-button {
  padding: 0.75rem 1.5rem;
  background: transparent;
  color: var(--maho-primary-color);
  border: 2px solid var(--maho-primary-color);
  border-radius: var(--maho-button-border-radius);
  font-weight: 500;
  cursor: pointer;
  font-family: inherit;
  font-size: 1rem;
  transition: background 150ms, color 150ms;
}

.welcome-theme-picker-button:hover {
  background: var(--maho-primary-color);
  color: white;
}
```

### Visual hierarchy after fix

| 위치 | 버튼 | Style | 역할 |
|---|---|---|---|
| 우측 가운데 | "Pick a Theme" | **Outline** (transparent + indigo border) | Secondary (테마 선택 dialog 열기) |
| 우측 하단 | "Next" | **Filled** indigo | Primary (wizard 진행) |

→ 클릭 우선순위 명확. "Pick a Theme"는 선택 행동, "Next"는 진행 행동.

### Acceptance

`look_at` (or `read PNG`) `screen-6-space-theme.png`:
- "Pick a Theme" 버튼이 transparent background + indigo border + indigo text
- Hover 시 indigo fill로 채워짐
- "Next" 버튼은 변함없이 solid indigo
- 두 버튼 시각적으로 명백히 다름

### Effort
- CSS 수정: 2min
- 빌드 + 캡처 + 검증: 8min
- 총 ~10min

### Risk
NONE — pure CSS change, no functional impact.

---

## Issue 3: S5 Essentials mock browser bottom row 잘림

### Symptom
S5 Essentials 화면 우측의 mock browser preview에서 9개 essential 아이콘 중 bottom row (Notion, Calendar, Figma) 가 mock browser frame 하단에서 잘림.

### Root cause
`welcome.css`:
- `.welcome-essentials-mock__container { width: 70%; height: 80% }` — 우측 pane의 70x80% 차지
- `.welcome-essentials-mock__grid { grid-template-columns: repeat(3, 1fr); gap: 1rem }`
- `.welcome-essentials-tile { min-height: 6rem (96px); min-width: 5rem (80px) }`
- `.welcome-essentials-mock__extra { grid-column: span 3; height: 4rem (64px) }` — 추가 placeholder 바

**Total grid height** (3 rows × 96px tiles + 2 gaps × 16px + extra 64px + 16px gap + padding 22.4px×2 = ~400px+).
**Mock browser height** (60% shell × 80% mock × ~520-780px clamp): 250-374px.
→ ~50-150px overflow → bottom row 잘림.

### Fix — 3 옵션 (복잡도 순)

#### Option A (추천) — `__extra` placeholder bar 제거 (5분)

**File**: `welcome.css`
```css
/* AFTER — set display:none (또는 라인 제거) */
.welcome-essentials-mock__extra {
  display: none;
}
```

→ 64px + 16px gap = ~80px 공간 확보. 9 tiles 모두 보임.

**Justification**: `__extra` placeholder bar는 시각적 의미 약함 (비어있는 회색 바). 제거해도 화면 정보 손실 없음.

#### Option B — Tile 사이즈 축소 (10분)

**File**: `welcome.css`
```css
.welcome-essentials-tile {
  min-height: 4.5rem;    /* 6rem → 4.5rem (96px → 72px) */
  min-width: 4rem;       /* 5rem → 4rem (80px → 64px) */
}
.welcome-essentials-tile__icon {
  width: 1.5rem;         /* 2rem → 1.5rem (32px → 24px) */
  height: 1.5rem;
}
.welcome-essentials-tile::after {
  width: 1.4rem;         /* 1.8rem → 1.4rem */
  height: 1.4rem;
}
```

→ tiles 24px 작아짐. 모든 9 tiles + extra bar fit.

**Tradeoff**: 아이콘 작아져서 시각적 임팩트 약간 감소.

#### Option C — 4-col grid로 변경 (15분)

**File**: `welcome.css`
```css
.welcome-essentials-mock__grid {
  grid-template-columns: repeat(4, 1fr);  /* 3 → 4 */
}
```

**File**: `essentials.tsx` 또는 page_handler에서 8개로 제한:
```tsx
{essentialSites.slice(0, 8).map(...)}
```

→ 2 rows × 4 = 8 tiles. 9번째 essential 안 보임.

**Tradeoff**: essential 1개 누락 (사용자가 9개 중 8개 선택만 가능).

### 추천 채택안: **Option A**

이유:
- 가장 빠름 (1줄 CSS)
- 모든 9 essentials 보임 (Option C는 손실)
- 아이콘 크기 유지 (Option B는 작아짐)
- `__extra` placeholder bar는 정보 가치 없음

### Acceptance

`screen-5-essentials.png` PNG `read tool`로 확인:
- 9개 essential 아이콘 모두 시각적으로 보임 (Obsidian, Discord, Trello / Slack, GitHub, Tuta / Notion, Calendar, Figma)
- Mock browser bottom 잘림 없음
- 다른 화면 영향 없음 (regression check)

### Effort
- CSS 수정 (Option A): 1min
- 빌드 + 캡처 + 검증: 5min
- 총 ~6min

### Risk
LOW — pure CSS, `__extra` 제거가 다른 placeholder/animation 의존성 없는지 확인 필요.

---

## Wave Structure

```
Wave F-1 (Welcome auto-open) — 가장 복잡
├── F-1a: 진단 로깅 patch (apply_chromium_src_overrides.py)
├── F-1b: 빌드 + launch (without --no-first-run for H2 test)
├── F-1c: 로그 분석 → 가설 판정
├── F-1d: 가설별 fix 적용
└── F-1e: 빌드 + 검증 (fresh profile에서 welcome 자동 열림)
   ETA: ~45-60min

Wave F-2 (Theme picker hierarchy) — 즉시 fix 가능
├── F-2a: welcome.css 1141-1152 outline 스타일로 변경
├── F-2b: 빌드 + 캡처 + read PNG 검증
   ETA: ~10min

Wave F-3 (Essentials bottom cutoff) — 즉시 fix 가능
├── F-3a: welcome.css `.welcome-essentials-mock__extra { display: none }`
├── F-3b: 빌드 + 캡처 + read PNG 검증 (9 tiles 모두 보임 확인)
   ETA: ~6min

Wave F-Final
├── 통합 캡처 (1280×720 + 1920×1080)
├── 7장 모두 read PNG로 확인
└── 회귀 없음 검증
   ETA: ~10min

총 ETA: ~70-86min sequential
F-2 + F-3은 독립적이므로 병렬 가능 (약 5min 절약)
```

---

## Approval Gate

**STATUS: pending user GO signal per wave.**

옵션:
- `GO F-2` — Theme picker hierarchy (즉시, 안전)
- `GO F-3` — Essentials cutoff (즉시, 안전)
- `GO F-2+F-3` — 둘 다 동시 (~15min)
- `GO F-1` — Welcome auto-open 진단 시작 (~45-60min, 가설 검증 필요)
- `GO ALL` — sequential F-1 → F-2 → F-3 → F-Final (~80min)
- `MODIFY` — plan 수정

**No execution starts until user GO signal received.**

## Cross-References

- Restructure plan: `.sisyphus/plans/maho-welcome-layout-restructure-2026-06-13.md` (1100+ lines, 11/11 spec implemented + browser/profile selector 추가 기능)
- Defer plan (DG-1~15 from earlier session): `.sisyphus/plans/maho-welcome-functional-gaps-defer-2026-06-12.md`
- Visual fix plan v4 (origin): `.sisyphus/plans/maho-welcome-7-bug-visual-fix-2026-06-12.md`

## Files Touched (총 3 source + 1 build script)

| Issue | File | Lines |
|---|---|---|
| F-1 | `maho-chromium/build/scripts/apply_chromium_src_overrides.py` | startup_browser_creator_impl.cc patch에 LOG 추가 |
| F-1 (fix 후) | `chromium/src/chrome/browser/ui/startup/startup_browser_creator_impl.cc` | 가설별 조건 수정 |
| F-2 | `maho-chromium/browser/resources/maho_welcome/welcome.css` | ~1141-1156 (theme-picker-button) |
| F-3 | `maho-chromium/browser/resources/maho_welcome/welcome.css` | `.welcome-essentials-mock__extra` |

## Capture artifacts

- `/tmp/maho-screenshots/wave-final-review/` (현재 restructure 상태, 1280×720)
- `/tmp/maho-screenshots/wave-final-fhd/` (현재, 1920×1080)
- 비교 baseline: 위 두 디렉토리의 screen-5, screen-6 PNGs
