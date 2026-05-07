// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "base/time/time.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/display/screen.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"

namespace maho {

namespace {

constexpr base::TimeDelta kHoverRevealSuppressionDuration =
    base::Milliseconds(250);

void RelayoutParent(views::View* view) {
  if (auto* parent = view ? view->parent() : nullptr) {
    parent->InvalidateLayout();
    parent->DeprecatedLayoutImmediately();
  }
}

}  // namespace

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(MahoSidebarContainerView,
                                      kMahoSidebarContainerElementId);

BEGIN_METADATA(MahoSidebarContainerView)
END_METADATA

MahoSidebarContainerView::MahoSidebarContainerView(Browser* browser)
    : browser_(browser), reveal_animation_(this) {
  SetProperty(views::kElementIdentifierKey, kMahoSidebarContainerElementId);
  SetLayoutManager(std::make_unique<views::FillLayout>());
  SetNotifyEnterExitOnChild(true);

  reveal_animation_.SetSlideDuration(
      base::Milliseconds(sidebar_layout::kSidebarRevealDurationMs));
  reveal_animation_.SetTweenType(gfx::Tween::EASE_OUT);

  sidebar_view_ = AddChildView(std::make_unique<MahoSidebarView>(browser));

  PrefService* prefs = browser->profile()->GetPrefs();
  was_panel_expanded_ = sidebar_prefs::IsSidebarPanelExpanded(prefs);
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
  UpdateResizeHandleBounds();

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

void MahoSidebarContainerView::ShowCommandOverlayForCurrentTab(
    const std::string& current_url) {
  (void)current_url;
  if (command_overlay_controller_) {
    if (command_overlay_controller_->IsVisible()) {
      command_overlay_controller_->Hide();
      return;
    }
    command_overlay_controller_->Show(this);
  }
}

void MahoSidebarContainerView::ShowCommandOverlayForNewTab() {
  if (command_overlay_controller_) {
    if (command_overlay_controller_->IsVisible()) {
      command_overlay_controller_->Hide();
      return;
    }
    command_overlay_controller_->Show(this);
  }
}

void MahoSidebarContainerView::ShowCommandOverlayForSearch(
    const std::string& initial_query) {
  (void)initial_query;
  if (command_overlay_controller_) {
    if (command_overlay_controller_->IsVisible()) {
      command_overlay_controller_->Hide();
      return;
    }
    command_overlay_controller_->Show(this);
  }
}

void MahoSidebarContainerView::UpdateResizeHandleBounds() {
  if (!resize_handle_view_) {
    return;
  }
}

void MahoSidebarContainerView::OnPanelExpandedChanged() {
  ApplyPanelState();
}

void MahoSidebarContainerView::OnSidebarWidthChanged() {
  if (IsAutoHideMode()) {
    if (reveal_animation_.is_animating()) {
      AnimationProgressed(&reveal_animation_);
    } else if (!is_hover_revealed_) {
      return;
    } else {
      ApplyPreferredWidthFromPrefs();
      if (parent()) {
        parent()->InvalidateLayout();
      }
    }
    return;
  }

  ApplyPreferredWidthFromPrefs();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void MahoSidebarContainerView::ApplyPanelState() {
  PrefService* prefs = browser_->profile()->GetPrefs();
  const bool pinned = sidebar_prefs::IsSidebarPanelExpanded(prefs);
  const bool was_pinned = was_panel_expanded_;

  if (pinned) {
    reveal_animation_.Reset(1.0);
    is_hover_revealed_ = false;
    suppress_hover_reveal_ = false;
    hide_delay_timer_.Stop();
    hover_reveal_suppression_timer_.Stop();
    sidebar_view_->SetVisible(true);
    ApplyPreferredWidthFromPrefs();
  } else {
    if (was_pinned) {
      suppress_hover_reveal_ = true;
      hover_reveal_suppression_timer_.Start(
          FROM_HERE, kHoverRevealSuppressionDuration, this,
          &MahoSidebarContainerView::OnHoverRevealSuppressionExpired);
    }
    if (is_hover_revealed_ || reveal_animation_.is_animating()) {
      if (reveal_animation_.IsShowing()) {
        reveal_animation_.Hide();
      }
    } else {
      reveal_animation_.Reset(0.0);
      sidebar_view_->SetVisible(false);
      SetPreferredSize(
          gfx::Size(sidebar_layout::kHoverTriggerWidthDp, 0));
      PreferredSizeChanged();
    }
  }

  was_panel_expanded_ = pinned;

  UpdateResizeHandleBounds();
  RelayoutParent(this);

}

void MahoSidebarContainerView::ApplyPreferredWidthFromPrefs() {
  PrefService* prefs = browser_->profile()->GetPrefs();
  const int width = sidebar_prefs::GetSidebarWidthForLayout(
      prefs->GetInteger(sidebar_prefs::kSidebarWidth));
  SetPreferredSize(gfx::Size(width, 0));
  PreferredSizeChanged();
}

void MahoSidebarContainerView::MaybePersistSidebarWidth(int width) {
  if (width <= sidebar_layout::kHoverTriggerWidthDp) {
    return;
  }
  if (reveal_animation_.is_animating()) {
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

void MahoSidebarContainerView::OnMouseEntered(const ui::MouseEvent& event) {
  if (!IsAutoHideMode() || suppress_hover_reveal_)
    return;

  const views::Widget* const widget = GetWidget();
  display::Screen* const screen = display::Screen::Get();
  const bool cursor_inside =
      widget && screen &&
      GetBoundsInScreen().Contains(screen->GetCursorScreenPoint());


  // Always cancel pending hide — even if already revealed.
  hide_delay_timer_.Stop();

  // If close animation is in progress, only reverse it when the live cursor
  // is actually still inside the container.
  if (reveal_animation_.IsClosing()) {
    if (!widget || !screen || !cursor_inside) {
      return;
    }

    reveal_animation_.Show();
    hide_delay_timer_.Start(
        FROM_HERE,
        base::Milliseconds(sidebar_layout::kSidebarHideDelayMs),
        this,
        &MahoSidebarContainerView::OnHideDelayExpired);
    return;
  }

  // Already fully revealed — nothing to do.
  if (is_hover_revealed_) {
    hide_delay_timer_.Start(
        FROM_HERE,
        base::Milliseconds(sidebar_layout::kSidebarHideDelayMs),
        this,
        &MahoSidebarContainerView::OnHideDelayExpired);
    return;
  }

  StartReveal();
}

void MahoSidebarContainerView::OnMouseExited(const ui::MouseEvent& event) {
  if (!IsAutoHideMode() || !is_hover_revealed_)
    return;


  if (reveal_animation_.IsClosing())
    return;

  hide_delay_timer_.Start(
      FROM_HERE,
      base::Milliseconds(sidebar_layout::kSidebarHideDelayMs),
      this,
      &MahoSidebarContainerView::OnHideDelayExpired);
}

void MahoSidebarContainerView::StartReveal() {
  is_hover_revealed_ = true;
  sidebar_view_->SetVisible(true);
  reveal_animation_.Show();
  hide_delay_timer_.Start(
      FROM_HERE,
      base::Milliseconds(sidebar_layout::kSidebarHideDelayMs),
      this,
      &MahoSidebarContainerView::OnHideDelayExpired);
}

void MahoSidebarContainerView::StartHide() {
  reveal_animation_.Hide();
}

void MahoSidebarContainerView::OnHideDelayExpired() {
  if (IsAutoHideMode() && is_hover_revealed_) {
    if (IsCursorInsideContainerScreenBounds()) {
      hide_delay_timer_.Start(
          FROM_HERE,
          base::Milliseconds(sidebar_layout::kSidebarHideDelayMs),
          this,
          &MahoSidebarContainerView::OnHideDelayExpired);
      return;
    }
    StartHide();
  }
}

void MahoSidebarContainerView::OnHoverRevealSuppressionExpired() {
  suppress_hover_reveal_ = false;
  if (IsAutoHideMode() && !is_hover_revealed_ &&
      IsCursorInsideContainerScreenBounds()) {
    StartReveal();
  }
}

bool MahoSidebarContainerView::IsCursorInsideContainerScreenBounds() const {
  if (sidebar_view_ && sidebar_view_->GetVisible()) {
    return sidebar_view_->IsMouseHovered() || IsMouseHovered();
  }

  const views::Widget* const widget = GetWidget();
  if (!widget) {
    return false;
  }

  display::Screen* const screen = display::Screen::Get();
  if (!screen) {
    return false;
  }

  return GetBoundsInScreen().Contains(screen->GetCursorScreenPoint());
}

void MahoSidebarContainerView::AnimationProgressed(
    const gfx::Animation* animation) {
  PrefService* prefs = browser_->profile()->GetPrefs();
  const int full_width = sidebar_prefs::GetSidebarWidthForLayout(
      prefs->GetInteger(sidebar_prefs::kSidebarWidth));
  const double fraction = reveal_animation_.GetCurrentValue();
  const int trigger_width = sidebar_layout::kHoverTriggerWidthDp;
  const int animated_width = trigger_width +
      static_cast<int>((full_width - trigger_width) * fraction);

  SetPreferredSize(gfx::Size(animated_width, 0));
  PreferredSizeChanged();
  RelayoutParent(this);
}

void MahoSidebarContainerView::AnimationEnded(
    const gfx::Animation* animation) {
  if (IsAutoHideMode() && reveal_animation_.GetCurrentValue() == 0.0) {
    is_hover_revealed_ = false;
    sidebar_view_->SetVisible(false);
    SetPreferredSize(
        gfx::Size(sidebar_layout::kHoverTriggerWidthDp, 0));
    PreferredSizeChanged();
    RelayoutParent(this);
  }
}

bool MahoSidebarContainerView::IsAutoHideMode() const {
  PrefService* prefs = browser_->profile()->GetPrefs();
  return !sidebar_prefs::IsSidebarPanelExpanded(prefs);
}

double MahoSidebarContainerView::GetRevealFraction() const {
  if (!IsAutoHideMode())
    return 1.0;
  return reveal_animation_.GetCurrentValue();
}

bool MahoSidebarContainerView::IsOverlayVisible() const {
  return IsAutoHideMode() &&
         (is_hover_revealed_ || reveal_animation_.is_animating());
}

}  // namespace maho
