# Landing Page 전면 리구조 계획 (Phase 2 재실행)

**Goal:** `mahobrowser.com/` 랜딩페이지를 Better Arc(-successor) 포지션에 맞게 재구조. 카피·레이아웃·IA·모바일 버그 일괄 정리.

**Root Cause:** 이전 Phase 2가 실제로 실행되지 않아 랜딩이 여전히 AI-native 시절 카피 + 검은 빈 타일 + 모바일 H1 clip 상태.

**작업 루트:** `website/src/` 전역 (컴포넌트, 페이지, 스타일)

---

## 확정 결정 (이번 세션 대화 산물)

| 결정 | 값 |
|---|---|
| 헤드라인 | `Built for people who took browsers seriously.` |
| 서브 | `The browser tools you liked, on a stack that isn't going anywhere.` |
| 톤 | Better Arc-successor. Zen `calmer internet` 회피. |
| Spaces/Boosts 이름 | 유지 (제품 내부 용어) |
| Pricing 홈 노출 | 3티어 유지 |
| 4번째 feature | AI (optional/BYOK) 재프레임 |
| 데스크톱 지원 | macOS + Windows + Linux |
| 모바일 | iOS/Android 개발 중 |
| Final CTA H2 | `Free forever. Private by default. AI when you want it.` |
| Discord CTA | Hero + Final CTA 두 곳 secondary |
| Discord URL | `https://discord.gg/Ty4J6pMdqg` |

---

## 새 섹션 구조 (10섹션)

```
1. Nav
2. Hero
3. What's Inside              (기존 Features 재구성, Gallery 흡수)
4. Import in One Click        [NEW]
5. Ships Weekly               [NEW]
6. Why Maho                   (condense — 짧게)
7. Social Proof strip         (marquee 재위치, 콘텐츠 재분류)
8. Pricing                    (3티어 유지, 카드 균형 수정)
9. Final CTA
10. Footer
```

**제거**: 기존 Product Gallery 섹션 (빈 타일 + 중복 taxonomy). 실제 스크린샷 자산은 `What's Inside`의 feature 카드로 흡수.

---

## Section-by-Section 명세

### 1. Nav (`components/Nav.astro`)

**Desktop:**
- Left: `Maho` wordmark (product switcher 제거 or 별도 dropdown으로)
- Right: `Docs` / `Blog` / `Pricing` / `Compare` / `Download` (CTA button)
- 제거: 개별 `Why Maho`, `About`, `Roadmap` (footer로 이관)

**Mobile (<768px):**
- 햄버거 메뉴 추가
- Drawer 안에 모든 링크
- Drawer 하단 `Download` CTA full-width

**Product switcher (Browser/Mail):** wordmark 옆 붙어있는 세그먼트 → 우측 nav 끝에 `Mail →` 링크 하나로 축소, 또는 dropdown.

---

### 2. Hero (`components/landing/HeroSection.tsx`)

**Copy:**
```
H1:    Built for people who took browsers seriously.
Sub:   The browser tools you liked, on a stack that isn't going anywhere.
CTA1:  [Download Maho] → /download
CTA2:  Join our Discord → https://discord.gg/Ty4J6pMdqg
Micro: Free. macOS, Windows, and Linux. Mobile in development.
```

**Visual:**
- 우측 mockup: **단일 데스크톱 스크린샷 1장** (기존 desktop + 폰 2개 + 패널 캐스케이드 → 대체)
- H1 좌측 clip 버그 수정 (모바일 CSS overflow / font shaping 이슈)
- 좌우 반씩 비율 (`grid-cols-2` 데스크톱, stack 모바일)

**CTA 스타일:**
- Primary (Download): solid button, high contrast
- Secondary (Discord): outline button 또는 underline link

---

### 3. What's Inside (`components/landing/FeaturesSection.tsx`)

**Header:**
```
Eyebrow: What's inside
H2:      Real tools, not talking points.
```

**4개 feature 카드 (2×2 grid 데스크톱, stack 모바일):**

각 카드 = `{eyebrow, title, body, visual}` 구조. Visual은 실제 screenshot 있으면 embed, 없으면 대체 illustration 또는 icon+accent (❌ 검은 빈 박스 금지).

1. **Spaces** — 스크린샷: 기존 `sidebar-target-prototype-verified.png` 사용
   - Body: "Persistent workspaces in the sidebar. Group tabs by project, client, or context. Switch instantly with ⌃1–9. Pick up where you left off."

2. **Split View** — visual: 아이콘 + 간단 다이어그램 (실제 스크린샷 없으면)
   - Body: "Two pages side by side. Compare, reference, and multitask without switching tabs."

3. **Command Bar** — 스크린샷: 기존 `command-bar-mockup-final-qa.png` 사용
   - Body: "One shortcut for everything. Navigate, search open tabs, run actions, jump between Spaces. Keyboard-first from the start."

4. **AI, when you want it** — visual: BYOK provider 로고 grid (OpenAI/Anthropic/Ollama)
   - Body: "Summarize pages, rename downloads, group tabs — with your own key, or a managed subscription. Never forced. Never mandatory."

**컷된 features** (Boosts / Sync / Extensions): 별도 `/features` 페이지 만들거나 나중에 추가. 랜딩엔 노출 안 함.

---

### 4. Import in One Click [NEW] (`components/landing/ImportSection.tsx`)

**Header:**
```
Eyebrow: Migration
H2:      Bring your setup with you.
Sub:     One-click import from Arc, Chrome, Safari, Firefox, Edge. 
         Bookmarks, tabs, history, and passwords land where you expect them.
```

**Visual:** 브라우저 로고 6개 grid (Arc / Chrome / Safari / Firefox / Edge / Brave). 아래 화살표 → Maho 로고. 정적 SVG 이미지 or CSS 레이아웃.

**Note:** Arc-refugee 포지션 강화. Zen도 이 각도 있지만 명시 안 함.

---

### 5. Ships Weekly [NEW] (`components/landing/ChangelogSection.tsx`)

**Header:**
```
Eyebrow: Momentum
H2:      Ships weekly. Publicly.
Sub:     No dark releases. Every change lands in the changelog before 
         you notice it. Read what shipped this week or subscribe to the RSS.
```

**Visual:** 최근 3–5개 changelog 엔트리 목록 (블로그 RSS 또는 static list).
- 각 항목: 날짜 + 제목 + 1줄 요약
- 하단: `View all changes →` link to `/blog`

**Note:** 신뢰 축적. "이 프로젝트 유지되는가" 우려에 정면 응답.

**구현:** Blog RSS 파싱 (build time) 또는 별도 `changelog` collection. **간단히 static array 시작 가능**.

---

### 6. Why Maho — CONDENSE (`components/landing/WhySection.tsx`)

**현재:** 큰 텍스트 블록 + agent-native 잔재 카피

**축소:**
```
Eyebrow: Why Maho
H2:      Browsers weren't built for how you work.
Body:    (1 문단, 100자 이내)
         You had a browser you liked. Sidebar, spaces, split view, 
         a command bar that actually worked. Then it stopped shipping.
         Maho picks up where that left off.
```

**대안:** 이 섹션 완전 삭제. "Why" 서사는 Hero + Import + Weekly로 이미 전달됨. **삭제 권장**.

---

### 7. Social Proof strip (`components/landing/SocialProofSection.astro`)

**재배치:** 현재 Final CTA와 Footer 사이 → **Hero 바로 아래**로 이동 (진입 직후 신뢰 축적)

**콘텐츠 재분류:**
- 제거: `SHIPPING SOON`, `PING SOON` (contradictory), `MACOS 12+` (obsolete)
- 유지: `PRIVACY-FIRST`, `FREE FOREVER`
- 신규: `NO TELEMETRY`, `BYOK OR MANAGED AI`, `WEEKLY RELEASES`

**Note:** 실제 press coverage / user count 데이터 아직 없음 → 값 진술만.

---

### 8. Pricing (`components/landing/PricingSection.tsx`)

**Copy:**
```
Eyebrow: Pricing
H2:      Free. Forever.
Body:    Maho Browser is free, forever. Every feature, no limits.
         AI features are optional — bring your own key, or subscribe 
         for managed inference.
```

**티어 3개 유지, 카드 균형 수정:**
- 각 티어 bullet 개수 균등 (4개씩)
- POPULAR 배지 시각적 무게 강화 (색상 accent)
- 카드 높이 uniform (min-height 지정)

**Body 문구:** AI 강조 → AI optional 톤으로 완화 완료

---

### 9. Final CTA (`components/landing/FinalCtaSection.tsx`)

```
H2:    Free forever. Private by default. AI when you want it.
Sub:   Download for macOS, Windows, or Linux. Mobile in development.
CTA1:  [Download Maho] → /download
CTA2:  Join our Discord → https://discord.gg/Ty4J6pMdqg
```

Secondary CTA 지원 필요 (컴포넌트 확장).

---

### 10. Footer (`components/Footer.astro`)

**컬럼 재편성 (4 → 4개 유지, 재분류):**

```
Product              Resources            Compare              Company
- Download           - Docs               - vs Arc             - About
- Pricing            - Blog               - vs Zen             - Roadmap
- Why Maho           - Changelog          - vs Dia             - Security
- Mail →             - Status             - All comparisons    - Contact

                                          (Mail comparisons 
                                           /mail 페이지 footer로 이관)
```

**주요 변경:**
- Compare 컬럼에서 Mail 비교 (`vs Superhuman/Apple Mail/Thunderbird`) 제거 → Browser 비교만
- 신규 Browser 비교 페이지 필요 (`/compare/maho-vs-arc`, `/compare/maho-vs-zen`, `/compare/maho-vs-dia`) — blog에는 있으나 compare 페이지 없음
- `Mail →` 링크로 `/mail` 이동
- `Status` 링크 (Cloudflare Status page 등, 없으면 나중에)

---

## Typography 스케일 재설계 (`styles/global.css`)

**현재:** 모든 섹션 헤딩이 동일한 italic serif → 계층 flat

**신규:**
- **Display** (Hero H1): 6xl serif italic — 유일한 최상위
- **Section H2**: 3xl serif (non-italic) — 섹션 헤더
- **Subsection H3**: xl sans-serif medium — feature/tier 이름
- **Body**: base sans-serif regular
- **Eyebrow**: xs mono uppercase — 섹션 라벨 (모든 섹션 통일)

Serif는 Display + Section H2에만. 나머지는 sans-serif.

---

## 크리티컬 버그 수정 (모바일)

1. **H1 좌측 clip** — `LandingPage.tsx`의 hero h1 style / global.css 확인. `overflow-x: hidden` 또는 `letter-spacing` 이슈 조사 후 수정.
2. **햄버거 nav** — `Nav.astro`에 mobile drawer 추가 (이미 관련 CSS 클래스 존재하는지 확인)
3. **Footer 컬럼 collapse** — <480px에서 accordion 또는 single column
4. **Image responsive** — hero + gallery 이미지에 mobile-optimized srcset 확인

---

## 구현 단계

### Phase A — 카피 + 컴포넌트 데이터 (1시간)
- `LandingPage.tsx`: `content.browser` 재작성
- `HeroSection.tsx`: secondary CTA 지원 추가
- `FinalCtaSection.tsx`: secondary CTA 지원 추가
- `ProductGallerySection.tsx`: **컴포넌트 자체 제거** (import에서만 빼기)
- `WhySection.tsx`: 컴포넌트 제거 or condense
- `SocialProofSection.astro`: 콘텐츠 재분류

### Phase B — 신규 섹션 (1.5시간)
- `ImportSection.tsx`: 브라우저 로고 grid + Maho 화살표
- `ChangelogSection.tsx`: static list of 5 entries (RSS 파싱은 follow-up)

### Phase C — 레이아웃/네비/푸터 (1.5시간)
- `Nav.astro`: 항목 축소 + 모바일 햄버거
- `Footer.astro`: 컬럼 재편성, Browser 비교로 축소, Status 추가
- `index.astro`: 섹션 순서 조정 (Hero → SocialProof → Features → Import → Changelog → Pricing → FinalCTA)

### Phase D — Typography + 모바일 버그 (1시간)
- `global.css`: 헤딩 스케일 재정의
- Hero H1 clip 수정
- Footer 모바일 collapse

### Phase E — 검증 (30분)
- 3해상도 스크린샷 재촬영
- Lighthouse (a11y, performance)
- `bun run build` 통과
- 배포 후 라이브 검증

---

## Out of Scope (별도 태스크)

- Mail 랜딩(`/mail`) 재구조
- `/download` 페이지 tri-platform 문구 반영
- 신규 Browser 비교 페이지 3개 (`/compare/maho-vs-{arc,zen,dia}`) 신설
- Changelog RSS 실시간 파싱 (지금은 static)
- 신규 provider 로고 아이콘 SVG (지금은 텍스트 or 기존 아이콘)

---

## Verification Gates

Phase 종료마다:
- [ ] `bun run build` 통과
- [ ] Preview deploy 후 desktop/tablet/mobile 3해상도 스크린샷
- [ ] `maho.app` 잔재 grep 0
- [ ] "macOS only" 표현 grep 0
- [ ] agent-native 잔재 grep 0

최종:
- [ ] Lighthouse a11y ≥ 90
- [ ] 모바일 H1 clip 없음
- [ ] 모바일 nav 햄버거 동작
- [ ] Discord CTA Hero + Final CTA 두 곳 노출
- [ ] Tri-platform 문구 (macOS/Windows/Linux) 최소 3곳 노출
