// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

class Browser;
class PrefRegistrySimple;
class PrefService;

namespace network {
class SimpleURLLoader;
class SharedURLLoaderFactory;
}  // namespace network

class MahoAIPageHandler : public maho_ai::mojom::PageHandler,
                          public network::SimpleURLLoaderStreamConsumer {
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
  void SendMessage(const std::string& message) override;
  void GetPageContent(GetPageContentCallback callback) override;
  void GetChatHistory(GetChatHistoryCallback callback) override;
  void clearHistory() override;
  void SetApiKey(const std::string& key) override;
  void GetApiKeyStatus(GetApiKeyStatusCallback callback) override;

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view string_piece,
                      base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

 private:
  static std::string HandleEventOnBackground(const std::string& event_json);

  void OnRustEventResponse(const std::string& user_message,
                           const std::string& rust_response_json);

  void OnLlmResponseDownloaded(std::optional<std::string> body);

  // SSE parsing: splits on `\n\n`, strips `data: ` prefix, extracts delta tokens.
  void ProcessSSEBuffer();

  void SendLlmResultToRust(const std::string& request_id,
                           const std::string& full_text);

  void FinalizeStreamingResponse();

  void ResetStreamingState();

  void OnPageContentExtracted(const std::string& title,
                              const std::string& url,
                              GetPageContentCallback callback,
                              base::Value result);

  SEQUENCE_CHECKER(sequence_checker_);

  mojo::Receiver<maho_ai::mojom::PageHandler> receiver_;
  mojo::Remote<maho_ai::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::vector<maho_ai::mojom::ChatMessagePtr> chat_history_;

  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  std::string sse_buffer_;
  std::string accumulated_response_;
  std::string current_request_id_;
  bool is_streaming_ = false;

  base::WeakPtrFactory<MahoAIPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PAGE_HANDLER_H_
