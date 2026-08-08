# maho — BRAND.md

**단일 진실 소스(Single Source of Truth).** 모든 사이트 카피·WebUI 문자열·SNS·마케팅·GTM 콘텐츠는 이 문서에서 파생된다. 사이트/메모리/카피가 이 문서와 다르면 이 문서가 우선이며, 사이트 쪽을 "수정 대상"(§8)으로 관리한다.

**근거:** 사이트 1차 소스(`why`/`about`/`index`/`compare`/`mail`) + 경쟁 리서치(`competitor-research-2026-07.md`, `competitor-deep-dive-2026-07.md`) + 제품 코드 실측(Profiles 격리) + 사용자 직접 확정 우선순위.

---

## 1. One-liner

> **The browser tools you liked — Spaces, split view, command bar — still shipping, still yours.**

- **What:** 파워유저용 데스크톱 브라우저 (탭·공간 정리, 키보드 우선).
- **Wedge:** "still shipping" = 살아있는 후계 (멈춘 경쟁 제품 대비). "still yours" = 사용자에게 속함 (agent가 대신 행동 안 함).
- **경쟁사 미언급:** Arc 난민은 "still shipping"에서 알아보되, 대외 카피에 경쟁사명은 넣지 않는다.
- 상위 가치 축약(내부용): "A browser that belongs to its user." — 철학 표현으로만, 단독 대외 헤드라인으로는 쓰지 않는다(제품 범주가 안 드러남).

## 2. 포지셔닝 우선순위 (확정 — 순서 = 강조 순위)

| 순위 | 기둥 | 뜻 | 대표 근거 |
|---|---|---|---|
| **1. 본체** | **Ergonomic design** | 일하는 방식에 맞춘 브라우저. 공간·집중·키보드 우선. "당신이 좋아하던 브라우저 도구, 계속 shipping되는 스택 위에서." | Spaces, Split View, Command Bar, 사이드바, **Chromium 위 Arc식 Profiles 격리** |
| **1.5. 강한 받침** | **Privacy** | 데이터는 기기에. 로컬·E2E sync·no telemetry. **리드 훅으로는 안 쓰되(레드오션), 강력한 신뢰 근거·기본 사양으로 항상 함께 간다.** ergonomic 다음, AI보다 훨씬 앞. | 로컬 저장, E2E 암호화 sync, no telemetry, SQLCipher(mail) |
| **3. 보조** | **AI, when asked** | hand-on-wheel. 시켰을 때만, 자율 에이전트 아님, 완전히 끌 수 있음. | BYOK/managed, "Never forced. Never mandatory." |

**리드 규칙:** 마케팅·이미지의 첫 메시지(훅)는 **항상 1순위(ergonomics)**. Privacy는 리드 훅으로 세우지 않되(첫인상에서 경쟁사와 안 구별됨), 거의 모든 소재에 **강한 받침·신뢰 근거**로 동반한다. AI는 역발상 보조. AI를 리드로 앞세우지 않는다.

## 2.5. 경쟁사 기준 전략 순서 — 내부 프레이밍 전용

> 이 섹션은 **내부 전략 정렬용**이다. 카드뉴스·SNS·랜딩 히어로·마케팅 이미지에는 경쟁사명을 직접 쓰지 않는다(§7). 외부 카피에서는 아래 경쟁사명을 일반화한다: “the browser tools you liked”, “Firefox-based alternatives”, “browsers that act for you”.

1. **Better Arc — ergonomic browser tools, still shipping**
   - 첫 번째 훅은 항상 ergonomic: Spaces, split view, command bar, sidebar, keyboard-first browsing.
   - Arc가 남긴 감정적 빈틈은 “좋아하던 브라우저 도구가 멈췄다”이다. maho는 그 도구를 **살아있는 Chromium 기반 제품**으로 이어간다.
   - 대표 문구: “The browser tools you liked — Spaces, split view, command bar — still shipping, still yours.”

2. **Better Zen — Arc-like ergonomics without Firefox/Profile tradeoffs**
   - Zen은 Arc 난민 수요를 검증하지만, Firefox 기반·Chromium 확장 호환·Arc식 in-window Profiles 부재가 maho의 기회다.
   - maho는 Zen의 “Arc-like ergonomics” 수요를 **Chromium + Profiles + 유지되는 제품**으로 해결한다.
   - 단, “calm / calmer”는 Arc·Zen 소유 표현이므로 외부 리드 카피로 쓰지 않는다. “정리 잘 되는 / 집중되는 / 조용한 기본값”은 제품 설명으로만 보조 사용한다.

3. **Better Aside — AI only when asked**
   - Aside류 agentic browser는 “AI가 대신 행동한다”는 방향이다. maho는 정확히 반대편: hand-on-wheel, 시켰을 때만, 완전히 끌 수 있음.
   - AI는 3순위 보조 가치다. 히어로 첫 줄에 AI를 세우지 않는다.
   - 외부 표현은 경쟁사명을 빼고 “not a browser that acts for you”, “AI when you ask”, “Never forced. Never mandatory.”처럼 쓴다.

**요약:** 시장 진입 순서는 `better Arc(ergonomic) → better Zen(Chromium+Profiles로 보완) → better Aside(hand-on-wheel AI)`이다. 단, 대외 첫 메시지는 언제나 **ergonomic/browser tools**이며, privacy는 신뢰 받침, AI는 절제된 보조다.

## 3. 차별화 3축 (경쟁 리서치로 검증된 무주공산)

경쟁사 지형상 maho의 3기둥 언어("calm", "built for how you work", Spaces/Split, privacy, AI)는 대부분 선점당함. 실제로 비어있는 자리는 아래 3개뿐이며, 브랜드는 여기에 선다:

1. **살아있는 Arc 후계 + Chromium Profiles**
   - Arc 단종(2025-05, "Chromium updates only") + Zen(Firefox, Profiles 부재)이 남긴 가장 구체적 빈 틈.
   - **maho는 Chromium 기반에서 Arc식 Profile 격리(같은 창 내 페르소나별 쿠키/세션 분리)를 실제 구현·실동작** (제품 코드 실측 확정, `competitor-deep-dive` §Profiles).
   - HN Arc 난민이 명시적으로 요구한 기능: "Besides Arc no other Chromium-based browser allows this."
   - **maho 최강 차별 wedge.**

2. **hand-on-wheel AI = agent 피로감 시장의 반대 진영**
   - 시장 전체(Aside/Dia/Edge/Opera/Comet/Atlas)가 "AI가 대신 한다"로 수렴. HN 정서는 이에 대한 불신·피로.
   - maho: "AI는 시켰을 때만, 완전히 끌 수 있음." Aside("do real work for you")의 정확한 안티테제.
   - HN 난민 직접 인용: "I certainly don't trust an AI to buy shit for me." / "should be possible to turn them off entirely."

3. **solo-dev 직접 책임**
   - 전 경쟁사가 YC/기업/비영리/자원봉사 크루. maho만 "single accountable person, answers their own email."
   - 신뢰·진정성 레이어. 위원회 없는 제품.

## 4. 승인된 문구 (사이트 검증됨 + 길이별 사용처)

**Primary one-liner (풀버전 — 사이트 히어로/About):**
- "The browser tools you liked — Spaces, split view, command bar — still shipping, still yours."

**짧은 헤드라인 (카드뉴스·SNS·좁은 공간):**
- "The browser tools you liked. Still shipping."
- "Built for people who took browsers seriously." (검증된 태그라인)

**서브/부연:**
- "The browser tools you liked, on a stack that isn't going anywhere."
- "Spaces, split view, command bar — and your logins stay separate when you want them to." (Profiles wedge)

**원칙 문구:**
- 우산(철학, 단독 헤드라인 금지): "A browser that belongs to its user."
- AI: "Never forced. Never mandatory." / "AI when you want it."
- Voice 예: "Real tools, not talking points." / "No changelog theater."

## 5. Voice & tone

- 담백·정직·반과장. 개발자에게 개발자가 말하듯.
- 1인 개발자의 직접적·솔직한 목소리. 세일즈 과장·AI티 표현(thrilled/passionate/revolutionary) 금지.
- 팩트 기반. 벤치마크 자화자찬·미검증 수치 금지 (Aside의 반면교사).
- 기능을 자랑하기보다 "당신이 하던 일을 방해하지 않는다"로 프레이밍.

## 6. 비주얼 아이덴티티

- **컬러:** 다크 `#0a0a0a` / 라이트 `#fafaf9`. 모노크롬 기반 (텍스트 다크 `#fafafa` / 라이트 `#18181b`). 보라 `#8b7ff5`(browser) / 청록 `#5fb3c7`(mail)은 OG/액센트 한정.
- **폰트:** Instrument Serif(제목) / Geist(본문) / Geist Mono(라벨·워드마크: 대문자, letter-spacing 0.18em).
- **워드마크:** `MAHO` (Geist Mono, uppercase, wide tracking).
- **무드:** 차분·프리미엄·미니멀. 정직한 여백. 렌더는 satori+resvg (레포 기존 파이프라인, 결정론적).

## 7. 금지 규칙

- ❌ **경쟁사 직접 네이밍** — 카드뉴스·SNS·마케팅 이미지에서 금지. **블로그·SEO 비교 페이지·문서는 허용**(SEO 유입). 마케팅에서는 "browsers that act for you" 식 일반화.
- ❌ **"AI-first" / "agent-native"** — AI는 3순위. 이 표현 자체가 포지셔닝 위반.
- ❌ **privacy를 리드 훅/첫 메시지로** — 레드오션(Brave/Safari/Firefox/Vivaldi)이라 첫인상에서 안 구별됨. 단 privacy 자체는 1.5순위 강한 받침으로 거의 모든 소재에 동반한다(§2). "금지"는 리드 위치일 뿐, privacy 언급 자체가 아님.
- ❌ **"calm / calmer"를 리드 카피로** — Arc("Clean and calm")·Zen("calmer internet") 소유. 미투 위험.
- ❌ **오픈소스 주장** ("open source", "MIT", "auditable source", "view source") — maho는 proprietary. 신뢰는 아키텍처 공개·네트워크 검증가능성·직접 연락으로.
- ❌ **자율 에이전트 마케팅** — hand-on-wheel이 원칙.
- ❌ **미검증 벤치마크/수치 fabricate** — 팩트만, 공식 출처 인용.
- ❌ **과장된 격리 주장** — "Space마다 자동 로그인 분리"는 거짓(기본 Default 공유). 정확: "Space에 프로필 배정 시 격리."

## 8. 사이트 정합성 — 처리 로그

BRAND.md 확정 우선순위와 충돌하던 기존 사이트 문구는 2026-07-22 기준 처리 완료:

- `compare/index.astro`: "Maho's AI-first architecture" 제거 → ergonomic Spaces workflow / Chromium Profiles / AI only when asked로 수정.
- `LandingPage.tsx` finalCta: "Free forever. Private by default. AI when you want it." 제거 → browser tools / Spaces / split view / command bar 리드로 재배치.
- `LandingPage.tsx` why: "Browsers weren't built for how you work." 제거 → "The tools you liked, on a browser that keeps moving."로 수정.

새 사이트 카피가 이 문서와 충돌하면 이 섹션에 추가하고 사이트 쪽을 수정 대상으로 관리한다.

## 9. 대상 고객 (wedge → 점진 확장)

**전략: 파워유저 wedge로 진입해, 일반 소비자까지 점진 확장.**

- **1차 wedge (진입점):** Arc 난민 (파워유저·개발자). Arc가 주던 것 + Arc가 못 준 것(유지되는 Chromium, 살아있는 개발, Profiles) 원하는 층. 신뢰·입소문의 씨앗.
- **2차:** Zen 사용자 중 Chromium/Profiles 필요층, agent 피로감 느끼는 프라이버시 중시 사용자.
- **최종 확장 타깃:** **AI를 잘 안 쓰는 일반 소비자까지.** "정리 잘 되는, 계속 개선되는 좋은 일상 브라우저"로 인식 확장. 파워유저 기능은 기본값으로 조용히, 일반 사용자는 복잡함 없이.

**GTM 채널 함의 (2단 구조):**
- **초기(파워유저 wedge):** HN·개발자 커뮤니티·기술 블로그·Reddit. 정직한 기술 서사(solo-dev, Chromium Profiles, hand-on-wheel)가 먹히는 곳.
- **확장(일반 소비자):** SEO/GEO 메인 + SNS 4채널(브랜드계정 TikTok/Reels/Shorts/X) 숏폼 바이럴. "탭 정리/공간" 같은 시각적으로 즉시 이해되는 ergonomic 훅 중심.
- 두 층 모두 리드는 ergonomics, 받침은 privacy, AI는 절제로 일관.

---

*최종 갱신 2026-07-22. 변경 시 근거(1차 소스/제품 실측/사용자 확정)를 함께 기록.*
