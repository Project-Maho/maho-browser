# Maho Settings — UI-Driven Custom AI Provider Management

**Status:** DRAFT (pending momus review + user approval)
**Author:** Sisyphus
**Date:** 2026-06-04
**Target branch:** main
**Single PR:** YES (Phase 1-5 통합)

---

## 1. Goal

Maho 의 `chrome://maho-settings?pane=maho-ai` UI 만으로 사용자가 **OpenCode `models.json` 캐시에 없는 임의의 AI provider** (private gateway, self-hosted Ollama, niche OpenAI-compatible service 등) 를 추가/편집/삭제/검증할 수 있게 한다. 추가로 mid-session 모델 전환을 AI 패널 model selector 로 가능하게 한다. 모든 작업은 브라우저 내부에서 완결 — 터미널 CLI 호출 불필요.

## 2. Out of scope

- **글로벌 OpenCode CLI 와의 config 공유** — Maho 격리 환경 (`~/Library/Application Support/Maho/opencode/`) 만 사용. 글로벌 `~/.config/opencode/` 는 안 건드림.
- **AWS Bedrock SigV4 / Vertex AI / Azure AAD** 자동 검증 — 인증 복잡도 큼. `npm` 으로 식별 후 "Skip test, save anyway" UX.
- **Plugin npm 자동 install** — `BUNDLED_PROVIDERS` 22개 어댑터만 노출. 비-bundled npm 은 사용자가 직접 명시 (advanced).
- **OAuth redirect-based flow** — device-code polling 만 first-class.
- **Provider 단위 cost tracking / usage analytics** — 별도 작업.

## 3. Constraints

- **Type safety**: `as any` / `@ts-ignore` / `@ts-expect-error` 금지. C++ 에서 type suppression 금지.
- **Anti-pattern (per AGENTS.md)**:
  - Mojo handler 안에 비대한 로직 금지 — `browser/ai/` helper 로 위임
  - Memo/AGENT comment 금지 (Chromium 표준 패턴 예외)
  - GN 파일 강제 수정 금지 (사용자 명시 승인 외)
  - working tree dirty cleanup 금지
- **Storage**: Single source of truth = Chromium `PrefService`. OpenCode 격리 환경의 file 은 안 쓴다.
- **Mojom 변경**: ABI 호환성 깨지면 새 빌드 필요 (현재 Maho 는 IPC 외부 노출 없으므로 OK).
- **Network**: connection test 는 `network::SimpleURLLoader` (UI thread 안전, sandbox 통과).
- **Process model**: OpenCode 자식 프로세스에 auth 변경을 hot-reload 시킬 방법 없음 → 변경 시 다음 session 재spawn 필요 (사용자가 "Restart AI session" 의 의미 인지하도록 UI 표시).

## 4. Architecture decisions (5 baked-in)

### D1 — Single source of truth = Chromium prefs (CONFIRMED)

- 모든 provider/model/auth 데이터는 Chromium `PrefService` 의 새 DictPref 에만 저장.
- spawn 시 `OPENCODE_AUTH_CONTENT` / `OPENCODE_CONFIG_CONTENT` 환경변수로 자식 프로세스에 inject.
- 격리 경로 (`~/Library/Application Support/Maho/opencode/data/opencode/auth.json`) 의 file 은 **존재 안 함** (만들지 않음). 기존 `ReadOpenCodeAuthBlocking` 의 file fallback 은 제거 또는 read-only legacy migration only.

**근거:** 격리 환경 외부와 공유하지 않으므로 dual-write 의 동기화 부담은 무가치. env var inject 만으로 OpenCode 작동.

### D2 — Multi-provider DictPref 스키마 (NEW)

```
maho.ai.providers (Dict)
  → {
      "<provider-id>": {
        "kind": "builtin" | "custom",
        "display_name": string,
        "npm": string,                    // custom 만, builtin 은 OpenCode 가 자동
        "base_url": string,
        "auth_type": "api" | "oauth" | "wellknown" | "none",
        "api_key": string,                // type=api 만 (Chromium prefs 에 plaintext)
        "oauth_refresh": string,
        "oauth_access": string,
        "oauth_expires": int64,           // unix ms
        "oauth_account_id": string,       // copilot 등
        "wellknown_env": string,          // env var 이름
        "wellknown_token": string,
        "headers": Dict<string,string>,
        "models": List<string>            // 모델 ID 목록
      }
    }

maho.ai.active_provider_id (String) — 현재 선택된 provider id
maho.ai.active_model_id    (String) — 현재 선택된 model id
```

**기존 deprecated 키들** (`kProvider`, `kBaseUrl`, `kApiKey`, `kModel`):
- 마이그레이션 경로 1회 적용 (페이지 핸들러 init 시): 4 키에 값 있으면 → DictPref 의 새 entry 로 옮김 → `active_provider_id` 설정 → 4 키 비움
- 4 키 자체는 prefs.h 에 남기되 read 시도 안 함 (legacy fallback 코드 제거)

### D3 — Multi-provider env var serialization (Rust 변경)

`maho/crates/maho-acp/src/process.rs` 의 `AuthConfig` 구조 변경:

```rust
struct AuthConfig {
  active: ActiveSelection,             // { provider_id, model_id }
  providers: Vec<ProviderEntry>,       // 모두 inject
}

struct ProviderEntry {
  id: String,
  kind: ProviderKind,                  // Builtin | Custom { npm, base_url, models, headers }
  auth: AuthCredential,                // Api { key } | Oauth { refresh, access, expires, account_id } | Wellknown { env, token } | None
}
```

`to_opencode_auth_content_json()` 출력:
```json
{
  "anthropic":      { "type": "api", "key": "sk-ant-..." },
  "my-ollama":      { "type": "api", "key": "ollama" },
  "github-copilot": { "type": "oauth", "refresh": "...", "access": "...", "expires": 0 }
}
```

`to_opencode_config_content_json()` 출력:
```json
{
  "model": "anthropic/claude-sonnet-4-20250514",
  "provider": {
    "my-ollama": {
      "name": "My Ollama",
      "npm": "@ai-sdk/openai-compatible",
      "options": { "baseURL": "http://localhost:11434/v1" },
      "models": { "llama3.2": {} }
    }
  }
}
```

builtin provider 는 `provider` 항목에 안 넣음 (OpenCode 자체 catalog 사용). custom 만 넣음.

### D4 — Connection test = HTTP `/v1/models` (Bearer + Anthropic) (NEW C++ helper)

`network::SimpleURLLoader` 로 GET. npm 으로 인증 방식 분기:

| AuthScheme | npm 매칭 | Headers |
|---|---|---|
| `kBearer` | `@ai-sdk/openai`, `@ai-sdk/openai-compatible`, `@ai-sdk/groq`, `@ai-sdk/mistral`, `@ai-sdk/perplexity`, `@ai-sdk/togetherai`, `@ai-sdk/cerebras`, `@ai-sdk/deepinfra`, `@ai-sdk/cohere`, `@ai-sdk/xai`, `@ai-sdk/vercel`, `@openrouter/ai-sdk-provider`, `@ai-sdk/alibaba`, `@ai-sdk/gateway` | `Authorization: Bearer <key>` |
| `kAnthropic` | `@ai-sdk/anthropic` | `x-api-key: <key>`, `anthropic-version: 2023-06-01` |
| `kUnsupported` | `@ai-sdk/google*`, `@ai-sdk/azure`, `@ai-sdk/amazon-bedrock`, `gitlab-ai-provider`, `venice-ai-sdk-provider`, 모든 비-bundled | UI 가 "Skip test" 표시 |

응답 처리:
- 200 + JSON parse 성공 → `data[].id` 또는 `data[].model_id` 추출 → models 자동 채움 → ✓
- 401/403 → "API key 무효 또는 권한 부족"
- 4xx 그 외 → "endpoint 응답 형식 비호환 — 수동 모델 입력 필요"
- 네트워크 실패/타임아웃 (10s) → "endpoint 도달 불가"

### D5 — OAuth = device-code polling in-browser (NEW)

OpenCode 의 `auth login` flow 가 device-code (HTTP redirect 없음) → 브라우저 UI 친화적.

흐름:
```
사용자 "Connect with GitHub Copilot" 클릭
  ↓ mojom: StartAIOAuth(provider_id) 호출
  ↓ Page handler: ephemeral 자식 프로세스 spawn
                  → opencode auth login github-copilot --json
                  → stdout 파싱: { url, user_code, device_code, interval, expires_in }
  ↓ mojom 응답: AIOAuthState { verification_url, user_code, expires_in_seconds, poll_handle }
  ↓ UI 표시: 큰 user_code (mono font) + "Open https://github.com/login/device" 버튼
            + 폴링 spinner + countdown
  ↓ UI 가 PollAIOAuth(poll_handle) 을 5초마다 호출
  ↓ Page handler: 자식 프로세스 stdout 추가 폴링 → access_token 도착 또는 timeout
  ↓ 성공 시 prefs DictPref 의 해당 entry 에 oauth_* 필드 저장
  ↓ mojom 응답: { complete: true } → UI 가 모달 close
사용자가 모달 닫거나 cancel → CancelAIOAuth(poll_handle) → 자식 프로세스 SIGKILL
```

**자식 프로세스 안전:**
- spawn 위치: `MahoSettingsPageHandler` 가 `base::Process` 로 직접 spawn (UI thread blocking 회피, ThreadPool 위에서)
- timeout: 10분 hard limit (OAuth 자체 expires_in 보다 짧게 설정)
- stdout 라인 단위 파싱, 1MB 한도

**Wellknown** (`opencode auth login <url>`):
- `.well-known/opencode` 는 임의 명령 실행 가능 → 사용자 confirmation modal 필수
- "이 URL 이 다음 명령을 실행합니다: `gh auth token` — 진행?" 형식
- Phase 5 에서 추가, Phase 1-4 까지는 OAuth 만 (api+wellknown 보다 우선순위 낮음)

## 5. Mojom 표면 변경 (`maho_settings.mojom`)

### 새 struct

```mojom
struct AIProvider {
  string id;                       // primary key
  string display_name;
  string kind;                     // "builtin" | "custom"
  string npm;                      // custom 만 의미 있음
  string base_url;
  string auth_type;                // "api" | "oauth" | "wellknown" | "none"
  bool has_credentials;            // api_key/oauth_access/wellknown_token 존재 여부
  array<string> models;
};

struct AIProviderTestResult {
  bool ok;
  int32 http_status;               // 0 = network err
  string error_message;
  array<string> discovered_models; // 성공 시 자동 fill
};

struct AIOAuthState {
  bool ok;                         // false 시 UI 가 error 표시
  string error_message;
  string verification_url;
  string user_code;
  int32 expires_in_seconds;
  string poll_handle;              // 후속 Poll/Cancel 호출에 전달
};

struct AIOAuthPollResult {
  bool complete;                   // true = 토큰 저장 완료, false = 계속 폴
  bool failed;                     // true 시 error_message 참조
  string error_message;
};
```

### 기존 `AISettings` 수정

```mojom
struct AISettings {
  string active_provider_id;       // 새 (DictPref active_provider_id)
  string active_model_id;          // 새 (DictPref active_model_id)
  string approval_policy;
  bool session_persistence_enabled;
  // 제거: provider, base_url, model, has_api_key
  // (이제 ListAIProviders 의 entry 로 대체됨)
};
```

### RPC 추가/변경

```mojom
// === 기존 (유지) ===
GetAISettings()                                  => (AISettings settings);
SetAIApprovalPolicy(string);                      // 변경 없음
SetAISessionPersistence(bool);                    // 변경 없음

// === 변경 ===
SetActiveAI(string provider_id, string model_id) => (bool ok, string error);
//          ↑ 기존 SetAIProvider/SetAIBaseUrl/SetAIApiKey/SetAIModel 4개 통합
//            (active selection 만, individual provider 편집은 Update 로 분리)

// === 신규: provider CRUD ===
ListAIProviders()                                                     => (array<AIProvider>);
GetAIProvider(string provider_id)                                     => (AIProvider provider, bool found);
AddCustomAIProvider(AIProvider provider, string api_key)              => (bool ok, string error);
UpdateCustomAIProvider(AIProvider provider, string api_key_or_empty)  => (bool ok, string error);
//                                              ↑ "" 면 api_key 변경 안 함 (마스킹 유지)
DeleteAIProvider(string provider_id)                                  => (bool ok, string error);

// === 신규: 검증 + 모델 발견 ===
TestAIProvider(string base_url, string api_key, string npm)           => (AIProviderTestResult);
FetchModelList(string base_url, string api_key, string npm)           => (AIProviderTestResult);
//                                                                      ↑ Test 와 동일 endpoint, 명시적 의미

// === 신규: OAuth ===
StartAIOAuth(string provider_id)                                      => (AIOAuthState state);
PollAIOAuth(string poll_handle)                                       => (AIOAuthPollResult);
CancelAIOAuth(string poll_handle);

// === Phase 5 ===
ConnectWellKnownProvider(string url, bool confirm_command_execution)  => (bool ok, string error);
//                                   ↑ false 면 first call 에서 (false, "<command preview>") 반환
//                                     → UI 가 confirm 받고 true 로 재호출
```

기존 (제거):
- `SetAIProvider(string)` — 삭제
- `SetAIBaseUrl(string)` — 삭제
- `SetAIApiKey(string)` — 삭제
- `SetAIModel(string)` — 삭제
- `GetAIProviderList()` — `ListAIProviders` 로 대체

## 6. Storage layer (Page handler)

### 새 helper 클러스터 (`maho_settings_page_handler.cc`)

```cpp
namespace {

// Pref ↔ DictPref entry 변환
base::Value::Dict ProviderToDict(const mojom::AIProvider& p);
mojom::AIProviderPtr DictToProvider(const base::Value::Dict& d);

// 마이그레이션
void MaybeMigrateLegacyPrefs(PrefService* prefs);
//   if kProvider 비어있지 않음 →
//     providers[kProvider] = { kind: "custom", base_url: kBaseUrl, api_key: kApiKey, models: [kModel] }
//     active_provider_id = kProvider, active_model_id = kModel
//     kProvider/kBaseUrl/kApiKey/kModel = ""

// HTTP test (UI thread 에서 호출, callback 으로 응답)
class ProviderTester {
 public:
  void Test(const std::string& base_url, const std::string& api_key,
            AuthScheme scheme, base::OnceCallback<void(mojom::AIProviderTestResultPtr)>);
 private:
  std::unique_ptr<network::SimpleURLLoader> loader_;
};

enum class AuthScheme { kBearer, kAnthropic, kUnsupported };
AuthScheme InferAuthScheme(const std::string& npm);

// OAuth 자식 프로세스 관리
class OAuthFlowManager {
 public:
  void Start(const std::string& provider_id,
             base::OnceCallback<void(mojom::AIOAuthStatePtr)>);
  void Poll(const std::string& poll_handle,
            base::OnceCallback<void(mojom::AIOAuthPollResultPtr)>);
  void Cancel(const std::string& poll_handle);
 private:
  struct PendingFlow { base::Process child; ... };
  std::map<std::string, PendingFlow> flows_;
};

}  // namespace
```

### Active selection 적용 흐름

`SetActiveAI(provider_id, model_id)` 호출 시:
1. DictPref 에서 provider_id 조회 → 없으면 `(false, "Provider not found")`
2. provider.models 에 model_id 포함 확인 → 없으면 `(false, "Model not in provider")`
3. `active_provider_id` / `active_model_id` 설정
4. `MahoOpenCodeAcpAdapter::Reset()` 호출 → 다음 prompt 시 새 child 프로세스로 재spawn
5. UI 에 "AI session will restart on next prompt" toast (별도 mojom event 또는 polling)

### Adapter 변경 (`maho_opencode_acp_adapter.cc`)

기존:
```cpp
auth_dict.Set("provider", prefs_->GetString(kProvider));
auth_dict.Set("base_url", prefs_->GetString(kBaseUrl));
auth_dict.Set("api_key", prefs_->GetString(kApiKey));
auth_dict.Set("model", prefs_->GetString(kModel));
maho_acp_set_auth_config(handle_, json.c_str());
```

신규:
```cpp
const auto& providers_pref = prefs_->GetDict(kProviders);
const auto active_provider = prefs_->GetString(kActiveProviderId);
const auto active_model = prefs_->GetString(kActiveModelId);

base::Value::Dict auth_payload;
auth_payload.Set("active", base::Value::Dict{
  {"provider_id", active_provider},
  {"model_id", active_model},
});
base::Value::List providers_list;
for (const auto [id, value] : providers_pref) {
  providers_list.Append(SerializeForFfi(id, value.GetDict()));
}
auth_payload.Set("providers", std::move(providers_list));

maho_acp_set_auth_config(handle_, base::WriteJson(auth_payload).c_str());
```

### Rust 변경 (`maho-acp/src/process.rs`)

기존:
```rust
struct AuthConfig {
  provider: String,
  base_url: String,
  api_key: String,
  model: String,
}
```

신규:
```rust
struct AuthConfig {
  active: ActiveSelection,
  providers: Vec<ProviderEntry>,
}

struct ActiveSelection { provider_id: String, model_id: String }

enum ProviderEntry {
  Builtin {
    id: String,
    auth: AuthCredential,
  },
  Custom {
    id: String,
    display_name: String,
    npm: String,
    base_url: String,
    headers: BTreeMap<String, String>,
    models: Vec<String>,
    auth: AuthCredential,
  },
}

enum AuthCredential {
  None,
  Api { key: String },
  Oauth { refresh: String, access: String, expires_ms: i64, account_id: Option<String> },
  Wellknown { env_name: String, token: String },
}

impl AuthConfig {
  fn to_opencode_auth_content_json(&self) -> String { /* per-provider type=api/oauth/wellknown */ }
  fn to_opencode_config_content_json(&self) -> String { /* model: "<active>/<model>", provider: {<custom>: {...}} */ }
}
```

JSON 호환 deserialize: C++ 가 보내는 payload schema 그대로.

## 7. WebUI 변경

### Settings AI pane (`react/domain_panes.tsx`)

기존 `AiPane` 폼 (단일 provider) 을 다음으로 교체:

```tsx
function AiPane({ pane, store }: ...) {
  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="AI Configuration" />

      <ActiveAISection providers={...} active={...} onChange={...} />
      {/* Provider/Model 콤보박스 (활성 선택). model selector 도 같은 pane 안. */}

      <SectionDivider title="Providers" />
      <ProviderListSection
        providers={...}
        onEdit={openEditModal}
        onDelete={handleDelete}
        onAdd={openAddModal}
      />
      {/* builtin (137) + custom 섹션 분리, custom 에 [Edit] [Delete] 액션 */}

      <SectionDivider title="Behavior" />
      <ApprovalPolicyRow ... />
      <SessionPersistenceRow ... />
    </DomainPaneSection>
  );
}
```

### 새 컴포넌트들

- **`AddCustomProviderModal`** (Radix Dialog)
  - Step 1: 프로토콜 선택 (라디오) — `OpenAI compatible (Bearer)` / `Anthropic compatible` / `Other (npm 직접 명시)`
  - Step 2: ID, display_name, base_url, API key 입력
  - Step 3: "Test connection" 버튼 → 성공 시 `discovered_models` 자동 채움 / 실패 시 error 표시 + 수동 추가
  - Step 4: Models multi-input (chip list, 추가/제거)
  - Save 버튼 (test 통과 또는 "Skip test" 체크 시 활성)

- **`EditProviderModal`** (Radix Dialog) — Add 와 동일 폼, API key 마스킹 (`••••••••`), `requireConfirm` chip 적용

- **`DeleteProviderConfirm`** — inline confirm chip pattern (이전 Phase 작업에서 도입한 패턴 재사용)

- **`OAuthFlowModal`** (Radix Dialog)
  - 큰 user_code (`SF Mono` 폰트)
  - "Open verification URL" 버튼 (chrome 새 탭)
  - 폴링 spinner + countdown timer
  - "Cancel" 버튼

- **`ProviderListSection`**
  - Builtin section: 콤보박스 + active 표시
  - Custom section: list 각 row 마다 `[provider name] [base_url] [Edit] [Delete]`

### AI 패널 model selector (`maho_ai/resources/...`)

AI 패널에 mojom event subscriber 추가. 활성 provider 의 models list 변경 시 dropdown refresh. 사용자가 dropdown 에서 model 선택 → mojom `SetActiveAI(current_provider, new_model)` 호출.

세션 진행 중 변경 시 toast: "Switching model — session will restart on next message".

## 8. 마이그레이션

페이지 핸들러 init 또는 사용자 첫 GetAISettings 호출 시:

```cpp
void MaybeMigrateLegacyPrefs(PrefService* prefs) {
  if (prefs->GetDict(kProviders).size() > 0) return;  // 이미 마이그레이션됨

  const std::string legacy_provider = prefs->GetString(kProviderLegacy);
  if (legacy_provider.empty()) return;

  base::Value::Dict providers;
  base::Value::Dict entry;
  entry.Set("kind", "custom");                // 격리 환경이므로 custom 으로 보존
  entry.Set("display_name", legacy_provider);
  entry.Set("base_url", prefs->GetString(kBaseUrlLegacy));
  entry.Set("auth_type", "api");
  entry.Set("api_key", prefs->GetString(kApiKeyLegacy));
  entry.Set("models", base::Value::List().Append(prefs->GetString(kModelLegacy)));

  providers.Set(legacy_provider, std::move(entry));
  prefs->SetDict(kProviders, std::move(providers));
  prefs->SetString(kActiveProviderId, legacy_provider);
  prefs->SetString(kActiveModelId, prefs->GetString(kModelLegacy));

  prefs->ClearPref(kProviderLegacy);
  prefs->ClearPref(kBaseUrlLegacy);
  prefs->ClearPref(kApiKeyLegacy);
  prefs->ClearPref(kModelLegacy);
}
```

기존 사용자가 격리 환경 `auth.json` / `opencode.json` 에 데이터를 가지고 있는 경우:
- 1회 import flow: `ImportFromOpenCodeFiles()` (mojom RPC) — Settings 에 "Import existing OpenCode config" 버튼
- 파일 read → DictPref 에 머지 → import 후 "Imported N providers" toast
- Phase 5 에 포함 (필수 아님, opt-in)

## 9. Phase 분해 (단일 PR 안)

| Phase | 변경 영역 | 검증 기준 |
|---|---|---|
| **P1** Storage migration | prefs.h 새 키, registration, MaybeMigrate 헬퍼, mojom AISettings 리팩토링, Rust AuthConfig 다중화, env var 직렬화 | 1) 빌드 green 2) 기존 단일 provider 사용자가 마이그레이션 후 동일하게 작동 3) `cargo test` clean 4) AI 채팅 회귀 없음 |
| **P2** CRUD RPCs + UI | mojom CRUD 5개, page handler 구현, AddCustomProviderModal, EditProviderModal, ProviderListSection, DeleteProviderConfirm | 1) custom provider 추가 → ListAIProviders 에 보임 2) Edit 으로 base_url 변경 → 다음 spawn 에 적용 3) Delete 후 active 였으면 fallback 처리 4) builtin 은 Edit/Delete 비활성 |
| **P3** Connection test | TestAIProvider/FetchModelList RPC, ProviderTester C++ 클래스, AuthScheme 분기, modal 의 Test 버튼 | 1) Anthropic API key 로 실제 `/v1/models` 호출 → models 자동 채움 2) OpenAI key 동일 3) 잘못된 key → 401 메시지 4) unsupported npm → "Skip test" 자동 활성 |
| **P4** OAuth | StartAIOAuth/PollAIOAuth/CancelAIOAuth, OAuthFlowManager, OAuthFlowModal | 1) GitHub Copilot OAuth 통과 → access token 저장 2) 사용자 cancel → child SIGKILL 3) timeout 10분 4) device code 표시 정확 |
| **P5** Mid-session model switch + Wellknown + Import | AI 패널 model selector dropdown, ConnectWellKnownProvider, ImportFromOpenCodeFiles | 1) AI 패널에서 model 변경 → toast + 다음 메시지 시 새 session 2) wellknown URL 추가 → 명령 confirm modal 3) Import 1회 |

각 phase 간 dependencies:
- P1 → P2 → P3 → P4 → P5 (선형)
- P3 가 P2 modal 의 "Test" 버튼 활성화 (P2 단독으로는 Test 버튼 disabled)
- P5 의 model selector 는 P1 의 active_model_id 분리 덕분에 가능

## 10. 검증 (Verification matrix)

### 자동 검증 (per phase)

- ✅ `cd maho && bun run typecheck` (cargo check workspace) clean
- ✅ `cd maho && bun run test` (cargo test workspace) green
- ✅ `python3 maho-chromium/build/scripts/build_maho.py` exit 0
- ✅ `lsp_diagnostics` on changed `.tsx` / `.cc` / `.h` 0 errors
- ✅ Mojom 변경 시 `mojom-webui.js.d.ts` 손수 동기화 (현재 수동 mirror 패턴)

### 수동 검증 (사용자 / E2E)

- ✅ chrome://maho-settings?pane=maho-ai 로딩 정상
- ✅ 기존 사용자 (legacy prefs 보유) 마이그레이션 후 active provider/model 보존
- ✅ "Add Custom Provider" → Anthropic 호환 endpoint 입력 → Test 통과 → Save → list 표시
- ✅ Edit → API key 변경 → 다음 prompt 시 새 key 적용
- ✅ Delete → confirm chip → list 에서 제거, active 였으면 fallback (또는 비어있음)
- ✅ AI 패널에서 model 변경 → toast → 다음 메시지에 새 model 사용
- ✅ OAuth flow (GitHub Copilot) → user_code + URL 표시 → 브라우저 인증 → 완료 후 자동 close
- ✅ AWS Bedrock npm 선택 → "Skip test" 자동 활성 / 수동 모델 입력
- ✅ 잘못된 base URL 입력 → "Endpoint 도달 불가" 메시지
- ✅ 잘못된 API key 입력 → "API key 무효" 메시지

### 회귀 (regression)

- ✅ AI 패널 채팅 동작 (file attachment 포함) 영향 없음
- ✅ Memory pane 영향 없음
- ✅ Settings 다른 pane 영향 없음
- ✅ approval policy / session persistence toggle 영향 없음

## 11. Risks (mitigation 포함)

| Risk | Severity | Mitigation |
|---|---|---|
| **API key plaintext storage in Chromium prefs** | Medium | Phase 후 별도 Keychain integration (out of scope). UI 에 "API key is stored in browser preferences (plaintext)" 안내 |
| **OAuth child 프로세스 leak (사용자가 cancel 안 하고 settings 닫음)** | Medium | OAuthFlowManager 가 페이지 핸들러 destruction 시 모든 flow cancel. 추가로 process timeout 10분 hard limit |
| **Mojom 변경으로 in-flight WebUI bundle 호환 깨짐** | Low | 같은 PR 안에서 mojom + WebUI + d.ts mirror 동시 변경. dev cycle 중간 reload 필요 (사용자에게 notice) |
| **연결 test 가 사용자 ip 노출 (private gateway 의 reverse DNS 등)** | Low | `network::SimpleURLLoader` 가 표준 fetch 와 동일. 사용자 자기 endpoint 호출이라 의도된 동작 |
| **AuthScheme 추론 실패 — bundled 안에 없는 npm 입력** | Low | `kUnsupported` 로 fallback → "Skip test" UX |
| **마이그레이션 실패 (legacy 데이터 손실)** | High | MaybeMigrate 가 prefs read 만 하고 ClearPref 는 마이그 성공 후 실행. 실패 시 legacy 데이터 보존, 다음 init 에 재시도 |
| **OpenCode 자체가 custom provider 의 npm 패키지 import 실패** | Medium | 실제로는 사용자가 잘못된 npm 입력 (예: typo). spawn 시 자식 프로세스가 실패 → 에러 toast 표시 (기존 ACP 에러 핸들링 경로) |
| **Maho 자식 프로세스 spawn 시 환경변수 max length 초과** | Low | 137 builtin + N custom × N model 직렬화 시 OPENCODE_AUTH_CONTENT 가 매우 클 수 있음. macOS env max ~1MB 라 비현실적이지만 모니터링. 초과 시 D1 변경 (file-write fallback) 검토 |

## 12. Open questions (구현 전 확정 필요)

- [ ] **Q1**: OAuth child 프로세스 spawn 시 `opencode` binary path 어떻게 결정? Maho 가 번들한 opencode 를 쓸지, PATH 의 system opencode 를 쓸지. (현재 ACP 와 동일하게 PATH `opencode` 사용 결정 권장)
- [ ] **Q2**: `discovered_models` 자동 채움 시 사용자가 manual list 갖고 있으면 merge vs replace? (replace 권장 — Test 의 의미가 reset 이 자연스러움)
- [ ] **Q3**: builtin provider 도 Edit (api_key 만) 가능하게 할지, 별도 dialog 로 분리할지? (별도 dialog 권장 — builtin 은 base_url/npm/models 못 바꾸니까 다른 폼)
- [ ] **Q4**: ImportFromOpenCodeFiles 가 P5 에 들어가는데, 격리 환경의 `auth.json` 이 D1 결정으로 더 이상 안 만들어짐 → import 할 source 가 사라짐. 글로벌 `~/.config/opencode/auth.json` 에서 import 하는 것도 OK 인가? (현재 D1 OFF-스코프 였지만 1회 import 는 별개 동작. 사용자 수동 trigger 라 문제 없을 듯)

## 13. Non-goals (다시 한 번 명시)

- 개별 모델 단가/사용량 표시
- Provider 간 자동 fail-over
- 멀티 동시 active provider (현재 single active selection)
- AI 패널 외 다른 surface 의 model selector

## 14. References

- 현재 mojom: `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom`
- 현재 page handler: `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- 현재 adapter: `maho-chromium/browser/ai/maho_opencode_acp_adapter.cc`
- 현재 ACP process: `maho/crates/maho-acp/src/process.rs`
- OpenCode provider schema: `packages/core/src/v1/config/provider.ts` @ sst/opencode 69cfc44
- OpenCode auth schema: `packages/opencode/src/auth/index.ts` @ sst/opencode 69cfc44
- OpenCode bundled providers: `packages/opencode/src/provider/provider.ts` @ sst/opencode 69cfc44 line 108-135

