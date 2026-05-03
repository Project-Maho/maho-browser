// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"

#include "base/logging.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "ui/menus/simple_menu_model.h"

MahoFolderContextMenu::MahoFolderContextMenu(Browser* browser,
                                               const std::string& folder_id)
    : browser_(browser), folder_id_(folder_id) {}

MahoFolderContextMenu::~MahoFolderContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel> MahoFolderContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_FOLDER_RENAME, u"Rename Folder");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FOLDER_PIN, u"Pin Folder");
  model->AddItem(IDC_MAHO_FOLDER_NEW_TAB, u"New Tab in Folder");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_FOLDER_DELETE, u"Delete Folder");

  return model;
}

void MahoFolderContextMenu::ExecuteCommand(int command_id, int event_flags) {
  // TODO(maho): Wire up Mojo IPC calls to MahoCore.
  switch (command_id) {
    case IDC_MAHO_FOLDER_RENAME:
      LOG(INFO) << "MahoFolderContextMenu: Rename Folder " << folder_id_;
      break;
    case IDC_MAHO_FOLDER_PIN:
      LOG(INFO) << "MahoFolderContextMenu: Pin Folder " << folder_id_;
      break;
    case IDC_MAHO_FOLDER_UNPIN:
      LOG(INFO) << "MahoFolderContextMenu: Unpin Folder " << folder_id_;
      break;
    case IDC_MAHO_FOLDER_NEW_TAB:
      LOG(INFO) << "MahoFolderContextMenu: New Tab in " << folder_id_;
      break;
    case IDC_MAHO_FOLDER_DELETE:
      LOG(INFO) << "MahoFolderContextMenu: Delete Folder " << folder_id_;
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
