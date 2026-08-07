// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/ai/maho_unified_agent_adapter.h"

#include <atomic>
#include <cstring>
#include <fstream>
#include <memory>

#include "base/base64.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_finder.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"  // nogncheck
#include "content/public/browser/browser_thread.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/ai/maho_browser_tool_executor.h"  // nogncheck
#include "maho/browser/mcp/maho_mcp_session.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "third_party/crashpad/crashpad/client/annotation.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "ui/base/base_window.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/no_destructor.h"
#include "base/synchronization/waitable_event.h"
#include "content/public/browser/browser_thread.h"

namespace {

std::string GenerateTaskId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

std::string NormalizeApprovalPolicyForPayload(const std::string& policy) {
  std::string normalized = base::ToLowerASCII(policy);
  normalized = base::TrimWhitespaceASCII(normalized, base::TRIM_ALL);
  if (normalized == "prompt" || normalized == "ask" ||
      normalized == "default") {
    return "prompt";
  }
  if (normalized == "allow" || normalized == "allow-all" ||
      normalized == "allow_all" || normalized == "yolo") {
    return "allow_all";
  }
  if (normalized == "allow-mcp" || normalized == "allow_mcp") {
    return "allow_mcp";
  }
  if (normalized == "deny-sensitive" || normalized == "deny_sensitive") {
    return "deny_sensitive";
  }
  if (normalized == "deny" || normalized == "deny-all" ||
      normalized == "deny_all") {
    return "deny_all";
  }
  return "deny_all";
}

std::string CurrentApprovalPolicyForPayload(PrefService* prefs) {
  if (!prefs) {
    return "prompt";
  }
  return NormalizeApprovalPolicyForPayload(
      prefs->GetString(maho::ai_prefs::kApprovalPolicy));
}

std::string SensitivityForToolPayload(const std::string& tool) {
  const maho::ai::BrowserActionContract* contract =
      maho::ai::FindBrowserActionContract(tool);
  if (contract &&
      contract->sensitivity == maho::ai::BrowserActionSensitivity::kLow) {
    return "read_only";
  }
  return "sensitive";
}

bool ExtractPageDerivedJustification(const char* arguments) {
  if (!arguments || !*arguments) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(arguments, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const base::DictValue& dict = parsed->GetDict();
  return dict.FindBool("page_derived_justification")
      .value_or(dict.FindBool("pageDerivedJustification").value_or(false));
}

constexpr char kSafeCredentialFailureText[] =
    "Your saved AI credential could not be used.";

struct CredentialEnvelopeParseResult {
  bool is_credential_envelope = false;
  std::optional<MahoAiRuntimeErrorCode> error_code;
};

std::optional<MahoAiRuntimeErrorCode> CredentialErrorCodeFromString(
    const std::string& code) {
  if (code == "provider_not_configured") {
    return MahoAiRuntimeErrorCode::kProviderNotConfigured;
  }
  if (code == "credential_unusable") {
    return MahoAiRuntimeErrorCode::kCredentialUnusable;
  }
  if (code == "secure_store_unavailable") {
    return MahoAiRuntimeErrorCode::kSecureStoreUnavailable;
  }
  if (code == "credential_decrypt_failed") {
    return MahoAiRuntimeErrorCode::kCredentialDecryptFailed;
  }
  if (code == "managed_auth_unavailable") {
    return MahoAiRuntimeErrorCode::kManagedAuthUnavailable;
  }
  if (code == "unsupported_provider") {
    return MahoAiRuntimeErrorCode::kUnsupportedProvider;
  }
  return std::nullopt;
}

CredentialEnvelopeParseResult ParseCredentialErrorEnvelope(
    const std::string& error) {
  CredentialEnvelopeParseResult result;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(error, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return result;
  }
  const base::DictValue& dict = parsed->GetDict();
  const std::string* kind = dict.FindString("kind");
  if (!kind || *kind != "credential_error") {
    return result;
  }

  result.is_credential_envelope = true;
  if (dict.size() != 3 || dict.FindInt("version") != 1) {
    return result;
  }
  const std::string* code = dict.FindString("code");
  if (!code) {
    return result;
  }
  result.error_code = CredentialErrorCodeFromString(*code);
  return result;
}

std::string ShapeOutboundMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent) {
  switch (chat_intent) {
    case maho_ai::mojom::ChatIntent::kSummarizeCurrentPage:
      return "[Instructions: Provide one main takeaway, followed by 3-7 concise key points summarizing the content. Preserve key names, dates, and numbers accurately. Do not invent facts not supported by the content.]\n\n" +
             message;
    case maho_ai::mojom::ChatIntent::kQuizCurrentPage:
      return "[Instructions: Based ONLY on the supplied page context, create a 5-question quiz to test understanding. Your initial response MUST contain the full text of question 1 and nothing else beyond it: do not announce what you are about to do, do not describe the quiz format, and do not state that you are waiting. Ask exactly one question per response. After each user answer, grade it, give a brief explanation, state the running score, and immediately ask the next single question in the same response. End the quiz after question 5 with a final score and summary of missed concepts.]\n\n" +
             message;
    case maho_ai::mojom::ChatIntent::kFreeform:
    default:
      return message;
  }
}

std::optional<std::string> PreferredProviderForRuntime(PrefService* prefs) {
  if (!prefs) {
    return std::nullopt;
  }
  const std::string provider = prefs->GetString(maho::ai_prefs::kProvider);
  if (provider.empty()) {
    return std::nullopt;
  }
  if (provider == "maho-managed") {
    return "managed";
  }
  if (provider == "openai-compatible") {
    return "openai";
  }
  return provider;
}

}  // namespace

namespace {
const MahoAgentFfi kProductionFfi = {
    &maho_agent_create_session_leased,
    &maho_agent_set_approval_policy,
    &maho_agent_session_set_preferred_provider,
    &maho_agent_send_message_leased,
    &maho_agent_cancel,
    &maho_agent_session_free,
    &maho_agent_set_artifact_root,
    &maho_agent_set_artifact_created_callback,
};
}  // namespace

const MahoAgentFfi* GetProductionMahoAgentFfi() {
  return &kProductionFfi;
}

PermissionCallState::PermissionCallState() = default;
PermissionCallState::~PermissionCallState() = default;

BrowserToolCallState::BrowserToolCallState() = default;
BrowserToolCallState::~BrowserToolCallState() = default;

MahoUnifiedAgentAdapter::ByokSnapshot::ByokSnapshot(
    const ByokSnapshot&) = default;
MahoUnifiedAgentAdapter::ByokSnapshot::ByokSnapshot(ByokSnapshot&&) =
    default;
MahoUnifiedAgentAdapter::ByokSnapshot&
MahoUnifiedAgentAdapter::ByokSnapshot::operator=(const ByokSnapshot&) =
    default;
MahoUnifiedAgentAdapter::ByokSnapshot&
MahoUnifiedAgentAdapter::ByokSnapshot::operator=(ByokSnapshot&&) = default;

MahoUnifiedAgentAdapter::TurnFfiResources::TurnFfiResources() = default;
MahoUnifiedAgentAdapter::TurnFfiResources::TurnFfiResources(
    TurnFfiResources&&) = default;
MahoUnifiedAgentAdapter::TurnFfiResources&
MahoUnifiedAgentAdapter::TurnFfiResources::operator=(TurnFfiResources&&) =
    default;
MahoUnifiedAgentAdapter::TurnFfiResources::~TurnFfiResources() = default;

MahoUnifiedAgentAdapter::ByokBridge::ByokBridge() = default;
MahoUnifiedAgentAdapter::ByokBridge::~ByokBridge() = default;

MahoUnifiedAgentAdapter::SessionFfiResources::SessionFfiResources() = default;
MahoUnifiedAgentAdapter::SessionFfiResources::SessionFfiResources(
    SessionFfiResources&&) = default;
MahoUnifiedAgentAdapter::SessionFfiResources&
MahoUnifiedAgentAdapter::SessionFfiResources::operator=(
    SessionFfiResources&&) = default;
MahoUnifiedAgentAdapter::SessionFfiResources::~SessionFfiResources() =
    default;

MahoUnifiedAgentAdapter::ActiveTurn::ActiveTurn() = default;
MahoUnifiedAgentAdapter::ActiveTurn::ActiveTurn(ActiveTurn&&) = default;
MahoUnifiedAgentAdapter::ActiveTurn&
MahoUnifiedAgentAdapter::ActiveTurn::operator=(ActiveTurn&&) = default;
MahoUnifiedAgentAdapter::ActiveTurn::~ActiveTurn() = default;

namespace {
base::Lock& GetActiveAdaptersLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}
} // namespace

std::set<MahoUnifiedAgentAdapter*>& MahoUnifiedAgentAdapter::GetActiveAdapters() {
  static base::NoDestructor<std::set<MahoUnifiedAgentAdapter*>> active_adapters;
  return *active_adapters;
}

void MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(const std::string& provider) {
  base::AutoLock lock(GetActiveAdaptersLock());
  for (auto* adapter : GetActiveAdapters()) {
    adapter->owning_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &MahoUnifiedAgentAdapter::ClearBYOKKeyOnOwningSequence,
            adapter->active_adapter_weak_ptr_, provider));
  }
}

void MahoUnifiedAgentAdapter::ClearBYOKKeyOnOwningSequence(
    std::string provider) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session_ffi_resources_ || !session_ffi_resources_->byok_snapshot ||
      !session_ffi_resources_->byok_bridge ||
      !session_ffi_resources_->byok_bridge->active.load(
          std::memory_order_acquire)) {
    return;
  }

  auto replacement = std::make_shared<ByokSnapshot>(
      *session_ffi_resources_->byok_snapshot);
  if (provider == "openai") {
    replacement->encrypted_byok_openai_b64.clear();
  } else if (provider == "anthropic") {
    replacement->encrypted_byok_anthropic_b64.clear();
  } else if (provider == "openai-compatible") {
    replacement->custom_api_key.clear();
  } else {
    return;
  }

  session_ffi_resources_->byok_snapshot = replacement;
  if (session_ffi_resources_->byok_bridge) {
    // A callback that already loaded the prior immutable snapshot may finish
    // with that copy. Every callback that starts after this publication sees
    // the replacement, without racing on either snapshot's strings.
    base::AutoLock lock(session_ffi_resources_->byok_bridge->lock);
    session_ffi_resources_->byok_bridge->snapshot = std::move(replacement);
  }
}

MahoUnifiedAgentAdapter::ByokSnapshot::ByokSnapshot() = default;

MahoUnifiedAgentAdapter::ByokSnapshot::~ByokSnapshot() = default;

MahoUnifiedAgentAdapter::MahoUnifiedAgentAdapter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    base::RepeatingCallback<bool()> ai_gate,
    base::RepeatingCallback<Browser*()> bound_browser_resolver,
    base::RepeatingCallback<base::expected<base::FilePath, std::string>()>
        artifact_root_resolver)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      ai_gate_(std::move(ai_gate)),
      bound_browser_resolver_(std::move(bound_browser_resolver)),
      artifact_root_resolver_(std::move(artifact_root_resolver)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  agent_ffi_ = GetProductionMahoAgentFfi();
  owning_task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();
  active_adapter_weak_ptr_ = weak_factory_.GetWeakPtr();
  tool_availability_handle_ =
      std::make_unique<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>(
          owning_task_runner_,
          weak_factory_.GetWeakPtr());
  artifact_created_handle_ =
      std::make_unique<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>(
          owning_task_runner_,
          weak_factory_.GetWeakPtr());
  {
    base::AutoLock lock(GetActiveAdaptersLock());
    GetActiveAdapters().insert(this);
  }
  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoUnifiedAgentAdapter::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()),
        os_crypt_async::Encryptor::Option::kEncryptSyncCompat);
  }
  if (MahoCore* core = maho::GetCore()) {
    tool_availability_callback_token_ =
        maho_core_register_tool_availability_callback(
            core, tool_availability_handle_->user_data(),
            &MahoUnifiedAgentAdapter::OnToolAvailabilityFfi);
  }
}

bool MahoUnifiedAgentAdapter::IsAiAllowed() {
  return !ai_gate_ || ai_gate_.Run();
}

Browser* MahoUnifiedAgentAdapter::ResolveBoundBrowser() {
  if (!bound_browser_resolver_) {
    return nullptr;
  }
  return bound_browser_resolver_.Run();
}


MahoUnifiedAgentAdapter::~MahoUnifiedAgentAdapter() {
  {
    base::AutoLock lock(GetActiveAdaptersLock());
    GetActiveAdapters().erase(this);
  }
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DenyAllPendingApprovals();

  if (active_turn_ && !active_turn_->cancelled) {
    active_turn_->conversation_task->Cancel();
    active_turn_->cancelled = true;
    active_turn_->ffi_resources->callback_handle->Cancel();
  }

  if (tool_availability_callback_token_ != 0) {
    maho_core_unregister_tool_availability_callback(
        maho::GetCore(), tool_availability_callback_token_);
    tool_availability_callback_token_ = 0;
  }

  if (agent_session_) {
    agent_ffi_->cancel(agent_session_);
    QuarantineSessionFfiResources();
    agent_ffi_->session_free(agent_session_);
    agent_session_ = nullptr;
  }

  session_ffi_resources_.reset();
  if (tool_availability_handle_) {
    tool_availability_handle_->Cancel();
  }
  if (artifact_created_handle_) {
    artifact_created_handle_->Cancel();
  }

}

void MahoUnifiedAgentAdapter::ReleaseTurnFfiResources(void* release_user_data) {
  auto* lease = static_cast<std::shared_ptr<TurnFfiResources>*>(release_user_data);
  std::shared_ptr<TurnFfiResources> resources = std::move(*lease);
  delete lease;
  CallbackHandle::ReleaseFromProducer(resources->callback_user_data);
  resources->owning_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnTurnFfiResourcesReleased,
                     resources->adapter, resources->generation));
}

void MahoUnifiedAgentAdapter::ReleaseSessionFfiResources(
    void* release_user_data) {
  auto* lease =
      static_cast<std::shared_ptr<SessionFfiResources>*>(release_user_data);
  std::shared_ptr<SessionFfiResources> resources = std::move(*lease);
  delete lease;
  CallbackHandle::ReleaseFromProducer(resources->callback_user_data);
}

void MahoUnifiedAgentAdapter::OnTurnFfiResourcesReleased(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_turn_ && active_turn_->generation == generation) {
    DenyAllPendingApprovals();
    active_turn_.reset();
  }
}

void MahoUnifiedAgentAdapter::QuarantineSessionFfiResources() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session_ffi_resources_) {
    return;
  }
  if (session_ffi_resources_->callback_handle) {
    session_ffi_resources_->callback_handle->Cancel();
  }
  if (session_ffi_resources_->byok_bridge) {
    session_ffi_resources_->byok_bridge->active.store(
        false, std::memory_order_release);
    base::AutoLock lock(session_ffi_resources_->byok_bridge->lock);
    session_ffi_resources_->byok_bridge->snapshot.reset();
  }
}

std::string MahoUnifiedAgentAdapter::GetAdapterName() const {
  return "unified-agent";
}

bool MahoUnifiedAgentAdapter::IsAvailable() const {
  return true;
}

void MahoUnifiedAgentAdapter::SubmitMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    bool attach_browser_context,
    maho_ai::mojom::InteractionMode mode,
    RuntimeEventCallback on_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (active_turn_) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "A turn is already in progress.";
    on_event.Run(std::move(event));
    return;
  }

  if (!IsAiAllowed()) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = "AI is not available in this context.";
    on_event.Run(std::move(event));
    return;
  }

  auto turn = std::make_unique<ActiveTurn>();
  turn->generation = ++next_generation_;
  turn->conversation_task =
      std::make_unique<maho::ConversationTask>(GenerateTaskId());
  turn->on_event = std::move(on_event);
  turn->ffi_resources = std::make_shared<TurnFfiResources>();
  turn->ffi_resources->callback_handle = std::make_unique<CallbackHandle>(
      owning_task_runner_, weak_factory_.GetWeakPtr(), turn->generation);
  turn->ffi_resources->callback_user_data =
      turn->ffi_resources->callback_handle->user_data();
  turn->ffi_resources->generation = turn->generation;
  turn->ffi_resources->owning_task_runner = owning_task_runner_;
  turn->ffi_resources->adapter = weak_factory_.GetWeakPtr();
  if (!turn->conversation_task->StartStreaming()) {
    return;
  }
  const uint64_t generation = turn->generation;
  active_turn_ = std::move(turn);

  std::optional<base::FilePath> artifact_root;
  if (artifact_root_resolver_) {
    auto ensured_root = artifact_root_resolver_.Run();
    if (!ensured_root.has_value()) {
      TransitionToErrorForGeneration(generation, ensured_root.error());
      return;
    }
    artifact_root = std::move(ensured_root.value());
  } else if (bound_browser_resolver_) {
    Browser* artifact_browser = ResolveBoundBrowser();
    maho::ai::MahoArtifactRegistry* artifact_registry =
        artifact_browser
            ? maho::ai::MahoArtifactRegistry::GetForProfile(
                  artifact_browser->profile())
            : nullptr;
    if (!artifact_registry) {
      TransitionToErrorForGeneration(
          generation, "Artifact storage is unavailable for this profile.");
      return;
    }
    auto ensured_root = artifact_registry->ArtifactRootForTurn();
    if (!ensured_root.has_value()) {
      TransitionToErrorForGeneration(generation, ensured_root.error());
      return;
    }
    artifact_root = std::move(ensured_root.value());
  }

  if (!agent_session_) {
    EmitConnectionState("connecting");

    MahoCore* core = maho::GetCore();
    if (!core) {
      TransitionToErrorForGeneration(generation, "MahoCore not initialized.");
      return;
    }

    if (session_id_.empty()) {
      session_id_ = "unified_agent_session";
    }

    if (base::StartsWith(session_id_, "ext_")) {
      permission_extension_id_ = session_id_.substr(4);
    } else {
      permission_extension_id_.clear();
    }

    auto session_resources = std::make_shared<SessionFfiResources>();
    session_resources->callback_handle = std::make_unique<CallbackHandle>(
        owning_task_runner_, weak_factory_.GetWeakPtr());
    session_resources->callback_user_data =
        session_resources->callback_handle->user_data();
    session_resources->byok_snapshot = std::make_shared<ByokSnapshot>();
    if (prefs_) {
      session_resources->byok_snapshot->encrypted_byok_openai_b64 =
          prefs_->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64);
      session_resources->byok_snapshot->encrypted_byok_anthropic_b64 =
          prefs_->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64);
      session_resources->byok_snapshot->custom_provider =
          prefs_->GetString(maho::ai_prefs::kProvider);
      session_resources->byok_snapshot->custom_api_key =
          prefs_->GetString(maho::ai_prefs::kApiKey);
      session_resources->byok_snapshot->custom_base_url =
          prefs_->GetString(maho::ai_prefs::kBaseUrl);
      session_resources->byok_snapshot->custom_model =
          prefs_->GetString(maho::ai_prefs::kModel);
      const auto& creds = prefs_->GetDict(maho::ai_prefs::kMcpCredentials);
      for (auto item : creds) {
        if (item.second.is_string()) {
          session_resources->byok_snapshot->mcp_credentials[item.first] =
              item.second.GetString();
        }
      }
    }
    if (encryptor_) {
      session_resources->byok_snapshot->encryptor = encryptor_;
    }
    session_resources->byok_bridge = std::make_unique<ByokBridge>();
    session_resources->byok_bridge->snapshot = session_resources->byok_snapshot;

    // Wire permission + secure-storage callbacks. allow_insecure_key_storage
    // is FALSE: when no encrypted key is available the agent will fail-closed
    // rather than fall through to plaintext SQLite settings.
    std::string active_space_id;
    if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance()) {
      Browser* browser = ResolveBoundBrowser();
      active_space_id = browser ? bridge->GetActiveSpaceId(browser)
                                : bridge->GetActiveSpaceId();
    }

    auto* session_release_data =
        new std::shared_ptr<SessionFfiResources>(session_resources);
    session_resources->callback_handle->HandOffToProducer();
    agent_session_ = agent_ffi_->create_session_leased(
        core, session_id_.c_str(), nullptr, /*allow_insecure_key_storage=*/false,
        &MahoUnifiedAgentAdapter::OnAgentPermission,
        session_resources->callback_user_data,
        &MahoUnifiedAgentAdapter::OnAgentSecureStorage,
        session_resources->byok_bridge.get(),
        &MahoUnifiedAgentAdapter::OnAgentBrowserTool,
        session_resources->callback_user_data,
        active_space_id.empty() ? nullptr : active_space_id.c_str(),
        &MahoUnifiedAgentAdapter::ReleaseSessionFfiResources,
        session_release_data);
    if (!agent_session_) {
      ReleaseSessionFfiResources(session_release_data);
      TransitionToErrorForGeneration(generation,
                                     "Failed to create agent session.");
      return;
    }

    session_ffi_resources_ = std::move(session_resources);

    agent_ffi_->set_artifact_created_callback(
        agent_session_, &MahoUnifiedAgentAdapter::OnAgentArtifactCreated,
        artifact_created_handle_->user_data());

    EmitConnectionState("connected");
  }

  if (artifact_root) {
    agent_ffi_->set_artifact_root(agent_session_,
                                  artifact_root->AsUTF8Unsafe().c_str());
  }

  if (!ConfigurePreferredProvider(generation)) {
    return;
  }

  // Push the current global approval policy each turn so a "deny" kill-switch
  // (or "allow"/"prompt") toggled via prefs takes effect on the next message.
  if (prefs_) {
    const std::string policy = prefs_->GetString(maho::ai_prefs::kApprovalPolicy);
    agent_ffi_->set_approval_policy(agent_session_, policy.c_str());
  }

  std::shared_ptr<TurnFfiResources> resources = active_turn_->ffi_resources;
  auto* turn_release_data = new std::shared_ptr<TurnFfiResources>(resources);
  resources->callback_handle->HandOffToProducer();
  std::string shaped_message = ShapeOutboundMessage(message, chat_intent);
  bool success = agent_ffi_->send_message_leased(
      agent_session_, shaped_message.c_str(),
      &MahoUnifiedAgentAdapter::OnAgentToken,
      &MahoUnifiedAgentAdapter::OnAgentThinking,
      &MahoUnifiedAgentAdapter::OnAgentToolCall,
      &MahoUnifiedAgentAdapter::OnAgentToolResult,
      &MahoUnifiedAgentAdapter::OnAgentComplete,
      &MahoUnifiedAgentAdapter::OnAgentError,
      resources->callback_user_data,
      &MahoUnifiedAgentAdapter::ReleaseTurnFfiResources,
      turn_release_data);

  if (!success) {
    ReleaseTurnFfiResources(turn_release_data);
    TransitionToErrorForGeneration(generation,
                                   "Failed to send message to agent.");
    return;
  }
}

void MahoUnifiedAgentAdapter::CancelCurrentTurn() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (agent_session_) {
    agent_ffi_->cancel(agent_session_);
  }
  DenyAllPendingApprovals();
  if (active_turn_ && !active_turn_->cancelled) {
    active_turn_->conversation_task->Cancel();
    active_turn_->cancelled = true;
    active_turn_->ffi_resources->callback_handle->Cancel();
  }
}

// static
std::string MahoUnifiedAgentAdapter::GenerateApprovalId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

void MahoUnifiedAgentAdapter::DenyAllPendingApprovals() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& [id, state] : pending_approvals_) {
    if (!state) {
      continue;
    }
    if (!state->cancelled.load()) {
      state->decision = PromptDecision::kDeny;
    }
    state->event.Signal();
  }
  pending_approvals_.clear();
}

void MahoUnifiedAgentAdapter::RespondToApproval(const std::string& approval_id,
                                               bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Look up the single parked request for this id. A duplicate, stale, or
  // forged id finds nothing and is a no-op, so a decision resolves exactly
  // once even under repeated UI calls.
  auto it = pending_approvals_.find(approval_id);
  if (it == pending_approvals_.end()) {
    return;
  }
  std::shared_ptr<PermissionCallState> state = std::move(it->second);
  pending_approvals_.erase(it);

  if (!state || !IsActiveGeneration(state->generation)) {
    if (state) {
      state->decision = PromptDecision::kDeny;
      state->event.Signal();
    }
    return;
  }

  if (!state->cancelled.load()) {
    state->decision =
        approved ? PromptDecision::kAllowOnce : PromptDecision::kDeny;
  }
  state->event.Signal();

  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kApprovalResult;
  event.payload.Set("approval_id", approval_id);
  event.payload.Set("approved", approved);
  event.payload.Set("approval_state", approved ? "approved" : "denied");
  event.payload.Set("approval_decision", approved ? "allow_once" : "deny");
  event.payload.Set("approval_policy", state->approval_policy);
  event.payload.Set("sensitivity", state->sensitivity);
  event.payload.Set("page_derived_justification",
                    state->page_derived_justification);
  EmitEvent(std::move(event));
}

std::string MahoUnifiedAgentAdapter::GetRuntimeSessionId() const {
  return session_id_;
}

void MahoUnifiedAgentAdapter::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DenyAllPendingApprovals();

  if (active_turn_ && !active_turn_->cancelled) {
    active_turn_->conversation_task->Cancel();
    active_turn_->cancelled = true;
    active_turn_->ffi_resources->callback_handle->Cancel();
  }

  if (agent_session_) {
    agent_ffi_->cancel(agent_session_);
    QuarantineSessionFfiResources();
    agent_ffi_->session_free(agent_session_);
    agent_session_ = nullptr;
  }

  session_ffi_resources_.reset();

  // Cancel any in-flight tool execution.
  if (tool_graph_) {
    tool_graph_->CancelInflight();
  }

  session_id_.clear();
  pending_message_.clear();
}

bool MahoUnifiedAgentAdapter::ConfigurePreferredProvider(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!agent_session_) {
    return false;
  }

  const std::optional<std::string> preferred_provider =
      PreferredProviderForRuntime(prefs_);
  const char* provider = preferred_provider ? preferred_provider->c_str() : nullptr;
  if (!agent_ffi_->set_preferred_provider(agent_session_, provider)) {
    TransitionToCredentialErrorForGeneration(
        generation, MahoAiRuntimeErrorCode::kUnsupportedProvider);
    return false;
  }

  if (session_ffi_resources_ && session_ffi_resources_->byok_bridge) {
    base::AutoLock lock(session_ffi_resources_->byok_bridge->lock);
    session_ffi_resources_->byok_bridge->preferred_provider =
        preferred_provider.value_or(std::string());
  }
  return true;
}

// static
namespace {

class MahoAgentPermissionPromptDialogView : public views::DialogDelegate {
 public:
  MahoAgentPermissionPromptDialogView(
      const std::string& extension_id,
      const std::string& tool_name,
      base::OnceCallback<void(PromptDecision)> callback)
      : callback_(std::move(callback)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetModalType(ui::mojom::ModalType::kWindow);
    SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Always Allow");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Deny");

    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 12));

    auto* label = contents->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16("The extension \"" + extension_id + "\" wants to use the agent tool \"" + tool_name + "\".")));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);

    contents->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoAgentPermissionPromptDialogView::OnAllowOnce, base::Unretained(this)),
        u"Allow Once"));

    SetContentsView(std::move(contents));
  }

  MahoAgentPermissionPromptDialogView(const MahoAgentPermissionPromptDialogView&) = delete;
  MahoAgentPermissionPromptDialogView& operator=(const MahoAgentPermissionPromptDialogView&) = delete;

  std::u16string GetWindowTitle() const override {
    return u"Agent Tool Access Request";
  }

  bool Accept() override {
    if (callback_) std::move(callback_).Run(PromptDecision::kAllow);
    return true;
  }

  bool Cancel() override {
    if (callback_) std::move(callback_).Run(PromptDecision::kDeny);
    return true;
  }

  void OnAllowOnce() {
    if (callback_) std::move(callback_).Run(PromptDecision::kAllowOnce);
    GetWidget()->Close();
  }

 private:
  base::OnceCallback<void(PromptDecision)> callback_;
};

}  // namespace

// static
namespace {
base::TimeDelta& AgentCallWaitTimeoutStorage() {
  static base::TimeDelta timeout = base::Seconds(30);
  return timeout;
}
}  // namespace

base::TimeDelta MahoUnifiedAgentAdapter::AgentCallWaitTimeout() {
  return AgentCallWaitTimeoutStorage();
}

void MahoUnifiedAgentAdapter::SetAgentCallWaitTimeoutForTesting(
    base::TimeDelta timeout) {
  AgentCallWaitTimeoutStorage() = timeout;
}

MahoAgentPermissionDecision MahoUnifiedAgentAdapter::OnAgentPermission(void* user_data,
                                                                        const char* tool_name,
                                                                        const char* arguments) {
  if (!tool_name) return MahoAgentPermissionDecision_Deny;
  const std::string tool(tool_name);
  if (tool == "shell_exec") return MahoAgentPermissionDecision_Deny;
  if (!user_data) return MahoAgentPermissionDecision_Deny;

  // Recover the adapter via the FfiCallbackHandle bridge (weak-lifetime),
  // never a raw `this`: this callback fires on a Rust worker thread and the
  // adapter may be destroyed mid-turn (e.g. the AI panel is closed while a
  // permission prompt is pending). FromUserData copies a shared_ptr that keeps
  // the pointee alive for the call; the owner WeakPtr and all adapter state
  // (prefs_, session_grants_, permission_extension_id_) are touched only on
  // the owning sequence inside the posted task.
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (!pointee || pointee->cancelled.load()) {
    return MahoAgentPermissionDecision_Deny;
  }

  auto call_state = std::make_shared<PermissionCallState>();
  call_state->generation = pointee->generation;
  const bool page_derived_justification =
      ExtractPageDerivedJustification(arguments);
  pointee->task_runner->PostTask(
      FROM_HERE,
      CreatePermissionDecisionTask(
          pointee->owner, pointee->generation, tool, call_state,
          page_derived_justification));

  PromptDecision decision_val;
  if (call_state->event.TimedWait(AgentCallWaitTimeout())) {
    decision_val = call_state->decision;
  } else {
    call_state->cancelled.store(true);
    decision_val = PromptDecision::kDeny;
  }

  return decision_val == PromptDecision::kDeny
             ? MahoAgentPermissionDecision_Deny
             : MahoAgentPermissionDecision_Allow;
}

base::OnceClosure MahoUnifiedAgentAdapter::CreatePermissionDecisionTask(
    base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
    uint64_t generation,
    std::string tool,
    std::shared_ptr<PermissionCallState> state,
    bool page_derived_justification) {
  return base::BindOnce(
      [](base::WeakPtr<MahoUnifiedAgentAdapter> adapter, uint64_t generation,
         std::string tool, std::shared_ptr<PermissionCallState> state,
         bool page_derived_justification) {
        if (!adapter || !adapter->IsActiveGeneration(generation)) {
          state->decision = PromptDecision::kDeny;
          state->event.Signal();
          return;
        }
        adapter->DecidePermissionOnUiThread(
            generation, tool, adapter->permission_extension_id_, state,
            page_derived_justification);
      },
      std::move(adapter), generation, std::move(tool), std::move(state),
      page_derived_justification);
}

base::OnceClosure
MahoUnifiedAgentAdapter::CreatePermissionDecisionTaskForTesting(
    void* user_data,
    const std::string& tool,
    std::shared_ptr<PermissionCallState> state,
    bool page_derived_justification) {
  auto pointee = CallbackHandle::FromUserData(user_data);
  state->generation = pointee->generation;
  return CreatePermissionDecisionTask(
      pointee->owner, pointee->generation, tool, std::move(state),
      page_derived_justification);
}

std::optional<PromptDecision> MahoUnifiedAgentAdapter::EvaluatePermissionSync(
    const std::string& tool,
    const std::string& extension_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (tool == "shell_exec") {
    return PromptDecision::kDeny;
  }

  // 0. Global approval policy override.
  std::string global_policy;
  if (prefs_) {
    global_policy = prefs_->GetString(maho::ai_prefs::kApprovalPolicy);
    if (global_policy == "deny") {
      return PromptDecision::kDeny;
    }
    if (global_policy == "allow") {
      return PromptDecision::kAllow;
    }
  }

  if (tool == "read_current_page" || tool == "get_selected_text" ||
      tool == "get_active_tab" || tool == "search_in_page" ||
      tool == "extract_structured_page_context") {
    return PromptDecision::kAllow;
  }

  if (extension_id.empty()) {
    // Desktop agent calls carry an empty extension id. web_fetch/web_search are
    // read-only public web retrieval, which product policy treats as needing no
    // per-call consent; their own URL validation is the enforcing boundary.
    // shell_exec is deliberately absent (denied above) and must stay absent.
    if (tool != "fs_read" && tool != "web_fetch" && tool != "web_search") {
      // Under the "prompt" (ask) policy, route sensitive desktop-agent tools to
      // the interactive approval broker (chat approval card, or native dialog
      // fallback) so the user can approve per call. Any other/unset policy
      // keeps the safe default deny.
      if (global_policy != "prompt") {
        return PromptDecision::kDeny;
      }
    }
  }

  // 1. Persistent grants in prefs.
  if (prefs_) {
    const auto& grants = prefs_->GetDict(maho::ai_prefs::kAgentToolGrants);
    const auto* tool_list = grants.FindList(extension_id);
    if (tool_list) {
      for (const auto& item : *tool_list) {
        if (item.is_string() && item.GetString() == tool) {
          return PromptDecision::kAllow;
        }
      }
    }
  }

  // 2. Session-level grants.
  if (session_grants_.contains(tool)) {
    return PromptDecision::kAllow;
  }

  return std::nullopt;
}

void MahoUnifiedAgentAdapter::DecidePermissionOnUiThread(
    uint64_t generation,
    const std::string& tool,
    const std::string& extension_id,
    std::shared_ptr<PermissionCallState> state,
    bool page_derived_justification) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsActiveGeneration(generation)) {
    state->decision = PromptDecision::kDeny;
    state->event.Signal();
    return;
  }
  state->generation = generation;

  if (std::optional<PromptDecision> decided =
          EvaluatePermissionSync(tool, extension_id)) {
    if (!state->cancelled.load()) {
      state->decision = *decided;
    }
    state->event.Signal();
    return;
  }

  // Canonical WebUI approval broker: when a live runtime event consumer is
  // attached, park this request under an unguessable id and publish a safe
  // kApprovalRequest event (tool name only, never raw arguments). The worker
  // stays blocked until RespondToApproval resolves it exactly once. No event
  // consumer means the WebUI is not driving this turn, so fall through to the
  // native Views dialog as the fallback consumer of the same broker path.
  if (active_turn_) {
    std::string approval_id = GenerateApprovalId();
    state->approval_policy = CurrentApprovalPolicyForPayload(prefs_);
    state->sensitivity = SensitivityForToolPayload(tool);
    state->page_derived_justification = page_derived_justification;
    pending_approvals_[approval_id] = state;

    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kApprovalRequest;
    event.text = tool;
    event.payload.Set("approval_id", approval_id);
    event.payload.Set("description",
                      "The agent is requesting to run a sensitive tool.");
    event.payload.Set("related_tool_name", tool);
    event.payload.Set("approval_policy", state->approval_policy);
    event.payload.Set("sensitivity", state->sensitivity);
    event.payload.Set("approval_state", "pending");
    event.payload.Set("page_derived_justification",
                      page_derived_justification);
    EmitEvent(std::move(event));
    return;
  }

  // 3. Lazy dialog; persistence happens in the completion callback so it also
  // runs on the UI thread.
  ShowPermissionPromptOnUIThread(
      extension_id, tool,
      base::BindOnce(
          [](base::WeakPtr<MahoUnifiedAgentAdapter> adapter, std::string tool,
              std::string extension_id,
              std::shared_ptr<PermissionCallState> state,
              uint64_t generation,
              PromptDecision result) {
             if (state->cancelled.load() || !adapter ||
                 !adapter->IsActiveGeneration(generation)) {
               state->decision = PromptDecision::kDeny;
               state->event.Signal();
               return;
             }
             if (result == PromptDecision::kAllow && adapter->prefs_ &&
                 !extension_id.empty()) {
              ScopedDictPrefUpdate update(adapter->prefs_,
                                          maho::ai_prefs::kAgentToolGrants);
              auto* tool_list = update->EnsureList(extension_id);
              if (tool_list) {
                bool exists = false;
                for (const auto& item : *tool_list) {
                  if (item.is_string() && item.GetString() == tool) {
                    exists = true;
                    break;
                  }
                }
                if (!exists) {
                  tool_list->Append(tool);
                }
              }
             } else if (result == PromptDecision::kAllowOnce) {
               adapter->session_grants_.insert(tool);
             }
            state->decision = result;
            state->event.Signal();
          },
          weak_factory_.GetWeakPtr(), tool, extension_id, state, generation));
}

void MahoUnifiedAgentAdapter::ShowPermissionPromptOnUIThread(
    const std::string& extension_id,
    const std::string& tool_name,
    base::OnceCallback<void(PromptDecision)> callback) {
  if (permission_prompt_override_for_testing_) {
    std::move(callback).Run(
        permission_prompt_override_for_testing_.Run(extension_id, tool_name));
    return;
  }

  BrowserWindowInterface* browser = ResolveBoundBrowser();
  if (!browser || !browser->GetWindow()) {
    std::move(callback).Run(PromptDecision::kDeny);
    return;
  }

  auto delegate = std::make_unique<MahoAgentPermissionPromptDialogView>(
      extension_id, tool_name, std::move(callback));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(delegate), browser->GetWindow()->GetNativeWindow(),
      gfx::NativeView());
  if (widget) {
    widget->Show();
  } else {
    std::move(callback).Run(PromptDecision::kDeny);
  }
}

MahoAgentPermissionDecision MahoUnifiedAgentAdapter::RunPermissionDialogForTest(
    const std::string& tool,
    const std::string& extension_id) {
  auto state = std::make_shared<PermissionCallState>();
  const uint64_t generation = active_turn_ ? active_turn_->generation : 0;
  DecidePermissionOnUiThread(generation, tool, extension_id, state);
  return state->decision == PromptDecision::kDeny
             ? MahoAgentPermissionDecision_Deny
             : MahoAgentPermissionDecision_Allow;
}

namespace {

// Annotation for tracking active secrets in memory. Crashpad reads this during dumps.
crashpad::StringAnnotation<32> g_byok_key_status("maho-byok-key-status");

// Secure-zero free_fn passed back to Rust via MahoAgentSecureKey. Scrubs
// plaintext via volatile-write loop before returning the heap chunk to
// libc. See maho_ffi.h MahoAgentSecureKey contract.
void FreeSecureKey(char* ptr, uintptr_t len) {
  if (!ptr) return;
  if (len > 0) {
    volatile char* p = ptr;
    for (uintptr_t i = 0; i < len; ++i) {
      p[i] = 0;
    }
  }
  free(ptr);
  g_byok_key_status.Clear();
}

MahoAgentSecureKey AllocateSecureKeyFromPlaintext(const std::string& plaintext) {
  MahoAgentSecureKey result{nullptr, 0, nullptr};
  if (plaintext.empty()) return result;
  char* buf = static_cast<char*>(malloc(plaintext.size()));
  if (!buf) return result;
  std::memcpy(buf, plaintext.data(), plaintext.size());
  result.ptr = buf;
  result.len = plaintext.size();
  result.free_fn = &FreeSecureKey;
  return result;
}

}  // namespace

// static
MahoAgentSecureKey MahoUnifiedAgentAdapter::OnAgentSecureStorage(
    void* user_data, const char* provider) {
  if (!user_data || !provider) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }
  // user_data is a std::shared_ptr<ByokSnapshot>* bridge (heap-allocated).
  // Atomic load pairs with key invalidation's atomic publication. Once loaded,
  // the snapshot stays immutable for this callback's entire lifetime.
  auto* bridge = static_cast<ByokBridge*>(user_data);
  if (!bridge->active.load(std::memory_order_acquire)) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }
  std::shared_ptr<ByokSnapshot> snapshot;
  {
    base::AutoLock lock(bridge->lock);
    snapshot = bridge->snapshot;
  }
  if (!snapshot) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }

  const std::string provider_str(provider);
  const auto null_key_with_outcome =
      [&bridge, &provider_str](MahoAiRuntimeErrorCode outcome) {
        RecordSecureStorageOutcome(bridge, provider_str, outcome);
        return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr,
                                  nullptr};
      };

  if (base::StartsWith(provider_str, "mcp:", base::CompareCase::SENSITIVE)) {
    std::string keychain_id = provider_str.substr(4);
    auto it = snapshot->mcp_credentials.find(keychain_id);
    if (it == snapshot->mcp_credentials.end()) {
      return null_key_with_outcome(
          MahoAiRuntimeErrorCode::kProviderNotConfigured);
    }
    std::string encrypted_b64 = it->second;
    std::string encrypted_bytes;
    if (!base::Base64Decode(encrypted_b64, &encrypted_bytes)) {
      return null_key_with_outcome(
          MahoAiRuntimeErrorCode::kCredentialDecryptFailed);
    }
    if (!snapshot->encryptor) {
      return null_key_with_outcome(
          MahoAiRuntimeErrorCode::kSecureStoreUnavailable);
    }
    std::string plaintext_key;
    if (snapshot->encryptor->DecryptString(encrypted_bytes, &plaintext_key)) {
      MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(plaintext_key);
      g_byok_key_status.Set("active");
      volatile char* p = plaintext_key.data();
      for (size_t i = 0; i < plaintext_key.size(); ++i) p[i] = 0;
      if (!result.ptr) {
        return null_key_with_outcome(
            MahoAiRuntimeErrorCode::kCredentialUnusable);
      }
      RecordSecureStorageOutcome(bridge, provider_str, std::nullopt);
      return result;
    }
    return null_key_with_outcome(
        MahoAiRuntimeErrorCode::kCredentialDecryptFailed);
  }

  // Custom-provider path: user configured a plain api_key + base_url via
  // the Chromium Settings pane (maho.ai.api_key, maho.ai.base_url,
  // maho.ai.model). Match by provider family: "openai" / "openai-compatible"
  // are both served when Rust asks for "openai"; "anthropic" /
  // "anthropic-compatible" are both served when Rust asks for "anthropic".
  auto provider_family_matches = [&](const std::string& family) -> bool {
    return snapshot->custom_provider == family ||
           snapshot->custom_provider == (family + "-compatible");
  };

  if (!snapshot->custom_api_key.empty() && provider_family_matches(provider_str)) {
    MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(snapshot->custom_api_key);
    if (!result.ptr) {
      return null_key_with_outcome(MahoAiRuntimeErrorCode::kCredentialUnusable);
    }
    if (!snapshot->custom_base_url.empty()) {
      result.base_url = strdup(snapshot->custom_base_url.c_str());
      result.cstring_free_fn = reinterpret_cast<void(*)(char*)>(&::free);
    }
    if (!snapshot->custom_model.empty()) {
      result.model = strdup(snapshot->custom_model.c_str());
      result.cstring_free_fn = reinterpret_cast<void(*)(char*)>(&::free);
    }
    g_byok_key_status.Set("active");
    RecordSecureStorageOutcome(bridge, provider_str, std::nullopt);
    return result;
  }

  // Encrypted BYOK path: original behaviour.
  if (!snapshot->encryptor) {
    return null_key_with_outcome(
        MahoAiRuntimeErrorCode::kSecureStoreUnavailable);
  }

  const std::string* encrypted_b64 = nullptr;
  if (provider_str == "openai") {
    encrypted_b64 = &snapshot->encrypted_byok_openai_b64;
  } else if (provider_str == "anthropic") {
    encrypted_b64 = &snapshot->encrypted_byok_anthropic_b64;
  } else {
    return null_key_with_outcome(MahoAiRuntimeErrorCode::kUnsupportedProvider);
  }

  if (encrypted_b64->empty()) {
    return null_key_with_outcome(
        MahoAiRuntimeErrorCode::kProviderNotConfigured);
  }

  std::string encrypted_bytes;
  if (!base::Base64Decode(*encrypted_b64, &encrypted_bytes)) {
    return null_key_with_outcome(
        MahoAiRuntimeErrorCode::kCredentialDecryptFailed);
  }
  std::string plaintext;
  const bool decrypted = snapshot->encryptor->DecryptString(encrypted_bytes, &plaintext);

  if (!decrypted || plaintext.empty()) {
    if (!plaintext.empty()) {
      volatile char* p = plaintext.data();
      for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
    }
    return null_key_with_outcome(
        MahoAiRuntimeErrorCode::kCredentialDecryptFailed);
  }
  MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(plaintext);
  g_byok_key_status.Set("active");
  volatile char* p = plaintext.data();
  for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
  if (!result.ptr) {
    return null_key_with_outcome(MahoAiRuntimeErrorCode::kCredentialUnusable);
  }
  RecordSecureStorageOutcome(bridge, provider_str, std::nullopt);
  return result;
}

void MahoUnifiedAgentAdapter::RecordSecureStorageOutcome(
    ByokBridge* bridge,
    const std::string& provider,
    std::optional<MahoAiRuntimeErrorCode> outcome) {
  if (!bridge) {
    return;
  }
  base::AutoLock lock(bridge->lock);
  if (outcome) {
    bridge->lookup_outcomes.insert_or_assign(provider, *outcome);
  } else {
    bridge->lookup_outcomes.erase(provider);
  }
}

namespace {

void FreeCStr(char* p) { ::free(p); }

MahoAgentToolResult MakeErrorToolResult(const std::string& error) {
  MahoAgentToolResult result{nullptr, nullptr, &FreeCStr};
  result.error_ptr = strdup(error.c_str());
  return result;
}

MahoAgentToolResult MakeJsonToolResult(const std::string& json) {
  MahoAgentToolResult result{nullptr, nullptr, &FreeCStr};
  result.json_ptr = strdup(json.c_str());
  return result;
}

}  // namespace

// static
MahoAgentToolResult MahoUnifiedAgentAdapter::OnAgentBrowserTool(
    void* user_data, const char* tool_name, const char* args_json) {
  if (!user_data) {
    return MakeErrorToolResult("Agent tool sink is unavailable");
  }
  if (!tool_name) {
    return MakeErrorToolResult("tool_name is null");
  }
  const std::string tool_name_str(tool_name);
  const std::string args_json_str(args_json ? args_json : "{}");

  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (!pointee || pointee->cancelled.load()) {
    return MakeErrorToolResult("Agent tool sink is unavailable");
  }
  const uint64_t generation = pointee->generation;

  const maho::ai::BrowserActionContract* browser_action_contract =
      maho::ai::FindBrowserActionContract(tool_name_str);
  const bool browser_action_requires_approval =
      (browser_action_contract &&
       maho::ai::RequiresApproval(*browser_action_contract)) ||
      MahoBrowserToolExecutor::IsMailWriteTool(tool_name_str);
  if (browser_action_requires_approval) {
    MahoAgentPermissionDecision permission =
        OnAgentPermission(user_data, tool_name, args_json);
    if (permission != MahoAgentPermissionDecision_Allow) {
      return MakeErrorToolResult("Browser action denied by approval policy");
    }
    if (pointee->cancelled.load(std::memory_order_acquire)) {
      return MakeErrorToolResult("Agent browser tool callback is stale");
    }
  }
#if !defined(NDEBUG)
  {
    // Never write raw tool arguments to a fixed world-readable path: they can
    // carry credentials or page secrets. Log the tool name only.
    std::ofstream log_file("/tmp/maho-agent-live.log", std::ios::app);
    if (log_file.is_open()) {
      log_file << "[OnAgentBrowserTool] invoked tool=" << tool_name_str << "\n";
    }
  }
#endif

  auto call_state = std::make_shared<BrowserToolCallState>();

  pointee->task_runner->PostTask(
      FROM_HERE,
      CreateBrowserToolTask(pointee->owner, generation, tool_name_str,
                            args_json_str, call_state,
                            browser_action_requires_approval));

  std::string result_json;
  std::string result_error;
  if (call_state->done.TimedWait(AgentCallWaitTimeout())) {
    result_json = std::move(call_state->result_json);
    result_error = std::move(call_state->result_error);
  } else {
    call_state->cancelled.store(true);
    result_error = "Browser tool execution timed out after 30s";
  }

  if (!result_error.empty()) {
    return MakeErrorToolResult(result_error);
  }
  return MakeJsonToolResult(result_json);
}

base::OnceClosure MahoUnifiedAgentAdapter::CreateBrowserToolTask(
    base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
    uint64_t generation,
    std::string tool_name,
    std::string args_json,
    std::shared_ptr<BrowserToolCallState> state,
    bool browser_action_preapproved) {
  return base::BindOnce(
      [](base::WeakPtr<MahoUnifiedAgentAdapter> adapter, uint64_t generation,
         std::string tool_name, std::string args_json_in,
         std::shared_ptr<BrowserToolCallState> state,
         bool browser_action_preapproved) {
        if (!adapter) {
          if (!state->cancelled.load()) {
            state->result_error = "Agent browser tool callback is stale";
          }
          state->done.Signal();
          return;
        }
        // The Rust session owns one browser-tool bridge for its lifetime, so
        // its callback handle has no per-turn generation. Resolve that
        // session-level sentinel only after crossing onto the adapter's
        // owning sequence. Per-turn callbacks retain their explicit
        // generation and still reject after Reset().
        if (generation == 0 && adapter->active_turn_ &&
            !adapter->active_turn_->cancelled) {
          generation = adapter->active_turn_->generation;
        }
        if (!adapter->IsActiveGeneration(generation)) {
          if (!state->cancelled.load()) {
            state->result_error = "Agent browser tool callback is stale";
          }
          state->done.Signal();
          return;
        }
        if (!adapter->IsAiAllowed()) {
          if (!state->cancelled.load()) {
            state->result_error = "AI is not available in this context";
          }
          state->done.Signal();
          return;
        }
        Browser* browser = adapter->ResolveBoundBrowser();
        if (!browser) {
          if (!state->cancelled.load()) {
            state->result_error = "No active browser window";
          }
          state->done.Signal();
          return;
        }
        std::optional<base::DictValue> parsed =
            base::JSONReader::ReadDict(args_json_in, base::JSON_PARSE_RFC);
        base::DictValue args_dict;
        if (parsed) {
          args_dict = std::move(*parsed);
        }
        std::string browser_action_lease_holder_id =
            adapter->permission_extension_id_.empty()
                ? adapter->session_id_
                : adapter->permission_extension_id_;
        auto executor = std::make_shared<MahoBrowserToolExecutor>(
            browser, /*browser_tools_v1_enabled=*/true, adapter->ai_gate_,
            base::BindRepeating(
                [](bool browser_action_preapproved,
                   base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
                   uint64_t generation,
                   const MahoBrowserToolExecutor::BrowserActionAuthorization&
                       metadata) {
                  MahoBrowserToolExecutor::BrowserActionApprovalDecision
                      decision;
                  if (!adapter || !adapter->IsActiveGeneration(generation)) {
                    decision.error = "Browser action owner is unavailable";
                    return decision;
                  }
                  if (!metadata.requires_approval) {
                    decision.approved = true;
                    return decision;
                  }
                  if (!browser_action_preapproved) {
                    decision.error =
                        "Browser action was not approved before dispatch";
                    return decision;
                  }
                  decision.approved = true;
                  return decision;
                },
                browser_action_preapproved, adapter, generation),
            nullptr, maho::MahoMcpSession::GetLeaseRegistryForBrowserActions(),
            std::move(browser_action_lease_holder_id));
        executor->Execute(
            tool_name, args_dict,
            base::BindOnce(
                [](std::shared_ptr<MahoBrowserToolExecutor> keep_alive,
                   base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
                   uint64_t generation,
                   std::shared_ptr<BrowserToolCallState> state,
                   base::DictValue result) {
                  if (!state->cancelled.load() && adapter &&
                      adapter->IsActiveGeneration(generation)) {
                    std::string json;
                    base::JSONWriter::Write(base::Value(std::move(result)),
                                            &json);
                    state->result_json = std::move(json);
                  } else if (!state->cancelled.load()) {
                    state->result_error =
                        "Agent browser tool callback is stale";
                  }
                  state->done.Signal();
                },
                executor, adapter, generation, state));
      },
      std::move(adapter), generation, std::move(tool_name),
      std::move(args_json), std::move(state), browser_action_preapproved);
}

base::OnceClosure MahoUnifiedAgentAdapter::CreateBrowserToolTaskForTesting(
    void* user_data,
    const std::string& tool_name,
    const std::string& args_json,
    std::shared_ptr<BrowserToolCallState> state,
    bool browser_action_preapproved) {
  auto pointee = CallbackHandle::FromUserData(user_data);
  return CreateBrowserToolTask(
      pointee->owner, pointee->generation, tool_name, args_json,
      std::move(state), browser_action_preapproved);
}

// static
void MahoUnifiedAgentAdapter::OnAgentToken(void* user_data, const char* token) {
  if (!user_data || !token) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string token_copy(token);
  pointee->task_runner->PostTask(
      FROM_HERE,
       base::BindOnce(&MahoUnifiedAgentAdapter::OnTokenOnSequence,
                      pointee->owner, pointee->generation, std::move(token_copy)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentComplete(void* user_data, const char* full_text, const char* tool_calls_json) {
  if (!user_data || !full_text) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string text_copy(full_text);
  pointee->task_runner->PostTask(
      FROM_HERE,
       base::BindOnce(&MahoUnifiedAgentAdapter::OnCompleteOnSequence,
                      pointee->owner, pointee->generation, std::move(text_copy)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentError(void* user_data, const char* error) {
  if (!user_data || !error) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string error_copy(error);
  pointee->task_runner->PostTask(
      FROM_HERE,
       base::BindOnce(&MahoUnifiedAgentAdapter::OnErrorOnSequence,
                      pointee->owner, pointee->generation, std::move(error_copy)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentThinking(void* user_data,
                                              const char* thinking) {
  if (!user_data || !thinking) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string copy(thinking);
  pointee->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnThinkingOnSequence,
                     pointee->owner, pointee->generation, std::move(copy)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentToolCall(void* user_data,
                                              const char* id,
                                              const char* name,
                                              const char* args) {
  if (!user_data || !id || !name || !args) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string id_copy(id), name_copy(name), args_copy(args);
  pointee->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnToolCallOnSequence,
                     pointee->owner, pointee->generation, std::move(id_copy),
                     std::move(name_copy), std::move(args_copy)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentToolResult(void* user_data,
                                                const char* id,
                                                const char* name,
                                                const char* result,
                                                bool success) {
  if (!user_data || !id || !name || !result) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string id_copy(id), name_copy(name), result_copy(result);
  pointee->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnToolResultOnSequence,
                     pointee->owner, pointee->generation, std::move(id_copy),
                     std::move(name_copy), std::move(result_copy), success));
}

// static
void MahoUnifiedAgentAdapter::OnAgentArtifactCreated(void* user_data,
                                                     const char* artifact_json) {
  if (!user_data || !artifact_json) return;
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (pointee->cancelled.load(std::memory_order_acquire)) return;
  std::string json_copy(artifact_json);
  pointee->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnArtifactCreatedOnSequence,
                     pointee->owner, std::move(json_copy)));
}

bool MahoUnifiedAgentAdapter::IsActiveGeneration(uint64_t generation) const {
  return active_turn_ && !active_turn_->cancelled &&
         active_turn_->generation == generation;
}

void MahoUnifiedAgentAdapter::OnTokenOnSequence(uint64_t generation,
                                                 std::string token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveGeneration(generation) || !IsAiAllowed()) {
    return;
  }
  if (!active_turn_->conversation_task->OnToken(token)) {
    // 2 MB cap reached — terminate with error (H13).
    active_turn_->conversation_task->Error("response exceeded 2 MB cap");
    TransitionToErrorForGeneration(generation, "response exceeded 2 MB cap");
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kAssistantToken;
  event.text = token;
  active_turn_->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::OnCompleteOnSequence(uint64_t generation,
                                                    std::string full_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveGeneration(generation) || !IsAiAllowed()) {
    return;
  }
  if (!active_turn_->conversation_task->Complete()) {
    return;
  }
  std::unique_ptr<ActiveTurn> completed_turn = std::move(active_turn_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kTurnComplete;
  event.text = full_text;
  completed_turn->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::OnThinkingOnSequence(uint64_t generation,
                                                   std::string thinking) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveGeneration(generation) || !IsAiAllowed()) {
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kAssistantThinking;
  event.text = std::move(thinking);
  active_turn_->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::OnToolCallOnSequence(uint64_t generation,
                                                   std::string id,
                                                   std::string name,
                                                   std::string args) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveGeneration(generation) || !IsAiAllowed()) {
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kToolRequest;
  // Populate event.payload with the exact keys ToMojoEvent() reads. Writing to
  // event.text with different keys leaves tool_call empty in the UI (blank tool
  // name renders a bogus "Thinking" activity label; blank call_id breaks
  // request/result correlation).
  event.payload.Set("call_id", id);
  event.payload.Set("tool_name", name);
  event.payload.Set("arguments_json", args);
  active_turn_->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::OnToolResultOnSequence(uint64_t generation,
                                                     std::string id,
                                                     std::string name,
                                                     std::string result,
                                                     bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveGeneration(generation) || !IsAiAllowed()) {
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kToolResult;
  event.payload.Set("call_id", id);
  event.payload.Set("success", success);
  event.payload.Set("output", result);
  if (!success) {
    event.payload.Set("error", result);
  }
  active_turn_->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::OnArtifactCreatedOnSequence(
    std::string artifact_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed()) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(artifact_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::DictValue& d = parsed->GetDict();
  const std::string* session_id = d.FindString("session_id");
  const std::string* display_name = d.FindString("display_name");
  const std::string* mime_type = d.FindString("mime_type");
  const std::string* storage_rel_path = d.FindString("storage_rel_path");
  if (!session_id || !display_name || !mime_type || !storage_rel_path) {
    return;
  }
  auto read_num = [&](const char* key) -> double {
    const base::Value* v = d.Find(key);
    if (!v) return 0;
    if (v->is_int()) return v->GetInt();
    if (v->is_double()) return v->GetDouble();
    return 0;
  };
  Browser* browser = ResolveBoundBrowser();
  if (!browser) {
    return;
  }
  maho::ai::MahoArtifactRegistry* registry =
      maho::ai::MahoArtifactRegistry::GetForProfile(browser->profile());
  if (!registry) {
    return;
  }
  auto registered =
      registry->RegisterArtifact(*session_id, *display_name, *mime_type,
                                 static_cast<uint64_t>(read_num("size_bytes")),
                                 *storage_rel_path,
                                 static_cast<int64_t>(read_num("created_at_ms")));
  if (!registered.has_value()) {
    return;
  }
  if (active_turn_) {
    // Emit a normalized ArtifactInfo payload carrying the registry's opaque id.
    // Paths never cross the mojo boundary; created_at is seconds-since-epoch to
    // match RuntimeEvent.timestamp.
    base::DictValue info;
    info.Set("artifact_id", registered.value());
    info.Set("session_id", *session_id);
    info.Set("display_name", *display_name);
    info.Set("mime_type", *mime_type);
    info.Set("size_bytes", read_num("size_bytes"));
    info.Set("created_at", read_num("created_at_ms") / 1000.0);
    std::string info_json;
    base::JSONWriter::Write(info, &info_json);
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kArtifactCreated;
    event.text = std::move(info_json);
    active_turn_->on_event.Run(std::move(event));
  }
}

void MahoUnifiedAgentAdapter::OnErrorOnSequence(uint64_t generation,
                                                  std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveGeneration(generation) || !IsAiAllowed()) {
    return;
  }
  const CredentialEnvelopeParseResult envelope =
      ParseCredentialErrorEnvelope(error);
  std::optional<MahoAiRuntimeErrorCode> error_code;
  if (envelope.is_credential_envelope) {
    error_code = envelope.error_code
                     ? ResolveSecureStorageOutcome(*envelope.error_code)
                     : std::nullopt;
    error = kSafeCredentialFailureText;
  }
  active_turn_->conversation_task->Error(error);
  std::unique_ptr<ActiveTurn> failed_turn = std::move(active_turn_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = error;
  event.runtime_error_code = error_code;
  failed_turn->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::EmitEvent(MahoAiRuntimeEvent event) {
  if (active_turn_) {
    active_turn_->on_event.Run(std::move(event));
  }
}

void MahoUnifiedAgentAdapter::EmitConnectionState(
    const std::string& state_str) {
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kConnectionStateChanged;
  event.text = state_str;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::TransitionToError(const std::string& message) {
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = message;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::TransitionToErrorForGeneration(
    uint64_t generation,
    const std::string& message) {
  if (!IsActiveGeneration(generation)) {
    return;
  }
  std::unique_ptr<ActiveTurn> failed_turn = std::move(active_turn_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = message;
  failed_turn->on_event.Run(std::move(event));
}

void MahoUnifiedAgentAdapter::TransitionToCredentialErrorForGeneration(
    uint64_t generation,
    MahoAiRuntimeErrorCode error_code) {
  if (!IsActiveGeneration(generation)) {
    return;
  }
  std::unique_ptr<ActiveTurn> failed_turn = std::move(active_turn_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = kSafeCredentialFailureText;
  event.runtime_error_code = error_code;
  failed_turn->on_event.Run(std::move(event));
}

std::optional<MahoAiRuntimeErrorCode>
MahoUnifiedAgentAdapter::ResolveSecureStorageOutcome(
    MahoAiRuntimeErrorCode error_code) const {
  if (error_code != MahoAiRuntimeErrorCode::kProviderNotConfigured ||
      !session_ffi_resources_ || !session_ffi_resources_->byok_bridge) {
    return error_code;
  }
  ByokBridge* bridge = session_ffi_resources_->byok_bridge.get();
  base::AutoLock lock(bridge->lock);
  auto outcome = bridge->lookup_outcomes.find(bridge->preferred_provider);
  return outcome == bridge->lookup_outcomes.end()
             ? std::optional<MahoAiRuntimeErrorCode>(error_code)
             : std::optional<MahoAiRuntimeErrorCode>(outcome->second);
}

std::optional<MahoAiRuntimeErrorCode>
MahoUnifiedAgentAdapter::SecureStorageOutcomeForTesting(
    const std::string& provider) const {
  if (!session_ffi_resources_ || !session_ffi_resources_->byok_bridge) {
    return std::nullopt;
  }
  ByokBridge* bridge = session_ffi_resources_->byok_bridge.get();
  base::AutoLock lock(bridge->lock);
  auto outcome = bridge->lookup_outcomes.find(provider);
  return outcome == bridge->lookup_outcomes.end()
             ? std::nullopt
             : std::optional<MahoAiRuntimeErrorCode>(outcome->second);
}

void MahoUnifiedAgentAdapter::ClearSecureStorageEncryptorForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session_ffi_resources_ || !session_ffi_resources_->byok_snapshot ||
      !session_ffi_resources_->byok_bridge) {
    return;
  }
  auto replacement =
      std::make_shared<ByokSnapshot>(*session_ffi_resources_->byok_snapshot);
  replacement->encryptor.reset();
  session_ffi_resources_->byok_snapshot = replacement;
  base::AutoLock lock(session_ffi_resources_->byok_bridge->lock);
  session_ffi_resources_->byok_bridge->snapshot = std::move(replacement);
}

void MahoUnifiedAgentAdapter::OnOsCryptReady(os_crypt_async::Encryptor encryptor) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  encryptor_ = std::make_shared<os_crypt_async::Encryptor>(std::move(encryptor));
  if (!session_ffi_resources_ || !session_ffi_resources_->byok_snapshot ||
      !session_ffi_resources_->byok_bridge ||
      !session_ffi_resources_->byok_bridge->active.load(
          std::memory_order_acquire)) {
    return;
  }

  auto replacement = std::make_shared<ByokSnapshot>(
      *session_ffi_resources_->byok_snapshot);
  replacement->encryptor = encryptor_;
  session_ffi_resources_->byok_snapshot = replacement;
  base::AutoLock lock(session_ffi_resources_->byok_bridge->lock);
  session_ffi_resources_->byok_bridge->snapshot = std::move(replacement);
}

void MahoUnifiedAgentAdapter::OnToolAvailabilityFfi(void* user_data,
                                                    const char* json_utf8) {
  if (!user_data || !json_utf8) {
    return;
  }
  auto pointee =
      maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(user_data);
  if (!pointee || pointee->cancelled.load(std::memory_order_acquire)) {
    return;
  }
  std::string json_copy(json_utf8);
  if (!pointee->task_runner) {
    return;
  }
  pointee->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnToolAvailabilityOnSequence,
                     pointee->owner, std::move(json_copy)));
}

void MahoUnifiedAgentAdapter::OnToolAvailabilityOnSequence(std::string json_utf8) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed()) {
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kToolAvailabilityChanged;
  event.text = std::move(json_utf8);
  EmitEvent(std::move(event));
}
