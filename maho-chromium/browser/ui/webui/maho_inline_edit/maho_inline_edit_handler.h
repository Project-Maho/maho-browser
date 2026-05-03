// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

class Browser;
class PrefService;

namespace network {
class SimpleURLLoader;
class SharedURLLoaderFactory;
}  // namespace network

class MahoInlineEditHandler
    : public maho_inline_edit::mojom::InlineEditPageHandler,
      public network::SimpleURLLoaderStreamConsumer {
 public:
  MahoInlineEditHandler(
      Browser* browser,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  MahoInlineEditHandler(const MahoInlineEditHandler&) = delete;
  MahoInlineEditHandler& operator=(const MahoInlineEditHandler&) = delete;

  ~MahoInlineEditHandler() override;

  void InjectScriptIntoActiveTab();

  // maho_inline_edit::mojom::InlineEditPageHandler:
  void GetSelectedText(GetSelectedTextCallback callback) override;
  void RequestEdit(const std::string& text,
                   const std::string& instruction,
                   RequestEditCallback callback) override;
  void ApplyEdit(const std::string& edited_text) override;

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view string_piece,
                      base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

 private:
  content::WebContents* GetActiveWebContents();
  void ExecuteJsInActiveTab(const std::u16string& script);

  static std::string HandleEventOnBackground(const std::string& event_json);
  void OnRustEventResponse(RequestEditCallback callback,
                           const std::string& rust_response_json);
  void StartLlmRequest(const std::string& api_key,
                       const base::ListValue& prompt_messages,
                       RequestEditCallback callback);
  void ProcessSSEBuffer();
  void FinalizeStreaming();

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  std::string sse_buffer_;
  std::string accumulated_response_;
  bool is_streaming_ = false;

  RequestEditCallback pending_callback_;

  base::WeakPtrFactory<MahoInlineEditHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_HANDLER_H_
