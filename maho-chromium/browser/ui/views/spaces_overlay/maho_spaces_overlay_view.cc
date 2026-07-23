// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho_spaces_overlay_board_view.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kOverlayInsetDp = 14;
constexpr int kSurfaceInsetDp = 14;
[[maybe_unused]] constexpr int kSurfaceCornerRadiusDp = 22;
constexpr int kSurfaceSpacingDp = 10;

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayView)
END_METADATA

MahoSpacesOverlayView::MahoSpacesOverlayView(Browser* browser,
                                             CancelCallback cancel_callback,
                                             ActionCallback action_callback)
    : browser_(browser) {
  Init(BuildSpacesBoardModel(),
       std::move(cancel_callback),
       std::move(action_callback));
}

MahoSpacesOverlayView::MahoSpacesOverlayView(ForTestingTag,
                                             SpaceBoardModel model,
                                             CancelCallback cancel_callback,
                                             ActionCallback action_callback)
    : browser_(nullptr) {
  Init(std::move(model),
       std::move(cancel_callback),
       std::move(action_callback));
}

void MahoSpacesOverlayView::Init(SpaceBoardModel model,
                                 CancelCallback cancel_callback,
                                 ActionCallback action_callback) {
  cancel_callback_ = std::move(cancel_callback);
  action_callback_ = std::move(action_callback);
  model_ = std::move(model);

  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kOverlayInsetDp, kOverlayInsetDp), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  surface_ = AddChildView(std::make_unique<views::View>());
  surface_->SetPaintToLayer();
  surface_->layer()->SetFillsBoundsOpaquely(false);
  surface_->layer()->SetOpacity(0.0f);
  surface_->layer()->SetTransform(
      gfx::Transform::MakeTranslation(0, 12));
  auto* surface_layout = surface_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kSurfaceInsetDp, kSurfaceInsetDp), kSurfaceSpacingDp));
  surface_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(surface_, 1);

  board_scroll_view_ = surface_->AddChildView(std::make_unique<views::ScrollView>());
  board_scroll_view_->SetBackgroundColor(std::nullopt);
  board_scroll_view_->SetDrawOverflowIndicator(false);
  board_scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  board_scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  surface_layout->SetFlexForView(board_scroll_view_, 1);

  board_view_ = board_scroll_view_->SetContents(
      std::make_unique<MahoSpacesOverlayBoardView>(
          browser_,
          SpacesBoardRenderMode::kFullViewport,
          model_,
          base::BindRepeating(&MahoSpacesOverlayView::HandleSpaceSelected,
                              base::Unretained(this)),
          base::BindRepeating(&MahoSpacesOverlayView::HandleTabSelected,
                              base::Unretained(this))));

  // Hairline separator
  separator_ = surface_->AddChildView(std::make_unique<views::View>());
  separator_->SetPreferredSize(gfx::Size(0, 1));
  separator_->SetVisible(false);
}

MahoSpacesOverlayView::~MahoSpacesOverlayView() = default;

void MahoSpacesOverlayView::RequestFocus() {
  if (board_view_ && !board_view_->children().empty()) {
    board_view_->children().front()->RequestFocus();
    return;
  }
  views::View::RequestFocus();
}

void MahoSpacesOverlayView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }
  // The themed gradient is painted in OnPaintBackground(); repaint so it picks
  // up the new space-theme colors.
  SchedulePaint();
}

void MahoSpacesOverlayView::SetThemeSourceWidget(const views::Widget* widget) {
  theme_source_widget_ = widget;
  SchedulePaint();
}

void MahoSpacesOverlayView::OnPaintBackground(gfx::Canvas* canvas) {
  // Paint the same space-theme-tinted gradient as the docked sidebar, forced
  // opaque, so the Spaces overlay follows the theme instead of a flat color.
  // Opaque stops keep the overlay a valid occluder (web contents must not bleed
  // through) — identical to the fixed-width library overlay.
  //
  // Source the ColorProvider from the parent browser-frame widget: this
  // top-level frameless overlay's own provider lacks the per-space tint mixer
  // and would resolve an achromatic base (fall back to this view's provider
  // only during teardown/reparent when the parent is not yet set).
  const ui::ColorProvider* provider =
      theme_source_widget_ ? theme_source_widget_->GetColorProvider()
                           : GetColorProvider();
  if (!provider) {
    return;
  }
  const SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  // Anchor the gradient span to the library-overlay content width (380dp): the
  // narrow library overlay renders correctly with a full-bounds span at that
  // width, so reusing it bounds the full-viewport diagonal to the same tint
  // density instead of washing out across the ~2000px width (kClamp holds the
  // final stop beyond the span). Chosen as the sanctioned proxy for the
  // sidebar/rail width the docked gradient effectively paints across.
  PaintMahoSidebarThemedBackground(
      canvas, GetLocalBounds(), provider, /*opaque=*/true, radii,
      /*gradient_span_dp=*/sidebar_layout::kLibraryOverlayContentWidthDp);
}

bool MahoSpacesOverlayView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    DismissCancel();
    return true;
  }
  return views::View::AcceleratorPressed(accelerator);
}

void MahoSpacesOverlayView::DismissCancel() {
  if (cancel_callback_) {
    std::move(cancel_callback_).Run();
  }
}

void MahoSpacesOverlayView::DismissAction() {
  if (action_callback_) {
    std::move(action_callback_).Run();
  }
}

void MahoSpacesOverlayView::HandleSpaceSelected(const std::string& space_id) {
  if (MahoSidebarView::ActivateSpaceAndTab(browser_, space_id)) {
    DismissAction();
  }
}


void MahoSpacesOverlayView::HandleTabSelected(const std::string& space_id,
                                              const std::string& tab_id) {
  if (!browser_) {
    DismissAction();
    return;
  }
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    if (space_id != bridge->GetActiveSpaceId(browser_)) {
      bridge->SwitchToSpace(browser_, space_id);
    }
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->maho_sidebar_container()) {
    auto* sidebar_container = static_cast<MahoSidebarContainerView*>(
        browser_view->maho_sidebar_container());
    if (auto* sidebar = static_cast<MahoSidebarView*>(
            sidebar_container->sidebar_view())) {
      if (auto* tab_list = sidebar->tab_list_view()) {
        tab_list->ActivateTabById(tab_id);
      }
    }
  }

  DismissAction();
}

}  // namespace maho
