// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/gfx/geometry/size.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_impl_macros.h"
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

  PrefService* prefs = browser->profile()->GetPrefs();
  pref_registrar_.Init(prefs);
  pref_registrar_.Add(
      sidebar_prefs::kSidebarVisible,
      base::BindRepeating(&MahoSidebarContainerView::OnSidebarVisibilityChanged,
                          weak_factory_.GetWeakPtr()));
  pref_registrar_.Add(
      sidebar_prefs::kSidebarWidth,
      base::BindRepeating(&MahoSidebarContainerView::OnSidebarWidthChanged,
                          weak_factory_.GetWeakPtr()));

  ApplyPreferredWidthFromPrefs();
  SetVisible(sidebar_prefs::ShouldShowSidebarInShell(prefs));

  command_overlay_controller_ =
      std::make_unique<MahoCommandOverlayController>(browser);
}

MahoSidebarContainerView::~MahoSidebarContainerView() = default;

void MahoSidebarContainerView::ShowCreateSpace() {
  auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
  if (sidebar) {
    sidebar->ShowCreateSpace();
  }
}

void MahoSidebarContainerView::ToggleCommandOverlay() {
  if (command_overlay_controller_) {
    command_overlay_controller_->Toggle(this);
  }
}

void MahoSidebarContainerView::OnSidebarVisibilityChanged() {
  PrefService* prefs = browser_->profile()->GetPrefs();
  bool visible = sidebar_prefs::ShouldShowSidebarInShell(prefs);
  SetVisible(visible);
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

void MahoSidebarContainerView::ApplyPreferredWidthFromPrefs() {
  PrefService* prefs = browser_->profile()->GetPrefs();
  const int width = sidebar_prefs::GetSidebarWidthForLayout(
      prefs->GetInteger(sidebar_prefs::kSidebarWidth));
  SetPreferredSize(gfx::Size(width, 0));
  PreferredSizeChanged();
}

void MahoSidebarContainerView::MaybePersistSidebarWidth(int width) {
  if (width <= 0) {
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

}  // namespace maho
