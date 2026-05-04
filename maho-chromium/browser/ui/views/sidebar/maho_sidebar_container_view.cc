// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"

#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"

namespace maho {

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(MahoSidebarContainerView,
                                      kMahoSidebarContainerElementId);

BEGIN_METADATA(MahoSidebarContainerView)
END_METADATA

MahoSidebarContainerView::MahoSidebarContainerView(Browser* browser)
    : browser_(browser) {
  SetProperty(views::kElementIdentifierKey, kMahoSidebarContainerElementId);
  SetLayoutManager(std::make_unique<views::FillLayout>());

  sidebar_view_ = AddChildView(std::make_unique<MahoSidebarView>(browser));

  auto reopen_button = views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(
          [](Browser* b, const ui::Event&) {
            sidebar_prefs::ToggleSidebarPanelExpanded(
                b->profile()->GetPrefs());
          },
          browser_),
      kDockToLeftIcon, sidebar_layout::kTopBarIconSizeDp,
      ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
      ui::kColorSysOnSurface);
  reopen_button->SetTooltipText(u"Expand Sidebar");
  reopen_button->SetAccessibleName(u"Expand Sidebar");
  rail_reopen_button_ = AddChildView(std::move(reopen_button));

  PrefService* prefs = browser->profile()->GetPrefs();
  pref_registrar_.Init(prefs);
  pref_registrar_.Add(
      sidebar_prefs::kSidebarPanelExpanded,
      base::BindRepeating(&MahoSidebarContainerView::OnPanelExpandedChanged,
                          weak_factory_.GetWeakPtr()));
  pref_registrar_.Add(
      sidebar_prefs::kSidebarWidth,
      base::BindRepeating(&MahoSidebarContainerView::OnSidebarWidthChanged,
                          weak_factory_.GetWeakPtr()));

  ApplyPanelState();

  command_overlay_controller_ =
      std::make_unique<MahoCommandOverlayController>(browser);
}

MahoSidebarContainerView::~MahoSidebarContainerView() = default;

void MahoSidebarContainerView::ShowCreateSpace() {
  auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_.get());
  if (sidebar) {
    sidebar->ShowCreateSpace();
  }
}

void MahoSidebarContainerView::ToggleCommandOverlay() {
  if (command_overlay_controller_) {
    command_overlay_controller_->Toggle(this);
  }
}

void MahoSidebarContainerView::OnPanelExpandedChanged() {
  ApplyPanelState();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void MahoSidebarContainerView::OnSidebarWidthChanged() {
  ApplyPreferredWidthFromPrefs();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void MahoSidebarContainerView::ApplyPanelState() {
  PrefService* prefs = browser_->profile()->GetPrefs();
  const bool expanded = sidebar_prefs::IsSidebarPanelExpanded(prefs);

  sidebar_view_->SetVisible(expanded);
  rail_reopen_button_->SetVisible(!expanded);

  if (expanded) {
    ApplyPreferredWidthFromPrefs();
  } else {
    SetPreferredSize(
        gfx::Size(sidebar_layout::kCollapsedRailWidthDp, 0));
    PreferredSizeChanged();
  }
}

void MahoSidebarContainerView::ApplyPreferredWidthFromPrefs() {
  PrefService* prefs = browser_->profile()->GetPrefs();
  const int width = sidebar_prefs::GetSidebarWidthForLayout(
      prefs->GetInteger(sidebar_prefs::kSidebarWidth));
  SetPreferredSize(gfx::Size(width, 0));
  PreferredSizeChanged();
}

void MahoSidebarContainerView::MaybePersistSidebarWidth(int width) {
  if (width <= sidebar_layout::kCollapsedRailWidthDp) {
    return;
  }

  PrefService* prefs = browser_->profile()->GetPrefs();
  const int clamped_width = sidebar_prefs::ClampSidebarWidthForLayout(width);
  if (prefs->GetInteger(sidebar_prefs::kSidebarWidth) == clamped_width) {
    return;
  }

  prefs->SetInteger(sidebar_prefs::kSidebarWidth, clamped_width);
}

void MahoSidebarContainerView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  if (bounds().width() != previous_bounds.width()) {
    MaybePersistSidebarWidth(bounds().width());
  }
}

bool MahoSidebarContainerView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  if (sidebar_view_ && sidebar_view_->GetVisible()) {
    gfx::Point point_in_sidebar = point;
    views::View::ConvertPointToTarget(this, sidebar_view_, &point_in_sidebar);
    if (sidebar_view_->HitTestPoint(point_in_sidebar)) {
      return static_cast<const MahoSidebarView*>(sidebar_view_.get())
          ->IsPositionInWindowCaption(point_in_sidebar);
    }
  }
  return false;
}

}  // namespace maho
