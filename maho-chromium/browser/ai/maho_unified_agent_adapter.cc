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
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_finder.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"  // nogncheck
#include "content/public/browser/browser_thread.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/ai/maho_browser_tool_executor.h"  // nogncheck
#include "maho/browser/maho_features.h"
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

struct BrowserToolCallState {
  base::WaitableEvent done{base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED};
  std::atomic<bool> cancelled{false};
  std::string result_json;
  std::string result_error;
};

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

}  // namespace

namespace {
const MahoAgentFfi kProductionFfi = {
    &maho_agent_create_session,
    &maho_agent_set_approval_policy,
    &maho_agent_send_message,
    &maho_agent_cancel,
    &maho_agent_session_free,
};
}  // namespace

const MahoAgentFfi* GetProductionMahoAgentFfi() {
  return &kProductionFfi;
}

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
  if (!byok_snapshot_) {
    return;
  }

  auto replacement = std::make_shared<ByokSnapshot>(*byok_snapshot_);
  if (provider == "openai") {
    replacement->encrypted_byok_openai_b64.clear();
  } else if (provider == "anthropic") {
    replacement->encrypted_byok_anthropic_b64.clear();
  } else if (provider == "openai-compatible") {
    replacement->custom_api_key.clear();
  } else {
    return;
  }

  byok_snapshot_ = replacement;
  if (byok_bridge_) {
    // A callback that already loaded the prior immutable snapshot may finish
    // with that copy. Every callback that starts after this publication sees
    // the replacement, without racing on either snapshot's strings.
    std::atomic_store_explicit(byok_bridge_.get(), std::move(replacement),
                               std::memory_order_release);
  }
}

MahoUnifiedAgentAdapter::ByokSnapshot::ByokSnapshot() = default;

MahoUnifiedAgentAdapter::ByokSnapshot::~ByokSnapshot() = default;

MahoUnifiedAgentAdapter::MahoUnifiedAgentAdapter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    base::RepeatingCallback<bool()> ai_gate,
    base::RepeatingCallback<Browser*()> bound_browser_resolver)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      ai_gate_(std::move(ai_gate)),
      bound_browser_resolver_(std::move(bound_browser_resolver)),
      conversation_task_(std::make_unique<maho::ConversationTask>(GenerateTaskId())) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  agent_ffi_ = GetProductionMahoAgentFfi();
  owning_task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();
  active_adapter_weak_ptr_ = weak_factory_.GetWeakPtr();
  callback_handle_ =
      std::make_unique<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>(
          owning_task_runner_,
          weak_factory_.GetWeakPtr());
  tool_availability_handle_ =
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

  if (tool_availability_callback_token_ != 0) {
    if (MahoCore* core = maho::GetCore()) {
      maho_core_unregister_tool_availability_callback(
          core, tool_availability_callback_token_);
    }
    tool_availability_callback_token_ = 0;
  }

  if (agent_session_) {
    agent_ffi_->session_free(agent_session_);
    agent_session_ = nullptr;
  }

  callback_handle_->Cancel();
  if (tool_availability_handle_) {
    tool_availability_handle_->Cancel();
  }

  delete byok_bridge_;
  byok_bridge_ = nullptr;
  byok_snapshot_.reset();
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

  current_on_event_ = on_event;

  if (!IsAiAllowed()) {
    TransitionToError("AI is not available in this context.");
    return;
  }

  conversation_task_->StartStreaming();

  if (!agent_session_) {
    EmitConnectionState("connecting");

    MahoCore* core = maho::GetCore();
    if (!core) {
      TransitionToError("MahoCore not initialized.");
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

    // Snapshot BYOK data for secure-storage callback (immutable, thread-safe).
    byok_snapshot_ = std::make_shared<ByokSnapshot>();
    if (prefs_) {
      byok_snapshot_->encrypted_byok_openai_b64 =
          prefs_->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64);
      byok_snapshot_->encrypted_byok_anthropic_b64 =
          prefs_->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64);
      byok_snapshot_->custom_provider =
          prefs_->GetString(maho::ai_prefs::kProvider);
      byok_snapshot_->custom_api_key =
          prefs_->GetString(maho::ai_prefs::kApiKey);
      byok_snapshot_->custom_base_url =
          prefs_->GetString(maho::ai_prefs::kBaseUrl);
      byok_snapshot_->custom_model =
          prefs_->GetString(maho::ai_prefs::kModel);
      const auto& creds = prefs_->GetDict(maho::ai_prefs::kMcpCredentials);
      for (auto item : creds) {
        if (item.second.is_string()) {
          byok_snapshot_->mcp_credentials[item.first] = item.second.GetString();
        }
      }
    }
    if (encryptor_) {
      byok_snapshot_->encryptor = encryptor_;
    }
    // Heap bridge for secure_storage_user_data (properly owned, deleted in
    // Reset/destructor).
    byok_bridge_ = new std::shared_ptr<ByokSnapshot>(byok_snapshot_);

    // Wire permission + secure-storage callbacks. allow_insecure_key_storage
    // is FALSE: when no encrypted key is available the agent will fail-closed
    // rather than fall through to plaintext SQLite settings.
    std::string active_space_id;
    if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance()) {
      Browser* browser = ResolveBoundBrowser();
      active_space_id = browser ? bridge->GetActiveSpaceId(browser)
                                : bridge->GetActiveSpaceId();
    }

    agent_session_ = agent_ffi_->create_session(
        core, session_id_.c_str(), nullptr, /*allow_insecure_key_storage=*/false,
        &MahoUnifiedAgentAdapter::OnAgentPermission, callback_handle_->user_data(),
        &MahoUnifiedAgentAdapter::OnAgentSecureStorage, byok_bridge_,
        &MahoUnifiedAgentAdapter::OnAgentBrowserTool,
        callback_handle_->user_data(),
        active_space_id.empty() ? nullptr : active_space_id.c_str());
    if (!agent_session_) {
      TransitionToError("Failed to create agent session.");
      return;
    }

    EmitConnectionState("connected");
  }

  // Get the user_data pointer from FfiCallbackHandle (owns the heap bridge).
  void* user_data = callback_handle_->user_data();

  // Push the current global approval policy each turn so a "deny" kill-switch
  // (or "allow"/"prompt") toggled via prefs takes effect on the next message.
  if (prefs_) {
    const std::string policy = prefs_->GetString(maho::ai_prefs::kApprovalPolicy);
    agent_ffi_->set_approval_policy(agent_session_, policy.c_str());
  }

  bool success = agent_ffi_->send_message(
      agent_session_, message.c_str(),
      &MahoUnifiedAgentAdapter::OnAgentToken,
      nullptr,  // on_thinking
      nullptr,  // on_tool_call
      nullptr,  // on_tool_result
      &MahoUnifiedAgentAdapter::OnAgentComplete,
      &MahoUnifiedAgentAdapter::OnAgentError,
      user_data);

  if (!success) {
    TransitionToError("Failed to send message to agent.");
    return;
  }
}

void MahoUnifiedAgentAdapter::CancelCurrentTurn() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (agent_session_) {
    agent_ffi_->cancel(agent_session_);
  }
  DenyAllPendingApprovals();
  conversation_task_->Cancel();
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

  if (state && !state->cancelled.load()) {
    state->decision =
        approved ? PromptDecision::kAllowOnce : PromptDecision::kDeny;
  }
  if (state) {
    state->event.Signal();
  }

  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kApprovalResult;
  event.payload.Set("approval_id", approval_id);
  event.payload.Set("approved", approved);
  event.payload.Set("approval_state", approved ? "approved" : "denied");
  event.payload.Set("approval_decision", approved ? "allow_once" : "deny");
  if (state) {
    event.payload.Set("approval_policy", state->approval_policy);
    event.payload.Set("sensitivity", state->sensitivity);
    event.payload.Set("page_derived_justification",
                      state->page_derived_justification);
  }
  EmitEvent(std::move(event));
}

std::string MahoUnifiedAgentAdapter::GetRuntimeSessionId() const {
  return session_id_;
}

void MahoUnifiedAgentAdapter::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DenyAllPendingApprovals();

  if (agent_session_) {
    agent_ffi_->session_free(agent_session_);
    agent_session_ = nullptr;
  }

  // Cancel the current callback handle and create a fresh one for the next
  // session. No leak: Cancel() deletes the heap bridge deterministically.
  callback_handle_->Cancel();
  callback_handle_ = std::make_unique<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>(
      base::SequencedTaskRunner::GetCurrentDefault(),
      weak_factory_.GetWeakPtr());

  // Fresh conversation task for the next turn.
  conversation_task_ = std::make_unique<maho::ConversationTask>(GenerateTaskId());

  // Cancel any in-flight tool execution.
  if (tool_graph_) {
    tool_graph_->CancelInflight();
  }

  // Clean up BYOK bridge.
  delete byok_bridge_;
  byok_bridge_ = nullptr;
  byok_snapshot_.reset();

  session_id_.clear();
  pending_message_.clear();
  current_on_event_.Reset();
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
  const bool page_derived_justification =
      ExtractPageDerivedJustification(arguments);
  pointee->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoUnifiedAgentAdapter> adapter, std::string tool,
             std::shared_ptr<PermissionCallState> state,
             bool page_derived_justification) {
            if (!adapter) {
              state->decision = PromptDecision::kDeny;
              state->event.Signal();
              return;
            }
            adapter->DecidePermissionOnUiThread(
                tool, adapter->permission_extension_id_, state,
                page_derived_justification);
          },
          pointee->owner, tool, call_state, page_derived_justification));

  PromptDecision decision_val;
  if (call_state->event.TimedWait(base::Seconds(30))) {
    decision_val = call_state->decision;
  } else {
    call_state->cancelled.store(true);
    decision_val = PromptDecision::kDeny;
  }

  return decision_val == PromptDecision::kDeny
             ? MahoAgentPermissionDecision_Deny
             : MahoAgentPermissionDecision_Allow;
}

std::optional<PromptDecision> MahoUnifiedAgentAdapter::EvaluatePermissionSync(
    const std::string& tool,
    const std::string& extension_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (tool == "shell_exec") {
    return PromptDecision::kDeny;
  }

  // 0. Global approval policy override.
  if (prefs_) {
    const std::string policy = prefs_->GetString(maho::ai_prefs::kApprovalPolicy);
    if (policy == "deny") {
      return PromptDecision::kDeny;
    }
    if (policy == "allow") {
      return PromptDecision::kAllow;
    }
  }

  if (tool == "read_current_page" || tool == "get_selected_text" ||
      tool == "get_active_tab" || tool == "search_in_page" ||
      tool == "extract_structured_page_context") {
    return PromptDecision::kAllow;
  }

  if (extension_id.empty()) {
    if (tool != "fs_read" && tool != "web_search") {
      return PromptDecision::kDeny;
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
    const std::string& tool,
    const std::string& extension_id,
    std::shared_ptr<PermissionCallState> state,
    bool page_derived_justification) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

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
  if (current_on_event_) {
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
             PromptDecision result) {
            if (state->cancelled.load()) {
              state->event.Signal();
              return;
            }
            if (result == PromptDecision::kAllow && adapter && adapter->prefs_ &&
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
            } else if (result == PromptDecision::kAllowOnce && adapter) {
              adapter->session_grants_.insert(tool);
            }
            state->decision = result;
            state->event.Signal();
          },
          weak_factory_.GetWeakPtr(), tool, extension_id, state));
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
  DecidePermissionOnUiThread(tool, extension_id, state);
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
  auto* bridge = static_cast<std::shared_ptr<ByokSnapshot>*>(user_data);
  std::shared_ptr<ByokSnapshot> snapshot =
      std::atomic_load_explicit(bridge, std::memory_order_acquire);
  if (!snapshot) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }

  const std::string provider_str(provider);

  if (base::StartsWith(provider_str, "mcp:", base::CompareCase::SENSITIVE)) {
    std::string keychain_id = provider_str.substr(4);
    auto it = snapshot->mcp_credentials.find(keychain_id);
    if (it == snapshot->mcp_credentials.end()) {
      return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
    }
    std::string encrypted_b64 = it->second;
    std::string encrypted_bytes;
    if (!base::Base64Decode(encrypted_b64, &encrypted_bytes)) {
      return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
    }
    std::string plaintext_key;
    if (snapshot->encryptor && snapshot->encryptor->DecryptString(encrypted_bytes, &plaintext_key)) {
      MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(plaintext_key);
      g_byok_key_status.Set("active");
      volatile char* p = plaintext_key.data();
      for (size_t i = 0; i < plaintext_key.size(); ++i) p[i] = 0;
      return result;
    }
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
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
    if (!snapshot->custom_base_url.empty()) {
      result.base_url = strdup(snapshot->custom_base_url.c_str());
      result.cstring_free_fn = reinterpret_cast<void(*)(char*)>(&::free);
    }
    if (!snapshot->custom_model.empty()) {
      result.model = strdup(snapshot->custom_model.c_str());
      result.cstring_free_fn = reinterpret_cast<void(*)(char*)>(&::free);
    }
    g_byok_key_status.Set("active");
    return result;
  }

  // Encrypted BYOK path: original behaviour.
  if (!snapshot->encryptor) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }

  const std::string* encrypted_b64 = nullptr;
  if (provider_str == "openai") {
    encrypted_b64 = &snapshot->encrypted_byok_openai_b64;
  } else if (provider_str == "anthropic") {
    encrypted_b64 = &snapshot->encrypted_byok_anthropic_b64;
  } else {
    DLOG(WARNING) << "OnAgentSecureStorage: unknown provider '" << provider_str
                  << "'; returning null key triple";
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }

  if (encrypted_b64->empty()) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }

  std::string encrypted_bytes;
  if (!base::Base64Decode(*encrypted_b64, &encrypted_bytes)) {
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }
  std::string plaintext;
  const bool decrypted = snapshot->encryptor->DecryptString(encrypted_bytes, &plaintext);

  if (!decrypted || plaintext.empty()) {
    if (!plaintext.empty()) {
      volatile char* p = plaintext.data();
      for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
    }
    return MahoAgentSecureKey{nullptr, 0, nullptr, nullptr, nullptr, nullptr};
  }
  MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(plaintext);
  g_byok_key_status.Set("active");
  volatile char* p = plaintext.data();
  for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
  return result;
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

  const maho::ai::BrowserActionContract* browser_action_contract =
      maho::ai::FindBrowserActionContract(tool_name_str);
  const bool browser_action_requires_approval =
      browser_action_contract &&
      maho::ai::RequiresApproval(*browser_action_contract) &&
      base::FeatureList::IsEnabled(maho::features::kMahoAgentBrowserActions);
  if (browser_action_requires_approval) {
    MahoAgentPermissionDecision permission =
        OnAgentPermission(user_data, tool_name, args_json);
    if (permission != MahoAgentPermissionDecision_Allow) {
      return MakeErrorToolResult("Browser action denied by approval policy");
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

  auto ui_runner = content::GetUIThreadTaskRunner({});
  auto call_state = std::make_shared<BrowserToolCallState>();

  ui_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
              std::string tool_name, std::string args_json_in,
              std::shared_ptr<BrowserToolCallState> state,
              bool browser_action_preapproved) {
            if (!adapter || !adapter->IsAiAllowed()) {
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
                       const MahoBrowserToolExecutor::BrowserActionAuthorization&
                           metadata) {
                      MahoBrowserToolExecutor::BrowserActionApprovalDecision
                          decision;
                      if (!adapter) {
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
                    browser_action_preapproved, adapter),
                nullptr,
                maho::MahoMcpSession::GetLeaseRegistryForBrowserActions(),
                std::move(browser_action_lease_holder_id));
            executor->Execute(
                tool_name, args_dict,
                base::BindOnce(
                    [](std::shared_ptr<MahoBrowserToolExecutor> keep_alive,
                       std::shared_ptr<BrowserToolCallState> state,
                       base::DictValue result) {
                      if (!state->cancelled.load()) {
                        std::string json;
                        base::JSONWriter::Write(base::Value(std::move(result)),
                                                &json);
                        state->result_json = std::move(json);
                      }
                      state->done.Signal();
                    },
                    executor, state));
          },
          pointee->owner, tool_name_str, args_json_str, call_state,
          browser_action_requires_approval));

  std::string result_json;
  std::string result_error;
  if (call_state->done.TimedWait(base::Seconds(30))) {
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
                     pointee->owner, std::move(token_copy)));
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
                     pointee->owner, std::move(text_copy)));
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
                     pointee->owner, std::move(error_copy)));
}

void MahoUnifiedAgentAdapter::OnTokenOnSequence(std::string token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed()) {
    return;
  }
  if (!conversation_task_->OnToken(token)) {
    // 2 MB cap reached — terminate with error (H13).
    conversation_task_->Error("response exceeded 2 MB cap");
    TransitionToError("response exceeded 2 MB cap");
    return;
  }
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kAssistantToken;
  event.text = token;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::OnCompleteOnSequence(std::string full_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed()) {
    return;
  }
  conversation_task_->Complete();
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kTurnComplete;
  event.text = full_text;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::OnErrorOnSequence(std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAiAllowed()) {
    return;
  }
  conversation_task_->Error(error);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = error;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::EmitEvent(MahoAiRuntimeEvent event) {
  if (current_on_event_) {
    current_on_event_.Run(std::move(event));
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

void MahoUnifiedAgentAdapter::OnOsCryptReady(os_crypt_async::Encryptor encryptor) {
  encryptor_ = std::make_shared<os_crypt_async::Encryptor>(std::move(encryptor));
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
