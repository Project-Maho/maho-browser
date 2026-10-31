// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/chromium_src/chrome/browser/ui/views/autofill/popup/popup_row_view.h"

#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/theme/maho_design_tokens.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"

#include "ui/views/view_class_properties.h"

// PopupRowView has exactly one unqualified row-margin write and two unqualified
// background writes. Route only those calls through Maho hooks while leaving
// Chromium's selection, accessibility, controller, and input implementation
// untouched. Because Chromium re-applies its background on selection changes,
// these hooks stay synchronized with hover/selection state automatically.
#define SetProperty(...) SetMahoMargins()
#define SetBackground(...) SetMahoBackground(__VA_ARGS__)
#include "../src/chrome/browser/ui/views/autofill/popup/popup_row_view.cc"
#undef SetBackground
#undef SetProperty

namespace autofill {

namespace {

constexpr int kMahoPasswordRowCornerRadius = maho::tokens::kRadiusMd;
constexpr int kMahoPasswordRowVerticalMargin = maho::tokens::kSpace1;

bool IsPasswordCredentialSuggestion(
    base::WeakPtr<AutofillPopupController> controller,
    int line_number) {
  return controller && line_number >= 0 &&
         line_number < controller->GetLineCount() &&
         controller->GetSuggestionAt(line_number).type ==
             SuggestionType::kPasswordEntry;
}

void StylePasswordLabels(views::View* root, bool* has_primary_label) {
  if (!root || !has_primary_label) {
    return;
  }

  if (auto* label = views::AsViewClass<views::Label>(root)) {
    if (!*has_primary_label) {
      label->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
      label->SetEnabledColor(kMahoColorPrimaryText);
      *has_primary_label = true;
    } else {
      label->SetTextStyle(views::style::STYLE_BODY_4);
      label->SetEnabledColor(kMahoColorSecondaryText);
    }
  }

  for (const auto& child : root->children()) {
    StylePasswordLabels(child.get(), has_primary_label);
  }
}

}  // namespace

void PopupRowView::SetMahoMargins() {
  const bool is_password_credential =
      IsPasswordCredentialSuggestion(controller_, line_number_);
  views::View::SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(is_password_credential ? kMahoPasswordRowVerticalMargin
                                             : 0,
                      GetHorizontalMargin()));
}

void PopupRowView::SetMahoBackground(std::unique_ptr<views::Background> background) {
  const bool is_password_credential =
      IsPasswordCredentialSuggestion(controller_, line_number_);

  // The constructor writes its first background before content_view_ is
  // attached. Preserve Chromium's initial surface for every non-credential
  // product; actual password entries start as a contained credential card.
  if (!content_view_) {
    if (is_password_credential) {
      views::View::SetBackground(views::CreateRoundedRectBackground(
          ui::ColorId(kMahoColorCardBackground), kMahoPasswordRowCornerRadius));
      SetBorder(views::CreateRoundedRectBorder(
          1, kMahoPasswordRowCornerRadius, ui::ColorId(kMahoColorCardBorder)));
    } else {
      views::View::SetBackground(std::move(background));
    }
    return;
  }

  const bool chromium_row_highlighted = [&]() {
    if (child_suggestions_displayed_) {
      return true;
    }
    if (selected_cell_ == CellType::kControl) {
      return true;
    }
    return !suggestion_is_acceptable_ && selected_cell_.has_value();
  }();

  // Credential suggestions read visually as one card. Selecting either the
  // content or control highlights the complete row instead of producing a
  // split-surface state. Non-password rows retain Chromium's exact behavior.
  const bool maho_is_highlighted =
      is_password_credential
          ? (child_suggestions_displayed_ || selected_cell_.has_value())
          : chromium_row_highlighted;

  if (is_password_credential) {
    views::View::SetBackground(views::CreateRoundedRectBackground(
        maho_is_highlighted ? ui::ColorId(ui::kColorDropdownBackgroundSelected)
                            : ui::ColorId(kMahoColorCardBackground),
        kMahoPasswordRowCornerRadius));
    SetBorder(views::CreateRoundedRectBorder(
        1, kMahoPasswordRowCornerRadius,
        maho_is_highlighted ? ui::ColorId(kMahoColorAccentBlue)
                            : ui::ColorId(kMahoColorCardBorder)));
    ApplyMahoPasswordTypography();
    return;
  }

  views::View::SetBackground(std::move(background));
}

void PopupRowView::ApplyMahoPasswordTypography() {
  if (!IsPasswordCredentialSuggestion(controller_, line_number_) ||
      !content_view_) {
    return;
  }

  bool has_primary_label = false;
  StylePasswordLabels(content_view_, &has_primary_label);
}

}  // namespace autofill
