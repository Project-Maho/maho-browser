// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/ai/maho_unified_agent_adapter.h"

#include <cstring>
#include <memory>

#include "base/base64.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
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
#include "base/synchronization/waitable_event.h"
#include "content/public/browser/browser_thread.h"

namespace {

std::string GenerateTaskId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

}  // namespace

MahoUnifiedAgentAdapter::ByokSnapshot::ByokSnapshot() = default;

MahoUnifiedAgentAdapter::ByokSnapshot::~ByokSnapshot() = default;

MahoUnifiedAgentAdapter::MahoUnifiedAgentAdapter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      conversation_task_(std::make_unique<maho::ConversationTask>(GenerateTaskId())) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  callback_handle_ =
      std::make_unique<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>(
          base::SequencedTaskRunner::GetCurrentDefault(),
          weak_factory_.GetWeakPtr());
  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoUnifiedAgentAdapter::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()),
        os_crypt_async::Encryptor::Option::kEncryptSyncCompat);
  }
}


MahoUnifiedAgentAdapter::~MahoUnifiedAgentAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (agent_session_) {
    maho_agent_session_free(agent_session_);
    agent_session_ = nullptr;
  }

  // Cancel the callback handle — marks all pending Rust callbacks as no-ops
  // and properly frees the heap bridge (no leak, closing C5).
  callback_handle_->Cancel();

  // Clean up BYOK bridge (deterministic, no leak).
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
    agent_session_ = maho_agent_create_session(
        core, session_id_.c_str(), nullptr, /*allow_insecure_key_storage=*/false,
        &MahoUnifiedAgentAdapter::OnAgentPermission, this,
        &MahoUnifiedAgentAdapter::OnAgentSecureStorage, byok_bridge_);
    if (!agent_session_) {
      TransitionToError("Failed to create agent session.");
      return;
    }

    EmitConnectionState("connected");
  }

  // Get the user_data pointer from FfiCallbackHandle (owns the heap bridge).
  void* user_data = callback_handle_->user_data();

  bool success = maho_agent_send_message(
      agent_session_, message.c_str(),
      &MahoUnifiedAgentAdapter::OnAgentToken,
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
    maho_agent_cancel(agent_session_);
  }
  conversation_task_->Cancel();
}

void MahoUnifiedAgentAdapter::RespondToApproval(const std::string& approval_id,
                                               bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::string MahoUnifiedAgentAdapter::GetRuntimeSessionId() const {
  return session_id_;
}

void MahoUnifiedAgentAdapter::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (agent_session_) {
    maho_agent_session_free(agent_session_);
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
  if (!user_data) {
    if (!tool_name) return MahoAgentPermissionDecision_Deny;
    const std::string tool(tool_name);
    if (tool == "shell_exec") return MahoAgentPermissionDecision_Deny;
    if (tool == "fs_read" || tool == "web_search") return MahoAgentPermissionDecision_Allow;
    return MahoAgentPermissionDecision_Deny;
  }
  MahoUnifiedAgentAdapter* adapter = static_cast<MahoUnifiedAgentAdapter*>(user_data);
  return adapter->OnAgentPermissionOnSequence(tool_name, arguments);
}

MahoAgentPermissionDecision MahoUnifiedAgentAdapter::OnAgentPermissionOnSequence(
    const char* tool_name,
    const char* arguments) {
  if (!tool_name) return MahoAgentPermissionDecision_Deny;
  const std::string tool(tool_name);
  if (tool == "shell_exec") return MahoAgentPermissionDecision_Deny;

  if (permission_extension_id_.empty()) {
    if (tool == "fs_read" || tool == "web_search") return MahoAgentPermissionDecision_Allow;
    return MahoAgentPermissionDecision_Deny;
  }

  // 1. Check persistent grants in prefs
  if (prefs_) {
    const auto& grants = prefs_->GetDict(maho::ai_prefs::kAgentToolGrants);
    const auto* tool_list = grants.FindList(permission_extension_id_);
    if (tool_list) {
      for (const auto& item : *tool_list) {
        if (item.is_string() && item.GetString() == tool) {
          return MahoAgentPermissionDecision_Allow;
        }
      }
    }
  }

  // 2. Check session-level grants
  if (session_grants_.contains(tool)) {
    return MahoAgentPermissionDecision_Allow;
  }

  // 3. Show lazy prompt and block using WaitableEvent
  PromptDecision decision_val = PromptDecision::kDeny;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          &MahoUnifiedAgentAdapter::ShowPermissionPromptOnUIThread,
          weak_factory_.GetWeakPtr(),
          permission_extension_id_,
          tool,
          base::BindOnce(
              [](PromptDecision* out_decision, base::WaitableEvent* event, PromptDecision result) {
                *out_decision = result;
                event->Signal();
              },
              &decision_val,
              &event)));

  event.Wait();

  LOG(INFO) << "MahoUnifiedAgentAdapter: Extension " << permission_extension_id_
            << " requested tool " << tool << ". Decision: "
            << (decision_val == PromptDecision::kAllow ? "ALLOW"
                : decision_val == PromptDecision::kAllowOnce ? "ALLOW_ONCE"
                : "DENY");

  if (decision_val == PromptDecision::kAllow) {
    if (prefs_) {
      ScopedDictPrefUpdate update(prefs_, maho::ai_prefs::kAgentToolGrants);
      auto* tool_list = update->EnsureList(permission_extension_id_);
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
    }
    return MahoAgentPermissionDecision_Allow;
  } else if (decision_val == PromptDecision::kAllowOnce) {
    session_grants_.insert(tool);
    return MahoAgentPermissionDecision_Allow;
  }

  return MahoAgentPermissionDecision_Deny;
}

void MahoUnifiedAgentAdapter::ShowPermissionPromptOnUIThread(
    const std::string& extension_id,
    const std::string& tool_name,
    base::OnceCallback<void(PromptDecision)> callback) {
  BrowserWindowInterface* browser = chrome::FindLastActive();
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
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }
  // user_data is a std::shared_ptr<ByokSnapshot>* bridge (heap-allocated).
  auto* bridge = static_cast<std::shared_ptr<ByokSnapshot>*>(user_data);
  std::shared_ptr<ByokSnapshot> snapshot = *bridge;
  if (!snapshot) {
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }

  if (!snapshot->encryptor) {
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }

  const std::string provider_str(provider);
  const std::string* encrypted_b64 = nullptr;
  if (provider_str == "openai") {
    encrypted_b64 = &snapshot->encrypted_byok_openai_b64;
  } else if (provider_str == "anthropic") {
    encrypted_b64 = &snapshot->encrypted_byok_anthropic_b64;
  } else {
    DLOG(WARNING) << "OnAgentSecureStorage: unknown provider '" << provider_str
                  << "'; returning null key triple";
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }

  if (encrypted_b64->empty()) {
    // No key configured for this provider; Rust runtime will fail closed
    // when allow_insecure_key_storage=false (see SubmitMessage).
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }

  std::string encrypted_bytes;
  if (!base::Base64Decode(*encrypted_b64, &encrypted_bytes)) {
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }
  std::string plaintext;
  const bool decrypted = snapshot->encryptor->DecryptString(encrypted_bytes, &plaintext);

  if (!decrypted || plaintext.empty()) {
    // Scrub any partial output before returning.
    if (!plaintext.empty()) {
      volatile char* p = plaintext.data();
      for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
    }
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }
  MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(plaintext);
  // Set crashpad annotation to indicate a BYOK key is active in memory
  g_byok_key_status.Set("active");
  // Scrub the local plaintext copy before std::string destructor returns
  // the buffer to the allocator.
  volatile char* p = plaintext.data();
  for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
  return result;
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
  conversation_task_->Complete();
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kTurnComplete;
  event.text = full_text;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::OnErrorOnSequence(std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
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
