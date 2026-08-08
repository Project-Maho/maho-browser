# phibrowser.com — 랜딩 참고 분석

**조사일:** 2026-07-19 (실측 — 공식 사이트 DOM/computedStyle 추출)
**목적:** maho 랜딩페이지 디자인 참고. Phi는 maho의 직접 경쟁자(macOS 네이티브 Chromium + AI).

---

## 1. 정체성 — maho와의 관계

**겹치는 포지션 (거의 동일):**
- macOS 전용 네이티브 Chromium ("native macOS soul", Apple Silicon, macOS 14+)
- 프로액티브 AI 친구 ("your AI friend", 이름 붙이는 개인화)
- local-first, "AI 끄는 스위치" 제공 ("disable all AI features. No offense taken")
- CLI/Playwright/agent 친화 ("Ready for agents", OpenClaw 언급)
- 메시징 앱 연동 = de facto 모바일 (maho의 Discord/모바일 전략과 유사)

**갈리는 지점 (maho 차별화 근거):**
- **AI 강도:** Phi = 강한 프로액티브 ("predicts intentions", "unprompted", "heavy lifting"). maho = hand-on-wheel(시켰을 때만). → Phi는 오히려 Aside 쪽에 가까움. maho의 "절제된 AI"가 여기서도 차별점.
- **오픈소스:** Phi = "the core is open-source... transparent, inspectable". maho = **proprietary**. → maho는 이 카드를 못/안 씀. 신뢰는 다른 축(solo-dev, local-first)으로.
- **AI 브랜딩:** Phi = "There is no Phi AI, only your AI friend"(브랜드 없는 개인 AI). maho도 유사 톤 가능.

---

## 2. 시각 디자인 (실측 computedStyle)

```
배경:     #000 (순검정) — maho 웹사이트 #0a0a0a와 근접
텍스트:    #fff (흰색)
헤드라인:  ivypresto-headline (프리미엄 디스플레이 serif), 129.6px(!), weight 600
          → maho의 Instrument Serif 디스플레이와 같은 "serif 히어로" 전략
본문:     ivyepic-variable (Adobe Fonts, sans/serif 혼용)
버튼:     투명 배경 + radius 0 (극단적 미니멀, 테두리/밑줄류로 추정)
글래스:   rgba(255,255,255,0.08) 반투명 오버레이
```

**핵심 시각 시그니처:**
- **거대한 serif 히어로** (129px) — 에디토리얼·프리미엄. maho도 Instrument Serif로 같은 방향 가능.
- **순검정 + 흰 텍스트 + 무채색** — 브랜드 컬러 액센트 거의 없음. maho 웹사이트(모노크롬)와 동일 철학.
- **극도의 미니멀 버튼** (투명, radius 0) — 장식 배제.
- 섹션 구조: 히어로 → The Foundation / The Brain / The Claws / The Extension / One more thing (Apple 키노트식 "one more thing" 오마주).

---

## 3. 카피 톤 (독특 — 벤치마크 가치 높음)

**영국식 위트 + 격식체:**
- "A remarkably well-behaved browser", "Quietly intelligent, beautifully native, and ready before you are."
- "terribly poor form", "dreadfully passé", "behind a velvet rope, air-locked and local-first"
- "An operating system for agents, disguised as a very civilized browser."
- "should the future prove entirely too exhausting, there is a switch to disable all AI features. No offense will be taken."

→ **차별점:** 대부분 브라우저(Arc="calm", Aside=벤치마크 자랑)와 완전히 다른 **문학적·유머러스·자신감 있는** 목소리. 제품을 "잘 교육받은 집사"처럼 의인화.

→ **maho 시사점:** maho의 voice(§BRAND.md "담백·정직·반과장")와는 다른 방향. Phi는 위트로 승부, maho는 정직으로 승부. 베끼면 안 되고(톤 충돌), **"강한 개성 있는 목소리가 먹힌다"는 점만 참고.** maho는 solo-dev의 진솔함을 개성으로.

---

## 4. maho 랜딩에 가져올 것 / 피할 것

**가져올 것:**
- **거대 serif 히어로** — Instrument Serif로 에디토리얼 임팩트 (maho DESIGN.md와 정합).
- **순검정 모노크롬 + 무채색 절제** — 이미 maho 웹사이트 방향과 일치, 확신 강화.
- **Apple 키노트식 섹션 서사** (Foundation→Brain→...→one more thing) — 기능을 나열이 아니라 "장(章)"으로.
- **강한 브랜드 보이스** — 밋밋한 SaaS 카피 대신 개성. (단 maho는 위트 아닌 정직 톤)

**피할 것:**
- **오픈소스 주장** — Phi는 하지만 maho는 proprietary (AGENTS.md 금지). 따라하면 안 됨.
- **강한 프로액티브 AI 카피** — Phi의 "predicts intentions, unprompted"는 maho의 hand-on-wheel과 충돌. maho는 절제 유지.
- **영국식 위트 톤 복제** — Phi 소유. maho가 따라하면 미투. 정직·solo-dev 개성으로 차별.
- **129px 헤드라인 그대로** — 과할 수 있음. maho clamp(3rem→6rem)이 이미 큼, 그 선에서.

---

## 5. 요약

Phi는 maho와 **포지션이 위험할 만큼 가깝다**(macOS 네이티브 Chromium + AI + local-first + agent친화). 시각(순검정+serif 히어로+모노크롬)은 maho가 이미 가려던 방향과 일치해 **확신을 준다**. 차별화는 (a) AI 강도(maho=절제 vs Phi=프로액티브), (b) 오픈소스(maho=proprietary), (c) 목소리(maho=정직 vs Phi=위트)에서 나온다. 랜딩은 serif 히어로 + 키노트식 서사 구조를 참고하되, 카피 톤과 OSS 주장은 명확히 갈라야 한다.
