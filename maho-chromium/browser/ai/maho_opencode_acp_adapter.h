// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_OPENCODE_ACP_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_OPENCODE_ACP_ADAPTER_H_

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

struct MahoAcpHandle;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

// ACP-based adapter that implements MahoAiRuntimeAdapter by calling into
// the maho-ffi C-ABI surface. Contains zero wire-protocol knowledge —
// all ACP wire logic lives in the Rust crate behind the FFI boundary.
class MahoOpenCodeAcpAdapter : public MahoAiRuntimeAdapter {
 public:
  MahoOpenCodeAcpAdapter(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoOpenCodeAcpAdapter() override;

  MahoOpenCodeAcpAdapter(const MahoOpenCodeAcpAdapter&) = delete;
  MahoOpenCodeAcpAdapter& operator=(const MahoOpenCodeAcpAdapter&) = delete;

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

  enum class State {
    kIdle,
    kStarting,
    kInitializing,
    kSessionPending,
    kReady,
    kSubmitting,
    kAwaitingTurn,
    kError,
  };

  State state() const { return state_; }

 private:
  friend class MahoOpenCodeAcpAdapterTest;

  // SAFETY: FfiUserData is heap-allocated and outlives the FFI handle.
  // It is freed in the destructor AFTER maho_acp_destroy returns, ensuring
  // no use-after-free if the FFI fires a final event during shutdown.
  struct FfiUserData {
    FfiUserData();
    ~FfiUserData();
    base::WeakPtr<MahoOpenCodeAcpAdapter> weak_adapter;
    scoped_refptr<base::SequencedTaskRunner> task_runner;
  };

  static void OnFfiEvent(void* user_data,
                         const uint8_t* blob,
                         size_t len);

  void OnEventOnSequence(std::vector<uint8_t> blob);
  void DriveStateMachine(const std::string& kind,
                         const base::DictValue& data);
  void EmitEvent(MahoAiRuntimeEvent event);
  void EmitConnectionState(const std::string& state_str);
  void TransitionToError(const std::string& message);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  State state_ = State::kIdle;
  RAW_PTR_EXCLUSION MahoAcpHandle* acp_handle_ = nullptr;
  std::unique_ptr<FfiUserData> ffi_user_data_;

  std::string session_id_;
  std::string accumulated_text_;
  RuntimeEventCallback current_on_event_;
  std::string pending_message_;

  // Maps approval_id (exposed to page handler) -> json_rpc_id (FFI).
  std::unordered_map<std::string, int64_t> approval_to_rpc_id_;
  int next_approval_id_ = 1;

  base::WeakPtrFactory<MahoOpenCodeAcpAdapter> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_OPENCODE_ACP_ADAPTER_H_
