// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"

#include <algorithm>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "components/prefs/pref_service.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_controller.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"
#include "maho_sidebar_now_playing_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/display/screen.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_WIN)
#include <dwmapi.h>

#include "base/win/windows_version.h"
#include "ui/views/win/hwnd_util.h"
#endif

namespace maho {

namespace {

// Applies a translucent DWM Acrylic backdrop to the browser window on Windows
// 11 22H2+ so the alpha-encoded sidebar background reveals it (macOS gets
// passive NSWindow vibrancy). No-op elsewhere.
void ApplySidebarWindowVibrancy(views::Widget* widget) {
#if BUILDFLAG(IS_WIN)
  if (base::win::GetVersion() < base::win::Version::WIN11_22H2) {
    return;
  }
  if (!widget) {
    return;
  }
  HWND hwnd = views::HWNDForNativeWindow(widget->GetNativeWindow());
  if (!hwnd) {
    return;
  }
  static constexpr DWORD kBackdropTransientAcrylic = 3;
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                        &kBackdropTransientAcrylic,
                        sizeof(kBackdropTransientAcrylic));
#endif
}

constexpr int kResizeHandleWidthDp = 6;

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
  const MahoSidebarPalette& palette =
      static_cast<MahoSidebarView*>(sidebar_view_)->sidebar_palette();
  auto expand_button = views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(
          [](base::WeakPtr<MahoSidebarContainerView> container,
             const ui::Event&) {
            if (container) {
              container->TogglePanelExpanded();
            }
      },
      weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kPanelLeftIcon,
      sidebar_layout::kTopBarIconSizeDp, palette.neutral_glyph,
      palette.primary_text, palette.primary_text);
  expand_button->SetTooltipText(u"Expand Sidebar");
  expand_button->SetAccessibleName(u"Expand Sidebar");
  expand_button->GetViewAccessibility().AddAction(
      ax::mojom::Action::kDoDefault);
  expand_button->GetViewAccessibility().SetDefaultActionVerb(
      ax::mojom::DefaultActionVerb::kClick);
  expand_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  expand_button->SetRequestFocusOnPress(false);
  expand_button->SetPreferredSize(
      gfx::Size(sidebar_layout::kTopBarButtonSizeDp,
                sidebar_layout::kTopBarButtonSizeDp));
  expand_button->SetProperty(views::kViewIgnoredByLayoutKey, true);
  views::InstallCircleHighlightPathGenerator(expand_button.get());
  collapsed_expand_button_ = AddChildView(std::move(expand_button));
  resize_handle_view_ = AddChildView(std::make_unique<views::ResizeArea>(this));

  is_private_ = browser->profile()->IsIncognitoProfile() &&
                browser->profile()->IsPrimaryOTRProfile();
  private_sidebar_width_ = sidebar_layout::kDefaultRailWidthDp;
  if (is_private_) {
    was_panel_expanded_ = private_panel_expanded_;
  } else {
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
  }

  ApplyPanelState();
  UpdateResizeHandleBounds();

}

MahoSidebarContainerView::~MahoSidebarContainerView() = default;


void MahoSidebarContainerView::ShowCommandOverlayForCurrentTab() {
  if (browser_) {
    if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {
      browser_view->ShowMahoCommandOverlayForCurrentTab();
    }
  }
}

bool MahoSidebarContainerView::TryActivateFavoriteByIndex(size_t index) {
  auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
  return sidebar && sidebar->ActivateFavoriteByIndex(index);
}

void MahoSidebarContainerView::ShowCommandOverlayForNewTab() {
  if (browser_) {
    if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {
      browser_view->ShowMahoCommandOverlayForNewTab();
    }
  }
}

void MahoSidebarContainerView::ShowCommandOverlayForSearch(
    const std::string& initial_query) {
  if (browser_) {
    if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {
      browser_view->ShowMahoCommandOverlayForSearch(initial_query);
    }
  }
}

bool MahoSidebarContainerView::ShowAppMenu() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
  if (!sidebar || !sidebar->top_bar_view()) {
    return false;
  }
  return sidebar->top_bar_view()->ShowAppMenu();
#else
  return false;
#endif
}

void MahoSidebarContainerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplySidebarWindowVibrancy(GetWidget());
}

void MahoSidebarContainerView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  UpdateCollapsedExpandButtonBounds();
  UpdateResizeHandleBounds();
}

void MahoSidebarContainerView::UpdateCollapsedExpandButtonBounds() {
  if (!collapsed_expand_button_) {
    return;
  }
  const int button_size = sidebar_layout::kTopBarButtonSizeDp;
  const int x = std::max(0, (width() - button_size) / 2);
  const int y = sidebar_layout::kRailInsetTopDp +
                sidebar_layout::kTopBarTrafficLightAlignmentHeightDp +
                sidebar_layout::kSectionSpacingDp;
  collapsed_expand_button_->SetBounds(x, y, button_size, button_size);
}

void MahoSidebarContainerView::UpdateResizeHandleBounds() {
  if (!resize_handle_view_) {
    return;
  }
  const gfx::Rect bounds = GetContentsBounds();
  resize_handle_view_->SetBoundsRect(
      gfx::Rect(bounds.right() - kResizeHandleWidthDp, bounds.y(),
                 kResizeHandleWidthDp, bounds.height()));
  resize_handle_view_->SetVisible(IsResizeDragEnabled());
}

bool MahoSidebarContainerView::IsResizeDragEnabled() const {
  return !IsAutoHideMode();
}

bool MahoSidebarContainerView::IsResizeDragging() const {
  return is_resizing_;
}

void MahoSidebarContainerView::BeginResizeDrag(
    const ui::MouseEvent& event) {
  is_resizing_ = true;
  resize_drag_start_width_ = width();
}

void MahoSidebarContainerView::UpdateResizeDrag(
    const ui::MouseEvent& event) {}

void MahoSidebarContainerView::EndResizeDrag() {
  is_resizing_ = false;
  MaybePersistSidebarWidth(width());
}

void MahoSidebarContainerView::OnResize(int resize_amount,
                                         bool done_resizing) {
  if (resize_drag_start_width_ < 0) {
    resize_drag_start_width_ = width();
  }

  const int new_width = resize_drag_start_width_ + resize_amount;
  const int clamped = sidebar_prefs::ClampSidebarWidthForLayout(new_width);

  WriteSidebarWidth(clamped);

  if (done_resizing) {
    resize_drag_start_width_ = -1;
    is_resizing_ = false;
  } else {
    is_resizing_ = true;
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
    if (overlay_host_) {
      overlay_host_->UpdateLibraryOverlayBounds();
    }
    return;
  }

  ApplyPreferredWidthFromPrefs();
  if (parent()) {
    parent()->InvalidateLayout();
  }
  if (overlay_host_) {
    overlay_host_->UpdateLibraryOverlayBounds();
  }
}

void MahoSidebarContainerView::ApplyPanelState() {
  const bool pinned = ReadPanelExpanded();
  const bool was_pinned = was_panel_expanded_;

  collapsed_expand_button_->SetVisible(!pinned);

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
          gfx::Size(sidebar_layout::kCollapsedRailWidthDp, 0));
      PreferredSizeChanged();
    }
  }
  if (now_playing_view_) {
    now_playing_view_->UpdateVisibilityState();
  }

  was_panel_expanded_ = pinned;

  UpdateResizeHandleBounds();
  CheckAndNotifyAutoHide();
}

void MahoSidebarContainerView::ApplyPreferredWidthFromPrefs() {
  int width = sidebar_prefs::GetSidebarWidthForLayout(ReadSidebarWidth());
  if (sidebar_view_) {
    auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
    if (sidebar->IsInLibraryMode()) {
      const auto active = sidebar->GetActiveLibraryCategory();
      const bool overlay_flag = ReadSpacesFullViewport();
      if (active == MahoSidebarLibraryRailView::Category::kSpaces &&
          overlay_flag) {
        const int rail_width = GetLibraryRailWidth();
        if (rail_width > 0) {
          width = rail_width;
        }
      } else if (active == MahoSidebarLibraryRailView::Category::kSpaces) {
        width = std::max(width, 1400);
      } else if (active ==
                     MahoSidebarLibraryRailView::Category::kDownloads ||
                 active ==
                     MahoSidebarLibraryRailView::Category::kArchivedTabs) {
        const int rail_width = GetLibraryRailWidth();
        if (rail_width > 0) {
          width = rail_width;
        }
      }
    }
  }
  SetPreferredSize(gfx::Size(width, 0));
  PreferredSizeChanged();
}

void MahoSidebarContainerView::MaybePersistSidebarWidth(int width) {
  if (width <= sidebar_layout::kCollapsedRailWidthDp) {
    return;
  }
  if (reveal_animation_.is_animating()) {
    return;
  }

  const int clamped_width = sidebar_prefs::ClampSidebarWidthForLayout(width);
  WriteSidebarWidth(clamped_width);
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
  if (resize_handle_view_ && resize_handle_view_->GetVisible()) {
    gfx::Point point_in_handle = point;
    views::View::ConvertPointToTarget(this, resize_handle_view_,
                                      &point_in_handle);
    if (resize_handle_view_->HitTestPoint(point_in_handle)) {
      return false;
    }
  }

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
  const int full_width =
      sidebar_prefs::GetSidebarWidthForLayout(ReadSidebarWidth());
  const double fraction = reveal_animation_.GetCurrentValue();
  const int trigger_width = sidebar_layout::kCollapsedRailWidthDp;
  const int animated_width = trigger_width +
      static_cast<int>((full_width - trigger_width) * fraction);

  SetPreferredSize(gfx::Size(animated_width, 0));
  PreferredSizeChanged();
}

void MahoSidebarContainerView::AnimationEnded(
    const gfx::Animation* animation) {
  if (IsAutoHideMode() && reveal_animation_.GetCurrentValue() == 0.0) {
    is_hover_revealed_ = false;
    sidebar_view_->SetVisible(false);
    SetPreferredSize(
        gfx::Size(sidebar_layout::kCollapsedRailWidthDp, 0));
    PreferredSizeChanged();
    RelayoutParent(this);
    CheckAndNotifyAutoHide();
  }
}

bool MahoSidebarContainerView::IsAutoHideMode() const {
  return !ReadPanelExpanded();
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

void MahoSidebarContainerView::AddedToWidget() {
  views::View::AddedToWidget();
  ApplySidebarWindowVibrancy(GetWidget());
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoSidebarContainerView::AttachOverlayHost,
                     weak_factory_.GetWeakPtr()));
  if (GetWidget()) {
    if (GetWidget()->IsVisible()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoSidebarContainerView::CreateNowPlayingHostIfNeeded,
                         weak_factory_.GetWeakPtr()));
    } else {
      GetWidget()->AddObserver(this);
    }
  }
}

void MahoSidebarContainerView::RemovedFromWidget() {
  if (GetWidget()) {
    GetWidget()->RemoveObserver(this);
  }
}

void MahoSidebarContainerView::OnWidgetVisibilityChanged(views::Widget* widget, bool visible) {
  if (visible) {
    if (widget) {
      widget->RemoveObserver(this);
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoSidebarContainerView::CreateNowPlayingHostIfNeeded,
                       weak_factory_.GetWeakPtr()));
  }
}

void MahoSidebarContainerView::CreateNowPlayingHostIfNeeded() {
  if (now_playing_view_) {
    return;
  }
  auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
  if (!sidebar) return;
  auto host = std::make_unique<MahoSidebarNowPlayingView>(browser_);
  auto* host_ptr = host.get();
  now_playing_view_ = sidebar->AddNowPlayingView(std::move(host));
  auto* coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(browser_->profile());
  if (coordinator) {
    coordinator->RegisterHost(host_ptr);
  }
}

void MahoSidebarContainerView::AttachOverlayHost() {
  if (overlay_host_) {
    return;
  }
  if (auto* browser_view = views::AsViewClass<BrowserView>(parent())) {
    auto host = std::make_unique<MahoBrowserFrameOverlayHost>(browser_, this);
    overlay_host_ = browser_view->AddChildView(std::move(host));
  }
}

void MahoSidebarContainerView::ToggleSpacesOverlay() {
  if (overlay_host_) {
    overlay_host_->ToggleSpacesOverlay();
  }
}

void MahoSidebarContainerView::DismissSpacesOverlay() {
  if (overlay_host_) {
    overlay_host_->DismissSpacesOverlay();
  }
}

bool MahoSidebarContainerView::IsSpacesOverlayVisible() const {
  return overlay_host_ && overlay_host_->IsSpacesOverlayVisible();
}

void MahoSidebarContainerView::ToggleLibraryOverlay(
    MahoSidebarLibraryRailView::Category category) {
  AttachOverlayHost();
  if (overlay_host_) {
    overlay_host_->ToggleLibraryOverlay(category);
  }
}

void MahoSidebarContainerView::DismissLibraryOverlay() {
  if (overlay_host_) {
    overlay_host_->DismissLibraryOverlay();
  }
}

bool MahoSidebarContainerView::IsLibraryOverlayVisible() const {
  return overlay_host_ && overlay_host_->IsLibraryOverlayVisible();
}

int MahoSidebarContainerView::GetLibraryRailWidth() const {
  if (sidebar_view_) {
    auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
    if (sidebar->library_rail_view()) {
      return sidebar->library_rail_view()->GetPreferredSize().width();
    }
  }
  return 0;
}

gfx::Insets MahoSidebarContainerView::GetSidebarSlotInsets() const {
  if (IsFullyAutoHidden()) {
    return gfx::Insets();
  }
  int width = sidebar_prefs::GetSidebarWidthForLayout(ReadSidebarWidth());
  if (sidebar_view_) {
    auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_);
    if (sidebar->IsInLibraryMode()) {
      const auto active = sidebar->GetActiveLibraryCategory();
      const bool overlay_flag = ReadSpacesFullViewport();
      if (active == MahoSidebarLibraryRailView::Category::kSpaces &&
          overlay_flag) {
        const int rail_width = GetLibraryRailWidth();
        if (rail_width > 0) {
          width = rail_width;
        }
      } else if (active == MahoSidebarLibraryRailView::Category::kSpaces) {
        width = std::max(width, 1400);
      } else if (active ==
                     MahoSidebarLibraryRailView::Category::kDownloads ||
                 active ==
                     MahoSidebarLibraryRailView::Category::kArchivedTabs) {
        const int rail_width = GetLibraryRailWidth();
        if (rail_width > 0) {
          width = rail_width;
        }
      }
    }
  }
  return gfx::Insets::TLBR(0, width, 0, 0);
}

bool MahoSidebarContainerView::IsFullyAutoHidden() const {
  return IsAutoHideMode() && reveal_animation_.GetCurrentValue() == 0.0 && !is_hover_revealed_;
}

void MahoSidebarContainerView::CheckAndNotifyAutoHide() {
  if (IsFullyAutoHidden()) {
    if (overlay_host_) {
      overlay_host_->DismissSpacesOverlay();
      overlay_host_->DismissLibraryOverlay();
    }
  }
}

void MahoSidebarContainerView::SetPrivateAppearance(
    const ui::ImageModel& identity_icon,
    ui::ColorId surface_color_id,
    ui::ColorId text_color_id) {
  is_private_ = true;
  if (auto* sidebar = static_cast<MahoSidebarView*>(sidebar_view_)) {
    sidebar->SetPrivateAppearance(identity_icon, surface_color_id,
                                  text_color_id);
  }
}

bool MahoSidebarContainerView::ReadPanelExpanded() const {
  if (is_private_) {
    return private_panel_expanded_;
  }
  return sidebar_prefs::IsSidebarPanelExpanded(browser_->profile()->GetPrefs());
}

int MahoSidebarContainerView::ReadSidebarWidth() const {
  if (is_private_) {
    return private_sidebar_width_;
  }
  return browser_->profile()->GetPrefs()->GetInteger(
      sidebar_prefs::kSidebarWidth);
}

void MahoSidebarContainerView::WriteSidebarWidth(int clamped_width) {
  if (is_private_) {
    private_sidebar_width_ = clamped_width;
    return;
  }
  PrefService* prefs = browser_->profile()->GetPrefs();
  if (prefs->GetInteger(sidebar_prefs::kSidebarWidth) != clamped_width) {
    prefs->SetInteger(sidebar_prefs::kSidebarWidth, clamped_width);
  }
}

bool MahoSidebarContainerView::ReadSpacesFullViewport() const {
  if (is_private_) {
    return false;
  }
  return browser_->profile()->GetPrefs()->GetBoolean(
      sidebar_prefs::kSpacesFullViewport);
}

void MahoSidebarContainerView::TogglePanelExpanded() {
  if (is_private_) {
    private_panel_expanded_ = !private_panel_expanded_;
    ApplyPanelState();
    if (parent()) {
      parent()->InvalidateLayout();
    }
    return;
  }
  sidebar_prefs::ToggleSidebarPanelExpanded(browser_->profile()->GetPrefs());
}

}  // namespace maho
