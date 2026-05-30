// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FOLDER_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FOLDER_CONTEXT_MENU_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

class MahoFolderContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  using BeginRenameCallback = base::OnceClosure;

  MahoFolderContextMenu(Browser* browser, const std::string& folder_id);

  MahoFolderContextMenu(Browser* browser,
                        const std::string& space_id,
                        const std::string& folder_id,
                        const std::string& parent_folder_id,
                        BeginRenameCallback begin_rename_callback);

  ~MahoFolderContextMenu() override;

  MahoFolderContextMenu(const MahoFolderContextMenu&) = delete;
  MahoFolderContextMenu& operator=(const MahoFolderContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  raw_ptr<Browser> browser_;
  std::string space_id_;
  std::string folder_id_;
  std::string parent_folder_id_;
  BeginRenameCallback begin_rename_callback_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FOLDER_CONTEXT_MENU_H_
