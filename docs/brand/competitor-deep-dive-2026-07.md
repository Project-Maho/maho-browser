# maho 경쟁 심층 리서치 — Aside / Arc 난민 정서 / Agentic 지형

**조사일:** 2026-07-19 (competitor-research-2026-07.md 후속 심층)
**방법:** HN Algolia item API로 코멘트 트리 직접 수집 + aside.com 공식홈 1차 소스.
**초점:** (1) Aside 심층 — 사용자가 지목한 최대 경쟁자, (2) Arc 난민 실제 정서, (3) agentic 브라우저 지형.

---

## 1. Aside (사용자 지목 최대 경쟁자) — 1차 소스 갱신

**도메인 주의:** `aside.new`는 Cloudflare 봇 차단(curl 000). 실제 접근 가능 도메인은 **`aside.com`**.

**최신 태그라인 (2026-07 기준, 이전 케이스스터디의 "operating system for AI era"에서 진화):**
- 헤드라인: **"The browser built to do real work for you"**
- 서브: **"The most intelligent AI assistant, but it's a browser."**
- Backed by Y Combinator.

**포지셔닝 논지 (공식홈 "Introducing Aside" 원문):**
> "Today's AI browsers are broken. They never complete a task. They say 'I can't do this' every time. They stop every time the work gets real, and they still lack the basics: speed and usability. Aside is a browser rebuilt for people and agents."

**핵심 주장:**
- **Unlimited capability** — "Anything you do in a browser, Aside can do for you." 통합(integration)이 아니라 로그인된 웹사이트를 직접 사용 (사람처럼).
- **Signing in / Communications / Docs** — 이메일·대시보드·내부툴에 로그인해서 댓글·답장·후속처리·파일작업 대행.
- **SOTA benchmark 주장** — Online-Mind2Web 99.0%, BU-Bench-V1, Odysseys에서 #1, "surpassing OpenAI(GPT-5.4 92.8%), Anthropic(Claude Opus 4.8 84.0%), Browser Use(97.7%), ChatGPT Atlas(70.0%)". **⚠️ 자체 벤치마크, 제3자 미검증.**
- **Local memory** — 브라우징 히스토리를 로컬 메모리로, 기기에만 저장·비공유. "Local Guard"로 민감 액션 게이팅.
- 모델: GPT-5.5 High 표시.

**Aside의 본질:** maho와 **정반대 극단**. Aside = "AI가 당신 대신 진짜 일을 함(full self-driving)". maho = "hand-on-wheel, 시켰을 때만". 같은 Chromium 기반이지만 철학이 대척점.

**Aside 약점 (maho 기회):**
- HN 반응 미미 (2026-06-23 "Aside: The AI Browser" 5pts 2c). 커뮤니티 검증 안 됨.
- 벤치마크 자화자찬 = HN/개발자 회의감 유발 (아래 난민 정서 참조).
- "AI가 결제·내부툴 대행" = 신뢰·실패비용 우려 정면.

---

## 2. Arc 난민 실제 정서 (HN 1차 코멘트, 최고 가치 신호)

**출처:** HN "Ask HN: What Arc/Dia features should we prioritize?" (2025-09, 31pts 64c). OP는 또다른 YC S24 Arc 대체 브라우저 팀. Arc 난민이 **직접 뭘 원하는지 말한** 스레드.

**핵심 정서 (거의 그대로 maho 포지셔닝을 지지):**

1. **AI 자율행동 거부 (강력·반복):**
   > "I want a browser to let me do more things, not fewer. I barely trust myself to press the right buttons on a page, I certainly don't trust an AI to buy shit for me."
   > "If AI agents must be a part of the product, it should be possible to turn them off entirely, not leaving the slightest trace anywhere."
   > (Aside식 자동 결제/소비에 대해) "Do we really need to automate consumption behavior now?"
   → **maho의 hand-on-wheel + "Never forced. Never mandatory."가 이 정서를 정확히 저격.**

2. **Zen이 현재 Arc 난민의 기본 도피처:**
   > "Zen scratches most of my Arc itches." / "Zen has done a good job of replicating most of the best parts of Arc."
   - BUT Zen 한계 (= maho 기회):
     - Firefox 기반 → 일부는 Chromium/확장 호환 원함 (uBlock은 장점이나 Chrome 확장 불가)
     - **Profiles 부재**: "I tried to migrate to Zen but I missed Arc's profiles. Container tabs do not replace profiles. I switch personae/profiles by switching spaces." (반복 언급)
     - **Maho Mini(임시 창) 부재**: "the big missing piece IMO is Maho Mini, which was workflow-changing."

3. **가장 원하는 Arc 기능 (우선순위 순):**
   - Vertical tabs / Workspaces (기본)
   - **Profiles = 같은 창에서 여러 페르소나 (쿠키·비번·카드 분리)** — "Besides Arc no other Chromium-based browser allows this. Would be really nice to have it in another browser."
   - Maho Mini / 임시 브라우저 창
   - 일관된 디자인 언어: "Don't use 4 sets of UI fonts and 7 color schemes in the same window."
   - 스마트 reader mode (요약 말고 시각분석으로 광고 제거)

4. **AI 회의감 (Q&A/요약조차):**
   > "I have a hard time understanding how this is useful to anyone. I've tried these tools and never see the point."
   → 단, 일부는 Gmail 답장 rephrasing 등 실사용 인정. AI는 **옵션·비침습**이어야 수용.

---

## 3. Agentic 브라우저 지형 (maho가 역발상으로 설 자리)

```
Aside    "do real work for you" — full agent, 로그인사이트 직접조작, 결제까지
Dia      "reads between the tabs" — Morning Brief, Slack/Notion 통합 agent
Edge     Copilot Vision — 화면 보고 task 완료
Opera    Aria — 무가입 AI, 이미지생성, YouTube 번역
Comet/Atlas  (Perplexity/OpenAI) — search/work agent
────────────────────────────────────────────────
maho     "AI when you want it" — hand-on-wheel, 시켰을 때만, 끌 수 있음
```

**시장 전체가 "AI가 대신 한다"로 수렴 중.** HN 정서는 이에 대한 **피로감·불신**이 뚜렷. maho의 절제된 AI는 소수지만 **명확한 반대 진영(파워유저·개발자·프라이버시 중시)**을 정확히 겨냥.

---

## maho 브랜드 정립에 직결되는 최종 시사점

### 차별화 3축 (심층조사로 검증·강화됨)

1. **"살아있는 Arc의 후계 + Arc가 못 준 Chromium Profiles"**
   - Arc 단종 + Zen의 Profiles 부재 = maho의 **가장 구체적인 빈 틈**.
   - "Chromium 기반에서 Arc식 profiles(페르소나별 쿠키/세션)를 주는 유일한 유지되는 브라우저" — HN 난민이 명시적으로 요구한 것.

2. **hand-on-wheel AI = agent 피로감 시장의 반대 진영 리더**
   - Aside("do real work for you")의 정확한 안티테제로 포지셔닝 가능.
   - "AI를 완전히 끌 수 있다"를 명시적 약속으로 (HN 난민 요구사항 직결).
   - 단, 경쟁사 직접 네이밍은 카드뉴스/SNS 금지 → "browsers that act for you" 식 일반화 표현.

3. **solo-dev 직접 책임** — 전 경쟁사(YC/기업/비영리) 대비 유일. 신뢰·진정성 레이어.

### 피해야 할 것 (심층조사로 재확인)
- "calm" 리드 → Arc/Zen 소유. 미투.
- AI 능력 자랑/벤치마크 경쟁 → Aside 영역, 자본력 열세 + HN 반감.
- privacy 단독 리드 → Brave/Safari/Firefox/Vivaldi 레드오션.

### Profiles wedge — 제품 코드 실측으로 확정됨 (2026-07-19)

**결론: maho는 Arc와 동일한 "Space=조직화 / Profile=격리" 모델을 실제로 구현·실동작.**

```
Space 구성                                    격리 수준
──────────────────────────────────────────────────────────────
profile_id 기본/빈값 → Default 디렉터리        격리 없음 (쿠키 공유)
비-기본 profile_id → MahoProfile_{id} 디렉터리  완전 격리 (쿠키/세션/스토리지)
같은 profile_id 공유하는 여러 Space            그들끼리 쿠키 공유, 타 프로필과 격리
```

- 통합테스트(`maho_sidebar_spaces_interactive_test.cc`): 새 Space는 기본 Default 프로필로 생성 확인.
- OTR/시크릿 격리(`MahoPrivateContextPolicy`): fail-closed 능력 매트릭스 완비+테스트 (시크릿에서 MahoCore 스토리지 미개방, MCP 탭 열거 차단).
- 미구현: R-11(사이드바 OTR 렌더링), R-12/R-13(비주얼/프리뷰) = GTEST_SKIP 스텁. **격리 로직 자체가 아니라 검증 테스트만 미완.**
- 혼동 주의: `profile_import.rs`/`workspace_manager.rs`의 "프로필/워크스페이스"는 AI 에이전트 설정(시스템프롬프트/MCP/CLI)이지 브라우저 스토리지 프로필이 아님.

**Arc 대응:** 개념적으로 동일. Arc도 Space=조직화(현재 프로필 상속), Profile=격리(별도 배정). maho의 "Arc식 Profiles"는 정당한 표현. (단 Arc 쪽은 Arc 모델 지식 기반 — arc.net 1차 대조는 미실시.)

**⚠️ 카피 정확성 (기본값 격리 안 됨):**
- ❌ 금지: "Space마다 자동으로 로그인/쿠키가 분리된다" → 거짓. 기본은 Default 공유.
- ✅ 정확: "Space에 프로필을 배정하면 페르소나별로 쿠키·세션이 완전히 격리된다 (Arc와 동일 동작)."

**wedge 확정:** "Chromium 기반에서 Arc식 Profile 격리(같은 창 내 페르소나별 쿠키 분리)를 유지하는, 계속 shipping되는 브라우저" — Zen(Firefox, profiles 부재)이 못 주는 것을 정확히 제공. HN 난민이 명시 요구한 기능. **maho 최강 차별 wedge.**

### 추가 검증 필요 (미해결)
- Maho Mini식 임시창(temporary window) 지원 여부 — 미실측.
- Arc 쪽 Space/Profile 모델 arc.net 1차 소스 대조 (현재 Arc 모델 지식 기반, 높은 확신이나 미재검증).
