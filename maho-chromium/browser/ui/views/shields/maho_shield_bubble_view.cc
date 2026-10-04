// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/shields/maho_shield_bubble_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_shield_site_state.h"
#include "maho/browser/net/maho_shield_site_state_factory.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/theme/maho_design_tokens.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/class_property.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(views::BubbleDialogDelegate,
                                 kShieldDelegateKey)

constexpr int kBubbleWidth = 296;
constexpr int kStatusIconSize = 18;
constexpr int kSettingsIconSize = 14;
constexpr int kStatusIndicatorSize = 6;
constexpr SkColor kActiveStatusBandColor = SkColorSetRGB(0x18, 0x52, 0x8C);
constexpr SkColor kActiveStatusForegroundColor =
    SkColorSetRGB(0xF6, 0xFA, 0xFF);
const gfx::Insets kStatusBandInsets = gfx::Insets::TLBR(tokens::kSpace3,
                                                        tokens::kSpace4,
                                                        tokens::kSpace4,
                                                        tokens::kSpace4);
const gfx::Insets kBodyInsets = gfx::Insets::TLBR(
    0, tokens::kSpace4, tokens::kSpace3, tokens::kSpace4);
const gfx::Insets kToggleRowInsets =
    gfx::Insets::VH(tokens::kSpace4, 0);
const gfx::Insets kSettingsRowInsets =
    gfx::Insets::TLBR(tokens::kSpace3, 0, 0, 0);

Profile* ResolveProfileForShieldSiteState(
    const base::WeakPtr<content::WebContents>& target_web_contents) {
  if (!target_web_contents) {
    return nullptr;
  }
  return Profile::FromBrowserContext(target_web_contents->GetBrowserContext());
}

MahoShieldSiteState* ResolveShieldSiteState(
    const base::WeakPtr<content::WebContents>& target_web_contents) {
  return MahoShieldSiteStateFactory::GetForProfile(
      ResolveProfileForShieldSiteState(target_web_contents));
}

std::u16string GetSiteLabel(const std::string& origin) {
  const GURL url(origin);
  return base::UTF8ToUTF16(url.host().empty() ? origin : url.host());
}

std::u16string GetBlockedRequestText(uint32_t blocked_count) {
  return blocked_count == 1 ? u"tracker or ad blocked"
                            : u"trackers & ads blocked";
}

std::u16string GetStatusText(int mode, bool is_excepted) {
  switch (mode) {
    case 0:
      return is_excepted ? u"Shields are off for this site"
                         : u"Shields are up on this site";
    case 1:
      return u"Managed by extension";
    case 2:
      return u"Shields are off";
    default:
      return u"Shields unavailable";
  }
}

std::u16string GetToggleSublabel(const std::string& origin,
                                 bool is_enabled) {
  return (is_enabled ? u"On for " : u"Off for ") + GetSiteLabel(origin);
}

ui::ColorVariant GetStatusBandColor(int mode, bool is_excepted) {
  if (mode == 0) {
    return is_excepted ? ui::ColorVariant(ui::kColorSysError)
                       : ui::ColorVariant(kActiveStatusBandColor);
  }
  if (mode == 1) {
    return ui::kColorSysTertiary;
  }
  if (mode == 2) {
    return ui::kColorSysError;
  }
  return ui::kColorSysSurface3;
}

ui::ColorVariant GetStatusForegroundColor(int mode, bool is_excepted) {
  if (mode == 0) {
    return is_excepted ? ui::ColorVariant(ui::kColorSysOnError)
                       : ui::ColorVariant(kActiveStatusForegroundColor);
  }
  if (mode == 1) {
    return ui::kColorSysOnTertiary;
  }
  if (mode == 2) {
    return ui::kColorSysOnError;
  }
  return ui::kColorSysOnSurface;
}

const gfx::VectorIcon& GetStatusIcon(int mode, bool is_excepted) {
  if (mode == 0) {
    return is_excepted ? maho_lucide_icons::kShieldXIcon
                       : maho_lucide_icons::kShieldCheckIcon;
  }
  if (mode == 1) {
    return maho_lucide_icons::kShieldHalfIcon;
  }
  if (mode == 2) {
    return maho_lucide_icons::kShieldXIcon;
  }
  return maho_lucide_icons::kShieldQuestionIcon;
}

views::Widget* CreateShieldBubble(
    std::unique_ptr<views::BubbleDialogDelegate> delegate,
    Browser* browser,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents) {
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->set_fixed_width(kBubbleWidth);
  delegate->set_corner_radius(tokens::kRadiusLg);
  delegate->SetContentsView(std::make_unique<MahoShieldBubbleView>(
      browser, origin, blocked_count, is_excepted, target_web_contents));

  views::Widget* widget =
      views::BubbleDialogDelegate::CreateBubbleDeprecated(
          delegate.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (widget) {
    widget->SetProperty(kShieldDelegateKey, std::move(delegate));
    widget->Show();
  }
  return widget;
}

}  // namespace

BEGIN_METADATA(MahoShieldBubbleView)
END_METADATA

MahoShieldBubbleView::~MahoShieldBubbleView() = default;

MahoShieldBubbleView::MahoShieldBubbleView(
    Browser* browser,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents)
    : browser_(browser),
      origin_(origin),
      is_excepted_(is_excepted),
      target_web_contents_(target_web_contents
                               ? target_web_contents->GetWeakPtr()
                               : base::WeakPtr<content::WebContents>()) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  MahoShieldSiteState* state = ResolveShieldSiteState(target_web_contents_);
  const int mode = state ? state->GetEffectiveContentBlockingMode() : -1;
  const MahoShieldCapabilities capabilities =
      state ? state->GetCapabilities() : MahoShieldCapabilities();
  const ui::ColorVariant status_foreground =
      GetStatusForegroundColor(mode, is_excepted_);

  status_band_ = AddChildView(std::make_unique<views::View>());
  status_band_->SetBackground(views::CreateRoundedRectBackground(
      GetStatusBandColor(mode, is_excepted_), tokens::kRadiusLg,
      tokens::kRadiusMd, 0));
  auto* status_band_layout =
      status_band_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, kStatusBandInsets,
          tokens::kSpace1));
  status_band_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* header_row =
      status_band_->AddChildView(std::make_unique<views::View>());
  auto* header_layout =
      header_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          tokens::kSpace2));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  site_label_ = header_row->AddChildView(std::make_unique<views::Label>(
      GetSiteLabel(origin), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_3_MEDIUM));
  site_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  site_label_->SetAutoColorReadabilityEnabled(false);
  site_label_->SetEnabledColor(status_foreground);
  site_label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  header_layout->SetFlexForView(site_label_, 1);

  status_icon_ = header_row->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          GetStatusIcon(mode, is_excepted_), status_foreground,
          kStatusIconSize)));
  status_icon_->SetCanProcessEventsWithinSubtree(false);

  blocked_count_label_ = status_band_->AddChildView(
      std::make_unique<views::Label>(base::NumberToString16(blocked_count),
                                     views::style::CONTEXT_LABEL,
                                     views::style::STYLE_HEADLINE_1));
  blocked_count_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  blocked_count_label_->SetAutoColorReadabilityEnabled(false);
  blocked_count_label_->SetEnabledColor(status_foreground);

  blocked_count_description_label_ = status_band_->AddChildView(
      std::make_unique<views::Label>(GetBlockedRequestText(blocked_count),
                                     views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_4_MEDIUM));
  blocked_count_description_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  blocked_count_description_label_->SetAutoColorReadabilityEnabled(false);
  blocked_count_description_label_->SetEnabledColor(status_foreground);

  auto* status_row =
      status_band_->AddChildView(std::make_unique<views::View>());
  status_row->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(tokens::kSpace1, 0, 0, 0)));
  auto* status_layout =
      status_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          tokens::kSpace2));
  status_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  status_indicator_ = status_row->AddChildView(std::make_unique<views::View>());
  status_indicator_->SetPreferredSize(
      gfx::Size(kStatusIndicatorSize, kStatusIndicatorSize));

  status_label_ = status_row->AddChildView(std::make_unique<views::Label>(
      GetStatusText(mode, is_excepted_), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM));
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetAutoColorReadabilityEnabled(false);
  status_label_->SetEnabledColor(status_foreground);
  UpdateStatusPresentation(mode);

  auto* body = AddChildView(std::make_unique<views::View>());
  auto* body_layout =
      body->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, kBodyInsets, 0));
  body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  if (capabilities.site_exception_toggle) {
    auto* toggle_row = body->AddChildView(std::make_unique<views::View>());
    toggle_row->SetBorder(views::CreateEmptyBorder(kToggleRowInsets));
    auto* toggle_layout =
        toggle_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
            tokens::kSpace3));
    toggle_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* toggle_text =
        toggle_row->AddChildView(std::make_unique<views::View>());
    auto* toggle_text_layout =
        toggle_text->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            tokens::kSpace1));
    toggle_text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    toggle_layout->SetFlexForView(toggle_text, 1);

    toggle_label_ = toggle_text->AddChildView(
        std::make_unique<views::Label>(u"Block trackers & ads",
                                       views::style::CONTEXT_LABEL,
                                       views::style::STYLE_BODY_4_MEDIUM));
    toggle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    toggle_label_->SetEnabledColor(ui::kColorSysOnSurface);

    toggle_sublabel_ = toggle_text->AddChildView(
        std::make_unique<views::Label>(GetToggleSublabel(origin, !is_excepted_),
                                       views::style::CONTEXT_LABEL,
                                       views::style::STYLE_BODY_5));
    toggle_sublabel_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    toggle_sublabel_->SetEnabledColor(ui::kColorSysOnSurfaceVariant);
    toggle_sublabel_->SetElideBehavior(gfx::ELIDE_MIDDLE);

    toggle_ = toggle_row->AddChildView(
        std::make_unique<views::ToggleButton>(base::BindRepeating(
            &MahoShieldBubbleView::OnTogglePressed, base::Unretained(this))));
    toggle_->SetIsOn(!is_excepted_);
    toggle_->SetAccessibleName(u"Block trackers & ads");
    ++native_toggle_row_count_;
  }

  auto* separator = body->AddChildView(std::make_unique<views::View>());
  separator->SetPreferredSize(gfx::Size(0, 1));
  separator->SetBackground(views::CreateSolidBackground(ui::kColorSysDivider));
  separator->SetCanProcessEventsWithinSubtree(false);

  auto* footer_row = body->AddChildView(std::make_unique<views::View>());
  footer_row->SetBorder(views::CreateEmptyBorder(kSettingsRowInsets));
  auto* footer_layout =
      footer_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          tokens::kSpace2));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  settings_button_ = footer_row->AddChildView(
      std::make_unique<views::LabelButton>(
          base::BindRepeating(&MahoShieldBubbleView::OnSettingsPressed,
                              base::Unretained(this)),
          u"Shield settings"));
  settings_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  settings_button_->SetLabelStyle(views::style::STYLE_BODY_5);
  settings_button_->SetEnabledTextColors(ui::kColorSysPrimary);
  settings_button_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  footer_layout->SetFlexForView(settings_button_, 1);

  auto* settings_icon = footer_row->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          maho_lucide_icons::kArrowUpRightIcon,
          ui::kColorSysOnSurfaceVariant, kSettingsIconSize)));
  settings_icon->SetCanProcessEventsWithinSubtree(false);
}

views::Widget* MahoShieldBubbleView::Show(
    views::View* anchor_view,
    Browser* browser,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT);
  return CreateShieldBubble(std::move(delegate), browser, origin,
                            blocked_count, is_excepted, target_web_contents);
}

views::Widget* MahoShieldBubbleView::Show(
    views::Widget* anchor_widget,
    const gfx::Rect& anchor_rect,
    Browser* browser,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents) {
  if (!anchor_widget || anchor_rect.IsEmpty()) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      nullptr, views::BubbleBorder::TOP_RIGHT);
  delegate->SetAnchorWidget(anchor_widget);
  delegate->SetAnchorRect(anchor_rect);
  return CreateShieldBubble(std::move(delegate), browser, origin,
                            blocked_count, is_excepted, target_web_contents);
}

int MahoShieldBubbleView::GetContentBlockingModeForTesting() {
  MahoCore* core = maho::GetCore();
  return core ? maho::core::GetContentBlockingMode(core) : -1;
}

bool MahoShieldBubbleView::SetContentBlockingModeForTesting(int mode) {
  MahoCore* core = maho::GetCore();
  return core && maho::core::SetContentBlockingMode(core, mode);
}

std::string MahoShieldBubbleView::GetSiteExceptionsForTesting() {
  MahoCore* core = maho::GetCore();
  return core ? maho::core::GetSiteExceptions(core) : "[]";
}

void MahoShieldBubbleView::OnTogglePressed() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoShieldSiteState* state =
      ResolveShieldSiteState(target_web_contents_);
  if (!state) {
    return;
  }

  const bool was_excepted = state->IsSiteExceptedForOrigin(origin_);
  const int mode = state->GetEffectiveContentBlockingMode();
  if (mode != 0) {
    is_excepted_ = was_excepted;
    if (toggle_) {
      toggle_->SetIsOn(!is_excepted_);
    }
    if (status_label_) {
      UpdateStatusPresentation(mode);
    }
    return;
  }

  const bool should_be_excepted = toggle_ ? !toggle_->GetIsOn() : !was_excepted;
  if (was_excepted == should_be_excepted) {
    return;
  }

  if (should_be_excepted) {
    state->AddSiteException(origin_);
  } else {
    state->RemoveSiteException(origin_);
  }

  is_excepted_ = state->IsSiteExceptedForOrigin(origin_);
  if (toggle_) {
    toggle_->SetIsOn(!is_excepted_);
  }
  if (is_excepted_ != should_be_excepted) {
    return;
  }

  if (target_web_contents_) {
    target_web_contents_->GetController().Reload(content::ReloadType::NORMAL,
                                                 true);
  }

  if (status_label_) {
    UpdateStatusPresentation(mode);
  }
}

void MahoShieldBubbleView::UpdateStatusPresentation(int mode) {
  const ui::ColorVariant foreground =
      GetStatusForegroundColor(mode, is_excepted_);
  if (status_band_) {
    status_band_->SetBackground(views::CreateRoundedRectBackground(
        GetStatusBandColor(mode, is_excepted_), tokens::kRadiusLg,
        tokens::kRadiusMd, 0));
  }
  if (site_label_) {
    site_label_->SetEnabledColor(foreground);
  }
  if (status_icon_) {
    status_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        GetStatusIcon(mode, is_excepted_), foreground, kStatusIconSize));
  }
  if (blocked_count_label_) {
    blocked_count_label_->SetEnabledColor(foreground);
  }
  if (blocked_count_description_label_) {
    blocked_count_description_label_->SetEnabledColor(foreground);
  }
  if (status_indicator_) {
    status_indicator_->SetBackground(views::CreateRoundedRectBackground(
        foreground, tokens::kRadiusPill));
  }
  if (status_label_) {
    status_label_->SetText(GetStatusText(mode, is_excepted_));
    status_label_->SetEnabledColor(foreground);
  }
  if (toggle_sublabel_) {
    toggle_sublabel_->SetText(GetToggleSublabel(origin_, !is_excepted_));
  }
}

void MahoShieldBubbleView::OnSettingsPressed(
    [[maybe_unused]] const ui::Event& event) {
  OpenSettings();
}

void MahoShieldBubbleView::OpenSettings() {
  if (!browser_ || !target_web_contents_) {
    return;
  }
  MahoShieldSiteState* state =
      ResolveShieldSiteState(target_web_contents_);
  const int mode = state ? state->GetEffectiveContentBlockingMode() : -1;
  maho::OpenMahoSettingsPane(browser_,
                             mode == 1 ? "extensions" : "content-blocker");
}

void MahoShieldBubbleView::ToggleForTesting() {
  OnTogglePressed();
}

void MahoShieldBubbleView::OpenSettingsForTesting() {
  OpenSettings();
}

}  // namespace maho
