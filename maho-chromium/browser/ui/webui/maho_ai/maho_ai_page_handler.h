#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_runtime_event_persistence.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;
class PrefRegistrySimple;
class PrefService;
class MahoAiVoiceSession;
class MahoAIPageHandlerTestPeer;
class MahoAIEventLogPersistence;
class MahoRoutinesPageHandler;

namespace maho::ai {
class MahoAiPendingSurface;
}

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace content {
class WebContents;
}  // namespace content

class MahoAIPageHandler : public maho_ai::mojom::PageHandler,
                          public maho_routines::mojom::Page,
                          private maho::ai::MahoControlActivityService::Observer {
 public:
  MahoAIPageHandler(
      mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_ai::mojom::Page> page,
      std::unique_ptr<MahoPrivateContextToken> token,
      Browser* browser,
      content::WebContents* host_web_contents,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  MahoAIPageHandler(const MahoAIPageHandler&) = delete;
  MahoAIPageHandler& operator=(const MahoAIPageHandler&) = delete;

  ~MahoAIPageHandler() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);
  static bool CancelActiveControllerSession(std::string_view session_id);

  // maho_ai::mojom::PageHandler — session-oriented methods:
  void ConsumePendingSurface(uint64_t last_seen_generation,
                             ConsumePendingSurfaceCallback callback) override;
  void ListAllRoutines(ListAllRoutinesCallback callback) override;
  void CreateRoutine(const std::string& name,
                     const std::string& prompt,
                     const std::optional<std::string>& schedule,
                     const std::optional<std::string>& trigger,
                     CreateRoutineCallback callback) override;
  void StartRoutine(const std::string& id,
                    StartRoutineCallback callback) override;
  void GetRoutineRunStatuses(GetRoutineRunStatusesCallback callback) override;
  void GetRoutineUserTier(GetRoutineUserTierCallback callback) override;
  void RespondToRoutineApproval(
      const std::string& run_id,
      const std::string& approval_id,
      bool approved,
      RespondToRoutineApprovalCallback callback) override;
  void ListRunHistory(const std::optional<std::string>& routine_id,
                      uint32_t limit,
                      ListRunHistoryCallback callback) override;

  // maho_routines::mojom::Page:
  void OnRoutineComplete(
      maho_routines::mojom::RoutineRunResultPtr result) override;
  void OnRoutineError(const std::string& id,
                      const std::string& error) override;
  void OnRoutineRunStatusChanged(
      maho_routines::mojom::RoutineRunStatusPtr status) override;

  void GetConnectionState(GetConnectionStateCallback callback) override;
  void GetSessionList(GetSessionListCallback callback) override;
  void StartSession(const std::optional<std::string>& initial_prompt,
                    maho_ai::mojom::InteractionMode mode,
                    StartSessionCallback callback) override;
  void ResumeSession(const std::string& session_id,
                     ResumeSessionCallback callback) override;
  void GetSessionHistory(const std::string& session_id,
                         uint32_t offset,
                         uint32_t limit,
                         GetSessionHistoryCallback callback) override;
  void ListArtifacts(const std::string& session_id,
                     ListArtifactsCallback callback) override;
  void RenameArtifact(const std::string& artifact_id,
                      const std::string& display_name,
                      RenameArtifactCallback callback) override;
  void DeleteArtifact(const std::string& artifact_id,
                      DeleteArtifactCallback callback) override;
  void GetArtifactPreviewUrl(const std::string& artifact_id,
                             GetArtifactPreviewUrlCallback callback) override;
  void GetArtifactExportUrl(const std::string& artifact_id,
                            GetArtifactExportUrlCallback callback) override;
  void GetOpenTabs(GetOpenTabsCallback callback) override;
  void SearchHistory(const std::string& query,
                     uint32_t max_results,
                     SearchHistoryCallback callback) override;
  void SearchBookmarks(const std::string& query,
                       uint32_t max_results,
                       SearchBookmarksCallback callback) override;

  void SubmitPrompt(
      const std::string& session_id,
      const std::string& prompt,
      bool attach_browser_context,
      maho_ai::mojom::InteractionMode mode,
      std::optional<std::vector<maho_ai::mojom::ContextAttachmentPtr>>
          attachments,
      maho_ai::mojom::ChatIntent chat_intent,
      SubmitPromptCallback callback) override;
  void ComposerDraftGet(const std::string& scope_json,
                        ComposerDraftGetCallback callback) override;
  void ComposerDraftSet(const std::string& scope_json,
                        const std::string& text,
                        ComposerDraftSetCallback callback) override;
  void ComposerDraftDelete(const std::string& scope_json,
                           ComposerDraftDeleteCallback callback) override;
  void CancelTurn(const std::string& session_id) override;
  void RespondToApproval(const std::string& session_id,
                         const std::string& approval_id,
                         bool approved) override;
  void ClosePanel() override;
  void OpenSettings() override;
  void OpenSettingsPane(const std::string& pane_key) override;
  void GetAISettings(GetAISettingsCallback callback) override;
  void SetDefaultAISelection(const std::string& provider_id,
                      const std::string& model_id,
                      maho_ai::mojom::ReasoningEffort reasoning_effort,
                      SetDefaultAISelectionCallback callback) override;
  // Session runtime configuration (plan row-1 plumbing; the CapabilityBroker
  // remains the sole enforcement point). Thin passthrough to the active
  // runtime adapter; Get reports broker defaults (guard / true / false) when
  // no session is active, Set returns accepted=false in that case.
  void GetRuntimeConfig(GetRuntimeConfigCallback callback) override;
  void SetRuntimeConfig(maho_ai::mojom::RuntimeConfigInfoPtr config,
                        SetRuntimeConfigCallback callback) override;
  void GetViewMode(GetViewModeCallback callback) override;
  void SetViewMode(maho_ai::mojom::ViewMode mode) override;

  // Tab Tidy — default-ON, no pref gate.
  void RequestTabTidy(std::vector<maho_ai::mojom::TabInfoPtr> tabs,
                      RequestTabTidyCallback callback) override;
  void ApplyTabTidyFolders(
      std::vector<maho_ai::mojom::TidyFolderPtr> folders) override;

  // PAYG credit balance methods.
  void GetCreditBalance(GetCreditBalanceCallback callback) override;
  void GetBuyCreditsUrl(int32_t pack_size_usd,
                        GetBuyCreditsUrlCallback callback) override;

  // AI Extensibility:
  void CreateAiProfile(const std::string& name,
                       const std::string& system_prompt,
                       const std::optional<std::string>& model,
                       CreateAiProfileCallback callback) override;
  void GetAiProfiles(GetAiProfilesCallback callback) override;
  void UpdateAiProfile(const std::string& id,
                       const std::string& profile_json,
                       UpdateAiProfileCallback callback) override;
  void DeleteAiProfile(const std::string& id,
                       DeleteAiProfileCallback callback) override;
  void ImportProfileFromToml(const std::string& workspace_id,
                             const std::string& toml_path,
                             ImportProfileFromTomlCallback callback) override;

  void CreateAiWorkspace(const std::string& name,
                         const std::optional<std::string>& space_id,
                         CreateAiWorkspaceCallback callback) override;
  void GetActiveAiWorkspace(const std::string& space_id,
                            GetActiveAiWorkspaceCallback callback) override;
  void GetAiWorkspaces(GetAiWorkspacesCallback callback) override;
  void SwitchWorkspaceProfile(const std::string& workspace_id,
                              const std::string& profile_id,
                              SwitchWorkspaceProfileCallback callback) override;

  void RegisterMcpServer(const std::string& workspace_id,
                         const std::string& config_json,
                         RegisterMcpServerCallback callback) override;
  void RemoveMcpServer(const std::string& workspace_id,
                       const std::string& server_name,
                       RemoveMcpServerCallback callback) override;
  void GetMcpServers(const std::string& workspace_id,
                     GetMcpServersCallback callback) override;
  void ApproveMcpServerTrust(const std::string& workspace_id,
                             const std::string& server_name,
                             const std::vector<std::string>& tools,
                             ApproveMcpServerTrustCallback callback) override;

  void RegisterCliTool(const std::string& workspace_id,
                       const std::string& tool_json,
                       RegisterCliToolCallback callback) override;
  void RemoveCliTool(const std::string& workspace_id,
                     const std::string& tool_name,
                     RemoveCliToolCallback callback) override;
  void GetCliTools(const std::string& workspace_id,
                   GetCliToolsCallback callback) override;

  void GetWorkspaceEffectiveTools(
      const std::string& workspace_id,
      GetWorkspaceEffectiveToolsCallback callback) override;

  // Voice input (desktop on-device STT).
  void StartVoiceSession(StartVoiceSessionCallback callback) override;
  void PushAudioChunk(const std::vector<float>& pcm16k) override;
  void StopVoiceSession() override;

  // External control activity feed (Maho CLI / remote MCP client).
  void WatchControlActivity(WatchControlActivityCallback callback) override;

 private:
  friend class MahoAIPageHandlerTestPeer;

  struct Correlation {
    Correlation();
    Correlation(std::string session_id, std::optional<std::string> request_id);
    Correlation(const Correlation&);
    Correlation(Correlation&&);
    Correlation& operator=(const Correlation&);
    Correlation& operator=(Correlation&&);
    ~Correlation();

    std::string session_id;
    std::optional<std::string> request_id;
  };

  void SettleIngressRequest(const std::string& request_id,
                            bool post_terminal = false);
  void InterruptActiveIngressRequest(bool post_terminal = false);

  maho_ai::mojom::SessionInfoPtr StartSessionInternal(
      const std::optional<std::string>& initial_prompt,
      maho_ai::mojom::InteractionMode mode);

  void SubmitPromptInternal(
      const std::string& session_id,
      const std::string& prompt,
      bool attach_browser_context,
      maho_ai::mojom::InteractionMode mode,
      std::optional<std::vector<maho_ai::mojom::ContextAttachmentPtr>>
          attachments,
      maho_ai::mojom::ChatIntent chat_intent,
      Correlation correlation,
      base::OnceCallback<bool(bool)> dispatch_gate);

  // Translates internal runtime events into Mojo and pushes them to the Page
  // remote. Also maintains chat history for the simple chat UI.
  void OnRuntimeEvent(MahoAiRuntimeEvent event);
  void OnRuntimeEventForSession(const std::string& session_id,
                                std::optional<std::string> request_id,
                                MahoAiRuntimeEvent event);

  // Async continuation: called once page-context extraction completes for the
  // legacy |attach_browser_context| path. Builds the augmented prompt (or
  // falls back to the original) and forwards to the active adapter.
  void OnContextExtractedThenSubmit(
      Correlation correlation,
      base::OnceCallback<bool(bool)> dispatch_gate,
      std::string user_prompt,
      maho_ai::mojom::InteractionMode mode,
      maho_ai::mojom::ChatIntent chat_intent,
      MahoAiPageContextExtractor::PageContextResult ctx);

  // Phase B picker path: called once context extraction completes for a
  // specific open tab chosen via |GetOpenTabs|. |tab_label| is the human-
  // readable label for the injected context event.
  void OnTabContextExtractedThenSubmit(
      Correlation correlation,
      base::OnceCallback<bool(bool)> dispatch_gate,
      std::string user_prompt,
      maho_ai::mojom::InteractionMode mode,
      maho_ai::mojom::ChatIntent chat_intent,
      std::string tab_label,
      MahoAiPageContextExtractor::PageContextResult ctx);

  // Phase B multi-attachment aggregation: collects extraction results from
  // multiple attachments in submission order and emits a single
  // SubmitMessage once every slot has been filled.
  struct AttachmentSlot {
    AttachmentSlot();
    AttachmentSlot(AttachmentSlot&&) noexcept;
    AttachmentSlot& operator=(AttachmentSlot&&) noexcept;
    ~AttachmentSlot();
    std::string label;
    std::string header_tag;
    MahoAiPageContextExtractor::PageContextResult result;
    bool completed = false;
  };

  struct PendingAttachmentBatch {
    PendingAttachmentBatch();
    ~PendingAttachmentBatch();
    std::string user_prompt;
    maho_ai::mojom::InteractionMode mode;
    maho_ai::mojom::ChatIntent chat_intent;
    Correlation correlation;
    base::OnceCallback<bool(bool)> dispatch_gate;
    std::vector<AttachmentSlot> slots;
    size_t completed_count = 0;
  };

  void OnAttachmentSlotCompleted(
      std::shared_ptr<PendingAttachmentBatch> batch,
      size_t slot_index,
      MahoAiPageContextExtractor::PageContextResult ctx);

  // Builds a Mojo RuntimeEvent from an internal event struct.
  maho_ai::mojom::RuntimeEventPtr ToMojoEvent(const MahoAiRuntimeEvent& event);

  // Creates a SessionInfo for the current active session.
  maho_ai::mojom::SessionInfoPtr MakeActiveSessionInfo() const;

  // Emits OnConnectionStateChanged to the page if state actually changed.
  void MaybeEmitConnectionStateChanged();

  // Maps last_runtime_state_ + IsAvailable() to the truthful enum value.
  maho_ai::mojom::RuntimeConnectionState ResolveConnectionState() const;

  // Emits OnSessionUpdated to the page with current session metadata.
  void EmitSessionUpdated();

  // Updates the session status and emits appropriate events.
  void SetSessionStatus(maho_ai::mojom::SessionStatus status);

  void PersistSessionList(bool synchronous = false);
  void LoadPersistedSessions();

  // Re-adopts the newest persisted session as the live session so closing and
  // reopening the panel continues the previous conversation instead of
  // silently starting a new one. No-op when a session is already active, when
  // persistence is disabled, or when the newest session has no stored events.
  void AdoptMostRecentPersistedSessionAsActive();
  void PersistEventLog(const std::string& session_id, bool synchronous = false);
  void PersistEventLogWithReplyForTesting(
      const std::string& session_id,
      base::OnceCallback<void(bool)> completed);
  void PersistEventLogForSession(
      const std::string& session_id,
      const std::vector<maho_ai::mojom::RuntimeEventPtr>& log,
      bool synchronous = false);
  std::vector<maho_ai::mojom::RuntimeEventPtr> LoadPersistedEventLog(
      const std::string& session_id, bool* unavailable = nullptr);
  void OnSessionListSerialized(std::vector<std::string> kept_ids,
                               std::string json);
  void ClearPersistedEventLog(const std::string& session_id);
  void UpdateSessionTitle(const std::string& text);

  SEQUENCE_CHECKER(sequence_checker_);

  mojo::Receiver<maho_ai::mojom::PageHandler> receiver_;
  mojo::Remote<maho_ai::mojom::Page> page_;

  raw_ptr<maho::ai::MahoControlActivityService> control_activity_service_ =
      nullptr;
  bool control_activity_watched_ = false;
  std::string external_controller_session_;

  // maho::ai::MahoControlActivityService::Observer:
  void OnControlActivityChanged(
      const maho::ai::ControlActivity& activity) override;
  std::vector<maho_ai::mojom::ControlActivitySnapshotPtr>
  BuildControlActivityTimeline();
  std::unique_ptr<MahoPrivateContextToken> token_;
  // The WebUI host (side panel or tab) WebContents. The capability tokens for
  // the untrusted preview/export iframes must bind to THIS WebContents (the
  // token_ intentionally carries no web_contents for the R-9 classification).
  base::WeakPtr<content::WebContents> host_web_contents_;
  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<maho::ai::MahoAiPendingSurface> pending_surface_;
  mojo::Receiver<maho_routines::mojom::Page> routine_page_receiver_{this};
  mojo::Remote<maho_routines::mojom::PageHandler> routine_handler_remote_;
  std::unique_ptr<MahoRoutinesPageHandler> routine_handler_;
  std::unique_ptr<MahoAiRuntimeRouter> runtime_router_;
  raw_ptr<MahoAiRuntimeAdapter> runtime_adapter_for_testing_ = nullptr;
  std::unique_ptr<MahoAiPageContextExtractor> page_context_extractor_;

  // R-9: revalidates kAI for this handler's bound context. Every Mojo method
  // and every constructed subsystem is gated by this before touching
  // MahoCore/network. Denied contexts get interface-native defaults and a
  // reset receiver/page.
  bool IsAiAllowed();
  void DenyAndResetConnection();
  static bool RevalidateAiThunk(base::WeakPtr<MahoAIPageHandler> self);
  static Browser* ResolveBoundBrowserThunk(
      base::WeakPtr<MahoAIPageHandler> self);
  void PrepareForViewModeHandoff();
  void RestoreViewModeHandoff(
      maho::MahoAiIngressCoordinator::SessionHandoff handoff);

  // Session state for the new contract.
  std::string active_session_id_;
  std::string session_title_;
  std::string runtime_session_id_;
  std::string last_runtime_state_;
  maho_ai::mojom::SessionStatus session_status_ =
      maho_ai::mojom::SessionStatus::kCreated;
  maho_ai::mojom::RuntimeConnectionState last_emitted_connection_state_ =
      maho_ai::mojom::RuntimeConnectionState::kDisconnected;
  double session_created_at_ = 0;
  double session_updated_at_ = 0;
  uint64_t event_sequence_ = 0;
  uint32_t tool_call_count_ = 0;
  std::vector<maho_ai::mojom::RuntimeEventPtr> session_event_log_;
  maho::ai::RuntimeReplayBudget replay_budget_;
  std::vector<maho_ai::mojom::SessionInfoPtr> persisted_sessions_;
  base::WeakPtr<MahoAIEventLogPersistence> event_log_persistence_;

  // Test-only reply gate. The closure owns the real serialized result and
  // original weak commit callback; an unset gate leaves delivery unchanged.
  base::RepeatingCallback<void(base::OnceClosure)>
      event_log_reply_gate_for_testing_;

  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);

  PrefChangeRegistrar pref_change_registrar_;
  void OnAISettingsPrefsChanged();
  void OnRuntimeConfigPrefsChanged();
  maho_ai::mojom::AISettingsInfoPtr BuildAISettingsInfo() const;

  // Voice input: forwards final transcripts / errors to the Page remote.
  void OnVoiceFinalTranscript(const std::string& transcript);
  void OnVoiceSessionError(const std::string& message);
  std::unique_ptr<MahoAiVoiceSession> voice_session_;

  void OnAskMahoDispatch(
      const maho_ai::mojom::AskMahoDispatch& dispatch,
      uint64_t delivery_id,
      maho::MahoAiIngressCoordinator::AcceptanceCallback accept_callback);
  base::WeakPtr<maho::MahoAiIngressCoordinator> ask_maho_coordinator_;
  maho::MahoAiIngressCoordinator::ConsumerRegistration ask_maho_registration_;
  std::string active_ingress_request_id_;
  uint64_t active_ingress_delivery_id_ = 0;

  base::WeakPtrFactory<MahoAIPageHandler> weak_factory_{this};
  base::WeakPtrFactory<MahoAIPageHandler> session_list_weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
