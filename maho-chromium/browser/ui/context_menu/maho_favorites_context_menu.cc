// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"

#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"

MahoFavoritesContextMenu::MahoFavoritesContextMenu(
    Browser* browser,
    const GURL& url,
    const std::u16string& title,
    const std::string& tab_id,
    Delegate* delegate)
    : browser_(browser),
      url_(url),
      title_(title),
      tab_id_(tab_id),
      delegate_(delegate) {}

MahoFavoritesContextMenu::~MahoFavoritesContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel>
MahoFavoritesContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  content::WebContents* contents =
      maho::context_menu::GetWebContentsForTabId(browser_, tab_id_);

  model->AddItem(IDC_MAHO_FAV_OPEN_NEW_TAB, u"Open in New Tab");
  model->AddItem(IDC_MAHO_FAV_OPEN_SPLIT, u"Open in Split View");
  model->AddItem(IDC_MAHO_FAV_BOOST, u"Boost");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_COPY_LINK, u"Copy Link");
  model->AddItem(IDC_MAHO_FAV_SHARE, u"Share");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_CHANGE_ICON, u"Change Icon...");
  model->AddItem(IDC_MAHO_FAV_RENAME, u"Rename...");

  edit_pinned_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  edit_pinned_submenu_->AddItem(IDC_MAHO_FAV_EDIT_PINNED_REPLACE,
                                u"Replace Pinned URL with Current");
  edit_pinned_submenu_->AddItem(IDC_MAHO_FAV_EDIT_PINNED_EDIT, u"Edit...");
  model->AddSubMenu(IDC_MAHO_FAV_EDIT_PINNED, u"Edit Pinned Page",
                    edit_pinned_submenu_.get());

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FAV_RELOAD, u"Reload");
  model->AddItem(IDC_MAHO_FAV_DUPLICATE, u"Duplicate Tab");
  if (contents && contents->IsAudioMuted()) {
    model->AddItem(IDC_MAHO_FAV_UNMUTE, u"Unmute Tab");
  } else {
    model->AddItem(IDC_MAHO_FAV_MUTE, u"Mute Tab");
  }

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  move_to_space_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  space_entries_.clear();
  folder_entries_.clear();
  active_space_id_.clear();

  if (!tab_id_.empty()) {
    if (MahoCore* core = maho::GetCore()) {
      if (char* result = maho_core_get_space_view_models(core)) {
        std::string json(result);
        maho_string_free(result);

        std::optional<base::Value> parsed =
            base::JSONReader::Read(json, base::JSON_PARSE_RFC);
        if (parsed && parsed->is_list()) {
          for (const auto& space_value : parsed->GetList()) {
            const auto* space = space_value.GetIfDict();
            if (!space) {
              continue;
            }
            const std::string* id = space->FindString("id");
            const std::string* name = space->FindString("name");
            const std::string* icon = space->FindString("icon");
            if (!id || !name) {
              continue;
            }
            space_entries_.push_back(
                SpaceEntry{*id, *name, icon ? *icon : std::string()});
          }
        }
      }
    }

    active_space_id_ =
        maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser_);
    std::vector<SpaceEntry> visible_spaces;
    visible_spaces.reserve(space_entries_.size());
    for (const SpaceEntry& entry : space_entries_) {
      if (entry.id != active_space_id_) {
        visible_spaces.push_back(entry);
      }
    }
    space_entries_ = std::move(visible_spaces);

    for (size_t i = 0; i < space_entries_.size(); ++i) {
      const SpaceEntry& entry = space_entries_[i];
      const std::string label =
          (entry.icon.empty() ? std::string() : entry.icon + " ") + entry.name;
      move_to_space_submenu_->AddItem(
          IDC_MAHO_TAB_MOVE_SPACE_BASE + static_cast<int>(i),
          base::UTF8ToUTF16(label));
    }

    if (MahoCore* core = maho::GetCore()) {
      if (!active_space_id_.empty()) {
        std::string active_space_id_json = "\"" + active_space_id_ + "\"";
        if (char* result = maho_core_get_folder_view_models(
                core, active_space_id_json.c_str())) {
          std::string json(result);
          maho_string_free(result);

          std::optional<base::Value> parsed =
              base::JSONReader::Read(json, base::JSON_PARSE_RFC);
          if (parsed && parsed->is_list()) {
            for (const auto& folder_value : parsed->GetList()) {
              const auto* folder = folder_value.GetIfDict();
              if (!folder) {
                continue;
              }
              const std::string* id = folder->FindString("id");
              const std::string* name = folder->FindString("name");
              if (!id || !name) {
                continue;
              }
              folder_entries_.push_back(FolderEntry{*id, *name});
            }
          }
        }
      }
    }

    for (size_t i = 0; i < folder_entries_.size(); ++i) {
      const FolderEntry& entry = folder_entries_[i];
      move_to_space_submenu_->AddItem(
          IDC_MAHO_FAV_MOVE_FOLDER_BASE + static_cast<int>(i),
          base::UTF8ToUTF16(entry.name));
    }

    if (space_entries_.empty() && folder_entries_.empty()) {
      move_to_space_submenu_->AddItem(0, u"No other spaces");
    }

    model->AddSubMenu(IDC_MAHO_FAV_MOVE_TO_SPACE, u"Move to",
                      move_to_space_submenu_.get());

    model->AddSeparator(ui::NORMAL_SEPARATOR);
  }

  model->AddItem(IDC_MAHO_FAV_REMOVE, u"Remove from Favorites");

  return model;
}

void MahoFavoritesContextMenu::ExecuteCommand(int command_id,
                                              int event_flags) {
  switch (command_id) {
    case IDC_MAHO_FAV_OPEN_NEW_TAB:
      if (browser_) {
        browser_->OpenURL(
            content::OpenURLParams(url_, content::Referrer(),
                                   WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                   ui::PAGE_TRANSITION_TYPED, false),
            base::OnceCallback<void(content::NavigationHandle&)>());
      }
      break;
    case IDC_MAHO_FAV_COPY_LINK: {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(url_.spec()));
      NotifyLinkCopied();
      break;
    }
    case IDC_MAHO_FAV_OPEN_SPLIT:
      if (browser_) {
        MahoSplitViewController(browser_).AddSplitWithURL(url_);
      }
      break;
    case IDC_MAHO_FAV_BOOST:
      maho::context_menu::BoostTab(
          browser_,
          maho::context_menu::GetWebContentsForTabId(browser_, tab_id_));
      break;
    case IDC_MAHO_FAV_RELOAD:
      maho::context_menu::ReloadTab(
          maho::context_menu::GetWebContentsForTabId(browser_, tab_id_));
      break;
    case IDC_MAHO_FAV_DUPLICATE:
      maho::context_menu::DuplicateTab(
          browser_, maho::context_menu::ResolveTabIndex(browser_, tab_id_));
      break;
    case IDC_MAHO_FAV_MUTE:
      maho::context_menu::SetTabMuted(
          maho::context_menu::GetWebContentsForTabId(browser_, tab_id_), true);
      break;
    case IDC_MAHO_FAV_UNMUTE:
      maho::context_menu::SetTabMuted(
          maho::context_menu::GetWebContentsForTabId(browser_, tab_id_), false);
      break;
    case IDC_MAHO_FAV_SHARE:
      if (delegate_) {
        delegate_->OnFavoriteShareRequested();
      }
      break;
    case IDC_MAHO_FAV_CHANGE_ICON:
      if (delegate_) {
        delegate_->OnFavoriteIconChangeRequested();
      }
      break;
    case IDC_MAHO_FAV_RENAME:
      if (delegate_) {
        delegate_->OnFavoriteRenameRequested();
      }
      break;
    case IDC_MAHO_FAV_EDIT_PINNED_REPLACE: {
      content::WebContents* contents =
          maho::context_menu::GetWebContentsForTabId(browser_, tab_id_);
      if (contents) {
        maho::DispatchShellEvent(
            "set_tab_pinned_url",
            {{"tab_id", tab_id_}, {"url", contents->GetVisibleURL().spec()}});
      }
      break;
    }
    case IDC_MAHO_FAV_EDIT_PINNED_EDIT:
      if (delegate_) {
        delegate_->OnFavoritePinnedUrlEditRequested();
      }
      break;
    case IDC_MAHO_FAV_EDIT_PINNED:
      break;
    case IDC_MAHO_FAV_REMOVE:
      if (!tab_id_.empty()) {
        // ADR 11: Decoupled Favorites Close-Protection Policy.
        // Removing a favorite from the grid transitions its role back to normal
        // rather than closing the tab. The tab remains open in the normal list.
        base::DictValue event;
        event.Set("tab_id", tab_id_);
        base::DictValue new_role;
        new_role.Set("type", "normal");
        event.Set("new_role", std::move(new_role));
        maho::DispatchShellEventDict("change_tab_role", std::move(event));
      }
      break;
    case IDC_MAHO_FAV_MOVE_TO_SPACE:
      break;
    default:
      if (IsMoveToSpaceCommand(command_id) && !tab_id_.empty()) {
        int index = MoveToSpaceIndex(command_id);
        if (index >= 0 && index < static_cast<int>(space_entries_.size())) {
          // Core preserves the favorite role across the move (see
          // MoveTabToSpace in event_dispatcher.rs), so the tab stays a favorite
          // in the destination space's grid.
          maho::DispatchShellEvent(
              "move_tab_to_space",
              {{"target_space_id", space_entries_[index].id},
               {"tab_id", tab_id_},
               {"section", "normal"}});
        }
      } else if (IsMoveToFolderCommand(command_id) && !tab_id_.empty()) {
        int index = MoveToFolderIndex(command_id);
        if (index >= 0 && index < static_cast<int>(folder_entries_.size())) {
          base::DictValue event;
          event.Set("tab_id", tab_id_);
          base::DictValue new_role;
          new_role.Set("type", "normal");
          event.Set("new_role", std::move(new_role));
          maho::DispatchShellEventDict("change_tab_role", std::move(event));

          maho::DispatchShellEvent(
              "move_tab_to_folder",
              {{"space_id", active_space_id_},
               {"folder_id", folder_entries_[index].id},
               {"tab_id", tab_id_}});
        }
      }
      break;
  }
}

bool MahoFavoritesContextMenu::HasLiveTab() const {
  return maho::context_menu::GetWebContentsForTabId(browser_, tab_id_) !=
         nullptr;
}

void MahoFavoritesContextMenu::NotifyLinkCopied() {
  maho::ShowLinkCopiedToast(browser_);
}

bool MahoFavoritesContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDC_MAHO_FAV_OPEN_NEW_TAB:
    case IDC_MAHO_FAV_COPY_LINK:
    case IDC_MAHO_FAV_OPEN_SPLIT:
    case IDC_MAHO_FAV_SHARE:
      return url_.is_valid();
    case IDC_MAHO_FAV_BOOST:
    case IDC_MAHO_FAV_RELOAD:
    case IDC_MAHO_FAV_DUPLICATE:
    case IDC_MAHO_FAV_MUTE:
    case IDC_MAHO_FAV_UNMUTE:
      // Only actionable when the favorite is a live tab in the current strip.
      return HasLiveTab();
    case IDC_MAHO_FAV_CHANGE_ICON:
    case IDC_MAHO_FAV_RENAME:
    case IDC_MAHO_FAV_EDIT_PINNED:
    case IDC_MAHO_FAV_EDIT_PINNED_EDIT:
    case IDC_MAHO_FAV_REMOVE:
      return !tab_id_.empty();
    case IDC_MAHO_FAV_EDIT_PINNED_REPLACE:
      return HasLiveTab() && url_.is_valid();
    case IDC_MAHO_FAV_MOVE_TO_SPACE:
      return !tab_id_.empty() &&
             (!space_entries_.empty() || !folder_entries_.empty());
    default:
      if (IsMoveToSpaceCommand(command_id)) {
        return !tab_id_.empty();
      }
      if (IsMoveToFolderCommand(command_id)) {
        return !tab_id_.empty();
      }
      return false;
  }
}

bool MahoFavoritesContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
