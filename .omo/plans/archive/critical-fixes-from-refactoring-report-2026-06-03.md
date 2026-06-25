# Critical Fixes from Refactoring Report

**Date:** 2026-06-03
**Source:** `/Users/indo/.gemini/antigravity-cli/brain/9be2ff29-7d16-4191-a5c7-5bbade40096d/refactoring_report.md`
**Scope:** Refactoring report 의 항목 중 **critical 등급 2건만** 다룬다. 나머지(Boost 애니메이션, split tab 하드코딩, DnD 테스트 커버리지, Rust snake_case 경고, `maho-chromium/maho/` 빈 디렉토리, Mojom non-critical 정리)는 본 계획서에서 명시적으로 제외한다.

---

## Critical Item Selection Rationale

| Item | Severity | Reason |
|---|---|---|
| `chrome://maho-test` 진단 WebUI 의 무가드 등록 | **Critical** | 1,247-line 디버그 화면이 북마크 모델, 사이드바 노드, 활성 프로필 등 내부 상태를 HTML 로 덤프함. `maho_test_ui.cc/h`, `BUILD.gn` 어디에도 `#if !defined(NDEBUG)` / `BUILDFLAG(IS_CHROMEOS)` / dev-channel gate 가 없음. 릴리즈 빌드에 함께 컴파일·등록되면 사용자 단말에서 `chrome://maho-test` 가 살아있게 되어 정보 노출·시드 변조 위험. |
| Space Config `theme_json` 인터페이스 잔재 | **Critical** | ADR (`.sisyphus/plans/theme-picker-architecture-decision.md`, 2026-05-22) 에 의해 theme picker 는 `chrome://maho-space-create` 로 영구 확정. 그러나 `maho_space_config.mojom` 의 `UpdateColor(string color_hex, string? theme_json)` 와 `SpaceInfo.theme_json`, 그리고 `MahoSpaceConfigPageHandler::cached_theme_json_` 경로가 살아있음. 프런트엔드 (`app.ts:196`) 는 항상 빈 문자열을 보내 도달 불가. **API 계약 위반 + 영구 결정된 ADR 위반 가능성** 때문에 critical. |

> 위 분류 근거는 보고서 리뷰 단계에서 `maho_test_ui.cc:1207-1247`, `maho_space_config_page_handler.cc:64-66 / 130-167 / 188-203 / 280-301`, `maho_space_config.mojom:17-43`, `maho_space_config_page_handler.h:43-57`, `app.ts:195-197` 를 직접 읽어 검증함.

---

## Goal

1. `chrome://maho-test` 가 **release 빌드에 노출되지 않음을 컴파일 타임에 보장**한다.
2. `maho_space_config` 의 Mojom · 페이지 핸들러 · 프런트엔드에서 **`theme_json` 잔존 경로를 일관되게 정리**해 ADR 과 코드 상태를 일치시킨다.

## Non-Goals

- `chrome://maho-test` 의 기능 자체 제거 (debug 빌드에서는 그대로 유지).
- `maho-space-create` (theme picker) 쪽 변경.
- 보고서의 non-critical 항목 (TODO 주석 4건, Rust naming 2건, 빈 디렉토리, `maho-chromium/maho/` 트리 삭제) — 별도 계획서에서 다룬다.

---

## Item 1 — `chrome://maho-test` 프로덕션 노출 차단

### Current State (verified)

- `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.h` :
  - `MahoTestUIConfig : public DefaultWebUIConfig<MahoTestUI>` (line 21–24)
  - `MahoTestHTMLSource` 가 `BuildSidebarSnapshotHtml(profile_)` 결과를 그대로 응답 (line 26–45)
- `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc` :
  - `MahoTestUIConfig::MahoTestUIConfig() : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoTestHost) {}` (line 1209–1210)
  - `StartDataRequest` 가 `ShouldSeedRuntimeVerificationState(url)` 면 `SeedRuntimeVerificationState(profile_, GetSeedLevel(url))` 를 직접 실행 (line 1227–1229) — **단순 read-only 가 아니라 사이드바 시드 변조까지 가능**.
- `maho-chromium/browser/ui/webui/maho_test/BUILD.gn` :
  - `source_set("maho_test")` 가 어떤 빌드 플래그도 없이 무조건 컴파일 (line 3–20).
- `maho-chromium/components/constants/webui_url_constants.h:31-32` 에 `kMahoTestHost = "maho-test"` 가 상수로 노출.
- 사용처 확인:
  - `browser/ui/views/sidebar/maho_sidebar_dnd_browser_test.cc:73`
  - `browser/ui/views/sidebar/maho_sidebar_interactive_test_base.cc:159`
  - 즉, **interactive / browser test 에서만 사용**되고 production UI 코드 경로에서 직접 참조하지 않는다.

### Target State

- Release/official 빌드에서는 `MahoTestUI`, `MahoTestUIConfig`, `MahoTestHTMLSource`, 그리고 `kMahoTestHost`/`kMahoTestURL` 상수 사용처가 **컴파일에서 제외 또는 등록 차단** 된다.
- `chrome://maho-test` 입력 시 일반 `chrome://` 404/invalid-URL 경로로 처리된다.
- 기존 sidebar interactive/browser test 는 그대로 동작한다 (테스트 빌드는 가드 통과).

### Approach

두 가지 후보 중 **(B) GN buildflag 도입** 을 채택한다.

- (A) `#if !defined(NDEBUG)` 만 사용 — Chromium 의 official build 도 NDEBUG 라 가드되지만, GN 레벨에서 source 자체가 빌드되어 바이너리에 잔존. 추적성 떨어짐.
- (B) 전용 `enable_maho_test_webui` GN buildflag 도입 — `is_official_build` / `is_debug` / 채널 정책에 맞춰 GN 한 곳에서 켜고 끌 수 있고, C++ 가드도 명시적이라 리뷰가 쉽다.

### Steps

1. **GN buildflag 신설**
   - 파일: `maho-chromium/build/config/BUILD.gn` (없으면 신규) 또는 기존 `maho-chromium/build/config/` 내 적절한 위치.
   - 추가 내용 (의사):
     ```gn
     declare_args() {
       # Whether to compile the chrome://maho-test diagnostic WebUI.
       # Must remain false in official/stable builds.
       enable_maho_test_webui = is_debug || !is_official_build
     }

     buildflag_header("maho_test_buildflags") {
       header = "maho_test_buildflags.h"
       flags = [ "ENABLE_MAHO_TEST_WEBUI=$enable_maho_test_webui" ]
     }
     ```
   - 채널 정책이 따로 있으면 (`is_chrome_branded`, `is_official_build`) 거기에 맞춰 default 를 결정한다. **default 는 release-safe (false)** 가 되어야 한다.

2. **`source_set("maho_test")` 게이팅**
   - 파일: `maho-chromium/browser/ui/webui/maho_test/BUILD.gn`
   - `source_set` 전체를 `if (enable_maho_test_webui) { ... }` 로 감싼다.
   - `public_deps` 에 buildflag header target 추가.

3. **WebUI 등록 경로 가드**
   - `MahoTestUIConfig` 가 등록되는 지점을 확인 후 (`MahoWebUIConfigs`, `RegisterChromeWebUIConfigs`, 또는 overlay-side 등록 함수) `#if BUILDFLAG(ENABLE_MAHO_TEST_WEBUI)` 로 감싼다.
   - 등록 지점이 아직 식별되지 않은 경우 → **구현 단계에서 explore 로 우선 식별** (`grep -r "MahoTestUIConfig" maho-chromium`).

4. **상수 사용처 가드**
   - `kMahoTestHost`, `kMahoTestURL` 자체는 그대로 두되, **production 코드에서의 사용처가 없는지** 다시 확인 (현재까지 확인된 사용처는 sidebar 관련 test 2 곳뿐).
   - test 빌드는 buildflag 가 켜진 상태이므로 추가 작업 불필요.

5. **방어적 런타임 체크 (선택, 구현 시 합의 후 결정)**
   - `MahoTestHTMLSource::StartDataRequest` 진입 시 `chrome::GetChannel() == version_info::Channel::STABLE` 이면 즉시 빈 응답 / NOTREACHED. 컴파일 가드와 이중 방어.

### Verification

- `python3 maho-chromium/build/scripts/build_maho.py` (release/official 인자) → `chrome://maho-test` 호출 시 invalid URL.
- debug 빌드 → 기존 동작 유지, sidebar interactive/browser test 통과.
- `cd maho && bun run test` 영향 없음 (Rust 워크스페이스 미관여).
- `cd maho && bun run typecheck` 통과.
- 등록 지점에서 `MahoTestUIConfig` 가 컴파일 단위에 포함되지 않음을 `nm` / 빌드 산출물 grep 으로 확인.

### Risks

- **등록 지점 식별 실패** : interactive test 는 `chrome://maho-test/?seed=N` 으로 직접 navigate 하므로, config 등록이 어딘가에는 있다. 등록 위치를 못 찾으면 가드가 무력화될 수 있음 → 구현 첫 단계로 explore agent 1 회 필수.
- **테스트 빌드 환경에서 buildflag 누락** : interactive test 가 official 빌드 구성을 쓰면 깨질 수 있음 → 테스트 GN 구성에서 `enable_maho_test_webui = true` 강제 또는 default 가 그렇게 되도록 조건 설계.
- GN 파일 직접 수정은 AGENTS.md 의 “GN 강제 수정 금지” 규칙 대상이므로, **이 단계는 사용자 승인 후에만 진행**.

### Rollback

- buildflag 와 가드 패치만 revert 하면 원상 복구. 동작 코드를 삭제하지 않으므로 부작용 없음.

---

## Item 2 — Space Config `theme_json` 잔재 정리

### Current State (verified)

- ADR : `.sisyphus/plans/theme-picker-architecture-decision.md` (2026-05-22) — theme picker 는 `chrome://maho-space-create` 단일.
- Mojom (`browser/ui/webui/maho_space_config/maho_space_config.mojom`) :
  - line 22 : `string? theme_json;` in `SpaceInfo`
  - line 39 : `UpdateColor(string color_hex, string? theme_json) => (bool success);`
- 페이지 핸들러 헤더 (`maho_space_config_page_handler.h`) :
  - line 43–45 : `UpdateColor(... std::optional<std::string>& theme_json, ...)`
  - line 56 : `std::optional<std::string> cached_theme_json_;`
- 페이지 핸들러 구현 (`maho_space_config_page_handler.cc`) :
  - line 64–66 : `LoadSpaceOnPool` 결과의 `theme_json` 읽음.
  - line 130–167 : `RecolorSpaceOnPool` 안에서 `theme_json.empty()` 가 false 일 때 `update_space_config` 이벤트 디스패치 — **현재 도달 불가능 분기**.
  - line 188–203 : `GetSpaceInfo` 응답에 `cached_theme_json_` 가 있으면 다시 채워줌.
  - line 280–301 : `UpdateColor` → `RecolorSpaceOnPool` 후 성공 시 `cached_theme_json_` 캐시.
- 프런트엔드 (`browser/resources/maho_space_config/app.ts`) :
  - line 196 : `handler.updateColor(getColorHex(), '')` — 두 번째 인자는 항상 빈 문자열.
  - `theme_json` / `themeJson` 참조 0건 (grep 확인).

### Target State

- Mojom 인터페이스에서 `theme_json` 인자가 사라지거나 (옵션 A), 명시적으로 deprecated 표기 + ADR 참조 코멘트 추가 (옵션 B).
- 페이지 핸들러에서 `cached_theme_json_`, `RecolorSpaceOnPool` 의 `theme_json` 분기, `GetSpaceInfo` 의 캐시 주입 로직이 제거되거나, 유지된다면 ADR 위반이 아님이 코드 코멘트로 명문화된다.
- `app.ts` 와 동기화되어 빈 인자 호출이 사라지고 시그니처 일치.

### Approach

선호: **옵션 A — Mojom 인자 제거 + 백엔드 dead path 삭제**.

- ADR 이 명시적으로 “theme picker 는 `maho-space-create` 만” 이라고 못박았으므로, `space_config` 쪽에 `theme_json` 을 남겨두면 향후 다시 살아날 수 있는 유혹이 됨.
- `SpaceInfo.theme_json` 은 외부 source-of-truth 가 다른 경로 (e.g. core 가 보내주는 space dict) 일 가능성이 있으므로 **두 단계로 쪼갠다**.

### Steps

1. **Step 1 — `UpdateColor` 인자 제거 (낮은 위험, 우선 처리)**
   - `maho_space_config.mojom` line 39 → `UpdateColor(string color_hex) => (bool success);`
   - 페이지 핸들러 헤더 line 43–45 → `theme_json` 파라미터 삭제.
   - 구현 line 280–302 → `theme_json` 처리 분기 삭제. `RecolorSpaceOnPool` 호출도 인자 2개로 축소.
   - `RecolorSpaceOnPool` (line 130–167) 의 `theme_json` 분기 제거 → 함수 시그니처에서도 제거.
   - `app.ts:196` → `handler.updateColor(getColorHex())` 로 변경.
   - 프런트 Mojo TS 바인딩 자동 재생성 확인.

2. **Step 2 — `SpaceInfo.theme_json` 처리 (한 단계 더 신중)**
   - 호출자 / 소비자 audit:
     - 백엔드 : `LoadSpaceOnPool` 가 core dict 의 `theme_json` 을 읽음 (line 64–66) — core 쪽에서 여전히 보내주는지 확인 필요.
     - 프런트 : `app.ts` 에 `theme_json` 사용처 없음 (이미 grep 으로 검증).
   - 결정 트리:
     - core dict 가 `theme_json` 을 더 이상 보내지 않거나 무관하다고 확정되면 → Mojom `SpaceInfo.theme_json` 및 `cached_theme_json_`, `GetSpaceInfo` 캐시 주입 (line 188–203) 함께 제거.
     - core 쪽 의존이 남아 있으면 → Mojom 필드는 유지하되 핸들러 코멘트로 “read-only mirror; theme picker is `chrome://maho-space-create` only (ADR 2026-05-22)” 명시 + `cached_theme_json_` 만 제거 (write 경로는 죽음).
   - 이 결정은 구현 단계 explore 로 `LoadSpaceOnPool` / core 측 `space` dict 방출 지점 1회 확인 후 확정.

3. **Step 3 — ADR 링크 코멘트 추가**
   - 남게 되는 모든 `theme_json` 관련 식별자 위에 `// See .sisyphus/plans/theme-picker-architecture-decision.md (ADR 2026-05-22).` 한 줄.

### Verification

- `cd maho && bun run typecheck` 통과 (Rust 워크스페이스 영향 없음 가능성 높음).
- `python3 maho-chromium/build/scripts/build_maho.py` 통과 — Mojom 변경은 generated TS/CC 양쪽이 함께 갱신되어야 함.
- 수동 QA :
  1. `chrome://maho-space-config` 다이얼로그에서 색 변경 → 저장 → space 색이 반영.
  2. `chrome://maho-space-create` (theme picker) 흐름은 변경 없이 동작.
  3. core 이벤트 로그에 `update_space_config` 가 의도치 않게 사라지지 않았는지 확인 (어차피 도달 불가능 분기였음).
- `lsp_diagnostics` : 변경된 `.cc/.h/.ts/.mojom` 모두 0 error.

### Risks

- Mojom 변경은 generated 코드 광범위 영향. **반드시 빌드까지 통과시키고, 시그니처 mismatch 없는지 build_maho.py 결과로 확인**.
- core dict 의 `theme_json` 이 다른 의도로 사용 중일 수 있음 (예: future ADR-호환 broadcasting). Step 2 의 audit 단계가 부실하면 데이터 손실 가능 → 그래서 Step 2 는 **explore 1 회 + 결정 트리 명시** 후에만 진행.
- ADR 가 “theme picker 는 maho-space-create” 라고 했지, **per-space theme broadcast 자체가 금지**라고는 하지 않았을 가능성. 따라서 Step 2 에서 “기능 제거”가 아니라 “write path 만 제거 / read path 는 코멘트와 함께 유지” 옵션을 보존한다.

### Rollback

- Mojom 인자 복원 + 핸들러 시그니처 복원 + `app.ts` 호출부 복원 = revert 1 commit.
- `cached_theme_json_` 만 제거한 경우는 별도 commit 으로 분리해 revert 단위가 깔끔하도록 유지.

---

## Execution Order

1. Item 1 의 buildflag 신설 + `source_set` 가드 + 등록 지점 가드 (PR #1).
2. Item 2 의 Step 1 (`UpdateColor` 인자 제거) (PR #2).
3. Item 2 의 Step 2 (`SpaceInfo.theme_json` audit & 정리) (PR #3, audit 결과에 따라 옵션 A/B 분기).

각 PR 은 build + 수동 QA 통과 후에만 다음 단계로.

## Hard Blockers (사용자 승인 필요)

- AGENTS.md 의 “**빌드 실패 시 GN 파일 강제 수정 금지**” 규칙 때문에, Item 1 의 GN 파일 신규 추가/수정은 **사용자 명시 승인 후에만** 진행한다.
- Item 2 는 Mojom 변경이라 generated 코드와 함께 빌드를 돌려야 하므로, `build_maho.py` 가 GN regen 단계에서 실패할 경우 즉시 중단·보고 (GN 임의 수정 금지).

## Out of Scope (이 계획서에서 다루지 않음)

- 보고서 §2 (Boost 애니메이션 미구현, split tab 비주얼 데이터 하드코딩, sidebar DnD 테스트 커버리지).
- 보고서 §3 Rust snake_case 경고 2건.
- 보고서 §1 의 `maho-chromium/maho/` 빈 디렉토리 정리.
- 위 항목들은 별도 “tech-debt cleanup” 계획서에서 묶어서 다룬다.
