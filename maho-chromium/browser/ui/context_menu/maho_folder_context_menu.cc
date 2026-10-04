// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"

#include <memory>

#include "base/functional/callback_helpers.h"
#include "chrome/browser/ui/browser.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_dnd_events.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "ui/menus/simple_menu_model.h"

MahoFolderContextMenu::MahoFolderContextMenu(Browser* browser,
                                               const std::string& folder_id)
    : browser_(browser), folder_id_(folder_id) {}

MahoFolderContextMenu::MahoFolderContextMenu(
    Browser* browser,
    const std::string& space_id,
    const std::string& folder_id,
    const std::string& parent_folder_id,
    BeginRenameCallback begin_rename_callback)
    : browser_(browser),
      space_id_(space_id),
      folder_id_(folder_id),
      parent_folder_id_(parent_folder_id),
      begin_rename_callback_(std::move(begin_rename_callback)) {}

MahoFolderContextMenu::~MahoFolderContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel> MahoFolderContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_FOLDER_RENAME, u"Rename Folder");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  if (!parent_folder_id_.empty()) {
    model->AddItem(IDC_MAHO_FOLDER_MOVE_TO_ROOT, u"Move Folder to Root");
    model->AddSeparator(ui::NORMAL_SEPARATOR);
  }

  model->AddItem(IDC_MAHO_FOLDER_CONVERT_TO_SPACE, u"Convert Folder to Space");
  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FOLDER_DELETE, u"Delete Folder");

  return model;
}

void MahoFolderContextMenu::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case IDC_MAHO_FOLDER_RENAME:
      if (begin_rename_callback_) {
        std::move(begin_rename_callback_).Run();
      }
      break;



    case IDC_MAHO_FOLDER_DELETE:
      if (!space_id_.empty() && !folder_id_.empty()) {
        maho::DispatchShellEvent("delete_folder",
                                 {{"space_id", space_id_},
                                  {"folder_id", folder_id_}});
      }
      break;

    case IDC_MAHO_FOLDER_CONVERT_TO_SPACE:
      if (!space_id_.empty() && !folder_id_.empty()) {
        maho::DispatchShellEvent("convert_folder_to_space",
                                 {{"space_id", space_id_},
                                  {"folder_id", folder_id_}});
      }
      break;

    case IDC_MAHO_FOLDER_MOVE_TO_ROOT:
      if (!space_id_.empty() && !folder_id_.empty()) {
        maho::DispatchShellEvent(maho::sidebar::kMoveFolderToRoot,
                                 {{"space_id", space_id_},
                                  {"folder_id", folder_id_}});
      }
      break;

    default:
      break;
  }
}

bool MahoFolderContextMenu::IsCommandIdEnabled(int command_id) const {
  return true;
}

bool MahoFolderContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
