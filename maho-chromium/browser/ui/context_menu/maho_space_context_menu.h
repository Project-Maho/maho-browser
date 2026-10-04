// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_SPACE_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_SPACE_CONTEXT_MENU_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

class MahoSpaceContextMenu : public ui::SimpleMenuModel::Delegate,
                             public ui::SelectFileDialog::Listener {
 public:
  MahoSpaceContextMenu(Browser* browser, const std::string& space_id);
  ~MahoSpaceContextMenu() override;

  MahoSpaceContextMenu(const MahoSpaceContextMenu&) = delete;
  MahoSpaceContextMenu& operator=(const MahoSpaceContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

   // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
  enum class PendingFileOp {
    kNone,
    kExportTheme,
    kImportTheme,
  };

  void ShareSpace();
  void ExportTheme();
  void ImportTheme();

  raw_ptr<Browser> browser_;
  std::string space_id_;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  PendingFileOp pending_file_op_ = PendingFileOp::kNone;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_SPACE_CONTEXT_MENU_H_
