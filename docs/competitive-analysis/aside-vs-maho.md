# Aside Browser vs Maho Browser — 종합 기능 비교 보고서

**작성일**: 2026-06-27
**버전**: 1.0
**작성 목적**: 내부 전략 문서 — Aside Browser(YC F25)의 기능/로드맵/포지셔닝을 분석하고, Maho Browser가 차용할 만한 패턴과 차별화 가능 영역을 식별
**대상 독자**: Maho Browser 제품/엔지니어링 리더십
**언어**: 한국어 본문 + 영어 기술 용어 혼용
**1차 정보 출처**: 로컬 설치본 바이너리 직접 분석 (Aside.app v1.0.626.1, 2026-06-26 빌드) + 공개 웹 리서치

---

> **⚠️ 상태 안내 (2026-07 갱신)** — 본 문서는 **2026-06-27 v1.0 시점의 역사적(historical) 경쟁 분석**입니다. 아래 본문의 "Maho 현재 상태" 서술 일부는 이후 구현이 진행되어 **더 이상 최신이 아닙니다**. Maho 측 현재 구현 상태의 캐논 소스는 다음 두 문서입니다:
> - 구현 증거 원장: `docs/competitive-analysis/aside-benchmark-evidence-2026-07.md`
> - 상태 요약: `docs/competitive-analysis/aside-benchmark-status-2026-07.md`
>
> Aside 측 사실(가격 Free/$20 Pro/$200 Max, Online-Mind2Web SOTA 주장 등)은 **경쟁사 사실로서 원문 그대로 보존**합니다. 본문에 `정정(2026-07)` 표기가 붙은 항목은 Maho 측 최신 상태로 갱신된 부분입니다. Maho는 proprietary/closed-source이며, 본 문서 어디에도 OSS 주장은 없습니다.

---

## §1 TL;DR — Executive Summary

### 한 문장 포지셔닝

- **Aside Browser**는 한국계 창업자(Jun Kim 등 3인) 주도의 **YC F25 배치 출신 Aside Computer Inc.**가 만든 **Chromium 142 fork (branch-heads/7827)** 기반 AI 에이전트 브라우저로, "AI 에이전트가 로그인된 웹사이트에서 실제 업무를 완수한다"를 표방한다. Online-Mind2Web 벤치 99% SOTA 주장, Gemini 번들 내장, AsideAgentManager 자체 에이전트 시스템, Split View / Vertical Tabs / MCP 호스트 / CLI를 1.0 시점에 출하했다. **2026-06-23 macOS 공개 출시** (4일 차), 무료 / Pro $20 / Max $200 / Enterprise 4-tier.
- **Maho Browser**는 Brave-style Chromium fork + Rust 백엔드 크레이트 + Swiftide 기반 agent 런타임(Maho-managed 빌링 프록시 경유) + iOS/Android 네이티브 셸 + E2EE self-hostable sync relay를 갖춘 cross-platform 브라우저로, **Maho-managed zero-setup AI(키 불필요)** 를 기본으로 하고 선택적 BYOK 다중-프로바이더 LLM과 사용자 통제형 approval flow를 지원한다. 출시 전 상태. `정정(2026-07)`: 원문의 "ACP 외부 agent 런타임 / BYOK 다중-프로바이더를 추구"는 stale. 현재 agent 경로는 `maho-agent`의 Swiftide 백엔드가 access token 존재 시 managed 프록시를 기본 사용하고 BYOK는 fallback (evidence 원장 §1 참조).

### 핵심 차별 7가지

| 차원 | 우위자 | 핵심 |
|---|---|---|
| Zero-setup AI | 동등 `정정(2026-07)` | Aside=Gemini 키 번들 / Maho=Maho-managed 프록시로 **키 없이 즉시 동작(shipped-in-browser)**. 원문의 구 BYOK-only 표현은 stale |
| Agentic 자동화 깊이 | Aside | Routines (cron/이벤트 트리거), Ultrabrowse, Cloud handoff, MCP 호스트, CLI |
| Tab auto-organization | 동등 `정정(2026-07)` | Aside `aside.verticalTabsTidy` / Maho **Tab Tidy shipped-in-browser** (Mojo `RequestTabTidy`/`ApplyTabTidyFolders`). 원문의 구 Tidy-gap 표현은 stale |
| 아키텍처 깊이 | Maho | Rust 백엔드 크레이트, Swiftide agent 런타임 + managed 빌링 프록시, managed+BYOK 다중 프로바이더 `정정(2026-07)`: 원문의 구 ACP-watchdog 표현은 stale |
| Cross-platform 범위 | Maho | iOS + Android 네이티브 셸 코드 보유(현재 reference-only, 출시 미확정) — Aside는 macOS-only |
| 통합 productivity | Maho | Spaces (테마 탭 그룹), Easel, Notes, Command palette, ad/tracker block, Maho Mail |
| 데이터 주권 | Maho | Self-hostable sync relay, BYOK, 광고/트래커 차단 |

### 양보 안 되는 한 줄

> **Aside = AI-maximalist task-completion browser (에이전트가 일을 끝낸다). Maho = Cross-platform privacy-architected productivity browser (사용자가 도구를 통제한다).**
> 둘 다 Chromium fork + AI 브라우저 카테고리이지만, **목표 사용자 멘탈 모델이 다르다.** Aside는 "AI에게 작업을 위임", Maho는 "AI를 도구로 사용". 두 제품의 차별화 축은 충돌하지 않으므로 Maho는 Aside의 agentic 메시지와 정면 충돌하지 말고, Aside가 진입하지 않은 (모바일, privacy, productivity 통합) 영역에서 시장을 만들어야 한다.

### 즉시 실행 가능한 5가지 권장 사항

> `정정(2026-07)`: 아래 원문 5개 항목은 대부분 이미 구현되어 우선순위가 바뀌었습니다. 현재 벤치 우선순위는 evidence 원장 기준으로 갱신했습니다.

1. **Zero-setup AI 유지·폴리시(parity/maintain)**: Maho-managed 프록시 경로는 이미 shipped(키 없이 동작). 신규 개발이 아니라 **데모/QA로 first-run 경험을 다듬는 것**이 과제. 선택적 BYOK는 fallback으로 보존.
2. **Tab Tidy — shipped, 데모/QA 크리티컬**: AI 클러스터링은 이미 Mojo로 출하됨. 남은 리스크는 신규 기능 개발이 아니라 **데모 촬영·수동 QA 신뢰성**(evidence 원장 §2 caveat: webui `.d.ts` 스텁 갱신).
3. **Routines — 구현됨, 단 launch 비커밋**: CLI/MCP/`chrome://maho-routines` 표면 존재(Max-tier 게이트). 그러나 `docs/demos/README.md`가 launch 클레임에서 명시 제외 → **출시 마케팅에 넣지 말 것**.
4. **MCP 호스트 — 구현됨(tools-only), launch 비커밋**: `maho-browser-mcp`가 tools-only 표면 제공(ADR 0014 NO-GO for resources/prompts). launch 클레임 제외 유지.
5. **모바일 — 과대주장 금지**: `ios-shell`/`android-shell` 코드는 있으나 **reference-only, 출시 미확정**. "폰에서도 동작"을 **런치 패리티로 주장 금지** — 코드 존재와 출시 커밋은 다름.

---

## §2 Aside 회사 / 팀 / 출시 / 가격

### 2.1 회사 정보

| 항목 | 내용 | 출처 |
|---|---|---|
| 법인명 | **Aside Computer Inc.** | [aside.com/policy/terms](https://aside.com/policy/terms) (2026-06-19 약관) |
| 카논 도메인 | `aside.com` (alias: `asidebrowser.com`, `aside.net`) | aside.com |
| 설립 | 2024 | YC company page |
| YC 배치 | **Fall 2025 (F25)** | [ycombinator.com/companies/aside](https://www.ycombinator.com/companies/aside) |
| YC 파트너 | Andrew Miklas | YC page |
| 본사 | San Francisco, CA | YC page |
| 팀 규모 | **3명** | YC page |
| 펀딩 | YC 표준 deal (~$500K 추정), 시리즈 A 비공개 | YC page |
| 바이너리상 Team ID | `8CPD4K4TBB` (registrant: "At Inc.") | `codesign -dvvv` |
| Bundle ID | `at.studio.AsideBrowser` | Info.plist |

> **이름 불일치 메모**: 바이너리는 `at.studio.AsideBrowser` 번들 ID와 "At Inc." 개발자 인증으로 서명되어 있으나, 공식 법인은 "Aside Computer Inc."로 명시되어 있다. `at.studio`는 이전 사명/내부 코드네임의 잔재로 추정되며, 공개 웹에 "At Inc."로 검색되는 자료는 없다. 별도 holding entity 가능성도 배제 불가.

### 2.2 창업자

| 이름 | 역할 | 배경 |
|---|---|---|
| **Jun Kim** (`@hyojun_at`) | Co-founder & CEO | Airbridge.io 창립 멤버 (17세 입사, $30M ARR 도달), 9세부터 코딩 |
| **Chanhee Lee** | Co-founder | Airbridge.io Founding Engineer, 15세에 Microsoft 후원 비영리 프로젝트 |
| **Sanghun Lee** | Co-founder | 14세 코딩 시작, 1M+ 다운로드 앱 빌더 |

세 명 모두 **한국계 / Airbridge.io 출신 / 어린 나이에 기술적 성취**라는 공통 서사. 이 점이 LinkedIn 런칭 포스트의 5K+ likes와 X 바이럴 트랙션의 한 축.

### 2.3 피벗 히스토리

YC F25 입주 시 **"Live answers for enterprise tech sales"** (영업 통화 라이브 AI 코칭)으로 피칭했으나, 2025년 말 ~ 2026년 초 사이 **AI 브라우저로 피벗**. 피벗 이전 도메인은 `AsideHQ.com` (sales tool 시절).

- 피벗 전 YC launch: [ycombinator.com/launches/Oj4-aside-live-answers-for-enterprise-tech-sales](https://www.ycombinator.com/launches/Oj4-aside-live-answers-for-enterprise-tech-sales)
- 피벗 직후 인터뷰: [chreporter.com 프로필 (2026-04-19)](https://chreporter.com/how-jun-kim-built-aside-from-a-sales-knowledge-problem-into-a-y-combinator-backed-ai-startup/)

> **시사점**: 짧은 시간(<1년) 안에 sales tool → 브라우저 fork → vertical tabs + agentic + MCP + CLI + Routines + multi-provider까지 출하한 속도가 인상적. 3인 팀 + Chromium fork 학습 곡선을 감안하면 매우 공격적인 실행력. 단, **테크 자체보다 마케팅과 벤치 데모로 트랙션을 만든 측면이 더 크다**는 점(아래 §5 참조).

### 2.4 출시 타임라인

| 이벤트 | 날짜 | 출처 |
|---|---|---|
| Company founded | 2024 | YC page |
| YC F25 entry (sales tool 시절) | ~Oct/Nov 2025 | YC launch post |
| "Top Startups to Watch from YC Fall 2025" | 2025-11-13 | YC mentions |
| Browser pivot 인터뷰 게재 | 2026-04-19 | chreporter.com |
| **Public browser launch (v1.0)** | **2026-06-23** | [Jun Kim LinkedIn](https://www.linkedin.com/posts/therne_today-were-launching-aside-yc-f25-an-activity-7475267050425282560-W9Qz) |
| 첫 외신 리뷰 | 2026-06-24 | PiunikaWeb |
| HN 스레드 (낮은 트랙션) | 2026-06-24 | [HN #48651217](https://news.ycombinator.com/item?id=48651217) |
| 본 보고서 작성 | 2026-06-27 | — |

> **현재 상태**: GA, 공개 다운로드, macOS 우선. Windows 빌드는 changelog에 작업 흔적 있으나 다운로드 페이지는 macOS 단일 옵션. **출시 4일 차** — 매우 fresh.

### 2.5 가격 / 액세스 모델

대기열·초대 없음. macOS 직접 다운로드 즉시 사용 가능.

| 티어 | 가격 | 핵심 제한 / 기능 |
|---|---|---|
| Free | $0/영구 | 500 credits/월, routines 최대 3개, Password Manager, Personalized Memory |
| Pro | $20/월 (연간 ~$16/월, 20% off) | Free 대비 3× 사용량, **Ultrabrowse** (deep research), unlimited routines, **Cloud handoff**, 14-day trial |
| **Max** | **$200/월** (연간 ~$160/월) | 30× 사용량, **early access program**, 14-day trial |
| Enterprise | sales 문의 | 팀 공유 profile/vault, seat 관리, API/cron 트리거, 전용 지원 |

**BYOK 옵션**: 자체 ChatGPT/Claude 구독 또는 raw API key 사용 가능 (번들 플랜 대안). [aside.com](https://aside.com)

**과금**: 자동 갱신, 비환불 (법정 강제 제외), usage-based 추가 과금 가능. [aside.com/policy/terms](https://aside.com/policy/terms)

> **메모**: $200/월 Max 티어는 ChatGPT Plus($200/월 Pro와 동일)와 직접 비교되는 가격대. **개인 사용자에겐 비쌈, enterprise 진입을 노린 가격 sandboxing**으로 해석. Free 500 credits는 routine 무거운 작업 한두 번이면 소진될 가능성.

---

## §3 Aside 기술 스택 / 기능 인벤토리

### 3.1 베이스 아키텍처 (바이너리 직접 확인)

| 항목 | 값 | 확인 방법 |
|---|---|---|
| 상위 베이스 | **Chromium** (Brave-style fork) | Framework Info.plist `SCMRevision: c4198d130c11d41dd01ec4f57b781c40cce65d38-refs/branch-heads/7827@{#3698}` |
| Chromium 시리즈 | **branch-heads/7827** (~Chrome 142 era) | 위 SCM ref |
| 빌드 SDK | macOS 26.5 (Tahoe-era) | DTSDKName `macosx26.5` |
| 빌드 도구 | Xcode 26.5.0 (build 17F42) | DTXcode `2650` |
| 바이너리 아키 | Universal (x86_64 + arm64) | `lipo -info` |
| 코드 서명 | Developer ID Application: At Inc. (8CPD4K4TBB), notarized | `codesign -dvvv` |
| 프레임워크 크기 | "Aside Framework" 483 MB | `ls -la` (Chromium content 일체 포함) |
| AppleScript class | `BrowserCrApplication` | scripting.sdef |

**확정 사실**: Chromium fork. WebKit 기반 아님. Brave-style "single Framework with everything inside" 패키징.

### 3.2 AI / Agent 스택

| 기능 | 증거 |
|---|---|
| Gemini 내장 (1급 통합) | 문자열 `AGENT_TYPE_GEMINI_IN_CHROME`, `browser.gemini_settings`, `ai/gemini`, `ai/gemini/login`, `geminiProAutoroute`, `geminiProNoGenUi`, `Acquire Gemini Threads via sync.` |
| 자체 에이전트 매니저 | `AsideAgentManager` 심볼 |
| ChatGPT-Atlas 어댑터 | `ChatGPT Atlas`, `chatgpt-atlas`, `chatgpt.com` 문자열 (Atlas는 OpenAI의 에이전트 인프라; Aside가 호스트 측에서 어댑팅) |
| 다중 프로바이더 | 공식 changelog에서 DeepSeek, Kimi(k2.6), Claude, OpenAI, Gemini 모두 지원 명시. Per-model thinking effort 컨트롤. |
| Routines (자동화) | Free 3개 / Pro unlimited. 브라우저 히스토리 기반 routine 제안. Cron + 이벤트 트리거 (push notification, inbox event "heartbeat"). |
| Skills | 사용자 커스텀 skill 관리, 채팅에서 skill 생성 |
| Artifacts | 에이전트 출력의 "draft artifact" 폴더 저장 |
| Review-before-action | agent 동작 사전 승인 토글 |
| Ultrabrowse | "deep research" 모드 (Pro 전용) |
| Cloud handoff | long-running task의 클라우드 위임 (Pro 전용) |

### 3.3 UI / UX 스택

| 기능 | 증거 |
|---|---|
| Split View (1급) | `splitViewId` (Tabs API), `BookmarkBar_ContextMenu_OpenSplitView`, `allowSplitViewDragAndDrop`, `SPLIT_VIEW_ID_NONE` |
| Vertical Tabs | `kVerticalTabStripBottomCornerElementId`, `kVerticalTabStripTopCornerElementId`, `aside.verticalTabsTidy`, `AsideAiTabsViewport` |
| 확장 SidePanel API | extension API 매니페스트에 `sidePanel.Side` enum (`left`, `right`), `sidePanel.PanelLayout`, `sidePanel.getLayout()` 추가 — Chrome 기본 API보다 풍부 |
| Bookmarks SidePanel | `BookmarksSidePanel`, `BookmarksSidePanelCoordinator` |
| History in SidePanel | `ByDateHistoryInSidePanel` |
| Read Anything | `Accessibility.ReadAnything.SidePanel`, `..TriggeredByEmptyState` (접근성 + Reader 모드) |
| 60+ locale | macOS lproj 디렉토리 60개 + 프레임워크 Resources에 gendered locale (`af_FEMININE`, `af_MASCULINE`, `af_NEUTER`) — 매우 최신 Chrome i18n |

### 3.4 시스템 통합

| 기능 | 증거 |
|---|---|
| 자체 Password Manager | `AsidePasswordManager` 심볼. Secure Enclave 백, post-quantum 암호화 (homepage 클레임). External password manager 연결도 가능. |
| 자체 Updater | `AsideUpdater` + `AsideDaemon` 보조 프로세스 |
| Self-component 업데이트 | `aside_component_crx_cache`, `aside_component_update.log`, `ChromeFeatureState` (server-driven feature flag) |
| WebAuthn / Passkey | entitlement `com.apple.developer.web-browser.public-key-credential` |
| Secure Payment Confirmation | keychain group `secure-payment-confirmation` |
| Device Trust | keychain group `devicetrust` |
| AppleScript automation | scripting.sdef 64KB+, Chrome 호환 (tabs/windows/incognito) + `mode` 프로퍼티 (`normal`/`incognito`) |
| Payment iframe autofill | changelog: Stripe 등 hosted payment iframe에서 카드 autofill |
| Okta passcode 자동 인식 | changelog 명시 |
| 브라우저 import | Safari, Comet, ChatGPT Atlas, Firefox, Chromium, Dia, Arc, Chrome, Edge |
| Task completion 데스크탑 알림 | changelog 명시 |

### 3.5 외부 인터페이스

| 기능 | 증거 |
|---|---|
| MCP 호스트 | Aside가 MCP host로 동작 — 외부 MCP 도구가 브라우저를 driver | PiunikaWeb / 창업자 X 포스트 |
| CLI | 터미널에서 agent 세션 kick off, REPL 모드, "CLI update notice" changelog | PiunikaWeb / changelog |
| Chrome 확장 호환 | Tabs API 풀 셋(deprecated 항목 포함), SidePanel API 확장 |

### 3.6 미발견 / 부재 기능 (공개 자료 silent)

- **Mail 클라이언트 없음** (Maho Mail에 대응할 자체 앱 없음)
- **Calendar 없음**
- **Notes 없음** (있다면 changelog/홈에 노출됐을 것)
- **Reader Mode**는 `Accessibility.ReadAnything` 외 별도 UX 미확인
- **광고/트래커 차단** 부재 (개인정보 메시지는 Secure Enclave + post-quantum 위주)
- **iOS / Android** 없음 — 로드맵 silent

---

## §4 Aside 포지셔닝 / 마케팅 메시지

### 4.1 공식 헤로카피 (verbatim)

> *"The most intelligent AI assistant, but it's a browser."*
> — [aside.com](https://aside.com) 헤로

> *"The browser built to do real work for you."*
> — 보조 태그라인

> *"Today's AI browsers are broken. They never complete a task. They say 'I can't do this' every time. They stop every time the work gets real, and they still lack the basics: speed and usability."*
> — 문제 정의

> *"Aside is a browser rebuilt for people and agents. It works across your logged-in websites and handles complex work other agents can't: messages, payments, internal tools, and everything in between."*
> — 핵심 포지셔닝

> *"Unlike other AI agents that rely on integrations, Aside just uses websites and accounts directly, just like you do. That means you can ask any task in your mind anytime you need. The only wall is the imagination."*
> — 차별화 (anti-integration 메시지)

> *"Its password manager lets agents autofill credentials without exposing secrets to the AI. Sensitive actions like payments, posts, and messages wait for your approval."*
> — YC page (signal of security/control narrative)

### 4.2 벤치마크 클레임 (홈페이지)

| 모델/에이전트 | Online-Mind2Web 점수 (Aside 주장) |
|---|---|
| **Aside** | **99.0%** |
| Browser Use | 97.7% |
| GPT-5.4 | 92.8% |
| Claude Opus 4.8 | 84.0% |
| ChatGPT Atlas | 70.0% |

또한 BU-Bench-V1, Odysseys 벤치에서도 #1 주장.

> **메모**: 벤치마크 자체는 industry-standard지만, **자체 측정 + LLM-as-judge 그레이딩 + Claude Fable(현재 서스펜드 상태) 베이스라인** 사용 등 방법론 비판 존재 (§5 참조). "SOTA 주장의 시각적 효과"가 마케팅에서 큰 비중.

### 4.3 타겟 사용자 페르소나

홈페이지 푸터: **Operations, Hiring Teams, Sales, Developers, Researchers**.

특히 다음 사용 케이스 강조:
- 다중 SaaS 도구 간 작업 자동화 (CRM, 채용 ATS, GitHub 등)
- 로그인 상태 유지 자동화 (multi-step authenticated state — LinkedIn 코멘트에서 "serious engineering achievement"로 언급된 부분)
- 결제 / 메시지 / 게시물 등 민감 액션은 사용자 승인 게이트

### 4.4 라이벌 대비 자체 포지셔닝

| 라이벌 | Aside의 프레이밍 | 출처 |
|---|---|---|
| Comet (Perplexity) | "Comet과 Dia가 처음부터 해야 했던 것" | 외신 인용 (PiunikaWeb) |
| Dia (Browser Company) | 위와 동일 | 동상 |
| ChatGPT Atlas (OpenAI) | 벤치 99% vs 70% (직접 표 비교) | aside.com |
| Arc | "Arc만큼 polished + 강력한 agent 내장" | Jun Kim LinkedIn |
| Brave / Zen / Helium | 직접 언급 없음 | — |

> **시사점**: Aside는 **AI 에이전트 브라우저 카테고리 내에서 1등 자리를 노리는 포지셔닝**이지, "프라이버시 브라우저", "macOS 네이티브 브라우저", "데스크탑 OS 대체" 카테고리는 진입하지 않음. **카테고리 경계가 명확** → Maho가 다른 카테고리에서 차별화 시 충돌 적음.

---

## §5 평판 / 비판 / 알려진 한계

### 5.1 긍정 평가

- **Comet/Dia보다 첫인상 우월**:
  > *"the first impression is better than what I got from Comet or Dia after much longer trials. The combination of an actual browser that feels designed for agent use, rather than a wrapper around one"*
  > — [PiunikaWeb, Dwayne Cubbins, 2026-06-24](https://piunikaweb.com/2026/06/24/aside-ai-browser-comet-dia-alternative/)

- **벤치 신뢰성**:
  > *"the fact that the team is shipping real benchmark data instead of just marketing slides, gives it more credibility than most of these projects"*
  > — 동상

- **Multi-step 인증 작업 처리**:
  > *"Executing multi-step actions like payments and authenticated state management over weeks is where most web-scraping and browser agents completely break."*
  > — LinkedIn 코멘트

- **X 바이럴**: Jun Kim 런칭 트윗 5K+ likes
- **사용자 응답성**: 빌링 버그 발견 시 usage limit 리셋, 테스터 피드백 빠른 반영 (외신 인용)

### 5.2 비판 / 회의

- **벤치마크 방법론**:
  > *"Benchmarking against a model [Claude Fable] that has been suspended for two weeks is a bold choice. It anchors your SOTA claim to a baseline that is currently impossible for anyone to verify."*
  > — LinkedIn 코멘트

  > *"the headline SOTA runs use gpt-5.5 / openai-codex, so where does the 'smaller models' claim come from, the kimi-k2.6 run?"*
  > — LinkedIn 코멘트 (헤드라인 vs 디테일 불일치 지적)

- **LLM-as-judge 검증 부재**: Online-Mind2Web 99% 점수의 사람 검증 누락 의혹
- **HN 트랙션 약함**: 5점 / 2 코멘트 (5K X likes 대비)
- **사이드 AI 패널 버그**: *"some little bugs with the side ai"* — HN 사용자
- **신규 탭 정렬 조잡함**: vertical tab 리스트 하단에 신규 탭 생성 (외신)
- **Enterprise 회의** (피벗 전 시점이지만 캐리오버 가능): 통합-라이트 접근의 enterprise 보안 심사 통과 어려움
- **수익 모델 검증 부재**: 명명된 enterprise 고객, 사례 연구, 배포 수 모두 미공개

### 5.3 알려진 한계

- macOS only (Windows는 changelog에서 작업 중, 다운로드 페이지에 부재)
- iOS / Android 없음
- 광고 / 트래커 차단 부재
- 자체 호스팅 sync 또는 self-hostable 컴포넌트 없음 (number-one 우선순위가 아닌 듯)
- 가격 진입 장벽: Free 500 credits / 월은 routine 무거운 작업에 빠르게 소진

---

## §6 Maho ↔ Aside 9개 핵심 기능 매핑

각 항목은 Maho 코드베이스 grep으로 확인된 파일·라인 증거 기반.

### 6.1 Built-in AI 사이드패널 (Aside: Gemini 번들)

- **Maho 상태**: ✅ **SHIPPED — 프로바이더-불가지론적 (Gemini 아님)**
- 증거:
  - `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.h` — `chrome://maho-ai` WebUI + Mojo (`maho_ai.mojom`)
  - `maho-chromium/browser/ui/views/side_panel/maho_ai_side_panel_coordinator.h` — Chromium `SidePanelRegistry`에 native 등록
  - `maho-chromium/browser/ai/maho_ai_llm_client.cc:28` — `const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions"` (OpenAI-compatible 기본)
  - `maho-chromium/browser/ai/maho_unified_agent_adapter.h:76-77` — `openai` + `anthropic` BYOK 키를 prefs에 암호화 저장
- **차이**: Maho는 **multi-provider**(managed 프록시 기본 + 선택적 BYOK), Aside는 Gemini hardwire + bundled key. `정정(2026-07)`: 원문의 "Aside의 zero-setup 우위는 분명"은 stale — Maho의 Maho-managed 경로는 access token만 있으면 키 없이 동작하므로 **zero-setup은 현재 동등(parity)**. Aside 우위 아님 (evidence 원장 §1).

### 6.2 Agentic Browsing 시스템 (Aside: AsideAgentManager)

- **Maho 상태**: ✅ **SHIPPED — Swiftide agent 런타임 + human-in-the-loop 승인 게이트** `정정(2026-07)`
- 증거:
  - `maho-chromium/browser/ai/maho_unified_agent_adapter.h` — `MahoUnifiedAgentAdapter : public MahoAiRuntimeAdapter`, `SubmitMessage`, `CancelCurrentTurn`, `RespondToApproval` (사람-인-루프 승인 게이트)
  - `maho-chromium/browser/ai/maho_browser_tool_registry.h` — agent tool registry
  - `maho-chromium/browser/ai/maho_browser_tool_executor.h` — tool 실행기
  - `maho/crates/maho-agent/src/swiftide_backend.rs` — **현재 캐논 agent 백엔드**: managed 프록시 경로 + BYOK fallback + tool 셋(fs/shell/browser/mail/MCP/CLI)
- **차이**: `정정(2026-07)`: 원문은 "ACP 표준 프로토콜 + watchdog가 현재 아키텍처"라 서술했으나 이는 stale. 현재 캐논 agent 경로는 `maho-agent`의 **Swiftide 멀티턴 백엔드**이며, managed 빌링 프록시를 기본 사용한다 (evidence 원장 §1, ADR 0009 Decision Drivers). ACP 계열 심볼은 잔재/보조 경로로 취급하고 "현재 아키텍처"로 인용하지 말 것. Aside는 ChatGPT-Atlas 의존.

### 6.3 First-class Split View (Aside: splitViewId in Tabs API)

- **Maho 상태**: ✅ **SHIPPED**
- 증거:
  - `maho-chromium/browser/ui/views/split_view/maho_split_view_controller.h` — `MahoSplitViewController` with `AddSplit()`, `AddSplitWithURL()`, `RemoveSplit()`, `SwapSplit()`, `ToggleSplit()`, `ToggleSplitWithURL()`, `IsSplitActive()`
  - `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.h` — upstream patch
  - `maho/crates/maho-core/src/split_view_manager.rs` (156 lines) — Rust 상태 관리
- **차이**: Maho는 native Views + Rust state. Aside는 동일 + **extension-facing `splitViewId` API** (JS에서 split 제어 가능). Maho 갭: extension API 미노출.

### 6.4 Vertical Tabs + Tidy (Aside: 자동 그룹화)

- **Maho 상태**: ✅ **SHIPPED — vertical tab UI + AI Tab Tidy** `정정(2026-07)`
- 증거:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h` — `MahoSidebarTabListView` + `SidebarTabRowView` + `SidebarFolderRowView` + DnD (`WriteDragDataForView`, `GetDropCallback`) + 핀 sections + folder expand/collapse + 인라인 rename + hover preview
  - `maho_sidebar_state_models.h` — 섹션 모델 (pinned, normal)
  - sidebar 디렉토리 ~25 파일 (spaces, favorites, search 등 포함)
  - AI Tidy: `maho_ai.mojom` `RequestTabTidy(array<TabInfo>) => (array<TidyFolder>)` + `ApplyTabTidyFolders(...)`, 사이드바 `wand-2` 아이콘 → 폴더 제안 다이얼로그 (evidence 원장 §2)
- **차이**: `정정(2026-07)`: 원문의 "AI tidy 부재 — 명확한 갭"은 stale. Tab Tidy는 이제 **shipped-in-browser**(Mojo 배선 + full `chrome` 링크 확인). 남은 리스크는 신규 개발이 아니라 **데모/수동 QA 신뢰성**(webui `.d.ts` 스텁이 `TabInfo`/`TidyFolder` 누락 — IDE 타입체크 한정, 빌드 영향 없음).

### 6.5 Extension SidePanel API (Aside: left/right 레이아웃)

- **Maho 상태**: 🟡 **PARTIAL — 자체 AI 패널만 등록, extension-facing API 없음**
- 증거:
  - `maho-chromium/browser/ui/views/side_panel/maho_ai_side_panel_coordinator.h` — `SidePanelRegistry`에 자체 등록
  - extension API 추가 (left/right layout) Mojo / JS 브리지 **없음**
- **차이**: Maho는 Chromium 기본 `SidePanelRegistry` 위에 자체 패널만. Aside는 **third-party extension에 layout 제어 노출**. 갭. (단, 사용자 수요는 적음.)

### 6.6 자체 Password Manager (Aside: AsidePasswordManager)

- **Maho 상태**: 🟡 **구현 존재하나 gated/insecure — "secure agent 볼트" 클레임 불가** `정정(2026-07)`
- 증거:
  - `maho/crates/maho-core/src/password_manager.rs` — `struct PasswordManager`, `list_passwords()`, `search_passwords()`, `add_password()`, `delete_password()` (단, 현재 시크릿을 평문 `Option<String>`로 메모리 보관 — 암호화 미적용)
  - `maho/crates/maho-types/src/passwords.rs` — `SavedPassword` (id, domain, username)
  - `maho-chromium/browser/ui/webui/maho_settings/maho_settings_password_helpers.h` — 설정 UI 통합
  - `maho/crates/maho-import/src/parsers/chromium/passwords.rs` + `firefox/passwords.rs` — Chrome/Firefox import
  - `maho/crates/maho-import/src/decrypt/chromium_keychain.rs` + `firefox_nss.rs` — keychain/NSS 복호화
- **차이**: `정정(2026-07)`: 원문의 "거의 동등, Secure Enclave/post-quantum 매칭 가능, 마케팅 메시지 보강"은 **위험한 stale**. 현재 보안 문서(`docs/password-manager-security-fix-plan.md`)는 **평문 디스크 저장(SQLCipher 키 미배선), 잔존 평문 백업, reveal 시 OS/생체 재인증 부재**를 명시하며, 신규 저장 기능은 Milestone 1 전 사용자 노출 금지. 또한 agent tool 셋에 **password-manager 도구가 배선돼 있지 않음**. 따라서 "secure agent autofill / shipped 볼트" 메시지는 **보안 fix 완료 전까지 금지** (evidence 원장 §4).

### 6.7 자체 Updater (Aside: AsideUpdater + AsideDaemon)

- **Maho 상태**: ✅ **SHIPPED + 설계 우수 (MahoUpdateManager)**
- 증거:
  - `maho-chromium/browser/updates/maho_update_manager.h` — singleton + `CheckForUpdates()`, `ApplyUpdateAndRestart()`, `SetChannel()` (stable/beta/dev/canary), observer
  - `maho-chromium/browser/updates/platform_updater_delegate.h` — 플랫폼 추상화
  - 보조: `crash_loop_sentinel.h`, `rollout_bucket.h`, `maho_update_signature.h`, `maho_update_server_url.h` — **staged rollout, EdDSA 서명, crash-loop 가드**
  - `docs/maho_browser_update_architecture.md` — macOS Sparkle 2 / Windows MSIX / Linux 패키징 설계
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.h` — 사이드바 업데이트 알림
- **차이**: Maho의 updater 설계는 **Aside보다 견고**. cross-platform + staged rollout + EdDSA 서명 + crash-loop 가드 모두 갖춤. Aside는 별도 `AsideDaemon` helper 프로세스 모델 (macOS/Win에서는 일반적이지 않음).

### 6.8 Chrome 확장 호환 (Aside: 풀 호환)

- **Maho 상태**: ✅ **SHIPPED — 풀 Chromium engine + Rust bridge**
- 증거:
  - `maho-chromium/browser/extensions/maho_extension_state_bridge.h` — `class MahoExtensionStateBridge : public extensions::ExtensionRegistryObserver`, `RequestEnable/Disable/Uninstall` + 정책 체크
  - 업스트림 Chromium extension engine 상속
  - `maho-chromium/browser/net/maho_webstore_ua_throttle.h` — WebStore UA 처리
  - `maho/crates/maho-extension/` — Rust-side 관리 크레이트
- **차이**: 동등. 양쪽 모두 Chrome Web Store 호환. Maho는 추가로 **상태를 Rust core에 노출**해서 설정 UI 통합 깊이가 있음.

### 6.9 macOS 네이티브 폴리시 + 멀티플랫폼 (Aside: macOS only, 60+ locales)

- **Maho 상태**: 🟢 **MULTI-PLATFORM 우위 / 🟡 폴리시 미성숙**
- 증거:
  - `maho/macos-shell/` — AppKit + Xcode 프로젝트 + Assets.xcassets
  - `maho/ios-shell/` — `MahoApp.swift`, `Views/`, `MahoWidget/`, `ShareExtension/`, `web-ai/` + Xcode
  - `maho/android-shell/` — Gradle 프로젝트 + JNI 브리지 (`maho-jni`)
  - `maho-chromium/` — 데스크탑 (macOS/Windows/Linux)
  - 단, AGENTS.md: *"Active implementation work is happening in the Chromium fork"* (Swift 셸은 reference)
- **차이**: **Maho 우위 — Aside는 macOS only.** Maho는 iOS + Android 셸 보유. **현재 Swift 셸이 reference-only**라는 점이 risk → 모바일 마케팅 활용 시 구체 출시 일정 명확화 필요.

### 6.10 매핑 요약 표

| # | 기능 | Aside | Maho | 우위 |
|---|---|---|---|---|
| 1 | AI 사이드패널 | ✅ Gemini 번들 | ✅ Managed 프록시(키 불필요) + 선택적 BYOK | 동등 `정정(2026-07)` (원문 "Aside onboarding 우위" stale) |
| 2 | Agentic 시스템 | ✅ AsideAgentManager | ✅ Swiftide 백엔드 + 승인 게이트 | Maho `정정(2026-07)` (원문 "ACP watchdog" stale) |
| 3 | Split View | ✅ + JS API | ✅ native만 | Aside (extension API), Maho (구현 깊이 동등) |
| 4 | Vertical Tabs | ✅ + Tidy | ✅ folder/sections **+ Tab Tidy shipped** | 동등 `정정(2026-07)` (원문 구 Tidy-gap 표현 stale) |
| 5 | Extension SidePanel API | ✅ left/right | 🟡 자체만 | Aside |
| 6 | Password Manager | ✅ SE + PQ 클레임 | 🟡 구현 존재하나 gated/insecure | Aside `정정(2026-07)` (Maho는 보안 fix 전까지 secure 클레임 불가) |
| 7 | Updater | ✅ daemon 모델 | ✅ Sparkle/MSIX + staged | **Maho** (설계 견고) |
| 8 | Chrome 확장 | ✅ | ✅ | 동등 |
| 9 | 멀티플랫폼 | ❌ macOS only | 🟡 macOS 데스크탑 + iOS/Android 셸(reference-only, 출시 미확정) | Maho (코드 폭), 단 모바일 런치 패리티 주장 금지 |

---

## §7 Maho 고유 강점 (Aside가 못하는 것)

### 7.1 모바일 네이티브 셸

- `maho/ios-shell/` — Swift + WKWebView + 위젯 + 공유 익스텐션 + `web-ai/` 모듈
- `maho/android-shell/` — Gradle + JNI (`maho-jni`)
- Aside의 공개 로드맵에는 모바일이 **silent** — 4일 차 신생 회사가 모바일까지 출시할 가능성 낮음. **6-12개월의 windowing 우위**.

### 7.2 Rust-native 백엔드 아키텍처

- `maho/crates/maho-core/`, `maho-types/`, `maho-ffi/`, `maho-storage/`, `maho-engine/`, `maho-extension/`, `maho-jni/`, `maho-bench/` (8+ crates)
- 모든 로직 (tabs, spaces, passwords, split view, settings, sync) Rust로 → 테스트 가능, 메모리 안전, cross-platform
- Aside는 Chromium C++ 레이어에 로직 거주

### 7.3 Spaces (테마 탭 그룹)

- `chrome://maho-space-create` (테마 picker)
- `chrome://maho-space-config`
- `chrome://maho-space-manage`
- 사이드바의 space dot navigation
- 공간별 시각 테마 + 탭 관리
- Aside의 vertical tabs + tidy와 다른 차원의 조직화 패러다임

### 7.4 Boost (사이트별 콘텐츠 injection)

- `chrome://maho-boost` + `maho_boost_injection_handler`
- Aside에 직접 대응 없음 — Maho 고유 차별

### 7.5 Easel + Notes (1급 WebUI 표면)

- `chrome://maho-easel` — 내장 드로잉 캔버스
- `chrome://maho-notes` — 탭별 노트
- Aside: 부재

### 7.6 Command Palette

- `maho-chromium/browser/ui/views/command/` — native Views 커맨드 오버레이
- 키보드 우선 launcher (Arc/Linear/Raycast 류)
- Aside: 부재 (확인된 한)

### 7.7 광고 / 트래커 차단

- `maho-chromium/browser/net/maho_ad_block_throttle.cc` + cosmetic filters
- Aside: **완전 부재** — Aside의 "privacy" 메시지는 password vault에만 머무름
- Maho 차별 마케팅 포인트 후보

### 7.8 Maho Mail (별도 Tauri 앱)

- `mail/` — Tauri v2 + React 19 + Vite
- 브라우저 외부 productivity 표면 추가
- Aside: 부재

### 7.9 Sync Relay 자체 호스팅

- `maho/relay/` — 독립 Rust workspace, server + entity + migration crates
- E2EE + self-hostable
- Aside: 공개된 sync 서버 없음

### 7.10 ACP Watchdog 프로세스

- `maho-acp-watchdog` — 감독된 AI agent 프로세스 모델
- Aside의 in-process AgentManager보다 견고 (crash isolation)
- `정정(2026-07)`: 현재 캐논 agent 경로는 `maho-agent`의 **Swiftide 백엔드 + managed 프록시**(§6.2 참조). ACP watchdog을 "현재 agent 아키텍처"로 인용하지 말 것 — 잔재/보조 경로로 취급.

---

## §8 Aside가 앞서는 영역 (Maho 갭)

> `정정(2026-07)`: 원문의 §8.1~§8.5는 대부분 **이미 해소되었거나(구현 완료) launch 정책상 갭이 아닙니다**. 아래에 현재 상태를 반영해 갱신했습니다 (evidence 원장 §1~§5).

### 8.1 Zero-setup AI 온보딩 — `정정(2026-07)`: 갭 아님(parity)

- Aside는 Gemini 키 번들 → 다운로드 즉시 AI 사용 가능
- Maho도 **Maho-managed 프록시로 키 없이 즉시 동작(shipped-in-browser)**; BYOK는 선택적 fallback. 원문의 구 BYOK-only 표현(→ 온보딩 마찰 주장)은 stale
- 현재 과제는 신규 개발이 아니라 first-run UX **폴리시/데모/QA**

### 8.2 Tab Tidy (AI 자동 정리) — `정정(2026-07)`: 갭 아님(shipped)

- Aside의 `aside.verticalTabsTidy`는 AI 기반 자동 그룹화
- Maho도 **Tab Tidy shipped-in-browser** (`RequestTabTidy`/`ApplyTabTidyFolders` Mojo, `wand-2` 사이드바 액션). 원문의 "수동 폴더/섹션만"은 stale
- 남은 리스크: 데모/수동 QA 신뢰성

### 8.3 Routines (자동화 스케줄러) — `정정(2026-07)`: 구현됨, launch 비커밋

- cron + 이벤트 트리거 + push notification heartbeat (Aside)
- Maho도 **구현 존재**: CLI `Routine` 서브커맨드 + MCP `browser_routines_*` + `chrome://maho-routines` WebUI (Max-tier 게이트)
- 단, `docs/demos/README.md`가 launch 클레임에서 명시 제외 → **출시 마케팅 금지**

### 8.4 MCP 호스트 — `정정(2026-07)`: 구현됨(tools-only), launch 비커밋

- 외부 MCP 도구가 브라우저를 driver (Aside)
- Maho도 **`maho-browser-mcp` tools-only 표면 구현** (ADR 0014: resources/prompts는 v1 NO-GO). 원문의 "미진입"은 stale
- launch 클레임 제외 유지

### 8.5 CLI 인터페이스 — `정정(2026-07)`: 구현됨, launch 비커밋

- 터미널에서 agent 세션 launch / REPL
- Maho도 **`maho-cli` 구현**: `Mcp`/`Run`/`tool list|call` 서브커맨드. 원문의 "미진입"은 stale
- launch 클레임 제외 유지

### 8.6 Extension SidePanel left/right API

- Chrome 기본 API 확장
- 영향 범위는 작지만 power user / 확장 개발자에게 차별
- Maho 미진입

### 8.7 macOS 단일 빌드의 UX 폴리시 성숙도

- Aside는 60+ locale + gendered i18n + macOS 26.5 SDK 활용
- Maho는 멀티플랫폼 폭이 넓은 만큼 단일 플랫폼 폴리시 깊이는 낮을 수 있음
- **measurement 필요 (실제 macOS UX 비교 테스트)**

### 8.8 마케팅 / 출시 momentum

- YC F25 배지, 5K+ X likes, Comet/Dia 대비 매체 호평
- 단기적으로 "AI 브라우저" 카테고리 화제 점유율 Aside가 가져갈 가능성
- Maho는 출시 전 — **포지셔닝을 다른 카테고리에 두는 것이 중요** (직접 충돌 회피)

---

## §9 Maho를 위한 권장 사항

> `정정(2026-07)`: 원문의 §9는 "이 기능들을 만들자"는 개발 로드맵이었으나, 대부분 **이미 구현**되었습니다. 아래는 현재 벤치마크 우선순위로 재작성했습니다 (evidence 원장 §1~§6 기준). 핵심 전환: **신규 기능 개발 → 데모/QA/포지셔닝 + 보안 fix + launch 정책 준수.**

### 9.1 즉시 행동 (1-2 sprint)

1. **Zero-setup AI — 유지·폴리시(parity/maintain)**:
   - Maho-managed 프록시 경로는 이미 shipped(키 없이 동작). 신규 개발 아님
   - first-run 경험을 **데모/수동 QA로 다듬기**; 선택적 BYOK fallback 보존
   - evidence caveat: agent 기본 모델 slug(`google/gemini-3-flash-lite:free`)와 ADR 0009 Annex A allowlist(`gemini-3` 계열) 간 정합은 이미 맞춰짐(과거 `gemini-2.5-flash` drift 해소). 허용목록 회전 시 재확인만 필요

2. **Tab Tidy — shipped, 데모/QA 크리티컬**:
   - AI 클러스터링은 이미 Mojo로 출하됨 (`RequestTabTidy`/`ApplyTabTidyFolders`)
   - 남은 리스크는 **데모 촬영·수동 QA 신뢰성** (webui `.d.ts` 스텁이 `TabInfo`/`TidyFolder` 누락 — IDE 타입체크 한정)

3. **Password Manager — 보안 fix 완료 전 메시지 동결**:
   - 현재 평문 저장(SQLCipher 미배선)/평문 백업/reveal 재인증 부재 (`docs/password-manager-security-fix-plan.md`)
   - **"secure agent autofill / shipped 볼트" 마케팅 금지** — Milestone 1 보안 fix 이후로 연기
   - agent tool 셋에 password 도구 미배선 → agent-vault 클레임 불가

### 9.2 단기 (1-2달)

4. **MCP 호스트 — 구현됨(tools-only), launch 비커밋 유지**:
   - `maho-browser-mcp`가 tools-only 표면 제공 (ADR 0014: resources/prompts v1 NO-GO)
   - `docs/demos/README.md` launch 클레임 제외 준수 → **출시 마케팅에 넣지 말 것**

5. **Routines — 구현됨, launch 비커밋 유지**:
   - CLI/MCP/`chrome://maho-routines` 표면 존재 (Max-tier 게이트)
   - Maho Mail 트리거 연동은 향후 차별 아이디어이나, **현재 launch 클레임 금지**

6. **CLI — 구현됨, launch 비커밋 유지**:
   - `maho-cli` `Mcp`/`Run`/`tool` 서브커맨드 존재
   - 개발자 페르소나 자산이나 **출시 마케팅 제외** (README 하드 제약)

### 9.3 중장기 (3-6개월)

7. **iOS / Android — 코드 보유, 출시 미확정 (과대주장 금지)**:
   - `ios-shell`/`android-shell` 코드는 있으나 **reference-only**
   - "폰에서도 동작"을 **런치 패리티로 주장 금지**; 구체 출시 커밋 확정 후에만 마케팅

8. **Linux 데스크탑**:
   - Maho updater 설계에 Linux 패키징 포함됨 → 출시 가속
   - Aside 미진입 시장 → first-mover

9. **검증 가능 privacy 마케팅**:
   - Maho는 proprietary/closed-source이므로 **"소스 공개/외부 감사" 류 OSS 프레이밍 금지**
   - relay E2EE("데이터를 못 본다") + LLM 프록시 물리 분리(ADR 0009)를 검증 서사로 활용

### 9.4 포지셔닝 (마케팅)

10. **AI 브라우저 카테고리 직접 충돌 회피**:
    - Aside의 "AI가 일을 끝낸다" 메시지와 정면 충돌 금지
    - Maho 메시지: **"You + AI"** 또는 **"AI 도구 + Privacy 아키텍처"**
    - 사용자 멘탈 모델: "AI에게 위임" vs "AI를 도구로 사용" — 후자 선택

11. **모바일 마케팅은 출시 커밋 이후로**:
    - 코드 존재 ≠ 출시. reference-only 상태에서 "iPhone/Android에서도" 헤드라인 금지
    - 출시 확정 시에만 모바일 데모 자산화

12. **벤치마크 — SOTA 숫자 아닌 데모 기반으로**:
    - Aside의 99% SOTA 클레임은 방법론 회의 대상 → Maho는 **숫자 경쟁 지양**
    - `docs/brand/BRAND.md` §5/§7: 자가당착적·미검증 벤치 금지, 사실·공식 출처만
    - 차별 자산은 **재현 가능한 데모**(동일 task를 실제로 수행하는 영상), 조작된 벤치 숫자 아님

---

## §10 부록

### 10.1 바이너리 분석 명령 모음 (재현 가능)

```bash
# 설치 위치 확인
mdfind "kMDItemKind == 'Application'" | grep -i aside

# 코드 서명
codesign -dvvv /Applications/Aside.app

# Entitlements
codesign -d --entitlements - /Applications/Aside.app

# Framework Info.plist
/usr/libexec/PlistBuddy -c "Print" "/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Resources/Info.plist"

# 핵심 심볼/문자열 추출
strings "/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.626.1/Aside Framework" | \
  grep -iE "(SidePanel|SplitView|VerticalTab|Aside[A-Z]|gemini|chatgpt|MCP|routine|tidy)"

# AppleScript SDEF
cat /Applications/Aside.app/Contents/Resources/scripting.sdef

# 사용자 프로필 위치
ls ~/Library/Application\ Support/Aside*
defaults read at.studio.AsideBrowser
```

### 10.2 출처 정리

#### 1차 (바이너리)
- `/Applications/Aside.app/Contents/Info.plist`
- `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Resources/Info.plist` (SCM ref)
- `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.626.1/Aside Framework` (strings)
- `/Applications/Aside.app/Contents/Resources/scripting.sdef`

#### 2차 (공식)
- [aside.com](https://aside.com) — 홈페이지, 헤로카피
- [aside.com/pricing](https://aside.com/pricing) — 가격
- [aside.com/policy/terms](https://aside.com/policy/terms) — 법인명
- [docs.aside.com/changelog/native](https://docs.aside.com/changelog/native) — Windows 빌드, import 소스
- [docs.aside.com/changelog/components](https://docs.aside.com/changelog/components) — Routines, Skills, multi-provider
- [ycombinator.com/companies/aside](https://www.ycombinator.com/companies/aside) — F25, 팀, 포지셔닝
- [ycombinator.com/launches/Oj4-aside-...](https://www.ycombinator.com/launches/Oj4-aside-live-answers-for-enterprise-tech-sales) — 피벗 전 sales tool 시절

#### 3차 (외신/SNS)
- [Jun Kim LinkedIn 런칭 포스트 (2026-06-23)](https://www.linkedin.com/posts/therne_today-were-launching-aside-yc-f25-an-activity-7475267050425282560-W9Qz)
- [PiunikaWeb 리뷰 (2026-06-24)](https://piunikaweb.com/2026/06/24/aside-ai-browser-comet-dia-alternative/)
- [HN 스레드 #48651217](https://news.ycombinator.com/item?id=48651217)
- [chreporter.com Jun Kim 프로필 (2026-04-19)](https://chreporter.com/how-jun-kim-built-aside-from-a-sales-knowledge-problem-into-a-y-combinator-backed-ai-startup/)

### 10.3 미해결 / 추가 조사 필요

1. **"At Inc." vs "Aside Computer Inc."**: 번들 ID `at.studio` 잔재의 정확한 이유 — 별도 holding entity 가능성
2. **펀딩 규모**: YC 표준 deal 외 추가 라운드 미확인
3. **벤치 99% 방법론**: LLM-as-judge 그레이딩 + Claude Fable 베이스라인의 통계적 유효성 — 독립 검증 자료 부재
4. **Windows GA 시점**: changelog에 작업 흔적, 홈페이지에는 부재 — 정확한 출시일 미상
5. **번들 모델 구성**: Free/Pro/Max credit pool의 실제 모델 mix (gpt-5.5? Claude? Gemini? kimi-k2.6?) 미공개
6. **Enterprise 고객**: 명명된 고객 / 사례 / 매출 모두 비공개
7. **`AsideHQ.com` 도메인 상태**: 피벗 전 sales tool 시절 도메인 — 현재 리다이렉트 여부 미확인
8. **Maho iOS 셸 출시 실제 상태**: AGENTS.md "reference-only" 명시 — 출시 의지/타임라인 내부 확정 필요

---

**문서 끝.**
