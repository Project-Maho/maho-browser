// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
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

  // maho_ai::mojom::PageHandler:
  void SendMessage(maho_ai::mojom::ChatIntent chat_intent,
                   const std::string& message) override;
  void GetPageContent(GetPageContentCallback callback) override;
  void GetChatHistory(GetChatHistoryCallback callback) override;
  void clearHistory() override;
  void SetApiKey(const std::string& key) override;
  void GetApiKeyStatus(GetApiKeyStatusCallback callback) override;

  struct PageContextResult {
    enum class Status {
      kSuccess,
      kNoActiveTab,
      kCannotAccess,
      kExtractionFailed,
    };

    PageContextResult();
    ~PageContextResult();
    PageContextResult(PageContextResult&&);
    PageContextResult& operator=(PageContextResult&&);

    Status status = Status::kExtractionFailed;
    std::string title;
    std::string url;
    std::string text;
  };

 private:
  static std::string HandleEventOnBackground(const std::string& event_json);

  void OnRustEventResponse(const std::string& user_message,
                           const std::string& rust_response_json);
  void SendLlmResultToRust(const std::string& request_id,
                           const std::string& full_text);
  void OnLlmToken(const std::string& token);
  void FinalizeStreamingResponse(const std::string& full_text);
  void HandleLlmError(const std::string& error);
  void ResetStreamingState();

  using PageContextCallback =
      base::OnceCallback<void(PageContextResult page_context)>;

  void GetPageContext(PageContextCallback callback);
  void OnPageContextExtracted(const std::string& title,
                              const std::string& url,
                              PageContextCallback callback,
                              base::Value result);

  SEQUENCE_CHECKER(sequence_checker_);

  mojo::Receiver<maho_ai::mojom::PageHandler> receiver_;
  mojo::Remote<maho_ai::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  MahoAiLlmClient llm_client_;
  std::vector<maho_ai::mojom::ChatMessagePtr> chat_history_;

  std::string current_request_id_;
  bool received_llm_content_ = false;

  base::WeakPtrFactory<MahoAIPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
