// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/ui/context_menu/maho_address_bar_context_menu.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kSearchShellInsetTopDp = 0;
constexpr int kSearchShellInsetBottomDp = 0;
constexpr int kSearchShellInnerInsetDp = 0;
constexpr int kSearchCardLeadingInsetDp = 13;
constexpr int kSearchCardTrailingInsetDp = 13;
constexpr int kSearchPillTrailingInsetDp = 13;
constexpr int kSearchPillLeadingInsetDp = 13;
constexpr int kSearchPillIconLabelSpacingDp = 8;
constexpr int kSearchTextfieldHorizontalInsetDp = 13;
constexpr int kSearchPillActionShellWidthDp = 24;
constexpr int kSearchPillActionShellHeightDp = 20;
constexpr int kSearchPillUtilitySlotSpacingDp = 0;
constexpr int kSearchPillPrimarySecondarySpacingDp = 0;
constexpr int kSearchPillInnerVerticalInsetDp = 5;
constexpr int kSearchPillSecondaryBaselineOffsetDp = -1;

class SidebarSearchIdleButton : public views::LabelButton {
 public:
  SidebarSearchIdleButton(views::Button::PressedCallback callback,
                          const std::u16string& text,
                          base::RepeatingCallback<void(bool)> hover_callback)
      : views::LabelButton(std::move(callback), text),
        hover_callback_(std::move(hover_callback)) {
    SetNotifyEnterExitOnChild(true);
  }

  SidebarSearchIdleButton(const SidebarSearchIdleButton&) = delete;
  SidebarSearchIdleButton& operator=(const SidebarSearchIdleButton&) = delete;
  ~SidebarSearchIdleButton() override = default;

  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::LabelButton::OnMouseEntered(event);
    if (hover_callback_) {
      hover_callback_.Run(true);
    }
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    views::LabelButton::OnMouseExited(event);
    if (hover_callback_) {
      hover_callback_.Run(false);
    }
  }

 private:
  base::RepeatingCallback<void(bool)> hover_callback_;
};

}  // namespace

BEGIN_METADATA(MahoSidebarSearchView)
END_METADATA

MahoSidebarSearchView::MahoSidebarSearchView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kSearchShellInsetTopDp, 0, kSearchShellInsetBottomDp, 0),
      0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  search_shell_ = AddChildView(std::make_unique<views::View>());
  auto* shell_layout =
      search_shell_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::VH(kSearchShellInnerInsetDp, kSearchShellInnerInsetDp),
          0));
  shell_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  search_shell_->SetBackground(nullptr);
  search_shell_->SetBorder(nullptr);

  std::unique_ptr<views::LabelButton> idle_button =
      std::make_unique<SidebarSearchIdleButton>(
      base::BindRepeating(&MahoSidebarSearchView::OnIdleButtonPressed,
                          weak_factory_.GetWeakPtr()),
      std::u16string(),
      base::BindRepeating(&MahoSidebarSearchView::OnIdleButtonHoverChanged,
                          weak_factory_.GetWeakPtr()));
  auto* idle_button_raw =
      static_cast<SidebarSearchIdleButton*>(idle_button.get());
  idle_button->SetAccessibleName(u"Search or Enter URL...");
  idle_button->SetTooltipText(u"Search or Enter URL...");
  idle_button_raw->SetRequestFocusOnPress(false);
  idle_button_raw->SetPreferredSize(
      gfx::Size(1, sidebar_layout::kSearchPillHeightDp));
  auto* idle_layout = idle_button_raw->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kSearchPillIconLabelSpacingDp));
  idle_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  idle_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  idle_button_ = static_cast<views::LabelButton*>(
      search_shell_->AddChildView(std::move(idle_button)));
  idle_button_->set_context_menu_controller(this);

  auto icon_shell = std::make_unique<views::View>();
  icon_shell->SetPreferredSize(gfx::Size(14, 14));
  auto* icon_layout = icon_shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  icon_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  icon_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  leading_icon_shell_ = idle_button_raw->AddChildView(std::move(icon_shell));
  leading_icon_ =
      leading_icon_shell_->AddChildView(std::make_unique<views::ImageView>());

  auto text_stack = std::make_unique<views::View>();
  auto* text_layout = text_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      kSearchPillPrimarySecondarySpacingDp));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* text_stack_raw = idle_button_raw->AddChildView(std::move(text_stack));
  idle_layout->SetFlexForView(text_stack_raw, 1);

  primary_label_ = text_stack_raw->AddChildView(std::make_unique<views::Label>());
  primary_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  primary_label_->SetAutoColorReadabilityEnabled(false);
  primary_label_->SetEnabledColor(ui::kColorSysOnSurface);
  primary_label_->SetTextStyle(views::style::STYLE_BODY_4);
  primary_label_->SetMultiLine(false);
  primary_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  secondary_label_ = text_stack_raw->AddChildView(std::make_unique<views::Label>());
  secondary_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  secondary_label_->SetAutoColorReadabilityEnabled(false);
  secondary_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  secondary_label_->SetTextStyle(views::style::STYLE_BODY_5);
  secondary_label_->SetMultiLine(false);
  secondary_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  secondary_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kSearchPillSecondaryBaselineOffsetDp, 0, 0, 0)));

  auto utility_icon_slot = std::make_unique<views::View>();
  auto* utility_slot_layout = utility_icon_slot->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  utility_slot_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  utility_slot_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  utility_icon_slot_ = idle_button_raw->AddChildView(std::move(utility_icon_slot));
  utility_icon_slot_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, kSearchPillUtilitySlotSpacingDp, 0, 0));

  utility_icon_ = utility_icon_slot_->AddChildView(
      std::make_unique<MahoLocationBarUtilityIconView>(base::BindRepeating(
          [](base::WeakPtr<MahoSidebarSearchView> view) -> views::Widget* {
            return view ? view->ShowUtilityPanel() : nullptr;
          },
          weak_factory_.GetWeakPtr())));
  utility_icon_->SetPreferredSize(gfx::Size(kSearchPillActionShellWidthDp,
                                            kSearchPillActionShellHeightDp));
  utility_icon_->SetHoverVisible(false);
  idle_layout->SetFlexForView(text_stack_raw, 1);
  idle_layout->SetFlexForView(utility_icon_slot_, 0);

  auto textfield = std::make_unique<views::Textfield>();
  textfield->SetVisible(false);
  textfield->SetPlaceholderText(u"Search or Enter URL...");
  textfield->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(0, kSearchTextfieldHorizontalInsetDp)));
  textfield->SetBackgroundColor(SK_ColorTRANSPARENT);
  textfield->SetTextColor(ui::kColorSysOnSurface);
  textfield->SetAccessibleName(u"Search or Enter URL...");
  textfield->set_controller(this);
  textfield_ = search_shell_->AddChildView(std::move(textfield));
  UpdateSearchChrome();
  UpdateIdleSurface();
}

MahoSidebarSearchView::~MahoSidebarSearchView() = default;

void MahoSidebarSearchView::FocusVisibleControl() {
  if (is_editing_ && textfield_ && textfield_->GetVisible()) {
    textfield_->RequestFocus();
    return;
  }

  if (idle_button_ && idle_button_->GetVisible()) {
    idle_button_->RequestFocus();
  }
}

void MahoSidebarSearchView::Update(const MahoSidebarSearchModel& model) {
  model_ = model;
  UpdateSearchChrome();
  if (!is_editing_) {
    UpdateIdleSurface();
  }
}

void MahoSidebarSearchView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_) {
    return;
  }

  MahoAddressBarContextMenu context_menu(
      browser_,
      base::BindRepeating(&MahoSidebarSearchView::BeginEditing,
                          weak_factory_.GetWeakPtr()));
  auto menu_model = context_menu.BuildMenuModel();
  views::MenuRunner menu_runner(menu_model.get(),
                                views::MenuRunner::CONTEXT_MENU);
  menu_runner.RunMenuAt(source->GetWidget(), nullptr,
                        gfx::Rect(point, gfx::Size()),
                        views::MenuAnchorPosition::kTopLeft, source_type);
}

void MahoSidebarSearchView::ExecuteCommand(int command_id, int event_flags) {}

bool MahoSidebarSearchView::IsCommandIdEnabled(int command_id) const {
  return true;
}

bool MahoSidebarSearchView::IsCommandIdChecked(int command_id) const {
  return false;
}

bool MahoSidebarSearchView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      EndEditing(true);
      return true;
    case ui::VKEY_ESCAPE:
      EndEditing(false);
      return true;
    default:
      return false;
  }
}

void MahoSidebarSearchView::OnWidgetDestroyed(views::Widget* widget) {
  if (widget != utility_panel_widget_) {
    return;
  }

  utility_panel_widget_observation_.Reset();
  utility_panel_widget_ = nullptr;
  UpdateUtilityAffordanceVisibility();
}

void MahoSidebarSearchView::OnSearchPressed(const ui::Event& event) {
  FocusCommandSurface();
}

void MahoSidebarSearchView::BeginEditing() {
  if (is_editing_) {
    return;
  }

  CloseUtilityPanel();
  OnIdleButtonHoverChanged(false);
  is_editing_ = true;
  idle_button_->SetVisible(false);
  textfield_->SetVisible(true);
  textfield_->SetText(std::u16string());
  UpdateSearchChrome();
  textfield_->RequestFocus();
}

void MahoSidebarSearchView::EndEditing(bool submit) {
  if (!is_editing_) {
    return;
  }

  const std::u16string text(textfield_->GetText().begin(),
                             textfield_->GetText().end());
  is_editing_ = false;
  textfield_->SetVisible(false);
  idle_button_->SetVisible(true);
  if (submit) {
    SubmitText(text);
  }
  UpdateSearchChrome();
  UpdateIdleSurface();
}

void MahoSidebarSearchView::FocusCommandSurface() {
  if (!browser_) {
    return;
  }
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }

  for (views::View* child : browser_view->children()) {
    auto* container = views::AsViewClass<MahoSidebarContainerView>(child);
    if (container) {
      std::string current_url;
      auto* active_contents =
          browser_->tab_strip_model()->GetActiveWebContents();
      if (active_contents) {
        current_url = active_contents->GetVisibleURL().spec();
      }
      container->ShowCommandOverlayForCurrentTab(current_url);
      return;
    }
  }
}

void MahoSidebarSearchView::FocusCommandSurfaceWithQuery(
    const std::string& query) {
  if (!browser_) {
    return;
  }
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }

  for (views::View* child : browser_view->children()) {
    auto* container = views::AsViewClass<MahoSidebarContainerView>(child);
    if (container) {
      container->ShowCommandOverlayForSearch(query);
      return;
    }
  }
}

void MahoSidebarSearchView::SubmitText(const std::u16string& text) {
  if (!browser_) {
    return;
  }

  std::string input = base::UTF16ToUTF8(text);
  base::TrimWhitespaceASCII(input, base::TRIM_ALL, &input);
  if (input.empty()) {
    return;
  }

  if (!LooksLikeUrl(input)) {
    FocusCommandSurfaceWithQuery(input);
    return;
  }

  const std::string normalized = input.starts_with("http://") ||
                                         input.starts_with("https://") ||
                                         input.starts_with("chrome://")
                                     ? input
                                     : "https://" + input;
  GURL target_url(normalized);
  if (!target_url.is_valid()) {
    return;
  }

  browser_->OpenURL(content::OpenURLParams(
                        target_url, content::Referrer(),
                        WindowOpenDisposition::CURRENT_TAB,
                        ui::PAGE_TRANSITION_TYPED, false),
                    base::NullCallback());
}

void MahoSidebarSearchView::OnIdleButtonPressed(const ui::Event& event) {
  OnSearchPressed(event);
}

void MahoSidebarSearchView::OnIdleButtonHoverChanged(bool hover_visible) {
  if (!utility_icon_) {
    return;
  }

  utility_icon_->SetHoverVisible(CanShowUtilityAffordance() && hover_visible);
  UpdateUtilityAffordanceVisibility();
}

views::Widget* MahoSidebarSearchView::ShowUtilityPanel() {
  if (!browser_ || !utility_icon_) {
    return nullptr;
  }

  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModelForBrowser(browser_);
  views::Widget* widget =
      MahoLocationBarUtilityPanelView::CreateWidget(utility_icon_, std::move(model));
  if (widget) {
    widget->Show();
    if (!widget->IsClosed()) {
      widget->widget_delegate()->GetContentsView()->RequestFocus();
    }
  }
  utility_icon_->SetPanelWidget(widget);
  SetUtilityPanelWidget(widget);
  return utility_panel_widget_;
}

void MahoSidebarSearchView::SetUtilityPanelWidget(views::Widget* widget) {
  if (utility_panel_widget_ == widget) {
    UpdateUtilityAffordanceVisibility();
    return;
  }

  if (utility_panel_widget_) {
    utility_panel_widget_observation_.Reset();
  }

  utility_panel_widget_ = widget;
  if (utility_panel_widget_) {
    utility_panel_widget_observation_.Observe(utility_panel_widget_);
  }
  UpdateUtilityAffordanceVisibility();
}

void MahoSidebarSearchView::CloseUtilityPanel() {
  if (utility_panel_widget_) {
    utility_panel_widget_->Close();
  }
}

bool MahoSidebarSearchView::CanShowUtilityAffordance() const {
  const bool show_placeholder =
      model_.force_placeholder_for_empty_favorites ||
      model_.display_mode == MahoSidebarAddressDisplayMode::kSearchPlaceholder;
  return !is_editing_ && model_.has_active_tab && !show_placeholder;
}

void MahoSidebarSearchView::UpdateUtilityAffordanceVisibility() {
  if (!utility_icon_ || !utility_icon_slot_) {
    return;
  }

  utility_icon_slot_->SetVisible(CanShowUtilityAffordance() &&
                                 utility_icon_->GetVisible());
}

void MahoSidebarSearchView::UpdateIdleSurface() {
  const std::u16string primary_text = BuildPrimaryText();
  const std::u16string secondary_text = BuildSecondaryText();
  const bool show_placeholder =
      model_.force_placeholder_for_empty_favorites ||
      model_.display_mode == MahoSidebarAddressDisplayMode::kSearchPlaceholder;
  const bool has_active_tab = model_.has_active_tab;
  idle_button_->SetAccessibleName(primary_text);
  idle_button_->SetTooltipText(primary_text);
  primary_label_->SetText(primary_text);
  secondary_label_->SetText(secondary_text);
  secondary_label_->SetVisible(!secondary_text.empty());
  leading_icon_shell_->SetVisible(true);
  if (utility_icon_) {
    const bool utility_available = CanShowUtilityAffordance();
    if (!utility_available) {
      CloseUtilityPanel();
      utility_icon_->SetHoverVisible(false);
    }
    utility_icon_->SetEnabled(utility_available);
    UpdateUtilityAffordanceVisibility();
  }
  leading_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      show_placeholder ? vector_icons::kSearchIcon
                       : (has_active_tab ? vector_icons::kGlobeIcon
                                         : vector_icons::kSearchChromeRefreshIcon),
      show_placeholder ? ui::kColorSysOnSurfaceSubtle
                       : (has_active_tab ? ui::kColorSysOnSurface
                                          : ui::kColorSysOnSurfaceSubtle),
      show_placeholder ? 11 : 10));
}

void MahoSidebarSearchView::UpdateSearchChrome() {
  const bool show_placeholder =
      !is_editing_ &&
      (model_.force_placeholder_for_empty_favorites ||
       model_.display_mode == MahoSidebarAddressDisplayMode::kSearchPlaceholder);
  idle_button_->SetBackground(views::CreateRoundedRectBackground(
      show_placeholder ? ui::kColorSysSurface1 : ui::kColorSysSurface1,
      show_placeholder ? sidebar_layout::kSearchPillCompactCornerRadiusDp
                       : sidebar_layout::kSearchPillCornerRadiusDp));
  idle_button_->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(
          1,
          show_placeholder ? sidebar_layout::kSearchPillCompactCornerRadiusDp
                           : sidebar_layout::kSearchPillCornerRadiusDp,
          show_placeholder ? ui::kColorSysSurfaceVariant
                           : ui::kColorSysNeutralOutline),
      gfx::Insets::TLBR(
          kSearchPillInnerVerticalInsetDp,
          show_placeholder ? kSearchPillLeadingInsetDp
                           : kSearchCardLeadingInsetDp,
          kSearchPillInnerVerticalInsetDp,
          show_placeholder ? kSearchPillTrailingInsetDp
                           : kSearchCardTrailingInsetDp)));
  idle_button_->SetPreferredSize(
      gfx::Size(1, show_placeholder ? sidebar_layout::kSearchPillCompactHeightDp
                                    : sidebar_layout::kSearchPillHeightDp));
  leading_icon_shell_->SetPreferredSize(show_placeholder ? gfx::Size(11, 11)
                                                         : gfx::Size(14, 14));
  leading_icon_shell_->SetBackground(nullptr);
  if (utility_icon_slot_) {
    utility_icon_slot_->SetBackground(nullptr);
    utility_icon_slot_->SetBorder(nullptr);
  }
  if (utility_icon_) {
    utility_icon_->SetBackground(nullptr);
  }
  primary_label_->SetEnabledColor(show_placeholder ? ui::kColorSysOnSurfaceSubtle
                                                    : ui::kColorSysOnSurface);
  primary_label_->SetTextStyle(show_placeholder ? views::style::STYLE_BODY_5
                                                : views::style::STYLE_BODY_4);
  textfield_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(0, kSearchTextfieldHorizontalInsetDp)));
  textfield_->SetPreferredSize(
      gfx::Size(1, show_placeholder ? sidebar_layout::kSearchPillCompactHeightDp
                                    : sidebar_layout::kSearchPillHeightDp));
  PreferredSizeChanged();
}

std::u16string MahoSidebarSearchView::BuildPrimaryText() const {
  if (model_.force_placeholder_for_empty_favorites ||
      model_.display_mode == MahoSidebarAddressDisplayMode::kSearchPlaceholder ||
      !model_.has_active_tab) {
    return u"Search or Enter URL...";
  }

  GURL url(model_.current_url);
  if (url.is_valid() && !url.host().empty()) {
    std::string host(url.host());
    if (host.rfind("www.", 0) == 0) {
      host.erase(0, 4);
    }
    return base::UTF8ToUTF16(host);
  }
  if (!model_.idle_text.empty()) {
    return model_.idle_text;
  }
  return u"Search or Enter URL...";
}

std::u16string MahoSidebarSearchView::BuildSecondaryText() const {
  if (model_.display_mode != MahoSidebarAddressDisplayMode::kAddressCard ||
      !model_.has_active_tab) {
    return std::u16string();
  }
  return u"Translation Available";
}

bool MahoSidebarSearchView::LooksLikeUrl(const std::string& input) const {
  if (input.find(' ') != std::string::npos) {
    return false;
  }
  return input.starts_with("http://") || input.starts_with("https://") ||
         input.starts_with("chrome://") || input.find('.') != std::string::npos;
}

}  // namespace maho
