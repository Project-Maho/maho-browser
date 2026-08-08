# 커맨드 팔레트 AI 진입 플로우 분석 (Desktop Maho)

**작성:** 2026-07-20
**기준:** 2026-07-20 시점의 현재 작업 트리(working tree)를 근거로 분석했습니다. 워크스페이스가 다수의 미커밋 변경으로 상당히 더러운 상태이므로, 이 보고서는 깨끗한 `main` 스냅샷을 의미하지 않습니다.
**대상:** Desktop 커맨드 팔레트의 AI 진입 경로 두 가지에 한정합니다.

> **2026-07-24 현재 구현 업데이트:** 이 보고서는 2026-07-20의 더러운 작업 트리 상태를 기록한 역사적 분석입니다. 아래에서 설명하는 네이티브 `CommandOverlayMode::kAI` 팔레트 모드는 이후 영속적인 `Search / Ask Maho` 선택기와 `PaletteAction::kAskMaho`로 대체되었습니다. AI 사이드 패널/비공개 기능의 개인정보 게이트에는 계속 `MahoPrivateCapability::kAI`를 사용합니다. 시크릿 또는 off-the-record 컨텍스트에서는 Ask Maho와 모든 AI 진입을 차단하되, Search 입력은 계속 사용할 수 있어야 합니다.

- **(A) 인라인 `Ask Maho AI` 제안**: `kSearch` 계열 모드에서 사용자가 입력한 질의에 대해 자동으로 노출되는 제안 행입니다. `?`를 포함한 질의도 여기에 해당합니다.
- **(B) 사용자 호출형 `AI Mods` 플로우**: 정식 명칭은 `CommandOverlayMode::kAI`이며, `ai` 또는 `AI`를 입력한 뒤 Tab을 눌러 진입합니다.

본 보고서는 구현 근거에 기반하며, 모든 주요 동작 주장에는 저장소 상대 경로 `path:line` 인용을 붙였습니다. 라벨 `검증됨`, `교차 파일 결론`, `미검증/범위 밖`을 일관되게 사용합니다.

---

## 1. 요약 (Executive Summary)

- 두 플로우는 진입 방식이 근본적으로 다릅니다. (A)는 질의 분류 결과로 **자동 노출**되는 제안 행이고, (B)는 사용자가 명시적으로 **모드 전환**을 호출하는 상호작용입니다.
- **(A) 인라인 `Ask Maho AI` 행은 현재 데스크톱에서 활성화되지 않습니다.** Rust 코어는 `aiAnswer` 제안을 올바르게 생성하지만, C++ 측 파서·열거형·활성화 핸들러가 이 종류를 인지하지 못해 선택 시 아무 동작도 일어나지 않습니다. `교차 파일 결론`
- **(B) `AI Mods` 플로우의 네이티브 핸드오프는 구현된 대로 동작합니다.** Enter 시 대기 질의(pending query)를 pref에 기록하고 AI 사이드 패널을 엽니다. 다만 패널 부트스트랩은 질의를 **작성창(composer)에 채워 넣기만** 하며, 자동 제출은 하지 않습니다. 따라서 사용자는 패널에서 한 번 더 제출해야 합니다. 이 2회 제출 UX는 의도된 것이 아닐 수 있습니다. `검증됨`
- `?`는 특수 UI 트리거가 아니라 Rust `classify_query`가 검사하는 **일반 질의 문자**입니다. `?`가 포함되면 질의는 `AiPreferred`로 분류됩니다.
- 시크릿(incognito) 컨텍스트에서는 두 경로 모두 차단됩니다.

---

## 2. 용어 정정

| 명칭 | 정정 내용 |
|---|---|
| `?` | **특수 네이티브 UI 모드 트리거가 아닙니다.** Rust `classify_query`가 검사하는 일반 질의 문자이며, `?`가 포함된 질의는 모두 `AiPreferred`가 됩니다 (`maho/crates/maho-core/src/ai_search_intent.rs:8-19`). `>`(커맨드 전용 트리거)와 혼동해서는 안 됩니다. |
| `AI Mods` | 사용자 호출형 플로우의 통칭이며, 정식 코드명은 `CommandOverlayMode::kAI`입니다 (`maho-chromium/browser/ui/views/command/maho_command_model.h:38-45`). |
| `Ask Maho AI` | 인라인 제안 행의 제목 문자열입니다. Rust에서 `Ask Maho AI "<query>"` 형태로 생성됩니다 (`maho/crates/maho-core/src/command_bar.rs:410`). |
| Rust 커맨드바 경로 | **Android 전용이 아닙니다.** 데스크톱 C++도 동일한 FFI를 호출합니다 (§3.2 참조). |

---

## 3. 플로우 A: 인라인 `Ask Maho AI` 제안

### 3.1 사용자 관점 플로우

1. 사용자가 `kSearch`, `kNewTab`, `kCurrentTab` 중 한 모드에서 질의를 입력합니다.
2. 질의가 AI에 적합하다고 분류되면, 제안 목록에 `Ask Maho AI "<query>"` 행이 노출되어야 합니다.
3. 사용자가 이 행을 선택(Enter/클릭)하면 AI 응답으로 이어질 것으로 기대합니다.

### 3.2 구현 관점 플로우

**질의 분류 (Rust).** `classify_query`는 질의를 세 갈래로 나눕니다. `?`가 포함되면 곧바로 `AiPreferred`를 반환합니다 (`maho/crates/maho-core/src/ai_search_intent.rs:8-19`). WH 단어, 비교/설명 키워드, 4단어 이상 문장도 `AiPreferred`로 분류됩니다.

**FFI 호출 (C++ → Rust).** 데스크톱 C++는 Rust 커맨드바 FFI를 호출합니다. `MahoCommandModel::SearchOnPool`이 `maho_core_command_bar_search`를 호출하고 반환된 JSON을 파싱합니다 (`maho-chromium/browser/ui/views/command/maho_command_model.cc:782-797`). 따라서 `AiAnswer` 생성 경로는 Android 전용이 아니라 데스크톱에서도 실행됩니다. `검증됨`

**AiAnswer 생성 (Rust).** 분류 결과가 `AiPreferred` 또는 `Ambiguous`이고 시크릿이 아닐 때, 다음 제안이 추가됩니다 (`maho/crates/maho-core/src/command_bar.rs:396-420`).

- `kind = AiAnswer`
- `key = "ai_search:<query>"`
- `title = Ask Maho AI "<query>"`
- `subtitle = None`
- `execution_payload = None`
- `relevance_score / score = 1.8` (`AiPreferred`) 또는 `1.6` (`Ambiguous`)
- 게이트: `!ctx.is_incognito` (`command_bar.rs:396`)

**와이어 포맷 (계약).** `SuggestionType::AiAnswer`는 `#[serde(rename = "aiAnswer")]`로 직렬화됩니다 (`maho/crates/maho-types/src/traits/shell_renderer.rs:143-176`). `execution_payload`는 `None`일 때 `skip_serializing_if`로 생략됩니다. 계약 테스트가 `"aiAnswer"` 정확 문자열을 양방향으로 고정합니다 (`maho/crates/maho-types/tests/shared_contract_test.rs:556-569`).

### 3.3 시퀀스 다이어그램 (플로우 A)

```
User 입력("what is rust?")
   │
   ▼
[C++] MahoCommandModel::SearchOnPool
   │  maho_core_command_bar_search(query, mode, tabs, is_incognito)
   │  (maho_command_model.cc:782-797)
   ▼
[Rust] classify_query("...?") → AiPreferred
   │  (ai_search_intent.rs:8-19)
   ▼
[Rust] SearchResult { kind=AiAnswer, execution_payload=None, score=1.8 }
   │  (command_bar.rs:396-420)
   │  JSON: {"kind":"aiAnswer", ... (executionPayload 생략)}
   ▼
[C++] JSON 파싱 → ParseType("aiAnswer")
   │  (maho_command_model.cc:824-846, 914-939)
   │  ✗ "aiAnswer" 분기 없음 → kHistory 폴백 (:939)
   ▼
[C++] 사용자가 행 선택 → OpenCommandSuggestion
   │  type=kHistory, execution_payload="" (빈 문자열)
   │  (maho_command_action_handler.cc:622-735)
   ▼
   GURL("") 무효 → SchemeIsHTTPOrHTTPS/chrome 아님 → return
   ✗ 내비게이션 없음, AI 패널 열림 없음
```

---

## 4. 플로우 B: `AI Mods` (`CommandOverlayMode::kAI`)

### 4.1 사용자 관점 플로우

1. 사용자가 `kSearch`/`kNewTab`/`kCurrentTab`에서 `ai` 또는 `AI`만 입력합니다.
2. Tab을 누르면 오버레이가 AI 모드로 전환되고, 플레이스홀더는 `Ask anything…`, 칩 라벨은 `AI Mode`로 바뀝니다.
3. 질문을 입력하고 Enter를 누르면 오버레이가 닫히고 AI 사이드 패널이 열립니다.
4. Esc를 누르면 AI 모드에서 나와 진입 이전 모드로 복귀합니다. 오버레이 자체는 닫히지 않습니다.

### 4.2 구현 관점 플로우

**진입 (`ai`/`AI` + Tab).** Tab 처리에서 텍스트를 트리밍한 뒤 `kSearch`/`kNewTab`/`kCurrentTab`에서만, 정확히 `trimmed == "ai" || trimmed == "AI"`일 때 `AiIngressAllowed()` 게이트를 거쳐 `EnterAIMode()`를 호출합니다 (`maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:972-1003`).

**모드 상태 변경.** `EnterAIMode()`는 진입 이전 모드를 `origin_mode_`에 저장하고 `mode_ = kAI`로 전환하며 텍스트를 비웁니다 (`...maho_command_overlay_view.cc:1408-1430`).

**플레이스홀더/칩.** AI 모드 플레이스홀더는 `Ask anything…`, 칩 라벨은 `AI Mode`입니다 (`...maho_command_overlay_view.cc:1475-1553`).

**Esc 복귀.** Esc는 `ExitAIMode()`를 호출하며 (`...maho_command_overlay_view.cc:915-933`), `mode_ = origin_mode_`로 되돌려 진입 이전 모드를 복원합니다. 오버레이는 유지됩니다 (`...maho_command_overlay_view.cc:1432-1444`).

**Enter 확정.** AI 모드에서 Enter를 누르면, 질의가 비어 있지 않고 `AiIngressAllowed()`가 참일 때 `kPendingInitialQuery` pref에 질의를 기록하고, 오버레이를 닫은 뒤 `SidePanelEntryId::kMahoAiPanel`을 엽니다 (`...maho_command_overlay_view.cc:935-945`).

### 4.3 시퀀스 다이어그램 (플로우 B)

```
User 입력("ai") + Tab
   │  trimmed=="ai" && AiIngressAllowed()  (overlay_view.cc:972-1003)
   ▼
[C++] EnterAIMode()  origin_mode_ 저장, mode_=kAI, 텍스트 클리어
   │  플레이스홀더 "Ask anything…", 칩 "AI Mode"  (:1408-1430, :1475-1553)
   ▼
User 질문 입력 + Enter
   │  query 비어있지 않음 && AiIngressAllowed()  (:935-945)
   ▼
[C++] prefs.SetString(kPendingInitialQuery, query)
   │  Dismiss() + side_panel_ui->Show(kMahoAiPanel)
   ▼
[C++] MahoAIUI 생성자: pendingAiQuery 주입 후 pref 즉시 클리어
   │  (maho_ai_ui.cc:58-70)
   ▼
[React] bootstrap(initialPrompt) → composer.prompt = query
   │  자동 제출 없음  (react/store.ts:5-15, store.ts:102-107)
   ▼
User가 패널에서 다시 제출 → handleComposerSubmit  (app.tsx:192-276)
   ✓ 이때 비로소 실제 요청 전송
```

---

## 5. 두 플로우 비교

| 항목 | (A) 인라인 `Ask Maho AI` | (B) `AI Mods` (`kAI`) |
|---|---|---|
| 진입 방식 | 질의 분류 결과로 자동 노출되는 제안 행 | `ai`/`AI` + Tab으로 명시적 모드 전환 |
| 트리거 주체 | 시스템(분류기) | 사용자 |
| 근거 위치 | `command_bar.rs:396-420` | `overlay_view.cc:972-1003` |
| 활성화 payload | `execution_payload=None` | pref `kPendingInitialQuery` |
| 시크릿 처리 | Rust에서 제안 생략 (`command_bar.rs:396`) | 네이티브 진입 차단 (`overlay_view.cc:1403-1406`) |
| 현재 동작 여부 | **동작하지 않음** (활성화 갭) `교차 파일 결론` | 네이티브 핸드오프는 구현된 대로 동작, 단 패널에서 재제출 필요 `검증됨` |
| 최종 도착지 | (기대) AI 패널, (실제) 없음 | AI 사이드 패널(질의 프리필) |

---

## 6. 사이드 패널 핸드오프

이 절은 **AI Mode(플로우 B)가 실제로 사용하는** 사이드 패널 핸드오프와, **인라인 경로(플로우 A)가 도달하도록 의도된** 목적지를 함께 문서화합니다. 두 경로가 현재 모두 이 핸드오프를 실행한다는 의미가 아닙니다. §7.1에서 확인하듯 인라인 경로는 현재 AI 패널까지 도달하지 못합니다.

- **네이티브 → WebUI.** `MahoAIUI` 생성자가 `kPendingInitialQuery`를 읽어 `pendingAiQuery`로 데이터소스에 주입한 뒤, 곧바로 pref를 빈 문자열로 클리어합니다. 즉 일회성 전달입니다 (`maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc:58-70`).
- **부트스트랩은 프리필만.** React 부트스트랩은 대기 질의를 읽어 작성창 프롬프트에만 채웁니다 (`maho-chromium/browser/resources/maho_ai/react/store.ts:5-15`, `maho-chromium/browser/resources/maho_ai/store.ts:102-107`). 부트스트랩 단계에는 자동 제출이 없습니다.
- **제출 경로 (검토 범위 한정).** 검토한 부트스트랩 코드는 프리필만 수행합니다. 일반 작성창 제출 핸들러 `handleComposerSubmit`는 세션 유무에 따라 `startSessionThenSubmit` 또는 `submitPrompt`를 호출합니다 (`maho-chromium/browser/resources/maho_ai/react/app.tsx:192-276`). 이 외 다른 제출 진입점이 없다고 단정하지는 않습니다.
- **`검증됨` UX 결론.** 네이티브 Enter는 질문이 프리필된 상태로 패널을 열 뿐이며, 검토한 부트스트랩 코드 중 이를 자동으로 제출하는 호출은 없습니다. 사용자가 패널에서 직접 제출 동작을 수행해야 답변이 생성됩니다.

---

## 7. 검증된 결함과 UX 함의

### 7.1 인라인 `Ask Maho AI` 활성화 갭 `교차 파일 결론`

Rust는 `aiAnswer` 제안을 올바르게 생성하지만, C++ 경계에서 다음이 모두 성립합니다.

1. C++ 열거형 `CommandSuggestionType`에 `kAiAnswer`가 없습니다 (`maho-chromium/browser/ui/views/command/maho_command_model.h:47-60`).
2. `ParseType`에 `aiAnswer` 분기가 없어 알 수 없는 종류로 취급되어 `kHistory`로 폴백합니다 (`maho-chromium/browser/ui/views/command/maho_command_model.cc:914-939`).
3. Rust가 `execution_payload=None`을 보내므로 해당 필드는 비어 있습니다 (`maho-chromium/browser/ui/views/command/maho_command_model.cc:829-846`).
4. 활성화 시 `kHistory` 경로가 모든 특수 분기를 지나 마지막에 빈 문자열로 `GURL`을 만들고, 유효하지 않은 스킴이므로 그대로 반환합니다 (`maho-chromium/browser/ui/views/command/maho_command_action_handler.cc:622-735`).

**교차 파일 결론:** 인라인 `Ask Maho AI` 행을 선택해도 현재로서는 내비게이션도, AI 패널 열림도 발생하지 않습니다. 사용자에게는 아무 반응이 없는 행으로 보입니다.

### 7.2 `AI Mods` 재제출 부담 `검증됨`

플로우 B는 패널을 열지만 질문을 프리필만 하므로, 사용자가 패널에서 다시 제출해야 합니다 (§6). 커맨드 팔레트에서 Enter를 눌렀는데 답변이 즉시 시작되지 않으면 혼동을 줄 수 있습니다.

---

## 8. 기존 테스트와 커버리지 공백

| 영역 | 상태 | 근거 |
|---|---|---|
| `?` 분류기 테스트 | 존재 | `maho/crates/maho-core/src/ai_search_intent.rs:100-142` (`what is rust?` → `AiPreferred`) |
| `aiAnswer` 와이어 포맷 계약 | 존재 | `maho/crates/maho-types/tests/shared_contract_test.rs:556-569` |
| `ParseType`의 `aiAnswer` 매핑 | **공백** | `maho-chromium/browser/ui/views/command/maho_command_model_unittest.cc:221-255` (알려진 종류 목록에 `aiAnswer` 없음, 미지 종류는 `kHistory` 폴백) |
| AI 모드 진입/Esc 복귀 | 존재 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view_unittest.cc:86-123` |
| 시크릿 진입 차단 | 존재 | `...overlay_view_unittest.cc:86-123`, `maho-chromium/browser/ui/views/command/maho_command_incognito_interactive_uitest.cc:145-168` |
| AiAnswer 활성화가 패널까지 도달하는 교차 경계 테스트 | **공백** | 해당 시나리오를 검증하는 테스트가 없습니다 `검증됨` |

`미검증/범위 밖`: 활성화 갭이 패널에 도달하지 못함을 증명하는 자동화 테스트는 존재하지 않으므로, 회귀가 조용히 유지될 수 있습니다.

---

## 9. 시크릿(Incognito) 처리

- **Rust 측:** `!ctx.is_incognito`일 때만 `AiAnswer` 제안을 추가하므로 시크릿에서는 인라인 행이 생성되지 않습니다 (`maho/crates/maho-core/src/command_bar.rs:396`).
- **네이티브 측:** `AiIngressAllowed()`는 `kRegular` 또는 `kNull`만 허용하므로 시크릿에서는 `ai`+Tab 진입이 차단됩니다 (`maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:1403-1406`).
- **테스트:** 정규 진입/Esc/시크릿 거부 (`...overlay_view_unittest.cc:86-123`), 인프로세스 브라우저 테스트에서 시크릿 진입 거부 (`...maho_command_incognito_interactive_uitest.cc:145-168`).

---

## 10. 위험도 우선순위 권고 (상위 접합부만)

세부 구현은 규정하지 않고, 정렬이 필요한 상위 접합부만 제시합니다.

1. **(높음) AiAnswer 계약 정렬.** Rust가 생성하는 `aiAnswer` 종류를 C++ 열거형·`ParseType`·활성화 핸들러가 인식하도록 계약을 맞춰야 합니다. 현재는 조용한 무동작 상태입니다 (§7.1).
2. **(중간) 질의 핸드오프 의미 정의.** 패널 진입 시 프리필과 즉시 제출 중 무엇이 기대 동작인지 제품 차원에서 확정해야 합니다 (§6). 두 플로우가 동일한 의미를 공유하도록 정렬하는 것이 바람직합니다.
3. **(중간) 교차 경계 테스트 추가.** 인라인 AiAnswer 선택이 실제로 AI 표면에 도달하는지 검증하는 테스트를 추가해 활성화 갭 회귀를 방지합니다 (§8).
4. **(낮음) 제품 네이밍 정규화.** `Ask Maho AI`(제안 제목)와 `AI Mode`(칩 라벨), 내부 코드명 `kAI`, 통칭 `AI Mods` 사이의 명칭을 정리해 사용자·엔지니어 혼동을 줄입니다.

---

## 11. 증거 인덱스

| 주장 | 위치 |
|---|---|
| `?` → `AiPreferred` (일반 질의 문자) | `maho/crates/maho-core/src/ai_search_intent.rs:8-19` |
| `?` 분류기 테스트 | `maho/crates/maho-core/src/ai_search_intent.rs:100-142` |
| 데스크톱 C++가 Rust FFI 호출 | `maho-chromium/browser/ui/views/command/maho_command_model.cc:782-797` |
| AiAnswer 생성 (kind/title/payload/score/시크릿) | `maho/crates/maho-core/src/command_bar.rs:396-420` |
| `aiAnswer` 와이어 포맷 | `maho/crates/maho-types/src/traits/shell_renderer.rs:143-176` |
| 와이어 포맷 계약 테스트 | `maho/crates/maho-types/tests/shared_contract_test.rs:556-569` |
| C++ 열거형에 `kAiAnswer` 없음 | `maho-chromium/browser/ui/views/command/maho_command_model.h:47-60` |
| `ParseType` `aiAnswer` 분기 없음 → `kHistory` | `maho-chromium/browser/ui/views/command/maho_command_model.cc:914-939` |
| payload 파싱 (빈 상태) | `maho-chromium/browser/ui/views/command/maho_command_model.cc:829-846` |
| 활성화가 무효 빈 GURL로 반환 | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc:622-735` |
| `kAI` 열거형 주석 | `maho-chromium/browser/ui/views/command/maho_command_model.h:38-45` |
| `ai`/`AI` + Tab + 진입 게이트 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:972-1003` |
| `EnterAIMode` 상태 변경 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:1408-1430` |
| 플레이스홀더/칩 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:1475-1553` |
| Esc → 진입 이전 모드 복귀 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:915-933`, `:1432-1444` |
| Enter → pending pref 기록 + 패널 열기 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:935-945` |
| pending 질의 주입 후 pref 클리어 | `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc:58-70` |
| React 부트스트랩은 composer만 채움 | `maho-chromium/browser/resources/maho_ai/react/store.ts:5-15`, `maho-chromium/browser/resources/maho_ai/store.ts:102-107` |
| 작성창 제출 핸들러 `handleComposerSubmit` (startSessionThenSubmit/submitPrompt 호출) | `maho-chromium/browser/resources/maho_ai/react/app.tsx:192-276` |
| 시크릿: Rust 제안 생략 | `maho/crates/maho-core/src/command_bar.rs:396` |
| 시크릿: 네이티브 진입 차단 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:1403-1406` |
| 시크릿 테스트 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view_unittest.cc:86-123`, `maho-chromium/browser/ui/views/command/maho_command_incognito_interactive_uitest.cc:145-168` |
| `ParseType` 테스트 (aiAnswer 공백) | `maho-chromium/browser/ui/views/command/maho_command_model_unittest.cc:221-255` |
