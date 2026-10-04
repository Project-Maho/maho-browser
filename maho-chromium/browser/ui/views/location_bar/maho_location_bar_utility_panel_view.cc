// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"

#include "maho/browser/ui/views/location_bar/maho_utility_panel_vibrancy.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/views/controls/button/md_text_button.h"
#include "build/build_config.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/security_state/core/security_state.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/ssl_status.h"
#if BUILDFLAG(IS_MAC)
#include "components/remote_cocoa/browser/window.h"
#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"
#endif
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_icon_image.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(MahoLocationBarUtilityPanelView,
                                 kUtilityPanelDelegateKey)

constexpr int kPanelCornerRadius = 18;
constexpr int kPanelWidth = 304;
constexpr int kPanelSectionSpacing = 14;
constexpr int kPanelOuterPadding = 14;
constexpr int kFooterTopPadding = 6;
constexpr int kFooterIconSize = 12;
constexpr int kZoneHeaderTopPadding = 2;
constexpr int kZoneHeaderBottomPadding = 8;
constexpr int kPanelChipButtonHeight = 32;
constexpr int kPanelChipButtonCornerRadius = 10;
constexpr int kPanelChipButtonHorizontalPadding = 10;
constexpr int kPanelChipButtonIconLabelSpacing = 6;
constexpr int kToolbarRowHeight = 36;
constexpr int kToolbarIconSize = 13;
constexpr int kToolbarIconSpacing = 8;
constexpr int kToolbarButtonCornerRadius = 10;
constexpr int kChipRowSpacing = 8;
constexpr int kChipHeight = 30;
constexpr int kChipCornerRadius = 10;
constexpr int kChipHorizontalPadding = 10;
constexpr int kAddChipWidth = 30;
constexpr int kPinButtonSize = 16;
constexpr int kPinIconSize = 10;
constexpr int kSettingsRowHeight = 38;
constexpr int kSettingsRowHorizontalPadding = 10;
constexpr int kSettingsRowCornerRadius = 10;
constexpr int kSettingsIconSize = 12;

bool FooterIsEmpty(const MahoLocationBarUtilityPanelFooter& footer) {
  return footer.title.empty() && footer.subtitle.empty();
}

std::u16string ActionTooltip(const MahoLocationBarUtilityPanelAction& action) {
  if (!action.accessible_name.empty()) {
    return action.accessible_name;
  }
  if (action.subtitle.empty()) {
    return action.title;
  }
  return action.title + u" · " + action.subtitle;
}

std::u16string BuildAccessibleName(
    const MahoLocationBarUtilityPanelAction& action) {
  if (!action.accessible_name.empty()) {
    return action.accessible_name;
  }
  if (action.subtitle.empty()) {
    return action.title;
  }
  return action.title + u" — " + action.subtitle;
}

std::u16string ComposeAccessibleName(const std::u16string& title,
                                     const std::u16string& subtitle) {
  if (subtitle.empty()) {
    return title;
  }
  return title + u" — " + subtitle;
}

bool IsDisabledAction(const MahoLocationBarUtilityPanelAction& action) {
  return !action.enabled;
}

class MahoLocationBarUtilityPanelActionView
    : public views::View,
      public extensions::ExtensionActionIconFactory::Observer,
      public extensions::IconImage::Observer {
  METADATA_HEADER(MahoLocationBarUtilityPanelActionView, views::View)

 public:
  enum class DisplayMode {
    kToolbarButton,
    kExtensionChip,
    kExtensionIconChip,
    kSettingsRow,
  };

  explicit MahoLocationBarUtilityPanelActionView(
      Browser* browser,
      MahoLocationBarUtilityPanelAction action,
      DisplayMode display_mode)
      : browser_(browser), action_(std::move(action)), display_mode_(display_mode) {
    const bool is_toolbar = display_mode_ == DisplayMode::kToolbarButton;
    const bool is_chip = display_mode_ == DisplayMode::kExtensionChip;
    const bool is_icon_chip = display_mode_ == DisplayMode::kExtensionIconChip;
    const bool is_settings = display_mode_ == DisplayMode::kSettingsRow;
    const bool has_inline_toggle = HasInlineToggle();
    const bool show_disclosure_indicator = ShouldShowDisclosureIndicator();
    const bool show_settings_subtitle = is_settings && !action_.subtitle.empty();
    SetFocusBehavior(action_.enabled && !has_inline_toggle ? FocusBehavior::ALWAYS
                                                           : FocusBehavior::NEVER);
    if (GetFocusBehavior() == FocusBehavior::ALWAYS) {
      views::FocusRing::Install(this);
    }
    SetNotifyEnterExitOnChild(true);
    if (is_toolbar) {
      SetPreferredSize(gfx::Size(kToolbarRowHeight, kToolbarRowHeight));
    } else if (is_settings) {
      SetPreferredSize(gfx::Size(0, kSettingsRowHeight));
    } else if (is_icon_chip) {
      SetPreferredSize(gfx::Size(kChipHeight, kChipHeight));
    } else if (is_chip && action_.title == u"+") {
      SetPreferredSize(gfx::Size(kAddChipWidth, kChipHeight));
    }

    const int corner_radius = is_toolbar   ? kToolbarButtonCornerRadius
                              : (is_chip || is_icon_chip) ? kChipCornerRadius
                              : is_settings ? kSettingsRowCornerRadius
                              : 0;
    SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface1,
        corner_radius));

    if (IsDisabledAction(action_)) {
      SetPaintToLayer();
      layer()->SetOpacity(0.55f);
    }

    selection_background_ = AddChildView(std::make_unique<views::View>());
    selection_background_->SetVisible(false);
    selection_background_->SetCanProcessEventsWithinSubtree(false);
    selection_background_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface2,
        corner_radius));

    auto* content = AddChildView(std::make_unique<views::View>());
    content_view_ = content;
    if (!has_inline_toggle) {
      content->SetCanProcessEventsWithinSubtree(false);
    }
    auto* layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, is_toolbar   ? 0
                           : (is_chip || is_icon_chip) ? kChipHorizontalPadding
                           : is_settings ? kSettingsRowHorizontalPadding
                           : 0),
        is_toolbar   ? 0
        : (is_chip || is_icon_chip) ? kToolbarIconSpacing
        : is_settings ? kToolbarIconSpacing
                      : 0));
    if (is_toolbar || is_icon_chip) {
      layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kCenter);
    }
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    views::View* icon_parent = content;
    const int icon_size = is_toolbar   ? kToolbarIconSize
                         : is_icon_chip ? 16
                         : is_chip     ? kSettingsIconSize
                         : is_settings ? kSettingsIconSize
                         : 0;

    auto* icon_image = icon_parent->AddChildView(std::make_unique<views::ImageView>());
    icon_image->SetImageSize(gfx::Size(icon_size, icon_size));
    icon_image->SetCanProcessEventsWithinSubtree(false);
    icon_image_view_ = icon_image;

    if (is_icon_chip && browser_ && !action_.extension_id.empty()) {
      Profile* profile = browser_->GetProfile();
      auto* registry = extensions::ExtensionRegistry::Get(profile);
      const extensions::Extension* extension =
          registry ? registry->GetInstalledExtension(action_.extension_id) : nullptr;
      if (extension) {
        auto* action_manager = extensions::ExtensionActionManager::Get(profile);
        extensions::ExtensionAction* extension_action =
            action_manager ? action_manager->GetExtensionAction(*extension) : nullptr;
        if (extension_action) {
          icon_factory_ = std::make_unique<extensions::ExtensionActionIconFactory>(
              extension, extension_action, this);
        } else {
          icon_image_ = std::make_unique<extensions::IconImage>(
              profile, extension, extensions::IconsInfo::GetIcons(extension),
              icon_size, gfx::ImageSkia(), this);
        }
      }
    }

    if (is_icon_chip && (icon_factory_ || icon_image_)) {
      UpdateIconImage();
    } else {
      icon_image->SetImage(action_.icon
          ? ui::ImageModel::FromVectorIcon(*action_.icon,
                                           ui::kColorSysOnSurfaceSubtle,
                                           icon_size)
          : ui::ImageModel());
    }

    if (is_icon_chip && action_.pin_enabled && !action_.pin_callback.is_null()) {
      pin_button_ = AddChildView(std::make_unique<views::ImageButton>(
          base::BindRepeating(
              &MahoLocationBarUtilityPanelActionView::OnPinButtonPressed,
              base::Unretained(this))));
      pin_button_->SetInstallFocusRingOnFocus(false);
      views::InkDrop::Get(pin_button_)->SetMode(
          views::InkDropHost::InkDropMode::OFF);
      UpdatePinButton();
    }

    if (!is_toolbar && !is_icon_chip) {
      auto* text_column = content->AddChildView(std::make_unique<views::View>());
      auto* text_layout = text_column->SetLayoutManager(
          std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                             gfx::Insets(),
                                             2));
      text_layout->set_cross_axis_alignment(
           views::BoxLayout::CrossAxisAlignment::kStart);
      text_layout->set_main_axis_alignment(
           views::BoxLayout::MainAxisAlignment::kCenter);
      layout->SetFlexForView(text_column, 1);

        auto* title_label = text_column->AddChildView(std::make_unique<views::Label>(
            action_.title, views::style::CONTEXT_LABEL,
            is_chip || is_settings ? views::style::STYLE_BODY_4
                                   : views::style::STYLE_BODY_4));
      title_label->SetAutoColorReadabilityEnabled(false);
      title_label->SetEnabledColor(ui::kColorSysOnSurface);
      title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      title_label->SetMultiLine(false);
      title_label->SetElideBehavior(gfx::ELIDE_TAIL);

        if (show_settings_subtitle) {
          subtitle_label_ = text_column->AddChildView(
              std::make_unique<views::Label>(action_.subtitle,
                                             views::style::CONTEXT_LABEL,
                                             views::style::STYLE_BODY_5));
          subtitle_label_->SetAutoColorReadabilityEnabled(false);
          subtitle_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
          subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
          subtitle_label_->SetMultiLine(false);
          subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);
        }
    }

    if (has_inline_toggle) {
      toggle_button_ = content->AddChildView(std::make_unique<views::ToggleButton>(
          base::BindRepeating(
              &MahoLocationBarUtilityPanelActionView::OnInlineTogglePressed,
              base::Unretained(this))));
      toggle_button_->SetFocusBehavior(FocusBehavior::ALWAYS);
      toggle_button_->SetAccessibleName(BuildAccessibleName(action_));
      toggle_button_->SetIsOn(action_.toggle_is_on);
    }

    if (show_disclosure_indicator) {
      auto* disclosure =
          content->AddChildView(std::make_unique<views::ImageView>());
      disclosure->SetImageSize(gfx::Size(kSettingsIconSize, kSettingsIconSize));
      disclosure->SetImage(ui::ImageModel::FromVectorIcon(
          maho_lucide_icons::kChevronRightIcon,
          ui::kColorSysOnSurfaceSubtle, kSettingsIconSize));
      disclosure->SetCanProcessEventsWithinSubtree(false);
    }

    GetViewAccessibility().SetRole(has_inline_toggle ? ax::mojom::Role::kGroup
                                                     : ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(BuildAccessibleName(action_));
    SetTooltipText(ActionTooltip(action_));
    CHECK(!icon_factory_ || !icon_image_);
    if (!action_.extension_id.empty()) {
      // Keep enabled in Views framework to receive right-clicks for the context menu,
      // but interactions are blocked via action_.enabled checks in event handlers.
      SetEnabled(true);
    } else {
      SetEnabled(action_.enabled);
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& size_bounds) const override {
    if (display_mode_ != DisplayMode::kExtensionChip || action_.title == u"+") {
      return views::View::CalculatePreferredSize(size_bounds);
    }

    gfx::Size preferred_size = views::View::CalculatePreferredSize(size_bounds);
    preferred_size.set_height(kChipHeight);
    return preferred_size;
  }

  void Layout(PassKey) override {
    const gfx::Rect bounds = GetLocalBounds();
    if (selection_background_) {
      selection_background_->SetBoundsRect(bounds);
    }
    if (content_view_) {
      content_view_->SetBoundsRect(bounds);
    }
    if (pin_button_) {
      pin_button_->SetBounds(bounds.right() - kPinButtonSize - 1, 1,
                             kPinButtonSize, kPinButtonSize);
    }
  }

  void SetHighlighted(bool highlighted) { selection_background_->SetVisible(highlighted); }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    if (!action_.enabled) {
      return;
    }
    SetHighlighted(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    if (!action_.enabled) {
      return;
    }
    SetHighlighted(false);
  }

  void OnFocus() override {
    views::View::OnFocus();
    if (action_.enabled) {
      SetHighlighted(true);
    }
  }

  void OnBlur() override {
    views::View::OnBlur();
    if (action_.enabled) {
      SetHighlighted(false);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!action_.enabled || !event.IsOnlyLeftMouseButton()) {
      return false;
    }

    if (HasInlineToggle()) {
      if (!toggle_button_) {
        return false;
      }

      toggle_button_->SetIsOn(!toggle_button_->GetIsOn());
      OnInlineTogglePressed();
      return true;
    }

    if (!HasPrimaryCallback()) {
      return false;
    }

    RunPrimaryCallback();
    if (action_.close_after_activate) {
      if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
        widget->CloseWithReason(
            views::Widget::ClosedReason::kAcceptButtonClicked);
      }
    }
    return true;
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (HasInlineToggle() || !action_.enabled || !HasPrimaryCallback()) {
      return false;
    }

    switch (event.key_code()) {
      case ui::VKEY_RETURN:
      case ui::VKEY_SPACE:
        RunPrimaryCallback();
        if (action_.close_after_activate) {
          if (views::Widget* widget = GetWidget(); widget &&
                                                  !widget->IsClosed()) {
            widget->CloseWithReason(
                views::Widget::ClosedReason::kAcceptButtonClicked);
          }
        }
        return true;
      default:
        return false;
    }
  }

 private:
  bool HasPrimaryCallback() const {
    return !action_.anchor_callback.is_null() || !action_.callback.is_null();
  }

  void RunPrimaryCallback() {
    if (!action_.anchor_callback.is_null()) {
      action_.anchor_callback.Run(this);
      return;
    }
    action_.callback.Run();
  }

  bool HasInlineToggle() const {
    return display_mode_ == DisplayMode::kSettingsRow && action_.has_toggle;
  }

  bool ShouldShowDisclosureIndicator() const {
    return display_mode_ == DisplayMode::kSettingsRow &&
           action_.show_disclosure_indicator && !HasInlineToggle() &&
           !action_.anchor_callback.is_null();
  }

  std::u16string GetSubtitleForCurrentToggleState() const {
    return action_.toggle_is_on ? action_.toggle_on_subtitle
                                : action_.toggle_off_subtitle;
  }

  void UpdateInlineTogglePresentation(
      const std::u16string& effective_subtitle = std::u16string()) {
    if (!HasInlineToggle()) {
      return;
    }

    action_.subtitle = effective_subtitle.empty()
                           ? GetSubtitleForCurrentToggleState()
                           : effective_subtitle;
    action_.accessible_name =
        ComposeAccessibleName(action_.title, action_.subtitle);
    if (subtitle_label_) {
      subtitle_label_->SetText(action_.subtitle);
    }
    if (toggle_button_) {
      toggle_button_->SetIsOn(action_.toggle_is_on);
      toggle_button_->SetAccessibleName(action_.accessible_name);
    }
    GetViewAccessibility().SetName(action_.accessible_name);
    SetTooltipText(ActionTooltip(action_));
  }

  void OnInlineTogglePressed() {
    if (!HasInlineToggle() || !toggle_button_) {
      return;
    }

    const bool requested_on = toggle_button_->GetIsOn();
    action_.toggle_is_on = requested_on;
    std::u16string effective_subtitle;
    if (!action_.toggle_callback.is_null()) {
      // The callback performs the write and reports the *effective* state back
      // via out-params (e.g. Auto-PiP can remain blocked under embargo), so the
      // row reflects the real resulting setting rather than the optimistic
      // request.
      bool effective_on = requested_on;
      action_.toggle_callback.Run(requested_on, effective_on,
                                  effective_subtitle);
      action_.toggle_is_on = effective_on;
    }
    UpdateInlineTogglePresentation(effective_subtitle);
  }

  void OnPinButtonPressed(const ui::Event& event) {
    if (action_.pin_callback.is_null()) {
      return;
    }
    action_.is_pinned = !action_.is_pinned;
    action_.pin_callback.Run(action_.is_pinned);
    UpdatePinButton();
  }

  void UpdatePinButton() {
    if (!pin_button_) {
      return;
    }
    pin_button_->SetTooltipText(action_.is_pinned ? u"Unpin extension"
                                                   : u"Pin extension");
    pin_button_->GetViewAccessibility().SetName(
        action_.is_pinned ? u"Unpin extension" : u"Pin extension");
    pin_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(
            maho_lucide_icons::kPinIcon,
            action_.is_pinned ? ui::kColorSysPrimary
                              : ui::kColorSysOnSurfaceSubtle,
            kPinIconSize));
  }

  void UpdateIconImage() {
    if (!icon_image_view_) {
      return;
    }
    gfx::Image image;
    if (icon_factory_) {
      int tab_id = -1;
      if (browser_) {
        if (auto* web_contents = browser_->GetTabStripModel()->GetActiveWebContents()) {
          SessionID session_id = sessions::SessionTabHelper::IdForTab(web_contents);
          if (session_id.is_valid()) {
            tab_id = session_id.id();
          }
        }
      }
      image = icon_factory_->GetIcon(tab_id);
    } else if (icon_image_) {
      image = icon_image_->image();
    }

    if (!image.IsEmpty()) {
      icon_image_view_->SetImage(ui::ImageModel::FromImageSkia(image.AsImageSkia()));
    } else {
      icon_image_view_->SetImage(ui::ImageModel());
    }
  }


  // extensions::ExtensionActionIconFactory::Observer:
  void OnIconUpdated() override {
    UpdateIconImage();
  }

  // extensions::IconImage::Observer:
  void OnExtensionIconImageChanged(extensions::IconImage* image) override {
    UpdateIconImage();
  }

  void ShowContextMenu(const gfx::Point& p,
                       ui::mojom::MenuSourceType source_type) override {
    if (action_.context_menu_callback) {
      action_.context_menu_callback.Run(this);
    }
  }

  raw_ptr<Browser> browser_ = nullptr;
  MahoLocationBarUtilityPanelAction action_;
  DisplayMode display_mode_;
  raw_ptr<views::View> selection_background_ = nullptr;
  raw_ptr<views::View> content_view_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::ToggleButton> toggle_button_ = nullptr;
  raw_ptr<views::ImageButton> pin_button_ = nullptr;
  raw_ptr<views::ImageView> icon_image_view_ = nullptr;
  std::unique_ptr<extensions::ExtensionActionIconFactory> icon_factory_;
  std::unique_ptr<extensions::IconImage> icon_image_;
};

BEGIN_METADATA(MahoLocationBarUtilityPanelActionView)
END_METADATA

class PanelChipButton : public views::LabelButton {
  METADATA_HEADER(PanelChipButton, views::LabelButton)

 public:
  PanelChipButton(PressedCallback callback,
                  const std::u16string& text,
                  const gfx::VectorIcon* icon,
                  int icon_size,
                  bool text_uses_accent,
                  const std::u16string& accessible_name)
      : views::LabelButton(std::move(callback), text) {
    SetTextSubpixelRenderingEnabled(false);
    SetInstallFocusRingOnFocus(true);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    SetHorizontalAlignment(text.empty() ? gfx::ALIGN_CENTER : gfx::ALIGN_LEFT);
    SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(0, kPanelChipButtonHorizontalPadding)));
    label()->SetTextContext(views::style::CONTEXT_LABEL);
    label()->SetTextStyle(views::style::STYLE_BODY_5);
    const ui::ColorId text_color = text_uses_accent
                                       ? ui::kColorSysPrimary
                                       : ui::kColorSysOnSurfaceSubtle;
    const ui::ColorId text_color_active = text_uses_accent
                                              ? ui::kColorSysPrimary
                                              : ui::kColorSysOnSurface;
    SetTextColor(views::Button::STATE_NORMAL, text_color);
    SetTextColor(views::Button::STATE_HOVERED, text_color_active);
    SetTextColor(views::Button::STATE_PRESSED, text_color_active);
    if (icon) {
      SetImageLabelSpacing(text.empty() ? 0 : kPanelChipButtonIconLabelSpacing);
      SetImageModel(views::Button::STATE_NORMAL,
                    ui::ImageModel::FromVectorIcon(
                        *icon, ui::kColorSysOnSurfaceSubtle, icon_size));
      SetImageModel(views::Button::STATE_HOVERED,
                    ui::ImageModel::FromVectorIcon(
                        *icon, ui::kColorSysOnSurface, icon_size));
      SetImageModel(views::Button::STATE_PRESSED,
                    ui::ImageModel::FromVectorIcon(
                        *icon, ui::kColorSysOnSurface, icon_size));
    }
    if (text.empty() && icon) {
      SetPreferredSize(gfx::Size(kPanelChipButtonHeight, kPanelChipButtonHeight));
    } else {
      SetMinSize(gfx::Size(0, kPanelChipButtonHeight));
    }
    UpdateBackground();
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    const std::u16string& a11y_name =
        !accessible_name.empty() ? accessible_name : text;
    GetViewAccessibility().SetName(a11y_name);
    SetTooltipText(a11y_name);
  }
  PanelChipButton(const PanelChipButton&) = delete;
  PanelChipButton& operator=(const PanelChipButton&) = delete;
  ~PanelChipButton() override = default;

  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateBackground();
  }

 private:
  void UpdateBackground() {
    const bool active =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBackground(views::CreateRoundedRectBackground(
        active ? ui::kColorSysSurface2 : ui::kColorSysSurface1,
        kPanelChipButtonCornerRadius));
  }
};

BEGIN_METADATA(PanelChipButton)
END_METADATA

void DisableSubpixelRenderingRecursive(views::View* root) {
  if (!root) return;
  if (auto* label = views::AsViewClass<views::Label>(root)) {
    label->SetSubpixelRenderingEnabled(false);
  }
  for (views::View* child : root->children()) {
    DisableSubpixelRenderingRecursive(child);
  }
}

}  // namespace

MahoLocationBarUtilityPanelView::MahoLocationBarUtilityPanelView(
    views::View* anchor_view,
    MahoLocationBarUtilityPanelModel model)
    : views::BubbleDialogDelegate(anchor_view, views::BubbleBorder::TOP_RIGHT),
      model_(std::move(model)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_close_on_deactivate(true);
  set_margins(gfx::Insets());
  set_fixed_width(kPanelWidth);
  set_shadow(views::BubbleBorder::NO_SHADOW);
  SetBackgroundColor(SkColorSetARGB(0, 0, 0, 0));

  contents_view_ = SetContentsView(std::make_unique<views::View>());
  contents_view_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  contents_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  contents_view_->GetViewAccessibility().SetName(u"Location bar utility panel");
  contents_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPanelOuterPadding), kPanelSectionSpacing));

  contents_view_->SetBorder(views::CreateRoundedRectBorder(
      1, kPanelCornerRadius, ui::kColorSysSurfaceVariant));

  BuildContents();
  DisableSubpixelRenderingRecursive(contents_view_);

  if (model_.owning_browser) {
    if (auto* bridge = MahoExtensionStateBridge::FromProfile(
            model_.owning_browser->GetProfile())) {
      bridge_observation_.Observe(bridge);
    }
  }
}

MahoLocationBarUtilityPanelView::~MahoLocationBarUtilityPanelView() {
  // Stop observing before any further teardown so that a bridge notification
  // posted just before shutdown cannot fire into a half-destroyed view.
  bridge_observation_.Reset();

  // Cancel any live context-menu runner and release the model deterministically.
  // MenuRunner holds a raw pointer to the model; resetting runner first avoids
  // a dangling reference during the model's destructor.
  context_menu_runner_.reset();
  current_menu_model_.reset();
}

views::View* MahoLocationBarUtilityPanelView::GetActionViewForTesting(
    const std::u16string& title) const {
  auto it = action_views_for_testing_.find(title);
  return it == action_views_for_testing_.end() ? nullptr : it->second.get();
}

views::Widget* MahoLocationBarUtilityPanelView::Show(
    views::View* anchor_view,
    MahoLocationBarUtilityPanelModel model) {
  if (!anchor_view) {
    return nullptr;
  }

  auto bubble = std::make_unique<MahoLocationBarUtilityPanelView>(
      anchor_view, std::move(model));
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      bubble.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (widget) {
    // Keep the delegate alive through Widget teardown, including CloseNow()
    // and native-parent destruction, without a second deletion owner.
    widget->SetProperty(kUtilityPanelDelegateKey, std::move(bubble));
    ApplyVibrancyToUtilityPanel(widget);
    widget->Show();
    widget->widget_delegate()->GetContentsView()->RequestFocus();
  }
  return widget;
}

void MahoLocationBarUtilityPanelView::BuildContents() {
  for (const auto& zone : model_.zones) {
    AddZone(zone);
  }

  if (!FooterIsEmpty(model_.footer)) {
    AddFooterSummary();
  }
}

void MahoLocationBarUtilityPanelView::AddZone(const MahoUtilityPanelZone& zone) {
  if (zone.id == MahoUtilityPanelZoneId::kExtensions) {
    extensions_container_ = contents_view_->AddChildView(std::make_unique<views::View>());
    extensions_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    BuildExtensionsZoneContents(zone);
    return;
  }

  auto* zone_container = contents_view_->AddChildView(std::make_unique<views::View>());
  zone_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  AddZoneHeader(zone.header_label, zone.header_trailing_action, zone_container);
  switch (zone.style) {
    case MahoUtilityPanelZoneStyle::kToolbarRow:
      AddToolbarRowZone(zone, zone_container);
      return;
    case MahoUtilityPanelZoneStyle::kChipRow:
      AddChipRowZone(zone, zone_container);
      return;
    case MahoUtilityPanelZoneStyle::kSettingsList:
      AddSettingsListZone(zone, zone_container);
      return;
  }
}

void MahoLocationBarUtilityPanelView::BuildExtensionsZoneContents(
    const MahoUtilityPanelZone& zone) {
  DCHECK(extensions_container_);
  for (const auto& item : zone.items) {
    action_views_for_testing_.erase(item.title);
  }
  AddZoneHeader(zone.header_label, zone.header_trailing_action, extensions_container_);
  AddChipRowZone(zone, extensions_container_);
}

void MahoLocationBarUtilityPanelView::AddZoneHeader(
    const std::u16string& header_label,
    const std::optional<MahoLocationBarUtilityPanelAction>& trailing_action,
    views::View* parent_view) {
  if (header_label.empty()) {
    return;
  }

  if (trailing_action.has_value()) {
    auto* container = parent_view->AddChildView(std::make_unique<views::View>());
    auto* layout = container->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kZoneHeaderTopPadding, 2, kZoneHeaderBottomPadding, 2),
        0));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* header = container->AddChildView(std::make_unique<views::Label>(
        header_label, views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_3_MEDIUM));
    header->SetAutoColorReadabilityEnabled(false);
    header->SetEnabledColor(ui::kColorSysOnSurface);
    header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->SetFlexForView(header, 1);
    auto* trailing_button = container->AddChildView(std::make_unique<PanelChipButton>(
        base::BindRepeating(&MahoLocationBarUtilityPanelView::OnManageButtonClicked,
                            base::Unretained(this), *trailing_action),
        trailing_action->title,
        /*icon=*/nullptr,
        /*icon_size=*/0,
        /*text_uses_accent=*/false,
        /*accessible_name=*/std::u16string()));
    RegisterActionViewForTesting(*trailing_action, trailing_button);
  } else {
    auto* header = parent_view->AddChildView(std::make_unique<views::Label>(
        header_label, views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_3_MEDIUM));
    header->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(kZoneHeaderTopPadding, 2, kZoneHeaderBottomPadding, 2)));
    header->SetAutoColorReadabilityEnabled(false);
    header->SetEnabledColor(ui::kColorSysOnSurface);
    header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
}

void MahoLocationBarUtilityPanelView::AddToolbarRowZone(
    const MahoUtilityPanelZone& zone,
    views::View* parent_view) {
  if (zone.items.empty()) {
    return;
  }

  auto* section = parent_view->AddChildView(std::make_unique<views::View>());
  auto* layout = section->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kToolbarIconSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (const auto& action : zone.items) {
    auto* action_view = section->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            model_.owning_browser, action,
            MahoLocationBarUtilityPanelActionView::DisplayMode::kToolbarButton));
    RegisterActionViewForTesting(action, action_view);
    layout->SetFlexForView(action_view, 1);
  }
}

void MahoLocationBarUtilityPanelView::AddChipRowZone(
    const MahoUtilityPanelZone& zone,
    views::View* parent_view) {
  if (zone.items.empty()) {
    return;
  }

  auto* section = parent_view->AddChildView(std::make_unique<views::View>());
  auto* layout = section->SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetMainAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetInteriorMargin(gfx::Insets());
  layout->SetDefault(views::kMarginsKey,
                     gfx::Insets::VH(0, kChipRowSpacing / 2));

  for (const auto& action : zone.items) {
    const bool is_real_ext = !action.extension_id.empty();
    auto* action_view = section->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            model_.owning_browser, action,
            is_real_ext
                ? MahoLocationBarUtilityPanelActionView::DisplayMode::kExtensionIconChip
                : MahoLocationBarUtilityPanelActionView::DisplayMode::kExtensionChip));
    RegisterActionViewForTesting(action, action_view);
  }
}

void MahoLocationBarUtilityPanelView::AddSettingsListZone(
    const MahoUtilityPanelZone& zone,
    views::View* parent_view) {
  if (zone.items.empty()) {
    return;
  }

  auto* section = parent_view->AddChildView(std::make_unique<views::View>());
  auto* layout = section->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  for (const auto& action : zone.items) {
    auto* action_view = section->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            model_.owning_browser, action,
            MahoLocationBarUtilityPanelActionView::DisplayMode::kSettingsRow));
    RegisterActionViewForTesting(action, action_view);
  }
}

void MahoLocationBarUtilityPanelView::OnInstalledExtensionsChanged() {
  if (rebuilding_extensions_) {
    return;
  }
  if (!model_.owning_browser) {
    return;
  }

  views::Widget* widget = GetWidget();
  if (!widget || widget->IsClosed()) {
    return;
  }

  base::AutoReset<bool> auto_reset(&rebuilding_extensions_, true);

  if (extensions_container_) {
    // Cancel any open context menu before removing the chip views it is
    // anchored to, to prevent the menu runner from referencing destroyed
    // child views.
    context_menu_runner_.reset();
    current_menu_model_.reset();

    for (auto it = action_views_for_testing_.begin(); it != action_views_for_testing_.end(); ) {
      if (it->second && extensions_container_->Contains(it->second)) {
        it = action_views_for_testing_.erase(it);
      } else {
        ++it;
      }
    }
    extensions_container_->RemoveAllChildViews();
    MahoUtilityPanelZone zone = internal::BuildExtensionsZone(model_.owning_browser);
    BuildExtensionsZoneContents(zone);
    contents_view_->InvalidateLayout();
    SizeToContents();
  }
}

void MahoLocationBarUtilityPanelView::RunExtensionContextMenu(
    std::unique_ptr<extensions::ExtensionContextMenuModel> menu_model,
    views::View* anchor) {
  if (!anchor || !anchor->GetWidget()) {
    return;
  }

  const int generation = ++menu_generation_;
  context_menu_runner_.reset();
  current_menu_model_ = std::move(menu_model);
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      current_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&MahoLocationBarUtilityPanelView::OnMenuClosed,
                          base::Unretained(this), generation));

  context_menu_runner_->RunMenuAt(
      anchor->GetWidget(), nullptr, anchor->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
}

void MahoLocationBarUtilityPanelView::AddFooterSummary() {
  auto* footer = contents_view_->AddChildView(std::make_unique<views::View>());
  footer->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(kFooterTopPadding, 0, 0, 0)));
  auto* footer_layout = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kToolbarIconSpacing));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  bool has_cert = false;
  if (model_.owning_browser) {
    if (auto* web_contents =
            model_.owning_browser->GetTabStripModel()->GetActiveWebContents()) {
      if (auto* entry = web_contents->GetController().GetVisibleEntry()) {
        has_cert = entry->GetSSL().certificate != nullptr;
      }
    }
  }

  footer->AddChildView(std::make_unique<PanelChipButton>(
      base::BindRepeating(
          &MahoLocationBarUtilityPanelView::OnSecuritySummaryClicked,
          weak_factory_.GetWeakPtr()),
      model_.footer.title,
      model_.footer.icon,
      kFooterIconSize,
      /*text_uses_accent=*/false,
      /*accessible_name=*/std::u16string()))
      ->SetEnabled(
          model_.footer.data_state ==
              MahoLocationBarUtilityPanelDataState::kLive &&
          model_.footer.security_level !=
              static_cast<int>(security_state::NONE) &&
          has_cert);
}

void MahoLocationBarUtilityPanelView::RegisterActionViewForTesting(
    const MahoLocationBarUtilityPanelAction& action,
    views::View* view) {
  if (!action.title.empty() && view) {
    action_views_for_testing_[action.title] = view;
  }
}

void MahoLocationBarUtilityPanelView::OnMenuClosed(int generation) {
  if (generation != menu_generation_) {
    return;
  }
  context_menu_runner_.reset();
  current_menu_model_.reset();
}

void MahoLocationBarUtilityPanelView::OnSecuritySummaryClicked() {
  if (!model_.owning_browser) {
    return;
  }
  auto* web_contents =
      model_.owning_browser->GetTabStripModel()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  content::NavigationEntry* entry =
      web_contents->GetController().GetVisibleEntry();
  if (!entry) {
    return;
  }
  scoped_refptr<net::X509Certificate> cert = entry->GetSSL().certificate;
  if (!cert) {
    return;
  }
  gfx::NativeWindow parent = web_contents->GetTopLevelNativeWindow();
#if BUILDFLAG(IS_MAC)
  if (auto* mojo_window = remote_cocoa::GetWindowMojoInterface(parent)) {
    mojo_window->ShowCertificateViewer(cert);
    if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
      widget->Close();
    }
    return;
  }
#endif
  ShowCertificateViewer(web_contents, parent, cert.get());
}

void MahoLocationBarUtilityPanelView::OnManageButtonClicked(
    MahoLocationBarUtilityPanelAction action) {
  if (!action.callback.is_null()) {
    action.callback.Run();
  }
  if (action.close_after_activate) {
    if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
      widget->CloseWithReason(
          views::Widget::ClosedReason::kAcceptButtonClicked);
    }
  }
}

}  // namespace maho
