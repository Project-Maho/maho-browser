#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
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
  void SubmitPrompt(const std::string& session_id,
                    const std::string& prompt,
                    bool attach_browser_context,
                    maho_ai::mojom::InteractionMode mode) override;
  void CancelTurn(const std::string& session_id) override;
  void RespondToApproval(const std::string& session_id,
                         const std::string& approval_id,
                         bool approved) override;
  void ClosePanel() override;

 private:
  // Translates internal runtime events into Mojo and pushes them to the Page
  // remote. Also maintains chat history for the simple chat UI.
  void OnRuntimeEvent(MahoAiRuntimeEvent event);

  // Async continuation: called once page-context extraction completes. Builds
  // the augmented prompt (or falls back to the original) and forwards to the
  // active adapter.
  void OnContextExtractedThenSubmit(
      std::string user_prompt,
      maho_ai::mojom::InteractionMode mode,
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

  base::WeakPtrFactory<MahoAIPageHandler> weak_factory_{this};
  base::WeakPtrFactory<MahoAIPageHandler> session_list_weak_factory_{this};
  base::WeakPtrFactory<MahoAIPageHandler> event_log_weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
