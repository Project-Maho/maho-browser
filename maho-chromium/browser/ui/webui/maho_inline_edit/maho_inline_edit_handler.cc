// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/third_party/maho/maho_bridge.h"

namespace {

// Embedded JS injection script — loaded at compile time.
#include "maho/browser/ui/webui/maho_inline_edit/inline_edit_script_string.h"

}  // namespace

MahoInlineEditHandler::MahoInlineEditHandler(
    std::unique_ptr<MahoPrivateContextToken> token,
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : token_(std::move(token)),
      browser_(browser),
      prefs_(prefs),
      llm_client_(prefs, std::move(url_loader_factory), nullptr, false) {
  DCHECK(prefs_);
}

MahoInlineEditHandler::~MahoInlineEditHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool MahoInlineEditHandler::IsAiAllowed() {
  return token_ && token_->Revalidate(MahoPrivateCapability::kAI);
}

content::WebContents* MahoInlineEditHandler::GetActiveWebContents() {
  if (!browser_) {
    return nullptr;
  }
  return browser_->GetTabStripModel()->GetActiveWebContents();
}

void MahoInlineEditHandler::ExecuteJsInActiveTab(
    const std::u16string& script) {
  content::WebContents* wc = GetActiveWebContents();
  if (!wc) {
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      script, base::NullCallback(),
      content::ISOLATED_WORLD_ID_CONTENT_END + 2);
}

void MahoInlineEditHandler::InjectScriptIntoActiveTab() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ExecuteJsInActiveTab(base::UTF8ToUTF16(std::string(kInlineEditScript)));
}

void MahoInlineEditHandler::GetSelectedText(GetSelectedTextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    std::move(callback).Run("");
    return;
  }

  content::WebContents* wc = GetActiveWebContents();
  if (!wc) {
    std::move(callback).Run("");
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    std::move(callback).Run("");
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"window.__mahoGetSelectedText ? window.__mahoGetSelectedText() : ''",
      base::BindOnce(
          [](GetSelectedTextCallback cb, base::Value result) {
            std::string text;
            if (result.is_string()) {
              text = result.GetString();
            }
            std::move(cb).Run(text);
          },
          std::move(callback)),
      content::ISOLATED_WORLD_ID_CONTENT_END + 2);
}

void MahoInlineEditHandler::RequestEdit(const std::string& text,
                                        const std::string& instruction,
                                        RequestEditCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    std::move(callback).Run("");
    return;
  }

  auto route_res = maho::ai::ResolveMahoAiModelRoute(
      prefs_, maho::ai::MahoAiTask::kInlineEdit);
  if (!route_res.is_ok()) {
    std::move(callback).Run("No AI model configured for Inline Edit.");
    return;
  }

  base::DictValue event = base::DictValue().Set(
      "ChatMessage",
      base::DictValue()
          .Set("message", instruction)
          .Set("include_page_context", false));

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);

  maho::PostCoreTask<std::string>(
      FROM_HERE,
      base::BindOnce(&MahoInlineEditHandler::HandleEventOnBackground,
                     event_json),
      base::BindOnce(&MahoInlineEditHandler::OnRustEventResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

// static
std::string MahoInlineEditHandler::HandleEventOnBackground(
    const std::string& event_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return "{}";
  }
  std::string updates = maho::core::HandleEvent(core, event_json.c_str());
  maho::InvalidateSidebarCoreCacheForUpdatesJson(updates);
  return updates;
}

void MahoInlineEditHandler::OnRustEventResponse(
    RequestEditCallback callback,
    std::string rust_response_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    std::move(callback).Run("");
    return;
  }

  auto parsed =
      base::JSONReader::Read(rust_response_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(callback).Run("Failed to process edit request.");
    return;
  }

  const base::DictValue* llm_request = nullptr;

  if (parsed->is_dict()) {
    llm_request = parsed->GetDict().FindDict("RequestLlmCompletion");
    if (!llm_request) {
      llm_request = parsed->GetDict().FindDict("request_llm_completion");
    }
    if (!llm_request) {
      const std::string* kind = parsed->GetDict().FindString("kind");
      if (kind && *kind == "request_llm_completion") {
        llm_request = &parsed->GetDict();
      }
    }
  } else if (parsed->is_list()) {
    for (const auto& item : parsed->GetList()) {
      const base::DictValue* d = item.GetIfDict();
      if (!d) continue;
      const std::string* kind = d->FindString("kind");
      if (kind && *kind == "request_llm_completion") {
        llm_request = d;
        break;
      }
    }
  }

  if (!llm_request) {
    std::move(callback).Run("Unexpected response from AI engine.");
    return;
  }

  auto prompt_messages = maho::ai::ExtractPromptMessages(*llm_request);
  if (!prompt_messages) {
    std::move(callback).Run("Invalid prompt from AI engine.");
    return;
  }

  StartLlmRequest(std::move(*prompt_messages), std::move(callback));
}

void MahoInlineEditHandler::StartLlmRequest(
    base::ListValue prompt_messages,
    RequestEditCallback callback) {
  if (!IsAiAllowed()) {
    std::move(callback).Run("");
    return;
  }
  pending_callback_ = std::move(callback);

  auto route_res = maho::ai::ResolveMahoAiModelRoute(
      prefs_, maho::ai::MahoAiTask::kInlineEdit);
  MahoAiLlmClient::CompletionRequest request;
  if (route_res.is_ok()) {
    request.provider_id = route_res.route.provider_id;
    request.endpoint = route_res.route.endpoint;
    request.model = route_res.route.model_id;
  }
  request.prompt_messages = std::move(prompt_messages);
  llm_client_.StartCompletion(
      std::move(request), MahoAiLlmClient::TokenCallback(),
      base::BindOnce(&MahoInlineEditHandler::FinalizeStreaming,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&MahoInlineEditHandler::HandleLlmError,
                     weak_factory_.GetWeakPtr()));
}

void MahoInlineEditHandler::FinalizeStreaming(
    MahoAiLlmClient::CompletionResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    pending_callback_.Reset();
    return;
  }

  if (pending_callback_) {
    std::move(pending_callback_).Run(result.full_text);
  }

  std::string escaped = result.full_text;
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\\", "\\\\");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "'", "\\'");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\n", "\\n");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\r", "\\r");

  ExecuteJsInActiveTab(base::UTF8ToUTF16(
      "window.__mahoInlineEditResult && window.__mahoInlineEditResult('" +
      escaped + "')"));
}

void MahoInlineEditHandler::HandleLlmError(const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (pending_callback_) {
    std::move(pending_callback_).Run(error);
  }
}

void MahoInlineEditHandler::ApplyEdit(const std::string& edited_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsAiAllowed()) {
    return;
  }

  std::string escaped = edited_text;
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\\", "\\\\");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "'", "\\'");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\n", "\\n");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\r", "\\r");

  ExecuteJsInActiveTab(base::UTF8ToUTF16(
      "window.__mahoReplaceSelection && window.__mahoReplaceSelection('" +
      escaped + "')"));
}
