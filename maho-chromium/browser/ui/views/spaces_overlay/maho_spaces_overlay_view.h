// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_VIEW_H_

#include <string>
#include <memory>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class ImageButton;
class Label;
class ScrollView;
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoSpacesOverlayBoardView;

class MahoSpacesOverlayView : public views::View {
  METADATA_HEADER(MahoSpacesOverlayView, views::View)

 public:
  using CancelCallback = base::OnceClosure;
  using ActionCallback = base::OnceClosure;
  using TabSelectedCallback =
      base::RepeatingCallback<void(const std::string& space_id,
                                   const std::string& tab_id)>;

  MahoSpacesOverlayView(Browser* browser,
                        CancelCallback cancel_callback,
                        ActionCallback action_callback);

  // Test-only constructor: accepts a pre-built model so unit tests can exercise
  // view rendering without a live Browser instance or maho-core FFI.
  struct ForTestingTag {};
  MahoSpacesOverlayView(ForTestingTag,
                        SpaceBoardModel model,
                        CancelCallback cancel_callback,
                        ActionCallback action_callback);

  MahoSpacesOverlayView(const MahoSpacesOverlayView&) = delete;
  MahoSpacesOverlayView& operator=(const MahoSpacesOverlayView&) = delete;
  ~MahoSpacesOverlayView() override;

  // Testing accessors.
  MahoSpacesOverlayBoardView* GetBoardViewForTesting() { return board_view_; }
  views::View* GetAnimatedView() { return surface_; }

  void SetPalette(const MahoSidebarPalette& palette);
  // Invoked from OnThemeChanged() to pull the docked rail's authoritative
  // snapshot. This overlay lives in its own top-level widget, so Chromium
  // delivers its theme change independently of the rail's; without the pull the
  // widget can repaint from a snapshot the rail has already replaced.
  void set_palette_refresh_callback(base::RepeatingClosure callback) {
    palette_refresh_callback_ = std::move(callback);
  }
  bool palette_opaque_for_testing() const { return palette_.opaque; }

  void RequestFocus() override;
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

 private:
  void Init(SpaceBoardModel model,
            CancelCallback cancel_callback,
            ActionCallback action_callback);
  void DismissCancel();
  void DismissAction();
  void HandleSpaceSelected(const std::string& space_id);
  void HandleTabSelected(const std::string& space_id, const std::string& tab_id);

  raw_ptr<Browser> browser_;
  CancelCallback cancel_callback_;
  ActionCallback action_callback_;
  SpaceBoardModel model_;
  raw_ptr<views::View> surface_ = nullptr;
  raw_ptr<views::ScrollView> board_scroll_view_ = nullptr;
  raw_ptr<MahoSpacesOverlayBoardView> board_view_ = nullptr;
  raw_ptr<views::View> separator_ = nullptr;
  MahoSidebarPalette palette_;
  base::RepeatingClosure palette_refresh_callback_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_VIEW_H_
