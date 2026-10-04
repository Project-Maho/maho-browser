// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/passwords/maho_password_save_update_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/passwords/passwords_model_delegate.h"
#include "chrome/grit/generated_resources.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/url_formatter/elide_url.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/theme/maho_design_tokens.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "url/origin.h"

namespace maho::passwords {

namespace {

constexpr int kBubbleWidth = 400;
constexpr int kVaultIconSize = 18;
constexpr int kSiteIconSize = 16;
constexpr int kDialogButtonHeight = 40;
constexpr int kPrimaryButtonMinWidth = 112;
constexpr int kSecondaryButtonMinWidth = 84;
constexpr int kInlineActionHeight = 32;
constexpr size_t kMaskedPasswordLength = 8;
constexpr char16_t kSaveToMahoVault[] = u"Save password";
constexpr char16_t kNotNow[] = u"Not now";
constexpr char16_t kNeverForThisSite[] = u"Never on this site";
constexpr char16_t kShowPassword[] = u"Show";
constexpr char16_t kHidePassword[] = u"Hide";
constexpr char16_t kShowPasswordAccessibleName[] = u"Show password";
constexpr char16_t kHidePasswordAccessibleName[] = u"Hide password";
constexpr char16_t kUsernameAccessibleName[] = u"Username";
constexpr char16_t kPasswordAccessibleName[] = u"Password, masked";
constexpr char16_t kPasswordVisibleAccessibleName[] = u"Password, visible";

const gfx::Insets kBubbleMargins = gfx::Insets::TLBR(
    tokens::kSpace2, tokens::kSpace5, tokens::kSpace4, tokens::kSpace5);
const gfx::Insets kCredentialFieldInsets =
    gfx::Insets::VH(tokens::kSpace3, tokens::kSpace4);

struct CredentialFieldViews {
  views::Label* value_label = nullptr;
  views::View* value_row = nullptr;
};

std::u16string CanonicalOrigin(const url::Origin& origin) {
  if (origin.opaque()) {
    return std::u16string();
  }
  return url_formatter::FormatOriginForSecurityDisplay(
      origin, url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS);
}

std::u16string DisplayOrigin(const password_manager::PasswordForm& password_form) {
  std::u16string origin =
      CanonicalOrigin(url::Origin::Create(password_form.url));
  return origin.empty() ? std::u16string(u"This site") : origin;
}

std::u16string DisplayUsername(
    const password_manager::PasswordForm& password_form) {
  return password_form.username_value.empty() ? u"No username"
                                               : password_form.username_value;
}

// A fixed-size mask deliberately avoids exposing password length through the
// confirmation UI while keeping the credential easy to scan visually.
std::u16string MaskPassword(
    const password_manager::PasswordForm& password_form) {
  return password_form.password_value.empty()
             ? std::u16string(u"No password")
             : std::u16string(kMaskedPasswordLength, u'\u2022');
}

CredentialFieldViews AddCredentialField(views::View* parent,
                                        std::u16string field_name,
                                        std::u16string value,
                                        std::u16string accessible_name) {
  auto* field = parent->AddChildView(std::make_unique<views::View>());
  auto* field_layout = field->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kCredentialFieldInsets,
                                         tokens::kSpace1));
  field_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* caption = field->AddChildView(std::make_unique<views::Label>(
      std::move(field_name), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM));
  caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  caption->SetEnabledColor(kMahoColorTertiaryText);

  auto* value_row = field->AddChildView(std::make_unique<views::View>());
  auto* value_layout = value_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), tokens::kSpace2));
  value_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* value_label = value_row->AddChildView(std::make_unique<views::Label>(
      std::move(value), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_3_MEDIUM));
  value_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  value_label->SetElideBehavior(gfx::ELIDE_TAIL);
  value_label->SetEnabledColor(kMahoColorPrimaryText);
  value_label->SetAccessibleName(std::move(accessible_name));
  value_layout->SetFlexForView(value_label, 1);

  return {.value_label = value_label, .value_row = value_row};
}

void AddCredentialDivider(views::View* parent) {
  auto* divider = parent->AddChildView(std::make_unique<views::View>());
  divider->SetPreferredSize(gfx::Size(1, 1));
  divider->SetBackground(
      views::CreateSolidBackground(kMahoColorCardBorder));
}

}  // namespace

bool ShouldUseMahoPasswordSaveUpdateView(EffectivePasswordProvider provider,
                                         PrefService* local_state) {
  return provider == EffectivePasswordProvider::kMahoNative &&
         IsNativePasswordWriteEnabled(local_state);
}

MahoPasswordSaveUpdateView::MahoPasswordSaveUpdateView(
    content::WebContents* web_contents,
    views::BubbleAnchor anchor_view,
    DisplayReason reason)
    : PasswordBubbleViewBase(web_contents,
                             anchor_view,
                             /*easily_dismissable=*/reason == USER_GESTURE),
      controller_(
          PasswordsModelDelegateFromWebContents(web_contents),
          reason == AUTOMATIC
              ? PasswordBubbleControllerBase::DisplayReason::kAutomatic
              : PasswordBubbleControllerBase::DisplayReason::kUserAction),
      is_update_bubble_(controller_.state() ==
                        password_manager::ui::PENDING_PASSWORD_UPDATE_STATE) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(controller_.state() == password_manager::ui::PENDING_PASSWORD_STATE ||
         controller_.state() ==
             password_manager::ui::PENDING_PASSWORD_UPDATE_STATE);

  const password_manager::PasswordForm& password_form =
      controller_.pending_password();

  SetTitle(controller_.GetTitle());
  SetSubtitle(u"Maho Vault");
  SetShowIcon(true);
  set_shadow(views::BubbleBorder::STANDARD_SHADOW);
  set_fixed_width(kBubbleWidth);
  set_corner_radius(tokens::kRadiusLg);
  set_margins(kBubbleMargins);

  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
  SetButtonLabel(
      ui::mojom::DialogButton::kOk,
      is_update_bubble_
          ? l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_UPDATE_BUTTON)
          : std::u16string(kSaveToMahoVault));
  SetButtonLabel(ui::mojom::DialogButton::kCancel, kNotNow);
  SetAcceptCallback(base::BindOnce(
      &MahoPasswordSaveUpdateView::SaveOrUpdate, base::Unretained(this)));
  SetCancelCallback(base::BindOnce(&MahoPasswordSaveUpdateView::CancelAction,
                                   base::Unretained(this)));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      tokens::kSpace3));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* site_row = AddChildView(std::make_unique<views::View>());
  auto* site_layout = site_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), tokens::kSpace2));
  site_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  site_row->AddChildView(std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(maho_lucide_icons::kGlobeIcon,
                                     kMahoColorSecondaryText, kSiteIconSize)));
  origin_label_ = site_row->AddChildView(std::make_unique<views::Label>(
      DisplayOrigin(password_form), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_4_MEDIUM));
  origin_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  origin_label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  origin_label_->SetEnabledColor(kMahoColorSecondaryText);
  origin_label_->SetAccessibleName(u"Password site");
  site_layout->SetFlexForView(origin_label_, 1);

  credential_card_ = AddChildView(std::make_unique<views::View>());
  credential_card_->SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCardBackground, tokens::kRadiusMd));
  credential_card_->SetBorder(views::CreateRoundedRectBorder(
      1, tokens::kRadiusMd, kMahoColorCardBorder));
  auto* card_layout = credential_card_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 0));
  card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  const CredentialFieldViews username_field =
      AddCredentialField(credential_card_, u"Username",
                         DisplayUsername(password_form),
                         kUsernameAccessibleName);
  username_label_ = username_field.value_label;
  AddCredentialDivider(credential_card_);
  const CredentialFieldViews password_field =
      AddCredentialField(credential_card_, u"Password",
                         MaskPassword(password_form), kPasswordAccessibleName);
  password_label_ = password_field.value_label;

  if (!password_form.password_value.empty()) {
    auto reveal_button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoPasswordSaveUpdateView::TogglePasswordVisibility,
                            base::Unretained(this)),
        kShowPassword);
    reveal_button->SetStyle(ui::ButtonStyle::kText);
    reveal_button->SetCornerRadius(tokens::kRadiusSm);
    reveal_button->SetMinSize(gfx::Size(0, kInlineActionHeight));
    reveal_button->SetEnabledTextColors(kMahoColorAccentBlue);
    reveal_button->SetAccessibleName(kShowPasswordAccessibleName);
    reveal_button->SetTooltipText(kShowPasswordAccessibleName);
    password_reveal_button_ =
        password_field.value_row->AddChildView(std::move(reveal_button));
  }

  if (!is_update_bubble_) {
    auto never_button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoPasswordSaveUpdateView::NeverForThisSite,
                            base::Unretained(this)),
        kNeverForThisSite);
    never_button->SetAccessibleName(kNeverForThisSite);
    never_button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    never_button->SetStyle(ui::ButtonStyle::kText);
    never_button->SetCornerRadius(tokens::kRadiusSm);
    never_button->SetMinSize(gfx::Size(0, kInlineActionHeight));
    never_button->SetEnabledTextColors(kMahoColorSecondaryText);
    never_button_ = SetFootnoteView(std::move(never_button));
  }
}

MahoPasswordSaveUpdateView::~MahoPasswordSaveUpdateView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

PasswordBubbleControllerBase* MahoPasswordSaveUpdateView::GetController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return &controller_;
}

const PasswordBubbleControllerBase*
MahoPasswordSaveUpdateView::GetController() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return &controller_;
}

views::View* MahoPasswordSaveUpdateView::GetInitiallyFocusedView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetOkButton();
}

ui::ImageModel MahoPasswordSaveUpdateView::GetWindowIcon() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ui::ImageModel::FromVectorIcon(maho_lucide_icons::kShieldCheckIcon,
                                        kMahoColorAccentBlue, kVaultIconSize);
}

void MahoPasswordSaveUpdateView::AddedToWidget() {
  PasswordBubbleViewBase::AddedToWidget();
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (auto* ok_button = GetOkButton()) {
    ok_button->SetStyle(ui::ButtonStyle::kProminent);
    ok_button->SetCornerRadius(tokens::kRadiusSm);
    ok_button->SetMinSize(
        gfx::Size(kPrimaryButtonMinWidth, kDialogButtonHeight));
  }
  if (auto* cancel_button = GetCancelButton()) {
    cancel_button->SetStyle(ui::ButtonStyle::kText);
    cancel_button->SetCornerRadius(tokens::kRadiusSm);
    cancel_button->SetMinSize(
        gfx::Size(kSecondaryButtonMinWidth, kDialogButtonHeight));
  }
}

void MahoPasswordSaveUpdateView::VisibilityChanged(views::View* starting_from,
                                                   bool is_visible) {
  PasswordBubbleViewBase::VisibilityChanged(starting_from, is_visible);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_visible) {
    if (auto* ok_button = GetOkButton()) {
      ok_button->SetStyle(ui::ButtonStyle::kProminent);
      ok_button->SetCornerRadius(tokens::kRadiusSm);
      ok_button->SetMinSize(
          gfx::Size(kPrimaryButtonMinWidth, kDialogButtonHeight));
    }
    if (auto* cancel_button = GetCancelButton()) {
      cancel_button->SetStyle(ui::ButtonStyle::kText);
      cancel_button->SetCornerRadius(tokens::kRadiusSm);
      cancel_button->SetMinSize(
          gfx::Size(kSecondaryButtonMinWidth, kDialogButtonHeight));
    }
  }
}

void MahoPasswordSaveUpdateView::SaveOrUpdate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  controller_.OnSaveClicked();
}

void MahoPasswordSaveUpdateView::CancelAction() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_update_bubble_) {
    controller_.OnNoThanksClicked();
  } else {
    controller_.OnNotNowClicked();
  }
}

void MahoPasswordSaveUpdateView::NeverForThisSite() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  controller_.OnNeverForThisSiteClicked();
  if (GetWidget()) {
    GetWidget()->CloseWithReason(
        views::Widget::ClosedReason::kCloseButtonClicked);
  }
}

void MahoPasswordSaveUpdateView::TogglePasswordVisibility() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!password_label_ || !password_reveal_button_) {
    return;
  }

  const password_manager::PasswordForm& password_form =
      controller_.pending_password();
  if (password_form.password_value.empty()) {
    return;
  }

  password_revealed_ = !password_revealed_;
  password_label_->SetText(password_revealed_ ? password_form.password_value.value()
                                              : MaskPassword(password_form));
  password_label_->SetAccessibleName(password_revealed_
                                         ? kPasswordVisibleAccessibleName
                                         : kPasswordAccessibleName);
  password_reveal_button_->SetText(password_revealed_ ? kHidePassword
                                                       : kShowPassword);
  password_reveal_button_->SetAccessibleName(
      password_revealed_ ? kHidePasswordAccessibleName
                         : kShowPasswordAccessibleName);
  password_reveal_button_->SetTooltipText(
      password_revealed_ ? kHidePasswordAccessibleName
                         : kShowPasswordAccessibleName);
}

BEGIN_METADATA(MahoPasswordSaveUpdateView)
END_METADATA

}  // namespace maho::passwords
