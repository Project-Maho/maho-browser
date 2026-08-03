// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_TAB_CONTEXT_MENU_H_
#define MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_TAB_CONTEXT_MENU_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

namespace maho::context_menu {

// Shared tab-action primitives used by BOTH the tab context menu and the
// favorites context menu, so the two menus never diverge in behavior. A
// favorite is a live tab with a role, so favorite menu actions resolve the
// underlying tab through these helpers instead of re-implementing them.

// Resolves a stable core tab id to its live tab strip index in |browser|,
// or -1 when |browser|/|tab_id| is unavailable or the tab is not present in
// the current strip.
int ResolveTabIndex(Browser* browser, const std::string& tab_id);

// Live WebContents for |tab_id| in |browser|, or nullptr.
content::WebContents* GetWebContentsForTabId(Browser* browser,
                                             const std::string& tab_id);

// Opens the Boost window for the active domain of |contents|. No-op on null.
void BoostTab(Browser* browser, content::WebContents* contents);

// Reloads |contents|. No-op on null.
void ReloadTab(content::WebContents* contents);

// Duplicates the tab at |tab_index| in |browser|. No-op when out of range.
void DuplicateTab(Browser* browser, int tab_index);

// Mutes/unmutes |contents| and notifies core. No-op on null.
void SetTabMuted(content::WebContents* contents, bool muted);

}  // namespace maho::context_menu

class MahoTabContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void RenameTab(int tab_index) {}
    virtual void CloseSelectedTabs() {}
    virtual void CloseTabById(const std::string& tab_id) {}
    virtual void PinSelectedTabs(bool pin) {}
    virtual void MoveSelectedTabsToSpace(const std::string& space_id) {}
    virtual void MuteSelectedTabs(bool mute) {}
    virtual void ArchiveSelectedTabs() {}
    virtual void OpenSelectedTabsInSplit() {}
    virtual void NewFolderWithSelectedTabs() {}
  };

  MahoTabContextMenu(Browser* browser, int tab_index);
  MahoTabContextMenu(Browser* browser, int tab_index, Delegate* delegate);
  MahoTabContextMenu(Browser* browser,
                     int tab_index,
                     Delegate* delegate,
                     std::string core_tab_id,
                     std::vector<std::string> selected_tab_ids = {});
  ~MahoTabContextMenu() override;

  MahoTabContextMenu(const MahoTabContextMenu&) = delete;
  MahoTabContextMenu& operator=(const MahoTabContextMenu&) = delete;

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

  const std::vector<std::string>& selected_tab_ids_for_testing() const {
    return selected_tab_ids_;
  }

 private:
  struct SpaceEntry {
    std::string id;
    std::string name;
    std::string icon;
  };

  // Number of selected tabs whose id resolves to a contained live strip index
  // with no existing split. 0 when the browser/model is unavailable. The
  // multi-select Open-in-Split command is shown/enabled only when this is >= 2.
  int CountEligibleSplitTabs() const;

  raw_ptr<Browser> browser_;
  int tab_index_;
  raw_ptr<Delegate> delegate_ = nullptr;
  std::string core_tab_id_;
  std::vector<std::string> selected_tab_ids_;
  std::vector<SpaceEntry> space_entries_;
  std::unique_ptr<ui::SimpleMenuModel> move_to_space_submenu_;
};

#endif  // MAHO_CHROMIUM_BROWSER_UI_CONTEXT_MENU_MAHO_TAB_CONTEXT_MENU_H_
