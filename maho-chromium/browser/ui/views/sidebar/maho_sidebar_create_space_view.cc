// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"

#include <array>
#include <string>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"

namespace maho {

namespace {

constexpr int kCreatePanelCornerRadius = 18;
constexpr int kHeroBadgeSize = 44;
constexpr int kHeroBadgeCornerRadius = 14;
constexpr int kCardCornerRadius = 16;
constexpr int kInputCornerRadius = 12;
constexpr int kInputHeight = 40;
constexpr int kSectionGlyphSize = 30;
constexpr int kSectionGlyphCornerRadius = 11;
constexpr int kBottomDotSize = 6;
constexpr int kBottomDotCornerRadius = 3;
constexpr char kDefaultProfileId[] = "Default";

struct ColorPreset {
  double hue;
  double saturation;
  double brightness;
};

constexpr auto kColorPresets = std::to_array<ColorPreset>({
    {0.10, 0.15, 0.95},
    {0.92, 0.80, 0.90},
    {0.75, 0.80, 0.90},
    {0.00, 0.80, 0.90},
    {0.08, 0.80, 0.90},
    {0.15, 0.80, 0.90},
    {0.35, 0.80, 0.90},
    {0.58, 0.80, 0.90},
    {0.62, 0.30, 0.55},
});

constexpr int kSwatchSize = 22;
constexpr int kSwatchCornerRadius = 11;
constexpr int kSwatchSpacing = 6;

struct CreateResult {
  bool success = false;
  std::string space_id;
};

const gfx::Insets kCreateSurfaceInsets = gfx::Insets::TLBR(12, 2, 2, 2);
const gfx::Insets kContentInsets = gfx::Insets::TLBR(10, 12, 10, 12);
const gfx::Insets kHeroBadgeInsets = gfx::Insets::VH(0, 14);
const gfx::Insets kCardInsets = gfx::Insets::TLBR(12, 12, 12, 12);
const gfx::Insets kInputInsets = gfx::Insets::VH(0, 12);
const gfx::Insets kBottomRegionInsets = gfx::Insets::TLBR(8, 0, 2, 0);

views::Label* AddLabel(views::View* parent,
                       const std::u16string& text,
                       views::style::TextStyle text_style,
                       ui::ColorId color_id,
                       bool multiline = false) {
  auto label = std::make_unique<views::Label>(text);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(color_id);
  label->SetTextStyle(text_style);
  label->SetMultiLine(multiline);
  label->SetElideBehavior(multiline ? gfx::NO_ELIDE : gfx::ELIDE_TAIL);
  return parent->AddChildView(std::move(label));
}

views::View* CreateGlyphBadge(views::View* parent, const std::u16string& glyph) {
  auto* badge = parent->AddChildView(std::make_unique<views::View>());
  badge->SetPreferredSize(gfx::Size(kSectionGlyphSize, kSectionGlyphSize));
  badge->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface3, kSectionGlyphCornerRadius));
  badge->SetBorder(views::CreateRoundedRectBorder(
      1, kSectionGlyphCornerRadius, ui::kColorSysSurfaceVariant));
  auto* layout = badge->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* label = AddLabel(badge, glyph, views::style::STYLE_BODY_4_MEDIUM,
                         ui::kColorSysOnSurfaceSubtle);
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return badge;
}

views::View* CreateInfoCard(views::View* parent,
                            const std::u16string& badge_glyph,
                            const std::u16string& title,
                            const std::u16string& subtitle) {
  auto* card = parent->AddChildView(std::make_unique<views::View>());
  auto* layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, kCardInsets, 10));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  card->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  card->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  CreateGlyphBadge(card, badge_glyph);

  auto* text_stack = card->AddChildView(std::make_unique<views::View>());
  auto* text_layout = text_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(text_stack, 1);

  AddLabel(text_stack, title, views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  AddLabel(text_stack, subtitle, views::style::STYLE_BODY_5,
           ui::kColorSysOnSurfaceSubtle, true);

  auto* trailing = AddLabel(card, u"›", views::style::STYLE_BODY_3_MEDIUM,
                            ui::kColorSysOnSurfaceSubtle);
  trailing->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return card;
}

CreateResult CreateSpaceOnPool(const std::string& name,
                               ColorPreset color) {
  CreateResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return result;
  }

  base::DictValue color_dict;
  color_dict.Set("hue", color.hue);
  color_dict.Set("saturation", color.saturation);
  color_dict.Set("brightness", color.brightness);
  color_dict.Set("grain", 0.0);
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  char* ffi_result = maho_core_create_space(core, name.c_str(),
                                            color_json.c_str(),
                                            kDefaultProfileId);
  if (ffi_result) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(ffi_result, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const std::string* parsed_id = parsed->GetDict().FindString("id");
      if (parsed_id) {
        result.space_id = *parsed_id;
        result.success = true;
      }
    }
    maho_string_free(ffi_result);
  }
  return result;
}

}  // namespace

BEGIN_METADATA(MahoSidebarCreateSpaceView)
END_METADATA

MahoSidebarCreateSpaceView::MahoSidebarCreateSpaceView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kCreateSurfaceInsets, 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* content = AddChildView(std::make_unique<views::View>());
  auto* content_layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kContentInsets, 12));
  content_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(content, 1, true);

  auto* hero = content->AddChildView(std::make_unique<views::View>());
  auto* hero_layout = hero->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  hero_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* hero_badge = hero->AddChildView(std::make_unique<views::View>());
  hero_badge->SetPreferredSize(gfx::Size(kHeroBadgeSize, kHeroBadgeSize));
  hero_badge->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface3, kHeroBadgeCornerRadius));
  hero_badge->SetBorder(views::CreateRoundedRectBorder(
      1, kHeroBadgeCornerRadius, ui::kColorSysSurfaceVariant));
  hero_badge->SetProperty(views::kMarginsKey, kHeroBadgeInsets);
  auto* hero_badge_layout = hero_badge->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 0));
  hero_badge_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  hero_badge_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* hero_badge_label = AddLabel(hero_badge, u"✦",
                                    views::style::STYLE_HEADLINE_5,
                                    ui::kColorSysOnSurfaceSubtle);
  hero_badge_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  auto* title_label = AddLabel(hero, u"Create a new space",
                               views::style::STYLE_HEADLINE_4,
                               ui::kColorSysOnSurface, true);
  title_label->SetMaximumWidth(260);

  auto* subtitle_label = AddLabel(
      hero,
      u"Set up a focused home for tabs, notes, and future personalizations.",
      views::style::STYLE_BODY_5, ui::kColorSysOnSurfaceSubtle, true);
  subtitle_label->SetMaximumWidth(260);

  auto* card_stack = content->AddChildView(std::make_unique<views::View>());
  auto* card_stack_layout = card_stack->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 10));
  card_stack_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* name_card = card_stack->AddChildView(std::make_unique<views::View>());
  auto* name_card_layout = name_card->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kCardInsets, 10));
  name_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  name_card->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  name_card->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  auto* name_header = name_card->AddChildView(std::make_unique<views::View>());
  auto* name_header_layout = name_header->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 10));
  name_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  CreateGlyphBadge(name_header, u"Aa");

  auto* name_header_text =
      name_header->AddChildView(std::make_unique<views::View>());
  auto* name_header_text_layout = name_header_text->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 2));
  name_header_text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  name_header_layout->SetFlexForView(name_header_text, 1);
  AddLabel(name_header_text, u"Space name", views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  AddLabel(name_header_text, u"Choose a label you’ll recognize at a glance.",
           views::style::STYLE_BODY_5, ui::kColorSysOnSurfaceSubtle, true);

  name_field_ = name_card->AddChildView(std::make_unique<views::Textfield>());
  name_field_->SetPlaceholderText(u"Space name");
  name_field_->SetAccessibleName(u"Space name");
  name_field_->SetPreferredSize(gfx::Size(0, kInputHeight));
  name_field_->SetBorder(views::CreateRoundedRectBorder(
      1, kInputCornerRadius, ui::kColorSysSurfaceVariant));
  name_field_->SetExtraInsets(kInputInsets);
  name_field_->set_controller(this);

  CreateInfoCard(card_stack, u"☺", u"Profile",
                 u"Default profile selected for this space.");

  auto* theme_card = card_stack->AddChildView(std::make_unique<views::View>());
  auto* theme_card_layout = theme_card->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kCardInsets, 10));
  theme_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  theme_card->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  theme_card->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  auto* theme_header = theme_card->AddChildView(std::make_unique<views::View>());
  auto* theme_header_layout = theme_header->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 10));
  theme_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  CreateGlyphBadge(theme_header, u"◐");

  auto* theme_header_text =
      theme_header->AddChildView(std::make_unique<views::View>());
  auto* theme_header_text_layout = theme_header_text->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 2));
  theme_header_text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  theme_header_layout->SetFlexForView(theme_header_text, 1);
  AddLabel(theme_header_text, u"Theme", views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  AddLabel(theme_header_text, u"Pick a color for your new space",
           views::style::STYLE_BODY_5, ui::kColorSysOnSurfaceSubtle, true);

  swatch_row_ = theme_card->AddChildView(std::make_unique<views::View>());
  auto* swatch_layout = swatch_row_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), kSwatchSpacing));
  swatch_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  for (int i = 0; i < static_cast<int>(std::size(kColorPresets)); ++i) {
    const auto& preset = kColorPresets[i];
    SkScalar hsv[3] = {
        static_cast<SkScalar>(preset.hue * 360.0),
        static_cast<SkScalar>(preset.saturation),
        static_cast<SkScalar>(preset.brightness)};
    SkColor swatch_color = SkHSVToColor(hsv);

    auto button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoSidebarCreateSpaceView::OnColorSwatchPressed,
                            weak_factory_.GetWeakPtr(), i),
        u"");
    button->SetAccessibleName(u"Theme color " + base::NumberToString16(i + 1));
    button->SetPreferredSize(gfx::Size(kSwatchSize, kSwatchSize));
    button->SetCornerRadius(kSwatchCornerRadius);
    button->SetBgColorOverrideDeprecated(swatch_color);
    button->SetMinSize(gfx::Size(kSwatchSize, kSwatchSize));
    button->SetMaxSize(gfx::Size(kSwatchSize, kSwatchSize));
    if (i == selected_color_index_) {
      button->SetBorder(views::CreateRoundedRectBorder(
          2, kSwatchCornerRadius, SK_ColorWHITE));
    }
    swatch_row_->AddChildView(std::move(button));
  }

  auto* spacer = content->AddChildView(std::make_unique<views::View>());
  content_layout->SetFlexForView(spacer, 1, true);

  auto* bottom_region = content->AddChildView(std::make_unique<views::View>());
  auto* bottom_layout = bottom_region->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kBottomRegionInsets, 8));
  bottom_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  create_button_ = bottom_region->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCreatePressed,
                              weak_factory_.GetWeakPtr()),
          u"Create Space"));
  create_button_->SetStyle(ui::ButtonStyle::kProminent);
  create_button_->SetCornerRadius(14);
  create_button_->SetMinSize(gfx::Size(0, 42));
  create_button_->SetAccessibleName(u"Create Space");

  error_label_ = AddLabel(bottom_region, u"", views::style::STYLE_BODY_5,
                          ui::kColorSysError, true);
  error_label_->SetVisible(false);

  auto* cancel_row = bottom_region->AddChildView(std::make_unique<views::View>());
  auto* cancel_row_layout = cancel_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 0));
  cancel_row_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  cancel_button_ = cancel_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCancelPressed,
                              weak_factory_.GetWeakPtr()),
          u"Cancel"));
  cancel_button_->SetStyle(ui::ButtonStyle::kText);
  cancel_button_->SetAccessibleName(u"Cancel create space");

  auto* dots_row = bottom_region->AddChildView(std::make_unique<views::View>());
  auto* dots_layout = dots_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  dots_layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  dots_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  for (int i = 0; i < 3; ++i) {
    auto* dot = dots_row->AddChildView(std::make_unique<views::View>());
    dot->SetPreferredSize(gfx::Size(kBottomDotSize, kBottomDotSize));
    dot->SetBackground(views::CreateRoundedRectBackground(
        i == 0 ? ui::kColorSysOnSurfaceSubtle : ui::kColorSysSurfaceVariant,
        kBottomDotCornerRadius));
  }

  UpdateCreateButtonState();
}

MahoSidebarCreateSpaceView::~MahoSidebarCreateSpaceView() = default;

void MahoSidebarCreateSpaceView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface,
                                                   kCreatePanelCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCreatePanelCornerRadius,
                                           ui::kColorSysSurfaceVariant));

  if (!name_field_ || !GetColorProvider()) {
    return;
  }

  name_field_->SetBackgroundColor(
      GetColorProvider()->GetColor(ui::kColorSysSurface3));
  name_field_->SetTextColor(GetColorProvider()->GetColor(ui::kColorSysOnSurface));
  name_field_->set_placeholder_text_color(
      GetColorProvider()->GetColor(ui::kColorSysOnSurfaceSubtle));
}

void MahoSidebarCreateSpaceView::SetCloseCallback(
    base::RepeatingClosure close_callback) {
  close_callback_ = std::move(close_callback);
}

void MahoSidebarCreateSpaceView::RequestNameFocus() {
  if (!name_field_) {
    return;
  }
  name_field_->RequestFocus();
  name_field_->SelectAll(false);
}

void MahoSidebarCreateSpaceView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (sender != name_field_) {
    return;
  }
  if (error_label_) {
    error_label_->SetVisible(false);
  }
  UpdateCreateButtonState();
}

bool MahoSidebarCreateSpaceView::HandleKeyEvent(views::Textfield* sender,
                                                const ui::KeyEvent& event) {
  if (sender != name_field_ || event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      OnCreatePressed();
      return true;
    case ui::VKEY_ESCAPE:
      OnCancelPressed();
      return true;
    default:
      return false;
  }
}

void MahoSidebarCreateSpaceView::UpdateCreateButtonState() {
  if (!create_button_ || !name_field_) {
    return;
  }
  std::u16string trimmed_name;
  base::TrimWhitespace(name_field_->GetText(), base::TRIM_ALL, &trimmed_name);
  create_button_->SetEnabled(!trimmed_name.empty());
}

void MahoSidebarCreateSpaceView::OnCreatePressed() {
  if (!name_field_) {
    return;
  }

  std::u16string trimmed_name;
  base::TrimWhitespace(name_field_->GetText(), base::TRIM_ALL, &trimmed_name);
  if (trimmed_name.empty()) {
    return;
  }

  create_button_->SetEnabled(false);
  if (error_label_) {
    error_label_->SetVisible(false);
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&CreateSpaceOnPool, base::UTF16ToUTF8(trimmed_name),
                     kColorPresets[selected_color_index_]),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
             CreateResult result) {
            if (view) {
              view->OnSpaceCreated(result.success, result.space_id);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarCreateSpaceView::OnColorSwatchPressed(int index) {
  selected_color_index_ = index;
  if (!swatch_row_) {
    return;
  }
  int i = 0;
  for (views::View* child : swatch_row_->children()) {
    if (i == selected_color_index_) {
      child->SetBorder(views::CreateRoundedRectBorder(
          2, kSwatchCornerRadius, SK_ColorWHITE));
    } else {
      child->SetBorder(nullptr);
    }
    ++i;
  }
  swatch_row_->InvalidateLayout();
}

void MahoSidebarCreateSpaceView::OnCancelPressed() {
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  SetVisible(false);
}

void MahoSidebarCreateSpaceView::SetNameForTesting(const std::u16string& text) {
  if (!name_field_) {
    return;
  }
  name_field_->SetText(text);
  UpdateCreateButtonState();
}

void MahoSidebarCreateSpaceView::CompleteCreateSpaceForTesting(
    bool success,
    const std::string& new_space_id) {
  OnSpaceCreated(success, new_space_id);
}

void MahoSidebarCreateSpaceView::OnSpaceCreated(bool success,
                                                const std::string& new_space_id) {
  if (!success) {
    if (error_label_) {
      error_label_->SetText(u"Couldn’t create space. Try again.");
      error_label_->SetVisible(true);
    }
    UpdateCreateButtonState();
    return;
  }

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    bridge->RegisterSpace(new_space_id,
                          base::FilePath::FromASCII(kDefaultProfileId));
    bridge->SwitchToSpace(new_space_id);
  }
  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();

  if (name_field_) {
    name_field_->SetText(u"");
  }
  if (create_button_) {
    UpdateCreateButtonState();
  }
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  SetVisible(false);
}

}  // namespace maho
