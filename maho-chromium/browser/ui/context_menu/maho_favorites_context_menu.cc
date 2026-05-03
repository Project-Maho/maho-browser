// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"

MahoFavoritesContextMenu::MahoFavoritesContextMenu(
    Browser* browser,
    const GURL& url,
    const std::u16string& title)
    : browser_(browser), url_(url), title_(title) {}

MahoFavoritesContextMenu::~MahoFavoritesContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel>
MahoFavoritesContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_FAV_OPEN_NEW_TAB, u"Open in New Tab");
  model->AddItem(IDC_MAHO_FAV_OPEN_SPLIT, u"Open in Split View");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_COPY_LINK, u"Copy Link");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_RENAME, u"Rename");

  // TODO(maho): Populate with actual icon options from MahoCore.
  change_icon_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  model->AddSubMenu(IDC_MAHO_FAV_CHANGE_ICON, u"Change Icon",
                     change_icon_submenu_.get());

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  // TODO(maho): Populate with actual space names from MahoCore.
  move_to_space_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  model->AddSubMenu(IDC_MAHO_FAV_MOVE_TO_SPACE, u"Move to Space",
                     move_to_space_submenu_.get());
  model->AddItem(IDC_MAHO_FAV_DUPLICATE, u"Duplicate");

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
      // TODO(maho): Wire to maho-core FFI.
      LOG(INFO) << "MahoFavoritesContextMenu: Open in Split View "
                << url_.spec();
      break;
    case IDC_MAHO_FAV_RENAME:
      // TODO(maho): Wire to maho-core FFI.
      LOG(INFO) << "MahoFavoritesContextMenu: Rename";
      break;
    case IDC_MAHO_FAV_DUPLICATE:
      // TODO(maho): Wire to maho-core FFI.
      LOG(INFO) << "MahoFavoritesContextMenu: Duplicate";
      break;
    case IDC_MAHO_FAV_REMOVE:
      // TODO(maho): Wire to maho-core FFI.
      LOG(INFO) << "MahoFavoritesContextMenu: Remove from Favorites";
      break;
    default:
      break;
  }
}

bool MahoFavoritesContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDC_MAHO_FAV_OPEN_NEW_TAB:
    case IDC_MAHO_FAV_COPY_LINK:
      return url_.is_valid();
    case IDC_MAHO_FAV_OPEN_SPLIT:
    case IDC_MAHO_FAV_RENAME:
    case IDC_MAHO_FAV_CHANGE_ICON:
    case IDC_MAHO_FAV_MOVE_TO_SPACE:
    case IDC_MAHO_FAV_DUPLICATE:
    case IDC_MAHO_FAV_REMOVE:
      return false;
    default:
      return false;
  }
}

bool MahoFavoritesContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
