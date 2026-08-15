// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/passwords/maho_password_save_update_view.h"

#include <memory>
#include <string>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/passwords/passwords_model_delegate.h"
#include "components/password_manager/core/browser/password_form.h"
#include "chrome/grit/generated_resources.h"
#include "components/url_formatter/elide_url.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "url/origin.h"

namespace maho::passwords {

namespace {

constexpr int kBubbleWidth = 360;
constexpr int kVaultIconSize = 18;
constexpr char16_t kSaveToMahoVault[] = u"Save to Maho Vault";
constexpr char16_t kNotNow[] = u"Not now";
constexpr char16_t kNeverForThisSite[] = u"Never for this site";
constexpr char16_t kUsernameAccessibleName[] = u"Username";
constexpr char16_t kPasswordAccessibleName[] = u"Password, masked";

std::u16string CanonicalOrigin(const url::Origin& origin) {
  if (origin.opaque()) {
    return std::u16string();
  }
  return url_formatter::FormatOriginForSecurityDisplay(
      origin, url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS);
}

std::u16string DisplayUsername(
    const password_manager::PasswordForm& password_form) {
  return password_form.username_value.empty() ? u"No username"
                                               : password_form.username_value;
}

std::u16string MaskPassword(
    const password_manager::PasswordForm& password_form) {
  return std::u16string(password_form.password_value.size(), u'\u2022');
}

std::unique_ptr<views::Label> CreateCredentialLabel(
    std::u16string text,
    std::u16string accessible_name) {
  auto label = std::make_unique<views::Label>(
      std::move(text), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_3);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetAccessibleName(std::move(accessible_name));
  return label;
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
  set_fixed_width(kBubbleWidth);

  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
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
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  origin_label_ = AddChildView(CreateCredentialLabel(
      CanonicalOrigin(url::Origin::Create(password_form.url)),
      u"Password site"));
  username_label_ = AddChildView(CreateCredentialLabel(
      DisplayUsername(password_form), kUsernameAccessibleName));
  password_label_ = AddChildView(CreateCredentialLabel(
      MaskPassword(password_form), kPasswordAccessibleName));

  if (!is_update_bubble_) {
    auto never_button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoPasswordSaveUpdateView::NeverForThisSite,
                            base::Unretained(this)),
        kNeverForThisSite);
    never_button->SetAccessibleName(kNeverForThisSite);
    never_button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
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
                                        ui::kColorIcon, kVaultIconSize);
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

BEGIN_METADATA(MahoPasswordSaveUpdateView)
END_METADATA

}  // namespace maho::passwords
