// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"

MahoFavoritesContextMenu::MahoFavoritesContextMenu(
    Browser* browser,
    const GURL& url,
    const std::u16string& title,
    const std::string& tab_id)
    : browser_(browser), url_(url), title_(title), tab_id_(tab_id) {}

MahoFavoritesContextMenu::~MahoFavoritesContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel>
MahoFavoritesContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_FAV_OPEN_NEW_TAB, u"Open in New Tab");
  model->AddItem(IDC_MAHO_FAV_OPEN_SPLIT, u"Open in Split View");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_COPY_LINK, u"Copy Link");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_REMOVE, u"Remove from Favorites");

  return model;
}

void MahoFavoritesContextMenu::ExecuteCommand(int command_id,
                                                int event_flags) {
  switch (command_id) {
    case IDC_MAHO_FAV_OPEN_NEW_TAB:
      browser_->OpenURL(
          content::OpenURLParams(url_, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_TYPED, false),
          base::OnceCallback<void(content::NavigationHandle&)>());
      break;
    case IDC_MAHO_FAV_COPY_LINK: {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(url_.spec()));
      break;
    }
    case IDC_MAHO_FAV_OPEN_SPLIT:
      MahoSplitViewController(browser_).AddSplitWithURL(url_);
      break;
    case IDC_MAHO_FAV_REMOVE:
      if (!tab_id_.empty()) {
        maho::DispatchShellEvent("unfavorite_tab", {{"tab_id", tab_id_}});
      }
      break;
    default:
      break;
  }
}

bool MahoFavoritesContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDC_MAHO_FAV_OPEN_NEW_TAB:
    case IDC_MAHO_FAV_COPY_LINK:
    case IDC_MAHO_FAV_OPEN_SPLIT:
      return url_.is_valid();
    case IDC_MAHO_FAV_REMOVE:
      return !tab_id_.empty();
    default:
      return false;
  }
}

bool MahoFavoritesContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
