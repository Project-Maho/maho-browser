// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_HANDLER_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;
class PrefService;

namespace content {
class WebContents;
}  // namespace content

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

class MahoInlineEditHandler
    : public maho_inline_edit::mojom::InlineEditPageHandler {
 public:
  MahoInlineEditHandler(
      std::unique_ptr<MahoPrivateContextToken> token,
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

 private:
  content::WebContents* GetActiveWebContents();
  void ExecuteJsInActiveTab(const std::u16string& script);

  static std::string HandleEventOnBackground(const std::string& event_json);
  void OnRustEventResponse(RequestEditCallback callback,
                           std::string rust_response_json);
  void StartLlmRequest(base::ListValue prompt_messages,
                       RequestEditCallback callback);
  void FinalizeStreaming(MahoAiLlmClient::CompletionResult result);
  void HandleLlmError(const std::string& error);

  SEQUENCE_CHECKER(sequence_checker_);

  bool IsAiAllowed();

  std::unique_ptr<MahoPrivateContextToken> token_;
  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  MahoAiLlmClient llm_client_;

  RequestEditCallback pending_callback_;

  base::WeakPtrFactory<MahoInlineEditHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_HANDLER_H_
