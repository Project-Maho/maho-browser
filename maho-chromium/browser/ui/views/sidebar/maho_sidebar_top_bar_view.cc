// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_top_bar_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "maho/browser/mail_helper/maho_mail_badge.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace maho {

namespace {

// Extra margin below the top-bar row (currently zero; the kTopBarInsets bottom
// value handles separation from the search pill).
constexpr int kTopBarBottomMarginDp = 0;
// Internal padding inside each button cluster (leading and nav).
constexpr int kTopBarClusterHorizontalInsetDp = 0;
constexpr int kTopBarClusterVerticalInsetDp = 0;
// Left margin on the sidebar-toggle button, shifting it inward from the
// traffic-light spacer edge for optical alignment with macOS window controls.
constexpr int kSidebarToggleLeftMarginDp = 6;
constexpr int kMailBadgeHeightDp = 16;
constexpr int kMailBadgeSingleDigitWidthDp = 16;
constexpr int kMailBadgeWideWidthDp = 24;

ui::ImageModel CreateTopBarButtonImage(const gfx::VectorIcon& icon,
                                       int icon_size,
                                       ui::ColorId color_id) {
  return ui::ImageModel::FromVectorIcon(icon, color_id, icon_size);
}

void ApplyButtonChrome(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 0)));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);
}

void StyleClusterView(views::View* cluster) {
  cluster->SetBackground(nullptr);
  cluster->SetBorder(nullptr);
}

ui::ColorId GetTopBarButtonColor(bool enabled) {
  return enabled ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle;
}

views::ImageButton* AddControlButton(views::View* parent,
                                     views::Button::PressedCallback callback,
                                     const gfx::VectorIcon& icon,
                                     const std::u16string& tooltip,
                                     bool enabled = true) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateTopBarButtonImage(icon, sidebar_layout::kTopBarIconSizeDp,
                              GetTopBarButtonColor(enabled)));
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetPreferredSize(gfx::Size(sidebar_layout::kTopBarButtonSizeDp,
                                     sidebar_layout::kTopBarButtonSizeDp));
  button->SetEnabled(enabled);
  views::InstallCircleHighlightPathGenerator(button.get());
  ApplyButtonChrome(button.get());
  return parent->AddChildView(std::move(button));
}

}  // namespace

BEGIN_METADATA(MahoSidebarTopBarView)
END_METADATA

MahoSidebarTopBarView::MahoSidebarTopBarView(Browser* browser)
    : browser_(browser) {
  // Horizontal BoxLayout: [traffic-light spacer | leading cluster | flex | nav
  // cluster]
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(sidebar_layout::kTopBarInsets.top(),
                        sidebar_layout::kTopBarInsets.left(),
                        sidebar_layout::kTopBarInsets.bottom(),
                        sidebar_layout::kTopBarInsets.right()),
      0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetProperty(views::kMarginsKey,
              gfx::Insets::TLBR(0, 0, kTopBarBottomMarginDp, 0));

  // Fixed-width spacer reserving room for macOS traffic-light buttons.
  traffic_light_spacer_ = AddChildView(std::make_unique<views::View>());
  traffic_light_spacer_->SetPreferredSize(
      gfx::Size(sidebar_layout::kTrafficLightAlignmentWidthDp,
                sidebar_layout::kTopBarTrafficLightAlignmentHeightDp));
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  auto* menu_layout = traffic_light_spacer_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  menu_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  ellipsis_button_ = AddControlButton(
      traffic_light_spacer_,
      base::BindRepeating(&MahoSidebarTopBarView::OnMenuPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kEllipsisIcon, u"Menu");
#endif

  // Leading cluster: sidebar-toggle (and any future leading actions).
  leading_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* leading_layout =
      leading_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          sidebar_layout::kTopBarButtonSpacingDp));
  leading_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  sidebar_toggle_button_ = AddControlButton(
      leading_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnToggleSidebarPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kPanelLeftIcon, u"Toggle Sidebar");
  sidebar_toggle_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, kSidebarToggleLeftMarginDp, 0, 0));

  // Flexible spacer pushes the nav cluster to the trailing edge.
  flex_spacer_ = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(flex_spacer_, 1);

  nav_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* nav_layout =
      nav_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          sidebar_layout::kTopBarButtonSpacingDp));
  nav_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  mail_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnMailPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kInboxIcon, u"Mail");
  // Mail entry point follows the maho.mail.enabled pref (default false), the
  // same gate used by chrome://maho-mail and the helper subprocess.
  auto unread_badge = std::make_unique<views::Label>();
  unread_badge->SetBackground(views::CreateRoundedRectBackground(
      gfx::kGoogleRed600, kMailBadgeHeightDp / 2));
  unread_badge->SetEnabledColor(SK_ColorWHITE);
  unread_badge->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  unread_badge->SetFontList(
      unread_badge->font_list().DeriveWithSizeDelta(-3).DeriveWithWeight(
          gfx::Font::Weight::BOLD));
  unread_badge->SetVisible(false);
  unread_badge->SetCanProcessEventsWithinSubtree(false);
  mail_unread_badge_ = mail_button_->AddChildView(std::move(unread_badge));
  if (browser_) {
    mail_pref_registrar_.Init(browser_->GetProfile()->GetPrefs());
    mail_pref_registrar_.Add(
        sidebar_prefs::kMahoMailEnabled,
        base::BindRepeating(&MahoSidebarTopBarView::UpdateMailButtonVisibility,
                            weak_factory_.GetWeakPtr()));
    mail_pref_registrar_.Add(
        "maho.mail.unread_count",
        base::BindRepeating(&MahoSidebarTopBarView::UpdateMailUnreadBadge,
                            weak_factory_.GetWeakPtr()));
    mail_pref_registrar_.Add(
        "maho.mail.badge_enabled",
        base::BindRepeating(&MahoSidebarTopBarView::UpdateMailUnreadBadge,
                            weak_factory_.GetWeakPtr()));
  }
  UpdateMailButtonVisibility();
  ai_button_ =
      AddControlButton(nav_cluster_,
                       base::BindRepeating(&MahoSidebarTopBarView::OnAiPressed,
                                           weak_factory_.GetWeakPtr()),
                       maho_lucide_icons::kMessageSquareIcon, u"AI Panel");

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);

  ApplyChromeSpacing();
  UpdateButtonImages();
  control_activity_service_ =
      browser_ ? maho::ai::MahoControlActivityService::GetForProfile(
                     browser_->GetProfile())
               : nullptr;
  if (control_activity_service_) {
    control_activity_service_->AddObserver(this);
  }
}

MahoSidebarTopBarView::~MahoSidebarTopBarView() {
  if (control_activity_service_) {
    control_activity_service_->RemoveObserver(this);
  }
}

void MahoSidebarTopBarView::OnControlActivityChanged(
    const maho::ai::ControlActivity& activity) {
  if (activity.observer_revision <= control_activity_revision_) {
    return;
  }
  control_activity_revision_ = activity.observer_revision;
  control_activity_id_ = activity.session_id;
  MahoControlActivityIndicatorModel model;
  model.controller_name = base::UTF8ToUTF16(activity.controller_display_name);
  switch (activity.control_plane) {
    case maho::ai::ControlPlane::kLocalAgent:
      model.plane = MahoControlPlane::kEmbedded;
      break;
    case maho::ai::ControlPlane::kMcp:
      model.plane = MahoControlPlane::kExternalMcp;
      break;
    case maho::ai::ControlPlane::kEnterprise:
      model.plane = MahoControlPlane::kCli;
      break;
  }
  if (!activity.receipts.empty()) {
    model.current_step =
        base::UTF8ToUTF16(activity.receipts.back().summary);
  }
  switch (activity.state) {
    case maho::ai::ControlActivityState::kReading:
      model.state = MahoControlActivityState::kReading;
      break;
    case maho::ai::ControlActivityState::kActing:
      model.state = MahoControlActivityState::kActing;
      break;
    case maho::ai::ControlActivityState::kWaitingApproval:
      model.state = MahoControlActivityState::kWaitingApproval;
      break;
    case maho::ai::ControlActivityState::kPaused:
      model.state = MahoControlActivityState::kPaused;
      break;
    case maho::ai::ControlActivityState::kDisconnected:
      model.state = MahoControlActivityState::kDisconnected;
      break;
    case maho::ai::ControlActivityState::kFailed:
      model.state = MahoControlActivityState::kError;
      break;
    case maho::ai::ControlActivityState::kCompleted:
      model.state = MahoControlActivityState::kIdle;
      break;
  }
  std::string stable_target_tab_id;
  if (activity.target) {
    model.target_is_other_window =
        activity.target->window_id != browser_->GetSessionID().id();
    model.target_title = activity.target->title;
    if (!model.target_is_other_window) {
      TabStripModel* tab_strip = browser_->GetTabStripModel();
      for (int index = 0; tab_strip && index < tab_strip->count(); ++index) {
        content::WebContents* contents = tab_strip->GetWebContentsAt(index);
        auto* helper = contents
                           ? sessions::SessionTabHelper::FromWebContents(
                                 contents)
                           : nullptr;
        if (helper &&
            helper->session_id().id() == activity.target->tab_id) {
          model.target_is_background_tab =
              index != tab_strip->active_index();
          if (auto* tab_id_helper =
                  MahoTabIdHelper::FromWebContents(contents)) {
            stable_target_tab_id = tab_id_helper->stable_tab_id();
          }
          break;
        }
      }
    }
  }
  const bool controls_target =
      activity.target.has_value() && !model.target_is_other_window &&
      activity.state != maho::ai::ControlActivityState::kDisconnected &&
      activity.state != maho::ai::ControlActivityState::kCompleted &&
      activity.state != maho::ai::ControlActivityState::kFailed;
  if (controlled_tab_changed_callback_) {
    controlled_tab_changed_callback_.Run(
        controls_target ? std::move(stable_target_tab_id) : std::string());
  }
}

void MahoSidebarTopBarView::SetNavClusterVisible(bool visible) {
  if (nav_cluster_ && nav_cluster_->GetVisible() != visible) {
    nav_cluster_->SetVisible(visible);
  }
}

void MahoSidebarTopBarView::SetLeadingActionsVisible(bool visible) {
  leading_actions_visible_ = visible;
  if (leading_cluster_ && leading_cluster_->GetVisible() != visible) {
    leading_cluster_->SetVisible(visible);
  }
}

void MahoSidebarTopBarView::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  UpdateButtonImages();
}

void MahoSidebarTopBarView::Update(const MahoSidebarTopBarModel& model) {
  model_ = model;
  ApplyChromeSpacing();
  UpdateButtonImages();
}

void MahoSidebarTopBarView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  MaybeUpdateDynamicTopInset();
  ApplyChromeSpacing();
}

void MahoSidebarTopBarView::AddedToWidget() {
  views::View::AddedToWidget();
  MaybeUpdateDynamicTopInset();
}

void MahoSidebarTopBarView::MaybeUpdateDynamicTopInset() {
  auto* layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  if (!layout) {
    return;
  }
  std::optional<int> measured_center_y = GetTrafficLightCenterYInView(this);
  if (!measured_center_y.has_value()) {
    return;
  }
  const int desired_top_inset =
      std::max(0, *measured_center_y -
                      sidebar_layout::kTopBarTrafficLightAlignmentHeightDp / 2);
  const gfx::Insets current = layout->inside_border_insets();
  if (current.top() == desired_top_inset) {
    return;
  }
  layout->set_inside_border_insets(gfx::Insets::TLBR(
      desired_top_inset, current.left(), current.bottom(), current.right()));
  InvalidateLayout();
}

bool MahoSidebarTopBarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  if (ellipsis_button_ && ellipsis_button_->GetVisible()) {
    gfx::Point point_in_button = point;
    views::View::ConvertPointToTarget(this, ellipsis_button_, &point_in_button);
    if (ellipsis_button_->HitTestPoint(point_in_button)) {
      return false;
    }
  }
#endif
  for (const views::ImageButton* button :
       {sidebar_toggle_button_.get(), ai_button_.get(), mail_button_.get()}) {
    if (button && button->GetVisible()) {
      gfx::Point point_in_button = point;
      views::View::ConvertPointToTarget(this, button, &point_in_button);
      if (button->HitTestPoint(point_in_button)) {
        return false;
      }
    }
  }
  return true;
}

// Keeps the top-row scaffold and caption behavior stable even before the
// first navigable tab exists; button enabled state is handled separately in
// UpdateButtonImages().
void MahoSidebarTopBarView::ApplyChromeSpacing() {
  auto* leading_layout =
      static_cast<views::BoxLayout*>(leading_cluster_->GetLayoutManager());
  auto* nav_layout =
      static_cast<views::BoxLayout*>(nav_cluster_->GetLayoutManager());

  leading_layout->set_between_child_spacing(
      sidebar_layout::kTopBarButtonSpacingDp);
  nav_layout->set_between_child_spacing(sidebar_layout::kTopBarButtonSpacingDp);

  leading_cluster_->SetVisible(leading_actions_visible_);
  leading_cluster_->SetProperty(views::kMarginsKey,
                                gfx::Insets::TLBR(0, 0, 0, 0));
  flex_spacer_->SetVisible(true);
  nav_cluster_->SetVisible(true);

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);
  PreferredSizeChanged();
}

void MahoSidebarTopBarView::UpdateButtonImages() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  UpdateButtonState(ellipsis_button_, true, maho_lucide_icons::kEllipsisIcon,
                    u"Menu");
#endif
  UpdateButtonState(sidebar_toggle_button_, true,
                    maho_lucide_icons::kPanelLeftIcon, u"Toggle Sidebar");
  UpdateButtonState(ai_button_, true, maho_lucide_icons::kMessageSquareIcon,
                    u"AI Panel");
  UpdateButtonState(mail_button_, true, maho_lucide_icons::kInboxIcon, u"Mail");
}

void MahoSidebarTopBarView::UpdateButtonState(views::ImageButton* button,
                                              bool enabled,
                                              const gfx::VectorIcon& icon,
                                              const std::u16string& tooltip) {
  if (!button) {
    return;
  }

  button->SetEnabled(enabled);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  ui::ImageModel image_model;
  if (palette_.primary_text != SK_ColorTRANSPARENT) {
    // Functional glyphs use the glyph role (>= 3:1), not the text role.
    const SkColor icon_color =
        enabled ? palette_.neutral_glyph : palette_.disabled_text;
    image_model = ui::ImageModel::FromVectorIcon(
        icon, icon_color, sidebar_layout::kTopBarIconSizeDp);
    button->SetImageModel(
        views::Button::STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(icon, palette_.disabled_text,
                                       sidebar_layout::kTopBarIconSizeDp));
  } else {
    image_model = CreateTopBarButtonImage(
        icon, sidebar_layout::kTopBarIconSizeDp, GetTopBarButtonColor(enabled));
  }
  button->SetImageModel(views::Button::STATE_NORMAL, image_model);
}

void MahoSidebarTopBarView::OnAiPressed(const ui::Event& event) {
  if (!browser_) {
    return;
  }

  auto* side_panel_ui = browser_->GetFeatures().side_panel_ui();
  if (side_panel_ui->GetCurrentEntryId() ==
      SidePanelEntryId::kMahoAiPanel) {
    side_panel_ui->Close(
                         SidePanelEntryHideReason::kSidePanelClosed,
                         /*suppress_animations=*/true);
    return;
  }

  side_panel_ui->Show(SidePanelEntryKey(SidePanelEntryId::kMahoAiPanel),
                      SidePanelOpenTrigger::kToolbarButton);
}

void MahoSidebarTopBarView::OnMailPressed(const ui::Event& event) {
  if (!browser_) {
    return;
  }

  // Mail opens as a regular browser tab rather than a floating overlay. The
  // overlay did not observe TabStripModel, so it kept covering the content area
  // after a tab switch; a tab inherits activation, restore, back/forward, and
  // link handling from Chromium instead of reimplementing them. The tab is
  // filtered out of the sidebar tab list (see IsSidebarEligibleUrl) because the
  // control button above is the stable entry point.
  const GURL mail_url(maho::kMahoMailURL);
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (tab_strip) {
    for (int i = 0; i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      content::NavigationEntry* entry =
          contents->GetController().GetLastCommittedEntry();
      if (!entry) {
        continue;
      }
      const GURL& url = entry->GetVirtualURL();
      const bool is_mail =
          (url.SchemeIs("chrome") && url.host() == maho::kMahoMailHost) ||
          (url.SchemeIs("maho") &&
           url.host() == GURL(maho::kMahoMailPublicURL).host());
      if (is_mail) {
        tab_strip->ActivateTabAt(i);
        return;
      }
    }
  }

  NavigateParams params(browser_, mail_url, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}
void MahoSidebarTopBarView::OnToggleSidebarPressed(const ui::Event& event) {
  if (browser_) {
    sidebar_prefs::ToggleSidebarPanelExpanded(browser_->GetProfile()->GetPrefs());
  }
}

bool MahoSidebarTopBarView::ShowAppMenu() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  return ShowAppMenuWithRunFlags(views::MenuRunner::SHOULD_SHOW_MNEMONICS |
                                 views::MenuRunner::INVOKED_FROM_KEYBOARD);
#else
  return false;
#endif
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
void MahoSidebarTopBarView::OnMenuPressed(const ui::Event& event) {
  ShowAppMenuWithRunFlags(event.IsKeyEvent()
                              ? (views::MenuRunner::SHOULD_SHOW_MNEMONICS |
                                 views::MenuRunner::INVOKED_FROM_KEYBOARD)
                              : views::MenuRunner::NO_FLAGS);
}

bool MahoSidebarTopBarView::ShowAppMenuWithRunFlags(int run_flags) {
  if (!browser_ || !ellipsis_button_ || !ellipsis_button_->GetVisible()) {
    return false;
  }
  if (app_menu_ && app_menu_->IsShowing()) {
    return true;
  }

  views::Widget* widget = GetWidget();
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  ToolbarView* toolbar = browser_view ? browser_view->toolbar() : nullptr;
  if (!widget || !toolbar) {
    return false;
  }

  app_menu_.reset();
  app_menu_model_ = std::make_unique<AppMenuModel>(
      toolbar, browser_, toolbar->app_menu_icon_controller());
  app_menu_model_->Init();
  const int generation = ++app_menu_generation_;
  app_menu_ = std::make_unique<AppMenu>(
      browser_, app_menu_model_.get(), run_flags,
      base::BindRepeating(&MahoSidebarTopBarView::OnAppMenuClosed,
                          weak_factory_.GetWeakPtr(), generation));
  app_menu_->RunMenu(widget, ellipsis_button_->GetBoundsInScreen());
  return true;
}

void MahoSidebarTopBarView::OnAppMenuClosed(int generation) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MahoSidebarTopBarView::ResetAppMenu,
                                weak_factory_.GetWeakPtr(), generation));
}

void MahoSidebarTopBarView::ResetAppMenu(int generation) {
  if (generation != app_menu_generation_) {
    return;
  }
  app_menu_.reset();
  app_menu_model_.reset();
}
#endif

void MahoSidebarTopBarView::SetPrivateAppearance() {
  UpdateMailButtonVisibility();
}

void MahoSidebarTopBarView::UpdateMailButtonVisibility() {
  if (!mail_button_) {
    return;
  }

  const bool visible =
      browser_ && !browser_->GetProfile()->IsOffTheRecord() &&
      sidebar_prefs::IsMahoMailEnabled(browser_->GetProfile()->GetPrefs());
  if (mail_button_->GetVisible() != visible) {
    mail_button_->SetVisible(visible);
    PreferredSizeChanged();
  }
  UpdateMailUnreadBadge();
}

void MahoSidebarTopBarView::UpdateMailUnreadBadge() {
  if (!mail_unread_badge_ || !browser_) {
    return;
  }
  const bool mail_enabled =
      sidebar_prefs::IsMahoMailEnabled(browser_->GetProfile()->GetPrefs());
  const bool badge_enabled =
      browser_->GetProfile()->GetPrefs()->GetBoolean("maho.mail.badge_enabled");
  const int unread =
      browser_->GetProfile()->GetPrefs()->GetInteger("maho.mail.unread_count");
  const MailBadgePresentation presentation = ResolveMailBadgePresentation(
      mail_enabled && mail_button_ && mail_button_->GetVisible(), badge_enabled,
      unread);
  mail_unread_badge_->SetVisible(presentation.visible);
  mail_button_->SetAccessibleName(presentation.accessible_name);
  if (!presentation.visible) {
    return;
  }

  mail_unread_badge_->SetText(presentation.text);
  const int badge_width = presentation.text.size() == 1
                              ? kMailBadgeSingleDigitWidthDp
                              : kMailBadgeWideWidthDp;
  const int side = sidebar_layout::kTopBarButtonSizeDp;
  mail_unread_badge_->SetBounds(side - badge_width, 0, badge_width,
                                kMailBadgeHeightDp);
}

}  // namespace maho
