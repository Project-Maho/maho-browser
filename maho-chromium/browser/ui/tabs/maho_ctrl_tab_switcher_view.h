// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_VIEW_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_VIEW_H_

#include <cstddef>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}

namespace maho {

class MahoCtrlTabSwitcherCardView;

// The Ctrl+Tab MRU switcher overlay content view.
//
// Displays a horizontal row of tab preview cards, at most |max_visible|.
// The card at |selected_index()| is drawn highlighted.  When there are more
// tabs than |max_visible|, a "+N more" affordance card is appended (Phase 5;
// currently a placeholder card that shows a count when tabs.size() >
// max_visible).
//
// This view is passive: it owns no lifecycle logic and does not commit tab
// activation.  A separate controller (Phase 3) is responsible for creating
// the widget, advancing the cursor, and committing the selection on Ctrl
// release.
class MahoCtrlTabSwitcherView : public views::View {
  METADATA_HEADER(MahoCtrlTabSwitcherView, views::View)

 public:
  MahoCtrlTabSwitcherView(std::vector<content::WebContents*> tabs,
                          size_t initial_selected_index,
                          size_t max_visible);
  MahoCtrlTabSwitcherView(const MahoCtrlTabSwitcherView&) = delete;
  MahoCtrlTabSwitcherView& operator=(const MahoCtrlTabSwitcherView&) = delete;
  ~MahoCtrlTabSwitcherView() override;

  // Updates the highlighted card.  |index| must be within [0, card_count()).
  // Silently no-ops when out of range.
  void SetSelectedIndex(size_t index);

  size_t selected_index() const { return selected_index_; }
  size_t card_count() const { return card_views_.size(); }
  size_t total_tab_count() const { return tabs_.size(); }

  // True when the view has an extra "+N more" affordance appended after the
  // visible tab cards.  When this is true, the sequence of selectable slots
  // is [0, card_count()] rather than [0, card_count()).
  bool has_more_affordance() const { return has_more_affordance_; }
  size_t more_affordance_index() const { return card_views_.size(); }

  // Returns the WebContents at the current selection.  Returns nullptr if
  // the selected slot is the "+N more" affordance rather than a real tab.
  content::WebContents* GetSelectedWebContents() const;

  // Returns the WebContents at |index|, or nullptr if out of range or if
  // the slot is the "+N more" affordance.
  content::WebContents* GetWebContentsAt(size_t index) const;

  // views::View:
  void OnThemeChanged() override;

 private:
  void UpdateThemeColors();

  std::vector<content::WebContents*> tabs_;
  std::vector<raw_ptr<MahoCtrlTabSwitcherCardView>> card_views_;
  raw_ptr<views::View> more_affordance_view_ = nullptr;
  size_t selected_index_;
  bool has_more_affordance_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_VIEW_H_
