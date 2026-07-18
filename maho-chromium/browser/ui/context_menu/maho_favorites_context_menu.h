// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FAVORITES_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FAVORITES_CONTEXT_MENU_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

class Browser;

class MahoFavoritesContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  MahoFavoritesContextMenu(Browser* browser,
                           const GURL& url,
                           const std::u16string& title,
                           const std::string& tab_id);
  ~MahoFavoritesContextMenu() override;

  MahoFavoritesContextMenu(const MahoFavoritesContextMenu&) = delete;
  MahoFavoritesContextMenu& operator=(const MahoFavoritesContextMenu&) = delete;

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
  GURL url_;
  std::u16string title_;
  std::string tab_id_;
  std::unique_ptr<ui::SimpleMenuModel> move_to_space_submenu_;
  std::vector<SpaceEntry> space_entries_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_FAVORITES_CONTEXT_MENU_H_
