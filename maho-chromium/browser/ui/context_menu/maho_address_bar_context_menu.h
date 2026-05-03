// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_ADDRESS_BAR_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_ADDRESS_BAR_CONTEXT_MENU_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

class MahoAddressBarContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  MahoAddressBarContextMenu(Browser* browser,
                            base::RepeatingClosure on_edit_address =
                                base::RepeatingClosure());
  ~MahoAddressBarContextMenu() override;

  MahoAddressBarContextMenu(const MahoAddressBarContextMenu&) = delete;
  MahoAddressBarContextMenu& operator=(const MahoAddressBarContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  raw_ptr<Browser> browser_;
  base::RepeatingClosure on_edit_address_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_ADDRESS_BAR_CONTEXT_MENU_H_
