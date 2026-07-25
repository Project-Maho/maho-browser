# Aside 벤치마킹 현황: 단일 진실 문서 (Canonical Status)

**작성일:** 2026-07-21
**성격:** 내부 전략 문서. Aside 6개 벤치마크 영역에 대한 Maho의 현재 상태, 벤치마킹 대상, 구현 근거, 메시징 가드레일, 다음 문서 액션을 하나로 통합한 **단일 진실(single source of truth)** 문서.
**대상 독자:** Maho 제품/엔지니어링/마케팅 리더십.
**언어:** 한국어 본문 + 영어 기술 용어 혼용 (주변 경쟁 분석 문서 관례 유지).
**근거:** `docs/competitive-analysis/aside-benchmark-evidence-2026-07.md` (구현 증거 원장), 현행 ADR 0009 v2 / 0010 / 0014 / 0015, `docs/password-manager-security-fix-plan.md`, `docs/demos/README.md`, `docs/brand/BRAND.md`.

---

## 0. 이 문서를 읽는 법

- **왜 이 문서가 필요한가:** 2026-07 감사에서 구버전 v1 티어 가격 표기, 구버전 open-source ADR 등 **stale 문구**가 여러 문서에 남아 있음이 확인됐다. 앞으로 Aside 6개 영역에 대한 클레임은 이 문서를 기준으로 갱신한다.
- **상태 분류(taxonomy)** 는 증거 원장의 정의를 그대로 따른다: Shipped-in-browser / Implemented (crate), not launch-committed / Exists but gated/insecure / Planned / Strategy·docs only.
- **증거 원장이 사실의 원본이고, 이 문서는 그 사실을 의사결정 가능한 형태로 정리한 것이다.** 코드 라인 근거가 필요하면 증거 원장을 본다. 무엇을 말해도 되고 말면 안 되는지는 이 문서를 본다.

### 절대 규칙 (전 영역 공통)

- **Maho는 proprietary / closed-source 제품이다.** "open source", "auditable source", "코드 공개", "GitHub에서 소스 확인" 류의 문구를 마케팅·문서·WebUI 문자열에 넣지 않는다.
- **Maho 자체 소스를 가리키는 public 소스 저장소는 존재하지 않는다.** 다운로드는 release 전용 public 저장소(바이너리 + 릴리스 노트만)로만 배포된다. 소스 트리 URL을 링크하지 않는다.
- **모바일(iOS/Android) 출시 준비 완료(parity)를 주장하지 않는다.** 모바일 셸은 출시 시점 기준 reference-only.
- **Maho가 SOTA 벤치마크 수치로 경쟁한다고 주장하지 않는다.** (Aside의 반면교사, `docs/brand/BRAND.md` §5/§7.)

---

## 1. 현행 가격 정본 (Pricing Canon)

아래가 **현재 유일하게 유효한 가격 표기**다. 근거: ADR 0009 v2 (`docs/decisions/0009-openrouter-subscription-bm.md`), ADR 0010 (`docs/decisions/0010-payg-credits-model.md`).

| 항목 | 값 | 비고 |
|---|---|---|
| Free | **$0** | 월 $0.50 지출 상한(spend ceiling), 요청당 $0.05 cap, max_tokens 200, 기본 관리형 모델 `google/gemini-3-flash-lite:free` |
| Pro | **$5/월** | cheap-fast 계열 추가 (haiku/flash/mini/70B class), 요청당 $0.20 cap, max_tokens 2,000 |
| Max | **$20/월** | mid 계열 추가 (sonnet/gemini-pro/gpt-mini-medium class), 요청당 $0.50 cap, max_tokens 8,000 |
| PAYG 크레딧 | **$10 / $50 / $100 팩** | 소비 시 30% 마크업, 만료 없음, Max와 동일 모델 허용, subscription overflow 또는 standalone |
| BYOK | **무료 / 무제한** | 사용자 자기 키, 모든 모델, Maho 비용 0 |
| Frontier 모델 | **BYOK 전용** | Opus / GPT-5 base / o3·o4 / gemini-ultra / `:thinking`·`:reasoning-xhigh` 는 Maho-managed 티어(Free/Pro/Max/PAYG) 전부에서 제외 |

- **`$0.50` 은 가격이 아니라 Free 티어의 주기당 지출 상한이다.** CTA·데모 카피에서 "Free $0.50/mo" 로 쓰면 안 된다.
- **구표기 v1 3단 가격 조합(Free 지출 상한을 가격처럼 쓰고 상위 티어를 구값으로 적던 표기)은 폐기됐다.** 발견 시 위 §1 표로 정정한다 (Superseded 인덱스 참조).

---

## 2. 여섯 개 영역별 현황

### 2.1 Zero-setup AI 온보딩

- **현재 Maho 상태:** **Shipped-in-browser.** 로그인 사용자가 API 키 없이 바로 채팅 가능. `auth:access_token` 이 있으면 `SwiftideBackend` 가 BYOK 대신 billing 프록시로 OpenAI-호환 클라이언트를 구성한다. 데모 #01 "Zero-Setup AI" 가 출시 확정 기능.
- **Aside에서 벤치마킹하는 것:** Gemini 키 번들로 다운로드 즉시 AI가 동작하는 첫인상. Maho는 BYOK-only 모델이 아니라 **Maho-managed 프록시 경로**로 같은 zero-setup UX를 확보하되, Gemini 하드와이어가 아니라 프로바이더-불가지론적으로 간다.
- **구현 근거:** `maho/crates/maho-agent/src/swiftide_backend.rs` (managed 경로 981-996, BYOK 폴백 997-1021), `maho/crates/maho-agent/src/lib.rs` (`proxy_url()` 486-489), `maho/billing-proxy/src/lib.rs` (auth/tier/PAYG/OpenRouter), 테스트 `maho/crates/maho-agent/tests/swiftide_mahomanaged_test.rs`. 상세 라인은 증거 원장 §1.
- **메시징 가드레일:**
  - "키 없이 즉시 사용"은 말해도 된다 (출시 확정).
  - 번들 모델을 특정 브랜드로 못박지 않습니다. 코드 기본값은 `google/gemini-3-flash-lite:free` (`maho-agent` 의 `DEFAULT_MANAGED_MODEL` 및 billing-proxy `DEFAULT_MANAGED_MODEL`) 이며, ADR 0009 Annex A 의 `gemini-3` 계열 허용목록 안에 포함됩니다. 과거의 `gemini-2.5-flash` ↔ `gemini-3` slug drift 는 **해소**되었으나(양쪽 모두 `gemini-3-flash-lite:free` 사용), 허용목록은 회전될 수 있으므로 마케팅에서 구체 모델명을 확정 표기하지는 않습니다. 런치 문서에서 기본값을 인용할 때는 `google/gemini-3-flash-lite:free` 로 적습니다.
  - Frontier 모델 접근을 managed 경로로 약속하지 않는다 (BYOK 전용).
- **다음 문서 액션:** agent 기본 모델 slug 와 billing-proxy 허용목록 정합은 이미 맞춰졌습니다(양쪽 `google/gemini-3-flash-lite:free`). 향후 허용목록이 바뀌면 이 정합을 재확인하고 증거 원장 §1 을 갱신합니다. 가격/모델 관련 카피는 §1 정본을 인용합니다.

---

### 2.2 Tab Tidy

- **현재 Maho 상태:** **Shipped-in-browser.** 열린 탭을 AI로 원클릭 그룹화. Mojo IDL + page handler로 배선되어 full `chrome` 타겟에 링크됨. 데모 #02 "Tab Tidy" 가 출시 확정 기능이자 번들에서 가장 공유성 높은 샷.
- **Aside에서 벤치마킹하는 것:** `aside.verticalTabsTidy` 의 AI 기반 자동 탭 그룹화. Maho는 기존 사이드바 folder/section 인프라 위에 클러스터링 단계를 얹어 대응.
- **구현 근거:** `docs/operations/build-verification-2026-06-28.md` (PART 1/5, `RequestTabTidy`/`ApplyTabTidyFolders` override, full `chrome` 링크), `docs/operations/manual-test-runbook.md` B3/B7 (사이드바 `wand-2`/`wand_sparkles` → folder-suggestion 다이얼로그). 상세는 증거 원장 §2.
- **메시징 가드레일:**
  - "원클릭 AI 탭 정리"는 말해도 된다 (출시 확정, 데모 자산).
  - 로컬 IDE typecheck용 `maho_ai.mojom-webui.js.d.ts` 스텁에 `TabInfo`/`TidyFolder` 누락 caveat이 있으나 이는 **Chromium 빌드에 영향 없음**. 외부 메시징과 무관하니 언급 불필요.
- **다음 문서 액션:** 데모 스크립트/랜딩 카피에서 Tab Tidy 문구를 §1 정본 가격과 함께 검수. 별도 신규 문서 불필요, 이 문서 + 증거 원장으로 충분.

---

### 2.3 Routines

- **현재 Maho 상태:** **Implemented (crate), NOT launch-committed.** CLI 서브커맨드 + MCP 도구 쌍 + `chrome://maho-routines` WebUI 로 존재하고 Max 티어 게이트가 있으나, **출시 문서가 명시적으로 출시 클레임에서 제외**한다.
- **Aside에서 벤치마킹하는 것:** cron + 이벤트 트리거(푸시/inbox heartbeat) 기반 자동화 스케줄러. agentic 브라우저의 표준 UX가 될 가능성을 관찰 대상으로 둔다.
- **구현 근거:** CLI `maho/crates/maho-cli/src/main.rs` (`Routine` 129-133, `RoutineCommands::{List,Run}` 289-323, dispatch 810-822), MCP `maho/crates/maho-browser-mcp/src/server.rs` (`browser_routines_list` 1107-1118, `browser_routines_run` 1120-1131), WebUI 근거는 build-verification + manual-test-runbook B4. `Run` 의 과거 ignored-flag follow-up은 resolved 상태다. 현재 `--provider` / `--model` / `--yolo` / `--allow-tools` / `--workspace` 는 `unsupported_routine_run_flags` 로 감지되어 명시적 unsupported-flag 오류와 exit(2)로 거절된다. 상세는 증거 원장 §3.
- **메시징 가드레일:**
  - **ADR 0015에 따라 자동화 루틴 출시 클레임 허용 (범위 내).** (구 정책: historical note `docs/demos/README.md:80` "No claims about Routines, MCP host, or CLI. Not shipping at launch." 는 ADR 0015로 대체됨).
  - 외부 카피/데모 보이스오버에는 범위 내 기능(결과 인박스, 푸시/하트비트 트리거)만 반영.
- **다음 문서 액션:** Routines를 "구현됨, 출시 미확정" 상태로 유지. 출시 편입 결정이 생기면 이 문서 상태를 먼저 갱신하고, 그 다음 데모/랜딩 문서를 갱신 (역순 금지). `Run` 핸들러의 ignored-flag 후속 작업은 명시적 unsupported-flag rejection으로 완료되었으므로 별도 구현 티켓으로 다시 추적하지 않는다.

---

### 2.4 Agent 패스워드 매니저 메시징

- **현재 Maho 상태:** **Exists but gated/insecure.** 패스워드 저장 스택은 동작하나 **암호화가 실제로 활성화돼 있지 않고** 열람·provider 경로에 인가 공백이 있다. **"완전히 안전한(fully secure) / 출하된(shipped) agent-vault" 클레임은 현재 증거로 뒷받침되지 않는다.**
- **Aside에서 벤치마킹하는 것:** "agent가 시크릿을 AI에 노출하지 않고 자격증명을 autofill" + "결제/게시/메시지 등 민감 액션은 사용자 승인 대기" 라는 보안·통제 서사. Maho는 이 서사를 **지금 따라 주장할 수 없다.**
- **구현 근거:**
  - 코어 저장소가 인메모리 평문: `maho/crates/maho-core/src/password_manager.rs` 가 `Vec<SavedPassword>` 에 raw secret을 `password: Option<String>` 로 무암호 보관.
  - 활성 보안 결함: `docs/password-manager-security-fix-plan.md`. F1 "평문 디스크 저장 + SQLCipher 키 미배선(`set_sqlcipher_key` 호출자 0개)", F2 잔존 `*.db.plain_backup`, F3 열람 시 OS/생체 재인증 없음. **핵심 원칙(계획서 line 22): M1 배포 전에는 자체 매니저 신규 저장 기능을 사용자에게 노출하지 않는다.**
  - agent → 패스워드 vault 경로 없음: `maho/crates/maho-agent/src/swiftide_backend.rs` 의 agent 툴셋에 패스워드 매니저 툴이 배선돼 있지 않다.
- **메시징 가드레일:**
  - **금지:** "secure vault", "post-quantum", "Secure Enclave 보장", "agent가 안전하게 자격증명 사용", "패스워드 매니저 출하 완료".
  - 어떤 보안 클레임도 `docs/password-manager-security-fix-plan.md` (F1-F6, M1/M2/M3 DoD)를 정면으로 위배한다.
  - 대외 표기가 필요하면 "패스워드 관리 기능은 개발 중이며 보안 강화 작업(M1 암호화 활성화)이 선행 조건" 수준으로만.
- **다음 문서 액션:** M1(F1·F2) 완료·실측(키 없이 `maho.db` 미열림, `.plain_backup` 부재) 전까지 상태를 "gated/insecure" 로 고정. M1 DoD 충족 시 이 문서를 먼저 갱신하고 메시징 해제 여부를 리더십 리뷰에 회부.

---

### 2.5 MCP / CLI 개발자 표면과 in-browser agent action surface

- **현재 Maho 상태:** **Tools-Only MCP/CLI는 implemented이며 ADR 0015 범위 안의 launch-claim surface다.** MCP 능력 범위는 ADR 0014에 따라 **tools-only** 다. 별개로, in-browser agent action surface는 구현되었지만 `kMahoAgentBrowserActions` 로 flag-disabled 상태이며 **not launch-committed** 다. ADR 0015의 launch-claim scope를 확인한 결과, 이 flag-disabled action surface는 Tools-Only MCP/CLI GA 또는 scoped automation parity 항목에 포함되지 않는다.
- **Aside에서 벤치마킹하는 것:** MCP 호스트(외부 MCP 도구가 브라우저를 driver) + 터미널 CLI(agent 세션 kick-off, REPL). 개발자 페르소나 어필. Maho의 Tools-Only MCP/CLI는 ADR 0015의 현행 launch scope 안에 있으며, 새 in-browser action surface는 구현 상태 기록만 유지하고 출시 범위로 올리지 않는다.
- **구현 근거:** MCP 서버 `maho/crates/maho-browser-mcp/src/server.rs` 가 tab/history/bookmark/page-extract/a11y/network/leases/routines/mail 등 tools-only 표면을 노출하고 `{tools:{}}` 만 광고. tools-only 결정은 `docs/decisions/0014-oq2-resources-prompts.md` (**v1 NO-GO 확정**, resources/prompts 의도적 defer). CLI `maho/crates/maho-cli/src/main.rs` (`Mcp` 123-127/243-287, `Run` 138-162/830-848, `tool list/call` 164-168/227-241). Verified UDS transport는 `maho/crates/maho-browser-mcp/src/paths.rs` 기준 macOS/Linux 사용자 데이터 디렉터리의 `maho.sock` 으로 정렬되어 있으며, Todo 12 prep 기준 `tool list`, `tab list`, `browser_tab_list`, `browser_page_text`, `browser_accessibility_snapshot` 로 transport와 읽기 경로가 확인되었다. 이 확인은 `maho.sock` tools-only 통신의 동작 근거이지, flag-disabled in-browser action surface의 launch claim 근거가 아니다. 상세는 증거 원장 §5.
- **메시징 가드레일:**
  - **ADR 0015에 따라 Tools-Only MCP/CLI GA 출시 클레임은 허용된다.** 단, 이 허용은 tools-only transport와 CLI/MCP 개발자 표면에 한정된다.
  - **flag-disabled in-browser action surface에 대한 출시, shipped, GA 클레임은 금지한다.** 해당 표면은 구현되었지만 기본 비활성 상태이며, 출시 편입 결정 전까지 데모/랜딩/가격/마케팅 문구에 넣지 않는다.
  - **MCP는 v1에서 tools-only.** resources/prompts 지원을 주장하지 않는다 (ADR 0014). Full MCP 지원이나 OSS 소스 공개 클레임 금지.
  - `browser_ping` 은 광고되는 capability로 세지 않는다. 툴 개수나 capability 문구에는 포함하지 않는다.
- **다음 문서 액션:** MCP/CLI는 ADR 0015의 tools-only launch-claim scope 안에서만 다룬다. flag-disabled action surface를 출시 범위에 넣기로 결정되면 이 문서를 먼저 갱신하고, 그 다음 데모/랜딩 문서를 갱신한다 (역순 금지). 툴 개수는 재카운트 티켓으로 확정 후에만 문서 표기하며, `browser_ping` 은 카운트하지 않는다.

---

### 2.6 벤치마크 / 데모 프레이밍

- **현재 Maho 상태:** **Strategy/docs only.** 자체 셀러블 표면 없음. 출시 데모 3종(Zero-Setup AI, Tab Tidy, Tier Upgrade)과 하드 제약, 벤치 자세만 존재.
- **Aside에서 벤치마킹하는 것(반면교사):** Aside의 Online-Mind2Web 99% SOTA 주장은 자체 측정 + LLM-as-judge + suspend된 베이스라인 사용 등 방법론 비판을 받았다. Maho는 **SOTA 수치 경쟁을 하지 않고**, 사실 기반·공식 출처 수치만 쓴다.
- **구현 근거:** `docs/demos/README.md` (데모 3종 + 하드 제약: iOS/Android 클레임 금지, Routines/MCP/CLI 클레임 금지, 보이스오버에 경쟁사명 금지), `docs/brand/BRAND.md` §5(line 69)·§7(line 87) (자화자찬/미검증 벤치 금지). 상세는 증거 원장 §6.
- **메시징 가드레일:**
  - **금지:** "Maho SOTA", 미검증/자체측정 벤치 수치, 모바일 출시 준비 완료, 경쟁사명 데모 삽입.
  - **가격은 §1 정본만.** `docs/demos/README.md` line 82 CTA 가격은 §1 정본(Free $0/mo, $0.50 AI 상한 포함 / Pro $5/mo / Max $20/mo)으로 **이미 정정 완료(resolved)** 다. 구표기 v1 가격 조합은 여전히 폐기 상태이며 어떤 문서·카피에서도 재사용 금지.
  - 벤치가 필요하면 사용자 재현 가능한 비교(동일 task → 결과 비교) 형태로, 공식 출처만.
- **다음 문서 액션:** `docs/demos/README.md` line 82 CTA 가격은 §1 정본으로 **정정 완료(resolved)** — 추가 조치 불필요. 신규 벤치 자산 기획 시 BRAND.md §5/§7 체크리스트를 선통과.

---

## 3. 영역별 요약 표

| # | 영역 | 상태 | 출시 클레임 | 핵심 가드레일 |
|---|---|---|---|---|
| 1 | Zero-setup AI 온보딩 | Shipped-in-browser | 가능 | 모델 slug 확정 표기 금지, frontier managed 약속 금지 |
| 2 | Tab Tidy | Shipped-in-browser | 가능 | 데모 자산, 가격은 §1 정본 |
| 3 | Routines | Implemented, not launch-committed | **불가** | 출시 클레임/데모 삽입 금지 (demos README:80) |
| 4 | Agent 패스워드 매니저 | Exists but gated/insecure | **불가** | secure/shipped/PQ 클레임 금지 (fix-plan M1 선행) |
| 5 | MCP / CLI + in-browser agent actions | Tools-Only MCP/CLI implemented, ADR 0015 launch-scope. In-browser action surface implemented, flag-disabled, not launch-committed | MCP/CLI tools-only만 가능. Flag-disabled action surface는 **불가** | tools-only(ADR 0014), action surface 기본 비활성, `browser_ping` 카운트 금지 |
| 6 | 벤치마크 / 데모 프레이밍 | Strategy·docs only | n/a | SOTA·미검증 벤치 금지, §1 가격만 |

---

## 4. Canonical documents (정본 인덱스)

앞으로 아래 문서를 **현행 진실의 원본**으로 인용한다.

| 문서 | 경로 | 역할 |
|---|---|---|
| 본 문서 | `docs/competitive-analysis/aside-benchmark-status-2026-07.md` | 6개 영역 상태·가드레일·다음 액션의 단일 진실 |
| 구현 증거 원장 | `docs/competitive-analysis/aside-benchmark-evidence-2026-07.md` | 코드 라인 근거의 원본(사실) |
| Aside vs Maho 종합 비교 | `docs/competitive-analysis/aside-vs-maho.md` | 경쟁 분석 컨텍스트 (아래 4-note 예외 참조) |
| ADR 0009 v2 | `docs/decisions/0009-openrouter-subscription-bm.md` | **가격 정본** (Free $0 / Pro $5 / Max $20, frontier BYOK 전용) |
| ADR 0010 | `docs/decisions/0010-payg-credits-model.md` | PAYG 크레딧 정본 ($10/$50/$100, 30% 마크업) |
| ADR 0014 | `docs/decisions/0014-oq2-resources-prompts.md` | MCP v1 tools-only (resources/prompts NO-GO) |
| ADR 0015 | `docs/decisions/0015-ai-mcp-cli-launch-claims.md` | AI/MCP/CLI launch-claim scope와 hard guardrails. Flag-disabled in-browser action surface는 현행 launch scope 밖 |
| Aside local app architecture note | `docs/competitive-analysis/aside-local-app-architecture.md` | Aside 1.0.724.1 local bundle static-analysis note. Dirty-side observation이며 Maho 구현 계획이 아님 |
| 패스워드 보안 수정 계획 | `docs/password-manager-security-fix-plan.md` | 패스워드 매니저 보안 클레임의 게이트(M1-M3, DoD) |
| 데모 번들 | `docs/demos/README.md` | 출시 데모 범위 + 하드 제약 (line 82 CTA 가격은 §1 정본으로 정정 완료) |
| 브랜드 가이드 | `docs/brand/BRAND.md` §5/§7 | 보이스·벤치마크 규칙 |

> **4-note (aside-vs-maho.md 예외):** 이 문서 §9.3 #9 "sync relay 코드 공개 + 외부 감사" 권고는 현행 closed-source 정본과 충돌한다. 해당 open-source 권고는 **carry-forward 하지 않는다** (아래 Superseded 인덱스 참조). 문서의 나머지 경쟁 분석은 유효.

---

## 5. Superseded / historical documents (폐기·역사 인덱스)

아래는 **더 이상 현행 진실이 아니거나, 특정 문구를 carry-forward 하면 안 되는** 항목이다. (본 문서는 이들을 편집하지 않는다. 정정은 각 문서 자체의 갱신 티켓으로 처리한다.)

| 항목 | 위치 | 폐기 사유 / 정정 방향 |
|---|---|---|
| Open-source maho-core ADR | `docs/decisions/0004-open-source-maho-core.md` | "trust by transparency / auditable / 소스 공개" 전제가 현행 **proprietary·closed-source** 정본과 충돌. open-source·auditable 클레임의 근거로 인용 금지. |
| v1 가격 (구 3단 조합) | ADR 0001 v1 (`docs/decisions/0001-pricing-model.md`) 및 ADR 0009 revision history v1 | ADR 0009 **v2** ($0/$5/$20 + PAYG + frontier BYOK 전용)로 대체됨. §1 정본만 사용. |
| 과거 CTA 가격 표기 (정정 완료) | `docs/demos/README.md` line 82 | 과거 구표기 v1 가격 조합이 있었으나 §1 정본(Free $0/mo, Pro $5/mo, Max $20/mo)으로 **정정 완료(resolved)**. $0.50은 Free 지출 상한이지 가격이 아니며, 구조합은 어디에도 재사용 금지. |
| open-source relay 권고 | `docs/competitive-analysis/aside-vs-maho.md` §9.3 #9 | "relay 코드 공개 + 외부 감사" 는 closed-source 정본과 충돌. carry-forward 금지. |
| no-billing 정책 | `docs/security/no-billing-policy.md` | "Maho는 누구에게도 과금하지 않는다" 전제가 ADR 0009 v2·ADR 0010 으로 **역전(reversed)**. 이제 optional managed AI billing(Free $0 / Pro $5 / Max $20 + PAYG $10/$50/$100)이 존재하며, BYOK는 여전히 무료·무제한이고 frontier는 BYOK 전용. "과금 없음" 클레임의 근거로 인용 금지. Maho는 여전히 proprietary·closed-source (self-hostable relay는 아키텍처 신뢰이지 소스 공개가 아님). |
| 모바일 출시 준비 뉘앙스 | 각종 초기 문서의 iOS/Android 언급 | 출시 시점 모바일 셸은 reference-only. "출시 준비 완료/parity" 로 표기 금지 (demos README line 79). |

---

## 6. 갱신 규칙

1. 어떤 영역의 상태가 바뀌면 **이 문서를 먼저 갱신**하고, 그 다음 데모·랜딩·마케팅 문서를 갱신한다 (역순 금지).
2. 가격·모델·티어 표기는 항상 §1 정본과 ADR 0009 v2 / 0010 을 인용한다.
3. 패스워드 매니저 보안 문구는 `docs/password-manager-security-fix-plan.md` DoD 충족 실측 없이는 완화하지 않는다.
4. open-source / auditable / public 소스 저장소 문구는 어떤 영역에서도 추가하지 않는다.

**문서 끝.**
