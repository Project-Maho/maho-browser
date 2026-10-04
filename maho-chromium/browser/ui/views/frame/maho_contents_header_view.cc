// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/frame/maho_contents_header_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/page_info/page_info_dialog.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "components/security_state/core/security_state.h"
#include "components/translate/core/browser/translate_download_manager.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/net/maho_translate_injection_handler.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"
#include "maho/browser/ui/views/sidebar/maho_translate_popover_view.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vector_icon_types.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kHeaderIconSizeDp = 16;
constexpr int kHeaderButtonSizeDp = 22;
constexpr int kHeaderHorizontalInsetDp = 6;
constexpr int kHeaderChildSpacingDp = 2;
constexpr int kHostLeadingGapDp = 4;
constexpr float kInactiveGlyphOpacity = 0.6f;
// Disabled back/forward glyphs are drawn at this opacity so an unavailable
// action reads as unavailable even where the palette's disabled role is close
// to its glyph role (the sidebar top bar's disabled nav buttons at HEAD used
// the disabled role alone, which was indistinguishable on pastel spaces).
constexpr float kDisabledGlyphOpacity = 0.4f;
// Forced colors drop every tint, so the active pane of a split carries a
// bottom hairline in the system focus colour instead.
constexpr int kForcedColorsActiveCueDp = 2;

constexpr char16_t kSearchPlaceholder[] = u"Search or Enter URL...";

constexpr int kTranslateOptionsChooseLanguageCommand = 41000;
constexpr int kTranslateOptionsNeverTranslateSiteCommand = 41001;
constexpr int kTranslateOptionsLanguageSettingsCommand = 41002;
constexpr int kTranslateOptionsLanguageCommandFirst = 41100;

// Same resolution order as Chromium's translate manager: auto-translate or
// recent target, then UI language, then accept languages.
std::string ResolveTranslateTargetLanguage(Profile* profile) {
  if (!profile) {
    return "en";
  }
  std::unique_ptr<translate::TranslatePrefs> prefs =
      ChromeTranslateClient::CreateTranslatePrefs(profile->GetPrefs());
  return translate::TranslateManager::GetTargetLanguage(
      prefs.get(), /*language_model=*/nullptr);
}

ui::ColorVariant WithAlpha(SkColor color, bool dim) {
  return ui::ColorVariant(
      dim ? SkColorSetA(color, static_cast<U8CPU>(SkColorGetA(color) *
                                                  kInactiveGlyphOpacity))
          : color);
}

ui::ColorVariant DisabledGlyph(SkColor color, bool inactive) {
  float opacity = kDisabledGlyphOpacity;
  if (inactive) {
    opacity *= kInactiveGlyphOpacity;
  }
  return ui::ColorVariant(SkColorSetA(
      color, static_cast<U8CPU>(SkColorGetA(color) * opacity)));
}

// views::Button only activates on Return when
// PlatformStyle::kReturnClicksFocusedControl is true, which upstream sets to
// !BUILDFLAG(IS_MAC) (ui/views/style/platform_style.h). host_button_ is the
// header's only keyboard entry point into editing the address (there is no
// separate omnibox in the Arc layout), so Enter must open the command
// overlay on every platform, including macOS. This subclass handles
// VKEY_RETURN (and the numpad Enter key) explicitly instead of relying on
// the platform's default key-click action.
class HeaderHostButton : public views::LabelButton {
 public:
  using views::LabelButton::LabelButton;

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_RETURN) {
      NotifyClick(event);
      return true;
    }
    return views::LabelButton::OnKeyPressed(event);
  }

  METADATA_HEADER(HeaderHostButton, views::LabelButton)
};

BEGIN_METADATA(HeaderHostButton)
END_METADATA

std::unique_ptr<views::ImageButton> CreateHeaderButton(
    views::Button::PressedCallback callback,
    const std::u16string& name) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetPreferredSize(gfx::Size(kHeaderButtonSizeDp, kHeaderButtonSizeDp));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetTooltipText(name);
  button->GetViewAccessibility().SetName(name);
  button->SetRequestFocusOnPress(false);
  views::InkDrop::Get(button.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  return button;
}

}  // namespace

MahoTranslateOptionsForTesting::MahoTranslateOptionsForTesting() = default;
MahoTranslateOptionsForTesting::MahoTranslateOptionsForTesting(
    MahoTranslateOptionsForTesting&&) = default;
MahoTranslateOptionsForTesting& MahoTranslateOptionsForTesting::operator=(
    MahoTranslateOptionsForTesting&&) = default;
MahoTranslateOptionsForTesting::~MahoTranslateOptionsForTesting() = default;

BEGIN_METADATA(MahoContentsHeaderView)
END_METADATA

MahoContentsHeaderView::MahoContentsHeaderView(BrowserView* browser_view,
                                               ContentsWebView* web_view)
    : browser_view_(browser_view), web_view_tracker_(web_view) {
  // AccessiblePaneView: a toolbar-like keyboard pane. F6 / Ctrl+Back/Forward
  // traversal reaches it through ContentsContainerView::GetAccessiblePanes()
  // (maho-chromium/build/scripts/split_view_replacements.py), and Escape
  // restores focus to whatever had it before the pane was entered.
  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(u"Page controls");

  layout_ = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kHeaderHorizontalInsetDp), kHeaderChildSpacingDp));
  auto* layout = layout_.get();
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  back_button_ = AddChildView(CreateHeaderButton(
      base::BindRepeating(&MahoContentsHeaderView::OnBackPressed,
                          base::Unretained(this)),
      u"Back"));
  forward_button_ = AddChildView(CreateHeaderButton(
      base::BindRepeating(&MahoContentsHeaderView::OnForwardPressed,
                          base::Unretained(this)),
      u"Forward"));
  reload_button_ = AddChildView(CreateHeaderButton(
      base::BindRepeating(&MahoContentsHeaderView::OnReloadPressed,
                          base::Unretained(this)),
      u"Reload"));

  // The security glyph is the header's page info entry point (the Arc layout
  // has no omnibox location icon), and the page info bubble anchors to it.
  security_icon_ = AddChildView(CreateHeaderButton(
      base::BindRepeating(&MahoContentsHeaderView::OnSecurityPressed,
                          base::Unretained(this)),
      std::u16string()));
  security_icon_->SetProperty(views::kMarginsKey,
                              gfx::Insets::TLBR(0, kHostLeadingGapDp, 0, 0));
  security_icon_->SetVisible(false);

  host_button_ = AddChildView(std::make_unique<HeaderHostButton>(
      base::BindRepeating(&MahoContentsHeaderView::OnHostPressed,
                          base::Unretained(this)),
      std::u16string()));
  host_button_->SetLabelStyle(views::style::STYLE_BODY_4);
  host_button_->SetElideBehavior(gfx::ELIDE_HEAD);
  host_button_->SetRequestFocusOnPress(false);
  host_button_->SetBorder(nullptr);
  views::InkDrop::Get(host_button_)
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  layout->SetFlexForView(host_button_, 1);

  translate_label_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&MahoContentsHeaderView::OnTranslatePressed,
                          base::Unretained(this)),
      std::u16string()));
  translate_label_->SetLabelStyle(views::style::STYLE_BODY_4);
  translate_label_->SetRequestFocusOnPress(false);
  translate_label_->SetBorder(nullptr);
  views::InkDrop::Get(translate_label_)
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  translate_label_->SetVisible(false);

  utility_icon_ = AddChildView(
      std::make_unique<MahoLocationBarUtilityIconView>(base::BindRepeating(
          &MahoContentsHeaderView::ToggleUtilityPanel,
          base::Unretained(this))));
  utility_icon_->SetPreferredSize(
      gfx::Size(kHeaderButtonSizeDp, kHeaderButtonSizeDp));
  utility_icon_->SetAffordanceVisible(true);
  utility_icon_->SetTooltipText(u"Page tools");
  utility_icon_->GetViewAccessibility().SetName(u"Page tools");

  copy_url_button_ = AddChildView(CreateHeaderButton(
      base::BindRepeating(&MahoContentsHeaderView::OnCopyUrlPressed,
                          base::Unretained(this)),
      u"Copy URL"));

  utility_bubble_coordinator_ =
      std::make_unique<MahoLocationBarUtilityBubbleCoordinator>();
  if (Browser* browser = GetBrowser()) {
    utility_bubble_coordinator_->AttachToBrowser(browser);
  }
  utility_panel_close_subscription_ =
      utility_bubble_coordinator_->RegisterOnCloseCallback(base::BindRepeating(
          &MahoContentsHeaderView::OnUtilityPanelClosed,
          weak_factory_.GetWeakPtr()));

  if (web_view) {
    web_contents_attached_subscription_ =
        web_view->AddWebContentsAttachedCallback(base::BindRepeating(
            &MahoContentsHeaderView::OnWebContentsAttached,
            base::Unretained(this)));
    web_contents_detached_subscription_ =
        web_view->AddWebContentsDetachedCallback(base::BindRepeating(
            &MahoContentsHeaderView::OnWebContentsDetached,
            base::Unretained(this)));
    Observe(web_view->web_contents());
  }

  if (Browser* browser = GetBrowser()) {
    sidebar_layout_pref_registrar_.Init(browser->GetProfile()->GetPrefs());
    sidebar_layout_pref_registrar_.Add(
        sidebar_prefs::kSidebarLayoutEnabled,
        base::BindRepeating(&MahoContentsHeaderView::UpdateVisibility,
                            base::Unretained(this)));
  }

  UpdateTranslateElementId();
  UpdateState();
}

MahoContentsHeaderView::~MahoContentsHeaderView() {
  StopObservingFullscreen();
  CloseUtilityPanel();
  CloseTranslatePopover();
  if (page_info_widget_) {
    page_info_widget_->CloseNow();
  }
}

// static
std::u16string MahoContentsHeaderView::BuildHostText(const GURL& url) {
  if (url.is_valid() && !url.host().empty()) {
    std::string host(url.host());
    if (host.starts_with("www.")) {
      host.erase(0, 4);
    }
    return base::UTF8ToUTF16(host);
  }
  return kSearchPlaceholder;
}

// static
std::u16string MahoContentsHeaderView::BuildTranslateText(bool translated,
                                                          bool available) {
  if (translated) {
    return u"Translated";
  }
  if (available) {
    return u"Translation Available";
  }
  return std::u16string();
}

// static
bool MahoContentsHeaderView::ShouldShowSecurityIcon(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

void MahoContentsHeaderView::SetActive(bool active) {
  if (active_ == active) {
    // Split entry/exit re-sends the same state; the forced-colors active cue
    // depends on the split state, so repaint anyway.
    SchedulePaint();
    return;
  }
  active_ = active;
  UpdateTranslateElementId();
  UpdateChrome();
  SchedulePaint();
}

void MahoContentsHeaderView::UpdateTranslateElementId() {
  // The Arc-layout browser has no upstream translate page action, so the
  // translate bubble anchors to kTranslatePageActionElementId here. The
  // bubble resolves it with ElementTracker::GetUniqueElement, which requires
  // exactly one shown holder, so only the active pane carries it.
  if (active_) {
    translate_label_->SetProperty(views::kElementIdentifierKey,
                                  kTranslatePageActionElementId);
  } else {
    translate_label_->ClearProperty(views::kElementIdentifierKey);
  }
}

void MahoContentsHeaderView::SetPalette(const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (utility_icon_) {
    utility_icon_->SetSidebarPalette(palette_);
  }
  UpdateChrome();
  SchedulePaint();
}

views::View* MahoContentsHeaderView::translate_anchor() {
  if (translate_label_->GetVisible() && translate_label_->IsDrawn()) {
    return translate_label_;
  }
  return utility_icon_;
}

views::View* MahoContentsHeaderView::utility_anchor() {
  return utility_icon_;
}

views::View* MahoContentsHeaderView::security_anchor() {
  return security_icon_;
}

void MahoContentsHeaderView::FocusHostButton() {
  // host_button_ defaults to FocusBehavior::ACCESSIBLE_ONLY on macOS (see
  // ui/views/style/platform_style.h), so a plain RequestFocus() is a no-op
  // there unless the FocusManager is already in keyboard-accessible mode.
  // SetPaneFocus() (AccessiblePaneView, this header's own base class) sets
  // the focused view directly through the FocusManager, so it works
  // regardless of the button's FocusBehavior or platform.
  if (host_button_ && host_button_->GetVisible() && host_button_->GetEnabled()) {
    SetPaneFocus(host_button_);
  }
}

gfx::Size MahoContentsHeaderView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width =
      views::View::CalculatePreferredSize(available_size).width();
  return gfx::Size(width, kMahoContentsHeaderHeightDp);
}

void MahoContentsHeaderView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateChrome();
}

void MahoContentsHeaderView::OnPaintBackground(gfx::Canvas* canvas) {
  if (palette_.primary_text == SK_ColorTRANSPARENT) {
    // No sidebar palette (unit tests, non-Arc windows): native background.
    views::View::OnPaintBackground(canvas);
    return;
  }
  // Paint the owning sidebar's surface so the header text roles (resolved
  // against these opaque stops) keep their contrast. The gradient uses the same
  // span as the docked rail and keeps the browser-root anchor, so the diagonal
  // is continuous across the rail/content boundary: at the boundary both
  // surfaces evaluate the same diagonal position, and below the span both
  // saturate to the final stop together. A per-surface span instead restarts (or
  // stretches) the stops here and the strip reads as a different shade at the
  // same height.
  const gfx::Rect bounds = GetLocalBounds();
  gfx::Rect gradient_bounds = bounds;
  if (views::Widget* widget = GetWidget(); widget && widget->GetRootView()) {
    gradient_bounds = views::View::ConvertRectToTarget(
        widget->GetRootView(), this, widget->GetRootView()->GetLocalBounds());
  }
  const SkVector square_radii[4] = {};
  PaintMahoSidebarThemedBackground(
      canvas, palette_, bounds, /*opaque=*/true, square_radii,
      sidebar_layout::kThemedBackgroundGradientSpanDp, &gradient_bounds);
  if (!active_) {
    // Inactive split panes recede under the sidebar's selected-row tint; the
    // palette's text roles are contrast-resolved against that surface too.
    canvas->FillRect(bounds, palette_.row_selected);
  }
  if (palette_.forced_colors && active_ && IsInSplit()) {
    // Forced colors resolve row_selected to transparent and primary text to
    // secondary, so the tint above and the text swap vanish. Mark the active
    // pane with a system focus-colour hairline, a cue that is not a tint.
    canvas->FillRect(gfx::Rect(bounds.x(),
                               bounds.bottom() - kForcedColorsActiveCueDp,
                               bounds.width(), kForcedColorsActiveCueDp),
                     palette_.focus_ring);
  }
}

bool MahoContentsHeaderView::IsInSplit() const {
  if (in_split_for_testing_) {
    return *in_split_for_testing_;
  }
  MultiContentsView* multi_contents =
      browser_view_ ? browser_view_->multi_contents_view() : nullptr;
  return multi_contents && multi_contents->IsInSplitView();
}

void MahoContentsHeaderView::Layout(PassKey) {
  // Keep every child clear of the native window controls (macOS traffic
  // lights on the leading edge, Windows/Linux caption buttons on the trailing
  // edge) when the header sits under them, e.g. with the sidebar collapsed.
  // Each cluster of controls (Linux can put buttons on both edges) is
  // assigned a side from where it actually sits, so RTL and platform
  // placement both follow the real geometry. No measurable controls leaves a
  // zero extra inset.
  int leading = kHeaderHorizontalInsetDp;
  int trailing = kHeaderHorizontalInsetDp;
  for (const gfx::Rect& controls : GetWindowControlsRectsInView(this)) {
    gfx::Rect overlap = controls;
    overlap.Intersect(GetLocalBounds());
    if (!overlap.IsEmpty()) {
      if (overlap.CenterPoint().x() < width() / 2) {
        leading = std::max(leading, overlap.right() + kHeaderHorizontalInsetDp);
      } else {
        trailing = std::max(
            trailing, width() - overlap.x() + kHeaderHorizontalInsetDp);
      }
    }
  }
  // The insets only change when the window controls move relative to the
  // header, so this converges after at most one extra layout pass.
  layout_->set_inside_border_insets(gfx::Insets::TLBR(0, leading, 0, trailing));
  LayoutSuperclass<views::View>(this);
}

void MahoContentsHeaderView::AddedToWidget() {
  views::View::AddedToWidget();
  MaybeSubscribeToSidebarPalette();
  if (!MaybeObserveFullscreen() && !observed_fullscreen_controller_ &&
      GetBrowser()) {
    // BrowserView (and therefore this header) joins its widget before
    // Browser::InitPostWindowConstruction creates the exclusive access
    // manager; retry once that synchronous init has finished.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<MahoContentsHeaderView> header) {
                         if (header && header->MaybeObserveFullscreen()) {
                           header->UpdateVisibility();
                         }
                       },
                       weak_factory_.GetWeakPtr()));
  }
  UpdateVisibility();
}

void MahoContentsHeaderView::RemovedFromWidget() {
  StopObservingFullscreen();
  sidebar_palette_subscription_ = {};
  views::View::RemovedFromWidget();
}

void MahoContentsHeaderView::OnFullscreenStateChanged() {
  UpdateVisibility();
}

void MahoContentsHeaderView::MaybeSubscribeToSidebarPalette() {
  if (sidebar_palette_subscription_) {
    return;
  }
  MahoSidebarContainerView* container = GetSidebarContainer();
  auto* sidebar =
      container ? views::AsViewClass<MahoSidebarView>(container->sidebar_view())
                : nullptr;
  if (!sidebar) {
    // No sidebar: keep the default (transparent) palette, which UpdateChrome()
    // maps to the native theme colors.
    return;
  }
  sidebar_palette_subscription_ = sidebar->AddSidebarPaletteChangedCallback(
      base::BindRepeating(&MahoContentsHeaderView::SetPalette,
                          base::Unretained(this)));
  SetPalette(sidebar->sidebar_palette());
}

bool MahoContentsHeaderView::MaybeObserveFullscreen() {
  if (fullscreen_subscription_) {
    return false;
  }
  Browser* browser = GetBrowser();
  ExclusiveAccessManager* manager =
      browser ? browser->GetFeatures().exclusive_access_manager() : nullptr;
  FullscreenController* controller =
      manager ? manager->fullscreen_controller() : nullptr;
  if (!controller) {
    return false;
  }
  fullscreen_subscription_ = controller->RegisterOnFullscreenStateChanged(
      base::BindRepeating(&MahoContentsHeaderView::OnFullscreenStateChanged,
                          base::Unretained(this)));
  observed_fullscreen_controller_ = controller->GetWeakPtr();
  return true;
}

void MahoContentsHeaderView::StopObservingFullscreen() {
  fullscreen_subscription_ = {};
  observed_fullscreen_controller_ = nullptr;
}

void MahoContentsHeaderView::UpdateVisibility() {
  if (!browser_view_) {
    // No BrowserView (unit tests): no browser-type or fullscreen policy.
    return;
  }
  // Arc-layout gate: popup, app and devtools windows, and normal windows
  // with the sidebar-layout pref off (stock top chrome is shown there), get
  // no header. Sidebar collapse is not an input, so the header and the
  // contents offset stay put when the sidebar collapses (IS-3).
  bool visible = IsArcLayoutActive();
  if (visible && observed_fullscreen_controller_) {
    visible = !(observed_fullscreen_controller_->IsFullscreenForBrowser() ||
                observed_fullscreen_controller_->IsTabFullscreen());
  }
  if (GetVisible() == visible) {
    return;
  }
  SetVisible(visible);
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void MahoContentsHeaderView::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame()) {
    UpdateState();
  }
}

void MahoContentsHeaderView::DidStartLoading() {
  is_loading_ = true;
  UpdateNavigationButtons();
}

void MahoContentsHeaderView::DidStopLoading() {
  is_loading_ = false;
  UpdateState();
}

void MahoContentsHeaderView::DidChangeVisibleSecurityState() {
  UpdateSecurityIcon();
  UpdateChrome();
}

void MahoContentsHeaderView::WebContentsDestroyed() {
  Observe(nullptr);
  UpdateState();
}

void MahoContentsHeaderView::ExecuteCommand(int command_id, int event_flags) {
  if (command_id >= kTranslateOptionsLanguageCommandFirst) {
    const size_t language_index = static_cast<size_t>(
        command_id - kTranslateOptionsLanguageCommandFirst);
    if (language_index < translate_option_language_codes_.size()) {
      OnTranslateLanguageSelected(
          translate_option_language_codes_[language_index]);
    }
    return;
  }
  switch (command_id) {
    case kTranslateOptionsNeverTranslateSiteCommand:
      OnNeverTranslateThisSite();
      return;
    case kTranslateOptionsLanguageSettingsCommand:
      OnOpenTranslateLanguageSettings();
      return;
    default:
      return;
  }
}

bool MahoContentsHeaderView::IsCommandIdChecked(int command_id) const {
  if (command_id < kTranslateOptionsLanguageCommandFirst) {
    return false;
  }
  const size_t language_index =
      static_cast<size_t>(command_id - kTranslateOptionsLanguageCommandFirst);
  return language_index < translate_option_language_codes_.size() &&
         translate_option_language_codes_[language_index] ==
             ResolveTranslateTargetLanguage(GetProfile());
}

bool MahoContentsHeaderView::IsArcLayoutActive() const {
  // Mirrors BrowserView::IsMahoArcLayoutActive() rather than delegating to
  // it, because that method does not exist uniformly across Maho's pinned
  // Chromium revisions: at ee4bd9e9 (the local Mac pin) BrowserView has no
  // IsMahoArcLayoutActive() at all (build/scripts/apply_chromium_src_overrides.py
  // notes its Arc gate reads the sidebar-layout pref directly there); at
  // 72f18f12 (the omarchy/Linux pin) it exists but LOG(WARNING)s on every
  // call, which this header's per-layout-pass visibility check would spam.
  // This copy keeps the header's frequent checks both portable and silent.
  Browser* browser = GetBrowser();
  return browser && (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) &&
         sidebar_prefs::IsSidebarLayoutEnabled(browser->GetProfile()->GetPrefs());
}

Browser* MahoContentsHeaderView::GetBrowser() const {
  // Desktop BrowserViews are created by BrowserWindow::CreateBrowserWindow
  // for the Browser owned by BrowserManagerService. This does not take ownership.
  return browser_view_ ? static_cast<Browser*>(browser_view_->browser()) : nullptr;
}

MahoSidebarContainerView* MahoContentsHeaderView::GetSidebarContainer() const {
  if (!browser_view_) {
    return nullptr;
  }
  for (views::View* child : browser_view_->children()) {
    if (auto* container = views::AsViewClass<MahoSidebarContainerView>(child)) {
      return container;
    }
  }
  return nullptr;
}

Profile* MahoContentsHeaderView::GetProfile() const {
  if (web_contents()) {
    return Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  }
  Browser* browser = GetBrowser();
  return browser ? browser->GetProfile() : nullptr;
}

GURL MahoContentsHeaderView::GetCurrentUrl() const {
  if (!web_contents()) {
    return GURL();
  }
  const GURL& visible = web_contents()->GetVisibleURL();
  return visible.is_empty() ? web_contents()->GetLastCommittedURL() : visible;
}

void MahoContentsHeaderView::OnWebContentsAttached(views::WebView* web_view) {
  CloseUtilityPanel();
  CloseTranslatePopover();
  Observe(web_view->web_contents());
  is_loading_ = web_contents() && web_contents()->IsLoading();
  UpdateState();
}

void MahoContentsHeaderView::OnWebContentsDetached(views::WebView* web_view) {
  CloseUtilityPanel();
  CloseTranslatePopover();
  Observe(nullptr);
  is_loading_ = false;
  UpdateState();
}

void MahoContentsHeaderView::OnBackPressed(const ui::Event& event) {
  if (web_contents() && web_contents()->GetController().CanGoBack()) {
    web_contents()->GetController().GoBack();
  }
}

void MahoContentsHeaderView::OnForwardPressed(const ui::Event& event) {
  if (web_contents() && web_contents()->GetController().CanGoForward()) {
    web_contents()->GetController().GoForward();
  }
}

void MahoContentsHeaderView::OnReloadPressed(const ui::Event& event) {
  if (!web_contents()) {
    return;
  }
  if (is_loading_) {
    web_contents()->Stop();
    return;
  }
  web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                         /*check_for_repost=*/true);
}

void MahoContentsHeaderView::ActivatePaneTab() {
  Browser* browser = GetBrowser();
  if (!browser || !web_contents()) {
    return;
  }
  TabStripModel* tab_strip = browser->GetTabStripModel();
  const int index = tab_strip->GetIndexOfWebContents(web_contents());
  if (index == TabStripModel::kNoTab || index == tab_strip->active_index()) {
    // Already the active pane: leave focus alone, so the single-pane case
    // (and a click on the already-active split pane) keeps whatever view had
    // focus as the command overlay's restore target.
    return;
  }
  tab_strip->ActivateTabAt(index);
  // Focus rule: focus follows the pane only when this call changed the
  // active tab. The header buttons do not take focus on press, and upstream
  // BrowserView::UpdateActiveTabInSplitView() only moves focus when a
  // non-active pane held it, so without this the previously active pane's
  // web view keeps focus. Any later focus restore onto that web view (the
  // command overlay's RestoreFocusToPreShowView() on dismiss) then runs
  // MultiContentsView::OnWebContentsFocused(), which re-activates the old
  // pane, and a CURRENT_TAB navigation lands there (N2). The pane is active
  // by now, so focusing its web view does not re-enter that path.
  if (views::View* web_view = web_view_tracker_.view()) {
    web_view->RequestFocus();
  }
}

void MahoContentsHeaderView::OnHostPressed(const ui::Event& event) {
  ActivatePaneTab();
  if (MahoSidebarContainerView* container = GetSidebarContainer()) {
    container->ShowCommandOverlayForCurrentTab();
  }
}

void MahoContentsHeaderView::OnSecurityPressed(const ui::Event& event) {
  if (!web_contents()) {
    return;
  }
  if (page_info_widget_) {
    // A second press on the icon dismisses the open bubble.
    page_info_widget_->Close();
    return;
  }
  // Page info reads and acts on this pane's tab, so bring it forward.
  ActivatePaneTab();
  if (page_info_opener_for_testing_) {
    page_info_opener_for_testing_.Run(web_contents(), security_icon_);
    return;
  }
  content::NavigationEntry* entry =
      web_contents()->GetController().GetVisibleEntry();
  if (!entry || entry->IsInitialEntry() || !GetWidget()) {
    return;
  }
  // Built directly instead of through ShowPageInfoDialog(), whose anchor
  // resolution (bubble_anchor_util::GetPageInfoAnchorConfiguration) targets
  // the stock location bar / app menu, which the Arc layout hides. The bubble
  // anchors to this pane's security icon, so each split pane opens its own.
  PageInfoBubbleSpecification::Builder builder(
      views::BubbleAnchor(security_icon_.get()), GetWidget()->GetNativeWindow(),
      web_contents(), entry->GetVirtualURL());
  if (GetPageInfoDialogCreatedCallbackForTesting()) {
    builder.AddInitializedCallback(
        std::move(GetPageInfoDialogCreatedCallbackForTesting()));
  }
  views::BubbleDialogDelegateView* bubble =
      PageInfoBubbleView::CreatePageInfoBubble(builder.Build());
  // The icon is a Button anchor, so the bubble highlights it while shown.
  bubble->SetArrow(views::BubbleBorder::TOP_LEFT);
  bubble->GetWidget()->Show();
  page_info_widget_ = bubble->GetWidget()->GetWeakPtr();
}

void MahoContentsHeaderView::OnCopyUrlPressed(const ui::Event& event) {
  const GURL url = GetCurrentUrl();
  if (!url.is_valid()) {
    return;
  }
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::UTF8ToUTF16(url.spec()));
  ShowLinkCopiedToast(GetBrowser());
}

void MahoContentsHeaderView::OnTranslatePressed(const ui::Event& event) {
  if (!web_contents()) {
    return;
  }
  // Translated pages restore the original directly, with no popover.
  if (maho::IsMahoPageTranslated(web_contents())) {
    OnTranslatePopoverTranslate();
    return;
  }
  views::Widget* popover_widget = MahoTranslatePopoverView::Show(
      translate_label_, ResolveTranslateTargetDisplayName(),
      base::BindRepeating(&MahoContentsHeaderView::OnTranslatePopoverTranslate,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&MahoContentsHeaderView::OnTranslatePopoverOptions,
                          weak_factory_.GetWeakPtr()));
  translate_popover_widget_ =
      popover_widget ? popover_widget->GetWeakPtr() : nullptr;
}

std::u16string MahoContentsHeaderView::ResolveTranslateTargetDisplayName()
    const {
  return l10n_util::GetDisplayNameForLocale(
      ResolveTranslateTargetLanguage(GetProfile()),
      g_browser_process->GetApplicationLocale(), /*is_for_ui=*/true);
}

void MahoContentsHeaderView::OnTranslatePopoverTranslate() {
  if (!web_contents()) {
    return;
  }
  web_contents()->ClearFocusedElement();
  maho::TranslatePageViaOnDevice(web_contents(),
                                 ResolveTranslateTargetLanguage(GetProfile()));
  UpdateTranslateLabel();
  UpdateChrome();
}

void MahoContentsHeaderView::OnTranslatePopoverOptions(
    views::View* anchor,
    base::RepeatingClosure close_popover) {
  if (!anchor || !anchor->GetWidget() || !GetProfile()) {
    return;
  }
  OnTranslateOptionsMenuClosed();
  close_translate_popover_ = std::move(close_popover);
  BuildTranslateOptionsMenuModels();
  translate_options_menu_runner_ = std::make_unique<views::MenuRunner>(
      translate_options_menu_model_.get(), views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&MahoContentsHeaderView::OnTranslateOptionsMenuClosed,
                          weak_factory_.GetWeakPtr()));
  translate_options_menu_runner_->RunMenuAt(
      anchor->GetWidget(), nullptr, anchor->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopRight, ui::mojom::MenuSourceType::kMouse);
}

MahoTranslateOptionsForTesting
MahoContentsHeaderView::PrepareTranslateOptionsForTesting() {
  OnTranslateOptionsMenuClosed();
  BuildTranslateOptionsMenuModels();

  MahoTranslateOptionsForTesting result;
  for (size_t index = 0; index < translate_options_menu_model_->GetItemCount();
       ++index) {
    result.labels.push_back(translate_options_menu_model_->GetLabelAt(index));
  }
  if (!translate_option_language_codes_.empty()) {
    result.first_language_command_id =
        translate_languages_menu_model_->GetCommandIdAt(0);
    result.first_language_code = translate_option_language_codes_.front();
  }
  result.never_translate_command_id =
      translate_options_menu_model_->GetCommandIdAt(1);
  result.language_settings_command_id =
      translate_options_menu_model_->GetCommandIdAt(2);
  return result;
}

void MahoContentsHeaderView::BuildTranslateOptionsMenuModels() {
  Profile* profile = GetProfile();
  DCHECK(profile);

  translate_languages_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  std::vector<std::string> supported_languages;
  translate::TranslateDownloadManager::GetSupportedLanguages(
      true, &supported_languages);
  const std::string& app_locale = g_browser_process->GetApplicationLocale();

  // Pinned group: the current target, then the user's preferred languages.
  std::vector<std::string> pinned_codes;
  auto contains = [](const std::vector<std::string>& codes,
                     const std::string& code) {
    return std::ranges::find(codes, code) != codes.end();
  };
  auto pin = [&](const std::string& code) {
    if (contains(supported_languages, code) && !contains(pinned_codes, code)) {
      pinned_codes.push_back(code);
    }
  };
  pin(ResolveTranslateTargetLanguage(profile));
  const auto preferred_languages =
      ChromeTranslateClient::CreateTranslatePrefs(profile->GetPrefs())
          ->GetLanguageList();
  for (const auto& language : preferred_languages) {
    pin(translate::TranslateDownloadManager::GetLanguageCode(
        language.tag_string()));
  }

  // Remaining languages sorted by display name, not by code.
  std::vector<std::pair<std::u16string, std::string>> other_languages;
  for (const std::string& language_code : supported_languages) {
    if (contains(pinned_codes, language_code)) {
      continue;
    }
    std::u16string display_name =
        l10n_util::GetDisplayNameForLocale(language_code, app_locale, true);
    if (!display_name.empty()) {
      other_languages.emplace_back(std::move(display_name), language_code);
    }
  }
  std::ranges::sort(other_languages);

  auto add_language = [&](const std::u16string& display_name,
                          const std::string& language_code) {
    const int command_id =
        kTranslateOptionsLanguageCommandFirst +
        static_cast<int>(translate_option_language_codes_.size());
    translate_languages_menu_model_->AddCheckItem(command_id, display_name);
    translate_option_language_codes_.push_back(language_code);
  };
  for (const std::string& language_code : pinned_codes) {
    const std::u16string display_name =
        l10n_util::GetDisplayNameForLocale(language_code, app_locale, true);
    if (!display_name.empty()) {
      add_language(display_name, language_code);
    }
  }
  if (!translate_option_language_codes_.empty() && !other_languages.empty()) {
    translate_languages_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  for (const auto& [display_name, language_code] : other_languages) {
    add_language(display_name, language_code);
  }

  translate_options_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  translate_options_menu_model_->AddSubMenu(
      kTranslateOptionsChooseLanguageCommand, u"Choose Another Language",
      translate_languages_menu_model_.get());
  translate_options_menu_model_->AddItem(
      kTranslateOptionsNeverTranslateSiteCommand, u"Never Translate This Site");
  translate_options_menu_model_->AddItem(
      kTranslateOptionsLanguageSettingsCommand, u"Language Settings");
}

void MahoContentsHeaderView::OnTranslateOptionsMenuClosed() {
  translate_options_menu_runner_.reset();
  translate_options_menu_model_.reset();
  translate_languages_menu_model_.reset();
  translate_option_language_codes_.clear();
}

void MahoContentsHeaderView::OnTranslateLanguageSelected(
    const std::string& language_code) {
  Profile* profile = GetProfile();
  if (!profile || language_code.empty()) {
    return;
  }
  translate::TranslatePrefs(profile->GetPrefs())
      .SetRecentTargetLanguage(language_code);
  CloseTranslatePopover();
  OnTranslatePopoverTranslate();
}

void MahoContentsHeaderView::OnNeverTranslateThisSite() {
  Profile* profile = GetProfile();
  const std::string site_host(GetCurrentUrl().host());
  if (!profile || site_host.empty()) {
    return;
  }
  translate::TranslatePrefs(profile->GetPrefs())
      .AddSiteToNeverPromptList(site_host);
  CloseTranslatePopover();
  UpdateTranslateLabel();
  UpdateChrome();
}

void MahoContentsHeaderView::OnOpenTranslateLanguageSettings() {
  CloseTranslatePopover();
  if (Browser* browser = GetBrowser()) {
    OpenMahoSettingsPane(browser, "general");
  }
}

void MahoContentsHeaderView::CloseTranslatePopover() {
  if (close_translate_popover_) {
    close_translate_popover_.Run();
    close_translate_popover_.Reset();
  }
  if (translate_popover_widget_) {
    translate_popover_widget_->Close();
    translate_popover_widget_.reset();
  }
}

void MahoContentsHeaderView::ToggleUtilityPanel() {
  if (utility_bubble_coordinator_ && utility_bubble_coordinator_->IsShowing()) {
    CloseUtilityPanel();
    return;
  }
  ShowUtilityPanel();
}

void MahoContentsHeaderView::ShowUtilityPanel() {
  Browser* browser = GetBrowser();
  if (!browser || !utility_icon_ || !utility_bubble_coordinator_ ||
      !web_contents()) {
    return;
  }
  // The utility panel model reads the active tab, so bring this pane forward.
  ActivatePaneTab();
  utility_bubble_coordinator_->ShowBubble(
      utility_icon_, BuildMahoLocationBarUtilityPanelModelForBrowser(browser));
  utility_icon_->SetPanelIsShowing(true);
}

void MahoContentsHeaderView::CloseUtilityPanel() {
  if (utility_bubble_coordinator_) {
    utility_bubble_coordinator_->Hide();
  }
}

void MahoContentsHeaderView::OnUtilityPanelClosed() {
  if (utility_icon_) {
    utility_icon_->SetPanelIsShowing(false);
  }
}

void MahoContentsHeaderView::UpdateState() {
  const GURL url = GetCurrentUrl();
  const std::u16string host_text = BuildHostText(url);
  host_button_->SetText(host_text);
  host_button_->SetTooltipText(host_text);
  host_button_->GetViewAccessibility().SetName(u"Address: " + host_text +
                                               u". Press Enter to edit");
  const bool has_contents = web_contents() != nullptr;
  utility_icon_->SetEnabled(has_contents);
  copy_url_button_->SetEnabled(has_contents && url.is_valid());
  UpdateNavigationButtons();
  UpdateSecurityIcon();
  UpdateTranslateLabel();
  UpdateChrome();
}

void MahoContentsHeaderView::UpdateNavigationButtons() {
  content::NavigationController* controller =
      web_contents() ? &web_contents()->GetController() : nullptr;
  back_button_->SetEnabled(controller && controller->CanGoBack());
  forward_button_->SetEnabled(controller && controller->CanGoForward());
  reload_button_->SetEnabled(controller != nullptr);
  const std::u16string reload_name = is_loading_ ? u"Stop" : u"Reload";
  reload_button_->SetTooltipText(reload_name);
  reload_button_->GetViewAccessibility().SetName(reload_name);
  SetButtonIcon(reload_button_, is_loading_ ? maho_lucide_icons::kXIcon
                                            : maho_lucide_icons::kRotateCwIcon);
}

void MahoContentsHeaderView::UpdateSecurityIcon() {
  security_vector_icon_ = nullptr;
  security_dangerous_ = false;
  security_description_.clear();

  const GURL url = GetCurrentUrl();
  if (web_contents() && ShouldShowSecurityIcon(url)) {
    const security_state::SecurityLevel level =
        chrome_security_state::GetSecurityLevel(web_contents());
    if (level == security_state::DANGEROUS) {
      security_vector_icon_ = &maho_lucide_icons::kShieldAlertIcon;
      security_dangerous_ = true;
      security_description_ = u"Dangerous site";
    } else if (level == security_state::SECURE) {
      security_vector_icon_ = &maho_lucide_icons::kLockIcon;
      security_description_ = u"Connection is secure";
    } else if (!url.SchemeIsCryptographic()) {
      security_vector_icon_ = &maho_lucide_icons::kLockOpenIcon;
      security_description_ = u"Not secure";
    }
  }

  security_icon_->SetVisible(security_vector_icon_ != nullptr);
  security_icon_->SetTooltipText(security_description_);
  security_icon_->GetViewAccessibility().SetName(security_description_);
}

void MahoContentsHeaderView::UpdateTranslateLabel() {
  is_translated_ = false;
  translation_available_ = false;
  if (web_contents()) {
    is_translated_ = maho::IsMahoPageTranslated(web_contents());
    const std::string site_host(GetCurrentUrl().host());
    Profile* profile = GetProfile();
    const bool is_never_translate_site =
        profile && !site_host.empty() &&
        translate::TranslatePrefs(profile->GetPrefs())
            .IsSiteOnNeverPromptList(site_host);
    translation_available_ =
        maho::CanOnDeviceTranslate(web_contents()) && !is_never_translate_site;
  }
  const std::u16string text =
      BuildTranslateText(is_translated_, translation_available_);
  translate_label_->SetText(text);
  translate_label_->GetViewAccessibility().SetName(
      text.empty() ? u"Translate" : text);
  translate_label_->SetVisible(!text.empty());
}

void MahoContentsHeaderView::SetButtonIcon(views::ImageButton* button,
                                           const gfx::VectorIcon& icon) {
  const bool has_palette = palette_.primary_text != SK_ColorTRANSPARENT;
  const ui::ColorVariant normal =
      has_palette ? WithAlpha(palette_.neutral_glyph, !active_)
                  : ui::ColorVariant(active_ ? ui::kColorSysOnSurface
                                             : ui::kColorSysOnSurfaceSubtle);
  const ui::ColorVariant hovered =
      has_palette ? ui::ColorVariant(palette_.primary_text)
                  : ui::ColorVariant(ui::kColorSysOnSurface);
  // Bound explicitly so macOS inactive-window STATE_DISABLED stays on palette.
  const ui::ColorVariant disabled =
      has_palette ? DisabledGlyph(palette_.disabled_text, !active_)
                  : ui::ColorVariant(ui::kColorSysStateDisabled);
  button->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(icon, normal,
                                                       kHeaderIconSizeDp));
  button->SetImageModel(views::Button::STATE_HOVERED,
                        ui::ImageModel::FromVectorIcon(icon, hovered,
                                                       kHeaderIconSizeDp));
  button->SetImageModel(views::Button::STATE_PRESSED,
                        ui::ImageModel::FromVectorIcon(icon, hovered,
                                                       kHeaderIconSizeDp));
  button->SetImageModel(views::Button::STATE_DISABLED,
                        ui::ImageModel::FromVectorIcon(icon, disabled,
                                                       kHeaderIconSizeDp));
}

void MahoContentsHeaderView::UpdateChrome() {
  const bool has_palette = palette_.primary_text != SK_ColorTRANSPARENT;

  SetButtonIcon(back_button_, maho_lucide_icons::kChevronLeftIcon);
  SetButtonIcon(forward_button_, maho_lucide_icons::kChevronRightIcon);
  SetButtonIcon(reload_button_, is_loading_ ? maho_lucide_icons::kXIcon
                                            : maho_lucide_icons::kRotateCwIcon);
  SetButtonIcon(copy_url_button_, maho_lucide_icons::kLinkIcon);

  if (security_vector_icon_) {
    ui::ColorVariant color =
        security_dangerous_
            ? ui::ColorVariant(ui::kColorSysError)
            : (has_palette ? WithAlpha(palette_.neutral_glyph, !active_)
                           : ui::ColorVariant(ui::kColorSysOnSurfaceSubtle));
    // The same glyph in every state: the security state, not the pointer,
    // decides its colour.
    const ui::ImageModel image = ui::ImageModel::FromVectorIcon(
        *security_vector_icon_, color, kHeaderIconSizeDp);
    for (views::Button::ButtonState state :
         {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
          views::Button::STATE_PRESSED, views::Button::STATE_DISABLED}) {
      security_icon_->SetImageModel(state, image);
    }
  }

  const ui::ColorVariant host_color =
      has_palette ? ui::ColorVariant(active_ ? palette_.primary_text
                                             : palette_.secondary_text)
                  : ui::ColorVariant(active_ ? ui::kColorSysOnSurface
                                             : ui::kColorSysOnSurfaceSubtle);
  const ui::ColorVariant secondary_color =
      has_palette ? ui::ColorVariant(palette_.secondary_text)
                  : ui::ColorVariant(ui::kColorSysOnSurfaceSubtle);
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
        views::Button::STATE_PRESSED, views::Button::STATE_DISABLED}) {
    host_button_->SetTextColor(state, host_color);
    translate_label_->SetTextColor(state, secondary_color);
  }
}

}  // namespace maho
