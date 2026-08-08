# Maho AI 에이전트 패널 — 레퍼런스 파리티 구현 계획

**날짜**: 2026-07-26
**범위**: `maho-chromium/browser/resources/maho_ai/` (+ `maho_common` 아이콘 시밈 additive 1건)
**목표**: 에이전트 패널을 `maho/reference_screenshots/ai-chat-reference.png` · `preview-ai-panel-reference.png`
(AGENTS.md가 지정한 디자인 타깃)에 맞추고, 시각적 낭비를 제거한다.

## 확정된 결정

| 항목 | 결정 |
|---|---|
| 목표 수준 | 레퍼런스 파리티 — assistant 버블·역할 라벨·타임스탬프 제거, 상단바 플랫화, 빈 화면 → 제안 칩 |
| glass 범위 | `maho_ai` 호출부만. `maho_common/theme/tokens.css`의 `glass`/`glass-strong` 정의와 `maho_settings` 4개 사용처는 건드리지 않음 |
| 보이스 모달 | 나머지 톤에 맞춰 정리 (Siri 그라디언트·글로우 스마일리 제거) |
| 검증 | standalone dev server로 before/after 스크린샷 비교 |

## 제약

- **GN 편집·파일 삭제 금지** (AGENTS.md). 모든 변경은 in-place 수정.
  `maho_common/react/ui/glass-ambient.tsx`는 no-op 상태 그대로 유지 (BUILD.gn 소스로 등재됨).
  사용하지 않게 되는 `maho_ai/icons/*.svg`도 삭제하지 않음.
- `maho_common/react/icons/lucide.ts`는 export 추가만 (additive) — 기존 사용처 무영향.
- 아이콘은 Lucide로 통일 (AGENTS.md 아이콘 시스템 규약).

## 사전 확인 완료 사항

- `maho_ai` 테스트에 className 단언 없음 → 시각 변경으로 깨지는 유닛 테스트 없음
- i18n은 **의도적 미결정** 상태이며 영어 단일로 간다 (2026-07-26 확인). 모든 문자열은 TSX에
  영문으로 하드코딩되어 있고, 카피 수정에 `.grd`/`loadTimeData` 변경이 따라붙지 않는다.
  Phase 5에서도 새 문자열은 TSX에 영문으로 직접 추가한다.
- `react/app.tsx:486` 루트가 `bg-background`(불투명 `#1a1a1a`) → blur 표면 13개 중 실제로 뒤에
  콘텐츠가 지나가는 건 상단바·컴포저 2개뿐
- Developer 모드는 실제 출하되는 사용자 토글(`app.tsx:505`) → 범위 포함
- `store.setComposerPrompt()` + `store.submitPrompt()` 로 제안 칩 배선 가능
  (`store.ts:260-281`의 ask-maho ingest 경로가 동일 패턴의 선례)

---

## Phase 0 — 검증 기반

1. `opencode serve --hostname 127.0.0.1 --port 4096` **(사용자 실행 필요)**
2. `node maho-chromium/browser/resources/maho_ai/standalone/dev_server.mjs` → `http://127.0.0.1:4071`
3. before 스크린샷 4종 캡처: 빈 상태 / 대화 상태 / Developer 모드 / 보이스 모달
   (light·dark 양쪽, 사이드바 폭 ~400px와 넓은 폭)

## Phase 1 — 낭비 제거 (레이아웃 불변, 순수 이득)

`react/theme/app.css` `:root`에 그림자 토큰 2개 추가:

```css
--shadow-raised:  0 8px 24px -12px rgba(0,0,0,0.45);
--shadow-overlay: 0 20px 60px -40px rgba(0,0,0,0.65);
```

1. **헛도는 `backdrop-blur` 제거** — 뒤에 배경색밖에 없는 6곳
   - `features/developer/inspector.tsx:88`
   - `features/developer/left-rail.tsx:49`
   - `features/developer/timeline.tsx:19`
   - `features/developer/developer-workspace.tsx:33`
     (위 4곳은 `bg-card/90` + `supports-[backdrop-filter]:bg-card/85` → `bg-card`로 단순화)
   - `features/compact/composer.tsx:501` (`bg-popover/95` — 95% 불투명이라 블러 무의미)
   - `components/profile-switcher.tsx:34` (`bg-popover/95` 동일)
2. **일회성 그림자 11개 → 토큰 2개**로 치환
3. **드리프트 헤일로 제거** — `features/compact/compact-stage.tsx:16` + `app.css`의
   `halo-drift` 키프레임/유틸리티/`--color-accent-glow`.
   `glass-ambient.tsx` 주석에 기록된 기존 결정("radial blob gradients faked depth behind glass
   and were removed")의 누락분 정리 — 새 결정이 아니라 미완 작업 마무리.

→ 이 시점에서 `glass`는 상단바·컴포저 2곳만 남음.

## Phase 2 — 대화 화면 파리티

**`features/compact/conversation-message.tsx`** (핵심 변경)

- assistant: 버블 제거 → 배경 없는 좌측 정렬 평문. 마크다운 스타일은 유지
- user: 우측 pill 유지하되 `bg-primary/10` → `bg-secondary` (레퍼런스는 중립 회색)
- 역할 라벨(`MAHO`/`YOU` 대문자 + `tracking-[0.08em]`)과 hover 타임스탬프 제거
- `UserControls`(편집/삭제) 제거 → assistant 하단에 `AssistantActions`
  (ThumbsUp / ThumbsDown / Copy / RefreshCw), 기본 `text-muted-foreground`, hover 시 강조
- `React.memo` 비교 함수에서 `label` 항목 제거

**`maho_common/react/icons/lucide.ts`** — `ThumbsUp`, `ThumbsDown` export 추가 (additive)

**`features/compact/conversation-thread.tsx`**

- `ConversationMessage`의 `label` prop 제거
- `ThinkingBubble` 톤 정리: `bg-secondary/60` + `shadow-sm` → 테두리 없는 muted 텍스트,
  `uppercase tracking-[0.12em]` 라벨 제거
- assistant 평문 전환에 맞춰 항목 간격(`gap-4`) 재조정

## Phase 3 — 상단바 플랫화 + 빈 화면

**`features/compact/compact-topbar.tsx`**

- `glass` + `rounded-[1.25rem]` + 그림자 제거 → 배경 위 아이콘 행
- 오버레이 모드에서 대화가 밑으로 스크롤되므로 `bg-background` + 하단 짧은 fade mask로 처리
  (레퍼런스도 불투명 상단바 + 콘텐츠 페이드)
- `Sparkles`(탭 정리) → 의미가 드러나는 아이콘으로 교체

**`features/compact/compact-stage.tsx`**

- 히어로 `Card`("Ready when you are") 전면 제거 → 빈 여백
- 신규 `features/compact/suggestion-chips.tsx`: Analyze / Explain / Summarize 칩을
  컴포저 바로 위에 배치. `store.setComposerPrompt(text)` → `store.submitPrompt()` 배선
- `app.tsx`의 오버레이/비오버레이 두 경로 모두에서 칩이 컴포저 위에 오도록 배치 조정

## Phase 4 — 보이스 모달

**`features/compact/voice-input-modal.tsx`** + `react/theme/app.css`

- `VoiceSmiley`(글로우 눈·입, `drop-shadow`, `animate-voice-breathe`/`blink`) 제거 →
  마이크 상태 인디케이터로 대체
- `voice-bottom-glow`, `voice-headline-gradient`, `--voice-glow-*`,
  `--voice-headline-lavender` 토큰과 관련 키프레임 제거
- 웨이브폼은 유지하되 `bg-white/*` → `bg-foreground` 계열 토큰으로
- 오버레이 `backdrop-blur-2xl` → `bg-background/80`
- `prefers-reduced-motion` 블록에서 제거된 애니메이션 항목 정리

## Phase 5 — 카피

- `compact-stage.tsx`: "Ready when you are" / "Start with a prompt below and Maho AI will
  open a live session in this thread." → 제거 (빈 화면이 됨)
- 컴포저 placeholder: "Ask anything about this page…" → 레퍼런스 톤에 맞춰 조정
- 제안 칩 라벨 확정

## Phase 6 — 검증

1. `bun run typecheck` (`tsc --noEmit -p react/tsconfig.json`)
2. `bun run test` (vitest)
3. after 스크린샷 4종 재캡처 → before 및 레퍼런스와 3열 비교
4. light/dark 양쪽 대비 확인 — 특히 Phase 1에서 `bg-card/90` → `bg-card`로 바꾼 developer 패널

---

## 미결 / 진행 중 확인 필요

1. **Phase 0의 `opencode serve`** — 사용자 실행 필요. 없으면 Phase 6이 typecheck+vitest로 축소됨
2. **상단바 fade mask 처리** — 레퍼런스대로 불투명 전환 시 스크롤 겹침 마감을 실물로 보고 조정
3. **제안 칩 문구** — Analyze/Explain/Summarize 그대로 갈지, 브라우저 맥락에 맞게 바꿀지는
   Phase 5에서 스크린샷 보고 확정
