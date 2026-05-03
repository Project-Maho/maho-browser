// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/menus/simple_menu_model.h"

MahoTabContextMenu::MahoTabContextMenu(Browser* browser, int tab_index)
    : browser_(browser), tab_index_(tab_index) {}

MahoTabContextMenu::~MahoTabContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel> MahoTabContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_TAB_CLOSE, u"Close Tab");
  model->AddItem(IDC_MAHO_TAB_CLOSE_OTHERS, u"Close Other Tabs");
  model->AddItem(IDC_MAHO_TAB_CLOSE_RIGHT, u"Close Tabs to the Right");
  model->AddItem(IDC_MAHO_TAB_CLOSE_LEFT, u"Close Tabs to the Left");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_TAB_DUPLICATE, u"Duplicate Tab");

  if (browser_->tab_strip_model()->IsTabPinned(tab_index_)) {
    model->AddItem(IDC_MAHO_TAB_UNPIN, u"Unpin Tab");
  } else {
    model->AddItem(IDC_MAHO_TAB_PIN, u"Pin Tab");
  }

  content::WebContents* contents =
      browser_->tab_strip_model()->GetWebContentsAt(tab_index_);
  if (contents && contents->IsAudioMuted()) {
    model->AddItem(IDC_MAHO_TAB_UNMUTE, u"Unmute Tab");
  } else {
    model->AddItem(IDC_MAHO_TAB_MUTE, u"Mute Tab");
  }

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  move_to_space_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  space_entries_.clear();

  if (MahoCore* core = maho::GetCore()) {
    if (char* result = maho_core_get_space_view_models(core)) {
      std::string json(result);
      maho_string_free(result);

      std::optional<base::Value> parsed =
          base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_list()) {
        const auto& spaces = parsed->GetList();
        for (const auto& space_value : spaces) {
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
      maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId();

  std::vector<SpaceEntry> visible_spaces;
  visible_spaces.reserve(space_entries_.size());
  for (const SpaceEntry& entry : space_entries_) {
    if (entry.id == active_space_id) {
      continue;
    }
    visible_spaces.push_back(entry);
  }

  for (size_t i = 0; i < visible_spaces.size(); ++i) {
    const SpaceEntry& entry = visible_spaces[i];
    const std::string label_base =
        (entry.icon.empty() ? std::string() : entry.icon + " ") + entry.name;
    move_to_space_submenu_->AddItem(
        IDC_MAHO_TAB_MOVE_SPACE_BASE + static_cast<int>(i * 2),
        base::UTF8ToUTF16(label_base + " (Pinned)"));
    move_to_space_submenu_->AddItem(
        IDC_MAHO_TAB_MOVE_SPACE_BASE + static_cast<int>(i * 2) + 1,
        base::UTF8ToUTF16(label_base + " (Today)"));
  }

  space_entries_ = std::move(visible_spaces);

  if (space_entries_.empty()) {
    move_to_space_submenu_->AddItem(0, u"No other spaces");
  }

  model->AddSubMenu(IDC_MAHO_TAB_MOVE_TO_SPACE, u"Move to Space",
                     move_to_space_submenu_.get());

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_TAB_ADD_FAVORITE, u"Add to Favorites");
  model->AddItem(IDC_MAHO_TAB_COPY_URL, u"Copy URL");
  model->AddItem(IDC_MAHO_TAB_RELOAD, u"Reload");
  model->AddItem(IDC_MAHO_TAB_FREEZE, u"Freeze Tab");

  return model;
}

void MahoTabContextMenu::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case IDC_MAHO_TAB_CLOSE:
      browser_->tab_strip_model()->CloseWebContentsAt(
          tab_index_,
          TabCloseTypes::CLOSE_USER_GESTURE |
              TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      break;
    case IDC_MAHO_TAB_CLOSE_OTHERS: {
      TabStripModel* tab_strip = browser_->tab_strip_model();
      for (int i = tab_strip->count() - 1; i >= 0; --i) {
        if (i != tab_index_) {
          tab_strip->CloseWebContentsAt(
              i, TabCloseTypes::CLOSE_USER_GESTURE |
                     TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
          if (i < tab_index_) {
            --tab_index_;
          }
        }
      }
      break;
    }
    case IDC_MAHO_TAB_CLOSE_RIGHT: {
      TabStripModel* tab_strip = browser_->tab_strip_model();
      for (int i = tab_strip->count() - 1; i > tab_index_; --i) {
        tab_strip->CloseWebContentsAt(
            i, TabCloseTypes::CLOSE_USER_GESTURE |
                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      }
      break;
    }
    case IDC_MAHO_TAB_CLOSE_LEFT: {
      TabStripModel* tab_strip = browser_->tab_strip_model();
      for (int i = tab_index_ - 1; i >= 0; --i) {
        tab_strip->CloseWebContentsAt(
            i, TabCloseTypes::CLOSE_USER_GESTURE |
                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      }
      tab_index_ = 0;
      break;
    }
    case IDC_MAHO_TAB_DUPLICATE:
      browser_->tab_strip_model()->ActivateTabAt(tab_index_);
      chrome::DuplicateTab(browser_);
      break;
    case IDC_MAHO_TAB_PIN:
      browser_->tab_strip_model()->SetTabPinned(tab_index_, true);
      break;
    case IDC_MAHO_TAB_UNPIN:
      browser_->tab_strip_model()->SetTabPinned(tab_index_, false);
      break;
    case IDC_MAHO_TAB_MUTE: {
      content::WebContents* contents =
          browser_->tab_strip_model()->GetWebContentsAt(tab_index_);
      if (contents) {
        contents->SetAudioMuted(true);
      }
      break;
    }
    case IDC_MAHO_TAB_UNMUTE: {
      content::WebContents* contents =
          browser_->tab_strip_model()->GetWebContentsAt(tab_index_);
      if (contents) {
        contents->SetAudioMuted(false);
      }
      break;
    }
    case IDC_MAHO_TAB_COPY_URL: {
      content::WebContents* contents =
          browser_->tab_strip_model()->GetWebContentsAt(tab_index_);
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(
            base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
      break;
    }
    case IDC_MAHO_TAB_RELOAD: {
      content::WebContents* contents =
          browser_->tab_strip_model()->GetWebContentsAt(tab_index_);
      if (contents) {
        contents->GetController().Reload(content::ReloadType::NORMAL, true);
      }
      break;
    }
    case IDC_MAHO_TAB_ADD_FAVORITE:
      // TODO(maho): Wire to maho-core FFI.
      LOG(INFO) << "MahoTabContextMenu: Add to Favorites";
      break;
    case IDC_MAHO_TAB_REMOVE_FAVORITE:
      // TODO(maho): Wire to maho-core FFI.
      LOG(INFO) << "MahoTabContextMenu: Remove from Favorites";
      break;
    case IDC_MAHO_TAB_FREEZE:
      // TODO(maho): Wire to tab lifecycle/discard API.
      LOG(INFO) << "MahoTabContextMenu: Freeze Tab " << tab_index_;
      break;
    case IDC_MAHO_TAB_UNFREEZE:
      // TODO(maho): Wire to tab lifecycle/discard API.
      LOG(INFO) << "MahoTabContextMenu: Unfreeze Tab " << tab_index_;
      break;
    default:
      if (IsMoveToSpaceCommand(command_id)) {
        int index = MoveToSpaceIndex(command_id);
        if (index >= 0 && index < static_cast<int>(space_entries_.size())) {
          const auto& entry = space_entries_[index];
          std::string section = IsMoveToSpacePinned(command_id) ? "pinned"
                                                                 : "today";
          content::WebContents* contents =
              browser_->tab_strip_model()->GetWebContentsAt(tab_index_);
          if (contents) {
            std::string target_space_id = entry.id;
            int tab_index = tab_index_;
            base::ThreadPool::PostTask(
                FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
                base::BindOnce(
                    [](std::string space_id, std::string sec, int tab_idx) {
                      MahoCore* core = maho::GetCore();
                      if (!core) {
                        return;
                      }
                      base::DictValue event;
                      event.Set("kind", "MoveTabToSpace");
                      event.Set("tab_index", tab_idx);
                      event.Set("target_space_id", space_id);
                      event.Set("section", sec);
                      std::string json;
                      base::JSONWriter::Write(event, &json);
                      char* result = maho_core_handle_event(core, json.c_str());
                      if (result) {
                        maho_string_free(result);
                      }
                    },
                    target_space_id, section, tab_index));
          }
        }
      }
      break;
  }
}

bool MahoTabContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDC_MAHO_TAB_CLOSE:
    case IDC_MAHO_TAB_DUPLICATE:
    case IDC_MAHO_TAB_PIN:
    case IDC_MAHO_TAB_UNPIN:
    case IDC_MAHO_TAB_MUTE:
    case IDC_MAHO_TAB_UNMUTE:
    case IDC_MAHO_TAB_COPY_URL:
    case IDC_MAHO_TAB_RELOAD:
      return true;
    case IDC_MAHO_TAB_CLOSE_OTHERS:
      return browser_->tab_strip_model()->count() > 1;
    case IDC_MAHO_TAB_CLOSE_RIGHT:
      return tab_index_ < browser_->tab_strip_model()->count() - 1;
    case IDC_MAHO_TAB_CLOSE_LEFT:
      return tab_index_ > 0;
    case IDC_MAHO_TAB_ADD_FAVORITE:
      return true;
    case IDC_MAHO_TAB_REMOVE_FAVORITE:
    case IDC_MAHO_TAB_FREEZE:
    case IDC_MAHO_TAB_UNFREEZE:
      return false;
    default:
      if (IsMoveToSpaceCommand(command_id)) {
        return true;
      }
      return false;
  }
}

bool MahoTabContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
