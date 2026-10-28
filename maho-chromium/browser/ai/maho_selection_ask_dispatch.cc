// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_selection_ask_dispatch.h"

#include <optional>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/ai/maho_ai_file_attachment_registry.h"
#include "maho/browser/ai/maho_selection_ask_query.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

namespace maho {

namespace {

void DispatchAndShowPanel(Browser* browser,
                          maho_ai::mojom::AskMahoDispatchPtr dispatch) {
  MahoAiIngressCoordinator::GetOrCreateForBrowser(browser)->Dispatch(
      std::move(dispatch));
  auto* side_panel_ui = browser->GetFeatures().side_panel_ui();
  if (side_panel_ui && side_panel_ui->GetCurrentEntryId() !=
                           SidePanelEntryId::kMahoAiPanel) {
    side_panel_ui->Show(SidePanelEntryKey(SidePanelEntryId::kMahoAiPanel),
                        SidePanelOpenTrigger::kToolbarButton);
  }
}

}  // namespace

void AskMahoAboutSelection(BrowserWindowInterface* browser_interface,
                           content::WebContents* web_contents,
                           const GURL& page_url,
                           const std::u16string& selection) {
  // Same cast as maho_sync_relay_client.cc: every BrowserWindowInterface in
  // the desktop build is a Browser.
  Browser* browser = static_cast<Browser*>(browser_interface);
  if (!browser || !web_contents || !web_contents->GetBrowserContext() ||
      web_contents->GetBrowserContext()->IsOffTheRecord() ||
      !page_url.is_valid() || selection.empty()) {
    return;
  }
  auto attachment = maho_ai::mojom::ContextAttachment::New();
  attachment->kind = maho_ai::mojom::ContextSourceKind::kCurrentPage;
  attachment->tab_id = -1;
  attachment->label = "Current page";
  attachment->url = page_url.spec();
  auto dispatch = maho_ai::mojom::AskMahoDispatch::New();
  dispatch->request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  dispatch->query = BuildSelectionAskQuery(
      page_url.spec(), base::UTF16ToUTF8(selection));
  dispatch->source = maho_ai::mojom::AskMahoSource::kCommandPalette;
  dispatch->submit = true;
  dispatch->mode = maho_ai::mojom::InteractionMode::kAssistant;
  dispatch->context_intent = maho_ai::mojom::AskMahoContextIntent::kNone;
  dispatch->target_session_id = std::nullopt;
  dispatch->attachments.push_back(std::move(attachment));
  DispatchAndShowPanel(browser, std::move(dispatch));
}

bool AskMahoAboutCapture(BrowserWindowInterface* browser_interface,
                         content::WebContents* web_contents,
                         const GURL& page_url,
                         std::vector<uint8_t> png_bytes) {
  Browser* browser = static_cast<Browser*>(browser_interface);
  if (!browser || !web_contents || !web_contents->GetBrowserContext() ||
      web_contents->GetBrowserContext()->IsOffTheRecord()) {
    return false;
  }
  auto* registry = ai::GetMahoAiFileAttachmentRegistry(browser->GetProfile());
  const std::string token =
      registry ? registry->IssueCapturedImageToken(std::move(png_bytes))
               : std::string();
  if (token.empty()) {
    return false;
  }
  auto attachment = maho_ai::mojom::ContextAttachment::New();
  attachment->kind = maho_ai::mojom::ContextSourceKind::kCapturedImage;
  attachment->tab_id = -1;
  attachment->label = "Page capture";
  attachment->url = page_url.is_valid() ? page_url.spec() : std::string();
  attachment->file_token = token;
  auto dispatch = maho_ai::mojom::AskMahoDispatch::New();
  dispatch->request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  dispatch->query = "What is shown in this capture?";
  dispatch->source = maho_ai::mojom::AskMahoSource::kCommandPalette;
  dispatch->submit = true;
  dispatch->mode = maho_ai::mojom::InteractionMode::kAssistant;
  dispatch->context_intent = maho_ai::mojom::AskMahoContextIntent::kNone;
  dispatch->target_session_id = std::nullopt;
  dispatch->attachments.push_back(std::move(attachment));
  DispatchAndShowPanel(browser, std::move(dispatch));
  return true;
}

}
