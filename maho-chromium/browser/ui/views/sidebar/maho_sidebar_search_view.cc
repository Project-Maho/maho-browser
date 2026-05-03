// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "maho/browser/ui/context_menu/maho_address_bar_context_menu.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
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
constexpr int kSearchCardLeadingInsetDp = 10;
constexpr int kSearchCardTrailingInsetDp = 10;
constexpr int kSearchPillTrailingInsetDp = 10;
constexpr int kSearchPillLeadingInsetDp = 10;
constexpr int kSearchPillIconLabelSpacingDp = 7;
constexpr int kSearchTextfieldHorizontalInsetDp = 10;
constexpr int kSearchPillActionSpacingDp = 5;
constexpr int kSearchPillActionIconSizeDp = 10;
constexpr int kSearchPillActionShellSizeDp = 16;
constexpr int kSearchPillPrimarySecondarySpacingDp = 0;
constexpr int kSearchPillInnerVerticalInsetDp = 4;
constexpr int kSearchPillSecondaryBaselineOffsetDp = -1;

const gfx::VectorIcon& GetTrailingActionIcon() {
  return vector_icons::kSearchChromeRefreshIcon;
}

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

  auto idle_button = std::make_unique<views::LabelButton>(
      base::BindRepeating(&MahoSidebarSearchView::OnIdleButtonPressed,
                          weak_factory_.GetWeakPtr()),
      std::u16string());
  idle_button->SetAccessibleName(u"Search or Enter URL...");
  idle_button->SetTooltipText(u"Search or Enter URL...");
  idle_button->SetRequestFocusOnPress(false);
  idle_button->SetPreferredSize(
      gfx::Size(1, sidebar_layout::kSearchPillHeightDp));
  auto* idle_layout = idle_button->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kSearchPillIconLabelSpacingDp));
  idle_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* idle_button_raw = idle_button.get();
  idle_button_ = search_shell_->AddChildView(std::move(idle_button));
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

  auto trailing_actions = std::make_unique<views::View>();
  auto* trailing_actions_layout = trailing_actions->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(),
                                         kSearchPillActionSpacingDp));
  trailing_actions_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  trailing_actions_view_ = idle_button_raw->AddChildView(std::move(trailing_actions));

  auto make_action_shell = []() {
    auto shell = std::make_unique<views::View>();
    shell->SetPreferredSize(
        gfx::Size(kSearchPillActionShellSizeDp, kSearchPillActionShellSizeDp));
     auto* layout = shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
         views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    return shell;
  };

  auto primary_action_shell = make_action_shell();
  action_icon_primary_ =
      primary_action_shell->AddChildView(std::make_unique<views::ImageView>());
  trailing_actions_view_->AddChildView(std::move(primary_action_shell));

  auto secondary_action_shell = make_action_shell();
  action_icon_secondary_ =
      secondary_action_shell->AddChildView(std::make_unique<views::ImageView>());
  trailing_actions_view_->AddChildView(std::move(secondary_action_shell));

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

void MahoSidebarSearchView::OnSearchPressed(const ui::Event& event) {
  BeginEditing();
}

void MahoSidebarSearchView::BeginEditing() {
  if (!model_.has_active_tab || model_.force_placeholder_for_empty_favorites) {
    FocusCommandSurface();
    return;
  }

  is_editing_ = true;
  idle_button_->SetVisible(false);
  textfield_->SetVisible(true);
  textfield_->SetText(base::UTF8ToUTF16(model_.current_url));
  textfield_->RequestFocus();
  textfield_->SelectAll(false);
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
  if (!browser_view || !browser_view->maho_sidebar_container_) {
    return;
  }
  auto* container = views::AsViewClass<MahoSidebarContainerView>(
      browser_view->maho_sidebar_container_);
  if (container) {
    container->ToggleCommandOverlay();
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

  const std::string normalized = NormalizeAddressInput(input);
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
  trailing_actions_view_->SetVisible(has_active_tab && !show_placeholder);
  if (action_icon_primary_ && action_icon_primary_->parent()) {
    action_icon_primary_->parent()->SetVisible(has_active_tab && !show_placeholder);
  }
  if (action_icon_secondary_ && action_icon_secondary_->parent()) {
    action_icon_secondary_->parent()->SetVisible(has_active_tab && !show_placeholder);
  }
  leading_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      show_placeholder ? vector_icons::kSearchIcon
                       : (has_active_tab ? vector_icons::kGlobeIcon
                                         : vector_icons::kSearchChromeRefreshIcon),
      show_placeholder ? ui::kColorSysOnSurfaceSubtle
                       : (has_active_tab ? ui::kColorSysOnSurface
                                          : ui::kColorSysOnSurfaceSubtle),
      show_placeholder ? 11 : 10));
  action_icon_primary_->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kGlobeIcon, ui::kColorSysOnSurfaceSubtle,
      kSearchPillActionIconSizeDp));
  action_icon_secondary_->SetImage(ui::ImageModel::FromVectorIcon(
      GetTrailingActionIcon(), ui::kColorSysOnSurfaceSubtle,
      kSearchPillActionIconSizeDp));
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
  if (action_icon_primary_ && action_icon_primary_->parent()) {
    action_icon_primary_->parent()->SetBackground(nullptr);
  }
  if (action_icon_secondary_ && action_icon_secondary_->parent()) {
    action_icon_secondary_->parent()->SetBackground(nullptr);
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

std::string MahoSidebarSearchView::NormalizeAddressInput(
    const std::string& input) const {
  if (LooksLikeUrl(input)) {
    if (input.starts_with("http://") || input.starts_with("https://") ||
        input.starts_with("chrome://")) {
      return input;
    }
    return "https://" + input;
  }

  return maho::kMahoCommandURL;
}

}  // namespace maho
