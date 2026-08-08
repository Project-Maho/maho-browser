# maho 4표면 토큰 충돌 매트릭스 (실측)

**조사일:** 2026-07-19
**목적:** 무거운 길(코어 토큰 리팩터링) 실행 전, 4표면 실제 값 충돌 지점 특정.
**방법:** 각 표면 토큰 파일 전체 실측 (maho_color_mixer.cc, tokens.css, web-ai/styles.css, starlight-theme.css + marketing.css).

---

## 표면별 토큰 소스 + 기술

| 표면 | 기술 | 토큰 형식 | 파일 |
|---|---|---|---|
| A. Native Views | C++ (Skia) | `constexpr SkColor` + ColorMixer | `browser/ui/theme/maho_color_mixer.cc`, `maho_color_id.h` |
| B. WebUI | Tailwind v4 CSS | `--var` (shadcn) | `maho_common/react/theme/tokens.css` |
| C. web-ai | Plain CSS | `--color-*` | `maho/web-ai/src/styles.css` |
| D. Website | Astro/Starlight CSS | `--sl-color-*` + marketing | `starlight-theme.css`, `marketing.css`, `global.css` |

---

## 충돌 1: 다크 배경 (근본적으로 다름)

```
A Native  : rgb(16,15,21)  = #100f15   (보랏빛 다크 — purple-tinted)
B WebUI   : #1a1a1a (bg), #0f0f0f (card)  (중성 무채색)
C web-ai  : #1c1c1e (surface), #2c2c2e (raised)  (Apple 시스템 그레이)
D Website : #fafaf9 라이트 기본 / 다크는 순검정 계열  (모노크롬)
```
→ **4개 다 다름.** #100f15(보라) vs #1a1a1a(중성) vs #1c1c1e(Apple) vs 순검정.
→ 각자 이유: Native=Space 테마 보라 틴트, web-ai=iOS HIG, Website=마케팅 모노크롬.

## 충돌 2: 액센트 컬러 (완전 분기)

```
A Native  : kMahoColorCanvasAccent — Space 테마별 동적 (사용자 선택)
B WebUI   : #e5e5e5 (무채색! primary=밝은 회색, 액센트 개념 거의 없음)
C web-ai  : #0a84ff (Apple 시스템 블루)
D Website : #18181b/#000 (모노크롬, "no brand color accents" 명시) / OG만 보라 #8b7ff5
```
→ **통일 불가.** iOS블루 vs 무채색 vs 모노크롬 vs 동적. 표면 목적이 근본적으로 다름.

## 충돌 3: 시맨틱 색 (semantic — 여기는 통일 가능성 있음)

```
success:  A(?) · B #95d8bb · C #30d158 · D(없음)
error:    B #ef4444 · C #ff453a · D(없음)
warning:  B #f5b96a · C #ffd60a · D(없음)
```
→ B와 C가 **같은 개념 다른 값**. C는 Apple 시스템 색(#30d158=iOS green), B는 커스텀. 통일 후보.

## 충돌 4: Radius (스케일 컨셉 유사, 기준값 다름)

```
B WebUI   : --radius 0.75rem(12px) 기준 → sm 0.6x md 0.8x lg 1x xl 1.4x, pill 9999
C web-ai  : sm 8 / md 12 / lg 16 / chrome 20 / composer 22 / pill 9999
D Website : squircle factor 1.4 (sm 8→11.2, md 8→11, lg 12→17)
```
→ pill=9999 공통. 나머지는 기준값 다름. **스케일 구조는 통일 가능, 값은 근사.**

## 충돌 5: Spacing (통일 가능성 높음)

```
C web-ai  : 4/8/12/16/20/24/32 (4px 베이스, 명시)
B WebUI   : Tailwind 기본 (4px 베이스 — 사실상 동일 계열)
D Website : marketing ladder (1/1.5/3/6rem)
settings 감사: --maho-settings-space-7=32 vs --maho-ai-space-7=28 (같은 이름 다른 값!)
```
→ 4px 베이스는 공유. **settings vs ai의 space-7 불일치가 대표 버그.** 통일 최우선 후보.

## 충돌 6: 폰트 (표면별 정당한 차이)

```
A Native  : 시스템 (Chromium 기본)
B WebUI   : system-ui, -apple-system...
C web-ai  : -apple-system, BlinkMacSystemFont... (네이티브 느낌 의도)
D Website : Geist Variable + Instrument Serif + Geist Mono (브랜드 폰트)
```
→ 제품 UI(A/B/C)는 시스템 폰트(네이티브 성능·친숙), 웹사이트(D)만 브랜드 폰트.
→ **이건 통일하면 안 됨** — 제품에 웹폰트 강제하면 로딩·네이티브 이질감.

---

## 통일 판정 (실측 기반)

```
토큰            통일?       근거
────────────────────────────────────────────────────────────
아이콘          ✅ 이미 됨    Lucide 단일 소스
spacing 베이스   ✅ 강력후보   4px 공유, space-7 불일치는 명백한 버그
시맨틱 색        ⚠️ 후보      B/C 값 다름, Apple색으로 정렬 가능성
radius 스케일    ⚠️ 부분      구조 공유, 기준값 근사
light/dark 구조  ✅ 가능      네 표면 동일 패턴
다크 배경 hex    ❌ 불가      #100f15/#1a1a1a/#1c1c1e/순검정 — 목적 다름
액센트 컬러      ❌ 불가      동적/무채색/iOS블루/모노크롬
폰트 패밀리      ❌ 유지      제품=시스템폰트, 웹=브랜드폰트 (정당)
```

**핵심 결론:**
- **완전 값 통일은 불가능**하고 하면 안 됨 (배경·액센트·폰트는 표면 목적이 달라 의도적으로 다름).
- **통일이 실제로 유익하고 가능한 것:** spacing 스케일(특히 space-7 버그), 시맨틱 색 어휘, radius 스케일 구조, light/dark 패턴.
- **"코어 = 구조·스케일·어휘 계약"**, **"표면 = 값 오버라이드"**가 정답. 값까지 통일하려는 시도는 리팩터링을 망침.

## 무거운 길 재정의 (실측 후)

당초 "값 불일치 제거"라고 했지만, 실측 결과 **값 불일치의 대부분은 정당한 차이**임. 따라서 무거운 길의 실제 작업은:

1. **코어 토큰 계약 레이어 신설** — spacing/radius/semantic 스케일을 단일 정의(tokens.json 또는 build-time 생성 소스).
2. **네임스페이스 통일** — `--maho-settings-space-*` / `--maho-ai-space-*` → 공통 `--maho-space-*`. (space-7 같은 버그 제거)
3. **표면별 값은 오버라이드로 명시 유지** — 배경/액센트/폰트는 각 표면이 코어를 상속 후 override. 통일 강제 안 함.
4. **C++/CSS 기술 분리** — 단일 정의에서 C++ 헤더(SkColor)와 CSS(--var) **양쪽으로 생성**하는 빌드 스텝. (satori OG 파이프처럼 script 하나로)

→ 즉 "값을 하나로 만드는" 게 아니라 "**스케일·어휘·네임스페이스를 하나의 계약으로 만들고, 값은 표면이 채우는**" 리팩터링.
