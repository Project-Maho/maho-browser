#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/rand_util.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ai/maho_ai_security_utils.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/ai/maho_capability_broker.h"
#include "maho/browser/ai/maho_credential_redaction.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/browser/ai/maho_model_list_fetcher.h"
#include "maho/browser/ai/maho_unified_agent_adapter.h"
#include "maho/browser/maho_ai_popup_lifetime_tracker.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_pending_surface.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_prefs_registration.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_runtime_event_persistence.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_voice_session.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_ai_provider_oauth.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"
#include "maho/browser/ui/webui/maho_subscription_checkout.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace {
base::NoDestructor<std::set<MahoAIPageHandler*>> g_active_page_handlers;

namespace ai = maho::ai_prefs;

static constexpr size_t kMaxPageContextChars = 8000;

// Capped at 100 sessions to keep profile prefs loading fast on startup
// while providing sufficient history size for regular usage.
constexpr size_t kMaxPersistedSessions = 100;

struct AIProviderDefinition {
  const char* id;
  const char* label;
  bool accepts_dynamic_model;
};

constexpr AIProviderDefinition kAIProviderOptions[] = {
    {"maho-managed", "Maho Managed", false},
    {"openai", "OpenAI", false},
    {"anthropic", "Anthropic", false},
    {"openai-compatible", "OpenAI-compatible", true},
    {"local-server", "Local Server", true},
};

bool IsSelectableProvider(const std::string& provider_id) {
  for (const auto& provider : kAIProviderOptions) {
    if (provider_id == provider.id) {
      return true;
    }
  }
  return false;
}

bool AcceptsDynamicModel(const std::string& provider_id) {
  for (const auto& provider : kAIProviderOptions) {
    if (provider_id == provider.id) {
      return provider.accepts_dynamic_model;
    }
  }
  return false;
}

void AppendReplayEvent(
    std::vector<maho_ai::mojom::RuntimeEventPtr>& events,
    maho_ai::mojom::RuntimeEventPtr event) {
  maho::ai::RuntimeReplayBudget budget;
  maho::ai::NormalizeReplayWindow(events, budget);
  maho::ai::AppendBoundedReplayEvent(events, budget, event);
}

bool IsProviderConfigured(const std::string& provider_id, PrefService* prefs) {
  if (!prefs) {
    return false;
  }
  if (provider_id == "maho-managed") {
    return maho::auth::HasValidRelaySession(prefs);
  }
  if (provider_id == "openai") {
    return !prefs->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64).empty() ||
           maho::ai_oauth::HasOAuthSession(prefs, "openai") ||
           (prefs->GetString(maho::ai_prefs::kProvider) == "openai" &&
            !prefs->GetString(maho::ai_prefs::kApiKey).empty());
  }
  if (provider_id == "anthropic") {
    return !prefs->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64).empty() ||
           maho::ai_oauth::HasOAuthSession(prefs, "anthropic") ||
           (prefs->GetString(maho::ai_prefs::kProvider) == "anthropic" &&
            !prefs->GetString(maho::ai_prefs::kApiKey).empty());
  }
  if (provider_id == "openai-compatible") {
    return (prefs->GetString(maho::ai_prefs::kProvider) == "openai-compatible") &&
           !prefs->GetString(maho::ai_prefs::kBaseUrl).empty();
  }
  if (provider_id == "local-server") {
    return (prefs->GetString(maho::ai_prefs::kProvider) == "local-server") &&
           !prefs->GetString(maho::ai_prefs::kBaseUrl).empty();
  }
  return false;
}

std::vector<std::string> GetSelectableModels(
    const std::string& provider_id,
    const std::string& active_provider_id,
    const std::string& active_model_id,
    PrefService* prefs = nullptr) {
  std::vector<std::string> models =
      maho::ai::MahoModelListFetcher::GetHardcodedFallback(provider_id);
  if (prefs) {
    const auto& cache = prefs->GetDict(maho::ai_prefs::kModelCache);
    if (const auto* provider_cache = cache.FindDict(provider_id)) {
      if (const auto* list = provider_cache->FindList("models")) {
        for (const auto& item : *list) {
          if (item.is_string()) {
            const std::string& name = item.GetString();
            if (std::find(models.begin(), models.end(), name) == models.end()) {
              models.push_back(name);
            }
          }
        }
      }
    }
  }
  if (provider_id == active_provider_id && !active_model_id.empty() &&
      std::find(models.begin(), models.end(), active_model_id) ==
          models.end()) {
    models.push_back(active_model_id);
  }
  return models;
}

bool IsSelectableModel(const std::string& provider_id,
                       const std::string& model_id,
                       const std::string& active_provider_id,
                       const std::string& active_model_id,
                       PrefService* prefs = nullptr) {
  if (model_id.empty()) {
    return false;
  }

  const std::vector<std::string> models =
      GetSelectableModels(provider_id, active_provider_id, active_model_id, prefs);
  return AcceptsDynamicModel(provider_id) ||
         std::find(models.begin(), models.end(), model_id) != models.end();
}

bool IsValidReasoningEffort(maho_ai::mojom::ReasoningEffort effort) {
  switch (effort) {
    case maho_ai::mojom::ReasoningEffort::kLow:
    case maho_ai::mojom::ReasoningEffort::kMedium:
    case maho_ai::mojom::ReasoningEffort::kHigh:
      return true;
  }
  return false;
}

maho_ai::mojom::ReasoningEffort ReasoningEffortFromPref(
    const std::string& effort) {
  if (effort == "low") {
    return maho_ai::mojom::ReasoningEffort::kLow;
  }
  if (effort == "high") {
    return maho_ai::mojom::ReasoningEffort::kHigh;
  }
  return maho_ai::mojom::ReasoningEffort::kMedium;
}

const char* ReasoningEffortToPref(maho_ai::mojom::ReasoningEffort effort) {
  switch (effort) {
    case maho_ai::mojom::ReasoningEffort::kLow:
      return "low";
    case maho_ai::mojom::ReasoningEffort::kMedium:
      return "medium";
    case maho_ai::mojom::ReasoningEffort::kHigh:
      return "high";
  }
  return "medium";
}

std::string GetRuntimeUnavailableMessage() {
  return "OpenCode runtime is not available. "
         "Please configure the runtime endpoint in settings.";
}

std::string ResolveMahoAiSettingsPaneKey(const std::string& pane_key) {
  if (pane_key == "maho-ai-developers") {
    return pane_key;
  }

  return "maho-ai";
}

std::string GenerateSessionId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

const net::NetworkTrafficAnnotationTag kCreditBalanceTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_ai_credit_balance", R"(
      semantics {
        sender: "Maho AI Credit Balance"
        description:
          "Fetches PAYG credit balance for the active user from the Maho relay."
        trigger:
          "The user opens the Maho AI UI and the product requests the current credit balance."
        data:
          "Bearer access token in the Authorization header."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting:
          "This request is enabled when the user is signed in and opens the Maho AI surface."
        policy_exception_justification:
          "Not controlled by enterprise policy. Triggered when the signed-in "
          "user opens the AI panel. Fetches the current PAYG credit balance. "
          "No ambient or background network activity."
      }
    )");

// UTF-8 continuation bytes match 10xxxxxx (0x80–0xBF); walking back past them
// lands on the lead byte of the interrupted sequence, giving a safe cut point.
std::string UTF8SafeTruncate(const std::string& text, size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return text;
  }
  size_t cut = max_bytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return text.substr(0, cut);
}

std::string DeriveTitle(const std::string& text) {
  constexpr size_t kMaxTitleLen = 80;
  std::string title = UTF8SafeTruncate(text, kMaxTitleLen);
  size_t newline = title.find('\n');
  if (newline != std::string::npos) {
    title = title.substr(0, newline);
  }
  if (text.size() > kMaxTitleLen && title.size() < text.size()) {
    title += "...";
  }
  return title;
}

std::string SerializeValueToJson(base::Value value) {
  std::string json;
  base::JSONWriter::Write(value, &json);
  return json;
}

maho_ai::mojom::ApprovalPolicy ApprovalPolicyFromString(
    const std::string* value) {
  if (!value) {
    return maho_ai::mojom::ApprovalPolicy::kPrompt;
  }
  if (*value == "allow_all") {
    return maho_ai::mojom::ApprovalPolicy::kAllowAll;
  }
  if (*value == "allow_mcp") {
    return maho_ai::mojom::ApprovalPolicy::kAllowMcp;
  }
  if (*value == "deny_sensitive") {
    return maho_ai::mojom::ApprovalPolicy::kDenySensitive;
  }
  if (*value == "deny_all") {
    return maho_ai::mojom::ApprovalPolicy::kDenyAll;
  }
  return maho_ai::mojom::ApprovalPolicy::kPrompt;
}

maho_ai::mojom::ApprovalSensitivity ApprovalSensitivityFromString(
    const std::string* value) {
  if (value && *value == "read_only") {
    return maho_ai::mojom::ApprovalSensitivity::kReadOnly;
  }
  return maho_ai::mojom::ApprovalSensitivity::kSensitive;
}

maho_ai::mojom::ApprovalState ApprovalStateFromString(
    const std::string* value,
    maho_ai::mojom::ApprovalState fallback) {
  if (!value) {
    return fallback;
  }
  if (*value == "approved") {
    return maho_ai::mojom::ApprovalState::kApproved;
  }
  if (*value == "denied") {
    return maho_ai::mojom::ApprovalState::kDenied;
  }
  if (*value == "cancelled") {
    return maho_ai::mojom::ApprovalState::kCancelled;
  }
  return maho_ai::mojom::ApprovalState::kPending;
}

maho_ai::mojom::ApprovalDecision ApprovalDecisionFromString(
    const std::string* value,
    maho_ai::mojom::ApprovalDecision fallback) {
  if (!value) {
    return fallback;
  }
  if (*value == "allow") {
    return maho_ai::mojom::ApprovalDecision::kAllow;
  }
  if (*value == "allow_once") {
    return maho_ai::mojom::ApprovalDecision::kAllowOnce;
  }
  if (*value == "deny") {
    return maho_ai::mojom::ApprovalDecision::kDeny;
  }
  return maho_ai::mojom::ApprovalDecision::kNone;
}

constexpr char kSafeCredentialFailureText[] =
    "Your saved AI credential could not be used.";

maho_ai::mojom::CredentialErrorCode CredentialErrorCodeFromRuntime(
    MahoAiRuntimeErrorCode code) {
  switch (code) {
    case MahoAiRuntimeErrorCode::kProviderNotConfigured:
      return maho_ai::mojom::CredentialErrorCode::kProviderNotConfigured;
    case MahoAiRuntimeErrorCode::kCredentialUnusable:
      return maho_ai::mojom::CredentialErrorCode::kCredentialUnusable;
    case MahoAiRuntimeErrorCode::kSecureStoreUnavailable:
      return maho_ai::mojom::CredentialErrorCode::kSecureStoreUnavailable;
    case MahoAiRuntimeErrorCode::kCredentialDecryptFailed:
      return maho_ai::mojom::CredentialErrorCode::kCredentialDecryptFailed;
    case MahoAiRuntimeErrorCode::kManagedAuthUnavailable:
      return maho_ai::mojom::CredentialErrorCode::kManagedAuthUnavailable;
    case MahoAiRuntimeErrorCode::kUnsupportedProvider:
      return maho_ai::mojom::CredentialErrorCode::kUnsupportedProvider;
  }
  return maho_ai::mojom::CredentialErrorCode::kGeneric;
}

maho_ai::mojom::CredentialErrorCode CredentialErrorCodeFromPersistedSymbol(
    const std::string& symbol) {
  if (symbol == "provider_not_configured") {
    return maho_ai::mojom::CredentialErrorCode::kProviderNotConfigured;
  }
  if (symbol == "credential_unusable") {
    return maho_ai::mojom::CredentialErrorCode::kCredentialUnusable;
  }
  if (symbol == "secure_store_unavailable") {
    return maho_ai::mojom::CredentialErrorCode::kSecureStoreUnavailable;
  }
  if (symbol == "credential_decrypt_failed") {
    return maho_ai::mojom::CredentialErrorCode::kCredentialDecryptFailed;
  }
  if (symbol == "managed_auth_unavailable") {
    return maho_ai::mojom::CredentialErrorCode::kManagedAuthUnavailable;
  }
  if (symbol == "unsupported_provider") {
    return maho_ai::mojom::CredentialErrorCode::kUnsupportedProvider;
  }
  return maho_ai::mojom::CredentialErrorCode::kGeneric;
}

// Builds an ArtifactInfo from the normalized JSON payload carried in a
// kArtifactCreated event's text field. Returns null if the payload is missing
// required fields (paths are intentionally never present here).
maho_ai::mojom::ArtifactInfoPtr ArtifactInfoFromJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }
  const base::DictValue& d = parsed->GetDict();
  const std::string* id = d.FindString("artifact_id");
  const std::string* sid = d.FindString("session_id");
  const std::string* name = d.FindString("display_name");
  const std::string* mime = d.FindString("mime_type");
  if (!id || !sid || !name || !mime) {
    return nullptr;
  }
  auto info = maho_ai::mojom::ArtifactInfo::New();
  info->artifact_id = *id;
  info->session_id = *sid;
  info->display_name = *name;
  info->mime_type = *mime;
  info->size_bytes =
      static_cast<uint64_t>(d.FindDouble("size_bytes").value_or(0));
  info->created_at = d.FindDouble("created_at").value_or(0);
  return info;
}

maho_ai::mojom::RuntimeEventPtr DictToRuntimeEvent(
    const base::DictValue& dict,
    const std::string& owning_session_id) {
  const std::string* persisted_session_id = dict.FindString("session_id");
  if (persisted_session_id && !persisted_session_id->empty() &&
      *persisted_session_id != owning_session_id) {
    return nullptr;
  }

  auto event = maho_ai::mojom::RuntimeEvent::New();
  event->kind = static_cast<maho_ai::mojom::RuntimeEventKind>(
      dict.FindInt("kind").value_or(0));
  event->session_id = owning_session_id;
  const std::string* request_id = dict.FindString("request_id");
  if (request_id) {
    event->request_id = *request_id;
  }
  event->sequence =
      static_cast<uint64_t>(dict.FindDouble("sequence").value_or(0));
  event->sequence_start = static_cast<uint64_t>(
      dict.FindDouble("sequence_start").value_or(event->sequence));
  event->timestamp = dict.FindDouble("timestamp").value_or(0);
  if (const auto* window = dict.FindDict("replay_window")) {
    event->replay_window = maho_ai::mojom::ReplayWindowInfo::New(
        static_cast<uint64_t>(window->FindDouble("omitted_turns").value_or(0)),
        static_cast<uint64_t>(
            window->FindDouble("omitted_through_sequence").value_or(0)));
  }
  const std::string* text = dict.FindString("text");
  if (text) {
    event->text = *text;
  }
  if (event->kind == maho_ai::mojom::RuntimeEventKind::kArtifactCreated &&
      text) {
    event->artifact = ArtifactInfoFromJson(*text);
  }
  const std::string* credential_error_code =
      dict.FindString("credential_error_code");
  if (credential_error_code) {
    event->credential_error_code =
        CredentialErrorCodeFromPersistedSymbol(*credential_error_code);
    event->text = kSafeCredentialFailureText;
  }

  const base::DictValue* tc = dict.FindDict("tool_call");
  if (tc) {
    auto tool_call = maho_ai::mojom::ToolCallInfo::New();
    const std::string* cid = tc->FindString("call_id");
    tool_call->call_id = cid ? *cid : "";
    const std::string* tn = tc->FindString("tool_name");
    tool_call->tool_name = tn ? *tn : "";
    const std::string* aj = tc->FindString("arguments_json");
    tool_call->arguments_json = aj ? *aj : "";
    tool_call->status = static_cast<maho_ai::mojom::ToolCallStatus>(
        tc->FindInt("status").value_or(0));
    event->tool_call = std::move(tool_call);
  }
  const base::DictValue* tr = dict.FindDict("tool_result");
  if (tr) {
    auto tool_result = maho_ai::mojom::ToolResultInfo::New();
    const std::string* cid = tr->FindString("call_id");
    tool_result->call_id = cid ? *cid : "";
    tool_result->success = tr->FindBool("success").value_or(false);
    const std::string* out = tr->FindString("output");
    tool_result->output = out ? *out : "";
    const std::string* em = tr->FindString("error_message");
    if (em) {
      tool_result->error_message = *em;
    }
    event->tool_result = std::move(tool_result);
  }
  const base::DictValue* ar = dict.FindDict("approval_request");
  if (ar) {
    auto approval = maho_ai::mojom::ApprovalRequestInfo::New();
    const std::string* aid = ar->FindString("approval_id");
    approval->approval_id = aid ? *aid : "";
    const std::string* desc = ar->FindString("description");
    approval->description = desc ? *desc : "";
    approval->approval_policy = static_cast<maho_ai::mojom::ApprovalPolicy>(
        ar->FindInt("approval_policy")
            .value_or(
                static_cast<int>(maho_ai::mojom::ApprovalPolicy::kPrompt)));
    approval->sensitivity = static_cast<maho_ai::mojom::ApprovalSensitivity>(
        ar->FindInt("sensitivity")
            .value_or(static_cast<int>(
                maho_ai::mojom::ApprovalSensitivity::kSensitive)));
    approval->state = static_cast<maho_ai::mojom::ApprovalState>(
        ar->FindInt("state").value_or(
            static_cast<int>(maho_ai::mojom::ApprovalState::kPending)));
    approval->page_derived_justification =
        ar->FindBool("page_derived_justification").value_or(false);
    const base::DictValue* rtc = ar->FindDict("related_tool_call");
    if (rtc) {
      auto related = maho_ai::mojom::ToolCallInfo::New();
      const std::string* rtn = rtc->FindString("tool_name");
      related->tool_name = rtn ? *rtn : "";
      related->arguments_json = "";
      related->call_id = "";
      related->status = maho_ai::mojom::ToolCallStatus::kPending;
      approval->related_tool_call = std::move(related);
    }
    event->approval_request = std::move(approval);
  }
  const base::DictValue* ares = dict.FindDict("approval_result");
  if (ares) {
    auto result = maho_ai::mojom::ApprovalResultInfo::New();
    const std::string* aid = ares->FindString("approval_id");
    result->approval_id = aid ? *aid : "";
    result->approved = ares->FindBool("approved").value_or(false);
    const std::string* reason = ares->FindString("reason");
    if (reason) {
      result->reason = *reason;
    }
    result->approval_policy = static_cast<maho_ai::mojom::ApprovalPolicy>(
        ares->FindInt("approval_policy")
            .value_or(
                static_cast<int>(maho_ai::mojom::ApprovalPolicy::kPrompt)));
    result->sensitivity = static_cast<maho_ai::mojom::ApprovalSensitivity>(
        ares->FindInt("sensitivity")
            .value_or(static_cast<int>(
                maho_ai::mojom::ApprovalSensitivity::kSensitive)));
    result->state = static_cast<maho_ai::mojom::ApprovalState>(
        ares->FindInt("state").value_or(static_cast<int>(
            result->approved ? maho_ai::mojom::ApprovalState::kApproved
                             : maho_ai::mojom::ApprovalState::kDenied)));
    result->decision = static_cast<maho_ai::mojom::ApprovalDecision>(
        ares->FindInt("decision")
            .value_or(static_cast<int>(
                result->approved ? maho_ai::mojom::ApprovalDecision::kAllowOnce
                                 : maho_ai::mojom::ApprovalDecision::kDeny)));
    result->page_derived_justification =
        ares->FindBool("page_derived_justification").value_or(false);
    event->approval_result = std::move(result);
  }
  const base::DictValue* ird = dict.FindDict("interaction_request");
  if (ird) {
    auto interaction = maho_ai::mojom::InteractionRequestInfo::New();
    const std::string* rid = ird->FindString("request_id");
    interaction->request_id = rid ? *rid : "";
    interaction->kind = static_cast<maho_ai::mojom::InteractionRequestKind>(
        ird->FindInt("kind").value_or(static_cast<int>(
            maho_ai::mojom::InteractionRequestKind::kQuestion)));
    const std::string* q = ird->FindString("question");
    interaction->question = q ? *q : "";
    const base::ListValue* persisted_options = ird->FindList("options");
    if (persisted_options) {
      for (const auto& item : *persisted_options) {
        if (!item.is_dict()) {
          continue;
        }
        const auto& option_dict = item.GetDict();
        const std::string* option_id = option_dict.FindString("id");
        const std::string* option_label = option_dict.FindString("label");
        if (!option_id || !option_label) {
          continue;
        }
        auto option = maho_ai::mojom::InteractionRequestOption::New();
        option->id = *option_id;
        option->label = *option_label;
        const std::string* description = option_dict.FindString("description");
        if (description) {
          option->description = *description;
        }
        interaction->options.push_back(std::move(option));
      }
    }
    const std::string* ref = ird->FindString("artifact_ref");
    if (ref) {
      interaction->artifact_ref = *ref;
    }
    const std::string* state = ird->FindString("state");
    if (state) {
      interaction->state = *state;
    }
    event->interaction_request = std::move(interaction);
  }
  const base::DictValue* bc = dict.FindDict("browser_context");
  if (bc) {
    auto ctx = maho_ai::mojom::BrowserContextPayload::New();
    ctx->status = static_cast<maho_ai::mojom::BrowserContextStatus>(
        bc->FindInt("status").value_or(static_cast<int>(
            maho_ai::mojom::BrowserContextStatus::kNotRequested)));
    const std::string* bc_url = bc->FindString("url");
    ctx->url = bc_url ? *bc_url : "";
    const std::string* bc_title = bc->FindString("title");
    ctx->title = bc_title ? *bc_title : "";
    const std::string* bc_snippet = bc->FindString("content_snippet");
    ctx->content_snippet = bc_snippet ? *bc_snippet : "";
    ctx->content_length =
        static_cast<uint32_t>(bc->FindInt("content_length").value_or(0));
    const std::string* bc_label = bc->FindString("label");
    ctx->label = bc_label ? *bc_label : "";
    const base::ListValue* bc_warns = bc->FindList("warnings");
    if (bc_warns) {
      for (const auto& w : *bc_warns) {
        if (w.is_string()) {
          ctx->warnings.push_back(w.GetString());
        }
      }
    }
    event->browser_context = std::move(ctx);
  }
  return event;
}

maho_ai::mojom::RuntimeEventKind InternalToMojoEventKind(
    MahoAiRuntimeEventType type) {
  switch (type) {
    case MahoAiRuntimeEventType::kAssistantToken:
      return maho_ai::mojom::RuntimeEventKind::kAssistantToken;
    case MahoAiRuntimeEventType::kAssistantThinking:
      return maho_ai::mojom::RuntimeEventKind::kAssistantThinking;
    case MahoAiRuntimeEventType::kTurnComplete:
      return maho_ai::mojom::RuntimeEventKind::kTurnComplete;
    case MahoAiRuntimeEventType::kError:
      return maho_ai::mojom::RuntimeEventKind::kError;
    case MahoAiRuntimeEventType::kToolRequest:
      return maho_ai::mojom::RuntimeEventKind::kToolRequest;
    case MahoAiRuntimeEventType::kToolResult:
      return maho_ai::mojom::RuntimeEventKind::kToolResult;
    case MahoAiRuntimeEventType::kApprovalRequest:
      return maho_ai::mojom::RuntimeEventKind::kApprovalRequest;
    case MahoAiRuntimeEventType::kApprovalResult:
      return maho_ai::mojom::RuntimeEventKind::kApprovalResult;
    case MahoAiRuntimeEventType::kConnectionStateChanged:
      return maho_ai::mojom::RuntimeEventKind::kConnectionStateChanged;
    case MahoAiRuntimeEventType::kBrowserContextInjected:
      return maho_ai::mojom::RuntimeEventKind::kBrowserContextInjected;
    case MahoAiRuntimeEventType::kToolAvailabilityChanged:
      return maho_ai::mojom::RuntimeEventKind::kToolAvailabilityChanged;
    case MahoAiRuntimeEventType::kArtifactCreated:
      return maho_ai::mojom::RuntimeEventKind::kArtifactCreated;
    case MahoAiRuntimeEventType::kInteractionRequest:
      return maho_ai::mojom::RuntimeEventKind::kInteractionRequest;
    case MahoAiRuntimeEventType::kModelResolved:
      return maho_ai::mojom::RuntimeEventKind::kSessionStatus;
    default:
      return maho_ai::mojom::RuntimeEventKind::kError;
  }
}

// Maps a kInteractionRequest runtime event payload (the FFI kind-8 JSON passed
// through by the unified adapter) into the typed Mojo InteractionRequestInfo.
// Tolerates all three wire shapes: the live FFI payload
// {request_id, kind, args}, the kernel serde InteractionRequest
// {id, kind: {kind, question|effect_description, options}, state}, and the
// flat journal shape {id, question|effect_description, options}.
maho_ai::mojom::InteractionRequestInfoPtr InteractionRequestInfoFromPayload(
    const base::DictValue& payload) {
  auto info = maho_ai::mojom::InteractionRequestInfo::New();

  const std::string* request_id = payload.FindString("request_id");
  if (!request_id) {
    request_id = payload.FindString("id");
  }
  if (!request_id) {
    request_id = payload.FindString("interaction_id");
  }
  info->request_id = request_id ? *request_id : "";

  const base::DictValue* kind_dict = payload.FindDict("kind");
  bool confirmation = false;
  std::string question;
  const base::ListValue* options = payload.FindList("options");
  if (kind_dict) {
    const std::string* tag = kind_dict->FindString("kind");
    confirmation = tag && *tag == "confirmation";
    const std::string* nested_question = kind_dict->FindString("question");
    if (nested_question) {
      question = *nested_question;
    } else {
      const std::string* nested_effect =
          kind_dict->FindString("effect_description");
      if (nested_effect) {
        question = *nested_effect;
        confirmation = true;
      }
    }
    if (!options) {
      options = kind_dict->FindList("options");
    }
  } else {
    const std::string* kind_str = payload.FindString("kind");
    confirmation = kind_str && *kind_str == "confirmation";
    const std::string* effect = payload.FindString("effect_description");
    if (effect) {
      question = *effect;
      confirmation = true;
    } else {
      const std::string* top_question = payload.FindString("question");
      if (top_question) {
        question = *top_question;
      }
    }
  }

  // Live FFI kind-8 payloads carry the prompt fields inside a JSON-encoded
  // `args` string; parse them out when the top level did not provide them.
  const std::string* args = payload.FindString("args");
  std::optional<std::string> artifact_ref;
  if (args && !args->empty()) {
    std::optional<base::DictValue> args_dict =
        base::JSONReader::ReadDict(*args, base::JSON_PARSE_RFC);
    if (args_dict) {
      if (question.empty()) {
        const std::string* args_question = args_dict->FindString("question");
        if (args_question) {
          question = *args_question;
        } else {
          const std::string* args_effect =
              args_dict->FindString("effect_description");
          if (args_effect) {
            question = *args_effect;
            confirmation = true;
          }
        }
      }
      if (!options) {
        options = args_dict->FindList("options");
      }
      const std::string* args_ref = args_dict->FindString("artifact_ref");
      if (!args_ref) {
        args_ref = args_dict->FindString("artifact_id");
      }
      if (args_ref) {
        artifact_ref = *args_ref;
      }
    }
  }
  if (!artifact_ref) {
    const std::string* top_ref = payload.FindString("artifact_ref");
    if (!top_ref) {
      top_ref = payload.FindString("artifact_id");
    }
    if (top_ref) {
      artifact_ref = *top_ref;
    }
  }
  const std::string* state = payload.FindString("state");

  info->kind = confirmation
                   ? maho_ai::mojom::InteractionRequestKind::kConfirmation
                   : maho_ai::mojom::InteractionRequestKind::kQuestion;
  info->question = maho::credential_redaction::RedactCredentialText(question);
  if (options) {
    for (const auto& item : *options) {
      if (!item.is_dict()) {
        continue;
      }
      const auto& option_dict = item.GetDict();
      const std::string* option_id = option_dict.FindString("id");
      const std::string* option_label = option_dict.FindString("label");
      if (!option_id || !option_label) {
        continue;
      }
      auto option = maho_ai::mojom::InteractionRequestOption::New();
      option->id = *option_id;
      // Same sanitizer as the question: option text crosses the WebUI and
      // persists, so agent-echoed secrets must not survive verbatim.
      option->label = maho::credential_redaction::RedactCredentialText(
          *option_label);
      const std::string* description = option_dict.FindString("description");
      if (description) {
        option->description =
            maho::credential_redaction::RedactCredentialText(*description);
      }
      info->options.push_back(std::move(option));
    }
  }
  info->artifact_ref = artifact_ref;
  if (state) {
    info->state = *state;
  }
  return info;
}

}  // namespace

class MahoAIEventLogPersistence : public base::SupportsUserData::Data,
                                public ProfileObserver {
 public:
  using Snapshot = std::vector<maho_ai::mojom::RuntimeEventPtr>;
  using ReplyGate = base::RepeatingCallback<void(base::OnceClosure)>;

  static base::WeakPtr<MahoAIEventLogPersistence> Get(Profile* profile,
                                                     PrefService* prefs) {
    static const char key = 0;
    auto* owner = static_cast<MahoAIEventLogPersistence*>(
        profile->GetUserData(&key));
    if (!owner) {
      auto data = std::make_unique<MahoAIEventLogPersistence>(profile, prefs);
      owner = data.get();
      profile->SetUserData(&key, std::move(data));
    }
    return owner->weak_factory_.GetWeakPtr();
  }

  MahoAIEventLogPersistence(Profile* profile, PrefService* prefs)
      : prefs_(prefs),
        worker_(base::ThreadPool::CreateSequencedTaskRunner(
            {base::MayBlock(), base::TaskPriority::BEST_EFFORT})) {
    observation_.Observe(profile);
    pref_changes_.Init(prefs);
    pref_changes_.Add(
        ai::kSessionPersistenceEnabled,
        base::BindRepeating(
            &MahoAIEventLogPersistence::OnPersistenceSettingChanged,
            base::Unretained(this)));
  }

  bool IsCleared(const std::string& session_id) const {
    return cleared_sessions_.contains(session_id);
  }

  std::shared_ptr<const Snapshot> Pending(const std::string& session_id) const {
    auto it = pending_.find(session_id);
    return it == pending_.end() ? nullptr : it->second.snapshot;
  }

  void Persist(const std::string& session_id,
               const Snapshot& log,
               ReplyGate gate,
               base::OnceCallback<void(bool)> completed = {}) {
    if (!prefs_ || !prefs_->GetBoolean(ai::kSessionPersistenceEnabled) ||
        IsCleared(session_id)) {
      if (completed) {
        std::move(completed).Run(false);
      }
      return;
    }
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->reserve(log.size());
    for (const auto& event : log) {
      snapshot->push_back(event->Clone());
    }
    auto& pending = pending_[session_id];
    pending.snapshot = std::move(snapshot);
    pending.gate = std::move(gate);
    if (completed) {
      pending.receipts.push_back(std::move(completed));
    }
    if (!running_.contains(session_id)) {
      Start(session_id);
    }
  }

  void Clear(const std::string& session_id) {
    cleared_sessions_.insert(session_id);
    auto pending = pending_.extract(session_id);
    running_.erase(session_id);
    if (prefs_) {
      ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
      update->Remove(session_id);
    }
    if (!pending.empty()) {
      for (auto& receipt : pending.mapped().receipts) {
        std::move(receipt).Run(false);
      }
    }
  }

  void DetachReplyGate(const std::string& session_id) {
    auto pending = pending_.find(session_id);
    if (pending != pending_.end()) {
      pending->second.gate.Reset();
    }
  }

  void RetainSessions(const std::vector<std::string>& kept_ids) {
    const std::set<std::string> kept(kept_ids.begin(), kept_ids.end());
    std::vector<std::string> removed;
    for (const auto& [session_id, write] : pending_) {
      if (!kept.contains(session_id)) {
        removed.push_back(session_id);
      }
    }
    for (const auto& session_id : removed) {
      Clear(session_id);
    }
  }

  void OnProfileWillBeDestroyed(Profile* profile) override {
    observation_.Reset();
    pref_changes_.RemoveAll();
    weak_factory_.InvalidateWeakPtrs();
    prefs_ = nullptr;
    CancelPending();
  }

 private:
  struct PendingWrite {
    std::shared_ptr<const Snapshot> snapshot;
    ReplyGate gate;
    std::vector<base::OnceCallback<void(bool)>> receipts;
  };

  void CancelPending() {
    auto pending = std::move(pending_);
    pending_.clear();
    running_.clear();
    for (auto& [session_id, write] : pending) {
      for (auto& receipt : write.receipts) {
        std::move(receipt).Run(false);
      }
    }
  }

  void OnPersistenceSettingChanged() {
    if (!prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
      CancelPending();
    }
  }

  void Start(const std::string& session_id) {
    const auto& pending = pending_.at(session_id);
    auto snapshot = pending.snapshot;
    running_.insert_or_assign(session_id, snapshot);
    const bool posted = worker_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(
            [](std::shared_ptr<const Snapshot> snapshot) {
              base::ListValue list;
              for (const auto& event : *snapshot) {
                list.Append(maho::ai::SerializeRuntimeEventForPersistence(*event));
              }
              return SerializeValueToJson(base::Value(std::move(list)));
            },
            snapshot),
        base::BindOnce(
            [](ReplyGate gate, base::OnceCallback<void(std::string)> commit,
               std::string json) {
              auto reply = base::BindOnce(std::move(commit), std::move(json));
              if (gate) {
                gate.Run(std::move(reply));
              } else {
                std::move(reply).Run();
              }
            },
            pending.gate,
            base::BindOnce(&MahoAIEventLogPersistence::Commit,
                           weak_factory_.GetWeakPtr(), session_id, snapshot)));
    if (!posted) {
      auto rejected = pending_.extract(session_id);
      running_.erase(session_id);
      LOG(ERROR) << "AI event persistence worker rejected admitted write";
      for (auto& receipt : rejected.mapped().receipts) {
        std::move(receipt).Run(false);
      }
    }
  }

  void Commit(const std::string& session_id,
              std::shared_ptr<const Snapshot> snapshot,
              std::string json) {
    auto running = running_.find(session_id);
    if (running == running_.end() || running->second != snapshot) {
      return;
    }
    running_.erase(running);
    auto pending = pending_.find(session_id);
    if (pending == pending_.end()) {
      return;
    }
    if (pending->second.snapshot != snapshot) {
      Start(session_id);
      return;
    }
    auto receipts = std::move(pending->second.receipts);
    pending_.erase(pending);
    bool committed = false;
    if (prefs_ && prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
      ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
      update->Set(session_id, std::move(json));
      committed = true;
    }
    for (auto& receipt : receipts) {
      std::move(receipt).Run(committed);
    }
  }

  raw_ptr<PrefService> prefs_;
  scoped_refptr<base::SequencedTaskRunner> worker_;
  std::map<std::string, PendingWrite> pending_;
  std::map<std::string, std::shared_ptr<const Snapshot>> running_;
  std::set<std::string> cleared_sessions_;
  PrefChangeRegistrar pref_changes_;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
  base::WeakPtrFactory<MahoAIEventLogPersistence> weak_factory_{this};
};

MahoAIPageHandler::AttachmentSlot::AttachmentSlot() = default;
MahoAIPageHandler::AttachmentSlot::AttachmentSlot(AttachmentSlot&&) noexcept =
    default;
MahoAIPageHandler::AttachmentSlot& MahoAIPageHandler::AttachmentSlot::operator=(
    AttachmentSlot&&) noexcept = default;
MahoAIPageHandler::AttachmentSlot::~AttachmentSlot() = default;

MahoAIPageHandler::PendingAttachmentBatch::PendingAttachmentBatch() = default;
MahoAIPageHandler::PendingAttachmentBatch::~PendingAttachmentBatch() = default;

MahoAIPageHandler::MahoAIPageHandler(
    mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_ai::mojom::Page> page,
    std::unique_ptr<MahoPrivateContextToken> token,
    Browser* browser,
    content::WebContents* host_web_contents,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      token_(std::move(token)),
      host_web_contents_(host_web_contents
                             ? host_web_contents->GetWeakPtr()
                             : base::WeakPtr<content::WebContents>()),
      browser_(browser),
      prefs_(prefs),
      url_loader_factory_(url_loader_factory),
      pending_surface_(
          std::make_unique<maho::ai::MahoAiPendingSurface>(prefs)) {
  g_active_page_handlers->insert(this);
  DCHECK(prefs_);

  if (!IsAiAllowed()) {
    DenyAndResetConnection();
    return;
  }

  if (browser_) {
    event_log_persistence_ =
        MahoAIEventLogPersistence::Get(browser_->GetProfile(), prefs_);
  }

  pending_surface_->SetConsumer(base::BindRepeating(
      [](base::WeakPtr<MahoAIPageHandler> handler,
         maho_ai::mojom::SurfaceRequestPtr request) {
        if (handler && handler->page_) {
          handler->page_->OnSurfaceRequested(std::move(request));
        }
      },
      weak_factory_.GetWeakPtr()));
  routine_handler_ = std::make_unique<MahoRoutinesPageHandler>(
      routine_handler_remote_.BindNewPipeAndPassReceiver(),
      routine_page_receiver_.BindNewPipeAndPassRemote(), browser,
      host_web_contents);

  auto gate = base::BindRepeating(&MahoAIPageHandler::RevalidateAiThunk,
                                  weak_factory_.GetWeakPtr());
  auto browser_resolver = base::BindRepeating(
      &MahoAIPageHandler::ResolveBoundBrowserThunk, weak_factory_.GetWeakPtr());
  runtime_router_ = std::make_unique<MahoAiRuntimeRouter>(
      prefs, url_loader_factory, gate, browser_resolver);
  page_context_extractor_ =
      std::make_unique<MahoAiPageContextExtractor>(browser, gate);

  if (prefs_->GetString(maho::ai_prefs::kAiViewMode) == "floating" &&
      browser_ && !(browser_->GetType() == BrowserWindowInterface::TYPE_POPUP)) {
    prefs_->SetString(maho::ai_prefs::kAiViewMode, "sidebar");
  }
  LoadPersistedSessions();

  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoAIPageHandler::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()));
  }

  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(
      maho::ai_prefs::kPermissionTier,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeConfigPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kFinalConfirm,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeConfigPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kProactiveMode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeConfigPrefsChanged,
                          base::Unretained(this)));
  // Mail read consent rides the same runtime-config push so toggling it in
  // chrome://maho-settings live-updates the panel's permission sheet.
  pref_change_registrar_.Add(
      maho::ai_prefs::kMailReadAllowed,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeConfigPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kProvider,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kModel,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kReasoningEffort,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kBaseUrl,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kApiKey,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kByokOpenAIEncryptedB64,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kByokAnthropicEncryptedB64,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      maho::ai_prefs::kModelCache,
      base::BindRepeating(&MahoAIPageHandler::OnAISettingsPrefsChanged,
                          base::Unretained(this)));

  if (token_ && token_->context_class() == MahoPrivateContextClass::kRegular &&
      browser_) {
    maho::MahoAiIngressCoordinator::ConsumerType consumer_type =
        maho::MahoAiIngressCoordinator::ConsumerType::kSidebar;
    Browser* target_browser = browser_;
    if ((browser_->GetType() == BrowserWindowInterface::TYPE_POPUP)) {
      consumer_type = maho::MahoAiIngressCoordinator::ConsumerType::kFloating;
      if (auto* opener_interface =
              maho::MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(
                  browser_)) {
        target_browser = static_cast<Browser*>(opener_interface);
      }
    }
    if (target_browser) {
      auto* coordinator =
          maho::MahoAiIngressCoordinator::GetOrCreateForBrowser(target_browser);
      ask_maho_coordinator_ = coordinator->GetWeakPtr();
      if (auto handoff = coordinator->TakeSessionHandoff()) {
        RestoreViewModeHandoff(std::move(*handoff));
      }
      ask_maho_registration_ = coordinator->RegisterConsumer(
          consumer_type,
          base::BindRepeating(&MahoAIPageHandler::OnAskMahoDispatch,
                              weak_factory_.GetWeakPtr()));
    }
  }
}

bool MahoAIPageHandler::IsAiAllowed() {
  return token_ && token_->Revalidate(MahoPrivateCapability::kAI);
}

void MahoAIPageHandler::DenyAndResetConnection() {
  ask_maho_registration_.Reset();
  page_.reset();
  receiver_.reset();
  if (!active_ingress_request_id_.empty() || active_ingress_delivery_id_ != 0) {
    InterruptActiveIngressRequest();
  }
}

// static
bool MahoAIPageHandler::RevalidateAiThunk(
    base::WeakPtr<MahoAIPageHandler> self) {
  return self && self->IsAiAllowed();
}

// static
Browser* MahoAIPageHandler::ResolveBoundBrowserThunk(
    base::WeakPtr<MahoAIPageHandler> self) {
  if (!self || !self->IsAiAllowed()) {
    return nullptr;
  }
  return self->browser_.get();
}

void MahoAIPageHandler::PrepareForViewModeHandoff() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_session_id_.empty() || !ask_maho_coordinator_) {
    return;
  }

  PersistEventLog(active_session_id_, true);
  PersistSessionList(true);

  std::vector<maho_ai::mojom::RuntimeEventPtr> replay_events;
  replay_events.reserve(session_event_log_.size());
  for (const auto& event : session_event_log_) {
    replay_events.push_back(event->Clone());
  }

  ask_maho_coordinator_->StoreSessionHandoff(
      {MakeActiveSessionInfo(), std::move(replay_events),
       std::move(active_ingress_request_id_), active_ingress_delivery_id_});

  active_session_id_.clear();
  session_title_.clear();
  runtime_session_id_.clear();
  last_runtime_state_.clear();
  session_status_ = maho_ai::mojom::SessionStatus::kCreated;
  session_created_at_ = 0;
  session_updated_at_ = 0;
  event_sequence_ = 0;
  tool_call_count_ = 0;
  session_event_log_.clear();
  replay_budget_.Reset();
  active_ingress_request_id_.clear();
  active_ingress_delivery_id_ = 0;
}

MahoAIPageHandler::Correlation::Correlation() = default;

MahoAIPageHandler::Correlation::Correlation(
    std::string session_id,
    std::optional<std::string> request_id)
    : session_id(std::move(session_id)), request_id(std::move(request_id)) {}

MahoAIPageHandler::Correlation::Correlation(const Correlation&) = default;
MahoAIPageHandler::Correlation::Correlation(Correlation&&) = default;
MahoAIPageHandler::Correlation& MahoAIPageHandler::Correlation::operator=(
    const Correlation&) = default;
MahoAIPageHandler::Correlation& MahoAIPageHandler::Correlation::operator=(
    Correlation&&) = default;
MahoAIPageHandler::Correlation::~Correlation() = default;

void MahoAIPageHandler::RestoreViewModeHandoff(
    maho::MahoAiIngressCoordinator::SessionHandoff handoff) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!handoff.session || handoff.session->session_id.empty()) {
    return;
  }

  auto session = std::move(handoff.session);
  active_session_id_ = session->session_id;
  session_title_ = session->title.value_or("");
  runtime_session_id_ = session->runtime_session_id.value_or("");
  last_runtime_state_ = session->last_runtime_state.value_or("");
  session_status_ = session->status;
  session_created_at_ = session->created_at;
  session_updated_at_ = session->updated_at.value_or(session_created_at_);
  tool_call_count_ = session->tool_call_count;
  session_event_log_ = std::move(handoff.replay_events);
  maho::ai::NormalizeReplayWindow(session_event_log_, replay_budget_);
  event_sequence_ = 0;
  for (const auto& event : session_event_log_) {
    if (event->sequence > event_sequence_) {
      event_sequence_ = event->sequence;
    }
  }
  active_ingress_request_id_ = std::move(handoff.ingress_request_id);
  active_ingress_delivery_id_ = handoff.ingress_delivery_id;

  std::erase_if(persisted_sessions_, [this](const auto& persisted) {
    return persisted->session_id == active_session_id_;
  });
  PersistSessionList(true);

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  if (adapter) {
    adapter->StartSession(active_session_id_);
  }
}

MahoAIPageHandler::~MahoAIPageHandler() {
  g_active_page_handlers->erase(this);
  if (control_activity_service_) {
    control_activity_service_->RemoveObserver(this);
  }
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ask_maho_registration_.Reset();
  page_.reset();
  if (!active_ingress_request_id_.empty() || active_ingress_delivery_id_ != 0) {
    InterruptActiveIngressRequest();
  } else if (!active_session_id_.empty()) {
    if (session_status_ == maho_ai::mojom::SessionStatus::kActive ||
        session_status_ == maho_ai::mojom::SessionStatus::kIdle ||
        session_status_ == maho_ai::mojom::SessionStatus::kPausedForApproval) {
      SetSessionStatus(maho_ai::mojom::SessionStatus::kCompleted);
    }
    PersistEventLog(active_session_id_, /*synchronous=*/true);
    PersistSessionList(/*synchronous=*/true);
  }
  if (event_log_persistence_) {
    event_log_persistence_->DetachReplyGate(active_session_id_);
    for (const auto& session : persisted_sessions_) {
      event_log_persistence_->DetachReplyGate(session->session_id);
    }
  }
}

bool MahoAIPageHandler::CancelActiveControllerSession(
    std::string_view session_id) {
  for (MahoAIPageHandler* handler : *g_active_page_handlers) {
    if (handler->active_session_id_ == session_id) {
      handler->CancelTurn(std::string(session_id));
      return true;
    }
  }
  return false;
}

void MahoAIPageHandler::SettleIngressRequest(const std::string& request_id,
                                             bool post_terminal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (request_id.empty() || request_id != active_ingress_request_id_ ||
      active_ingress_delivery_id_ == 0) {
    return;
  }

  const std::string settled_request_id = request_id;
  const uint64_t settled_delivery_id = active_ingress_delivery_id_;
  base::WeakPtr<maho::MahoAiIngressCoordinator> coordinator =
      ask_maho_coordinator_;
  if (post_terminal) {
    active_ingress_request_id_.clear();
    active_ingress_delivery_id_ = 0;
    if (coordinator) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&maho::MahoAiIngressCoordinator::NotifyTerminal,
                         std::move(coordinator), settled_delivery_id));
    }
    return;
  }

  if (coordinator) {
    coordinator->NotifyTerminal(settled_delivery_id);
  }
  if (active_ingress_request_id_ == settled_request_id &&
      active_ingress_delivery_id_ == settled_delivery_id) {
    active_ingress_request_id_.clear();
    active_ingress_delivery_id_ = 0;
  }
}

void MahoAIPageHandler::InterruptActiveIngressRequest(bool post_terminal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_ingress_request_id_.empty() || active_ingress_delivery_id_ == 0) {
    active_ingress_request_id_.clear();
    active_ingress_delivery_id_ = 0;
    return;
  }

  const std::string request_id = active_ingress_request_id_;
  if (!active_session_id_.empty() &&
      session_status_ != maho_ai::mojom::SessionStatus::kCancelled &&
      session_status_ != maho_ai::mojom::SessionStatus::kCompleted &&
      session_status_ != maho_ai::mojom::SessionStatus::kError) {
    SetSessionStatus(maho_ai::mojom::SessionStatus::kCancelled);
  }
  PersistEventLog(active_session_id_, /*synchronous=*/true);
  PersistSessionList(/*synchronous=*/true);
  SettleIngressRequest(request_id, post_terminal);
}

// static
void MahoAIPageHandler::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  maho::ai::RegisterProfilePrefs(registry);
}

void MahoAIPageHandler::ConsumePendingSurface(
    uint64_t last_seen_generation,
    ConsumePendingSurfaceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed()) {
    DenyAndResetConnection();
    return;
  }
  std::move(callback).Run(pending_surface_->Consume(last_seen_generation));
}

void MahoAIPageHandler::ListAllRoutines(ListAllRoutinesCallback callback) {
  routine_handler_remote_->ListAllRoutines(std::move(callback));
}

void MahoAIPageHandler::CreateRoutine(
    const std::string& name,
    const std::string& prompt,
    const std::optional<std::string>& schedule,
    const std::optional<std::string>& trigger,
    CreateRoutineCallback callback) {
  routine_handler_remote_->CreateRoutine(name, prompt, schedule, trigger,
                                         std::move(callback));
}

void MahoAIPageHandler::StartRoutine(const std::string& id,
                                     StartRoutineCallback callback) {
  routine_handler_remote_->StartRoutine(id, std::move(callback));
}

void MahoAIPageHandler::GetRoutineRunStatuses(
    GetRoutineRunStatusesCallback callback) {
  routine_handler_remote_->GetRoutineRunStatuses(std::move(callback));
}

void MahoAIPageHandler::GetRoutineUserTier(
    GetRoutineUserTierCallback callback) {
  routine_handler_remote_->GetUserTier(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(callback), 0));
}

void MahoAIPageHandler::RespondToRoutineApproval(
    const std::string& run_id,
    const std::string& approval_id,
    bool approved,
    RespondToRoutineApprovalCallback callback) {
  routine_handler_remote_->RespondToRoutineApproval(
      run_id, approval_id, approved, std::move(callback));
}

void MahoAIPageHandler::ListRunHistory(
    const std::optional<std::string>& routine_id,
    uint32_t limit,
    ListRunHistoryCallback callback) {
  routine_handler_remote_->ListRunHistory(routine_id, limit,
                                          std::move(callback));
}

void MahoAIPageHandler::OnRoutineComplete(
    maho_routines::mojom::RoutineRunResultPtr result) {}

void MahoAIPageHandler::OnRoutineError(const std::string& id,
                                       const std::string& error) {}

void MahoAIPageHandler::OnRoutineRunStatusChanged(
    maho_routines::mojom::RoutineRunStatusPtr status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_) {
    page_->OnRoutineRunStatusChanged(std::move(status));
  }
}

void MahoAIPageHandler::OnRuntimeEvent(MahoAiRuntimeEvent event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  OnRuntimeEventForSession(
      active_session_id_,
      active_ingress_request_id_.empty()
          ? std::nullopt
          : std::optional<std::string>(active_ingress_request_id_),
      std::move(event));
}

void MahoAIPageHandler::OnRuntimeEventForSession(
    const std::string& session_id,
    std::optional<std::string> request_id,
    MahoAiRuntimeEvent event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id.empty()) {
    return;
  }

  if (event.type == MahoAiRuntimeEventType::kModelResolved) {
    // Model resolution is internal runtime adapter metadata, not a WebUI event.
    return;
  }

  if (session_id == active_session_id_) {
    session_updated_at_ = base::Time::Now().InSecondsFSinceUnixEpoch();

    // Capture the runtime session ID if the adapter now has one.
    MahoAiRuntimeAdapter* rid_adapter =
        runtime_adapter_for_testing_
            ? runtime_adapter_for_testing_.get()
            : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
    std::string rid =
        rid_adapter ? rid_adapter->GetRuntimeSessionId() : std::string();
    if (!rid.empty() && runtime_session_id_ != rid) {
      runtime_session_id_ = rid;
      PersistSessionList();
    }
  }

  auto mojo_event = ToMojoEvent(event);
  mojo_event->session_id = session_id;
  if (request_id.has_value()) {
    mojo_event->request_id = *request_id;
  }

  if (session_id == active_session_id_) {
    const auto admission = maho::ai::AppendBoundedReplayEvent(
        session_event_log_, replay_budget_, mojo_event);
    if (admission == maho::ai::ReplayAdmission::kIgnored) {
      return;
    }
    if (admission == maho::ai::ReplayAdmission::kExhausted) {
      event.type = MahoAiRuntimeEventType::kError;
      MahoAiRuntimeAdapter* adapter =
          runtime_adapter_for_testing_
              ? runtime_adapter_for_testing_.get()
              : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
      if (adapter) {
        adapter->CancelCurrentTurn();
      }
    }
    if (page_) {
      page_->OnRuntimeEvent(mojo_event->Clone());
    }

    switch (event.type) {
      case MahoAiRuntimeEventType::kTurnComplete:
        SetSessionStatus(maho_ai::mojom::SessionStatus::kIdle);
        PersistEventLog(session_id);
        PersistSessionList();
        EmitSessionUpdated();
        if (request_id.has_value()) {
          SettleIngressRequest(*request_id);
        }
        break;

      case MahoAiRuntimeEventType::kError:
        SetSessionStatus(maho_ai::mojom::SessionStatus::kError);
        PersistEventLog(session_id);
        PersistSessionList();
        if (request_id.has_value()) {
          SettleIngressRequest(*request_id);
        }
        break;

      case MahoAiRuntimeEventType::kToolRequest:
        ++tool_call_count_;
        break;

      case MahoAiRuntimeEventType::kApprovalRequest:
        SetSessionStatus(maho_ai::mojom::SessionStatus::kPausedForApproval);
        break;

      case MahoAiRuntimeEventType::kApprovalResult:
        SetSessionStatus(maho_ai::mojom::SessionStatus::kActive);
        break;

      case MahoAiRuntimeEventType::kConnectionStateChanged:
        if (!event.text.empty()) {
          last_runtime_state_ = event.text;
        }
        MaybeEmitConnectionStateChanged();
        break;

      default:
        break;
    }
  } else {
    // Late event for non-active session
    if (!event_log_persistence_ ||
        event_log_persistence_->IsCleared(session_id)) {
      return;
    }
    if (page_) {
      page_->OnRuntimeEvent(mojo_event->Clone());
    }
    bool unavailable = false;
    auto historical_log = LoadPersistedEventLog(session_id, &unavailable);
    if (unavailable) {
      return;
    }
    AppendReplayEvent(historical_log, mojo_event->Clone());
    PersistEventLogForSession(session_id, historical_log, /*synchronous=*/true);

    bool updated_metadata = false;
    for (auto& s : persisted_sessions_) {
      if (s->session_id == session_id) {
        if (event.type == MahoAiRuntimeEventType::kTurnComplete) {
          s->status = maho_ai::mojom::SessionStatus::kCompleted;
          s->updated_at = base::Time::Now().InSecondsFSinceUnixEpoch();
          updated_metadata = true;
        } else if (event.type == MahoAiRuntimeEventType::kError) {
          s->status = maho_ai::mojom::SessionStatus::kError;
          s->updated_at = base::Time::Now().InSecondsFSinceUnixEpoch();
          updated_metadata = true;
        }
      }
    }
    if (updated_metadata) {
      PersistSessionList();
    }
  }
}

maho_ai::mojom::RuntimeEventPtr MahoAIPageHandler::ToMojoEvent(
    const MahoAiRuntimeEvent& event) {
  auto mojo_event = maho_ai::mojom::RuntimeEvent::New();
  mojo_event->kind = InternalToMojoEventKind(event.type);
  mojo_event->sequence = ++event_sequence_;
  mojo_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  if (event.runtime_error_code.has_value()) {
    mojo_event->credential_error_code =
        CredentialErrorCodeFromRuntime(*event.runtime_error_code);
    mojo_event->text = kSafeCredentialFailureText;
  } else {
    mojo_event->text =
        maho::credential_redaction::RedactCredentialText(event.text);
  }

  if (event.type == MahoAiRuntimeEventType::kArtifactCreated) {
    mojo_event->artifact = ArtifactInfoFromJson(event.text);
  }

  if (event.type == MahoAiRuntimeEventType::kInteractionRequest) {
    mojo_event->interaction_request =
        InteractionRequestInfoFromPayload(event.payload);
  }

  if (event.type == MahoAiRuntimeEventType::kToolRequest) {
    auto tool_call = maho_ai::mojom::ToolCallInfo::New();
    const std::string* call_id = event.payload.FindString("call_id");
    const std::string* tool_name = event.payload.FindString("tool_name");
    const std::string* args = event.payload.FindString("arguments_json");
    tool_call->call_id = call_id ? *call_id : "";
    tool_call->tool_name = tool_name ? *tool_name : "";
    tool_call->arguments_json =
        args ? maho::credential_redaction::RedactJsonOrText(*args) : "";
    tool_call->status = maho_ai::mojom::ToolCallStatus::kPending;
    mojo_event->tool_call = std::move(tool_call);
  }

  if (event.type == MahoAiRuntimeEventType::kToolResult) {
    auto tool_result = maho_ai::mojom::ToolResultInfo::New();
    const std::string* call_id = event.payload.FindString("call_id");
    const std::string* output = event.payload.FindString("output");
    tool_result->call_id = call_id ? *call_id : "";
    tool_result->success = event.payload.FindBool("success").value_or(false);
    tool_result->output =
        output ? maho::credential_redaction::RedactJsonOrText(*output) : "";
    const std::string* error = event.payload.FindString("error");
    if (error) {
      tool_result->error_message =
          maho::credential_redaction::RedactCredentialText(*error);
    }
    mojo_event->tool_result = std::move(tool_result);
  }

  if (event.type == MahoAiRuntimeEventType::kApprovalRequest) {
    auto approval = maho_ai::mojom::ApprovalRequestInfo::New();
    const std::string* aid = event.payload.FindString("approval_id");
    const std::string* desc = event.payload.FindString("description");
    approval->approval_id = aid ? *aid : "";
    approval->description =
        desc ? maho::credential_redaction::RedactCredentialText(*desc) : "";
    approval->approval_policy =
        ApprovalPolicyFromString(event.payload.FindString("approval_policy"));
    approval->sensitivity =
        ApprovalSensitivityFromString(event.payload.FindString("sensitivity"));
    approval->state =
        ApprovalStateFromString(event.payload.FindString("approval_state"),
                                maho_ai::mojom::ApprovalState::kPending);
    approval->page_derived_justification =
        event.payload.FindBool("page_derived_justification").value_or(false);

    const std::string* related_tool =
        event.payload.FindString("related_tool_name");
    if (related_tool && !related_tool->empty()) {
      auto tool_call = maho_ai::mojom::ToolCallInfo::New();
      tool_call->tool_name = *related_tool;
      tool_call->arguments_json = "";
      tool_call->call_id = "";
      tool_call->status = maho_ai::mojom::ToolCallStatus::kPending;
      approval->related_tool_call = std::move(tool_call);
    }

    const std::string* method = event.payload.FindString("method");
    if (method && !method->empty()) {
      approval->description = approval->description + " [" + *method + "]";
    }
    const std::string* risk = event.payload.FindString("risk");
    if (risk && !risk->empty()) {
      approval->description = approval->description + " (risk: " + *risk + ")";
    }

    mojo_event->approval_request = std::move(approval);
  } else if (event.type == MahoAiRuntimeEventType::kApprovalResult) {
    auto result = maho_ai::mojom::ApprovalResultInfo::New();
    const std::string* aid = event.payload.FindString("approval_id");
    result->approval_id = aid ? *aid : "";
    result->approved = event.payload.FindBool("approved").value_or(false);
    const std::string* reason = event.payload.FindString("reason");
    if (reason) {
      result->reason =
          maho::credential_redaction::RedactCredentialText(*reason);
    }
    result->approval_policy =
        ApprovalPolicyFromString(event.payload.FindString("approval_policy"));
    result->sensitivity =
        ApprovalSensitivityFromString(event.payload.FindString("sensitivity"));
    result->state = ApprovalStateFromString(
        event.payload.FindString("approval_state"),
        result->approved ? maho_ai::mojom::ApprovalState::kApproved
                         : maho_ai::mojom::ApprovalState::kDenied);
    const std::string* decision_text = event.payload.FindString("decision");
    if (!decision_text) {
      decision_text = event.payload.FindString("approval_decision");
    }
    result->decision = ApprovalDecisionFromString(
        decision_text, result->approved
                           ? maho_ai::mojom::ApprovalDecision::kAllowOnce
                           : maho_ai::mojom::ApprovalDecision::kDeny);
    result->page_derived_justification =
        event.payload.FindBool("page_derived_justification").value_or(false);
    mojo_event->approval_result = std::move(result);
  }

  if (event.type == MahoAiRuntimeEventType::kBrowserContextInjected) {
    auto ctx = maho_ai::mojom::BrowserContextPayload::New();
    const std::string* status_str = event.payload.FindString("status");
    std::string s = status_str ? *status_str : "not_requested";
    if (s == "injected") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kInjected;
    } else if (s == "denied_internal_page") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kDeniedInternalPage;
    } else if (s == "no_active_tab") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kNoActiveTab;
    } else if (s == "cannot_access") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kCannotAccess;
    } else if (s == "extraction_failed") {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kExtractionFailed;
    } else {
      ctx->status = maho_ai::mojom::BrowserContextStatus::kNotRequested;
    }
    const std::string* url = event.payload.FindString("url");
    ctx->url = url ? maho::ai_security::RedactUrlForAi(*url) : "";
    const std::string* title = event.payload.FindString("title");
    ctx->title =
        title ? maho::credential_redaction::RedactCredentialText(*title) : "";
    const std::string* snippet = event.payload.FindString("content_snippet");
    ctx->content_snippet =
        snippet ? maho::credential_redaction::RedactCredentialText(*snippet)
                : "";
    ctx->content_length = static_cast<uint32_t>(
        event.payload.FindInt("content_length").value_or(0));
    const std::string* lbl = event.payload.FindString("label");
    ctx->label =
        lbl ? maho::credential_redaction::RedactCredentialText(*lbl) : "";
    const base::ListValue* warns = event.payload.FindList("warnings");
    if (warns) {
      for (const auto& w : *warns) {
        if (w.is_string()) {
          ctx->warnings.push_back(
              maho::credential_redaction::RedactCredentialText(w.GetString()));
        }
      }
    }
    mojo_event->browser_context = std::move(ctx);
  }

  return mojo_event;
}

maho_ai::mojom::SessionInfoPtr MahoAIPageHandler::MakeActiveSessionInfo()
    const {
  auto info = maho_ai::mojom::SessionInfo::New();
  info->session_id = active_session_id_;
  info->created_at = session_created_at_;
  info->adapter_name = runtime_router_->GetActiveAdapterName();
  info->is_active = !active_session_id_.empty();
  info->is_read_only = false;
  info->status = session_status_;
  info->event_count = static_cast<uint32_t>(session_event_log_.size());
  info->tool_call_count = tool_call_count_;
  if (!session_title_.empty()) {
    info->title = session_title_;
  }
  if (session_updated_at_ > 0) {
    info->updated_at = session_updated_at_;
  }
  if (!runtime_session_id_.empty()) {
    info->runtime_session_id = runtime_session_id_;
  }
  if (!last_runtime_state_.empty()) {
    info->last_runtime_state = last_runtime_state_;
  } else if (runtime_router_->IsAvailable()) {
    info->last_runtime_state = "connected";
  } else {
    info->last_runtime_state = "disconnected";
  }
  return info;
}

void MahoAIPageHandler::GetConnectionState(
    GetConnectionStateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    std::move(callback).Run(
        maho_ai::mojom::RuntimeConnectionState::kDisconnected, "disabled");
    DenyAndResetConnection();
    return;
  }

  auto state = ResolveConnectionState();

  std::move(callback).Run(state, runtime_router_->GetActiveAdapterName());
}

void MahoAIPageHandler::GetSessionList(GetSessionListCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::SessionInfoPtr> sessions;
  if (!active_session_id_.empty()) {
    sessions.push_back(MakeActiveSessionInfo());
  }
  for (const auto& s : persisted_sessions_) {
    if (s->session_id != active_session_id_) {
      sessions.push_back(s->Clone());
    }
  }
  std::move(callback).Run(std::move(sessions));
}

maho_ai::mojom::SessionInfoPtr MahoAIPageHandler::StartSessionInternal(
    const std::optional<std::string>& initial_prompt,
    maho_ai::mojom::InteractionMode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!active_session_id_.empty()) {
    // Finalize the outgoing session: persist events and snapshot metadata
    // into persisted_sessions_ so historical sessions retain truthful state.
    if (session_status_ == maho_ai::mojom::SessionStatus::kActive ||
        session_status_ == maho_ai::mojom::SessionStatus::kIdle ||
        session_status_ == maho_ai::mojom::SessionStatus::kPausedForApproval) {
      SetSessionStatus(maho_ai::mojom::SessionStatus::kCompleted);
    }
    session_updated_at_ = base::Time::Now().InSecondsFSinceUnixEpoch();
    PersistEventLog(active_session_id_);

    auto old_info = MakeActiveSessionInfo();
    old_info->is_active = false;
    old_info->is_read_only = true;
    // Insert at front so most recent historical session comes first.
    persisted_sessions_.insert(persisted_sessions_.begin(),
                               std::move(old_info));
    if (persisted_sessions_.size() > kMaxPersistedSessions) {
      persisted_sessions_.resize(kMaxPersistedSessions);
    }
  }

  active_session_id_ = GenerateSessionId();
  session_created_at_ = base::Time::Now().InSecondsFSinceUnixEpoch();
  session_updated_at_ = session_created_at_;
  event_sequence_ = 0;
  tool_call_count_ = 0;
  session_event_log_.clear();
  replay_budget_.Reset();
  session_title_.clear();
  runtime_session_id_.clear();
  last_runtime_state_.clear();
  session_status_ = maho_ai::mojom::SessionStatus::kActive;

  // Sync active session with runtime adapter
  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  if (adapter) {
    adapter->StartSession(active_session_id_);
  }

  if (initial_prompt.has_value() && !initial_prompt->empty()) {
    UpdateSessionTitle(*initial_prompt);
  }

  auto session_info = MakeActiveSessionInfo();
  PersistSessionList();

  auto status_event = maho_ai::mojom::RuntimeEvent::New();
  status_event->kind = maho_ai::mojom::RuntimeEventKind::kSessionStatus;
  status_event->sequence = ++event_sequence_;
  status_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  status_event->text = "started";
  status_event->session_id = active_session_id_;
  if (!active_ingress_request_id_.empty()) {
    status_event->request_id = active_ingress_request_id_;
  }
  maho::ai::AppendBoundedReplayEvent(session_event_log_, replay_budget_,
                                    status_event);
  if (page_) {
    page_->OnRuntimeEvent(std::move(status_event));
  }

  return session_info;
}

void MahoAIPageHandler::StartSession(
    const std::optional<std::string>& initial_prompt,
    maho_ai::mojom::InteractionMode mode,
    StartSessionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_ingress_request_id_.empty() && active_ingress_delivery_id_ != 0) {
    InterruptActiveIngressRequest(/*post_terminal=*/true);
  }
  auto info = StartSessionInternal(initial_prompt, mode);
  std::move(callback).Run(info->Clone());

  if (initial_prompt.has_value() && !initial_prompt->empty()) {
    SubmitPrompt(active_session_id_, *initial_prompt,
                 /*attach_browser_context=*/true, mode,
                 /*attachments=*/std::nullopt,
                 maho_ai::mojom::ChatIntent::kFreeform, base::DoNothing());
  }
}

void MahoAIPageHandler::ResumeSession(const std::string& session_id,
                                      ResumeSessionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id == active_session_id_ && !active_session_id_.empty()) {
    MahoAiRuntimeAdapter* adapter =
        runtime_adapter_for_testing_
            ? runtime_adapter_for_testing_.get()
            : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
    if (adapter) {
      adapter->StartSession(session_id);
    }

    auto session_info = MakeActiveSessionInfo();
    std::vector<maho_ai::mojom::RuntimeEventPtr> replay;
    for (const auto& evt : session_event_log_) {
      replay.push_back(evt->Clone());
    }
    std::move(callback).Run(std::move(session_info), std::move(replay));
    return;
  }

  for (const auto& s : persisted_sessions_) {
    if (s->session_id == session_id) {
      auto replay = LoadPersistedEventLog(session_id);
      auto info = s->Clone();
      // Historical sessions are read-only: no runtime connection exists,
      // so SubmitPrompt/CancelTurn/RespondToApproval will be rejected.
      // The frontend should present these as immutable history views.
      info->is_active = false;
      info->is_read_only = true;
      info->last_runtime_state = "disconnected";
      info->status = s->status;
      std::move(callback).Run(std::move(info), std::move(replay));
      return;
    }
  }

  std::move(callback).Run(nullptr,
                          std::vector<maho_ai::mojom::RuntimeEventPtr>());
}

void MahoAIPageHandler::GetOpenTabs(GetOpenTabsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::TabItemPtr> tabs;

  if (!IsAiAllowed()) {
    std::move(callback).Run(std::move(tabs));
    DenyAndResetConnection();
    return;
  }

  if (!browser_) {
    std::move(callback).Run(std::move(tabs));
    return;
  }

  if (!maho::ai_security::IsProfileEligible(browser_->GetProfile())) {
    std::move(callback).Run(std::move(tabs));
    return;
  }

  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (!tab_strip) {
    std::move(callback).Run(std::move(tabs));
    return;
  }

  const int count = tab_strip->count();
  const int active_index = tab_strip->active_index();
  tabs.reserve(static_cast<size_t>(count));

  for (int i = 0; i < count; ++i) {
    content::WebContents* wc = tab_strip->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    const GURL& committed_url = wc->GetLastCommittedURL();
    if (maho::ai_security::IsUrlBlocked(committed_url)) {
      continue;
    }

    auto item = maho_ai::mojom::TabItem::New();
    sessions::SessionTabHelper* helper =
        sessions::SessionTabHelper::FromWebContents(wc);
    item->tab_id = helper ? helper->session_id().id() : 0;
    item->url = maho::ai_security::RedactUrlForAi(committed_url);
    item->title = base::UTF16ToUTF8(wc->GetTitle());
    item->is_active = (i == active_index);
    item->index = i;
    tabs.push_back(std::move(item));
  }

  std::move(callback).Run(std::move(tabs));
}

void MahoAIPageHandler::SearchHistory(const std::string& query,
                                      uint32_t max_results,
                                      SearchHistoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::HistoryItemPtr> items;

  if (!IsAiAllowed()) {
    std::move(callback).Run(std::move(items));
    DenyAndResetConnection();
    return;
  }

  if (browser_ && !maho::ai_security::IsProfileEligible(browser_->GetProfile())) {
    std::move(callback).Run(std::move(items));
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::move(items));
    return;
  }

  // Clamp max_results to a reasonable upper bound to avoid returning huge
  // payloads to the frontend picker.
  const size_t kMaxHistoryResults = 50;
  size_t limit = (max_results == 0 || max_results > kMaxHistoryResults)
                     ? kMaxHistoryResults
                     : static_cast<size_t>(max_results);

  std::string json = maho::core::SearchHistory(core, query.c_str(), limit);
  if (json.empty()) {
    std::move(callback).Run(std::move(items));
    return;
  }

  // The bridge returns a JSON array of 3-tuples: [url, title, visited_at].
  // Example: [["https://example.com", "Example", 1716000000.0], ...]
  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    LOG(WARNING) << "MahoAIPageHandler::SearchHistory: failed to parse result";
    std::move(callback).Run(std::move(items));
    return;
  }

  for (const auto& entry : parsed->GetList()) {
    if (!entry.is_list()) {
      continue;
    }
    const base::ListValue& tuple = entry.GetList();
    if (tuple.size() < 3) {
      continue;
    }
    const std::string* url = tuple[0].GetIfString();
    const std::string* title = tuple[1].GetIfString();
    std::optional<double> visited_at = tuple[2].GetIfDouble();
    if (!url || url->empty()) {
      continue;
    }
    std::string redacted_url = maho::ai_security::RedactUrlForAi(*url);
    if (redacted_url.empty()) {
      continue;
    }

    auto item = maho_ai::mojom::HistoryItem::New();
    item->url = std::move(redacted_url);
    item->title = title ? *title : "";
    item->visited_at = visited_at.value_or(0.0);
    items.push_back(std::move(item));
  }

  std::move(callback).Run(std::move(items));
}

void MahoAIPageHandler::SearchBookmarks(const std::string& query,
                                        uint32_t max_results,
                                        SearchBookmarksCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::BookmarkItemPtr> items;

  if (!IsAiAllowed()) {
    std::move(callback).Run(std::move(items));
    DenyAndResetConnection();
    return;
  }

  if (browser_ && !maho::ai_security::IsProfileEligible(browser_->GetProfile())) {
    std::move(callback).Run(std::move(items));
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::move(items));
    return;
  }

  const size_t kMaxBookmarkResults = 50;
  size_t limit = (max_results == 0 || max_results > kMaxBookmarkResults)
                     ? kMaxBookmarkResults
                     : static_cast<size_t>(max_results);

  std::string json = maho::core::SearchBookmarks(core, query.c_str());
  if (json.empty()) {
    std::move(callback).Run(std::move(items));
    return;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    LOG(WARNING)
        << "MahoAIPageHandler::SearchBookmarks: failed to parse result";
    std::move(callback).Run(std::move(items));
    return;
  }

  for (const auto& entry : parsed->GetList()) {
    if (!entry.is_list()) {
      continue;
    }
    const base::ListValue& tuple = entry.GetList();
    if (tuple.size() < 3) {
      continue;
    }
    const std::string* id = tuple[0].GetIfString();
    const std::string* url = tuple[1].GetIfString();
    const std::string* title = tuple[2].GetIfString();
    if (!url || url->empty() || !id) {
      continue;
    }
    std::string redacted_url = maho::ai_security::RedactUrlForAi(*url);
    if (redacted_url.empty()) {
      continue;
    }

    auto item = maho_ai::mojom::BookmarkItem::New();
    item->id = *id;
    item->url = std::move(redacted_url);
    item->title = title ? *title : "";
    items.push_back(std::move(item));

    if (items.size() >= limit) {
      break;
    }
  }

  std::move(callback).Run(std::move(items));
}

void MahoAIPageHandler::SubmitPrompt(
    const std::string& session_id,
    const std::string& prompt,
    bool attach_browser_context,
    maho_ai::mojom::InteractionMode mode,
    std::optional<std::vector<maho_ai::mojom::ContextAttachmentPtr>>
        attachments,
    maho_ai::mojom::ChatIntent chat_intent,
    SubmitPromptCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  if (session_id != active_session_id_) {
    auto event = maho_ai::mojom::RuntimeEvent::New();
    event->kind = maho_ai::mojom::RuntimeEventKind::kError;
    event->session_id = session_id;
    event->sequence = ++event_sequence_;
    event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
    event->text =
        "Your prompt could not be delivered because the session is no longer active.";
    page_->OnRuntimeEvent(std::move(event));
    std::move(callback).Run(false);
    return;
  }
  SubmitPromptInternal(
      session_id, prompt, attach_browser_context, mode, std::move(attachments),
      chat_intent, Correlation{session_id, std::nullopt},
      base::BindOnce(
          [](MahoCore* core, std::string session_id, std::string prompt,
             SubmitPromptCallback callback, bool dispatchable) {
            const bool conversation_persisted =
                dispatchable &&
                maho_core_create_conversation(core, session_id.c_str(),
                                              DeriveTitle(prompt).c_str(),
                                              nullptr, nullptr);
            const bool accepted =
                conversation_persisted &&
                maho_core_save_conversation_message(
                    core, session_id.c_str(), "user", prompt.c_str(), nullptr);
            std::move(callback).Run(accepted);
            return accepted;
          },
          core, session_id, prompt, std::move(callback)));
}

void MahoAIPageHandler::ComposerDraftGet(const std::string& scope_json,
                                         ComposerDraftGetCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  char* raw = maho_core_get_composer_draft(core, scope_json.c_str());
  if (!raw) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::string draft_json(raw);
  maho_string_free(raw);
  std::move(callback).Run(std::move(draft_json));
}

void MahoAIPageHandler::ComposerDraftSet(const std::string& scope_json,
                                         const std::string& text,
                                         ComposerDraftSetCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  std::move(callback).Run(
      core && IsAiAllowed() &&
      maho_core_set_composer_draft(core, scope_json.c_str(), text.c_str()));
}

void MahoAIPageHandler::ComposerDraftDelete(
    const std::string& scope_json,
    ComposerDraftDeleteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  std::move(callback).Run(
      core && IsAiAllowed() &&
      maho_core_delete_composer_draft(core, scope_json.c_str()));
}

void MahoAIPageHandler::SubmitPromptInternal(
    const std::string& session_id,
    const std::string& prompt,
    bool attach_browser_context,
    maho_ai::mojom::InteractionMode mode,
    std::optional<std::vector<maho_ai::mojom::ContextAttachmentPtr>>
        attachments,
    maho_ai::mojom::ChatIntent chat_intent,
    Correlation correlation,
    base::OnceCallback<bool(bool)> dispatch_gate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    DenyAndResetConnection();
    return;
  }

  if (session_id != active_session_id_) {
    // The addressed session is no longer active (e.g. another session was
    // selected between Ask Maho creating its session and this submit landing).
    // Dropping the prompt here used to be silent: the panel showed a spinner
    // that vanished with no message and no error. Surface it instead.
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    MahoAiRuntimeEvent stale_event;
    stale_event.type = MahoAiRuntimeEventType::kError;
    stale_event.text =
        "This request could not be delivered because the conversation changed. "
        "Please try again.";
    OnRuntimeEventForSession(correlation.session_id, correlation.request_id,
                             std::move(stale_event));
    return;
  }

  // Persist the user prompt as a first-class event so history replay
  // includes it rather than relying on client-side synthetic entries.
  auto prompt_event = maho_ai::mojom::RuntimeEvent::New();
  prompt_event->kind = maho_ai::mojom::RuntimeEventKind::kUserPrompt;
  prompt_event->sequence = ++event_sequence_;
  prompt_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  prompt_event->text = prompt;
  prompt_event->session_id = correlation.session_id;
  if (correlation.request_id.has_value()) {
    prompt_event->request_id = *correlation.request_id;
  }
  const auto admission = maho::ai::AppendBoundedReplayEvent(
      session_event_log_, replay_budget_, prompt_event);
  if (page_) {
    page_->OnRuntimeEvent(std::move(prompt_event));
  }
  if (admission != maho::ai::ReplayAdmission::kAccepted) {
    SetSessionStatus(maho_ai::mojom::SessionStatus::kError);
    PersistEventLog(correlation.session_id);
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    return;
  }

  // Ensure session is active when a prompt is submitted.
  if (session_status_ != maho_ai::mojom::SessionStatus::kActive) {
    SetSessionStatus(maho_ai::mojom::SessionStatus::kActive);
  }

  UpdateSessionTitle(prompt);

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);

  if (adapter && !correlation.session_id.empty() &&
      adapter->GetRuntimeSessionId() != correlation.session_id) {
    adapter->StartSession(correlation.session_id);
  }

  if (!adapter || !adapter->IsAvailable()) {
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    MahoAiRuntimeEvent error_event;
    error_event.type = MahoAiRuntimeEventType::kError;
    error_event.text = GetRuntimeUnavailableMessage();
    OnRuntimeEventForSession(correlation.session_id, correlation.request_id,
                             std::move(error_event));
    return;
  }

  if (attachments.has_value()) {
    auto batch = std::make_shared<PendingAttachmentBatch>();
    batch->user_prompt = prompt;
    batch->mode = mode;
    batch->chat_intent = chat_intent;
    batch->correlation = correlation;
    batch->dispatch_gate = std::move(dispatch_gate);

    for (const auto& att : *attachments) {
      AttachmentSlot slot;
      slot.label = att->label;
      if (att->kind == maho_ai::mojom::ContextSourceKind::kCurrentPage) {
        slot.header_tag = "Current Page Context";
        batch->slots.push_back(std::move(slot));
      } else if (att->kind == maho_ai::mojom::ContextSourceKind::kOpenTab) {
        slot.header_tag = base::StrCat({"Tab Context: ", att->label});
        batch->slots.push_back(std::move(slot));
      } else if (att->kind == maho_ai::mojom::ContextSourceKind::kHistory) {
        slot.header_tag = base::StrCat({"History Context: ", att->label});
        batch->slots.push_back(std::move(slot));
      } else if (att->kind == maho_ai::mojom::ContextSourceKind::kBookmark) {
        slot.header_tag = base::StrCat({"Bookmark Context: ", att->label});
        batch->slots.push_back(std::move(slot));
      }
    }

    if (batch->slots.empty()) {
      if (batch->dispatch_gate && !std::move(batch->dispatch_gate).Run(true)) {
        return;
      }
      adapter->SubmitMessage(
          prompt, batch->chat_intent,
          /*attach_browser_context=*/false, mode,
          base::BindRepeating(&MahoAIPageHandler::OnRuntimeEventForSession,
                              weak_factory_.GetWeakPtr(),
                              correlation.session_id, correlation.request_id));
      return;
    }

    size_t idx = 0;
    for (const auto& att : *attachments) {
      if (att->kind == maho_ai::mojom::ContextSourceKind::kCurrentPage) {
        size_t slot_idx = idx++;
        page_context_extractor_->GetPageContext(
            base::BindOnce(&MahoAIPageHandler::OnAttachmentSlotCompleted,
                           weak_factory_.GetWeakPtr(), batch, slot_idx));
      } else if (att->kind == maho_ai::mojom::ContextSourceKind::kOpenTab) {
        size_t slot_idx = idx++;
        page_context_extractor_->GetPageContextForTabId(
            att->tab_id,
            base::BindOnce(&MahoAIPageHandler::OnAttachmentSlotCompleted,
                           weak_factory_.GetWeakPtr(), batch, slot_idx));
      } else if (att->kind == maho_ai::mojom::ContextSourceKind::kHistory) {
        size_t slot_idx = idx++;
        std::string redacted_url = maho::ai_security::RedactUrlForAi(att->url);
        MahoAiPageContextExtractor::PageContextResult history_result;
        history_result.extraction_status =
            MahoAiPageContextExtractor::PageContextResult::Status::kSuccess;
        history_result.title = att->label;
        history_result.url = redacted_url;
        history_result.main_text =
            base::StrCat({"Title: ", att->label, "\nURL: ", redacted_url});
        OnAttachmentSlotCompleted(batch, slot_idx, std::move(history_result));
      } else if (att->kind == maho_ai::mojom::ContextSourceKind::kBookmark) {
        size_t slot_idx = idx++;
        std::string redacted_url = maho::ai_security::RedactUrlForAi(att->url);
        MahoAiPageContextExtractor::PageContextResult bookmark_result;
        bookmark_result.extraction_status =
            MahoAiPageContextExtractor::PageContextResult::Status::kSuccess;
        bookmark_result.title = att->label;
        bookmark_result.url = redacted_url;
        bookmark_result.main_text =
            base::StrCat({"Title: ", att->label, "\nURL: ", redacted_url});
        OnAttachmentSlotCompleted(batch, slot_idx, std::move(bookmark_result));
      }
    }
    return;
  }

  if (attach_browser_context) {
    page_context_extractor_->GetPageContext(
        base::BindOnce(&MahoAIPageHandler::OnContextExtractedThenSubmit,
                       weak_factory_.GetWeakPtr(), std::move(correlation),
                       std::move(dispatch_gate), prompt, mode, chat_intent));
    return;
  }

  if (dispatch_gate && !std::move(dispatch_gate).Run(true)) {
    return;
  }
  adapter->SubmitMessage(
      prompt, chat_intent,
      /*attach_browser_context=*/false, mode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeEventForSession,
                          weak_factory_.GetWeakPtr(), correlation.session_id,
                          correlation.request_id));
}

void MahoAIPageHandler::OnContextExtractedThenSubmit(
    Correlation correlation,
    base::OnceCallback<bool(bool)> dispatch_gate,
    std::string user_prompt,
    maho_ai::mojom::InteractionMode mode,
    maho_ai::mojom::ChatIntent chat_intent,
    MahoAiPageContextExtractor::PageContextResult ctx) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  if (!adapter || !adapter->IsAvailable()) {
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    return;
  }

  bool is_required_page_intent =
      (chat_intent == maho_ai::mojom::ChatIntent::kSummarizeCurrentPage ||
       chat_intent == maho_ai::mojom::ChatIntent::kQuizCurrentPage);

  std::string trimmed_text(
      base::TrimWhitespaceASCII(ctx.main_text, base::TRIM_ALL));
  if (is_required_page_intent &&
      (!ctx.HasUsableContent() || trimmed_text.empty())) {
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    MahoAiRuntimeEvent error_event;
    error_event.type = MahoAiRuntimeEventType::kError;
    error_event.text = "This quick action requires readable page content.";
    OnRuntimeEventForSession(correlation.session_id, correlation.request_id,
                             std::move(error_event));
    return;
  }

  MahoAiRuntimeEvent ctx_event;
  ctx_event.type = MahoAiRuntimeEventType::kBrowserContextInjected;

  std::string final_prompt;
  if (!ctx.HasUsableContent()) {
    DVLOG(1) << "MahoAIPageHandler: page context extraction failed ("
             << ctx.GetExtractionStatusString() << "); sending original prompt";
    final_prompt = user_prompt;
    std::string status_str;
    switch (ctx.extraction_status) {
      case MahoAiPageContextExtractor::PageContextResult::Status::kNoActiveTab:
        status_str = "no_active_tab";
        break;
      case MahoAiPageContextExtractor::PageContextResult::Status::kCannotAccess:
        status_str = "cannot_access";
        break;
      default:
        status_str = "extraction_failed";
        break;
    }
    ctx_event.payload.Set("status", status_str);
    ctx_event.payload.Set("url", ctx.url);
    ctx_event.payload.Set("title", ctx.title);
    ctx_event.payload.Set("label", "Current page");
    ctx_event.text =
        "Page content could not be extracted; sending prompt without page "
        "context.";
  } else {
    std::string clipped_text = ctx.main_text;
    if (clipped_text.size() > kMaxPageContextChars) {
      clipped_text = clipped_text.substr(0, kMaxPageContextChars) + "...";
    }
    final_prompt = base::StrCat({"[Current Page Context]\nTitle: ", ctx.title,
                                 "\nURL: ", ctx.url, "\nContent:\n",
                                 maho::ai_security::WrapUntrusted(clipped_text),
                                 "\n\nUser Question:\n", user_prompt});
    ctx_event.payload.Set("status", "injected");
    ctx_event.payload.Set("url", ctx.url);
    ctx_event.payload.Set("title", ctx.title);
    ctx_event.payload.Set("content_length",
                          static_cast<int>(ctx.main_text.size()));
    std::string snippet = UTF8SafeTruncate(ctx.main_text, 200);
    ctx_event.payload.Set("content_snippet", snippet);
    ctx_event.payload.Set("label", "Current page");
    base::ListValue warns;
    for (const auto& w : ctx.extraction_warnings) {
      warns.Append(w);
    }
    ctx_event.payload.Set("warnings", std::move(warns));
    ctx_event.text = base::StrCat({"Attached page context: ", ctx.title});
  }

  OnRuntimeEventForSession(correlation.session_id, correlation.request_id,
                           std::move(ctx_event));

  if (dispatch_gate && !std::move(dispatch_gate).Run(true)) {
    return;
  }
  adapter->SubmitMessage(
      final_prompt, chat_intent,
      /*attach_browser_context=*/false, mode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeEventForSession,
                          weak_factory_.GetWeakPtr(), correlation.session_id,
                          correlation.request_id));
}

void MahoAIPageHandler::OnTabContextExtractedThenSubmit(
    Correlation correlation,
    base::OnceCallback<bool(bool)> dispatch_gate,
    std::string user_prompt,
    maho_ai::mojom::InteractionMode mode,
    maho_ai::mojom::ChatIntent chat_intent,
    std::string tab_label,
    MahoAiPageContextExtractor::PageContextResult ctx) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  if (!adapter || !adapter->IsAvailable()) {
    if (dispatch_gate) {
      std::move(dispatch_gate).Run(false);
    }
    return;
  }
  MahoAiRuntimeEvent ctx_event;
  ctx_event.type = MahoAiRuntimeEventType::kBrowserContextInjected;

  std::string final_prompt;
  if (!ctx.HasUsableContent()) {
    final_prompt = user_prompt;
    std::string status_str;
    switch (ctx.extraction_status) {
      case MahoAiPageContextExtractor::PageContextResult::Status::kNoActiveTab:
        status_str = "no_active_tab";
        break;
      case MahoAiPageContextExtractor::PageContextResult::Status::kCannotAccess:
        status_str = "cannot_access";
        break;
      default:
        status_str = "extraction_failed";
        break;
    }
    ctx_event.payload.Set("status", status_str);
    ctx_event.payload.Set("url", ctx.url);
    ctx_event.payload.Set("title", ctx.title);
    ctx_event.payload.Set("label", tab_label);
    ctx_event.text =
        base::StrCat({"Tab content could not be extracted for \"", tab_label,
                      "\"; sending prompt without tab context."});
  } else {
    std::string clipped_text = ctx.main_text;
    if (clipped_text.size() > kMaxPageContextChars) {
      clipped_text = clipped_text.substr(0, kMaxPageContextChars) + "...";
    }
    final_prompt =
        base::StrCat({"[Tab Context: ", tab_label, "]\nTitle: ", ctx.title,
                      "\nURL: ", ctx.url, "\nContent:\n",
                      maho::ai_security::WrapUntrusted(clipped_text),
                      "\n\nUser Question:\n", user_prompt});
    ctx_event.payload.Set("status", "injected");
    ctx_event.payload.Set("url", ctx.url);
    ctx_event.payload.Set("title", ctx.title);
    ctx_event.payload.Set("content_length",
                          static_cast<int>(ctx.main_text.size()));
    std::string snippet = UTF8SafeTruncate(ctx.main_text, 200);
    ctx_event.payload.Set("content_snippet", snippet);
    ctx_event.payload.Set("label", tab_label);
    base::ListValue warns;
    for (const auto& w : ctx.extraction_warnings) {
      warns.Append(w);
    }
    ctx_event.payload.Set("warnings", std::move(warns));
    ctx_event.text = base::StrCat({"Attached tab context: ", tab_label});
  }

  OnRuntimeEventForSession(correlation.session_id, correlation.request_id,
                           std::move(ctx_event));

  if (dispatch_gate && !std::move(dispatch_gate).Run(true)) {
    return;
  }
  adapter->SubmitMessage(
      final_prompt, chat_intent,
      /*attach_browser_context=*/false, mode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeEventForSession,
                          weak_factory_.GetWeakPtr(), correlation.session_id,
                          correlation.request_id));
}

void MahoAIPageHandler::OnAttachmentSlotCompleted(
    std::shared_ptr<PendingAttachmentBatch> batch,
    size_t slot_index,
    MahoAiPageContextExtractor::PageContextResult ctx) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DCHECK(slot_index < batch->slots.size());
  batch->slots[slot_index].result = std::move(ctx);
  batch->slots[slot_index].completed = true;
  ++batch->completed_count;

  if (batch->completed_count < batch->slots.size()) {
    return;
  }

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  if (!adapter || !adapter->IsAvailable()) {
    if (batch->dispatch_gate) {
      std::move(batch->dispatch_gate).Run(false);
    }
    return;
  }

  std::string combined_context;
  for (size_t i = 0; i < batch->slots.size(); ++i) {
    auto& slot = batch->slots[i];
    MahoAiRuntimeEvent ctx_event;
    ctx_event.type = MahoAiRuntimeEventType::kBrowserContextInjected;

    if (!slot.result.HasUsableContent()) {
      std::string status_str;
      switch (slot.result.extraction_status) {
        case MahoAiPageContextExtractor::PageContextResult::Status::
            kNoActiveTab:
          status_str = "no_active_tab";
          break;
        case MahoAiPageContextExtractor::PageContextResult::Status::
            kCannotAccess:
          status_str = "cannot_access";
          break;
        default:
          status_str = "extraction_failed";
          break;
      }
      ctx_event.payload.Set("status", status_str);
      ctx_event.payload.Set("url", slot.result.url);
      ctx_event.payload.Set("title", slot.result.title);
      ctx_event.payload.Set("label", slot.label);
      ctx_event.text = base::StrCat({"Content could not be extracted for \"",
                                     slot.label, "\"; skipping."});
    } else {
      std::string clipped = slot.result.main_text;
      if (clipped.size() > kMaxPageContextChars) {
        clipped = clipped.substr(0, kMaxPageContextChars) + "...";
      }
      combined_context +=
          base::StrCat({"[", slot.header_tag, "]\nTitle: ", slot.result.title,
                        "\nURL: ", slot.result.url, "\nContent:\n",
                        maho::ai_security::WrapUntrusted(clipped), "\n\n"});
      ctx_event.payload.Set("status", "injected");
      ctx_event.payload.Set("url", slot.result.url);
      ctx_event.payload.Set("title", slot.result.title);
      ctx_event.payload.Set("content_length",
                            static_cast<int>(slot.result.main_text.size()));
      std::string snippet = UTF8SafeTruncate(slot.result.main_text, 200);
      ctx_event.payload.Set("content_snippet", snippet);
      ctx_event.payload.Set("label", slot.label);
      base::ListValue warns;
      for (const auto& w : slot.result.extraction_warnings) {
        warns.Append(w);
      }
      ctx_event.payload.Set("warnings", std::move(warns));
      ctx_event.text = base::StrCat({"Attached context: ", slot.label});
    }
    OnRuntimeEventForSession(batch->correlation.session_id,
                             batch->correlation.request_id,
                             std::move(ctx_event));
  }

  std::string final_prompt;
  if (combined_context.empty()) {
    final_prompt = batch->user_prompt;
  } else {
    final_prompt = base::StrCat(
        {combined_context, "User Question:\n", batch->user_prompt});
  }

  if (batch->dispatch_gate && !std::move(batch->dispatch_gate).Run(true)) {
    return;
  }
  adapter->SubmitMessage(
      final_prompt, batch->chat_intent,
      /*attach_browser_context=*/false, batch->mode,
      base::BindRepeating(&MahoAIPageHandler::OnRuntimeEventForSession,
                          weak_factory_.GetWeakPtr(),
                          batch->correlation.session_id,
                          batch->correlation.request_id));
}

void MahoAIPageHandler::CancelTurn(const std::string& session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    DenyAndResetConnection();
    return;
  }

  if (session_id != active_session_id_) {
    return;
  }

  if (MahoAiRuntimeAdapter* adapter =
          runtime_adapter_for_testing_
              ? runtime_adapter_for_testing_.get()
              : (runtime_router_ ? runtime_router_->GetActiveAdapter()
                                 : nullptr)) {
    adapter->CancelCurrentTurn();
  }
  SetSessionStatus(maho_ai::mojom::SessionStatus::kCancelled);
  PersistEventLog(active_session_id_);
  PersistSessionList();

  const std::string request_id = active_ingress_request_id_;
  SettleIngressRequest(request_id);
}

void MahoAIPageHandler::RespondToApproval(const std::string& session_id,
                                          const std::string& approval_id,
                                          bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    DenyAndResetConnection();
    return;
  }

  if (session_id != active_session_id_) {
    return;
  }

  // Do NOT flip status to kActive here — wait for the runtime to confirm
  // via permission.replied (which arrives as kApprovalResult event).
  if (MahoAiRuntimeAdapter* adapter =
          runtime_adapter_for_testing_
              ? runtime_adapter_for_testing_.get()
              : (runtime_router_ ? runtime_router_->GetActiveAdapter()
                                 : nullptr)) {
    adapter->RespondToApproval(approval_id, approved);
  }
}

void MahoAIPageHandler::WatchControlActivity(
    WatchControlActivityCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "[maho-control-activity] WatchControlActivity called, browser="
            << (browser_ ? "set" : "null");
  auto* service = maho::ai::MahoControlActivityService::GetForProfile(
      browser_ ? browser_->GetProfile() : nullptr);
  if (!service) {
    std::move(callback).Run({});
    return;
  }
  if (!control_activity_watched_) {
    control_activity_watched_ = true;
    control_activity_service_ = service;
    service->AddObserver(this);
  }
  std::move(callback).Run(BuildControlActivityTimeline());
}

void MahoAIPageHandler::OnControlActivityChanged(
    const maho::ai::ControlActivity& activity) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!control_activity_watched_) {
    return;
  }
  if (activity.control_plane != maho::ai::ControlPlane::kLocalAgent &&
      activity.target.has_value() &&
      (activity.state == maho::ai::ControlActivityState::kReading ||
       activity.state == maho::ai::ControlActivityState::kActing)) {
    external_controller_session_ = activity.controller_session_id;
  }
  if (external_controller_session_.empty() ||
      activity.controller_session_id != external_controller_session_) {
    return;
  }
  page_->OnControlActivityChanged(BuildControlActivityTimeline());
}

std::vector<maho_ai::mojom::ControlActivitySnapshotPtr>
MahoAIPageHandler::BuildControlActivityTimeline() {
  using State = maho::ai::ControlActivityState;
  auto* service = maho::ai::MahoControlActivityService::GetForProfile(
      browser_ ? browser_->GetProfile() : nullptr);
  if (!service) {
    return {};
  }
  std::vector<maho::ai::ControlActivity> session_activities;
  for (const auto& activity : service->GetActivities()) {
    if (external_controller_session_.empty() ||
        activity.controller_session_id != external_controller_session_) {
      continue;
    }
    session_activities.push_back(activity);
  }
  std::sort(session_activities.begin(), session_activities.end(),
            [](const maho::ai::ControlActivity& a,
               const maho::ai::ControlActivity& b) {
              return a.created_at < b.created_at;
            });
  constexpr size_t kMaxTimelineEntries = 50;
  if (session_activities.size() > kMaxTimelineEntries) {
    session_activities.erase(
        session_activities.begin(),
        session_activities.end() - static_cast<long>(kMaxTimelineEntries));
  }
  std::vector<maho_ai::mojom::ControlActivitySnapshotPtr> entries;
  entries.reserve(session_activities.size());
  for (const auto& activity : session_activities) {
    auto entry = maho_ai::mojom::ControlActivitySnapshot::New();
    entry->controller_name = activity.controller_display_name;
    switch (activity.state) {
      case State::kReading:
        entry->state = maho_ai::mojom::ControlActivityState::kReading;
        break;
      case State::kActing:
        entry->state = maho_ai::mojom::ControlActivityState::kActing;
        break;
      case State::kWaitingApproval:
        entry->state = maho_ai::mojom::ControlActivityState::kWaitingApproval;
        break;
      case State::kPaused:
        entry->state = maho_ai::mojom::ControlActivityState::kPaused;
        break;
      case State::kDisconnected:
        entry->state = maho_ai::mojom::ControlActivityState::kDisconnected;
        break;
      case State::kCompleted:
        entry->state = maho_ai::mojom::ControlActivityState::kIdle;
        break;
      case State::kFailed:
        entry->state = maho_ai::mojom::ControlActivityState::kFailed;
        break;
    }
    entry->target_title =
        activity.target
            ? base::UTF16ToUTF8(activity.target->title)
            : std::string();
    entries.push_back(std::move(entry));
  }
  return entries;
}

void MahoAIPageHandler::ClosePanel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }

  if ((browser_->GetType() == BrowserWindowInterface::TYPE_POPUP)) {
    browser_->GetWindow()->Close();
    return;
  }

  auto* side_panel_ui = browser_->GetFeatures().side_panel_ui();
  if (!side_panel_ui) {
    return;
  }

  if (side_panel_ui->GetCurrentEntryId() !=
      SidePanelEntryId::kMahoAiPanel) {
    return;
  }

  side_panel_ui->Close(
                       SidePanelEntryHideReason::kSidePanelClosed,
                       /*suppress_animations=*/true);
}

void MahoAIPageHandler::OpenSettings() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }
  maho::OpenMahoSettingsPane(browser_, "maho-ai");
}

void MahoAIPageHandler::OpenSettingsPane(const std::string& pane_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }
  maho::OpenMahoSettingsPane(browser_, ResolveMahoAiSettingsPaneKey(pane_key));
}

maho_ai::mojom::AISettingsInfoPtr MahoAIPageHandler::BuildAISettingsInfo()
    const {
  auto info = maho_ai::mojom::AISettingsInfo::New();
  std::string active_provider = prefs_->GetString(maho::ai_prefs::kProvider);

  // If active provider is empty or not configured, fall back to the first configured provider.
  if (active_provider.empty() || !IsProviderConfigured(active_provider, prefs_)) {
    active_provider.clear();
    for (const auto& definition : kAIProviderOptions) {
      if (IsProviderConfigured(definition.id, prefs_)) {
        active_provider = definition.id;
        break;
      }
    }
  }

  info->active_provider_id = active_provider;
  info->active_model_id = prefs_->GetString(maho::ai_prefs::kModel);
  info->active_reasoning_effort = ReasoningEffortFromPref(
      prefs_->GetString(maho::ai_prefs::kReasoningEffort));

  for (const auto& definition : kAIProviderOptions) {
    if (!IsProviderConfigured(definition.id, prefs_)) {
      continue;
    }
    auto provider = maho_ai::mojom::AIProviderOption::New();
    provider->id = definition.id;
    provider->label = definition.label;
    for (const auto& model_id : GetSelectableModels(
             definition.id, info->active_provider_id, info->active_model_id, prefs_)) {
      provider->model_options.push_back(
          maho_ai::mojom::AIModelOption::New(model_id, model_id));
    }
    info->provider_options.push_back(std::move(provider));
  }

  // If active_model_id does not belong to active_provider or active_provider changed,
  // ensure active_model_id is valid for the active provider.
  if (!info->provider_options.empty()) {
    for (const auto& opt : info->provider_options) {
      if (opt->id == info->active_provider_id && !opt->model_options.empty()) {
        bool model_found = false;
        for (const auto& model_opt : opt->model_options) {
          if (model_opt->id == info->active_model_id) {
            model_found = true;
            break;
          }
        }
        if (!model_found) {
          info->active_model_id = opt->model_options[0]->id;
        }
        break;
      }
    }
  } else {
    // No providers configured: active_provider and active_model are empty
    info->active_provider_id = "";
    info->active_model_id = "";
  }

  info->reasoning_options.push_back(maho_ai::mojom::ReasoningOption::New(
      maho_ai::mojom::ReasoningEffort::kLow, "Low"));
  info->reasoning_options.push_back(maho_ai::mojom::ReasoningOption::New(
      maho_ai::mojom::ReasoningEffort::kMedium, "Medium"));
  info->reasoning_options.push_back(maho_ai::mojom::ReasoningOption::New(
      maho_ai::mojom::ReasoningEffort::kHigh, "High"));
  return info;
}

void MahoAIPageHandler::GetAISettings(GetAISettingsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildAISettingsInfo());
}

void MahoAIPageHandler::SetDefaultAISelection(
    const std::string& provider_id,
    const std::string& model_id,
    maho_ai::mojom::ReasoningEffort reasoning_effort,
    SetDefaultAISelectionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string active_provider_id =
      prefs_->GetString(maho::ai_prefs::kProvider);
  const std::string active_model_id = prefs_->GetString(maho::ai_prefs::kModel);
  if (!IsSelectableProvider(provider_id) ||
      !IsSelectableModel(provider_id, model_id, active_provider_id,
                         active_model_id, prefs_) ||
      !IsValidReasoningEffort(reasoning_effort)) {
    std::move(callback).Run(false);
    return;
  }

  prefs_->SetString(maho::ai_prefs::kProvider, provider_id);
  prefs_->SetString(maho::ai_prefs::kModel, model_id);
  prefs_->SetString(maho::ai_prefs::kReasoningEffort,
                    ReasoningEffortToPref(reasoning_effort));
  OnAISettingsPrefsChanged();
  std::move(callback).Run(true);
}
// Global Default switcher contract: SetDefaultAISelection writes the same
// maho.ai.provider + maho.ai.model pair that the Settings Models page's
// Chat & Agent — Default row edits, so inherited task routes move with it.

void MahoAIPageHandler::GetRuntimeConfig(GetRuntimeConfigCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto config = maho_ai::mojom::RuntimeConfigInfo::New();
  config->permission_tier = maho::ai::ParseRuntimeConfigTier(
      prefs_->GetString(maho::ai_prefs::kPermissionTier));
  config->final_confirm = prefs_->GetBoolean(maho::ai_prefs::kFinalConfirm);
  config->proactive_mode = prefs_->GetBoolean(maho::ai_prefs::kProactiveMode);
  config->mail_read_allowed =
      prefs_->GetBoolean(maho::ai_prefs::kMailReadAllowed);

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  MahoAiRuntimeConfig stored;
  if (adapter &&
      (!adapter->GetRuntimeConfig(&stored) ||
       stored.permission_tier != config->permission_tier ||
       stored.final_confirm != config->final_confirm ||
       stored.proactive_mode != config->proactive_mode)) {
    // Reconcile the live cache without letting it override durable prefs or
    // discarding session-only fields such as filesystem whitelist roots.
    stored.permission_tier = config->permission_tier;
    stored.final_confirm = config->final_confirm;
    stored.proactive_mode = config->proactive_mode;
    adapter->SetRuntimeConfig(stored);
  }
  std::move(callback).Run(std::move(config));
}

void MahoAIPageHandler::SetRuntimeConfig(
    maho_ai::mojom::RuntimeConfigInfoPtr config,
    SetRuntimeConfigCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!config) {
    std::move(callback).Run(false);
    return;
  }
  prefs_->SetString(maho::ai_prefs::kPermissionTier,
                    maho::ai::ParseRuntimeConfigTier(config->permission_tier));
  prefs_->SetBoolean(maho::ai_prefs::kFinalConfirm, config->final_confirm);
  prefs_->SetBoolean(maho::ai_prefs::kProactiveMode, config->proactive_mode);
  // Mail read consent shares this pref with chrome://maho-settings, so both
  // surfaces stay on one source of truth. Mail writes are unaffected: they
  // keep their per-call typed approval in the Mail authorization table.
  prefs_->SetBoolean(maho::ai_prefs::kMailReadAllowed,
                     config->mail_read_allowed);
  // Also reconcile when the values are unchanged and no pref observer fires.
  // Acceptance means persisted; a session created later inherits these prefs.
  OnRuntimeConfigPrefsChanged();
  std::move(callback).Run(true);
}

void MahoAIPageHandler::OnRuntimeConfigPrefsChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  GetRuntimeConfig(base::BindOnce(
      [](MahoAIPageHandler* handler,
         maho_ai::mojom::RuntimeConfigInfoPtr config) {
        if (handler->page_.is_bound()) {
          handler->page_->OnRuntimeConfigChanged(std::move(config));
        }
      },
      base::Unretained(this)));
}

void MahoAIPageHandler::OnAISettingsPrefsChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  if (page_.is_bound()) {
    page_->OnAISettingsChanged(BuildAISettingsInfo());
  }
}

void MahoAIPageHandler::GetViewMode(GetViewModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string pref_val = prefs_->GetString(maho::ai_prefs::kAiViewMode);
  maho_ai::mojom::ViewMode mode =
      (pref_val == "floating" ? maho_ai::mojom::ViewMode::kFloating
                              : maho_ai::mojom::ViewMode::kSidebar);
  std::move(callback).Run(mode);
}

void MahoAIPageHandler::SetViewMode(maho_ai::mojom::ViewMode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string new_pref =
      (mode == maho_ai::mojom::ViewMode::kFloating ? "floating" : "sidebar");
  std::string current_pref = prefs_->GetString(maho::ai_prefs::kAiViewMode);
  if (new_pref == current_pref) {
    return;
  }

  prefs_->SetString(maho::ai_prefs::kAiViewMode, new_pref);

  if (mode == maho_ai::mojom::ViewMode::kFloating) {
    if (browser_ && !(browser_->GetType() == BrowserWindowInterface::TYPE_POPUP)) {
      BrowserWindowCreateParams popup_params(Browser::TYPE_POPUP,
                                         browser_->GetProfile(),
                                         /*user_gesture=*/true);
      popup_params.is_trusted_source = true;
      popup_params.omit_from_session_restore = true;
      Browser* popup = static_cast<Browser*>(CreateBrowserWindow(std::move(popup_params)));
      if (!popup) {
        return;
      }
      PrepareForViewModeHandoff();
      chrome::AddTabAt(popup, GURL(maho::kMahoAIPublicURL),
                       /*index=*/-1, /*foreground=*/true);
      maho::MahoAiPopupLifetimeTracker::Get()->TrackPopup(browser_, popup);
      popup->GetWindow()->Show();
      ClosePanel();
    }
  } else {
    if (browser_ && (browser_->GetType() == BrowserWindowInterface::TYPE_POPUP)) {
      ProfileBrowserCollection* collection =
          ProfileBrowserCollection::GetForProfile(browser_->GetProfile());
      BrowserWindowInterface* tabbed_bwi =
          collection ? collection->FindTabbedBrowser(/*match_original_profiles=*/false)
                     : nullptr;
      Browser* tabbed_browser = static_cast<Browser*>(tabbed_bwi);
      if (tabbed_browser && tabbed_browser->GetFeatures().side_panel_ui()) {
        PrepareForViewModeHandoff();
        tabbed_browser->GetFeatures().side_panel_ui()->Show(
            SidePanelEntryId::kMahoAiPanel);
        tabbed_browser->GetWindow()->Activate();
      }
      browser_->GetWindow()->Close();
    }
  }
}

void MahoAIPageHandler::ListArtifacts(const std::string& session_id,
                                      ListArtifactsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<maho_ai::mojom::ArtifactInfoPtr> out;
  maho::ai::MahoArtifactRegistry* registry =
      browser_
          ? maho::ai::MahoArtifactRegistry::GetForProfile(browser_->GetProfile())
          : nullptr;
  if (registry) {
    for (const maho::ai::MahoArtifact& a :
         registry->ListArtifacts(session_id)) {
      auto info = maho_ai::mojom::ArtifactInfo::New();
      info->artifact_id = a.artifact_id;
      info->session_id = a.session_id;
      info->display_name = a.display_name;
      info->mime_type = a.mime_type;
      info->size_bytes = a.size_bytes;
      info->created_at = static_cast<double>(a.created_at_ms) / 1000.0;
      out.push_back(std::move(info));
    }
  }
  std::move(callback).Run(std::move(out));
}

void MahoAIPageHandler::RenameArtifact(const std::string& artifact_id,
                                       const std::string& display_name,
                                       RenameArtifactCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::ai::MahoArtifactRegistry* registry =
      browser_
          ? maho::ai::MahoArtifactRegistry::GetForProfile(browser_->GetProfile())
          : nullptr;
  if (!registry) {
    std::move(callback).Run(false, "registry unavailable");
    return;
  }
  auto result = registry->RenameArtifact(artifact_id, display_name);
  if (result.has_value()) {
    std::move(callback).Run(true, std::nullopt);
  } else {
    std::move(callback).Run(false, result.error());
  }
}

void MahoAIPageHandler::DeleteArtifact(const std::string& artifact_id,
                                       DeleteArtifactCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::ai::MahoArtifactRegistry* registry =
      browser_
          ? maho::ai::MahoArtifactRegistry::GetForProfile(browser_->GetProfile())
          : nullptr;
  std::move(callback).Run(registry && registry->DeleteArtifact(artifact_id));
}

void MahoAIPageHandler::GetArtifactPreviewUrl(
    const std::string& artifact_id,
    GetArtifactPreviewUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::ai::MahoArtifactRegistry* registry =
      browser_
          ? maho::ai::MahoArtifactRegistry::GetForProfile(browser_->GetProfile())
          : nullptr;
  content::WebContents* web_contents =
      host_web_contents_ ? host_web_contents_.get()
                         : (token_ ? token_->web_contents() : nullptr);
  if (!registry || !web_contents) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::optional<std::string> capability =
      registry->IssueCapability(artifact_id, "preview", web_contents);
  if (!capability) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(
      net::AppendQueryParameter(GURL(maho::kMahoArtifactPreviewUntrustedURL),
                                "cap", *capability)
          .spec());
}

namespace {

// Reads a contained artifact off the UI thread and returns it as a
// self-contained data: URL. Used as the drag-out DownloadURL source: macOS
// DragDownloadFile can fetch data: URLs (the download system supports the data
// scheme), whereas it cannot fetch the chrome-untrusted:// export endpoint, so
// a capability URL never produced a dropped file in Finder.
std::optional<std::string> ReadArtifactAsDragDataUrl(
    base::FilePath artifact_root,
    std::string storage_rel_path,
    std::string mime_type,
    size_t max_bytes) {
  std::optional<base::FilePath> path =
      maho::ai::MahoArtifactRegistry::ResolveContainedStoragePath(
          artifact_root, storage_rel_path);
  if (!path) {
    return std::nullopt;
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(*path, &contents, max_bytes)) {
    return std::nullopt;
  }
  const std::string mime =
      mime_type.empty() ? "application/octet-stream" : mime_type;
  return "data:" + mime + ";base64," + base::Base64Encode(contents);
}

}  // namespace

void MahoAIPageHandler::GetArtifactExportUrl(
    const std::string& artifact_id,
    GetArtifactExportUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::ai::MahoArtifactRegistry* registry =
      browser_
          ? maho::ai::MahoArtifactRegistry::GetForProfile(browser_->GetProfile())
          : nullptr;
  if (!registry) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::optional<maho::ai::MahoArtifact> artifact =
      registry->GetArtifact(artifact_id);
  if (!artifact) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  // Cap the drag data: URL size: base64 inflates ~33% and the whole thing is
  // held in memory. Larger artifacts return null (card stays non-draggable)
  // until a native file-promise drag fallback lands.
  constexpr size_t kMaxDragDataUrlBytes = 32u * 1024u * 1024u;
  if (artifact->size_bytes > kMaxDragDataUrlBytes) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadArtifactAsDragDataUrl, registry->artifact_root(),
                     artifact->storage_rel_path, artifact->mime_type,
                     kMaxDragDataUrlBytes),
      std::move(callback));
}

void MahoAIPageHandler::GetSessionHistory(const std::string& session_id,
                                          uint32_t offset,
                                          uint32_t limit,
                                          GetSessionHistoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  uint32_t total_count = 0;

  if (session_id == active_session_id_ && !active_session_id_.empty()) {
    total_count = static_cast<uint32_t>(session_event_log_.size());
    uint32_t start = std::min(offset, total_count);
    uint32_t end = std::min(start + limit, total_count);
    for (uint32_t i = start; i < end; ++i) {
      events.push_back(session_event_log_[i]->Clone());
    }
  } else {
    auto all_events = LoadPersistedEventLog(session_id);
    total_count = static_cast<uint32_t>(all_events.size());
    uint32_t start = std::min(offset, total_count);
    uint32_t end = std::min(start + limit, total_count);
    for (uint32_t i = start; i < end; ++i) {
      events.push_back(std::move(all_events[i]));
    }
  }

  std::move(callback).Run(std::move(events), total_count);
}

void MahoAIPageHandler::MaybeEmitConnectionStateChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto current = ResolveConnectionState();

  if (current != last_emitted_connection_state_) {
    last_emitted_connection_state_ = current;
    page_->OnConnectionStateChanged(current);
  }
}

maho_ai::mojom::RuntimeConnectionState
MahoAIPageHandler::ResolveConnectionState() const {
  if (!runtime_router_->IsAvailable()) {
    return maho_ai::mojom::RuntimeConnectionState::kDisconnected;
  }
  if (last_runtime_state_ == "paused_for_approval") {
    return maho_ai::mojom::RuntimeConnectionState::kPausedForApproval;
  }
  if (last_runtime_state_ == "resumed") {
    return maho_ai::mojom::RuntimeConnectionState::kResumed;
  }
  return maho_ai::mojom::RuntimeConnectionState::kConnected;
}

void MahoAIPageHandler::EmitSessionUpdated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_ && !active_session_id_.empty()) {
    page_->OnSessionUpdated(MakeActiveSessionInfo());
  }
}

void MahoAIPageHandler::SetSessionStatus(maho_ai::mojom::SessionStatus status) {
  if (session_status_ == status) {
    return;
  }
  session_status_ = status;

  // Emit a replayable kSessionStatus event so history replay reflects
  // meaningful lifecycle transitions (not just the initial "started").
  if (!active_session_id_.empty()) {
    auto status_event = maho_ai::mojom::RuntimeEvent::New();
    status_event->kind = maho_ai::mojom::RuntimeEventKind::kSessionStatus;
    status_event->sequence = ++event_sequence_;
    status_event->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
    switch (status) {
      case maho_ai::mojom::SessionStatus::kActive:
        status_event->text = "active";
        break;
      case maho_ai::mojom::SessionStatus::kIdle:
        status_event->text = "idle";
        break;
      case maho_ai::mojom::SessionStatus::kPausedForApproval:
        status_event->text = "paused_for_approval";
        break;
      case maho_ai::mojom::SessionStatus::kCompleted:
        status_event->text = "completed";
        break;
      case maho_ai::mojom::SessionStatus::kCancelled:
        status_event->text = "cancelled";
        break;
      case maho_ai::mojom::SessionStatus::kError:
        status_event->text = "error";
        break;
      case maho_ai::mojom::SessionStatus::kCreated:
        status_event->text = "created";
        break;
    }
    status_event->session_id = active_session_id_;
    if (!active_ingress_request_id_.empty()) {
      status_event->request_id = active_ingress_request_id_;
    }
    const auto admission = maho::ai::AppendBoundedReplayEvent(
        session_event_log_, replay_budget_, status_event);
    if (admission == maho::ai::ReplayAdmission::kExhausted) {
      session_status_ = maho_ai::mojom::SessionStatus::kError;
    }
    if (page_) {
      page_->OnRuntimeEvent(std::move(status_event));
    }
  }

  EmitSessionUpdated();
}

void MahoAIPageHandler::PersistSessionList(bool synchronous) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
    return;
  }
  session_list_weak_factory_.InvalidateWeakPtrs();

  // Trim `persisted_sessions_` in-memory to prevent unbounded growth.
  // The total number of sessions persisted on disk is capped at
  // kMaxPersistedSessions. If there is an active session, it takes 1 slot,
  // leaving kMaxPersistedSessions - 1 slots for historical sessions.
  size_t max_historical = active_session_id_.empty()
                              ? kMaxPersistedSessions
                              : (kMaxPersistedSessions - 1);
  if (persisted_sessions_.size() > max_historical) {
    persisted_sessions_.resize(max_historical);
  }

  base::ListValue list;
  std::vector<std::string> kept_ids;

  if (!active_session_id_.empty()) {
    kept_ids.push_back(active_session_id_);
    base::DictValue entry;
    entry.Set("session_id", active_session_id_);
    entry.Set("created_at", session_created_at_);
    entry.Set("updated_at", session_updated_at_);
    entry.Set("adapter_name", runtime_router_->GetActiveAdapterName());
    entry.Set("status", static_cast<int>(session_status_));
    entry.Set("event_count", static_cast<int>(session_event_log_.size()));
    entry.Set("tool_call_count", static_cast<int>(tool_call_count_));
    if (!session_title_.empty()) {
      entry.Set("title", session_title_);
    }
    if (!runtime_session_id_.empty()) {
      entry.Set("runtime_session_id", runtime_session_id_);
    }
    if (!last_runtime_state_.empty()) {
      entry.Set("last_runtime_state", last_runtime_state_);
    }
    list.Append(std::move(entry));
  }

  for (const auto& s : persisted_sessions_) {
    if (list.size() >= kMaxPersistedSessions) {
      break;
    }
    if (s->session_id == active_session_id_) {
      continue;
    }
    kept_ids.push_back(s->session_id);
    base::DictValue entry;
    entry.Set("session_id", s->session_id);
    entry.Set("created_at", s->created_at);
    entry.Set("adapter_name", s->adapter_name);
    entry.Set("status", static_cast<int>(s->status));
    entry.Set("event_count", static_cast<int>(s->event_count));
    entry.Set("tool_call_count", static_cast<int>(s->tool_call_count));
    if (s->title.has_value()) {
      entry.Set("title", *s->title);
    }
    if (s->updated_at.has_value()) {
      entry.Set("updated_at", *s->updated_at);
    }
    if (s->runtime_session_id.has_value()) {
      entry.Set("runtime_session_id", *s->runtime_session_id);
    }
    if (s->last_runtime_state.has_value()) {
      entry.Set("last_runtime_state", *s->last_runtime_state);
    }
    list.Append(std::move(entry));
  }

  if (synchronous) {
    std::string json = SerializeValueToJson(base::Value(std::move(list)));
    prefs_->SetString(ai::kSessionListJson, json);

    if (event_log_persistence_) {
      event_log_persistence_->RetainSessions(kept_ids);
    }
    ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
    std::vector<std::string> orphans_to_remove;
    std::unordered_set<std::string> kept_set(kept_ids.begin(), kept_ids.end());
    for (auto it : update.Get()) {
      if (kept_set.find(it.first) == kept_set.end()) {
        orphans_to_remove.push_back(it.first);
      }
    }
    for (const auto& orphan : orphans_to_remove) {
      update->Remove(orphan);
    }
  } else {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&SerializeValueToJson, base::Value(std::move(list))),
        base::BindOnce(&MahoAIPageHandler::OnSessionListSerialized,
                       session_list_weak_factory_.GetWeakPtr(),
                       std::move(kept_ids)));
  }
}

void MahoAIPageHandler::OnSessionListSerialized(
    std::vector<std::string> kept_ids,
    std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
    return;
  }
  prefs_->SetString(ai::kSessionListJson, json);

  if (event_log_persistence_) {
    event_log_persistence_->RetainSessions(kept_ids);
  }
  ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
  std::vector<std::string> orphans_to_remove;
  std::unordered_set<std::string> kept_set(kept_ids.begin(), kept_ids.end());
  for (auto it : update.Get()) {
    if (kept_set.find(it.first) == kept_set.end()) {
      orphans_to_remove.push_back(it.first);
    }
  }
  for (const auto& orphan : orphans_to_remove) {
    update->Remove(orphan);
  }
}

void MahoAIPageHandler::LoadPersistedSessions() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  persisted_sessions_.clear();

  const std::string& json = prefs_->GetString(ai::kSessionListJson);
  if (json.empty()) {
    return;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  for (const auto& item : parsed->GetList()) {
    if (persisted_sessions_.size() >= kMaxPersistedSessions) {
      break;
    }
    if (!item.is_dict()) {
      continue;
    }
    const base::DictValue& dict = item.GetDict();
    auto info = maho_ai::mojom::SessionInfo::New();
    const std::string* sid = dict.FindString("session_id");
    if (!sid) {
      continue;
    }
    info->session_id = *sid;
    info->created_at = dict.FindDouble("created_at").value_or(0);
    const std::string* adapter = dict.FindString("adapter_name");
    info->adapter_name = adapter ? *adapter : "";
    info->is_active = false;
    info->is_read_only = true;
    info->status = static_cast<maho_ai::mojom::SessionStatus>(
        dict.FindInt("status").value_or(
            static_cast<int>(maho_ai::mojom::SessionStatus::kCompleted)));
    info->event_count =
        static_cast<uint32_t>(dict.FindInt("event_count").value_or(0));
    info->tool_call_count =
        static_cast<uint32_t>(dict.FindInt("tool_call_count").value_or(0));

    const std::string* title = dict.FindString("title");
    if (title) {
      info->title = *title;
    }
    auto updated_at = dict.FindDouble("updated_at");
    if (updated_at.has_value()) {
      info->updated_at = *updated_at;
    }
    const std::string* rsid = dict.FindString("runtime_session_id");
    if (rsid) {
      info->runtime_session_id = *rsid;
    }
    const std::string* lrs = dict.FindString("last_runtime_state");
    if (lrs) {
      info->last_runtime_state = *lrs;
    } else {
      info->last_runtime_state = "disconnected";
    }

    persisted_sessions_.push_back(std::move(info));
  }

  // The panel's WebUI (and therefore this handler) is destroyed whenever the
  // panel is closed, so without adoption every reopen would start an empty
  // conversation even though the previous one is still persisted. Adopt the
  // most recent persisted session as the live one so the conversation
  // continues until the user explicitly starts a new chat.
  AdoptMostRecentPersistedSessionAsActive();

  // Orphan event logs cleanup on startup.
  {
    std::unordered_set<std::string> valid_ids;
    if (!active_session_id_.empty()) {
      valid_ids.insert(active_session_id_);
    }
    for (const auto& s : persisted_sessions_) {
      valid_ids.insert(s->session_id);
    }
    std::vector<std::string> orphans_to_remove;
    {
      const auto& dict = prefs_->GetDict(ai::kSessionEventLogs);
      for (auto it : dict) {
        if (valid_ids.find(it.first) == valid_ids.end()) {
          orphans_to_remove.push_back(it.first);
        }
      }
    }
    if (!orphans_to_remove.empty()) {
      ScopedDictPrefUpdate update(prefs_, ai::kSessionEventLogs);
      for (const auto& orphan : orphans_to_remove) {
        update->Remove(orphan);
      }
    }
  }
}

void MahoAIPageHandler::AdoptMostRecentPersistedSessionAsActive() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!active_session_id_.empty() || persisted_sessions_.empty()) {
    return;
  }
  if (!prefs_->GetBoolean(ai::kSessionPersistenceEnabled)) {
    return;
  }

  // `persisted_sessions_` is newest-first, so the front entry is the session
  // the user was last talking to.
  const maho_ai::mojom::SessionInfoPtr& newest = persisted_sessions_.front();
  if (newest->session_id.empty()) {
    return;
  }

  bool unavailable = false;
  std::vector<maho_ai::mojom::RuntimeEventPtr> replay =
      LoadPersistedEventLog(newest->session_id, &unavailable);
  if (unavailable || replay.empty()) {
    // Nothing to continue: leave the panel on a genuinely empty conversation
    // so the empty-state quick actions remain available.
    return;
  }

  active_session_id_ = newest->session_id;
  session_created_at_ = newest->created_at;
  session_event_log_ = std::move(replay);
  maho::ai::NormalizeReplayWindow(session_event_log_, replay_budget_);
  session_status_ = maho_ai::mojom::SessionStatus::kIdle;
  persisted_sessions_.erase(persisted_sessions_.begin());

  MahoAiRuntimeAdapter* adapter =
      runtime_adapter_for_testing_
          ? runtime_adapter_for_testing_.get()
          : (runtime_router_ ? runtime_router_->GetActiveAdapter() : nullptr);
  if (adapter) {
    adapter->StartSession(active_session_id_);
  }
}

void MahoAIPageHandler::PersistEventLog(const std::string& session_id,
                                        bool synchronous) {
  if (session_id == active_session_id_) {
    PersistEventLogForSession(session_id, session_event_log_, synchronous);
  }
}

void MahoAIPageHandler::PersistEventLogWithReplyForTesting(
    const std::string& session_id,
    base::OnceCallback<void(bool)> completed) {
  if (!event_log_persistence_ || session_id != active_session_id_) {
    std::move(completed).Run(false);
    return;
  }
  event_log_persistence_->Persist(session_id, session_event_log_, {},
                                   std::move(completed));
}

void MahoAIPageHandler::PersistEventLogForSession(
    const std::string& session_id,
    const std::vector<maho_ai::mojom::RuntimeEventPtr>& log,
    bool synchronous) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (session_id.empty() || !event_log_persistence_) {
    return;
  }
  event_log_persistence_->Persist(
      session_id, log, synchronous ? MahoAIEventLogPersistence::ReplyGate{}
                                  : event_log_reply_gate_for_testing_);
}

std::vector<maho_ai::mojom::RuntimeEventPtr>
MahoAIPageHandler::LoadPersistedEventLog(const std::string& session_id,
                                       bool* unavailable) {
  if (unavailable) {
    *unavailable = false;
  }
  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  auto reject = [&] {
    if (unavailable) {
      *unavailable = true;
    }
    auto error = maho_ai::mojom::RuntimeEvent::New();
    error->kind = maho_ai::mojom::RuntimeEventKind::kError;
    error->session_id = session_id;
    error->text = "history_unavailable";
    events.clear();
    events.push_back(std::move(error));
  };
  maho::ai::RuntimeReplayBudget budget;
  const auto pending = event_log_persistence_
                           ? event_log_persistence_->Pending(session_id)
                           : nullptr;
  if (pending) {
    for (const auto& item : *pending) {
      auto event = item->Clone();
      maho::ai::AppendBoundedReplayEvent(events, budget, event);
    }
    return events;
  }

  const auto& dict = prefs_->GetDict(ai::kSessionEventLogs);
  const std::string* json = dict.FindString(session_id);
  if (!json || json->empty()) {
    return events;
  }

  if (json->size() > maho::ai::kReplayBytes) {
    reject();
    return events;
  }
  auto parsed = base::JSONReader::Read(*json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    reject();
    return events;
  }

  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    auto event = DictToRuntimeEvent(item.GetDict(), session_id);
    if (event) {
      maho::ai::AppendBoundedReplayEvent(events, budget, event);
    }
  }
  return events;
}

void MahoAIPageHandler::ClearPersistedEventLog(const std::string& session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session_id.empty() && event_log_persistence_) {
    event_log_persistence_->Clear(session_id);
  }
}

void MahoAIPageHandler::UpdateSessionTitle(const std::string& text) {
  if (session_title_.empty() && !text.empty()) {
    session_title_ = DeriveTitle(text);
  }
}

// ==========================================================================
// Tab Tidy implementation — default-ON, no pref gate.
// ==========================================================================

void MahoAIPageHandler::RequestTabTidy(
    std::vector<maho_ai::mojom::TabInfoPtr> tabs,
    RequestTabTidyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    std::move(callback).Run({});
    DenyAndResetConnection();
    return;
  }

  // Return empty result immediately if no tabs or core unavailable.
  if (tabs.empty()) {
    std::move(callback).Run({});
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run({});
    return;
  }

  // Determine the active space ID from the browser context.
  std::string space_id;
  if (browser_) {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    if (bridge) {
      space_id = bridge->GetActiveSpaceId(browser_);
    }
  }
  if (space_id.empty()) {
    std::move(callback).Run({});
    return;
  }

  // Dispatch request_tidy_tabs shell event to Rust core.
  base::DictValue event_dict;
  event_dict.Set("kind", "request_tidy_tabs");
  event_dict.Set("space_id", space_id);
  std::string event_json;
  base::JSONWriter::Write(event_dict, &event_json);

  std::string updates = maho::core::HandleEvent(core, event_json.c_str());
  maho::InvalidateSidebarCoreCacheForUpdatesJson(updates);

  // Parse the request_llm_completion update from the core.
  std::optional<base::Value> parsed =
      base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(callback).Run({});
    return;
  }

  const base::DictValue* llm_request = nullptr;
  if (parsed->is_list()) {
    for (const auto& item : parsed->GetList()) {
      const auto* d = item.GetIfDict();
      if (d) {
        const std::string* kind = d->FindString("kind");
        if (kind && *kind == "request_llm_completion") {
          llm_request = d;
          break;
        }
      }
    }
  } else if (parsed->is_dict()) {
    const std::string* kind = parsed->GetDict().FindString("kind");
    if (kind && *kind == "request_llm_completion") {
      llm_request = &parsed->GetDict();
    }
  }

  if (!llm_request) {
    std::move(callback).Run({});
    return;
  }

  const std::string* request_id_ptr = llm_request->FindString("request_id");
  if (!request_id_ptr) {
    std::move(callback).Run({});
    return;
  }
  std::string request_id = *request_id_ptr;

  auto prompt_messages = maho::ai::ExtractPromptMessages(*llm_request);
  if (!prompt_messages) {
    std::move(callback).Run({});
    return;
  }

  std::string req_type_str;
  if (const std::string* rt = llm_request->FindString("request_type")) {
    req_type_str = *rt;
  }
  maho::ai::MahoAiTask task = maho::ai::MahoAiTask::kTabTidy;
  if (req_type_str == "tidy_tabs" || req_type_str == "TidyTabs") {
    task = maho::ai::MahoAiTask::kTabTidy;
  } else if (req_type_str == "tidy_tab_title" || req_type_str == "TidyTabTitle") {
    task = maho::ai::MahoAiTask::kTabTitle;
  } else if (req_type_str == "tidy_download" || req_type_str == "TidyDownload") {
    task = maho::ai::MahoAiTask::kDownloadTidy;
  } else if (req_type_str == "page_preview" || req_type_str == "PagePreview") {
    task = maho::ai::MahoAiTask::kPagePreview;
  } else if (req_type_str == "chat_completion" || req_type_str == "ChatCompletion") {
    task = maho::ai::MahoAiTask::kChat;
  }

  auto route_res = maho::ai::ResolveMahoAiModelRoute(
      browser_->GetProfile()->GetPrefs(), task);
  if (!route_res.is_ok()) {
    LOG(ERROR) << "MahoAIPageHandler::RequestTabTidy: No route for task "
               << TaskToString(task);
    std::move(callback).Run({});
    return;
  }

  // Build and fire the LLM completion request.
  auto* url_loader_factory = browser_->GetProfile()
                                 ->GetDefaultStoragePartition()
                                 ->GetURLLoaderFactoryForBrowserProcess()
                                 .get();
  auto llm_client = std::make_unique<MahoAiLlmClient>(
      browser_->GetProfile()->GetPrefs(), url_loader_factory,
      encryptor_ ? &*encryptor_ : nullptr,
      /*provider_adapter_v2_enabled=*/false,
      base::BindRepeating(&MahoAIPageHandler::RevalidateAiThunk,
                          weak_factory_.GetWeakPtr()));

  MahoAiLlmClient::CompletionRequest request;
  request.provider_id = route_res.route.provider_id;
  request.endpoint = route_res.route.endpoint;
  request.model = route_res.route.model_id;
  request.prompt_messages = base::ListValue(std::move(*prompt_messages));

  auto* raw_client = llm_client.get();
  raw_client->StartCompletion(
      std::move(request),
      /*on_token=*/base::BindRepeating([](const std::string&) {}),
      /*on_complete=*/
      base::BindOnce(
          [](base::WeakPtr<MahoAIPageHandler> handler,
             std::unique_ptr<MahoAiLlmClient> /*client*/,
             std::string space_id_arg, std::string request_id_arg,
             RequestTabTidyCallback cb,
             MahoAiLlmClient::CompletionResult result) {
            if (!handler) {
              std::move(cb).Run({});
              return;
            }

            MahoCore* core2 = maho::GetCore();
            if (!core2) {
              std::move(cb).Run({});
              return;
            }

            // Send LLM result back to Rust to get tidy_tabs_ready.
            base::DictValue llm_result_dict;
            llm_result_dict.Set("kind", "llm_result");
            llm_result_dict.Set("request_id", request_id_arg);
            llm_result_dict.Set("result", result.full_text);
            std::string llm_result_json;
            base::JSONWriter::Write(llm_result_dict, &llm_result_json);

            std::string updates2 =
                maho::core::HandleEvent(core2, llm_result_json.c_str());
            maho::InvalidateSidebarCoreCacheForUpdatesJson(updates2);

            // Parse tidy_tabs_ready from the updates.
            std::optional<base::Value> parsed2 =
                base::JSONReader::Read(updates2, base::JSON_PARSE_RFC);
            if (!parsed2) {
              std::move(cb).Run({});
              return;
            }

            const base::DictValue* tidy_ready = nullptr;
            if (parsed2->is_list()) {
              for (const auto& item : parsed2->GetList()) {
                const auto* d = item.GetIfDict();
                if (d) {
                  const std::string* kind = d->FindString("kind");
                  if (kind && *kind == "tidy_tabs_ready") {
                    tidy_ready = d;
                    break;
                  }
                }
              }
            } else if (parsed2->is_dict()) {
              const std::string* kind = parsed2->GetDict().FindString("kind");
              if (kind && *kind == "tidy_tabs_ready") {
                tidy_ready = &parsed2->GetDict();
              }
            }

            if (!tidy_ready) {
              std::move(cb).Run({});
              return;
            }

            const base::ListValue* raw_folders =
                tidy_ready->FindList("folders");
            if (!raw_folders) {
              std::move(cb).Run({});
              return;
            }

            // Convert to Mojo TidyFolder array.
            std::vector<maho_ai::mojom::TidyFolderPtr> mojo_folders;
            for (const auto& folder_val : *raw_folders) {
              const auto* folder_dict = folder_val.GetIfDict();
              if (!folder_dict) {
                continue;
              }
              const std::string* name = folder_dict->FindString("name");
              const base::ListValue* tab_ids = folder_dict->FindList("tab_ids");
              if (!name || !tab_ids) {
                continue;
              }
              auto mojo_folder = maho_ai::mojom::TidyFolder::New();
              mojo_folder->name = *name;
              for (const auto& id_val : *tab_ids) {
                const std::string* tid = id_val.GetIfString();
                if (tid) {
                  mojo_folder->tab_ids.push_back(*tid);
                }
              }
              mojo_folders.push_back(std::move(mojo_folder));
            }

            if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
                bridge) {
              bridge->NotifyChanged();
            }

            std::move(cb).Run(std::move(mojo_folders));
          },
          weak_factory_.GetWeakPtr(), std::move(llm_client), space_id,
          request_id, std::move(callback)),
      /*on_error=*/
      base::BindOnce(
          [](base::WeakPtr<MahoAIPageHandler> handler,
             std::unique_ptr<MahoAiLlmClient> /*client*/,
             RequestTabTidyCallback cb, const std::string& error) {
            LOG(ERROR) << "MahoAIPageHandler::RequestTabTidy LLM error: "
                       << error;
            std::move(cb).Run({});
          },
          weak_factory_.GetWeakPtr(), std::move(llm_client),
          std::move(callback)));
}

void MahoAIPageHandler::ApplyTabTidyFolders(
    std::vector<maho_ai::mojom::TidyFolderPtr> folders) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    DenyAndResetConnection();
    return;
  }

  if (folders.empty()) {
    return;
  }

  std::string space_id;
  if (browser_) {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    if (bridge) {
      space_id = bridge->GetActiveSpaceId(browser_);
    }
  }
  if (space_id.empty()) {
    return;
  }

  // Dispatch apply_tidy_tabs shell event — Rust core creates folders and
  // moves tabs using the pending tidy state set by request_tidy_tabs.
  base::DictValue event_dict;
  event_dict.Set("kind", "apply_tidy_tabs");
  event_dict.Set("space_id", space_id);
  std::string event_json;
  base::JSONWriter::Write(event_dict, &event_json);

  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  std::string updates = maho::core::HandleEvent(core, event_json.c_str());
  maho::InvalidateSidebarCoreCacheForUpdatesJson(updates);

  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
}

// ==========================================================================
// PAYG credit balance implementation
// ==========================================================================

void MahoAIPageHandler::GetCreditBalance(GetCreditBalanceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    auto info = maho_ai::mojom::CreditBalanceInfo::New();
    info->balance_usd = 0.0f;
    info->last_purchase_at = 0;
    info->last_consumption_at = 0;
    std::move(callback).Run(std::move(info));
    DenyAndResetConnection();
    return;
  }

  std::string relay_url = maho::auth::GetRelayBaseUrl();

  std::string access_token;
  if (encryptor_) {
    access_token = maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  }

  // Return zero balance immediately if there is no token — anonymous user.
  if (access_token.empty()) {
    auto info = maho_ai::mojom::CreditBalanceInfo::New();
    info->balance_usd = 0.0f;
    info->last_purchase_at = 0;
    info->last_consumption_at = 0;
    std::move(callback).Run(std::move(info));
    return;
  }

  // /billing/credit_balance is service-token-gated (billing-proxy only) and
  // rejects a user token; /auth/subscription is the user-token credit route.
  GURL request_url(relay_url + "/auth/subscription");
  if (!request_url.is_valid()) {
    auto info = maho_ai::mojom::CreditBalanceInfo::New();
    info->balance_usd = 0.0f;
    info->last_purchase_at = 0;
    info->last_consumption_at = 0;
    std::move(callback).Run(std::move(info));
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "GET";
  resource_request->headers.SetHeader("Authorization",
                                      "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  scoped_refptr<network::SharedURLLoaderFactory> factory = url_loader_factory_;
  if (!factory && browser_) {
    factory = browser_->GetProfile()
                  ->GetDefaultStoragePartition()
                  ->GetURLLoaderFactoryForBrowserProcess();
  }
  if (!factory) {
    auto info = maho_ai::mojom::CreditBalanceInfo::New();
    info->balance_usd = 0.0f;
    info->last_purchase_at = 0;
    info->last_consumption_at = 0;
    std::move(callback).Run(std::move(info));
    return;
  }

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kCreditBalanceTrafficAnnotation);
  url_loader->SetRetryOptions(
      /*max_retries=*/1, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);

  // Transfer ownership of loader into the callback so it stays alive during
  // the async download. The callback is bound to a WeakPtr; if the handler
  // is destroyed the loader is simply dropped.
  auto* raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      factory.get(),
      base::BindOnce(
          [](base::WeakPtr<MahoAIPageHandler> handler,
             std::unique_ptr<network::SimpleURLLoader> loader,
             GetCreditBalanceCallback cb,
             std::optional<std::string> response_body) {
            auto info = maho_ai::mojom::CreditBalanceInfo::New();
            info->balance_usd = 0.0f;
            info->last_purchase_at = 0;
            info->last_consumption_at = 0;

            if (!handler) {
              std::move(cb).Run(std::move(info));
              return;
            }

            int net_error = loader->NetError();
            const network::mojom::URLResponseHead* response_info =
                loader->ResponseInfo();
            int http_status =
                response_info ? response_info->headers->response_code() : 0;

            if (net_error != net::OK || http_status != 200 ||
                !response_body.has_value()) {
              // A rejected token (401/403) means the relay session expired, not
              // a real $0 balance. Surface it so the UI can prompt re-login
              // instead of showing a fabricated zero.
              info->session_expired =
                  (http_status == 401 || http_status == 403);
              DVLOG(1) << "MahoAIPageHandler::GetCreditBalance: request failed "
                       << "(net=" << net_error << " http=" << http_status
                       << " session_expired=" << info->session_expired << ")";
              std::move(cb).Run(std::move(info));
              return;
            }

            auto parsed =
                base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict()) {
              std::move(cb).Run(std::move(info));
              return;
            }

            const base::DictValue& dict = parsed->GetDict();
            std::optional<double> balance =
                dict.FindDouble("credit_balance_usd");
            if (balance.has_value()) {
              info->balance_usd = static_cast<float>(*balance);
            }
            std::optional<double> last_purchase =
                dict.FindDouble("last_purchase_at");
            if (last_purchase.has_value()) {
              info->last_purchase_at = static_cast<int64_t>(*last_purchase);
            }
            std::optional<double> last_consumption =
                dict.FindDouble("last_consumption_at");
            if (last_consumption.has_value()) {
              info->last_consumption_at =
                  static_cast<int64_t>(*last_consumption);
            }

            std::move(cb).Run(std::move(info));
          },
          weak_factory_.GetWeakPtr(), std::move(url_loader),
          std::move(callback)),
      /*max_body_size=*/4096);
}

void MahoAIPageHandler::GetBuyCreditsUrl(int32_t pack_size_usd,
                                         GetBuyCreditsUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Single "Maho AI Pay-as-you-go credits" pay-what-you-want product: the buyer
  // chooses the amount in the LemonSqueezy checkout, so pack_size_usd no longer
  // selects a fixed pack. The relay webhook credits the amount actually paid.
  const std::string user_id =
      prefs_ ? prefs_->GetString(maho::account_prefs::kRelayUserId)
             : std::string();
  std::move(callback).Run(maho::webui::BuildPaygCreditsCheckoutUrl(user_id));
}

void MahoAIPageHandler::CreateAiProfile(const std::string& name,
                                        const std::string& system_prompt,
                                        const std::optional<std::string>& model,
                                        CreateAiProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  const char* model_ptr = model.has_value() ? model->c_str() : nullptr;
  char* p = maho_ai_profile_create(core, name.c_str(), system_prompt.c_str(),
                                   model_ptr);
  if (!p) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::string json(p);
  maho_core_free_string(p);

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_dict()) {
    const std::string* id = parsed->GetDict().FindString("id");
    if (id) {
      std::move(callback).Run(*id);
      return;
    }
  }
  std::move(callback).Run(std::nullopt);
}

void MahoAIPageHandler::GetAiProfiles(GetAiProfilesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run("[]");
    return;
  }
  char* p = maho_ai_profile_list(core);
  if (!p) {
    std::move(callback).Run("[]");
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::UpdateAiProfile(const std::string& id,
                                        const std::string& profile_json,
                                        UpdateAiProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success = maho_ai_profile_update(core, id.c_str(), profile_json.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::DeleteAiProfile(const std::string& id,
                                        DeleteAiProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success = maho_ai_profile_delete(core, id.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::ImportProfileFromToml(
    const std::string& workspace_id,
    const std::string& toml_path,
    ImportProfileFromTomlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  char* p = maho_ai_profile_import_toml(core, workspace_id.c_str(),
                                        toml_path.c_str());
  if (!p) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::string result(p);
  maho_core_free_string(p);
  std::move(callback).Run(result);
}

void MahoAIPageHandler::CreateAiWorkspace(
    const std::string& name,
    const std::optional<std::string>& space_id,
    CreateAiWorkspaceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  const char* space_ptr = space_id.has_value() ? space_id->c_str() : nullptr;
  char* p = maho_ai_workspace_create(core, name.c_str(), space_ptr);
  if (!p) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::GetActiveAiWorkspace(
    const std::string& space_id,
    GetActiveAiWorkspaceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  char* p = maho_ai_workspace_get_by_space(core, space_id.c_str());
  if (!p) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::GetAiWorkspaces(GetAiWorkspacesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run("[]");
    return;
  }
  char* p = maho_ai_workspace_list(core);
  if (!p) {
    std::move(callback).Run("[]");
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::SwitchWorkspaceProfile(
    const std::string& workspace_id,
    const std::string& profile_id,
    SwitchWorkspaceProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success = maho_ai_workspace_switch_profile(core, workspace_id.c_str(),
                                                  profile_id.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::RegisterMcpServer(const std::string& workspace_id,
                                          const std::string& config_json,
                                          RegisterMcpServerCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success = maho_ai_mcp_server_register(core, workspace_id.c_str(),
                                             config_json.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::RemoveMcpServer(const std::string& workspace_id,
                                        const std::string& server_name,
                                        RemoveMcpServerCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success = maho_ai_mcp_server_remove(core, workspace_id.c_str(),
                                           server_name.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::GetMcpServers(const std::string& workspace_id,
                                      GetMcpServersCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run("[]");
    return;
  }
  char* p = maho_ai_mcp_server_list(core, workspace_id.c_str());
  if (!p) {
    std::move(callback).Run("[]");
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::ApproveMcpServerTrust(
    const std::string& workspace_id,
    const std::string& server_name,
    const std::vector<std::string>& tools,
    ApproveMcpServerTrustCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  std::string tools_json;
  base::ListValue list;
  for (const auto& tool : tools) {
    list.Append(tool);
  }
  base::JSONWriter::Write(list, &tools_json);

  bool success = maho_ai_mcp_server_approve_trust(
      core, workspace_id.c_str(), server_name.c_str(), tools_json.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::RegisterCliTool(const std::string& workspace_id,
                                        const std::string& tool_json,
                                        RegisterCliToolCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success =
      maho_ai_cli_tool_register(core, workspace_id.c_str(), tool_json.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::RemoveCliTool(const std::string& workspace_id,
                                      const std::string& tool_name,
                                      RemoveCliToolCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  bool success =
      maho_ai_cli_tool_remove(core, workspace_id.c_str(), tool_name.c_str());
  std::move(callback).Run(success);
}

void MahoAIPageHandler::GetCliTools(const std::string& workspace_id,
                                    GetCliToolsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run("[]");
    return;
  }
  char* p = maho_ai_cli_tool_list(core, workspace_id.c_str());
  if (!p) {
    std::move(callback).Run("[]");
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::GetWorkspaceEffectiveTools(
    const std::string& workspace_id,
    GetWorkspaceEffectiveToolsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !IsAiAllowed()) {
    std::move(callback).Run("[]");
    return;
  }
  char* p = maho_ai_workspace_get_tools(core, workspace_id.c_str());
  if (!p) {
    std::move(callback).Run("[]");
    return;
  }
  std::string json(p);
  maho_core_free_string(p);
  std::move(callback).Run(json);
}

void MahoAIPageHandler::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
}

void MahoAIPageHandler::StartVoiceSession(StartVoiceSessionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed() || !browser_) {
    std::move(callback).Run(false);
    return;
  }
  if (!voice_session_) {
    voice_session_ = std::make_unique<MahoAiVoiceSession>(
        browser_->GetProfile(), url_loader_factory_,
        base::BindRepeating(&MahoAIPageHandler::OnVoiceFinalTranscript,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(&MahoAIPageHandler::OnVoiceSessionError,
                            weak_factory_.GetWeakPtr()));
  }
  voice_session_->Start(std::move(callback));
}

void MahoAIPageHandler::PushAudioChunk(const std::vector<float>& pcm16k) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (voice_session_) {
    voice_session_->PushAudio(pcm16k);
  }
}

void MahoAIPageHandler::StopVoiceSession() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (voice_session_) {
    voice_session_->Stop();
  }
}

void MahoAIPageHandler::OnVoiceFinalTranscript(const std::string& transcript) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_) {
    page_->OnVoiceFinal(transcript);
  }
}

void MahoAIPageHandler::OnVoiceSessionError(const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_) {
    page_->OnVoiceError(message);
  }
}

void MahoAIPageHandler::OnAskMahoDispatch(
    const maho_ai::mojom::AskMahoDispatch& dispatch,
    uint64_t delivery_id,
    maho::MahoAiIngressCoordinator::AcceptanceCallback accept_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed() || !page_.is_bound()) {
    DenyAndResetConnection();
    return;
  }
  DCHECK(dispatch.submit);
  DCHECK(!dispatch.target_session_id.has_value());
  DCHECK_EQ(dispatch.mode, maho_ai::mojom::InteractionMode::kAssistant);

  active_ingress_request_id_ = dispatch.request_id;
  active_ingress_delivery_id_ = delivery_id;
  auto session_info = StartSessionInternal(std::nullopt, dispatch.mode);
  const std::string session_id = session_info->session_id;

  SubmitPromptInternal(
      session_id, dispatch.query,
      /*attach_browser_context=*/false, dispatch.mode,
      /*attachments=*/std::nullopt, maho_ai::mojom::ChatIntent::kFreeform,
      Correlation{active_session_id_, dispatch.request_id},
      base::BindOnce(
          [](base::WeakPtr<MahoAIPageHandler> self, std::string request_id,
             std::string session_id, maho_ai::mojom::SessionInfoPtr s_info,
             maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb,
             bool accepted) {
            if (!accepted || !self || !self->page_.is_bound()) {
              return false;
            }
            self->page_->OnAskMahoSessionAccepted(request_id,
                                                  std::move(s_info));
            std::move(accept_cb).Run(session_id);
            return true;
          },
          weak_factory_.GetWeakPtr(), dispatch.request_id, session_id,
          std::move(session_info), std::move(accept_callback)));
}
