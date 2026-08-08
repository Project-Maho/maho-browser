# maho 토큰 코어 → 표면별 생성·오버라이드 전략

**작성일:** 2026-07-19
**전제:** `tokens.core.json`(단일 정의, DTCG) = 통일 계약(space/radius/semantic/motion + role 이름). 값(bg/accent/font)은 표면 오버라이드.
**근거:** `token-conflict-matrix-2026-07.md`

---

## 아키텍처

```
                docs/brand/tokens.core.json   ← 단일 원천 (스케일·어휘·네임스페이스)
                          │
          build-time 생성 스크립트 (scripts/gen-tokens.mjs)
                          │
     ┌────────────────────┼────────────────────┬───────────────────┐
     ↓                    ↓                    ↓                   ↓
  Native C++          WebUI CSS            web-ai CSS          Website
  (SkColor 헤더)      (--var, Tailwind)    (--color-*)         (--sl-*, marketing)
     +                   +                    +                   +
  표면 오버라이드      표면 오버라이드        표면 오버라이드       표면 오버라이드
  (bg #100f15,        (bg #1a1a1a,         (bg #1c1c1e,         (monochrome,
   Space 동적accent)   neutral accent)      Apple blue)          brand fonts)
```

**원칙:** 코어는 "이름·스케일·구조"만 생성. 각 표면은 생성된 코어를 import 후 `role.*` 슬롯에 자기 값을 채움. 코어 스크립트가 값을 덮어쓰지 않음.

---

## 표면별 매핑 전략

### A. Native Views (C++)
- **생성 대상:** `maho_design_tokens.h` (신규) — `constexpr int kMahoSpace1 = 4;` 류 spacing/radius 상수 + semantic SkColor.
- **오버라이드 유지:** `maho_color_mixer.cc`의 `role.background/accent` 등은 **그대로** (Space 동적 테마, `kWindowBackgroundDark` 등). 코어는 spacing/radius/semantic만 제공.
- **주의:** C++는 CSS var 못 씀 → 생성 스크립트가 `.h` 헤더로 emit. `E_CPONLY` 색 ID 체계는 유지.
- **네임스페이스:** 신규 상수만 추가, 기존 `kMahoColor*` 건드리지 않음 (색은 mixer 소관).

### B. WebUI (Tailwind v4 CSS)
- **생성 대상:** `tokens.css`의 `@theme` 블록 중 **spacing/radius 스케일 부분**을 코어에서 생성.
- **오버라이드 유지:** `--background`, `--primary`(accent), `--foreground` 등 색 값은 표면 오버라이드로 유지 (#1a1a1a 등).
- **네임스페이스 정리:** 만약 `--maho-settings-space-*` 잔재가 있으면 → 코어 `--maho-space-*`로 치환. (space-7 버그 지점)
- **shadcn 호환:** `--radius` 기준값(0.75rem)은 코어 radius.md와 정렬.

### C. web-ai (Plain CSS)
- **생성 대상:** `styles.css`의 `--space-*`, `--radius-*`, semantic(`--color-success` 등)을 코어에서 생성.
- **오버라이드 유지:** `--color-surface`(#1c1c1e), `--color-accent`(#0a84ff Apple blue), 시스템 폰트 — iOS HIG 위해 유지.
- **네임스페이스 정리:** `--maho-ai-space-7`(28px) → 코어 `--space-7`(32px)로 통일. **이게 대표 버그 수정.**
- **주의:** Tailwind 안 씀 → 순수 CSS var로 생성. 네이티브가 런타임에 `--color-*` 주입하는 경로는 유지.

### D. Website (Astro/Starlight + marketing)
- **생성 대상:** marketing.css의 spacing ladder·radius(squircle)·motion을 코어에서 생성.
- **오버라이드 유지:** 모노크롬 팔레트(`--sl-color-*`), 브랜드 폰트(Geist/Instrument Serif), OG 보라 액센트 — 마케팅 정체성으로 유지.
- **주의:** 이미 DESIGN.md(마케팅)와 겹침. 코어는 구조만, DESIGN.md는 마케팅 값.

---

## 생성 스크립트 설계 (scripts/gen-tokens.mjs)

```
입력:  docs/brand/tokens.core.json
출력:
  1. maho-chromium/browser/ui/theme/maho_design_tokens.h        (C++ 상수)
  2. maho-chromium/browser/resources/maho_common/react/theme/_core-tokens.css  (@import into tokens.css)
  3. maho/web-ai/src/_core-tokens.css                           (@import into styles.css)
  4. website/src/styles/_core-tokens.css                        (optional)
규칙:
  - space/radius/semantic/motion/duration = 생성 (통일)
  - role.* = 생성 안 함 (표면이 자기 값 유지)
  - 각 출력은 "AUTO-GENERATED — do not edit" 헤더 + 소스 커밋 해시
  - bun run gen-tokens 로 실행, CI에서 drift check (생성물 == 커밋된 것)
```

satori OG 파이프(script 하나로 생성)와 동일한 패턴 → 레포에 이미 있는 방식, 새 추상화 아님.

---

## 리팩터링 실행 순서 (5단계 — 표면 하나씩, 각 빌드 검증)

**리스크 순 (낮음→높음), 각 단계 후 빌드·회귀:**

1. **web-ai 먼저** (가장 격리됨, 빌드 빠름 `bun run build`, 12KB 번들)
   - `--maho-ai-space-*` → 코어 `--space-*` 치환, space-7 버그 수정
   - `bun run test` (14 tests) + `bun run typecheck`
2. **WebUI** (Tailwind, GN 빌드 일부)
   - spacing/radius를 코어 생성물로 치환, 네임스페이스 정리
   - 해당 feature 빌드 + 시각 회귀
3. **Native C++** (가장 무거움, 풀 Chromium 빌드)
   - `maho_design_tokens.h` 신설, spacing/radius 상수 참조로 치환
   - **색 mixer는 안 건드림** (Space 테마 로직 보존)
   - `build_maho.py` 풀 빌드 — 시간 오래 걸림, 신중
4. **Website** (선택 — 이미 DESIGN.md 커버, 낮은 우선순위)

**게이트:** 각 표면은 별도 커밋. 빌드 실패 시 GN 파일 강제 수정 금지(AGENTS.md 규칙) — 중단하고 보고.

---

## ⚠️ 실행 전 필수 확인

- **5단계는 제품 코드 수정** → 사용자 명시 승인 필수.
- **Native C++ 빌드는 풀 Chromium 빌드**(~121GB 소스, 오래 걸림) — 실행 여부·타이밍 사용자 결정.
- **`.build.lock` 규칙 준수** (삭제 금지), **GN 파일 강제 수정 금지**.
- 각 표면 리팩터링은 **독립 커밋**, 회귀 나오면 해당 표면만 revert 가능하게.
