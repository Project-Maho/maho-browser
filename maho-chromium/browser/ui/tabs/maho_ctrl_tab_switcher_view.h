// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_VIEW_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_VIEW_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}

namespace gfx {
class Point;
}

namespace ui {
class MouseEvent;
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

  // Invoked with the slot index the pointer moved over (a tab card index in
  // [0, card_count()), or more_affordance_index()). Wired by the controller so
  // hovering moves the highlighted selection to match the keyboard cursor.
  using SlotCallback = base::RepeatingCallback<void(size_t index)>;
  void SetHoverCallback(SlotCallback callback);

  // Returns the selectable slot index whose card (or "+N more" affordance)
  // contains |screen_point|, or nullopt if the point is outside every slot.
  // Used by the controller's application-scoped mouse monitor to resolve a
  // click into a slot (clicks are not delivered to this view directly on
  // macOS, where the overlay is a non-activatable inactive window).
  std::optional<size_t> SlotIndexAtScreen(const gfx::Point& screen_point) const;

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
  void OnMouseMoved(const ui::MouseEvent& event) override;
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;

 private:
  void UpdateThemeColors();

  // Returns the selectable slot index containing |location| (in this view's
  // coordinate space), or nullopt if the point is not over a card or the
  // "+N more" affordance.
  std::optional<size_t> SlotIndexAt(const gfx::Point& location) const;

  std::vector<content::WebContents*> tabs_;
  std::vector<raw_ptr<MahoCtrlTabSwitcherCardView>> card_views_;
  raw_ptr<views::View> more_affordance_view_ = nullptr;
  size_t selected_index_;
  bool has_more_affordance_ = false;
  SlotCallback hover_callback_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_VIEW_H_
