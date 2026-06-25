# Maho AI Blank Panel Remediation Plan

**Created:** 2026-05-25
**Status:** Draft, 현재 main (commit `e2e5fbf`) 기준 분석 결과를 바탕으로 작성
**Primary scope:**
- `maho-chromium/build/scripts/apply_chromium_src_overrides.py`
- `maho-chromium/browser/resources/maho_ai/views/cards.ts`
- `maho-chromium/browser/resources/maho_ai/bundle_react.mjs`
- `maho-chromium/browser/resources/maho_ai/BUILD.gn`
- `maho-chromium/browser/resources/maho_ai/maho_ai.html`
- `maho-chromium/browser/resources/maho_ai/react/app.tsx`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
- `maho-chromium/browser/ui/views/side_panel/maho_ai_side_panel_coordinator.cc`

**Secondary scope (필요시 보조):**
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- 빌드 산출물 디렉터리 `chromium/src/out/Default/gen/maho-chromium/browser/resources/maho_ai/`

---

## 1. Objective

`chrome://maho-ai` 사이드 패널을 열었을 때 발생하는 빈 화면(blank panel) 증상을 제거한다. 동시에, 동일 부류의 문제가 다시 생겼을 때 원인을 빠르게 식별할 수 있도록 **무성(silent) 실패 경로를 모두 가시화**한다. 본 플랜은 조사용이 아니라 즉시 착수 가능한 구현 작업 단위로 구성한다.

성공 기준:
- 브라우저 재기동 후 사이드 패널 첫 진입에서 React 트리가 마운트되어 빈 화면이 보이지 않는다.
- 마크다운 렌더 실패, 번들 누락, 페이지 핸들러 부트스트랩 실패가 발생할 경우 콘솔/패널 모두에서 사용자/개발자가 식별 가능한 표시를 본다(은닉 빈 화면 금지).
- `parse_html_subset.ts` 미스매치 또는 stale 아티팩트가 발생해도 동기 렌더 크래시로 인한 패널 전체 공백이 발생하지 않는다.

---

## 2. Confirmed Findings

조사 단계에서 확인된 사실(요약):

1. **마크다운 렌더 경로의 동기 실패가 우선 원인.** `views/cards.ts`가 사용하는 `parse_html_subset.ts`는 Chromium 측 화이트리스트 기반 sanitizer이며, 허용되지 않는 태그/속성을 만나면 즉시 throw 한다. 이 throw 가 React 렌더 단계에서 잡히지 않으면 루트 컴포넌트가 마운트되지 못하고 패널이 통째로 빈 상태가 된다.
2. **`app_bundle.js` 산출물이 빠지거나 stale 한 케이스가 존재.** `bundle_react.mjs`가 `maho_ai.html`이 참조하는 번들 경로와 어긋날 때, 또는 `BUILD.gn`의 `grit`/`generate_grd` 입력 목록과 실제 산출물이 일치하지 않을 때, 스크립트 로딩 자체가 실패한다. 이 경우 HTML 컨테이너만 남고 React 부트가 일어나지 않는다.
3. **`CreatePageHandler()` 조기 반환.** `maho_ai_ui.cc`의 `CreatePageHandler()`가 `MahoOpencodeRuntimeService`/세션 매니저 미초기화, 잘못된 `BindInterface` 순서, 또는 사이드 패널 코디네이터(`maho_ai_side_panel_coordinator.cc`) 활성 시점 차이로 인해 receiver 바인딩 없이 일찍 반환되면, TS 측 `pageHandler.*` 호출이 무한 대기에 들어가고 초기 상태 머신이 “loading”에서 멈춰 빈 화면처럼 보인다.
4. **세 케이스 모두 콘솔 신호가 약하거나 사용자에게 보이지 않는다.** 현재 패널은 부트 실패 시 fallback UI 가 없어 “정상 vs 실패”를 구분할 수 없다.

---

## 3. Ranked Root-Cause Summary

우선순위는 **빈도와 사용자 영향**이 큰 순서다. 구현은 이 순서를 그대로 따른다.

| 순위 | 원인 | 핵심 증상 | 1차 책임 파일 |
|------|------|-----------|---------------|
| (a) | `parse_html_subset.ts` 화이트리스트 미스매치/stale 아티팩트로 인한 마크다운 렌더 동기 실패 | 응답이 도착하면 패널이 비어 보임. 콘솔에 sanitize 관련 throw 흔적 | `views/cards.ts`, `apply_chromium_src_overrides.py`, 산출물 sync |
| (b) | `app_bundle.js` 누락/경로 불일치/stale | 패널 진입 즉시 빈 화면. 네트워크 패널에서 404 또는 0 byte | `bundle_react.mjs`, `BUILD.gn`, `maho_ai.html` |
| (c) | `CreatePageHandler()` 조기 반환으로 Mojo 바인딩 실패 | React 트리는 마운트되지만 초기 데이터 로딩이 영원히 pending. 외형상 빈 화면 | `maho_ai_ui.cc`, `maho_ai_side_panel_coordinator.cc`, `react/app.tsx` 의 부트 게이트 |

---

## 4. Phased Implementation Plan

각 단계는 독립적으로 검증 가능하도록 분리한다. 단계 간 의존성은 5장에 명시한다.

### Phase 1: 마크다운 렌더 경로 안전화 (최우선)

목표: `parse_html_subset.ts`가 throw 하더라도 패널 전체가 죽지 않도록 한다. 그리고 `apply_chromium_src_overrides.py`가 부여하는 화이트리스트 경로의 일관성을 확정한다.

작업 내용:

1. `maho-chromium/browser/resources/maho_ai/views/cards.ts`
   - 마크다운 → 안전 HTML 변환 호출부를 좁은 try/catch 로 감싸고, 실패 시 다음 fallback 순서로 처리한다.
     1. 원문 텍스트(plain text) 그대로 렌더(코드 블록 또는 `<pre>` 형태).
     2. 카드 내부에 작은 경고 라인 표시: “렌더 실패: 안전하지 않은 마크업이 포함되었습니다.”
     3. 콘솔에 구조화 로그(`console.warn('[maho-ai] sanitize failed', { tag, reason })`)를 남긴다.
   - 위 fallback 은 컴포넌트 트리 어디에서도 throw 가 상위로 새지 않도록, 카드 단일 메시지 단위로 격리한다.

2. `maho-chromium/build/scripts/apply_chromium_src_overrides.py`
   - 현재 적용 중인 `parse_html_subset.ts` 오버라이드 또는 패치 항목을 점검하고, 다음 둘 중 하나를 명시적으로 보장한다.
     - 오버라이드 파일 경로/내용이 upstream Chromium 의 현재 시그니처와 일치(태그/속성 화이트리스트 동기화).
     - 오버라이드를 두지 않는다면, `cards.ts`에서 사용하는 화이트리스트는 우리 측에서 별도 sanitizer 모듈로 격리해 upstream 변동의 영향을 받지 않게 한다.
   - 어느 쪽이든 결과적으로 빌드 결정(deterministic) 이어야 한다. stale 아티팩트가 남으면 안 된다(아래 산출물 정합성 체크 포함).

3. `maho-chromium/browser/resources/maho_ai/react/app.tsx`
   - 루트에 React `ErrorBoundary`를 배치한다. 어떤 자식 트리가 throw 해도 패널은 “부트 실패 안내 + 재시도 버튼 + 마지막 에러 요약”을 표시한다.
   - 빈 화면 이슈의 사용자 가시성을 회복하는 핵심 장치다.

수용 기준:
- `cards.ts`에 의도적으로 잘못된 마크다운(허용되지 않은 태그 포함)을 주입해도, 해당 메시지만 fallback 렌더되고 패널 전체는 정상이다.
- ErrorBoundary 가 트리거되면 “Maho AI 패널 부트 실패” 메시지와 재시도 버튼이 보인다.

### Phase 2: 번들 산출물 정합성 보장

목표: `app_bundle.js`(또는 동등한 React 진입 번들) 가 `maho_ai.html` 이 참조하는 경로에 정확히 존재하고, GN 빌드 그래프와 일치한다.

작업 내용:

1. `maho-chromium/browser/resources/maho_ai/bundle_react.mjs`
   - 출력 파일 이름/경로를 단일 상수로 고정하고, 출력 직후 파일 존재/크기 검증을 수행해 0 byte 또는 누락 시 빌드를 실패시킨다.
   - 빌드 시점에 `app_bundle.js`의 import map / chunk 분할 결과를 표준 출력에 기록한다(원격 디버깅용).

2. `maho-chromium/browser/resources/maho_ai/BUILD.gn`
   - `bundle_react.mjs`의 출력물을 `grit`/`generate_grd` 입력에 명시적으로 포함하고, 산출물 경로가 `maho_ai.html`의 `<script src="...">` 와 1:1 매칭되도록 확인한다.
   - GN action 의 `outputs` 와 실제 스크립트 산출물 간 차이가 발생하지 않도록 입력/출력 목록을 정렬한다.
   - 본 플랜은 GN 파일 강제 수정 금지 정책(루트 AGENTS.md)에 따라, 변경이 필요할 경우 사용자 명시 승인 절차를 전제로 한다. 승인 없이 GN 파일을 임의로 “정리”하지 않는다.

3. `maho-chromium/browser/resources/maho_ai/maho_ai.html`
   - 스크립트 로딩 실패를 사용자에게 노출한다.
     - `<script>` 태그에 `onerror` 핸들러를 달아, 콘테이너에 “번들 로드 실패: <경로>” 메시지를 직접 주입한다.
     - DOM 마운트 게이트: 일정 시간(예: 2.5s) 안에 React 가 루트 컨테이너에 마운트되지 않으면, 동일한 안내를 표시한다(폴백 인라인 스크립트).
   - 이 단계는 빈 화면을 “식별 가능한 실패 화면”으로 바꾸는 핵심 가시화 작업이다.

수용 기준:
- 번들 산출물이 누락되도록 인위 조작했을 때, 패널은 빈 화면 대신 “번들 로드 실패: …” 안내를 표시한다.
- `BUILD.gn`/`bundle_react.mjs`/`maho_ai.html` 셋의 경로가 1:1 매칭됨을 코드 리뷰에서 확인 가능하다.

### Phase 3: 페이지 핸들러 부트스트랩 견고화

목표: `CreatePageHandler()` 가 어떤 사유로든 조기 반환할 때, 그 사실이 패널 UI 와 콘솔에 즉시 드러나도록 한다. 그리고 코디네이터 활성화 타이밍과의 race 를 줄인다.

작업 내용:

1. `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
   - `CreatePageHandler()` 내부의 모든 조기 반환 지점에 구조화된 로그를 추가한다(`LOG(ERROR) << "[maho-ai] CreatePageHandler aborted: <reason>"`). 단순 `return;` 금지, 사유 식별자 필수.
   - receiver 바인딩 직전에 “필수 의존성(런타임 서비스/세션 매니저/프로필) 가용성”을 한 번에 체크하고, 누락 시 명시적으로 “fail-fast” 경로를 타게 한다. 가능하다면 클라이언트로 일회성 “bootstrap_failed” 신호를 보낸다(Mojo 메시지 또는 클라이언트가 polling 가능한 상태값).

2. `maho-chromium/browser/ui/views/side_panel/maho_ai_side_panel_coordinator.cc`
   - WebUI 컨테이너 생성과 페이지 핸들러 바인딩 간의 순서를 점검하고, 다음을 보장한다.
     - 패널 가시화 직전에 의존 서비스가 준비됐는지 확인.
     - 준비되지 않은 상태에서 패널을 띄우는 경로를 차단하거나, 띄우더라도 “초기화 중” UI 가 React 측에 분명히 전달되도록 신호를 남긴다.
   - 본 단계 역시 GN/빌드 그래프 변경 없이 가능한 범위 안에서만 수정한다.

3. `maho-chromium/browser/resources/maho_ai/react/app.tsx`
   - 부트 시퀀스에 타임아웃 가드를 둔다. `pageHandler` 핸드셰이크가 일정 시간 이내(예: 3s) 응답하지 않으면, “페이지 핸들러 응답 없음” 안내와 재연결 버튼을 표시한다.
   - 재연결 버튼은 인터페이스 재바인딩을 시도하고, 실패 시 동일한 안내로 복귀한다.

수용 기준:
- `CreatePageHandler()` 가 의도적으로 조기 반환하도록 강제했을 때, 패널은 빈 화면 대신 “페이지 핸들러 응답 없음” 안내와 재연결 버튼을 보여준다.
- `chrome://device-log` 또는 표준 Chromium 로그에서 `[maho-ai] CreatePageHandler aborted: <reason>` 라인을 검색해 사유를 즉시 파악할 수 있다.

### Phase 4: 회귀 가시화 통합 점검

세 단계의 변경을 한 번에 묶어 점검한다.

작업 내용:
- `bundle_react.mjs`/`BUILD.gn`/`maho_ai.html` 의 파일명 상수가 한 곳에서 단일 진실 원본(single source of truth)으로 관리되는지 확인한다.
- `cards.ts` 의 sanitize fallback, `app.tsx` 의 ErrorBoundary, `app.tsx` 의 부트 타임아웃 가드, `maho_ai_ui.cc` 의 구조화 로그가 모두 켜져 있는 상태에서 패널을 재진입한다.
- 빈 화면이 발생했던 시나리오 재현 시도를 수행하고, 모든 시나리오에서 “식별 가능한 실패 화면 또는 정상 화면” 둘 중 하나만 나오는지 확인한다.

---

## 5. Sequencing and Dependencies

| Phase | 의존성 | 독립 적용 가능 여부 |
|-------|--------|---------------------|
| Phase 1 | 없음 | 가능. 빈 화면의 가장 흔한 트리거를 즉시 차단 |
| Phase 2 | 없음 | 가능. Phase 1 과 병렬 진행 가능 |
| Phase 3 | Phase 1 의 ErrorBoundary 가 먼저 들어와 있으면 “부분 실패 표시”가 자연스럽다 | 가능. 단독으로도 의미 있음 |
| Phase 4 | Phase 1, 2, 3 | 종합 점검 단계 |

권장 적용 순서: **Phase 1 → Phase 2 → Phase 3 → Phase 4.**

---

## 6. Verification Plan

검증은 빌드/테스트 실행을 동반하므로, 본 플랜 단계에서는 수행하지 않고 후속 구현 PR 에서 다음을 수행한다.

1. `python3 maho-chromium/build/scripts/build_maho.py` 가 정상 종료한다.
2. `bun run typecheck` 가 통과한다(루트 정책상 `cd maho && bun run typecheck`).
3. 수동 시나리오:
   - 정상 응답 마크다운 렌더 확인.
   - 의도적으로 잘못된 태그를 포함한 응답을 주입했을 때, 해당 메시지만 fallback 으로 표시되고 패널은 살아 있다.
   - `app_bundle.js` 산출물을 일시 제거했을 때, 패널은 빈 화면이 아닌 “번들 로드 실패: <경로>” 안내를 보여준다.
   - `CreatePageHandler()` 가 조기 반환하도록 의도적으로 트리거했을 때, 패널은 “페이지 핸들러 응답 없음” 안내 + 재연결 버튼을 보여준다.
4. 회귀 사례:
   - 정상 패널 진입에서 ErrorBoundary, 번들 폴백, 핸들러 타임아웃 안내가 보이지 않아야 한다(거짓 양성 0).

---

## 7. Risks and Open Questions

1. **`parse_html_subset.ts` 오버라이드 경계.** `apply_chromium_src_overrides.py` 로 upstream 파일을 덮어쓰는 방식이 향후 Chromium roll 에서 충돌을 만들 수 있다. 위험도가 높다고 판단되면 Phase 1 의 두 번째 옵션(우리 측 격리 sanitizer) 으로 전환을 검토한다.
2. **번들 경로 단일화.** `bundle_react.mjs` 출력, `BUILD.gn` action outputs, `maho_ai.html` script src 의 세 위치가 분산되어 있어 어긋날 여지가 크다. 본 플랜은 이를 “명시적으로 정렬”까지만 다루며, 단일 상수화는 별도 후속 항목으로 둔다.
3. **GN 파일 변경 정책.** 루트 AGENTS.md 정책에 따라, 빌드 실패가 GN regen 단계에서 발생할 경우 본 플랜의 작업자도 GN 파일을 임의 수정하지 않는다. 사용자 명시 승인 후에만 진행한다.
4. **부트 타임아웃 임계값.** 3초 가드가 저성능 환경에서 거짓 양성을 만들 수 있다. 설정 가능한 상수로 두고 실측 후 조정한다.
5. **`StartSession` 초기 컨텍스트 강제 부착(별건).** 본 플랜의 직접 범위는 아니지만, 빈 화면 회귀 디버깅 중 `StartSession` 의 `attach_browser_context=true` 강제 동작이 혼선을 줄 수 있어 메모로 남긴다.

---

## 8. Rollback and Containment

- 모든 변경은 단계 단위로 분리되어 있어, 문제가 발견된 단계만 되돌릴 수 있다.
- Phase 1 만 적용 후 회귀가 보이면, `cards.ts` 의 try/catch fallback 과 `app.tsx` 의 ErrorBoundary 만 제거해도 그 외 동작에는 영향이 없다.
- Phase 2 의 `maho_ai.html` 폴백 인라인 스크립트가 정상 부트 화면을 가리는 부작용이 보이면, 해당 스크립트만 제거하고 번들 산출물 정합성 작업만 유지한다.
- Phase 3 의 부트 타임아웃 가드가 거짓 양성을 만들면, 임계값을 늘리거나 가드를 잠시 비활성화한다. 구조화 로그(`maho_ai_ui.cc`) 는 그대로 유지해 회귀 추적 능력을 잃지 않는다.
- 빌드/번들 산출물 관련 사고가 의심되면, **`.build.lock` 을 절대 삭제하지 않는다**(루트 AGENTS.md 정책). 보유 프로세스를 식별하고 종료하거나 대기한다.
- 가시화 장치(콘솔 로그, 폴백 안내, ErrorBoundary, 타임아웃 가드)는 가능하면 항상 활성 상태로 둔다. 빈 화면을 다시 “보이지 않는 실패”로 되돌리지 않는 것이 본 플랜의 핵심 자산이다.

---

*본 플랜은 빈 화면 증상을 즉시 줄이는 것 못지않게, 동일 부류 회귀가 다시 “보이지 않게” 일어나지 않도록 가시화 장치를 함께 심는 데 무게를 둔다. 구현은 위 4단계의 순서대로 진행하고, 각 단계는 독립적으로 검증 가능한 산출물을 남긴다.*
