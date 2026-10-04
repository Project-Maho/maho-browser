// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <set>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_conversation_task.h"
#include "maho/browser/ai/maho_ffi_callback_handle.h"
#include "maho/browser/ai/maho_tool_execution_graph.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "maho/browser/ui/webui/maho_ai_provider_oauth.h"
#include "maho/third_party/maho/maho_ffi.h"



struct MahoAcpHandle;
class Browser;
class PrefService;

namespace maho::ai {
class MahoAuthenticatedServiceApiBroker;
class MahoAgentChannelGateway;
}  // namespace maho::ai

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

// Cross-thread rendezvous for one permission decision. A synchronous Rust
// worker thread blocks on |event| inside OnAgentPermission while the owning
// sequence resolves the decision (auto-policy, native dialog, or the canonical
// WebUI approval broker). |cancelled| lets the worker abandon the wait (timeout
// / teardown) so a late resolution can never mutate a dead call.
struct PermissionCallState {
  PermissionCallState();
  ~PermissionCallState();
  PermissionCallState(const PermissionCallState&) = delete;
  PermissionCallState& operator=(const PermissionCallState&) = delete;
  PermissionCallState(PermissionCallState&&) = delete;
  PermissionCallState& operator=(PermissionCallState&&) = delete;

  base::WaitableEvent event{base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED};
  std::atomic<bool> cancelled{false};
  uint64_t generation = 0;
  PromptDecision decision{PromptDecision::kDeny};
  std::string approval_policy = "prompt";
  std::string sensitivity = "sensitive";
  bool page_derived_justification = false;
  std::string consequence = "unknown";
};

// One parked human-in-the-loop help request (browser_request_help). Tracks
// the active request while the adapter waits for the human to complete the
// step; |timed_out| distinguishes timer expiry from explicit cancel in the
// emitted status event.
struct HelpRequestState {
  std::string request_id;
  std::string prompt;
  base::TimeTicks started_at;
  bool timed_out = false;
};

struct BrowserToolCallState {
  BrowserToolCallState();
  ~BrowserToolCallState();
  BrowserToolCallState(const BrowserToolCallState&) = delete;
  BrowserToolCallState& operator=(const BrowserToolCallState&) = delete;
  BrowserToolCallState(BrowserToolCallState&&) = delete;
  BrowserToolCallState& operator=(BrowserToolCallState&&) = delete;

  base::WaitableEvent done{base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED};
  std::atomic<bool> cancelled{false};
  std::string result_json;
  std::string result_error;
  std::string result_error_code;
  bool result_error_retryable = false;
};

struct BrowserToolPreflightState {
  BrowserToolPreflightState();
  ~BrowserToolPreflightState();
  BrowserToolPreflightState(const BrowserToolPreflightState&) = delete;
  BrowserToolPreflightState& operator=(const BrowserToolPreflightState&) =
      delete;

  base::WaitableEvent done{base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED};
  std::atomic<bool> cancelled{false};
  std::string result_json;
  std::string executor_tool_name;
  std::string error;
  std::string error_code;
  bool error_retryable = false;
  bool requires_approval = false;
};

// ABI v2 Agent Event Kind
enum class MahoAgentEventKindV2 : uint32_t {
  kToken = 1,
  kThinking = 2,
  kToolCall = 3,
  kToolResult = 4,
  kArtifactCreated = 5,
  kCompleted = 6,
  kError = 7,
  kInteractionRequest = 8,
  kStatus = 9,
};

// Unified Agent Event Envelope across FFI boundaries
struct MahoUnifiedAgentEventEnvelope {
  uint32_t abi_version;
  const char* run_id;
  uint64_t event_seq;
  MahoAgentEventKindV2 kind;
  const char* payload_json;
};

using MahoUnifiedAgentEventCallback =
    void (*)(void* user_data, const MahoUnifiedAgentEventEnvelope* event);

// Injectable table of the maho-agent C FFI entry points the adapter calls.
// Production uses GetProductionMahoAgentFfi() (forwards to the real symbols);
// tests inject a fake table via set_agent_ffi_for_testing() to exercise the
// session lifecycle without linking against the real maho-core prebuilt.
struct MahoAgentFfi {
  MahoAgentSession* (*create_session_leased)(
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
      const char* space_id,
      MahoAgentReleaseCallback on_session_release,
      void* session_release_user_data);
   void (*set_approval_policy)(MahoAgentSession* session, const char* policy);
   void (*set_mail_authorization_state)(MahoAgentSession* session,
                                        bool feature_enabled,
                                        bool helper_ready,
                                        bool helper_starting,
                                        bool read_allowed);
   bool (*set_preferred_provider)(MahoAgentSession* session,
                                  const char* provider);
   void (*set_runtime_config)(MahoAgentSession* session,
                              const char* permission_tier,
                              bool final_confirm,
                              bool proactive_mode);
   bool (*send_message_leased)(MahoAgentSession* session,
                       const char* message,
                       void (*on_token)(void*, const char*),
                       void (*on_thinking)(void*, const char*),
                       void (*on_tool_call)(void*, const char*, const char*, const char*),
                       void (*on_tool_result)(void*, const char*, const char*, const char*, bool),
                       void (*on_complete)(void*, const char*, const char*),
                        void (*on_error)(void*, const char*),
                        void* callback_user_data,
                        MahoAgentReleaseCallback on_release,
                        void* release_user_data);
  bool (*cancel)(MahoAgentSession* session);
  void (*session_free)(MahoAgentSession* session);
  // Artifact ABI (todo 4): appended session-scoped setters (table/ABI-stable).
  void (*set_artifact_root)(MahoAgentSession* session, const char* path);
  void (*set_artifact_created_callback)(
      MahoAgentSession* session,
      void (*cb)(void*, const char*),
      void* user_data);
  // Unified Agent Event envelope callback & interaction / replay FFI (todo 8 & 9).
  bool (*register_unified_event_callback_leased)(
      MahoAgentSession* session,
      void (*cb)(void*, const MahoUnifiedAgentEventEnvelope*),
      void* user_data,
      MahoAgentReleaseCallback on_release,
      void* release_user_data);
  bool (*unregister_unified_event_callback)(MahoAgentSession* session);
  bool (*interaction_resolve)(MahoAgentSession* session,
                              const char* request_id,
                              const char* answer_json);
  bool (*interaction_timeout)(MahoAgentSession* session,
                              const char* request_id);
  bool (*interaction_cancel)(MahoAgentSession* session,
                             const char* request_id);
  bool (*get_events_after)(MahoAgentSession* session,
                           const char* run_id,
                           uint64_t after_seq,
                           char** out_json);
  // Turn control and wait/wake / model routing FFI (todo 23).
  bool (*turn_submit)(MahoAgentSession* session,
                      const char* run_ctx,
                      const char* message_json,
                      const char* intent);
  bool (*turn_queue_depth)(MahoAgentSession* session, uint32_t* out_u32);
  char* (*wait_register)(MahoAgentSession* session,
                         const char* run_id,
                         const char* filter_json,
                         uint64_t timeout_ms);
  bool (*wait_wake)(MahoAgentSession* session,
                    const char* run_id,
                    const char* event_json);
  bool (*resolve_model)(MahoAgentSession* session,
                        const char* category,
                        const char* preference_opt_json,
                        const char* available_opt_json,
                        char** out_json);
};

const MahoAgentFfi* GetProductionMahoAgentFfi();

class MahoUnifiedAgentAdapter : public MahoAiRuntimeAdapter {
 public:
  MahoUnifiedAgentAdapter(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>(),
      base::RepeatingCallback<Browser*()> bound_browser_resolver =
          base::RepeatingCallback<Browser*()>(),
      base::RepeatingCallback<base::expected<base::FilePath, std::string>()>
          artifact_root_resolver = {});
  ~MahoUnifiedAgentAdapter() override;

  MahoUnifiedAgentAdapter(const MahoUnifiedAgentAdapter&) = delete;
  MahoUnifiedAgentAdapter& operator=(const MahoUnifiedAgentAdapter&) = delete;

  static void ClearActiveBYOKKeys(const std::string& provider);
  static void NotifyMailReadConsentChanged(PrefService* prefs);
  static void NotifyAISettingsChanged(PrefService* prefs);
  static std::set<MahoUnifiedAgentAdapter*>& GetActiveAdapters();
  bool IsMailReadAllowedForTesting() const { return mail_read_allowed_; }

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
  void RespondToInteraction(const std::string& interaction_id,
                            const std::string& answer_json) override;
  // Session runtime_config (plan row-1 store). Set normalizes the tier
  // fail-closed via ParseRuntimeConfigTier, forwards to the live FFI session,
  // and caches the effective value; it returns false when no session is
  // active (the caller keeps its previous displayed state). Get serves the
  // cached session config (broker defaults before the first Set).
  bool GetRuntimeConfig(MahoAiRuntimeConfig* out_config) const override;
  bool SetRuntimeConfig(const MahoAiRuntimeConfig& config) override;
  bool ReplayAfter(const std::string& run_id,
                   uint64_t after_seq,
                   RuntimeEventCallback on_event) override;
  uint64_t GetLastConsumedEventSeq(const std::string& run_id) const override;
  bool IsTurnActive() const override;
  bool SubmitFollowUp(const std::string& message,
                      const std::string& intent = "queue") override;
  uint32_t GetTurnQueueDepth() const override;
  bool WakeForNotification(const std::string& run_id,
                           const std::string& event_json) override;
  std::string RegisterWait(const std::string& run_id,
                           const std::string& filter_json,
                           uint64_t timeout_ms) override;
  std::string GetRuntimeSessionId() const override;
  void StartSession(const std::string& session_id) override;
  void ResetSession(const std::string& session_id) override;
  void Reset() override;

  maho::ai::MahoAuthenticatedServiceApiBroker* api_broker() const {
    return api_broker_.get();
  }
  maho::ai::MahoAgentChannelGateway* channel_gateway() const {
    return channel_gateway_.get();
  }

  // Human-in-the-loop help requests (browser_request_help). The adapter parks
  // in kWaitingForHumanHelp until the human completes the step; a timeout or
  // cancel resolves the parked request fail-closed. Returns false when another
  // request is already active (one at a time) or the id is empty.
  static constexpr char kHelpRequestWaitingState[] = "waiting_for_human_help";
  bool RequestHumanHelp(const std::string& request_id,
                        const std::string& prompt,
                        base::TimeDelta timeout);
  bool CompleteHumanHelp(const std::string& request_id);
  bool CancelHumanHelp(const std::string& request_id, bool timed_out);
  // Timer callback: WeakPtr-bound callbacks must return void.
  void OnHelpRequestTimeout(const std::string& request_id);
  bool IsWaitingForHumanHelp() const {
    return active_help_request_.has_value();
  }

 private:
  friend class MahoUnifiedAgentAdapterTest;
  friend class MahoUnifiedAgentAdapterTestHelper;
  friend class MahoUnifiedAgentAdapterPolicyTest;
  friend class MahoUnifiedAgentAdapterLifecycleTest;

  void ResetSessionInternal(const std::string& session_id);

  void set_agent_ffi_for_testing(const MahoAgentFfi* ffi) { agent_ffi_ = ffi; }

  // Thread-safe, immutable BYOK data snapshot for OnAgentSecureStorage.
  // Once published, a snapshot is never mutated, so arbitrary Rust threads
  // may read it without locking. Key invalidation publishes a replacement.
  struct ByokSnapshot {
    ByokSnapshot();
    ByokSnapshot(const ByokSnapshot&);
    ByokSnapshot(ByokSnapshot&&);
    ByokSnapshot& operator=(const ByokSnapshot&);
    ByokSnapshot& operator=(ByokSnapshot&&);
    ~ByokSnapshot();

    std::string encrypted_byok_openai_b64;
    std::string encrypted_byok_anthropic_b64;
    scoped_refptr<os_crypt_async::Encryptor> encryptor;
    std::string custom_provider;
    std::string custom_api_key;
    std::string custom_base_url;
    std::string custom_model;
    std::map<std::string, std::string> mcp_credentials;
  };

  using CallbackHandle = maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>;

  struct TurnFfiResources {
    TurnFfiResources();
    TurnFfiResources(const TurnFfiResources&) = delete;
    TurnFfiResources(TurnFfiResources&&);
    TurnFfiResources& operator=(const TurnFfiResources&) = delete;
    TurnFfiResources& operator=(TurnFfiResources&&);
    ~TurnFfiResources();

    std::unique_ptr<CallbackHandle> callback_handle;
    // RAW_PTR_EXCLUSION: opaque handle into Rust-heap (non-PartitionAlloc)
    // memory owned by maho-core for the duration of the FFI turn.
    RAW_PTR_EXCLUSION void* callback_user_data = nullptr;
    uint64_t generation = 0;
    scoped_refptr<base::SequencedTaskRunner> owning_task_runner;
    base::WeakPtr<MahoUnifiedAgentAdapter> adapter;
  };

  struct ByokBridge {
    ByokBridge();
    ByokBridge(const ByokBridge&) = delete;
    ByokBridge(ByokBridge&&) = delete;
    ByokBridge& operator=(const ByokBridge&) = delete;
    ByokBridge& operator=(ByokBridge&&) = delete;
    ~ByokBridge();

    std::atomic<bool> active{true};
    base::Lock lock;
    std::shared_ptr<ByokSnapshot> snapshot;
    // The secure-storage callback can be re-entered for independent MCP
    // lookups. Outcomes are therefore keyed by lookup provider and never kept
    // in a process-wide or bridge-wide "last error" slot.
    std::map<std::string, MahoAiRuntimeErrorCode> lookup_outcomes;
    std::string preferred_provider;
  };

  struct SessionFfiResources {
    SessionFfiResources();
    SessionFfiResources(const SessionFfiResources&) = delete;
    SessionFfiResources(SessionFfiResources&&);
    SessionFfiResources& operator=(const SessionFfiResources&) = delete;
    SessionFfiResources& operator=(SessionFfiResources&&);
    ~SessionFfiResources();

    std::unique_ptr<CallbackHandle> callback_handle;
    // RAW_PTR_EXCLUSION: see TurnFfiResources::callback_user_data.
    RAW_PTR_EXCLUSION void* callback_user_data = nullptr;
    std::shared_ptr<ByokSnapshot> byok_snapshot;
    std::unique_ptr<ByokBridge> byok_bridge;
  };

  struct ActiveTurn {
    ActiveTurn();
    ActiveTurn(const ActiveTurn&) = delete;
    ActiveTurn(ActiveTurn&&);
    ActiveTurn& operator=(const ActiveTurn&) = delete;
    ActiveTurn& operator=(ActiveTurn&&);
    ~ActiveTurn();

    uint64_t generation = 0;
    std::unique_ptr<maho::ConversationTask> conversation_task;
    RuntimeEventCallback on_event;
    std::shared_ptr<TurnFfiResources> ffi_resources;
    bool cancelled = false;
  };

  static MahoAgentPermissionDecision OnAgentPermission(void* user_data,
                                                        const char* tool_name,
                                                        const char* arguments);
  static std::string ClassifyActionConsequenceForTesting(
      const std::string& tool_name,
      const std::string& arguments);
  static MahoAgentSecureKey OnAgentSecureStorage(void* user_data,
                                                  const char* provider);
  static void RecordSecureStorageOutcome(
      ByokBridge* bridge,
      const std::string& provider,
      std::optional<MahoAiRuntimeErrorCode> outcome);
  static MahoAgentToolResult OnAgentBrowserTool(void* user_data,
                                                 const char* tool_name,
                                                 const char* args_json);

  // Worker-thread wait applied to permission / browser-tool resolution.
  // Defaults to 30s; overridable in tests to exercise the timeout path.
  static base::TimeDelta AgentCallWaitTimeout();
  static void SetAgentCallWaitTimeoutForTesting(base::TimeDelta timeout);

  void ClearBYOKKeyOnOwningSequence(std::string provider);

  static void OnAgentToken(void* user_data, const char* token);
  static void OnAgentComplete(void* user_data, const char* full_text, const char* tool_calls_json);
  static void OnAgentError(void* user_data, const char* error);
  static void OnAgentThinking(void* user_data, const char* thinking);
  static void OnAgentToolCall(void* user_data,
                              const char* id,
                              const char* name,
                              const char* args);
  static void OnAgentToolResult(void* user_data,
                                const char* id,
                                const char* name,
                                const char* result,
                                bool success);
  static void OnAgentArtifactCreated(void* user_data, const char* artifact_json);
  static void OnAgentUnifiedEvent(void* user_data,
                                  const MahoUnifiedAgentEventEnvelope* event);
  static void ReleaseTurnFfiResources(void* release_user_data);
  static void ReleaseSessionFfiResources(void* release_user_data);

  static void OnToolAvailabilityFfi(void* user_data, const char* json_utf8);
  void OnToolAvailabilityOnSequence(std::string json_utf8);

  void OnTokenOnSequence(uint64_t generation, std::string token);
  void OnCompleteOnSequence(uint64_t generation, std::string full_text);
  void OnErrorOnSequence(uint64_t generation, std::string error);
  void OnThinkingOnSequence(uint64_t generation, std::string thinking);
  void OnToolCallOnSequence(uint64_t generation,
                            std::string id,
                            std::string name,
                            std::string args);
  void OnToolResultOnSequence(uint64_t generation,
                              std::string id,
                              std::string name,
                              std::string result,
                              bool success);
  void OnArtifactCreatedOnSequence(std::string artifact_json);
  void OnUnifiedEventOnSequence(uint64_t generation,
                                std::string run_id,
                                uint64_t event_seq,
                                MahoAgentEventKindV2 kind,
                                std::string payload_json);
  void CancelAllPendingInteractions();
  void OnTurnFfiResourcesReleased(uint64_t generation);
  void QuarantineSessionFfiResources();
  void TransitionToErrorForGeneration(uint64_t generation,
                                      const std::string& message);
  void TransitionToCredentialErrorForGeneration(
      uint64_t generation,
      MahoAiRuntimeErrorCode error_code);
  bool SyncAISettingsFromPrefs(uint64_t generation = 0);
  bool ConfigurePreferredProvider(uint64_t generation);
  std::optional<MahoAiRuntimeErrorCode>
  ResolveSecureStorageOutcome(MahoAiRuntimeErrorCode error_code) const;
  bool IsActiveGeneration(uint64_t generation) const;
  static base::OnceClosure CreatePermissionDecisionTask(
      base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
      uint64_t generation,
      std::string tool,
      std::shared_ptr<PermissionCallState> state,
      bool page_derived_justification,
      std::string consequence);
  static base::OnceClosure CreateBrowserToolTask(
      base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
      uint64_t generation,
      std::string tool_name,
      std::string args_json,
      std::shared_ptr<BrowserToolCallState> state,
      bool browser_action_preapproved);
  static base::OnceClosure CreateBrowserToolPreflightTask(
      base::WeakPtr<MahoUnifiedAgentAdapter> adapter,
      uint64_t generation,
      std::string capability_id,
      std::shared_ptr<BrowserToolPreflightState> state);
  base::OnceClosure CreatePermissionDecisionTaskForTesting(
      void* user_data,
      const std::string& tool,
      std::shared_ptr<PermissionCallState> state,
      bool page_derived_justification = false,
      std::string consequence = "unknown");
  base::OnceClosure CreateBrowserToolTaskForTesting(
      void* user_data,
      const std::string& tool_name,
      const std::string& args_json,
      std::shared_ptr<BrowserToolCallState> state,
      bool browser_action_preapproved = false);
  void DeliverQueuedTokenForTesting(uint64_t generation, std::string token) {
    OnTokenOnSequence(generation, std::move(token));
  }
  uint64_t ActiveGenerationForTesting() const {
    return active_turn_ ? active_turn_->generation : 0;
  }
  std::optional<MahoAiRuntimeErrorCode> SecureStorageOutcomeForTesting(
      const std::string& provider) const;
  void ClearSecureStorageEncryptorForTesting();
  void EmitEvent(MahoAiRuntimeEvent event);
  void EmitConnectionState(const std::string& state_str);
  void TransitionToError(const std::string& message);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<PrefService> prefs_;
  bool mail_read_allowed_ = false;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // R-9: fail-closed kAI gate and the token-bound browser resolver. A denying
  // gate drops async callbacks; the resolver replaces chrome::FindLastActive()
  // so browser tools/permission prompts never retarget the active window.
  base::RepeatingCallback<bool()> ai_gate_;
  base::RepeatingCallback<Browser*()> bound_browser_resolver_;
  base::RepeatingCallback<base::expected<base::FilePath, std::string>()>
      artifact_root_resolver_;
  bool IsAiAllowed();
  Browser* ResolveBoundBrowser();

  raw_ptr<struct MahoAgentSession> agent_session_ = nullptr;

  // Cached session runtime_config (plan row-1 store). Reset to the broker
  // defaults wherever |agent_session_| is freed, so a Get between sessions
  // never reports a stale freed session's flags.
  MahoAiRuntimeConfig runtime_config_;

  // maho-agent FFI entry points. Defaults to the production table in the
  // constructor; overridable in tests via set_agent_ffi_for_testing().
  raw_ptr<const MahoAgentFfi> agent_ffi_ = nullptr;

  std::shared_ptr<SessionFfiResources> session_ffi_resources_;
  std::unique_ptr<ActiveTurn> active_turn_;
  uint64_t next_generation_ = 0;
  uint64_t next_activity_id_ = 0;

  // Tool executor with deterministic lifetime (C2). nullptr until a
  // Browser* is available (adapter may be constructed before Browser).
  std::unique_ptr<maho::ToolExecutionGraph> tool_graph_;

  std::string session_id_;
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
      const std::string& extension_id,
      const std::string& consequence);
  void DecidePermissionOnUiThread(uint64_t generation,
                                  const std::string& tool,
                                  const std::string& extension_id,
                                  std::shared_ptr<PermissionCallState> state,
                                  bool page_derived_justification = false,
                                  std::string consequence = "unknown");
  // Test-only: drives DecidePermissionOnUiThread synchronously (paired with
  // permission_prompt_override_for_testing_) and returns the resolved decision.
  MahoAgentPermissionDecision RunPermissionDialogForTest(
      const std::string& tool,
      const std::string& extension_id);

  // Canonical WebUI approval broker. When a live runtime event consumer exists,
  // a decision that needs the user is published as a kApprovalRequest event and
  // its PermissionCallState parked here keyed by an unguessable approval id;
  // RespondToApproval resolves and erases it exactly once. The native Views
  // dialog is used only as a fallback when no event consumer is attached.
  std::unordered_map<std::string, std::shared_ptr<PermissionCallState>>
      pending_approvals_;
  // Active human-in-the-loop help request (browser_request_help). Empty while
  // no request is parked. The timer resolves it fail-closed on expiry.
  std::optional<HelpRequestState> active_help_request_;
  base::OneShotTimer help_request_timeout_timer_;
  static std::string GenerateApprovalId();
  // Resolve every parked approval as deny and signal its waiter exactly once.
  // Called on turn cancel, reset, and destruction so a blocked worker fails
  // closed instead of hanging.
  void DenyAllPendingApprovals();
  // Test-only accessor for the count of parked approvals.
  size_t PendingApprovalCountForTesting() const {
    return pending_approvals_.size();
  }
  // Test-only accessor for the count of pending interactions.
  size_t PendingInteractionCountForTesting() const {
    return pending_interactions_.size();
  }

  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void RenewExpiringProviderOAuth(PrefService* prefs);
  void OnProviderOAuthRenewed(const std::string& provider,
                              bool ok,
                              maho::ai_oauth::ProviderTokens tokens,
                              const std::string& error_message);

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

  // Session-scoped handle for the artifact-created FFI callback (installed once
  // per session via set_artifact_created_callback; outlives individual turns).
  std::unique_ptr<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>
      artifact_created_handle_;

  // Session-scoped handle for the unified event callback.
  std::unique_ptr<maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>>
      unified_event_handle_;

  // Tracks the highest monotonic event_seq consumed per run_id for live deduplication.
  std::unordered_map<std::string, uint64_t> last_consumed_seq_per_run_;

  // Canonical interaction broker: maps pending interaction_id to question/confirmation text.
  std::unordered_map<std::string, std::string> pending_interactions_;

  // Last event callback saved for session-level events delivered outside active turns.
  RuntimeEventCallback last_event_callback_;

  std::unique_ptr<maho::ai::MahoAuthenticatedServiceApiBroker> api_broker_;
  std::unique_ptr<maho::ai::MahoAgentChannelGateway> channel_gateway_;

  base::WeakPtrFactory<MahoUnifiedAgentAdapter> weak_factory_{this};

};

#endif  // MAHO_BROWSER_AI_MAHO_UNIFIED_AGENT_ADAPTER_H_
