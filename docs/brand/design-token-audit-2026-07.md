# maho 디자인 토큰 — 4표면 실측 조사 (통일 가능/불가 리포트)

**조사일:** 2026-07-19
**목적:** DESIGN.md 범위 결정 전, 4개 표면의 실제 토큰이 얼마나 다른지 코드로 확인.
**방법:** 각 표면의 토큰 소스 파일 직접 읽음 (추측 아님).

---

## 조사한 4개 표면 + 토큰 소스

| 표면 | 기술 | 토큰 소스 파일 |
|---|---|---|
| **A. 데스크톱 Native Views** | Chromium C++ | `maho-chromium/browser/ui/theme/maho_color_id.h` (+ `maho_color_mixer.cc`) |
| **B. 데스크톱 WebUI** | shadcn + Tailwind v4 | `maho-chromium/browser/resources/maho_common/react/theme/tokens.css` |
| **C. 모바일+데스크톱 AI** | Preact | `maho/web-ai/src/styles.css` |
| **D. 웹사이트(마케팅)** | Astro | `website/src/styles/starlight-theme.css` + OG(`generate-og-images.mjs`) |

---

## 실측 값 비교 (여기서 차이가 드러남)

### 다크 배경
```
A Native Views  : 시맨틱 ID(kMahoColorWindowBackground 등) — 하드코딩 hex 아님, mixer에서 계산
B WebUI         : #1a1a1a (background), #0f0f0f (card)
C web-ai        : #1c1c1e (surface), #2c2c2e (raised)   ← Apple 시스템 그레이 계열
D 웹사이트       : #0a0a0a (거의 순검정)
```
→ **네 표면의 다크 베이스가 다 다름** (#1a1a1a vs #1c1c1e vs #0a0a0a). 의도적 차이(웹사이트는 프리미엄 순검정, web-ai는 iOS 네이티브 그레이).

### 액센트 컬러
```
A Native Views  : kMahoColorCanvasAccent (space 테마별 동적 — 사용자가 Space 색 고름)
B WebUI         : #e5e5e5 (거의 무채색! primary=밝은 회색)
C web-ai        : #0a84ff (Apple 시스템 블루)
D 웹사이트       : #8b7ff5 (보라, browser) / #5fb3c7 (청록, mail) — OG 전용
```
→ **액센트가 완전히 제각각.** web-ai는 iOS 블루, WebUI는 무채색, 웹사이트는 보라, Native는 사용자가 고르는 동적 색. **단일 액센트로 통일 불가** — 각자 이유 있음.

### Radius
```
B WebUI         : --radius 0.75rem(12px) 기준, sm=0.6x md=0.8x lg=1x xl=1.4x
C web-ai        : sm=8 md=12 lg=16 chrome=20 composer=22 pill=9999
D 웹사이트       : (Starlight 기본 + squircle)
```
→ 스케일 컨셉은 비슷(작은→큰 + pill)하나 **기준값이 다름.**

### Spacing
```
C web-ai : 4/8/12/16/20/24/32 (4px 베이스)  ← 명시적
B WebUI  : Tailwind 기본 스페이싱
```
→ web-ai는 4px 베이스 명시, WebUI는 Tailwind 관습. 호환은 되나 정의 위치 다름.

---

## 이미 통일된 것 (공유 레이어 존재)

- **아이콘: Lucide** — 전 플랫폼 통합 확정 (`docs/icon-system.md`). lucide-react(WebUI) / lucide-icons-swift(iOS) / AVD(Android) / aggregate_vector_icons(Native). **이건 이미 단일 소스.**
- **다크 우선 + light/dark 이원 구조** — 네 표면 모두 data-theme 또는 prefers-color-scheme로 동일 패턴.
- **시맨틱 네이밍 컨셉** — surface/foreground/border/accent/muted 류 어휘는 표면 간 유사(값은 달라도 구조는 공유).
- **WebUI 내부는 이미 통일** — `tokens.css` 한 곳, "per-app 토큰 정의 금지"가 AGENTS.md 규칙.

## 아직 안 통일된 것 (알려진 이슈)

- settings 감사(`maho-settings-style-consistency-2026-06-19.md`) R2/R3: "design-token 계약 레이어 부재, 표면마다 제각각 네임스페이스, `--maho-settings-space-7=32px` vs `--maho-ai-space-7=28px`" — **maho 내부에서도 이미 인지된 미해결 이슈.**

---

## 판정: 통일 가능/불가 매트릭스

```
토큰 종류        통일?      근거
──────────────────────────────────────────────────────────────
아이콘           ✅ 이미 됨   Lucide 단일 소스 (전 플랫폼)
폰트 패밀리       ⚠️ 부분     웹사이트=Geist/Instrument Serif 확정.
                            제품 UI는 플랫폼 시스템폰트 가능성(미확인)
light/dark 구조  ✅ 가능     네 표면 동일 패턴
시맨틱 어휘       ✅ 가능     surface/fg/border/accent 공유 어휘로 정렬 가능
다크 배경 hex    ❌ 불가     #1a1a1a/#1c1c1e/#0a0a0a 각자 의도 있음
액센트 컬러      ❌ 불가     iOS블루/무채색/보라/동적 — 표면 목적이 다름
radius 기준값    ⚠️ 부분     스케일 컨셉 공유, 기준값은 플랫폼별
spacing         ✅ 가능     4px 베이스로 정렬 가능(이미 유사)
```

**결론:**
- **"값 레벨 완전 통일"은 불가능하고 하면 안 됨** — 각 표면의 다크 베이스·액센트 차이는 네이티브 관습(iOS 블루)·목적(마케팅 순검정)·기능(Space 동적색) 때문이지 실수가 아님.
- **"구조·어휘 레벨 통일"은 가능하고 유익** — 시맨틱 네이밍(surface/fg/border/accent), light/dark 패턴, spacing 베이스, 아이콘(이미 됨).
- **GTM/마케팅 목적엔 표면 D(웹사이트) + OG 토큰만 있으면 충분** — 카드·SNS·사이트는 D 계열(#0a0a0a, Geist/Instrument Serif, 보라 액센트)로 이미 일관. 제품 UI(A/B/C)를 건드릴 이유가 지금은 없음.

---

## 범위 재결정을 위한 3안

1. **마케팅 전용 DESIGN.md** — 표면 D + OG 토큰만 담아 카드/SNS/사이트 생성용. 제품 UI 무관. (GTM 목적에 정확히 부합, 작음)
2. **브랜드 코어 DESIGN.md** — 전 표면이 공유 가능한 것만(아이콘=Lucide, 폰트 방향, 시맨틱 어휘, light/dark, spacing 베이스). 값 통일은 강제 안 함. 제품 팀도 참조 가능. (중간)
3. **풀 디자인 시스템** — settings 토큰 흩어짐 이슈까지 정리. 제품 엔지니어링 작업. (큼, GTM 범위 밖)
