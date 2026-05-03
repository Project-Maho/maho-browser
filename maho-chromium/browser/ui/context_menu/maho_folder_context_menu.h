// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FOLDER_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FOLDER_CONTEXT_MENU_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

class MahoFolderContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  MahoFolderContextMenu(Browser* browser, const std::string& folder_id);
  ~MahoFolderContextMenu() override;

  MahoFolderContextMenu(const MahoFolderContextMenu&) = delete;
  MahoFolderContextMenu& operator=(const MahoFolderContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  raw_ptr<Browser> browser_;
  std::string folder_id_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FOLDER_CONTEXT_MENU_H_
