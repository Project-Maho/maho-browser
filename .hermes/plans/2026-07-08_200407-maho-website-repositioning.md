# maho website 프론트엔드 재정비 계획

> **For Hermes:** 실행 단계는 subagent-driven-development 대신 사용자가 직접 리뷰하며 진행. 각 Phase 완료 시 사용자 승인 후 다음 단계.

**Goal:** website/ (Astro 6 + Starlight + React) 를 Better Arc 포지션 + mahobrowser.com 도메인 + zero-day release 전제에 맞게 재정비하고, 런칭 D-Day에 발사 가능한 상태로 만든다.

**Architecture:** 기존 Astro 프로젝트 유지. 컨텐츠(카피/블로그/compare) 재작성이 대부분이고 코드 구조 개조는 최소. 도메인 스트링·환경변수 3곳(astro.config.mjs, .env, JSON-LD)에 집중.

**Tech Stack:** Astro 6, Starlight, Starlight Blog, React 19, Tailwind, Cloudflare Pages Direct Upload.

**작업 루트:** `/Users/indo/code/project/maho-workspace/website/`

---

## 사용자 확정 제약 (모든 작업에 적용)

- **도메인**: `mahobrowser.com` (이전 `maho.app`은 전량 교체)
- **포지션**: Better Arc. **agent-native 명시적 부정**. AI 채팅 사이드바 없음.
- **소스**: 클로즈드. "open source" / "auditable" / "View on GitHub for source" 문구 금지. GitHub 링크는 릴리즈-온리 리포지토리만 허용.
- **런칭 방식**: Zero-day release. Waitlist 없음. 다운로드 링크 D-Day 아침 활성화.
- **상표**: 상업 카피(태그라인/헤드라인/CTA)에 Arc/Zen/Dia/Comet/Atlas 등 타사 상표 언급 금지. 블로그 본문(편집·논평)에서만 nominative fair use.
- **저격 금지**: 비교 콘텐츠 OK, 폄하 톤 금지.
- **개인 SNS 없음**: 창업자 개인 계정 링크 없음. 제품 명의 X 계정(@mahobrowser or 대체)만.
- **커뮤니티**: Discord만.

---

## Current Context

### 기존 구조
```
website/src/
├── pages/
│   ├── index.astro           # 랜딩 (React LandingPage 컴포넌트 마운트)
│   ├── about.astro
│   ├── why.astro             # ← agent-native 시절 서사 가능성 높음
│   ├── download.astro
│   ├── waitlist.astro        # ← zero-day 방침과 충돌
│   ├── pricing.astro
│   ├── roadmap.astro
│   ├── security.astro
│   ├── privacy.astro
│   ├── terms.astro
│   ├── mail.astro            # 별도 제품(Maho Mail)
│   └── compare/
│       ├── index.astro
│       ├── aside.astro       # ← 저격 톤 검토 대상
│       └── maho-mail-vs-*.astro (7개, Mail 비교라 이번 스코프 밖)
├── content/docs/
│   ├── browser/, mail/, cli/, api/  # 문서
│   └── blog/                 # 블로그 (agent-native 흔적 다수)
├── components/               # Nav, Footer, LandingPage 등
└── lib/site.ts               # getSiteUrl(Astro) — 도메인 헬퍼
```

### 블로그 카테고리별 현황 (블로그 스코프 판단용, 실측 필요)
- Agent-native 시절: `agentic-browser-*`, `agentic-vs-chat`, `byok-*`, `mcp-tools-in-the-browser`, `local-models-cost-vs-cloud`, `building-an-agentic-workflow`, `boosts-explained` 등
- Better Arc 재활용 가능: `maho-vs-zen-browser`, `maho-vs-dia`, `maho-vs-safari` (톤 재검토 필요)
- 프라이버시/보안: `zero-telemetry-by-default`, `browser-history-encryption-explained`, `browser-fingerprinting-defense`, `sync-architecture-end-to-end` (유지 가능)
- UX: `side-panel-as-control-surface` (재검토)

### 도메인 노출 지점 (grep 필요)
- `astro.config.mjs` — SITE_URL 기본값 `https://maho.app`
- `.env.example` — SITE_URL, PUBLIC_RELAY_URL
- `src/lib/site.ts` — JSDoc 예시
- `src/components/JSONLD.astro` — JSDoc 예시
- 각 페이지 상단 SEO meta / canonical / og:url
- 블로그 mdx 내부 링크
- `og.config.mjs` — OG 이미지 생성 시 도메인
- Cloudflare Pages 프로젝트 이름 (`maho-website` — 변경 안 함, 배포 이름일 뿐)

---

## Proposed Approach

**5개 Phase**, 각 Phase 완료 시 사용자 승인.
- Phase 1: 도메인 마이그레이션 (기술적, 파괴적 아님)
- Phase 2: 랜딩 페이지 재정비 (마케팅 핵심)
- Phase 3: 서브 페이지 재정비 (about/why/download/waitlist)
- Phase 4: 블로그 정리 및 W1 신규 작성
- Phase 5: compare/ 재검토 및 SEO/OG 마감

---

## Phase 1: 도메인 마이그레이션 `maho.app` → `mahobrowser.com`

### Task 1.1: 도메인 사용 실태 전수 조사

**Objective:** 교체 대상 파일과 라인 목록 확보.

**Files:** 없음(조사만)

**Step 1:** `search_files` 로 전수 검색
```
search_files(
  pattern="maho\\.app",
  path="/Users/indo/code/project/maho-workspace/website/",
  output_mode="content",
  context=1
)
```

**Step 2:** 결과를 Phase 1 실행 노트에 저장

**Verification:** 검색 결과 파일 목록에 최소한 아래 파일 포함되어야 함:
- `astro.config.mjs`
- `.env.example`
- `src/lib/site.ts`
- `src/components/JSONLD.astro`
- 각 페이지 SEO 블록

**Commit:** 없음(조사)

---

### Task 1.2: `astro.config.mjs` 기본 SITE_URL 교체

**Objective:** Astro 빌드 시 canonical / sitemap / OG url 기본값을 mahobrowser.com으로.

**Files:**
- Modify: `website/astro.config.mjs`

**Step 1:** `SITE_URL` 기본값 라인 찾기
- 현재: `process.env.SITE_URL || 'https://maho.app'`
- 변경: `process.env.SITE_URL || 'https://mahobrowser.com'`

**Step 2:** dev 서버 재기동
```bash
cd website && bun run dev
```

**Verification:** 브라우저에서 `http://localhost:5555` 접속, DevTools > Network에서 canonical link 확인.

**Commit:**
```bash
git add website/astro.config.mjs
git commit -m "chore(website): switch canonical domain to mahobrowser.com"
```

---

### Task 1.3: `.env.example` 및 프로덕션 배포 스크립트 교체

**Objective:** 로컬 개발자와 배포 스크립트 양쪽 모두 새 도메인 사용.

**Files:**
- Modify: `website/.env.example`
- Modify: `website/AGENTS.md` (DEPLOY 섹션의 환경변수 예시)

**Step 1:** `.env.example` 편집
```
SITE_URL=https://mahobrowser.com
PUBLIC_RELAY_URL=https://relay.mahobrowser.com
```

**Step 2:** AGENTS.md DEPLOY 섹션의 env 예시 3곳 교체:
- `SITE_URL=https://staging.maho.app` → `SITE_URL=https://staging.mahobrowser.com`
- 프로덕션 vars 표
- Verifying 섹션의 grep 예시

**Verification:** `git diff website/AGENTS.md` 로 확인

**Commit:**
```bash
git add website/.env.example website/AGENTS.md
git commit -m "chore(website): document mahobrowser.com in env + AGENTS"
```

---

### Task 1.4: `src/lib/site.ts` JSDoc + 헬퍼 fallback 교체

**Objective:** getSiteUrl 헬퍼의 안전 fallback을 새 도메인으로.

**Files:**
- Modify: `website/src/lib/site.ts`

**Step 1:** JSDoc 예시 `https://maho.app` → `https://mahobrowser.com`

**Step 2:** 코드 내 fallback 문자열 검색:
```
search_files(pattern="maho\\.app", path="website/src/lib/site.ts")
```
있으면 전부 교체.

**Verification:**
```bash
cd website && bun run typecheck
grep -n "maho\\.app" src/lib/site.ts   # 결과 0줄이어야 함
```

**Commit:**
```bash
git add website/src/lib/site.ts
git commit -m "chore(website): update site.ts fallback to mahobrowser.com"
```

---

### Task 1.5: JSONLD.astro + 페이지 상단 SEO 메타 교체

**Objective:** 구조화 데이터와 페이지별 canonical / og:url을 새 도메인으로.

**Files:**
- Modify: `website/src/components/JSONLD.astro`
- Modify: `website/src/pages/*.astro` (SEO 블록 있는 것들)

**Step 1:** JSONLD.astro JSDoc + hardcoded fallback 검색·교체

**Step 2:** 각 pages/*.astro 상단 프론트매터의 canonical / og:url / twitter:url 검사. 하드코딩되어 있으면 `getSiteUrl(Astro)` 헬퍼 사용으로 리팩토링.

**Verification:**
```bash
grep -rn "maho\\.app" website/src/    # 결과 0줄
grep -rn "mahobrowser\\.com" website/src/  # 최소 5줄 이상
```

**Commit:**
```bash
git add website/src/
git commit -m "chore(website): migrate SEO metadata to mahobrowser.com"
```

---

### Task 1.6: 정적 asset과 public/ 도메인 참조 정리

**Files:**
- Modify: `website/public/robots.txt` (있으면), `website/public/sitemap.xml` (자동생성이면 skip)
- Modify: `website/src/pages/robots.txt.ts`

**Step 1:** robots.txt Sitemap URL 확인
```bash
grep -n "maho\\.app" website/src/pages/robots.txt.ts website/public/
```

**Step 2:** 남은 참조 전량 교체

**Verification:**
```bash
grep -rn "maho\\.app" website/
# 결과: node_modules, dist, bun.lock 제외 0줄이어야 함
```

**Commit:**
```bash
git add website/src/pages/robots.txt.ts website/public/
git commit -m "chore(website): finish domain migration"
```

---

### Task 1.7: 스테이징 빌드로 도메인 검증

**Files:** 없음(빌드 검증)

**Step 1:**
```bash
cd website
SITE_URL=https://mahobrowser.com PUBLIC_RELAY_URL=https://relay.mahobrowser.com bun run build
```

**Step 2:** 빌드 결과 검사
```bash
grep -rl "maho\\.app" dist/    # 0개 파일이어야 함
grep -c "mahobrowser\\.com" dist/index.html   # 최소 3개 이상
```

**Verification:** 두 grep 모두 기대치 충족.

**Commit:** 없음(dist는 gitignored)

---

## Phase 2: 랜딩 페이지 (`index.astro` + `LandingPage`) 재정비

### Task 2.1: 기존 랜딩 페이지 카피 실측 및 스크린샷

**Objective:** 재작성 전에 현재 카피 아카이브.

**Files:** 없음(조사)

**Step 1:** `read_file` 로 `src/pages/index.astro` + `src/components/LandingPage*` 전량 읽기

**Step 2:** 헤드라인 / 서브 / 각 섹션의 문구를 목록화하고, 아래 3분류로 태깅:
- ⚫ agent-native 흔적 → 삭제
- 🟡 재활용 가능 → 톤만 조정
- 🟢 유지

**Verification:** 태그된 목록이 이 계획서에 append됨.

**Commit:** 없음

---

### Task 2.2: 랜딩 카피 세트 확정 (Better Arc 톤, 상표 X)

**Objective:** 최종 카피 세트 결정. 사용자 승인 필요.

**후보 카피 (이전 대화에서 도출)**

- 헤드라인 (택 1):
  - "A calmer browser for people who miss theirs."
  - "The browser you used to love, still around."
  - "Sidebar, workspaces, split view. On maintained Chromium."
  - "Built for the way you already work."

- 서브카피:
  - "Sidebar, workspaces, split view, and a command bar — on Chromium that stays current. One-click import from your previous browser. Closed source, weekly updates, no waitlist."

- CTA:
  - Primary: `Download for macOS` (즉시 다운로드, 링크 없으면 D-Day까지 disabled)
  - Secondary: `Join our Discord`

- 섹션 구조:
  1. Hero (헤드라인 + 서브 + CTA + 스크린샷)
  2. What's inside (기능 4개: Sidebar / Workspaces / Split View / Command Bar)
  3. Import in one click (마이그레이션 강조)
  4. Weekly changelog (신뢰 축적)
  5. Discord CTA

**Files:** 없음 (승인 대기)

**Verification:** 사용자가 헤드라인 1개 선택.

**Commit:** 없음

---

### Task 2.3: LandingPage React 컴포넌트에 새 카피 반영

**Files:**
- Modify: `website/src/components/LandingPage/*` (실제 경로는 Task 2.1에서 확인)
- Modify: `website/src/pages/index.astro`

**Step 1:** Task 2.2 확정 카피로 문자열 교체. Framer Motion 등 애니메이션은 유지.

**Step 2:** Hero의 스크린샷/영상 asset 확인. 필요 시 placeholder 유지, D-Day 전 교체 태스크로 이월.

**Step 3:** 다운로드 CTA는 `/download` 로 라우팅 (Task 3.3에서 처리).

**Step 4:**
```bash
cd website && bun run dev
```
브라우저에서 시각 확인.

**Verification:**
- Hero 헤드라인 = 확정한 문구
- 서브카피 = "Sidebar, workspaces, split view..."
- 상표(Arc/Zen/Dia 등) 등장 0회 (`grep -in "arc\\|zen\\|dia " src/components/LandingPage/*` 결과 없음)
- agent-native / AI-native / agent-first 등장 0회

**Commit:**
```bash
git add website/src/components/LandingPage/ website/src/pages/index.astro
git commit -m "feat(website): reposition landing to Better Arc"
```

---

### Task 2.4: 랜딩 페이지 접근성 & 반응형 회귀 확인

**Files:** 없음(검증)

**Step 1:** DevTools > Lighthouse (Accessibility) 실행. 90 이상 유지.
**Step 2:** DevTools > Device Toolbar 로 iPhone SE / iPad / 데스크톱 3해상도 확인.
**Step 3:** `bun run build` 통과 확인.

**Verification:** Lighthouse a11y ≥ 90, 3해상도 레이아웃 깨짐 없음.

**Commit:** 없음

---

## Phase 3: 서브 페이지 (about / why / download / waitlist) 재정비

### Task 3.1: `about.astro` 재작성

**Objective:** "The Browser Company" 스타일 팀 소개. 개인 SNS 링크 X.

**Files:**
- Modify: `website/src/pages/about.astro`

**Step 1:** 현재 카피 읽고 agent-native 흔적 파악.

**Step 2:** 아래 톤으로 재작성:
- 팀 소개 (익명 or "The maho team" 명의)
- 왜 만들었는가 (Miller 편지 인용은 허용, 저격 X)
- 소스 정책 명시("closed source, weekly public changelog")
- 연락처: `hello@mahobrowser.com`, Discord 링크

**Step 3:** 개인 SNS / GitHub 프로필 링크 전량 제거.

**Verification:**
- 상표 (Arc/Zen/Dia) 등장 0회 (블로그 인용 링크만 허용, 이 페이지에는 X)
- "open source" 문구 0회
- 개인 X/GitHub 링크 0개

**Commit:**
```bash
git add website/src/pages/about.astro
git commit -m "feat(website): rewrite about page for Better Arc positioning"
```

---

### Task 3.2: `why.astro` — agent-native 서사 제거 or 페이지 삭제

**Objective:** 이 페이지가 agent-native 시절 매니페스토라면 삭제 또는 재작성 결정.

**Files:**
- Read/Modify or Delete: `website/src/pages/why.astro`

**Step 1:** `read_file` 로 현재 내용 확인.

**Step 2:** 분기:
- (A) agent-native 매니페스토 → **삭제**. 대신 Task 4.2의 W1 블로그로 대체.
- (B) 일반적 "왜 새 브라우저인가" 톤 → Better Arc 톤으로 재작성.

**Step 3:** 삭제 시 nav / footer 링크도 함께 제거 (`src/components/Nav.astro`, `Footer.astro`).

**Verification:** `bun run build` 통과, dev 서버에서 dead link 없음.

**Commit:**
```bash
git add -A website/src/
git commit -m "chore(website): remove agent-native why page  # or rewrite"
```

---

### Task 3.3: `download.astro` — zero-day release 대응

**Objective:** D-Day 이전엔 "곧 출시" 상태, D-Day부터 다운로드 링크 활성.

**Files:**
- Modify: `website/src/pages/download.astro`

**Step 1:** 현재 페이지 구조 확인.

**Step 2:** 릴리즈-온리 GitHub repo URL을 환경변수로:
```astro
---
const RELEASE_REPO = import.meta.env.PUBLIC_RELEASE_REPO_URL ||
  'https://github.com/<org>/maho-releases'
const RELEASE_LIVE = import.meta.env.PUBLIC_RELEASE_LIVE === 'true'
---
```

**Step 3:** `RELEASE_LIVE=false` 상태(D-Day 이전):
- 헤드라인: "maho launches [DATE]."
- 서브: "No waitlist. Just come back on launch day."
- CTA: Discord 링크 하나만

**Step 4:** `RELEASE_LIVE=true` 상태(D-Day 이후):
- 각 플랫폼 다운로드 버튼(macOS .dmg, Windows .exe 있으면 활성 / 아니면 "coming soon")
- 링크는 `${RELEASE_REPO}/releases/latest/download/Maho-x.y.z.dmg` 패턴
- SHA256 / notarization 정보 표시

**Step 5:** `.env.example`에 두 변수 추가:
```
PUBLIC_RELEASE_LIVE=false
PUBLIC_RELEASE_REPO_URL=https://github.com/<org>/maho-releases
```

**Verification:**
- `PUBLIC_RELEASE_LIVE=false bun run build` → dist에 다운로드 링크 없음
- `PUBLIC_RELEASE_LIVE=true` → dist에 링크 노출

**Commit:**
```bash
git add website/src/pages/download.astro website/.env.example
git commit -m "feat(website): gate download page with PUBLIC_RELEASE_LIVE flag"
```

---

### Task 3.4: `waitlist.astro` — 처리 결정

**Objective:** zero-day 방침이라 waitlist 폼은 논리적으로 불필요. 그러나 이미 페이지 존재 + Relay 엔드포인트 구현되어 있음.

**분기 옵션**:
- **(A) 완전 삭제**: 페이지 + Nav 링크 + Relay `POST /waitlist` 엔드포인트 (Rust 쪽은 스코프 밖)
- **(B) 리퍼포즈**: "Get launch email" 로 이름·의도 바꿔서 유지. 런칭 알림 이메일 딱 1통만 발송하는 용도. 이건 Discord 부재층(이메일 선호 유저) 커버.
- **(C) 소프트 삭제**: 라우트만 유지하고 `/download`로 redirect

**권장:** (B). 이유:
- Discord 진입 부담 있는 유저의 이메일 캡처는 가치 있음
- 이후 환영 이메일(가입 시) + D+1 피드백(런칭 D-Day에 발송) 시퀀스와 자연스럽게 연결
- 카피만 변경, 백엔드 로직 재사용

**Files:**
- Modify: `website/src/pages/waitlist.astro`

**Step 1:** 페이지명은 유지하되(SEO, 링크 안깨짐), 헤드라인 재작성:
- "Get notified on launch day."
- 서브: "One email when maho is ready. No newsletter series until you download."
- 폼 필드 유지 (이메일 하나)
- 성공 문구: "You're on the list. See you on launch day."

**Step 2:** 이 데이터의 후속 처리는 Relay/이메일 스택 세팅 시 연결 (별도 태스크).

**Verification:** 폼 submit → PUBLIC_RELAY_URL로 POST 정상.

**Commit:**
```bash
git add website/src/pages/waitlist.astro
git commit -m "feat(website): rename waitlist to launch-day email signup"
```

---

### Task 3.5: `pricing.astro` — 가격 정책 결정 및 반영

**Objective:** Better Arc 포지션 + 클로즈드 소스에서 수익 모델 확정.

**⚠️ Blocked**: 사용자가 가격 모델 미확정. 아래 후보 중 선택 필요:
- (A) 완전 무료 (Arc 문법)
- (B) 무료 + Pro 티어 (Sync/Cloud 기능)
- (C) 유료 (Vivaldi Cloud, Kagi 문법)

**대기 액션:** 사용자 결정 → 카피 반영 태스크로 분리.

임시 조치:
- Modify: `pricing.astro` 를 "Free during beta. Details on launch." 로 최소화.

**Commit:** 결정 후

---

### Task 3.6: `roadmap.astro` — 정직 톤 유지, agent-native 흔적 제거

**Files:**
- Modify: `website/src/pages/roadmap.astro`

**Step 1:** 현재 로드맵 항목 읽기.

**Step 2:** 삭제 대상 항목:
- Agent / MCP / AI 관련 로드맵 (agent-native 포지션 흔적)

**Step 3:** 유지/추가 대상:
- Windows/Linux 지원 (no ETA)
- Extensions 호환성 확장
- Sync 기능
- 특정 Arc 기능 계승 (Boosts 대체, 등)

**Step 4:** ETA는 "no ETA yet" 정직 톤 유지.

**Commit:**
```bash
git add website/src/pages/roadmap.astro
git commit -m "chore(website): clean roadmap to Better Arc scope"
```

---

## Phase 4: 블로그 정리 + W1 신규 작성

### Task 4.1: 블로그 포스트 분류 및 처리 계획

**Objective:** `src/content/docs/blog/` 전 포스트를 3분류: 유지 / 아카이브 / 삭제.

**Files:** 없음(조사)

**Step 1:**
```
search_files(target="files", pattern="*.mdx", path="website/src/content/docs/blog/")
```

**Step 2:** 각 포스트 프론트매터의 title/description만 훑어 분류표 작성.

**분류 기준 초안 (실측 후 조정):**
- **삭제**: 제품 정체성이 agent-native일 것을 전제한 포스트 (agentic-browser-*, agentic-vs-chat, byok-*, mcp-tools-in-the-browser, building-an-agentic-workflow 등). 살려두면 신규 방문자에게 혼란.
- **유지 (톤 재검토)**: 프라이버시·기술 심층 (zero-telemetry, encryption, fingerprinting, sync-architecture)
- **유지 (수정)**: maho-vs-* 시리즈. Better Arc 톤으로 재작성 + 저격 톤 검토.
- **아카이브 (URL 남김, noindex)**: 애매한 경우, `draft: true` 로 스타라이트에서 hide.

**Verification:** 각 포스트 결정이 이 계획서에 기록됨.

**Commit:** 없음

---

### Task 4.2: 신규 블로그 W1 — "Reading Arc's goodbye letter"

**Files:**
- Create: `website/src/content/docs/blog/reading-arcs-goodbye-letter.mdx`

**Step 1:** 프론트매터
```yaml
---
title: "Reading Arc's goodbye letter"
description: "Josh Miller's letter to Arc members named a rule we've been building around: the novelty budget."
date: 2026-XX-XX
authors:
  - name: The maho team
tags: [background, philosophy]
---
```

**Step 2:** 본문은 이전 대화에서 도출한 초안 사용(전문):
- The novelty tax (Miller 인용)
- What we kept (Sidebar, Workspaces, Split View, Command Bar, Pinned tabs)
- What we didn't (Easels, Boosts, first-class Notes)
- What we're doing that Arc didn't (maintained Chromium, closed source + open changelog)
- What we're not (AI browser 아니라는 명시)
- CTA: `[Download maho](https://mahobrowser.com)`

**Step 3:** 원문 링크 명시:
- Josh Miller "Letter to Arc members" (browsercompany.substack.com)
- 링크는 편집·논평 맥락이라 fair use 강함

**Verification:**
- 상표 사용은 편집 맥락에서만 (nominative fair use 3요건 충족)
- 저격 톤 0
- "The maho team" 명의
- Astro 빌드 성공

**Commit:**
```bash
git add website/src/content/docs/blog/reading-arcs-goodbye-letter.mdx
git commit -m "blog: reading-arcs-goodbye-letter"
```

---

### Task 4.3: 신규 블로그 W2 — "What we kept from Arc, and why"

**Files:**
- Create: `website/src/content/docs/blog/what-we-kept-from-arc.mdx`

**Step 1:** 톤: 각 UX 요소 하나씩 뜯어보기. Miller 편지 인용 재활용.
- Sidebar → 왜 유지
- Workspaces → 왜 유지 ("Spaces" 상표 회피 위해 workspaces로 표기)
- Split View → 왜 유지
- Command Bar → 왜 유지
- 각 섹션에 스크린샷/GIF placeholder

**Step 2:** 첫 문단에 disclaimer:
> maho is not affiliated with, endorsed by, or sponsored by The Browser Company or Atlassian.

**Verification:** 편집 논평 톤, 상표는 fair use 범위.

**Commit:**
```bash
git add website/src/content/docs/blog/what-we-kept-from-arc.mdx
git commit -m "blog: what-we-kept-from-arc"
```

---

### Task 4.4: 신규 블로그 W3 — "Why we built on Chromium"

**Files:**
- Create: `website/src/content/docs/blog/why-chromium.mdx`

**Step 1:** 톤: Chromium vs Gecko(WebKit) 기술 비교. Zen 저격 아님.
- Extension 호환성 (Chrome Web Store)
- 개발자 도구 성숙도
- 유지보수 부담 (upstream 팔로우 비용)
- 트레이드오프 명시

**Step 2:** 결론:
- maho는 Chromium fork로 Extensions 생태계 + 개발자 도구 완전 계승
- 대신 소스 공개 안 하는 대가로 팀 규모 유지

**Verification:** 저격 톤 0, 기술 각도.

**Commit:**
```bash
git add website/src/content/docs/blog/why-chromium.mdx
git commit -m "blog: why-chromium"
```

---

### Task 4.5: 삭제 대상 블로그 처리

**Files:**
- Delete or `draft: true`: Task 4.1 삭제 리스트 각 포스트

**결정 필요:** 완전 삭제 vs `draft: true` (URL 살아있지만 404).

**권장:** `draft: true`. 이유:
- 백링크 있을 수 있음
- 향후 재활용 여지
- Astro/Starlight는 draft를 자동 404

**Step 1:** 각 대상 mdx의 프론트매터에 `draft: true` 추가.

**Step 2:** sitemap.xml에서 draft 포스트 제외되는지 확인 (Starlight 기본 동작 확인 필요).

**Verification:** `bun run build` 후 dist에 해당 URL 없음.

**Commit:**
```bash
git add website/src/content/docs/blog/
git commit -m "chore(blog): mark agent-native posts as draft"
```

---

### Task 4.6: `maho-vs-*` 시리즈 톤 재검토

**Files:**
- Modify: `website/src/content/docs/blog/maho-vs-zen-browser.mdx`
- Modify: `website/src/content/docs/blog/maho-vs-dia.mdx`
- Modify: `website/src/content/docs/blog/maho-vs-safari.mdx`

**Step 1:** 각 포스트를 읽고 저격 톤 스캐닝. 아래 표현이 있으면 재작성:
- "X is slow"
- "X lacks Y"
- "Unlike X"
- 부정문으로 시작하는 헤드라인/섹션

**Step 2:** 재작성 원칙:
- 각 브라우저의 "잘한 것" 문단으로 먼저 인정
- 팩트만 나열 (기능·설계·타깃)
- 마지막 문단에서 maho 각도 짧게

**Step 3:** 각 포스트 disclaimer 추가.

**Verification:** 저격 표현 카운트 0.

**Commit:**
```bash
git add website/src/content/docs/blog/maho-vs-*.mdx
git commit -m "blog: soften comparison posts, add fair-use disclaimers"
```

---

## Phase 5: compare/ 재검토 + SEO/OG 마감

### Task 5.1: `compare/aside.astro` 톤 재검토

**Files:**
- Modify or Delete: `website/src/pages/compare/aside.astro`

**Step 1:** `read_file` 로 현재 내용 확인.

**Step 2:** 저격 톤 여부 판단.
- 있으면 → Task 4.6과 동일 원칙으로 재작성
- 없으면 → 유지, disclaimer만 추가

**Step 3:** 만약 페이지 자체가 agent-native 시절 "우리가 Aside보다 낫다" 프레임이면 삭제. Better Arc 포지션에서 Aside는 사실 다른 세그먼트라 굳이 비교할 이유 없음.

**Verification:** 저격 표현 0, disclaimer 존재.

**Commit:**
```bash
git add website/src/pages/compare/aside.astro
git commit -m "chore(compare): rewrite or remove aside comparison"
```

---

### Task 5.2: `compare/index.astro` 재정비

**Files:**
- Modify: `website/src/pages/compare/index.astro`

**Step 1:** compare 페이지 목록에서 삭제된 항목 있으면 링크 제거.

**Step 2:** compare/ 자체가 저격 허브가 되지 않도록 헤더 톤 조정:
- "How maho compares" (팩트 톤) or "Alternatives" (중립)

**Commit:**
```bash
git add website/src/pages/compare/index.astro
git commit -m "chore(compare): neutral tone for index"
```

---

### Task 5.3: OG 이미지 재생성 (`og.config.mjs`)

**Files:**
- Modify: `website/og.config.mjs`
- Regenerate: `website/public/og-images/` (또는 자동생성 경로)

**Step 1:** og.config.mjs의 도메인 참조 확인·교체.

**Step 2:** 헤드라인 문자열 확정된 것으로 반영 (Task 2.2 결과).

**Step 3:** OG 생성 스크립트 재실행:
```bash
cd website && bun run og:generate   # 실제 스크립트 이름은 scripts/ 확인
```

**Verification:**
- 생성된 OG 이미지에 새 도메인 워터마크
- Twitter Card Validator / Facebook Debugger 로 프리뷰 확인

**Commit:**
```bash
git add website/og.config.mjs website/public/og-images/
git commit -m "chore(website): regenerate OG images for new domain and copy"
```

---

### Task 5.4: 최종 링크·404·리다이렉트 감사

**Files:** 없음(검증)

**Step 1:** 빌드된 사이트에서 dead link 스캔:
```bash
cd website && bun run build
bunx linkinator dist/ --recurse --skip "external|localhost"
```

**Step 2:** 삭제된 페이지의 하드코딩 링크 남아있는지 확인 (nav, footer, sitemap).

**Verification:** 4xx 결과 0개.

**Commit:** 없음

---

### Task 5.5: 프로덕션 배포 리허설

**Files:** 없음

**Step 1:** 스테이징 브랜치로 Cloudflare Pages 배포:
```bash
cd website
SITE_URL=https://staging.mahobrowser.com \
PUBLIC_RELAY_URL=https://relay.mahobrowser.com \
PUBLIC_RELEASE_LIVE=false \
bun run build

bunx wrangler pages deploy dist \
  --project-name=maho-website \
  --branch=staging \
  --commit-dirty=true
```

**Step 2:** 스테이징 URL에서 최종 시각 확인:
- 랜딩 카피
- Discord 링크 동작
- Waitlist 폼 submit
- Blog W1 렌더링
- OG 이미지 소셜 프리뷰

**Verification:** 시각적 스냅샷 이상 없음.

**Commit:** 없음

---

## Files Likely to Change (전체 요약)

**설정**
- `website/astro.config.mjs`
- `website/.env.example`
- `website/AGENTS.md` (env 예시)
- `website/og.config.mjs`

**공통 라이브러리**
- `website/src/lib/site.ts`
- `website/src/components/JSONLD.astro`
- `website/src/components/Nav.astro` (링크 정리)
- `website/src/components/Footer.astro` (링크 정리)

**페이지**
- `website/src/pages/index.astro`
- `website/src/pages/about.astro`
- `website/src/pages/why.astro` (삭제 또는 재작성)
- `website/src/pages/download.astro`
- `website/src/pages/waitlist.astro` (리퍼포즈)
- `website/src/pages/pricing.astro` (Blocked)
- `website/src/pages/roadmap.astro`
- `website/src/pages/robots.txt.ts`
- `website/src/pages/compare/index.astro`
- `website/src/pages/compare/aside.astro`

**컴포넌트**
- `website/src/components/LandingPage/**`

**블로그 (신규 3편)**
- `website/src/content/docs/blog/reading-arcs-goodbye-letter.mdx`
- `website/src/content/docs/blog/what-we-kept-from-arc.mdx`
- `website/src/content/docs/blog/why-chromium.mdx`

**블로그 (draft 처리)**
- `website/src/content/docs/blog/agentic-browser-*.mdx` (해당 파일들)
- `website/src/content/docs/blog/byok-*.mdx`
- `website/src/content/docs/blog/mcp-tools-in-the-browser.mdx`
- 기타 agent-native 시절 포스트

**블로그 (톤 조정)**
- `website/src/content/docs/blog/maho-vs-zen-browser.mdx`
- `website/src/content/docs/blog/maho-vs-dia.mdx`
- `website/src/content/docs/blog/maho-vs-safari.mdx`

---

## Tests / Validation

각 Phase 마다:
```bash
cd website
bun run typecheck         # TypeScript
bun run build             # Astro 빌드
grep -rn "maho\\.app" src/ dist/ | grep -v node_modules  # 도메인 잔재 0
grep -rin "open source\\|View source on GitHub" src/     # 클로즈드 정책 위반 0
grep -rin "agent-native\\|agentic browser\\|AI-native" src/pages src/components  # 포지션 위반 0
```

프로덕션 배포 전:
- Lighthouse (Performance / Accessibility / SEO) 각 90 이상
- 스테이징 URL에서 시각 QA
- OG 이미지 소셜 프리뷰 (Twitter Card Validator)

---

## Risks / Tradeoffs / Open Questions

### Risks
1. **도메인 마이그레이션 중 SEO 손실**: `maho.app` 로 기존 유입이 있으면 301 리다이렉트 필요. Cloudflare Pages에서 도메인 병행 운영 or `maho.app` → `mahobrowser.com` 301 rule 설정.
2. **블로그 URL 변경 없음**: 슬러그는 유지하고 프론트매터만 수정하는 방향이라 백링크 손실 위험 낮음.
3. **가격 미확정 (Task 3.5)**: 런칭 카피에 가격 언급 없으면 리뷰어가 "얼마?" 를 반복 질문. D-Day 전에 결정 필요.
4. **릴리즈 리포지토리 이름 미확정 (Task 3.3)**: `github.com/<org>/maho-releases` 의 `<org>` 확정 필요.
5. **X 계정 핸들 미확정**: `@mahobrowser` / `@maho_browser` / `@mahoapp` 중 선점 가능한 것 확인 필요. 프론트엔드에서는 소셜 링크 하나 넣을 자리라 결정 후 반영.

### Tradeoffs
- 블로그 삭제 대신 `draft: true` 유지 → 파일 정리 안 되지만 백링크 보호
- Waitlist 페이지 유지 (리퍼포즈) → zero-day 원칙과 소프트 충돌하지만 이메일 캡처 가치 있음

### Open Questions (사용자 결정 필요)
1. Task 2.2 헤드라인 4개 후보 중 1개 선택
2. Task 3.2 `why.astro` 삭제 vs 재작성
3. Task 3.4 waitlist 처리 방식 (A/B/C 중)
4. Task 3.5 가격 정책 결정
5. Task 4.1 블로그 개별 처리 (실측 후 결정)
6. Task 5.1 `compare/aside.astro` 삭제 vs 재작성
7. 릴리즈 배포 GitHub org 이름
8. 도메인 마이그레이션 시점 (Cloudflare Pages 프로덕션 컷오버 언제)

---

## 실행 순서 권장

- **오늘**: Phase 1 전부 (도메인 마이그레이션은 리스크 낮고 나머지 작업의 전제)
- **내일**: Phase 2 (사용자 카피 선택 후 진행)
- **D-14**: Phase 3.1 ~ 3.3 (about, why, download)
- **D-10**: Phase 4 (블로그 3편 신규 + 정리)
- **D-7**: Phase 5 (compare + OG + 스테이징 배포)
- **D-3**: 프로덕션 컷오버 (Cloudflare Pages)
- **D-Day**: `PUBLIC_RELEASE_LIVE=true` 로 재배포

---

## Next Immediate Action

Phase 1 착수 준비:
1. 사용자 승인 대기
2. Task 2.2 헤드라인 후보 4개 중 선택 (Phase 2 준비)
3. `git status` 로 현재 브랜치 clean 상태 확인 후 시작
