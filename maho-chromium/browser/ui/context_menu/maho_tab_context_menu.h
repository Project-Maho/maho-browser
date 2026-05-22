// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_TAB_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_TAB_CONTEXT_MENU_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

class MahoTabContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void ActivateTabAndOpenBoostBubble(int tab_index) = 0;
    virtual void RenameTab(int tab_index) {}
  };

  MahoTabContextMenu(Browser* browser, int tab_index);
  MahoTabContextMenu(Browser* browser, int tab_index, Delegate* delegate);
  MahoTabContextMenu(Browser* browser,
                     int tab_index,
                     Delegate* delegate,
                     std::string core_tab_id);
  ~MahoTabContextMenu() override;

  MahoTabContextMenu(const MahoTabContextMenu&) = delete;
  MahoTabContextMenu& operator=(const MahoTabContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  struct SpaceEntry {
    std::string id;
    std::string name;
    std::string icon;
  };

  raw_ptr<Browser> browser_;
  int tab_index_;
  raw_ptr<Delegate> delegate_ = nullptr;
  std::string core_tab_id_;
  std::vector<SpaceEntry> space_entries_;
  std::unique_ptr<ui::SimpleMenuModel> move_to_space_submenu_;
  std::unique_ptr<ui::SimpleMenuModel> boost_submenu_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_TAB_CONTEXT_MENU_H_
