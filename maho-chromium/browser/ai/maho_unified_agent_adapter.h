// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "maho/third_party/maho/maho_ffi.h"



struct MahoAcpHandle;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

// Unified Agent adapter that implements MahoAiRuntimeAdapter by calling into
// the maho-ffi C-ABI surface.
class MahoUnifiedAgentAdapter : public MahoAiRuntimeAdapter {
 public:
  MahoUnifiedAgentAdapter(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoUnifiedAgentAdapter() override;

  MahoUnifiedAgentAdapter(const MahoUnifiedAgentAdapter&) = delete;
  MahoUnifiedAgentAdapter& operator=(const MahoUnifiedAgentAdapter&) = delete;

  // MahoAiRuntimeAdapter:
  std::string GetAdapterName() const override;
  bool IsAvailable() const override;
  void SubmitMessage(const std::string& message,
                     maho_ai::mojom::ChatIntent chat_intent,
                     bool attach_browser_context,
                     maho_ai::mojom::InteractionMode mode,
                     RuntimeEventCallback on_event) override;
  void CancelCurrentTurn() override;
  void RespondToApproval(const std::string& approval_id,
                         bool approved) override;
  std::string GetRuntimeSessionId() const override;
  void Reset() override;

 private:
  friend class MahoUnifiedAgentAdapterTest;

  // SAFETY: FfiUserData is heap-allocated via shared_ptr. The adapter owns
  // a strong reference; callbacks dispatched via PostTask capture a copy of
  // the shared_ptr, keeping FfiUserData alive until the posted task runs.
  // This closes the UAF window between ~MahoUnifiedAgentAdapter (drops the
  // adapter's strong ref) and pending in-flight Rust callbacks that
  // dereference user_data.
  struct FfiUserData {
    FfiUserData();
    ~FfiUserData();
    base::WeakPtr<MahoUnifiedAgentAdapter> weak_adapter;
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    // BYOK encrypted blobs loaded at session creation (UI thread); decrypted
    // on demand in OnAgentSecureStorage. Encrypted bytes are not sensitive
    // so cross-thread access without lock is safe. Snapshot semantics:
    // pref changes during a session are NOT reflected; the new key takes
    // effect on the next session.
    std::string encrypted_byok_openai_b64;
    std::string encrypted_byok_anthropic_b64;
    std::shared_ptr<os_crypt_async::Encryptor> encryptor;
  };


  static MahoAgentPermissionDecision OnAgentPermission(void* user_data,
                                                        const char* tool_name,
                                                        const char* arguments);
  static MahoAgentSecureKey OnAgentSecureStorage(void* user_data,
                                                  const char* provider);

  static void OnAgentToken(void* user_data, const char* token);
  static void OnAgentComplete(void* user_data, const char* full_text, const char* tool_calls_json);
  static void OnAgentError(void* user_data, const char* error);

  void OnTokenOnSequence(std::string token);
  void OnCompleteOnSequence(std::string full_text);
  void OnErrorOnSequence(std::string error);
  void EmitEvent(MahoAiRuntimeEvent event);
  void EmitConnectionState(const std::string& state_str);
  void TransitionToError(const std::string& message);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  RAW_PTR_EXCLUSION struct MahoAgentSession* agent_session_ = nullptr;
  // Strong ref owned by adapter. Callbacks dispatched via PostTask capture
  // a shared_ptr copy to keep FfiUserData alive until the task runs, even
  // if the adapter is destroyed in the meantime.
  std::shared_ptr<FfiUserData> ffi_user_data_;
  // Weak handle on heap that we pass to Rust as user_data. Allocated when
  // ffi_user_data_ is created and intentionally leaked at destruction
  // (~16 bytes) because pending Rust-side callbacks may still dereference
  // it. A future enhancement can free this via a Rust-side free_fn.
  RAW_PTR_EXCLUSION std::weak_ptr<FfiUserData>* user_data_handle_ = nullptr;


  std::string session_id_;
  std::string accumulated_text_;
  RuntimeEventCallback current_on_event_;
  std::string pending_message_;

  std::shared_ptr<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);

  base::WeakPtrFactory<MahoUnifiedAgentAdapter> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
