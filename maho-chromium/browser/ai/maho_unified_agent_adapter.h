// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_conversation_task.h"
#include "maho/browser/ai/maho_ffi_callback_handle.h"
#include "maho/browser/ai/maho_tool_execution_graph.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "maho/third_party/maho/maho_ffi.h"



struct MahoAcpHandle;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

// Unified Agent adapter that implements MahoAiRuntimeAdapter by calling into
// the maho-ffi C-ABI surface.
//
// Ownership of cross-thread state is split across three primitives:
//   - FfiCallbackHandle<T>: owns the void* user_data bridge for Rust callbacks;
//     guarantees no leak (C5) and cancellation-based lifetime (H6).
//   - ConversationTask: owns the per-turn accumulator with 2 MB cap (H13).
//   - ToolExecutionGraph: owns the tool executor instance (C2, lifetime).
enum class PromptDecision {
  kAllow = 0,
  kDeny = 1,
  kAllowOnce = 2
};

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
  friend class MahoUnifiedAgentAdapterTestHelper;

  // Thread-safe, immutable BYOK data snapshot for OnAgentSecureStorage.
  // Created once per session in SubmitMessage; immutable thereafter so
  // safe to read from arbitrary Rust threads without locks.
  struct ByokSnapshot {
    ByokSnapshot();
    ~ByokSnapshot();

    std::string encrypted_byok_openai_b64;
    std::string encrypted_byok_anthropic_b64;
    std::shared_ptr<os_crypt_async::Encryptor> encryptor;
    std::string custom_provider;
    std::string custom_api_key;
    std::string custom_base_url;
    std::string custom_model;
    std::map<std::string, std::string> mcp_credentials;
  };

  static MahoAgentPermissionDecision OnAgentPermission(void* user_data,
                                                        const char* tool_name,
                                                        const char* arguments);
  static MahoAgentSecureKey OnAgentSecureStorage(void* user_data,
                                                  const char* provider);
  static MahoAgentToolResult OnAgentBrowserTool(void* user_data,
                                                const char* tool_name,
                                                const char* args_json);

  static void OnAgentToken(void* user_data, const char* token);
  static void OnAgentComplete(void* user_data, const char* full_text, const char* tool_calls_json);
  static void OnAgentError(void* user_data, const char* error);

  static void OnToolAvailabilityFfi(void* user_data, const char* json_utf8);
  void OnToolAvailabilityOnSequence(std::string json_utf8);

  void OnTokenOnSequence(std::string token);
  void OnCompleteOnSequence(std::string full_text);
  void OnErrorOnSequence(std::string error);
  void EmitEvent(MahoAiRuntimeEvent event);
  void EmitConnectionState(const std::string& state_str);
  void TransitionToError(const std::string& message);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  raw_ptr<struct MahoAgentSession> agent_session_ = nullptr;

  // --- Three primitives (4.1.1/4.1.2/4.1.3) ---

  // Owns the void* user_data bridge passed to maho_agent_send_message
  // callbacks (OnAgentToken, OnAgentComplete, OnAgentError). Closes C5
  // (no leak) and H6 (cancellation-based lifetime contract).
  std::unique_ptr<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>
      callback_handle_;

  // Per-turn conversation state with bounded 2 MB accumulator (H13).
  std::unique_ptr<maho::ConversationTask> conversation_task_;

  // Tool executor with deterministic lifetime (C2). nullptr until a
  // Browser* is available (adapter may be constructed before Browser).
  std::unique_ptr<maho::ToolExecutionGraph> tool_graph_;

  // --- BYOK secure storage bridge ---
  // Heap bridge for OnAgentSecureStorage callback user_data.
  // Properly deleted in Reset()/destructor (no leak).
  std::shared_ptr<ByokSnapshot> byok_snapshot_;
  raw_ptr<std::shared_ptr<ByokSnapshot>> byok_bridge_ = nullptr;

  std::string session_id_;
  RuntimeEventCallback current_on_event_;
  std::string pending_message_;

  std::string permission_extension_id_;
  std::set<std::string> session_grants_;
  void ShowPermissionPromptOnUIThread(
      const std::string& extension_id,
      const std::string& tool_name,
      base::OnceCallback<void(PromptDecision)> callback);
  MahoAgentPermissionDecision OnAgentPermissionOnSequence(
      const char* tool_name,
      const char* arguments);

  std::shared_ptr<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);

  uint64_t tool_availability_callback_token_ = 0;
  scoped_refptr<base::SequencedTaskRunner> owning_task_runner_;

  base::WeakPtrFactory<MahoUnifiedAgentAdapter> weak_factory_{this};

};

#endif  // MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
