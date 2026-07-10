#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;
class PrefRegistrySimple;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

class MahoAIPageHandler : public maho_ai::mojom::PageHandler {
 public:
  MahoAIPageHandler(
      mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_ai::mojom::Page> page,
      Browser* browser,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  MahoAIPageHandler(const MahoAIPageHandler&) = delete;
  MahoAIPageHandler& operator=(const MahoAIPageHandler&) = delete;

  ~MahoAIPageHandler() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);



  // maho_ai::mojom::PageHandler — session-oriented methods:
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
  void GetOpenTabs(GetOpenTabsCallback callback) override;
  void SearchHistory(const std::string& query,
                     uint32_t max_results,
                     SearchHistoryCallback callback) override;
  void SearchBookmarks(const std::string& query,
                       uint32_t max_results,
                       SearchBookmarksCallback callback) override;

  void SubmitPrompt(const std::string& session_id,
                    const std::string& prompt,
                    bool attach_browser_context,
                    maho_ai::mojom::InteractionMode mode,
                    std::optional<std::vector<maho_ai::mojom::ContextAttachmentPtr>>
                        attachments) override;
  void CancelTurn(const std::string& session_id) override;
  void RespondToApproval(const std::string& session_id,
                         const std::string& approval_id,
                         bool approved) override;
  void ClosePanel() override;
  void OpenSettings() override;
  void GetAISettings(GetAISettingsCallback callback) override;
  void GetViewMode(GetViewModeCallback callback) override;
  void SetViewMode(maho_ai::mojom::ViewMode mode) override;

  // Tab Tidy — default-ON, no pref gate.
  void RequestTabTidy(
      std::vector<maho_ai::mojom::TabInfoPtr> tabs,
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

  void GetWorkspaceEffectiveTools(const std::string& workspace_id,
                                  GetWorkspaceEffectiveToolsCallback callback) override;

 private:
  // Translates internal runtime events into Mojo and pushes them to the Page
  // remote. Also maintains chat history for the simple chat UI.
  void OnRuntimeEvent(MahoAiRuntimeEvent event);

  // Async continuation: called once page-context extraction completes for the
  // legacy |attach_browser_context| path. Builds the augmented prompt (or
  // falls back to the original) and forwards to the active adapter.
  void OnContextExtractedThenSubmit(
      std::string user_prompt,
      maho_ai::mojom::InteractionMode mode,
      MahoAiPageContextExtractor::PageContextResult ctx);

  // Phase B picker path: called once context extraction completes for a
  // specific open tab chosen via |GetOpenTabs|. |tab_label| is the human-
  // readable label for the injected context event.
  void OnTabContextExtractedThenSubmit(
      std::string user_prompt,
      maho_ai::mojom::InteractionMode mode,
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
    std::vector<AttachmentSlot> slots;
    size_t completed_count = 0;
  };

  void OnAttachmentSlotCompleted(
      std::shared_ptr<PendingAttachmentBatch> batch,
      size_t slot_index,
      MahoAiPageContextExtractor::PageContextResult ctx);

  // Builds a Mojo RuntimeEvent from an internal event struct.
  maho_ai::mojom::RuntimeEventPtr ToMojoEvent(
      const MahoAiRuntimeEvent& event);

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
  void PersistEventLog(const std::string& session_id, bool synchronous = false);
  std::vector<maho_ai::mojom::RuntimeEventPtr> LoadPersistedEventLog(
      const std::string& session_id);
  void OnEventLogSerialized(const std::string& session_id, std::string json);
  void OnSessionListSerialized(std::vector<std::string> kept_ids, std::string json);
  void ClearPersistedEventLog(const std::string& session_id);
  void UpdateSessionTitle(const std::string& text);

  SEQUENCE_CHECKER(sequence_checker_);

  mojo::Receiver<maho_ai::mojom::PageHandler> receiver_;
  mojo::Remote<maho_ai::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  MahoAiRuntimeRouter runtime_router_;
  MahoAiPageContextExtractor page_context_extractor_;


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
  std::vector<maho_ai::mojom::SessionInfoPtr> persisted_sessions_;

  std::optional<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);

  PrefChangeRegistrar pref_change_registrar_;
  void OnAISettingsPrefsChanged();
  maho_ai::mojom::AISettingsInfoPtr BuildAISettingsInfo() const;

  base::WeakPtrFactory<MahoAIPageHandler> weak_factory_{this};
  base::WeakPtrFactory<MahoAIPageHandler> session_list_weak_factory_{this};
  base::WeakPtrFactory<MahoAIPageHandler> event_log_weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
