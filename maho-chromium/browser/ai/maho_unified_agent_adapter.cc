// Copyright 2026 Maho Browser. All rights reserved.

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
#include "base/values.h"
#include "components/os_crypt/sync/os_crypt.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_acp_event_decoder.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/maho_core_holder.h"

MahoUnifiedAgentAdapter::FfiUserData::FfiUserData() = default;
MahoUnifiedAgentAdapter::FfiUserData::~FfiUserData() = default;

MahoUnifiedAgentAdapter::MahoUnifiedAgentAdapter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : prefs_(prefs), url_loader_factory_(std::move(url_loader_factory)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoUnifiedAgentAdapter::~MahoUnifiedAgentAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (agent_session_) {
    maho_agent_session_free(agent_session_);
    agent_session_ = nullptr;
  }
  // Reset our strong ref. Pending PostTask lambdas (if any) still hold their
  // own shared_ptr copies and will keep FfiUserData alive until they run.
  ffi_user_data_.reset();
  // Intentionally leak user_data_handle_: pending Rust-side callbacks may
  // still dereference it. The leak is bounded to one weak_ptr<> per adapter
  // lifetime (~16 bytes) and is acceptable until a Rust-side free_fn FFI
  // is added.
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
  accumulated_text_.clear();

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

    ffi_user_data_ = std::make_shared<FfiUserData>();
    ffi_user_data_->weak_adapter = weak_factory_.GetWeakPtr();
    ffi_user_data_->task_runner = base::SequencedTaskRunner::GetCurrentDefault();
    // Snapshot encrypted BYOK blobs from prefs on this (UI) sequence. The
    // callback will decrypt on demand from arbitrary Rust threads — encrypted
    // bytes are non-sensitive so no lock needed (see header comment).
    if (prefs_) {
      ffi_user_data_->encrypted_byok_openai_b64 =
          prefs_->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64);
      ffi_user_data_->encrypted_byok_anthropic_b64 =
          prefs_->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64);
    }
    user_data_handle_ = new std::weak_ptr<FfiUserData>(ffi_user_data_);

    // Wire permission + secure-storage callbacks. allow_insecure_key_storage
    // is FALSE: when no encrypted key is available the agent will fail-closed
    // rather than fall through to plaintext SQLite settings.
    agent_session_ = maho_agent_create_session(
        core, session_id_.c_str(), nullptr, /*allow_insecure_key_storage=*/false,
        &MahoUnifiedAgentAdapter::OnAgentPermission, user_data_handle_,
        &MahoUnifiedAgentAdapter::OnAgentSecureStorage, user_data_handle_);
    if (!agent_session_) {
      TransitionToError("Failed to create agent session.");
      return;
    }

    EmitConnectionState("connected");
  }

  bool success = maho_agent_send_message(
      agent_session_, message.c_str(),
      &MahoUnifiedAgentAdapter::OnAgentToken,
      &MahoUnifiedAgentAdapter::OnAgentComplete,
      &MahoUnifiedAgentAdapter::OnAgentError,
      user_data_handle_);

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
  ffi_user_data_.reset();
  // user_data_handle_ intentionally leaked (see destructor comment).
  user_data_handle_ = nullptr;

  session_id_.clear();
  accumulated_text_.clear();
  pending_message_.clear();
  current_on_event_.Reset();
}

// static
int MahoUnifiedAgentAdapter::OnAgentPermission(void* /*user_data*/,
                                                const char* tool_name,
                                                const char* /*arguments*/) {
  // Mirror iOS policy: default-deny, allowlist only safe read tools.
  // shell_exec is never granted from the desktop adapter callback path.
  if (!tool_name) return 1;  // Deny
  const std::string tool(tool_name);
  if (tool == "shell_exec") return 1;
  if (tool == "fs_read" || tool == "web_search") return 0;  // Allow
  return 1;  // Default deny
}

namespace {

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
  auto* weak = static_cast<std::weak_ptr<FfiUserData>*>(user_data);
  std::shared_ptr<FfiUserData> shared = weak->lock();
  if (!shared) {
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }

  const std::string provider_str(provider);
  const std::string* encrypted_b64 = nullptr;
  if (provider_str == "openai") {
    encrypted_b64 = &shared->encrypted_byok_openai_b64;
  } else if (provider_str == "anthropic") {
    encrypted_b64 = &shared->encrypted_byok_anthropic_b64;
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
  const bool decrypted = ::OSCrypt::DecryptString(encrypted_bytes, &plaintext);
  if (!decrypted || plaintext.empty()) {
    // Scrub any partial output before returning.
    if (!plaintext.empty()) {
      volatile char* p = plaintext.data();
      for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
    }
    return MahoAgentSecureKey{nullptr, 0, nullptr};
  }
  MahoAgentSecureKey result = AllocateSecureKeyFromPlaintext(plaintext);
  // Scrub the local plaintext copy before std::string destructor returns
  // the buffer to the allocator.
  volatile char* p = plaintext.data();
  for (size_t i = 0; i < plaintext.size(); ++i) p[i] = 0;
  return result;
}

// static
void MahoUnifiedAgentAdapter::OnAgentToken(void* user_data, const char* token) {
  if (!user_data || !token) return;
  auto* weak = static_cast<std::weak_ptr<FfiUserData>*>(user_data);
  std::shared_ptr<FfiUserData> shared = weak->lock();
  if (!shared) return;
  // LIFETIME: local `shared` keeps FfiUserData alive only across this stack frame.
  // PostTask binds `shared->weak_adapter` (base::WeakPtr, copyable); the queued
  // task does NOT depend on FfiUserData's lifetime — only on the adapter's.
  shared->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnTokenOnSequence,
                     shared->weak_adapter, std::string(token)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentComplete(void* user_data, const char* full_text, const char* tool_calls_json) {
  if (!user_data || !full_text) return;
  auto* weak = static_cast<std::weak_ptr<FfiUserData>*>(user_data);
  std::shared_ptr<FfiUserData> shared = weak->lock();
  if (!shared) return;
  shared->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnCompleteOnSequence,
                     shared->weak_adapter, std::string(full_text)));
}

// static
void MahoUnifiedAgentAdapter::OnAgentError(void* user_data, const char* error) {
  if (!user_data || !error) return;
  auto* weak = static_cast<std::weak_ptr<FfiUserData>*>(user_data);
  std::shared_ptr<FfiUserData> shared = weak->lock();
  if (!shared) return;
  shared->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoUnifiedAgentAdapter::OnErrorOnSequence,
                     shared->weak_adapter, std::string(error)));
}

void MahoUnifiedAgentAdapter::OnTokenOnSequence(std::string token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  accumulated_text_ += token;
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kAssistantToken;
  event.text = token;
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::OnCompleteOnSequence(std::string full_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kTurnComplete;
  event.text = full_text;
  accumulated_text_.clear();
  EmitEvent(std::move(event));
}

void MahoUnifiedAgentAdapter::OnErrorOnSequence(std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
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
