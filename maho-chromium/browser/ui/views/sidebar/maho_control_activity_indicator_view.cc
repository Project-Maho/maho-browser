// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"

namespace maho {

namespace {

// Separator between the controller prefix, the state phrase, and the target in
// both the visible status line and the accessible name. A plain hyphen keeps
// the string ASCII and screen-reader friendly.
constexpr char16_t kSeparator[] = u" - ";

// Shown when the controlled tab reports no title. The controller and state
// stay visible, so attribution is never lost to a missing title.
constexpr char16_t kUntitledTarget[] = u"Untitled tab";

std::u16string StatePhrase(const MahoControlActivityIndicatorModel& model) {
  // Cross-surface attribution outranks the specific verb: if the user cannot
  // see the target, saying "Reading this tab" would be actively misleading.
  if (model.state == MahoControlActivityState::kReading ||
      model.state == MahoControlActivityState::kActing) {
    if (model.target_is_other_window) {
      return u"Working in another Maho window";
    }
    if (model.target_is_background_tab) {
      return u"Working in another tab";
    }
  }

  switch (model.state) {
    case MahoControlActivityState::kIdle:
      return std::u16string();
    case MahoControlActivityState::kReading:
      return u"Reading this tab";
    case MahoControlActivityState::kActing:
      return u"Acting on this tab";
    case MahoControlActivityState::kWaitingApproval:
      return u"Waiting for approval";
    case MahoControlActivityState::kPaused:
      return u"Paused";
    case MahoControlActivityState::kDisconnected:
      return u"Disconnected";
    case MahoControlActivityState::kError:
      return u"Action failed";
  }
}

views::Label* AddTextLabel(views::View* parent,
                           int text_context,
                           int text_style) {
  auto label = std::make_unique<views::Label>(std::u16string(), text_context,
                                              text_style);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetSubpixelRenderingEnabled(false);
  label->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  return parent->AddChildView(std::move(label));
}

views::LabelButton* AddActionButton(views::View* parent,
                                    views::Button::PressedCallback callback,
                                    const std::u16string& text) {
  auto button = std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetAccessibleName(text);
  button->SetTooltipText(text);
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return parent->AddChildView(std::move(button));
}

}  // namespace

MahoControlActivityIndicatorModel::MahoControlActivityIndicatorModel() =
    default;
MahoControlActivityIndicatorModel::MahoControlActivityIndicatorModel(
    const MahoControlActivityIndicatorModel&) = default;
MahoControlActivityIndicatorModel& MahoControlActivityIndicatorModel::operator=(
    const MahoControlActivityIndicatorModel&) = default;
MahoControlActivityIndicatorModel::~MahoControlActivityIndicatorModel() =
    default;

BEGIN_METADATA(MahoControlActivityIndicatorView)
END_METADATA

MahoControlActivityIndicatorView::MahoControlActivityIndicatorView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(sidebar_layout::kControlIndicatorVerticalInsetDp,
                      sidebar_layout::kControlIndicatorHorizontalInsetDp),
      sidebar_layout::kControlIndicatorItemSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  state_icon_ = AddChildView(std::make_unique<views::ImageView>());
  state_icon_->SetCanProcessEventsWithinSubtree(false);
  state_icon_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  // The glyph repeats the state already spoken in the accessible name.
  state_icon_->GetViewAccessibility().SetIsIgnored(true);

  status_label_ = AddTextLabel(this, views::style::CONTEXT_LABEL,
                               views::style::STYLE_BODY_5_MEDIUM);
  target_label_ = AddTextLabel(this, views::style::CONTEXT_LABEL,
                               views::style::STYLE_BODY_5);

  // The text column absorbs the leftover width so a long title elides instead
  // of pushing Details/Stop out of the chip.
  layout->SetFlexForView(status_label_, 1);
  layout->SetFlexForView(target_label_, 2);

  details_button_ = AddActionButton(
      this,
      base::BindRepeating(
          [](base::WeakPtr<MahoControlActivityIndicatorView> view) {
            if (view && view->details_callback_) {
              view->details_callback_.Run();
            }
          },
          weak_factory_.GetWeakPtr()),
      u"Details");
  stop_button_ = AddActionButton(
      this,
      base::BindRepeating(
          [](base::WeakPtr<MahoControlActivityIndicatorView> view) {
            if (view && view->stop_callback_) {
              view->stop_callback_.Run();
            }
          },
          weak_factory_.GetWeakPtr()),
      u"Stop");

  GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);

  RebuildFromModel();
}

MahoControlActivityIndicatorView::~MahoControlActivityIndicatorView() = default;

// static
const gfx::VectorIcon& MahoControlActivityIndicatorView::IconForState(
    MahoControlActivityState state) {
  switch (state) {
    case MahoControlActivityState::kIdle:
    case MahoControlActivityState::kReading:
      return maho_lucide_icons::kGlassesIcon;
    case MahoControlActivityState::kActing:
      return maho_lucide_icons::kHammerIcon;
    case MahoControlActivityState::kWaitingApproval:
      return maho_lucide_icons::kShieldQuestionIcon;
    case MahoControlActivityState::kPaused:
      return maho_lucide_icons::kPauseIcon;
    case MahoControlActivityState::kDisconnected:
    case MahoControlActivityState::kError:
      return maho_lucide_icons::kShieldXIcon;
  }
}

// static
std::u16string MahoControlActivityIndicatorView::PlaneLabel(
    MahoControlPlane plane) {
  switch (plane) {
    case MahoControlPlane::kEmbedded:
      return std::u16string();
    case MahoControlPlane::kCli:
      return u"CLI";
    case MahoControlPlane::kExternalMcp:
      return u"MCP";
  }
}

// static
std::u16string MahoControlActivityIndicatorView::StatusTextForState(
    const MahoControlActivityIndicatorModel& model) {
  const std::u16string phrase = StatePhrase(model);
  if (phrase.empty()) {
    return std::u16string();
  }
  const std::u16string plane = PlaneLabel(model.plane);
  std::u16string text = u"AI";
  if (!plane.empty()) {
    text += kSeparator + plane;
  }
  return text + kSeparator + phrase;
}

// static
std::u16string MahoControlActivityIndicatorView::AccessibleNameForModel(
    const MahoControlActivityIndicatorModel& model) {
  const std::u16string phrase = StatePhrase(model);
  if (phrase.empty()) {
    return std::u16string();
  }
  // The accessible name uses the real controller name rather than the short
  // visual plane tag, so screen-reader users get the stronger attribution.
  const std::u16string controller =
      model.controller_name.empty() ? u"Maho AI" : model.controller_name;
  const std::u16string target =
      model.target_title.empty() ? kUntitledTarget : model.target_title;
  return controller + kSeparator + phrase + kSeparator + target;
}

// static
bool MahoControlActivityIndicatorView::StateAllowsStop(
    MahoControlActivityState state) {
  switch (state) {
    case MahoControlActivityState::kReading:
    case MahoControlActivityState::kActing:
    case MahoControlActivityState::kWaitingApproval:
    case MahoControlActivityState::kPaused:
      return true;
    case MahoControlActivityState::kIdle:
    case MahoControlActivityState::kDisconnected:
    case MahoControlActivityState::kError:
      return false;
  }
}

// static
bool MahoControlActivityIndicatorView::StateAllowsDetails(
    MahoControlActivityState state) {
  return state != MahoControlActivityState::kIdle;
}

// static
std::u16string MahoControlActivityIndicatorView::StopButtonLabelForState(
    MahoControlActivityState state) {
  switch (state) {
    // A paused or approval-blocked controller still holds its grant, so the
    // user is withdrawing authority rather than interrupting work in flight.
    case MahoControlActivityState::kPaused:
    case MahoControlActivityState::kWaitingApproval:
      return u"Revoke";
    default:
      return u"Stop";
  }
}

void MahoControlActivityIndicatorView::SetModel(
    const MahoControlActivityIndicatorModel& model) {
  model_ = model;
  RebuildFromModel();
}

void MahoControlActivityIndicatorView::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  ApplyPalette();
}

void MahoControlActivityIndicatorView::SetCompact(bool compact) {
  if (compact_ == compact) {
    return;
  }
  compact_ = compact;
  RebuildFromModel();
}

void MahoControlActivityIndicatorView::SetDetailsCallback(
    base::RepeatingClosure callback) {
  details_callback_ = std::move(callback);
}

void MahoControlActivityIndicatorView::SetStopCallback(
    base::RepeatingClosure callback) {
  stop_callback_ = std::move(callback);
}

void MahoControlActivityIndicatorView::RebuildFromModel() {
  const bool active = model_.state != MahoControlActivityState::kIdle;
  SetVisible(active);

  status_label_->SetText(StatusTextForState(model_));
  target_label_->SetText(model_.target_title.empty() ? kUntitledTarget
                                                     : model_.target_title);

  // Compact drops prose but never the glyph or the kill switch.
  status_label_->SetVisible(active && !compact_);
  target_label_->SetVisible(active && !compact_);
  state_icon_->SetVisible(active);

  const bool allows_stop = StateAllowsStop(model_.state);
  stop_button_->SetVisible(active && allows_stop);
  stop_button_->SetEnabled(allows_stop);
  const std::u16string stop_text = StopButtonLabelForState(model_.state);
  stop_button_->SetText(stop_text);
  stop_button_->SetAccessibleName(stop_text);
  stop_button_->SetTooltipText(stop_text);

  const bool allows_details = StateAllowsDetails(model_.state);
  // Details is text-free in compact mode; the accessible name carries it.
  details_button_->SetVisible(active && allows_details && !compact_);
  details_button_->SetEnabled(allows_details);

  const std::u16string accessible_name = AccessibleNameForModel(model_);
  if (accessible_name.empty()) {
    GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
  } else {
    GetViewAccessibility().SetName(accessible_name);
  }

  ApplyPalette();
  PreferredSizeChanged();
}

SkColor MahoControlActivityIndicatorView::ResolveStateColor() const {
  switch (model_.state) {
    // Observation and halted states stay in the ordinary functional-glyph
    // role so reading never looks louder than acting.
    case MahoControlActivityState::kIdle:
    case MahoControlActivityState::kReading:
    case MahoControlActivityState::kPaused:
    case MahoControlActivityState::kDisconnected:
      return palette_.neutral_glyph;
    // Mutation, pending approval, and failure are the states the user must
    // notice, so they take the palette's accent role.
    case MahoControlActivityState::kActing:
    case MahoControlActivityState::kWaitingApproval:
    case MahoControlActivityState::kError:
      return palette_.focus_ring;
  }
}

void MahoControlActivityIndicatorView::ApplyPalette() {
  // Every foreground here is pushed from MahoSidebarPalette. Sidebar surfaces
  // must not read ColorIds directly (see browser/ui/views/DESIGN.md, "Color
  // Authority"), so a palette-less view simply paints nothing new.
  if (palette_.primary_text == SK_ColorTRANSPARENT) {
    return;
  }

  status_label_->SetEnabledColor(palette_.primary_text);
  target_label_->SetEnabledColor(palette_.secondary_text);

  state_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      IconForState(model_.state), ResolveStateColor(),
      sidebar_layout::kControlIndicatorIconSizeDp));

  for (views::LabelButton* button :
       {details_button_.get(), stop_button_.get()}) {
    button->SetEnabledTextColors(palette_.primary_text);
    button->SetTextColor(views::Button::STATE_DISABLED, palette_.disabled_text);
  }

  SetBorder(views::CreateRoundedRectBorder(
      /*thickness=*/1, sidebar_layout::kControlIndicatorCornerRadiusDp,
      palette_.outline));
}

gfx::Size MahoControlActivityIndicatorView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (model_.state == MahoControlActivityState::kIdle) {
    return gfx::Size();
  }

  const int height = sidebar_layout::kControlIndicatorHeightDp;
  if (compact_) {
    return gfx::Size(sidebar_layout::kControlIndicatorCompactWidthDp, height);
  }

  gfx::Size preferred = views::View::CalculatePreferredSize(available_size);
  preferred.set_height(std::max(preferred.height(), height));
  // The chip is a flexible strip: never demand more than the sidebar offers,
  // because the text column elides while Details/Stop keep their intrinsic
  // width.
  if (available_size.width().is_bounded()) {
    preferred.set_width(
        std::min(preferred.width(), available_size.width().value()));
  }
  return preferred;
}

}  // namespace maho
