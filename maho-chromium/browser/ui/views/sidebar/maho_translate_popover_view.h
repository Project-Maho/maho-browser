// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRANSLATE_POPOVER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRANSLATE_POPOVER_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/view.h"

namespace views {
class Widget;
}  // namespace views

namespace maho {

// Arc-style confirm popover for the sidebar translate affordance: "Aa" badge,
// "Translate this page to <language>?", and Options/Translate buttons. Owns no
// translation logic; reports the choice through the Show() callbacks.
class MahoTranslatePopoverView : public views::View {
  METADATA_HEADER(MahoTranslatePopoverView, views::View)

 public:
  // The callback receives the Options button to anchor the menu and a weak
  // close closure. The owner runs the closure after a terminal menu action so
  // the confirm popover never remains behind a completed action.
  using OptionsCallback =
      base::RepeatingCallback<void(views::View*, base::RepeatingClosure)>;

  MahoTranslatePopoverView(const std::u16string& target_language_name,
                           base::RepeatingClosure translate_callback,
                           OptionsCallback options_callback);
  MahoTranslatePopoverView(const MahoTranslatePopoverView&) = delete;
  MahoTranslatePopoverView& operator=(const MahoTranslatePopoverView&) = delete;
  ~MahoTranslatePopoverView() override;

  // Shows the popover anchored below `anchor_view`; returns the hosting widget.
  static views::Widget* Show(views::View* anchor_view,
                             const std::u16string& target_language_name,
                             base::RepeatingClosure translate_callback,
                             OptionsCallback options_callback);

  // The arrow Show() uses for |anchor_view|: TOP_RIGHT when the anchor sits
  // in the trailing half of its window (the popover then grows toward the
  // leading edge), TOP_LEFT otherwise.
  static views::BubbleBorder::Arrow ArrowForAnchorForTesting(
      const views::View* anchor_view);

 private:
  void OnTranslatePressed();
  void OnOptionsPressed();
  void ClosePopover();

  base::RepeatingClosure translate_callback_;
  OptionsCallback options_callback_;
  raw_ptr<views::View> options_button_ = nullptr;

  base::WeakPtrFactory<MahoTranslatePopoverView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRANSLATE_POPOVER_VIEW_H_
