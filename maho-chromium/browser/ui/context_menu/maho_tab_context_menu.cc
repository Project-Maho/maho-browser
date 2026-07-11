// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"

#include <optional>
#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/menus/simple_menu_model.h"

namespace {
std::string QueryTabRole(const std::string& tab_id) {
  ::MahoCore* core = maho::GetCore();
  if (!core) {
    return "";
  }
  char* json_str = maho_core_get_tab_snapshot_by_id(core, tab_id.c_str());
  if (!json_str) {
    return "";
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return "";
  }

  const auto& dict = parsed->GetDict();
  if (const auto* role = dict.FindDict("role")) {
    if (const std::string* type = role->FindString("type")) {
      return *type;
    }
  }
  return "";
}

int ResolveTabIndex(Browser* browser, const std::string& tab_id) {
  if (!browser || !browser->tab_strip_model()) return -1;
  TabStripModel* model = browser->tab_strip_model();
  for (int i = 0; i < model->count(); ++i) {
    if (content::WebContents* wc = model->GetWebContentsAt(i)) {
      if (auto* helper = MahoTabIdHelper::FromWebContents(wc)) {
        if (helper->stable_tab_id() == tab_id) {
          return i;
        }
      }
    }
  }
  return -1;
}
}  // namespace

MahoTabContextMenu::MahoTabContextMenu(Browser* browser, int tab_index)
    : browser_(browser), tab_index_(tab_index), delegate_(nullptr) {}

MahoTabContextMenu::MahoTabContextMenu(Browser* browser,
                                       int tab_index,
                                       Delegate* delegate)
    : browser_(browser), tab_index_(tab_index), delegate_(delegate) {}

MahoTabContextMenu::MahoTabContextMenu(Browser* browser,
                                       int tab_index,
                                       Delegate* delegate,
                                       std::string core_tab_id,
                                       std::vector<std::string> selected_tab_ids)
    : browser_(browser),
      tab_index_(tab_index),
      delegate_(delegate),
      core_tab_id_(std::move(core_tab_id)),
      selected_tab_ids_(std::move(selected_tab_ids)) {}

MahoTabContextMenu::~MahoTabContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel> MahoTabContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  const bool is_multi = (selected_tab_ids_.size() > 1);

  if (is_multi) {
    model->AddItem(IDC_MAHO_TAB_CLOSE, u"Close Selected Tabs");

    bool all_pinned = true;
    for (const auto& id : selected_tab_ids_) {
      std::string role = QueryTabRole(id);
      if (role != "pinned") {
        all_pinned = false;
        break;
      }
    }
    if (all_pinned) {
      model->AddItem(IDC_MAHO_TAB_UNPIN, u"Unpin Selected Tabs");
    } else {
      model->AddItem(IDC_MAHO_TAB_PIN, u"Pin Selected Tabs");
    }

    bool any_unmuted = false;
    TabStripModel* tab_strip_model = browser_ ? browser_->tab_strip_model() : nullptr;
    if (tab_strip_model) {
      for (const auto& id : selected_tab_ids_) {
        int idx = ResolveTabIndex(browser_, id);
        if (idx >= 0) {
          content::WebContents* contents = tab_strip_model->GetWebContentsAt(idx);
          if (contents && !contents->IsAudioMuted()) {
            any_unmuted = true;
            break;
          }
        }
      }
    }
    if (any_unmuted) {
      model->AddItem(IDC_MAHO_TAB_MUTE, u"Mute Selected Tabs");
    } else {
      model->AddItem(IDC_MAHO_TAB_UNMUTE, u"Unmute Selected Tabs");
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

    model->AddSubMenu(IDC_MAHO_TAB_MOVE_TO_SPACE, u"Move Selected Tabs to Space",
                      move_to_space_submenu_.get());

    return model;
  }

  const TabStripModel* tab_strip_model =
      browser_ ? browser_->tab_strip_model() : nullptr;
  const bool has_valid_tab_index = tab_strip_model && tab_index_ >= 0 &&
                                   tab_index_ < tab_strip_model->count();

  content::WebContents* contents =
      has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                          : nullptr;

  //  0  Copy Link
  model->AddItem(IDC_MAHO_TAB_COPY_URL, u"Copy Link");

  //  1  Share
  model->AddItem(IDC_MAHO_TAB_SHARE, u"Share");

  //  2  [separator]
  model->AddSeparator(ui::NORMAL_SEPARATOR);

  //  3  Change Icon…
  model->AddItem(IDC_MAHO_TAB_CHANGE_ICON, u"Change Icon...");

  //  4  Rename…
  model->AddItem(IDC_MAHO_TAB_RENAME, u"Rename...");

  //  5  Mute
  if (contents && contents->IsAudioMuted()) {
    model->AddItem(IDC_MAHO_TAB_UNMUTE, u"Unmute Tab");
  } else {
    model->AddItem(IDC_MAHO_TAB_MUTE, u"Mute Tab");
  }

  //  7  [separator]
  model->AddSeparator(ui::NORMAL_SEPARATOR);

  //  8  Open in Split View / Remove from Split View
  {
    TabStripModel* split_tsm =
        browser_ ? browser_->tab_strip_model() : nullptr;
    const bool in_split = split_tsm &&
                          split_tsm->ContainsIndex(tab_index_) &&
                          split_tsm->GetSplitForTab(tab_index_).has_value();
    if (in_split) {
      model->AddItem(IDC_MAHO_TAB_REMOVE_SPLIT, u"Remove from Split View");
    } else {
      model->AddItem(IDC_MAHO_TAB_OPEN_SPLIT, u"Open in Split View");
    }
  }

  //  9  Duplicate
  model->AddItem(IDC_MAHO_TAB_DUPLICATE, u"Duplicate Tab");

  // 10  Boost
  model->AddItem(IDC_MAHO_TAB_BOOST, u"Boost");

  // 11  Move to (submenu)
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

  // 12  [separator]
  model->AddSeparator(ui::NORMAL_SEPARATOR);

  // 13  Archive Tab
  model->AddItem(IDC_MAHO_TAB_ARCHIVE, u"Archive Tab");

  // 14  Archive Tabs Below
  model->AddItem(IDC_MAHO_TAB_ARCHIVE_BELOW, u"Archive Tabs Below");

  return model;
}


void MahoTabContextMenu::ExecuteCommand(int command_id, int event_flags) {
  const bool is_multi = (selected_tab_ids_.size() > 1);

  if (is_multi && delegate_) {
    switch (command_id) {
      case IDC_MAHO_TAB_CLOSE:
        delegate_->CloseSelectedTabs();
        break;
      case IDC_MAHO_TAB_PIN:
        delegate_->PinSelectedTabs(true);
        break;
      case IDC_MAHO_TAB_UNPIN:
        delegate_->PinSelectedTabs(false);
        break;
      case IDC_MAHO_TAB_MUTE:
        delegate_->MuteSelectedTabs(true);
        break;
      case IDC_MAHO_TAB_UNMUTE:
        delegate_->MuteSelectedTabs(false);
        break;
      default:
        if (IsMoveToSpaceCommand(command_id)) {
          int index = MoveToSpaceIndex(command_id);
          if (index >= 0 && index < static_cast<int>(space_entries_.size())) {
            const auto& entry = space_entries_[index];
            delegate_->MoveSelectedTabsToSpace(entry.id);
          }
        }
        break;
    }
    return;
  }

  TabStripModel* tab_strip_model =
      browser_ ? browser_->tab_strip_model() : nullptr;
  const bool has_valid_tab_index = tab_strip_model && tab_index_ >= 0 &&
                                   tab_index_ < tab_strip_model->count();

  switch (command_id) {
    case IDC_MAHO_TAB_CLOSE:
      if (!has_valid_tab_index) {
        break;
      }
      if (content::WebContents* wc =
              tab_strip_model->GetWebContentsAt(tab_index_)) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("close_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      tab_strip_model->CloseWebContentsAt(
          tab_index_, TabCloseTypes::CLOSE_USER_GESTURE |
                          TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      break;
    case IDC_MAHO_TAB_CLOSE_OTHERS: {
      if (!has_valid_tab_index) {
        break;
      }
      TabStripModel* tab_strip = browser_->tab_strip_model();
      for (int i = tab_strip->count() - 1; i >= 0; --i) {
        if (i != tab_index_) {
          if (content::WebContents* wc = tab_strip->GetWebContentsAt(i)) {
            if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
                helper && !helper->stable_tab_id().empty()) {
              if (std::find(selected_tab_ids_.begin(),
                            selected_tab_ids_.end(),
                            helper->stable_tab_id()) !=
                  selected_tab_ids_.end()) {
                continue;
              }
              maho::DispatchShellEvent("close_tab",
                                       {{"tab_id", helper->stable_tab_id()}});
            }
          }
          tab_strip->CloseWebContentsAt(
              i, TabCloseTypes::CLOSE_USER_GESTURE |
                     TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
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
        if (content::WebContents* wc = tab_strip->GetWebContentsAt(i)) {
          if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
              helper && !helper->stable_tab_id().empty()) {
            maho::DispatchShellEvent("close_tab",
                                     {{"tab_id", helper->stable_tab_id()}});
          }
        }
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
        if (content::WebContents* wc = tab_strip->GetWebContentsAt(i)) {
          if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
              helper && !helper->stable_tab_id().empty()) {
            maho::DispatchShellEvent("close_tab",
                                     {{"tab_id", helper->stable_tab_id()}});
          }
        }
        tab_strip->CloseWebContentsAt(
            i, TabCloseTypes::CLOSE_USER_GESTURE |
                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      }
      break;
    }
    case IDC_MAHO_TAB_DUPLICATE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        chrome::DuplicateTabAt(browser_, tab_index_);
      }
      break;
    }
    case IDC_MAHO_TAB_BOOST: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        maho::MahoBoostWindowController::GetForBrowser(browser_,
                                                       browser_->profile())
            .ShowForActiveDomain(contents);
      }
      break;
    }
    case IDC_MAHO_TAB_PIN: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("pin_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      break;
    }
    case IDC_MAHO_TAB_UNPIN: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("unpin_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      break;
    }
    case IDC_MAHO_TAB_MUTE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        contents->SetAudioMuted(true);
        if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("mute_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      break;
    }
    case IDC_MAHO_TAB_UNMUTE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        contents->SetAudioMuted(false);
        if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("unmute_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      break;
    }
    case IDC_MAHO_TAB_COPY_URL: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
      break;
    }
    case IDC_MAHO_TAB_SHARE: {
      content::WebContents* contents =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
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
    case IDC_MAHO_TAB_ADD_FAVORITE: {  // L3-EXEMPT
      if (!has_valid_tab_index) {
        break;
      }
      content::WebContents* wc = tab_strip_model->GetWebContentsAt(tab_index_);
      if (wc) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("favorite_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      break;
    }
    case IDC_MAHO_TAB_REMOVE_FAVORITE: {  // L3-EXEMPT
      if (!has_valid_tab_index) {
        break;
      }
      if (content::WebContents* wc =
              tab_strip_model->GetWebContentsAt(tab_index_)) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          maho::DispatchShellEvent("close_tab",
                                   {{"tab_id", helper->stable_tab_id()}});
        }
      }
      tab_strip_model->CloseWebContentsAt(
          tab_index_, TabCloseTypes::CLOSE_USER_GESTURE |
                          TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      break;
    }
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
    case IDC_MAHO_TAB_REMOVE_SPLIT:
      MahoSplitViewController(browser_).RemoveSplitForTab(tab_index_);
      break;
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
    case IDC_MAHO_TAB_MOVE_TO_SPACE:
      break;
    default:
      if (IsMoveToSpaceCommand(command_id)) {
        int index = MoveToSpaceIndex(command_id);
        if (index >= 0 && index < static_cast<int>(space_entries_.size()) && has_valid_tab_index) {
          const auto& entry = space_entries_[index];
          if (content::WebContents* wc =
                  tab_strip_model->GetWebContentsAt(tab_index_)) {
            if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
                helper && !helper->stable_tab_id().empty()) {
              maho::DispatchShellEvent("move_tab_to_space",
                                       {{"space_id", entry.id},
                                        {"tab_id", helper->stable_tab_id()}});
            }
          }
        }
      }
      break;
  }
}

bool MahoTabContextMenu::IsCommandIdEnabled(int command_id) const {
  if (selected_tab_ids_.size() > 1) {
    switch (command_id) {
      case IDC_MAHO_TAB_CLOSE:
      case IDC_MAHO_TAB_PIN:
      case IDC_MAHO_TAB_UNPIN:
      case IDC_MAHO_TAB_MUTE:
      case IDC_MAHO_TAB_UNMUTE:
      case IDC_MAHO_TAB_MOVE_TO_SPACE:
        return true;
      default:
        if (IsMoveToSpaceCommand(command_id)) {
          return true;
        }
        return false;
    }
  }

  const TabStripModel* tab_strip_model =
      browser_ ? browser_->tab_strip_model() : nullptr;
  const bool has_valid_tab_index = tab_strip_model && tab_index_ >= 0 &&
                                   tab_index_ < tab_strip_model->count();

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
    case IDC_MAHO_TAB_REMOVE_SPLIT:
    case IDC_MAHO_TAB_BOOST:
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
      return has_valid_tab_index && tab_index_ < tab_strip_model->count() - 1;
    case IDC_MAHO_TAB_CLOSE_LEFT:
      return has_valid_tab_index && tab_index_ > 0;
    case IDC_MAHO_TAB_ADD_FAVORITE: {  // L3-EXEMPT
      content::WebContents* wc =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (wc) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          std::string role_type = QueryTabRole(helper->stable_tab_id());
          if (role_type == "favorite") {
            return false;
          }
        }
      }
      return has_valid_tab_index;
    }
    case IDC_MAHO_TAB_REMOVE_FAVORITE: {  // L3-EXEMPT
      content::WebContents* wc =
          has_valid_tab_index ? tab_strip_model->GetWebContentsAt(tab_index_)
                              : nullptr;
      if (wc) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
            helper && !helper->stable_tab_id().empty()) {
          std::string role_type = QueryTabRole(helper->stable_tab_id());
          if (role_type == "favorite") {
            return true;
          }
        }
      }
      return false;
    }
    case IDC_MAHO_TAB_FREEZE:
    case IDC_MAHO_TAB_UNFREEZE:
      return false;
    case IDC_MAHO_TAB_MOVE_TO_SPACE:
      return has_valid_tab_index;
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
