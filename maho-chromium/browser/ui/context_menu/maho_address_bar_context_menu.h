// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_ADDRESS_BAR_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_ADDRESS_BAR_CONTEXT_MENU_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

class MahoAddressBarContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  // |target_contents_getter| resolves the tab the copy actions act on (e.g.
  // the right-clicked split pane, which need not be the active tab). When
  // null, the browser's active tab is used.
  MahoAddressBarContextMenu(
      Browser* browser,
      base::RepeatingClosure on_edit_address = base::RepeatingClosure(),
      base::RepeatingCallback<content::WebContents*()> target_contents_getter =
          base::RepeatingCallback<content::WebContents*()>());
  ~MahoAddressBarContextMenu() override;

  MahoAddressBarContextMenu(const MahoAddressBarContextMenu&) = delete;
  MahoAddressBarContextMenu& operator=(const MahoAddressBarContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  content::WebContents* GetTargetContents() const;

  raw_ptr<Browser> browser_;
  base::RepeatingClosure on_edit_address_;
  base::RepeatingCallback<content::WebContents*()> target_contents_getter_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_ADDRESS_BAR_CONTEXT_MENU_H_
