// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/menus/simple_menu_model.h"

MahoTabContextMenu::MahoTabContextMenu(Browser* browser, int tab_index)
    : browser_(browser), tab_index_(tab_index), delegate_(nullptr) {}

MahoTabContextMenu::MahoTabContextMenu(Browser* browser,
                                       int tab_index,
                                       Delegate* delegate)
    : browser_(browser), tab_index_(tab_index), delegate_(delegate) {}

MahoTabContextMenu::MahoTabContextMenu(Browser* browser,
                                       int tab_index,
                                       Delegate* delegate,
                                       std::string core_tab_id)
    : browser_(browser),
      tab_index_(tab_index),
      delegate_(delegate),
      core_tab_id_(std::move(core_tab_id)) {}

MahoTabContextMenu::~MahoTabContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel> MahoTabContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);
  const TabStripModel* tab_strip_model =
      browser_ ? browser_->tab_strip_model() : nullptr;
  const bool has_valid_tab_index =
      tab_strip_model && tab_index_ >= 0 && tab_index_ < tab_strip_model->count();

  content::WebContents* contents =
      has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_) : nullptr;

  model->AddItem(IDC_MAHO_TAB_CLOSE, u"Close Tab");
  model->AddItem(IDC_MAHO_TAB_CLOSE_OTHERS, u"Close Other Tabs");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_TAB_DUPLICATE, u"Duplicate Tab");
  model->AddItem(IDC_MAHO_TAB_OPEN_SPLIT, u"Open in Split View");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_TAB_COPY_URL, u"Copy Link");
  if (contents && contents->IsAudioMuted()) {
    model->AddItem(IDC_MAHO_TAB_UNMUTE, u"Unmute Tab");
  } else {
    model->AddItem(IDC_MAHO_TAB_MUTE, u"Mute Tab");
  }

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  if (has_valid_tab_index && tab_strip_model->IsTabPinned(tab_index_)) {
    model->AddItem(IDC_MAHO_TAB_UNPIN, u"Unpin Tab");
  } else {
    model->AddItem(IDC_MAHO_TAB_PIN, u"Pin Tab");
  }
  model->AddItem(IDC_MAHO_TAB_CLOSE_RIGHT, u"Close Tabs to the Right");
  model->AddItem(IDC_MAHO_TAB_CLOSE_LEFT, u"Close Tabs to the Left");

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
      maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser_);

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

  model->AddItem(IDC_MAHO_TAB_ARCHIVE, u"Archive Tab");
  model->AddItem(IDC_MAHO_TAB_ARCHIVE_BELOW, u"Archive Tabs Below");

  return model;
}

void MahoTabContextMenu::ExecuteCommand(int command_id, int event_flags) {
  TabStripModel* tab_strip_model =
      browser_ ? browser_->tab_strip_model() : nullptr;
  const bool has_valid_tab_index =
      tab_strip_model && tab_index_ >= 0 && tab_index_ < tab_strip_model->count();

  switch (command_id) {
    case IDC_MAHO_TAB_CLOSE:
      if (!has_valid_tab_index) {
        break;
      }
      if (content::WebContents* wc =
              tab_strip_model->GetWebContentsAt(tab_index_)) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("close_tab", {{"tab_id", helper->stable_tab_id()}});
        }
      }
      tab_strip_model->CloseWebContentsAt(
          tab_index_,
          TabCloseTypes::CLOSE_USER_GESTURE |
              TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      break;
    case IDC_MAHO_TAB_CLOSE_OTHERS: {
      if (!has_valid_tab_index) {
        break;
      }
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
      if (!has_valid_tab_index) {
        break;
      }
      TabStripModel* tab_strip = browser_->tab_strip_model();
      for (int i = tab_strip->count() - 1; i > tab_index_; --i) {
        tab_strip->CloseWebContentsAt(
            i, TabCloseTypes::CLOSE_USER_GESTURE |
                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      }
      break;
    }
    case IDC_MAHO_TAB_CLOSE_LEFT: {
      if (!has_valid_tab_index) {
        break;
      }
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
      if (!has_valid_tab_index) {
        break;
      }
      tab_strip_model->ActivateTabAt(tab_index_);
      chrome::DuplicateTab(browser_);
      break;
    case IDC_MAHO_TAB_PIN:
      if (!has_valid_tab_index) {
        break;
      }
      tab_strip_model->SetTabPinned(tab_index_, true);
      break;
    case IDC_MAHO_TAB_UNPIN:
      if (!has_valid_tab_index) {
        break;
      }
      tab_strip_model->SetTabPinned(tab_index_, false);
      break;
    case IDC_MAHO_TAB_MUTE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        contents->SetAudioMuted(true);
      }
      break;
    }
    case IDC_MAHO_TAB_UNMUTE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        contents->SetAudioMuted(false);
      }
      break;
    }
    case IDC_MAHO_TAB_COPY_URL: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(
            base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
      break;
    }
    case IDC_MAHO_TAB_SHARE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(
            base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
      break;
    }
    case IDC_MAHO_TAB_CHANGE_ICON:
      break;
    case IDC_MAHO_TAB_RENAME:
      if (delegate_) {
        delegate_->RenameTab(tab_index_);
      }
      break;
    case IDC_MAHO_TAB_RELOAD: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        contents->GetController().Reload(content::ReloadType::NORMAL, true);
      }
      break;
    }
    case IDC_MAHO_TAB_ADD_FAVORITE:
      break;
    case IDC_MAHO_TAB_REMOVE_FAVORITE:
      break;
    case IDC_MAHO_TAB_FREEZE:
      if (!has_valid_tab_index) {
        break;
      }
      break;
    case IDC_MAHO_TAB_OPEN_SPLIT: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        MahoSplitViewController(browser_).AddSplitWithURL(
            contents->GetVisibleURL());
      }
      break;
    }
    case IDC_MAHO_TAB_UNFREEZE:
      break;
    case IDC_MAHO_TAB_ARCHIVE:
      if (!has_valid_tab_index) {
        break;
      }
      break;
    case IDC_MAHO_TAB_ARCHIVE_BELOW:
      if (!has_valid_tab_index) {
        break;
      }
      break;
    case IDC_MAHO_TAB_BOOST_MY_BOOST:
    case IDC_MAHO_TAB_BOOST_EDIT:
      if (delegate_ && has_valid_tab_index) {
        delegate_->ActivateTabAndOpenBoostBubble(tab_index_);
      }
      break;
    default:
      if (IsMoveToSpaceCommand(command_id)) {
        int index = MoveToSpaceIndex(command_id);
        if (index >= 0 && index < static_cast<int>(space_entries_.size())) {
          const auto& entry = space_entries_[index];
          std::string section = IsMoveToSpacePinned(command_id) ? "pinned"
                                                                 : "today";
          content::WebContents* contents =
              has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                                  : nullptr;
          if (contents) {
            std::string target_space_id = entry.id;
            std::string tab_id;
            if (auto* helper = MahoTabIdHelper::FromWebContents(contents)) {
              tab_id = helper->stable_tab_id();
            }
            if (tab_id.empty()) {
              break;
            }
            base::ThreadPool::PostTask(
                FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
                base::BindOnce(
                    [](std::string space_id, std::string sec, std::string t_id) {
                      MahoCore* core = maho::GetCore();
                      if (!core) {
                        return;
                      }
                      base::DictValue event;
                      event.Set("kind", "move_tab_to_space");
                      event.Set("tab_id", t_id);
                      event.Set("target_space_id", space_id);
                      event.Set("section", sec);
                      std::string json;
                      base::JSONWriter::Write(event, &json);
                      char* result = maho_core_handle_event(core, json.c_str());
                      if (result) {
                        maho_string_free(result);
                      }
                    },
                    target_space_id, section, tab_id));
          }
        }
      }
      break;
  }
}

bool MahoTabContextMenu::IsCommandIdEnabled(int command_id) const {
  const TabStripModel* tab_strip_model =
      browser_ ? browser_->tab_strip_model() : nullptr;
  const bool has_valid_tab_index =
      tab_strip_model && tab_index_ >= 0 && tab_index_ < tab_strip_model->count();

  switch (command_id) {
    case IDC_MAHO_TAB_CLOSE:
    case IDC_MAHO_TAB_DUPLICATE:
    case IDC_MAHO_TAB_PIN:
    case IDC_MAHO_TAB_UNPIN:
    case IDC_MAHO_TAB_MUTE:
    case IDC_MAHO_TAB_UNMUTE:
    case IDC_MAHO_TAB_COPY_URL:
    case IDC_MAHO_TAB_RELOAD:
    case IDC_MAHO_TAB_OPEN_SPLIT:
    case IDC_MAHO_TAB_SHARE:
    case IDC_MAHO_TAB_CHANGE_ICON:
    case IDC_MAHO_TAB_ARCHIVE:
    case IDC_MAHO_TAB_ARCHIVE_BELOW:
      return has_valid_tab_index;
    case IDC_MAHO_TAB_RENAME:
      return has_valid_tab_index && delegate_ != nullptr;
    case IDC_MAHO_TAB_CLOSE_OTHERS:
      return has_valid_tab_index && tab_strip_model->count() > 1;
    case IDC_MAHO_TAB_CLOSE_RIGHT:
      return has_valid_tab_index &&
             tab_index_ < tab_strip_model->count() - 1;
    case IDC_MAHO_TAB_CLOSE_LEFT:
      return has_valid_tab_index && tab_index_ > 0;
    case IDC_MAHO_TAB_ADD_FAVORITE:
      return true;
    case IDC_MAHO_TAB_REMOVE_FAVORITE:
    case IDC_MAHO_TAB_FREEZE:
    case IDC_MAHO_TAB_UNFREEZE:
      return false;
    case IDC_MAHO_TAB_MOVE_TO_SPACE:
      return has_valid_tab_index;
    case IDC_MAHO_TAB_CUSTOMIZE_PAGE:
    case IDC_MAHO_TAB_BOOST_MY_BOOST:
    case IDC_MAHO_TAB_BOOST_EDIT:
      return delegate_ != nullptr && has_valid_tab_index;
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
