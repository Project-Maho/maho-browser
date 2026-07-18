// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"

#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "maho/third_party/maho/maho_ffi.h"
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

  move_to_space_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  space_entries_.clear();

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

    const std::string active_space_id =
        maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser_);
    std::vector<SpaceEntry> visible_spaces;
    visible_spaces.reserve(space_entries_.size());
    for (const SpaceEntry& entry : space_entries_) {
      if (entry.id != active_space_id) {
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
    if (space_entries_.empty()) {
      move_to_space_submenu_->AddItem(0, u"No other spaces");
    }

    model->AddSubMenu(IDC_MAHO_FAV_MOVE_TO_SPACE, u"Move to Space",
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
      }
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
    case IDC_MAHO_FAV_MOVE_TO_SPACE:
      return !tab_id_.empty() && !space_entries_.empty();
    default:
      if (IsMoveToSpaceCommand(command_id)) {
        return !tab_id_.empty();
      }
      return false;
  }
}

bool MahoFavoritesContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
