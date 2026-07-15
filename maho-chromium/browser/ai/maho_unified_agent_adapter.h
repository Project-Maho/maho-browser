// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_

#include <map>
#include <memory>
#include <optional>
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
struct PermissionCallState;
class Browser;
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

struct PermissionCallState;

// Injectable table of the maho-agent C FFI entry points the adapter calls.
// Production uses GetProductionMahoAgentFfi() (forwards to the real symbols);
// tests inject a fake table via set_agent_ffi_for_testing() to exercise the
// session lifecycle without linking against the real maho-core prebuilt.
struct MahoAgentFfi {
  MahoAgentSession* (*create_session)(
      MahoCore* core,
      const char* session_id,
      const char* workspace_root,
      bool allow_insecure_key_storage,
      MahoAgentPermissionDecision (*permission_cb)(void*, const char*, const char*),
      void* permission_user_data,
      MahoAgentSecureKey (*secure_storage_cb)(void*, const char*),
      void* secure_storage_user_data,
      MahoAgentToolResult (*browser_tool_cb)(void*, const char*, const char*),
      void* browser_tool_user_data,
      const char* space_id);
  void (*set_approval_policy)(MahoAgentSession* session, const char* policy);
  bool (*send_message)(MahoAgentSession* session,
                       const char* message,
                       void (*on_token)(void*, const char*),
                       void (*on_complete)(void*, const char*, const char*),
                       void (*on_error)(void*, const char*),
                       void* user_data);
  bool (*cancel)(MahoAgentSession* session);
  void (*session_free)(MahoAgentSession* session);
};

const MahoAgentFfi* GetProductionMahoAgentFfi();

class MahoUnifiedAgentAdapter : public MahoAiRuntimeAdapter {
 public:
  MahoUnifiedAgentAdapter(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>(),
      base::RepeatingCallback<Browser*()> bound_browser_resolver =
          base::RepeatingCallback<Browser*()>());
  ~MahoUnifiedAgentAdapter() override;

  MahoUnifiedAgentAdapter(const MahoUnifiedAgentAdapter&) = delete;
  MahoUnifiedAgentAdapter& operator=(const MahoUnifiedAgentAdapter&) = delete;

  static void ClearActiveBYOKKeys(const std::string& provider);
  static std::set<MahoUnifiedAgentAdapter*>& GetActiveAdapters();

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
  friend class MahoUnifiedAgentAdapterPolicyTest;
  friend class MahoUnifiedAgentAdapterLifecycleTest;

  void set_agent_ffi_for_testing(const MahoAgentFfi* ffi) { agent_ffi_ = ffi; }

  // Thread-safe, immutable BYOK data snapshot for OnAgentSecureStorage.
  // Once published, a snapshot is never mutated, so arbitrary Rust threads
  // may read it without locking. Key invalidation publishes a replacement.
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

  void ClearBYOKKeyOnOwningSequence(std::string provider);

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

  // R-9: fail-closed kAI gate and the token-bound browser resolver. A denying
  // gate drops async callbacks; the resolver replaces chrome::FindLastActive()
  // so browser tools/permission prompts never retarget the active window.
  base::RepeatingCallback<bool()> ai_gate_;
  base::RepeatingCallback<Browser*()> bound_browser_resolver_;
  bool IsAiAllowed();
  Browser* ResolveBoundBrowser();

  raw_ptr<struct MahoAgentSession> agent_session_ = nullptr;

  // maho-agent FFI entry points. Defaults to the production table in the
  // constructor; overridable in tests via set_agent_ffi_for_testing().
  raw_ptr<const MahoAgentFfi> agent_ffi_ = nullptr;

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
  // Heap bridge for OnAgentSecureStorage callback user_data. The pointed-to
  // shared_ptr is accessed with atomic shared_ptr operations so invalidation
  // can publish a replacement while an in-flight callback keeps its old copy.
  // Properly deleted in Reset()/destructor (no leak).
  std::shared_ptr<ByokSnapshot> byok_snapshot_;
  raw_ptr<std::shared_ptr<ByokSnapshot>> byok_bridge_ = nullptr;

  std::string session_id_;
  RuntimeEventCallback current_on_event_;
  std::string pending_message_;

  std::string permission_extension_id_;
  std::set<std::string> session_grants_;
  // Test seam: when set, ShowPermissionPromptOnUIThread resolves synchronously
  // with this decision instead of showing the interactive dialog widget.
  base::RepeatingCallback<PromptDecision(const std::string&, const std::string&)>
      permission_prompt_override_for_testing_;
  void ShowPermissionPromptOnUIThread(
      const std::string& extension_id,
      const std::string& tool_name,
      base::OnceCallback<void(PromptDecision)> callback);
  // Synchronous permission decision (policy, auto-allowed page tools, extension
  // gate, persistent + session grants). Runs on the owning sequence because it
  // reads PrefService. Returns nullopt when a user dialog is required.
  std::optional<PromptDecision> EvaluatePermissionSync(
      const std::string& tool,
      const std::string& extension_id);
  void DecidePermissionOnUiThread(const std::string& tool,
                                  const std::string& extension_id,
                                  std::shared_ptr<PermissionCallState> state);
  // Test-only: drives DecidePermissionOnUiThread synchronously (paired with
  // permission_prompt_override_for_testing_) and returns the resolved decision.
  MahoAgentPermissionDecision RunPermissionDialogForTest(
      const std::string& tool,
      const std::string& extension_id);

  std::shared_ptr<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);

  uint64_t tool_availability_callback_token_ = 0;
  scoped_refptr<base::SequencedTaskRunner> owning_task_runner_;
  // Created on the owning sequence before registration. ClearActiveBYOKKeys
  // may safely copy it under GetActiveAdaptersLock() without calling
  // WeakPtrFactory from another sequence.
  base::WeakPtr<MahoUnifiedAgentAdapter> active_adapter_weak_ptr_;

  // R-9: dedicated FFI handle so OnToolAvailabilityFfi recovers the adapter
  // via a weak/refcounted bridge instead of dereferencing a raw `this` from a
  // foreign thread after teardown.
  std::unique_ptr<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>
      tool_availability_handle_;

  base::WeakPtrFactory<MahoUnifiedAgentAdapter> weak_factory_{this};

};

#endif  // MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
