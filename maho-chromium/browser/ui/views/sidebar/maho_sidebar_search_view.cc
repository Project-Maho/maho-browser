// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"

#include <memory>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/translate/translate_bubble_controller.h"
#include "components/language_detection/core/constants.h"
#include "components/prefs/pref_service.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/common/translate_errors.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/maho_features.h"
#include "maho/browser/ui/context_menu/maho_address_bar_context_menu.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace maho {

namespace {

// --- Search pill spacing constants ---
//
// The pill layout (left to right) is:
//   [leading-inset | icon | icon-label-gap | text-stack | utility-gap | utility-icon | trailing-inset]
//
// "Card" variants apply when displaying a real URL (address card mode).
// "Pill" variants apply in the compact placeholder/search mode.

constexpr int kSearchShellInsetTopDp = 0;
constexpr int kSearchShellInsetBottomDp = 0;
constexpr int kSearchShellInnerInsetDp = 0;

// Leading inset: space from the pill's left border stroke to the leading icon.
constexpr int kSearchCardLeadingInsetDp = 13;

// Trailing inset: space from the utility icon's trailing edge to the pill's
// right border stroke. Intentionally wider than the leading inset so the
// utility affordance doesn't feel cramped against the pill edge.
constexpr int kSearchCardTrailingInsetDp = 15;
constexpr int kSearchPillTrailingInsetDp = 15;

// Leading inset for the compact (placeholder) pill variant.
constexpr int kSearchPillLeadingInsetDp = 13;

// Textfield horizontal padding (used in editing mode).
constexpr int kSearchTextfieldHorizontalInsetDp = 13;

// Dimensions of the trailing utility icon hit target.
constexpr int kSearchPillActionShellWidthDp = 24;
constexpr int kSearchPillActionShellHeightDp = 20;

// Gap between the text stack's trailing edge and the utility icon slot.
// This creates a small breathing room so the icon doesn't touch the text.
constexpr int kSearchPillUtilitySlotSpacingDp = 2;

// Vertical spacing between the primary label and secondary label (0 = flush).
constexpr int kSearchPillPrimarySecondarySpacingDp = 0;

// Vertical breathing room inside the pill, above and below content.
constexpr int kSearchPillInnerVerticalInsetDp = 4;

// Negative top-margin on the secondary label to pull its baseline closer
// to the primary label, achieving tighter optical alignment.
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
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  idle_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  idle_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  idle_button_ = static_cast<views::LabelButton*>(
      search_shell_->AddChildView(std::move(idle_button)));
  idle_button_->set_context_menu_controller(this);

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
  // Direct pref + flag check (no BrowserView lookup): MahoSidebarSearchView is
  // constructed inside the upstream BrowserView ctor before Browser::window_
  // is assigned, so BrowserView::GetBrowserViewForBrowser(browser_) returns
  // nullptr here. Mirror the predicate that BrowserView::IsMahoArcLayoutActive
  // computes — both inputs (Profile + is_type_normal) are available now.
  const bool arc_active =
      browser_ && browser_->is_type_normal() && browser_->profile() &&
      sidebar_prefs::IsSidebarLayoutEnabled(browser_->profile()->GetPrefs());
  if (arc_active && base::FeatureList::IsEnabled(
                        features::kMahoArcToolbarButtonProvider)) {
    secondary_label_->SetProperty(views::kElementIdentifierKey,
                                   kTranslatePageActionElementId);
  }
  auto utility_icon_slot = std::make_unique<views::View>();
  auto* utility_slot_layout = utility_icon_slot->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  utility_slot_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  utility_slot_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  utility_icon_slot_ = idle_button_raw->AddChildView(std::move(utility_icon_slot));
  // Left margin on the utility slot creates the gap between the flexible text
  // stack and the trailing utility icon, preventing them from touching.
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
  MahoSidebarSearchModel patched = model;

  // Live-tab-strip priming: if the incoming model reports no active tab but the
  // browser's tab strip currently has a valid active entry, force has_active_tab
  // to true before the anti-demotion latch below runs. This handles the startup
  // failure mode where a valid active index exists in the model but
  // GetActiveWebContents() is still null (contents not yet committed); we match
  // the adapter's own fallback: active_index != kNoTab && ContainsIndex.
  if (!patched.has_active_tab && browser_) {
    TabStripModel* tsm = browser_->tab_strip_model();
    if (tsm) {
      const int active_index = tsm->active_index();
      if (active_index != TabStripModel::kNoTab &&
          tsm->ContainsIndex(active_index)) {
        patched.has_active_tab = true;
      }
    }
  }

  // Cold-start safety: once has_active_tab has been observed as true, do not
  // demote it back to false on a transient refresh unless the tab strip is
  // genuinely empty (zero tabs). Genuine zero-tab is `!tsm || tsm->count() == 0`.
  // A transient `active_index() == kNoTab` while the strip still has tabs is
  // not zero-tab; demoting on it would hide the utility affordance during
  // normal tab-activation transitions.
  if (model_.has_active_tab && !patched.has_active_tab && browser_) {
    TabStripModel* tsm = browser_->tab_strip_model();
    const bool strip_is_empty = !tsm || tsm->count() == 0;
    if (!strip_is_empty) {
      patched.has_active_tab = true;
    }
  }
  model_ = patched;
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
      container->ShowCommandOverlayForCurrentTab();
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
  if (ShouldToggleUtilityPanelForEvent(event)) {
    ToggleUtilityPanel();
    return;
  }

  if (ShouldRouteEventToTranslate(event)) {
    OnTranslateLabelClicked();
    return;
  }

  OnSearchPressed(event);
}

void MahoSidebarSearchView::OnTranslateLabelClicked() {
  // Direct-anchor path (avoids ToolbarButtonProvider::From routing entirely).
  // The systemic provider replacement plan (§2C) is not viable on macOS Arc
  // because BrowserView::AddedToWidget() requires a valid provider in its body
  // before any child view's AddedToWidget can run, and ToolbarView ctor cannot
  // know the Maho-side anchor target. Anchor the translate bubble at the Maho
  // sidebar label here. Other bubbles (zoom/memory-saver/etc.) continue to
  // anchor through the upstream ToolbarView, which is unchanged.
  if (!browser_ || !secondary_label_) {
    return;
  }
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  auto* translate_client = ChromeTranslateClient::FromWebContents(contents);
  if (!translate_client) {
    return;
  }
  auto* translate_manager = translate_client->GetTranslateManager();
  if (!translate_manager) {
    return;
  }
  auto* controller = TranslateBubbleController::From(browser_);
  if (!controller) {
    return;
  }

  std::string source_language;
  std::string target_language;
  translate_client->GetTranslateLanguages(contents, &source_language,
                                           &target_language);
  if (source_language == target_language) {
    source_language = language_detection::kUnknownLanguageCode;
  }

  const auto* language_state = translate_manager->GetLanguageState();
  translate::TranslateStep step = translate::TRANSLATE_STEP_BEFORE_TRANSLATE;
  if (language_state) {
    if (language_state->translation_pending()) {
      step = translate::TRANSLATE_STEP_TRANSLATING;
    } else if (language_state->translation_error()) {
      step = translate::TRANSLATE_STEP_TRANSLATE_ERROR;
    } else if (language_state->IsPageTranslated()) {
      step = translate::TRANSLATE_STEP_AFTER_TRANSLATE;
    }
  }

  contents->ClearFocusedElement();
  controller->ShowTranslateBubble(
      contents, views::BubbleAnchor(secondary_label_),
      /*highlighted_element=*/std::nullopt, step, source_language,
      target_language, translate::TranslateErrors::NONE,
      LocationBarBubbleDelegateView::USER_GESTURE);
}

void MahoSidebarSearchView::OnIdleButtonHoverChanged(bool hover_visible) {
  if (!utility_icon_) {
    return;
  }

  utility_icon_->SetHoverVisible(CanShowUtilityAffordance() && hover_visible);
  UpdateUtilityAffordanceVisibility();
}

void MahoSidebarSearchView::ToggleUtilityPanel() {
  if (utility_panel_widget_) {
    CloseUtilityPanel();
    return;
  }

  ShowUtilityPanel();
}

views::Widget* MahoSidebarSearchView::ShowUtilityPanel() {
  if (!browser_ || !utility_icon_) {
    return nullptr;
  }

  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModelForBrowser(browser_);
  views::Widget* widget =
      MahoLocationBarUtilityPanelView::Show(utility_icon_, std::move(model));
  utility_icon_->SetPanelWidget(widget);
  SetUtilityPanelWidget(widget);
  return widget;
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
  // Editing suppresses the utility affordance; placeholder/new-tab style does
  // not — the icon should remain available whenever there is an active tab and
  // the user is not actively typing in the search field.
  return !is_editing_ && model_.has_active_tab;
}

void MahoSidebarSearchView::UpdateUtilityAffordanceVisibility() {
  if (!utility_icon_ || !utility_icon_slot_) {
    return;
  }

  const bool affordance = CanShowUtilityAffordance();
  utility_icon_slot_->SetVisible(affordance);
  utility_icon_->SetAffordanceVisible(affordance);
  if (!affordance) {
    utility_icon_->SetHoverVisible(false);
  }
}

void MahoSidebarSearchView::UpdateIdleSurface() {
  const std::u16string primary_text = BuildPrimaryText();
  const std::u16string secondary_text = BuildSecondaryText();
  idle_button_->SetAccessibleName(primary_text);
  idle_button_->SetTooltipText(primary_text);
  primary_label_->SetText(primary_text);
  secondary_label_->SetText(secondary_text);
  secondary_label_->SetVisible(!secondary_text.empty());
  if (utility_icon_) {
    const bool utility_available = CanShowUtilityAffordance();
    if (!utility_available) {
      CloseUtilityPanel();
      utility_icon_->SetHoverVisible(false);
    }
    utility_icon_->SetEnabled(utility_available);
    UpdateUtilityAffordanceVisibility();
  }
}

void MahoSidebarSearchView::UpdateSearchChrome() {
  const bool show_placeholder =
      !is_editing_ &&
      model_.display_mode == MahoSidebarAddressDisplayMode::kSearchPlaceholder;
  idle_button_->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface1,
      show_placeholder ? sidebar_layout::kSearchPillCompactCornerRadiusDp
                       : sidebar_layout::kSearchPillCornerRadiusDp));
  // Padded border provides the pill's inner leading/trailing breathing room.
  // The trailing side is intentionally wider than the leading side so the
  // utility affordance (trailing icon) doesn't hug the pill's right edge.
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
  if (model_.display_mode == MahoSidebarAddressDisplayMode::kSearchPlaceholder ||
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
      !model_.has_active_tab || !model_.translation_available) {
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

bool MahoSidebarSearchView::ShouldToggleUtilityPanelForEvent(
    const ui::Event& event) const {
  if (!idle_button_ || !utility_icon_slot_ || !utility_icon_ ||
      !utility_icon_slot_->GetVisible() || !CanShowUtilityAffordance()) {
    return false;
  }

  if (!event.IsLocatedEvent()) {
    return false;
  }

  const auto* located_event = event.AsLocatedEvent();
  if (!located_event) {
    return false;
  }

  // Primary check: click landed inside the utility icon slot itself.
  gfx::Point point_in_utility_slot = located_event->location();
  views::View::ConvertPointToTarget(idle_button_, utility_icon_slot_,
                                     &point_in_utility_slot);
  if (utility_icon_slot_->HitTestPoint(point_in_utility_slot)) {
    return true;
  }

  // Secondary check: click landed in the trailing inset — the visible area of
  // the pill that lies to the right of the utility icon slot (between the
  // slot's trailing edge and the pill's right border stroke). Live clicks in
  // this region are visually part of the utility affordance but fall outside
  // utility_icon_slot_'s bounds, so we extend the hit zone here.
  const gfx::Point& point_in_idle = located_event->location();
  const gfx::Rect slot_bounds_in_idle = utility_icon_slot_->bounds();
  const gfx::Rect idle_local_bounds = idle_button_->GetLocalBounds();
  const gfx::Rect trailing_inset_rect(
      slot_bounds_in_idle.right(),
      idle_local_bounds.y(),
      idle_local_bounds.right() - slot_bounds_in_idle.right(),
      idle_local_bounds.height());
  return trailing_inset_rect.Contains(point_in_idle);
}

bool MahoSidebarSearchView::ShouldRouteEventToTranslate(
    const ui::Event& event) const {
  if (!idle_button_ || !secondary_label_ || !secondary_label_->GetVisible() ||
      !model_.translation_available) {
    return false;
  }

  if (!event.IsLocatedEvent()) {
    return false;
  }

  const auto* located_event = event.AsLocatedEvent();
  if (!located_event) {
    return false;
  }

  gfx::Point point_in_label = located_event->location();
  views::View::ConvertPointToTarget(idle_button_, secondary_label_,
                                     &point_in_label);
  return secondary_label_->HitTestPoint(point_in_label);
}

}  // namespace maho
