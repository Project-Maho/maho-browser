// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "cc/paint/paint_flags.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kCreatePanelCornerRadius = 18;
constexpr int kHeroBadgeSize = 48;
constexpr int kHeroBadgeCornerRadius = 14;
constexpr int kCardCornerRadius = 18;
constexpr int kInputCornerRadius = 12;
constexpr int kInputHeight = 44;
constexpr int kSectionGlyphSize = 32;
constexpr int kSectionGlyphCornerRadius = 11;
constexpr int kBottomDotSize = 6;
constexpr int kBottomDotCornerRadius = 3;
constexpr int kThemePreviewDotSize = 14;
constexpr int kThemePreviewDotCornerRadius = 7;
constexpr char kDefaultProfileId[] = "Default";

struct ColorPreset {
  double hue;
  double saturation;
  double brightness;
};

#if __cplusplus >= 202002L
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
#else
constexpr std::array<ColorPreset, 9> kColorPresets = {{
    {0.10, 0.15, 0.95},
    {0.92, 0.80, 0.90},
    {0.75, 0.80, 0.90},
    {0.00, 0.80, 0.90},
    {0.08, 0.80, 0.90},
    {0.15, 0.80, 0.90},
    {0.35, 0.80, 0.90},
    {0.58, 0.80, 0.90},
    {0.62, 0.30, 0.55},
}};
#endif

constexpr int kSwatchSize = 40;
constexpr int kSwatchCornerRadius = 20;
constexpr int kSwatchSpacing = 14;
constexpr int kThemeDialogWidth = 1110;
constexpr int kThemeDialogSurfaceCornerRadius = 30;
constexpr int kThemeDialogLeftPanelWidth = 430;
constexpr int kThemeChipSize = 52;
constexpr int kThemeCanvasCornerRadius = 30;
constexpr int kThemeCanvasHeight = 596;
constexpr int kThemePreviewControlSize = 24;
constexpr int kThemeChevronButtonSize = 30;
constexpr int kThemeGrainDialSize = 106;
constexpr int kThemeCanvasDotSpacing = 11;
constexpr float kPi = 3.14159265f;

struct CreateResult {
  bool success = false;
  std::string space_id;
};

views::Widget* g_theme_picker_widget = nullptr;

class MahoCreateSpaceThemePickerDialogView;
MahoCreateSpaceThemePickerDialogView* g_theme_picker_dialog_view = nullptr;

const gfx::Insets kCreateSurfaceInsets = gfx::Insets();
const gfx::Insets kContentInsets = gfx::Insets::TLBR(24, 24, 20, 24);
const gfx::Insets kHeroBadgeInsets = gfx::Insets::VH(0, 14);
const gfx::Insets kCardInsets = gfx::Insets::TLBR(14, 14, 14, 14);
const gfx::Insets kInputInsets = gfx::Insets::VH(0, 12);
const gfx::Insets kBottomRegionInsets = gfx::Insets::TLBR(14, 0, 0, 0);

MahoSidebarCreateSpaceThemeSelection DefaultThemeSelection() {
  return MahoSidebarCreateSpaceThemeSelection();
}

const ColorPreset& GetColorPresetForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const int max_index = static_cast<int>(std::size(kColorPresets) - 1);
  const int clamped_index = std::clamp(selection.preset_index, 0, max_index);
  return kColorPresets.at(clamped_index);
}

int ClampBrightness(int brightness) {
  return std::clamp(brightness, 15, 100);
}

int ClampGrain(int grain) {
  return std::clamp(grain, 0, 100);
}

int ClampPresetIndex(int preset_index) {
  const int max_index = static_cast<int>(std::size(kColorPresets) - 1);
  return std::clamp(preset_index, 0, max_index);
}

SkColor BuildSkColorForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const ColorPreset& preset = GetColorPresetForSelection(selection);
  SkScalar hsv[3] = {
      static_cast<SkScalar>(preset.hue * 360.0),
      static_cast<SkScalar>(preset.saturation),
      static_cast<SkScalar>(ClampBrightness(selection.brightness) / 100.0)};
  return SkHSVToColor(hsv);
}

std::string BuildColorHexForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const SkColor color = BuildSkColorForSelection(selection);
  std::ostringstream stream;
  stream << '#'
         << std::uppercase << std::hex << std::setfill('0')
         << std::setw(2) << static_cast<int>(SkColorGetR(color))
         << std::setw(2) << static_cast<int>(SkColorGetG(color))
         << std::setw(2) << static_cast<int>(SkColorGetB(color));
  return stream.str();
}

std::string BuildThemeJsonForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const ColorPreset& preset = GetColorPresetForSelection(selection);
  base::DictValue color_dict;
  color_dict.Set("hue", preset.hue);
  color_dict.Set("saturation", preset.saturation);
  color_dict.Set("brightness", ClampBrightness(selection.brightness) / 100.0);
  color_dict.Set("grain", ClampGrain(selection.grain) / 100.0);

  base::DictValue theme_dict;
  theme_dict.Set("type", "solid");
  theme_dict.Set("color", std::move(color_dict));

  std::string theme_json;
  base::JSONWriter::Write(theme_dict, &theme_json);
  return theme_json;
}

std::u16string BuildThemeSummaryForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const std::u16string mode_label =
      selection.mode == MahoSidebarCreateSpaceThemeMode::kSolid ? u"Solid"
                                                                : u"Zen";
  return mode_label + u" · " +
         base::UTF8ToUTF16(BuildColorHexForSelection(selection));
}

SkColor WithAlpha(SkColor color, U8CPU alpha) {
  return SkColorSetA(color, alpha);
}

SkColor GetThemeCanvasBackgroundColor(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  return selection.mode == MahoSidebarCreateSpaceThemeMode::kZen
              ? SkColorSetRGB(0x12, 0x17, 0x27)
              : SkColorSetRGB(0x18, 0x1E, 0x30);
}

SkColor GetThemeDialogShellColor() {
  return SkColorSetRGB(0x1F, 0x1D, 0x30);
}

SkColor GetThemeDialogLeftPanelColor() {
  return SkColorSetRGB(0x10, 0x0F, 0x1F);
}

SkColor GetThemeDialogRightPanelColor() {
  return SkColorSetRGB(0x24, 0x22, 0x35);
}

SkColor GetThemeDialogCardColor() {
  return SkColorSetRGB(0x31, 0x2E, 0x43);
}

SkColor GetThemeDialogCardBorderColor() {
  return SkColorSetARGB(0x44, 0xFF, 0xFF, 0xFF);
}

SkColor GetThemeDialogSoftTextColor() {
  return SkColorSetRGB(0xB8, 0xB3, 0xC7);
}

SkColor GetThemeDialogMutedStrokeColor() {
  return SkColorSetARGB(0x26, 0xFF, 0xFF, 0xFF);
}

class ThemeCanvasView : public views::View {
  METADATA_HEADER(ThemeCanvasView, views::View)

 public:
  ThemeCanvasView() = default;
  ThemeCanvasView(const ThemeCanvasView&) = delete;
  ThemeCanvasView& operator=(const ThemeCanvasView&) = delete;
  ~ThemeCanvasView() override = default;

  void SetSelection(const MahoSidebarCreateSpaceThemeSelection& selection) {
    selection_ = selection;
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    views::View::OnPaintBackground(canvas);

    const gfx::Rect bounds = GetContentsBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const SkColor accent = BuildSkColorForSelection(selection_);

    cc::PaintFlags ambient_flags;
    ambient_flags.setAntiAlias(true);
    ambient_flags.setStyle(cc::PaintFlags::kFill_Style);
    ambient_flags.setColor(WithAlpha(accent, 0x18));
    canvas->DrawCircle(
        gfx::PointF(bounds.x() + bounds.width() * 0.77f,
                    bounds.y() + bounds.height() * 0.27f),
        bounds.height() * 0.23f, ambient_flags);

    ambient_flags.setColor(WithAlpha(accent, 0x10));
    canvas->DrawCircle(
        gfx::PointF(bounds.x() + bounds.width() * 0.22f,
                    bounds.y() + bounds.height() * 0.82f),
        bounds.height() * 0.18f, ambient_flags);

    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);
    dot_flags.setColor(WithAlpha(SK_ColorWHITE, 0x30));

    for (int row = 0, y = bounds.y() + 16; y < bounds.bottom() - 16;
         y += kThemeCanvasDotSpacing, ++row) {
      const int x_offset = row % 2 == 0 ? 0 : kThemeCanvasDotSpacing / 2;
      for (int x = bounds.x() + 18 + x_offset; x < bounds.right() - 18;
           x += kThemeCanvasDotSpacing) {
        canvas->DrawCircle(gfx::PointF(x, y), 1.2f, dot_flags);
      }
    }
  }

 private:
  MahoSidebarCreateSpaceThemeSelection selection_;
};

class ThemeWaveView : public views::View {
  METADATA_HEADER(ThemeWaveView, views::View)

 public:
  ThemeWaveView() = default;
  ThemeWaveView(const ThemeWaveView&) = delete;
  ThemeWaveView& operator=(const ThemeWaveView&) = delete;
  ~ThemeWaveView() override = default;

  void SetGrain(int grain) {
    grain_ = ClampGrain(grain);
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    const gfx::Rect bounds = GetContentsBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    cc::PaintFlags wave_flags;
    wave_flags.setAntiAlias(true);
    wave_flags.setStyle(cc::PaintFlags::kFill_Style);
    wave_flags.setColor(SkColorSetARGB(0x80, 0x8C, 0x91, 0xA3));

    const float amplitude = 4.0f + (grain_ / 100.0f) * 9.0f;
    const float center_y = bounds.CenterPoint().y();
    const float start_x = bounds.x() + 8.0f;
    const float step = 6.0f;
    const int sample_count = std::max(1, (bounds.width() - 16) / 6);
    for (int i = 0; i <= sample_count; ++i) {
      const float x = start_x + i * step;
      const float y = center_y + std::sin(i * 0.58f) * amplitude;
      canvas->DrawCircle(gfx::PointF(x, y), 1.9f, wave_flags);
    }
  }

 private:
  int grain_ = 0;
};

class ThemeDialView : public views::View {
  METADATA_HEADER(ThemeDialView, views::View)

 public:
  ThemeDialView() = default;
  ThemeDialView(const ThemeDialView&) = delete;
  ThemeDialView& operator=(const ThemeDialView&) = delete;
  ~ThemeDialView() override = default;

  void SetSelection(const MahoSidebarCreateSpaceThemeSelection& selection) {
    selection_ = selection;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    const gfx::Rect bounds = GetContentsBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const gfx::PointF center(bounds.CenterPoint().x(), bounds.CenterPoint().y());
    const float outer_radius = std::min(bounds.width(), bounds.height()) / 2.0f - 6.0f;
    const float inner_radius = outer_radius - 10.0f;
    const SkColor accent = BuildSkColorForSelection(selection_);

    cc::PaintFlags ring_flags;
    ring_flags.setAntiAlias(true);
    ring_flags.setStyle(cc::PaintFlags::kStroke_Style);
    ring_flags.setStrokeWidth(1.2f);
    ring_flags.setColor(SkColorSetARGB(0x6A, 0xB8, 0xBD, 0xCC));
    canvas->DrawCircle(center, inner_radius, ring_flags);

    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);
    dot_flags.setColor(SkColorSetARGB(0x66, 0xB8, 0xBD, 0xCC));
    for (int i = 0; i < 18; ++i) {
      const float theta = (kPi * 2.0f * i) / 18.0f - (kPi / 2.0f);
      canvas->DrawCircle(
          gfx::PointF(center.x() + std::cos(theta) * outer_radius,
                      center.y() + std::sin(theta) * outer_radius),
          1.5f, dot_flags);
    }

    const float knob_theta =
        (-kPi / 2.0f) + ((selection_.grain / 100.0f) * kPi * 1.8f);
    cc::PaintFlags knob_flags;
    knob_flags.setAntiAlias(true);
    knob_flags.setStyle(cc::PaintFlags::kFill_Style);
    knob_flags.setColor(accent);
    canvas->DrawCircle(
        gfx::PointF(center.x() + std::cos(knob_theta) * inner_radius,
                    center.y() + std::sin(knob_theta) * inner_radius),
        4.0f, knob_flags);
  }

 private:
  MahoSidebarCreateSpaceThemeSelection selection_;
};

class ThemeHeroBadgeView : public views::View {
  METADATA_HEADER(ThemeHeroBadgeView, views::View)

 public:
  ThemeHeroBadgeView() {
    SetPreferredSize(gfx::Size(110, 80));

    auto make_card = [](const std::u16string& glyph) {
      auto card = std::make_unique<views::View>();
      card->SetBackground(views::CreateRoundedRectBackground(
          SkColorSetRGB(0xF6, 0xF1, 0xF4), 12));
      card->SetBorder(views::CreateRoundedRectBorder(
          1, 12, SkColorSetARGB(0x18, 0x00, 0x00, 0x00)));
      auto* layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
      layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kCenter);
      layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* label = card->AddChildView(std::make_unique<views::Label>(glyph));
      label->SetAutoColorReadabilityEnabled(false);
      label->SetEnabledColor(SkColorSetRGB(0x3A, 0x36, 0x47));
      label->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
      label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
      return card;
    };

    left_card_ = AddChildView(make_card(u"🌿"));
    center_card_ = AddChildView(make_card(u"▥"));
    right_card_ = AddChildView(make_card(u"✦"));
  }

  ThemeHeroBadgeView(const ThemeHeroBadgeView&) = delete;
  ThemeHeroBadgeView& operator=(const ThemeHeroBadgeView&) = delete;
  ~ThemeHeroBadgeView() override = default;

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    const gfx::Rect bounds = GetContentsBounds();
    const int card_width = 34;
    const int card_height = 48;
    left_card_->SetBounds(bounds.x() + 12, bounds.y() + 16, card_width,
                          card_height);
    center_card_->SetBounds(bounds.CenterPoint().x() - (card_width / 2),
                            bounds.y() + 4, card_width, 54);
    right_card_->SetBounds(bounds.right() - card_width - 12, bounds.y() + 16,
                           card_width, card_height);
  }

 private:
  raw_ptr<views::View> left_card_ = nullptr;
  raw_ptr<views::View> center_card_ = nullptr;
  raw_ptr<views::View> right_card_ = nullptr;
};

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
  label->SetSubpixelRenderingEnabled(false);
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

views::MdTextButton* CreateCircularGlyphButton(
    views::View* parent,
    views::Button::PressedCallback callback,
    const std::u16string& glyph,
    const std::u16string& accessible_name,
    int size,
    int corner_radius) {
  auto button = std::make_unique<views::MdTextButton>(std::move(callback), glyph);
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetCornerRadius(corner_radius);
  button->SetMinSize(gfx::Size(size, size));
  button->SetMaxSize(gfx::Size(size, size));
  button->SetAccessibleName(accessible_name);
  button->SetRequestFocusOnPress(false);
  return parent->AddChildView(std::move(button));
}

views::View* CreatePassiveThemeChip(views::View* parent,
                                    const std::u16string& glyph) {
  auto* chip = parent->AddChildView(std::make_unique<views::View>());
  chip->SetPreferredSize(gfx::Size(kThemeChipSize, kThemeChipSize));
  chip->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface2, kThemeChipSize / 2));
  chip->SetBorder(views::CreateRoundedRectBorder(
      1, kThemeChipSize / 2, ui::kColorSysSurfaceVariant));
  auto* layout = chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* label = AddLabel(chip, glyph, views::style::STYLE_BODY_4_MEDIUM,
                         ui::kColorSysOnSurfaceSubtle);
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return chip;
}

BEGIN_METADATA(ThemeCanvasView)
END_METADATA

BEGIN_METADATA(ThemeWaveView)
END_METADATA

BEGIN_METADATA(ThemeDialView)
END_METADATA

BEGIN_METADATA(ThemeHeroBadgeView)
END_METADATA

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
                               const MahoSidebarCreateSpaceThemeSelection&
                                   theme_selection) {
  CreateResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    DLOG(WARNING) << "Sidebar create-space failed: Maho core unavailable";
    return result;
  }

  base::DictValue color_dict;
  color_dict.Set("color", BuildColorHexForSelection(theme_selection));
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  // The inline sidebar create-space flow always passes a concrete profile id.
  // A null FFI result here is therefore a backend/contract failure, not the
  // older nullptr-profile path captured in the original remediation plan.
  char* ffi_result = maho_core_create_space(core, name.c_str(),
                                            color_json.c_str(),
                                            kDefaultProfileId);
  if (!ffi_result) {
    DLOG(WARNING)
        << "Sidebar create-space failed: maho_core_create_space returned null"
        << " for profile_id='" << kDefaultProfileId << "'";
    return result;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(ffi_result, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    DLOG(WARNING)
        << "Sidebar create-space failed: create-space response was not a dict";
    maho_string_free(ffi_result);
    return result;
  }

  const std::string* parsed_id = parsed->GetDict().FindString("id");
  if (!parsed_id || parsed_id->empty()) {
    DLOG(WARNING)
        << "Sidebar create-space failed: create-space response missing id";
    maho_string_free(ffi_result);
    return result;
  }

  result.space_id = *parsed_id;
  const std::string theme_json = BuildThemeJsonForSelection(theme_selection);
  if (!theme_json.empty()) {
    maho_theme::ApplyThemeJsonToSpace(core, result.space_id, theme_json);
  }
  result.success = true;
  maho_string_free(ffi_result);
  return result;
}

class MahoCreateSpaceThemePickerDialogView : public views::DialogDelegate {
 public:
  using ThemeChosenCallback =
      base::OnceCallback<void(MahoSidebarCreateSpaceThemeSelection)>;

  MahoCreateSpaceThemePickerDialogView(
      MahoSidebarCreateSpaceThemeSelection selection,
      ThemeChosenCallback chosen_callback)
      : selection_(selection), chosen_callback_(std::move(chosen_callback)) {
    SetButtons(0);
    SetModalType(ui::mojom::ModalType::kWindow);
    set_fixed_width(kThemeDialogWidth);

    auto* contents_view = SetContentsView(std::make_unique<views::View>());
    auto* layout = contents_view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    contents_view->SetBackground(views::CreateSolidBackground(
        GetThemeDialogShellColor()));

    auto* shell = contents_view->AddChildView(std::make_unique<views::View>());
    auto* shell_layout = shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    shell_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    shell->SetBackground(views::CreateRoundedRectBackground(
        GetThemeDialogShellColor(), kThemeDialogSurfaceCornerRadius));
    shell->SetBorder(views::CreateRoundedRectBorder(
        1, kThemeDialogSurfaceCornerRadius, GetThemeDialogCardBorderColor()));

    auto* left_panel = shell->AddChildView(std::make_unique<views::View>());
    left_panel->SetPreferredSize(gfx::Size(kThemeDialogLeftPanelWidth, 0));
    left_panel->SetBackground(
        views::CreateSolidBackground(GetThemeDialogLeftPanelColor()));
    auto* left_layout = left_panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(22, 20, 22, 20), 12));
    left_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* hero_badge = left_panel->AddChildView(
        std::make_unique<ThemeHeroBadgeView>());
    hero_badge->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 8, 6, 8));

    auto* title_label = AddLabel(left_panel, u"Create a Space",
                                 views::style::STYLE_HEADLINE_3,
                                 ui::kColorSysOnSurface, true);
    title_label->SetMaximumWidth(300);
    title_label->SetEnabledColor(SK_ColorWHITE);

    auto* subtitle_label = AddLabel(
        left_panel,
        u"Separate your tabs for life,\nwork, projects, and more.",
        views::style::STYLE_BODY_3, ui::kColorSysOnSurfaceSubtle, true);
    subtitle_label->SetMaximumWidth(300);
    subtitle_label->SetEnabledColor(GetThemeDialogSoftTextColor());
    subtitle_label->SetProperty(views::kMarginsKey,
                                gfx::Insets::TLBR(0, 0, 14, 0));

    auto make_dialog_card = [](views::View* parent, const gfx::Insets& insets,
                               int spacing) {
      auto* card = parent->AddChildView(std::make_unique<views::View>());
      auto* card_layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, insets, spacing));
      card_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      card->SetBackground(views::CreateRoundedRectBackground(
          GetThemeDialogCardColor(), 16));
      card->SetBorder(views::CreateRoundedRectBorder(
          1, 16, GetThemeDialogCardBorderColor()));
      return card;
    };

    auto* name_card = make_dialog_card(
        left_panel, gfx::Insets::TLBR(16, 14, 16, 14), 12);
    auto* name_icon_shell = name_card->AddChildView(std::make_unique<views::View>());
    name_icon_shell->SetPreferredSize(gfx::Size(36, 36));
    name_icon_shell->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF), 12));
    name_icon_shell->SetBorder(views::CreateRoundedRectBorder(
        1, 12, GetThemeDialogCardBorderColor()));
    auto* name_icon_layout = name_icon_shell->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets(), 0));
    name_icon_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    name_icon_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* plus_icon = AddLabel(name_icon_shell, u"＋",
                               views::style::STYLE_BODY_4_MEDIUM,
                               ui::kColorSysOnSurfaceSubtle);
    plus_icon->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    plus_icon->SetEnabledColor(GetThemeDialogSoftTextColor());

    auto* name_text = name_card->AddChildView(std::make_unique<views::View>());
    auto* name_text_layout = name_text->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets(), 0));
    name_text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    static_cast<views::BoxLayout*>(name_card->GetLayoutManager())
        ->SetFlexForView(name_text, 1);
    auto* name_placeholder = AddLabel(name_text, u"Space name...",
                                      views::style::STYLE_HEADLINE_5,
                                      ui::kColorSysOnSurfaceSubtle);
    name_placeholder->SetEnabledColor(GetThemeDialogSoftTextColor());

    auto* profile_card = make_dialog_card(
        left_panel, gfx::Insets::TLBR(12, 14, 12, 14), 12);
    auto* profile_glyph = profile_card->AddChildView(std::make_unique<views::View>());
    profile_glyph->SetPreferredSize(gfx::Size(36, 36));
    auto* profile_glyph_layout = profile_glyph->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets(), 0));
    profile_glyph_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    profile_glyph_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* profile_icon = AddLabel(profile_glyph, u"◡",
                                  views::style::STYLE_HEADLINE_5,
                                  ui::kColorSysOnSurfaceSubtle);
    profile_icon->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    profile_icon->SetEnabledColor(GetThemeDialogSoftTextColor());

    auto* profile_title = AddLabel(profile_card, u"Profile",
                                   views::style::STYLE_HEADLINE_5,
                                   ui::kColorSysOnSurface);
    profile_title->SetEnabledColor(SK_ColorWHITE);

    auto* profile_pill = profile_card->AddChildView(std::make_unique<views::View>());
    profile_pill->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(0x20, 0xFF, 0xFF, 0xFF), 18));
    auto* profile_pill_layout = profile_pill->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::VH(8, 14), 8));
    profile_pill_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* profile_value = AddLabel(profile_pill, u"Default",
                                   views::style::STYLE_BODY_3_MEDIUM,
                                   ui::kColorSysOnSurface);
    profile_value->SetEnabledColor(SK_ColorWHITE);
    auto* profile_chevron = AddLabel(profile_pill, u"⌄",
                                     views::style::STYLE_BODY_3_MEDIUM,
                                     ui::kColorSysOnSurfaceSubtle);
    profile_chevron->SetEnabledColor(SK_ColorWHITE);
    profile_chevron->SetHorizontalAlignment(gfx::ALIGN_CENTER);

    auto* profile_spacer = profile_card->AddChildView(std::make_unique<views::View>());
    static_cast<views::BoxLayout*>(profile_card->GetLayoutManager())
        ->SetFlexForView(profile_spacer, 1);

    auto* info_shell = profile_card->AddChildView(std::make_unique<views::View>());
    info_shell->SetPreferredSize(gfx::Size(28, 28));
    info_shell->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF), 14));
    info_shell->SetBorder(views::CreateRoundedRectBorder(
        1, 14, GetThemeDialogCardBorderColor()));
    auto* info_layout = info_shell->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets(), 0));
    info_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    info_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* info_label = AddLabel(info_shell, u"i",
                                views::style::STYLE_BODY_4_MEDIUM,
                                ui::kColorSysOnSurfaceSubtle);
    info_label->SetEnabledColor(SK_ColorWHITE);
    info_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);

    theme_button_ = left_panel->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoCreateSpaceThemePickerDialogView::OnThemePressed,
                            weak_factory_.GetWeakPtr()),
        std::u16string()));
    auto* theme_card_layout = theme_button_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::TLBR(18, 16, 18, 16), 12));
    theme_card_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    theme_button_->SetAccessibleName(u"Choose theme");
    theme_button_->SetBackground(views::CreateRoundedRectBackground(
        GetThemeDialogCardColor(), 16));
    theme_button_->SetBorder(views::CreateRoundedRectBorder(
        1, 16, GetThemeDialogCardBorderColor()));

    auto* theme_glyph_shell = theme_button_->AddChildView(std::make_unique<views::View>());
    theme_glyph_shell->SetPreferredSize(gfx::Size(36, 36));
    theme_glyph_shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    static_cast<views::BoxLayout*>(theme_glyph_shell->GetLayoutManager())
        ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
    static_cast<views::BoxLayout*>(theme_glyph_shell->GetLayoutManager())
        ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* theme_glyph = AddLabel(theme_glyph_shell, u"✎",
                                 views::style::STYLE_HEADLINE_5,
                                 ui::kColorSysOnSurfaceSubtle);
    theme_glyph->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    theme_glyph->SetEnabledColor(GetThemeDialogSoftTextColor());

    auto* theme_text = theme_button_->AddChildView(std::make_unique<views::View>());
    auto* theme_text_layout = theme_text->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), 4));
    theme_text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    theme_card_layout->SetFlexForView(theme_text, 1);
    auto* theme_title = AddLabel(theme_text, u"Choose a Theme",
                                 views::style::STYLE_HEADLINE_4,
                                 ui::kColorSysOnSurface);
    theme_title->SetEnabledColor(SK_ColorWHITE);
    theme_summary_label_ = AddLabel(theme_text, u"",
                                    views::style::STYLE_BODY_5,
                                    ui::kColorSysOnSurfaceSubtle, true);
    theme_summary_label_->SetEnabledColor(GetThemeDialogSoftTextColor());

    theme_preview_dot_ = theme_button_->AddChildView(std::make_unique<views::View>());
    theme_preview_dot_->SetPreferredSize(gfx::Size(18, 18));

    auto* left_spacer = left_panel->AddChildView(std::make_unique<views::View>());
    left_layout->SetFlexForView(left_spacer, 1);

    auto* action_row = left_panel->AddChildView(std::make_unique<views::View>());
    auto* action_layout = action_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
    action_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    dismiss_button_ = action_row->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoCreateSpaceThemePickerDialogView::OnDismissPressed,
                            weak_factory_.GetWeakPtr()),
        u"Close"));
    dismiss_button_->SetCornerRadius(16);
    dismiss_button_->SetMinSize(gfx::Size(112, 44));
    dismiss_button_->SetAccessibleName(u"Close theme picker");

    apply_button_ = action_row->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoCreateSpaceThemePickerDialogView::OnConfirmPressed,
                            weak_factory_.GetWeakPtr()),
        u"Use Theme"));
    apply_button_->SetCornerRadius(16);
    apply_button_->SetMinSize(gfx::Size(150, 44));
    apply_button_->SetAccessibleName(u"Use selected theme");

    auto* right_panel = shell->AddChildView(std::make_unique<views::View>());
    right_panel->SetBackground(
        views::CreateSolidBackground(GetThemeDialogRightPanelColor()));
    auto* right_layout = right_panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(12, 12, 14, 12), 12));
    right_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    shell_layout->SetFlexForView(right_panel, 1);

    preview_view_ = right_panel->AddChildView(std::make_unique<ThemeCanvasView>());
    preview_view_->SetPreferredSize(gfx::Size(0, kThemeCanvasHeight));
    preview_view_->SetBorder(views::CreateRoundedRectBorder(
        1, kThemeCanvasCornerRadius, GetThemeDialogCardBorderColor()));
    preview_view_->SetBackground(views::CreateRoundedRectBackground(
        GetThemeCanvasBackgroundColor(selection_), kThemeCanvasCornerRadius));
    auto* canvas_layout = preview_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(18, 18, 16, 18),
        0));
    canvas_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    right_layout->SetFlexForView(preview_view_, 1);

    auto* chip_row = preview_view_->AddChildView(std::make_unique<views::View>());
    auto* chip_layout = chip_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(4, 0, 0, 0),
        22));
    chip_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    chip_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    zen_mode_button_ = CreateCircularGlyphButton(
        chip_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              view->selection_.mode = MahoSidebarCreateSpaceThemeMode::kZen;
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"✦", u"Zen mode", kThemeChipSize, kThemeChipSize / 2);
    solid_mode_button_ = CreateCircularGlyphButton(
        chip_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              view->selection_.mode = MahoSidebarCreateSpaceThemeMode::kSolid;
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"☼", u"Solid mode", kThemeChipSize, kThemeChipSize / 2);
    passive_mode_chip_ = CreatePassiveThemeChip(chip_row, u"☾");

    auto* top_spacer = preview_view_->AddChildView(std::make_unique<views::View>());
    canvas_layout->SetFlexForView(top_spacer, 1);

    auto* message_row = preview_view_->AddChildView(std::make_unique<views::View>());
    auto* message_layout = message_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    message_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    preview_message_label_ = AddLabel(message_row,
                                      u"Tap to pick a color for this space",
                                      views::style::STYLE_HEADLINE_4,
                                      ui::kColorSysOnSurface, true);
    preview_message_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    preview_message_label_->SetMaximumWidth(450);
    preview_message_label_->SetEnabledColor(SK_ColorWHITE);

    auto* middle_spacer = preview_view_->AddChildView(std::make_unique<views::View>());
    canvas_layout->SetFlexForView(middle_spacer, 1);

    auto* brightness_row = preview_view_->AddChildView(std::make_unique<views::View>());
    auto* brightness_layout = brightness_row->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::TLBR(0, 0, 20, 0), 64));
    brightness_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    brightness_minus_button_ = CreateCircularGlyphButton(
        brightness_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              view->selection_.brightness =
                  ClampBrightness(view->selection_.brightness - 5);
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"−", u"Decrease brightness", kThemePreviewControlSize,
        kThemePreviewControlSize / 2);
    brightness_plus_button_ = CreateCircularGlyphButton(
        brightness_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              view->selection_.brightness =
                  ClampBrightness(view->selection_.brightness + 5);
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"+", u"Increase brightness", kThemePreviewControlSize,
        kThemePreviewControlSize / 2);

    auto* swatch_nav_row = right_panel->AddChildView(std::make_unique<views::View>());
    auto* swatch_nav_layout = swatch_nav_row->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::TLBR(0, 22, 0, 22), 18));
    swatch_nav_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    previous_swatch_button_ = CreateCircularGlyphButton(
        swatch_nav_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              const int max_index = static_cast<int>(std::size(kColorPresets) - 1);
              view->selection_.preset_index =
                  view->selection_.preset_index <= 0 ? max_index
                                                      : view->selection_.preset_index - 1;
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"‹", u"Previous color", kThemeChevronButtonSize,
        kThemeChevronButtonSize / 2);

    swatch_row_ = swatch_nav_row->AddChildView(std::make_unique<views::View>());
    auto* swatch_layout = swatch_row_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
            kSwatchSpacing));
    swatch_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    swatch_nav_layout->SetFlexForView(swatch_row_, 1);

    for (int i = 0; i < static_cast<int>(std::size(kColorPresets)); ++i) {
      const auto& preset = kColorPresets.at(i);
      SkScalar hsv[3] = {
          static_cast<SkScalar>(preset.hue * 360.0),
          static_cast<SkScalar>(preset.saturation),
          static_cast<SkScalar>(preset.brightness)};
      auto button = std::make_unique<views::MdTextButton>(
          base::BindRepeating(
              [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view,
                 int index) {
                if (!view) {
                  return;
                }
                view->selection_.preset_index = index;
                view->UpdateUi();
              },
               weak_factory_.GetWeakPtr(), i),
           u"");
      button->SetPreferredSize(gfx::Size(kSwatchSize, kSwatchSize));
      button->SetCornerRadius(kSwatchCornerRadius);
      button->SetAccessibleName(u"Theme color " + base::NumberToString16(i + 1));
      button->SetBgColorOverrideDeprecated(SkHSVToColor(hsv));
      button->SetMinSize(gfx::Size(kSwatchSize, kSwatchSize));
      button->SetMaxSize(gfx::Size(kSwatchSize, kSwatchSize));
      button->SetStyle(ui::ButtonStyle::kText);
      swatch_row_->AddChildView(std::move(button));
    }

    next_swatch_button_ = CreateCircularGlyphButton(
        swatch_nav_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              const int max_index = static_cast<int>(std::size(kColorPresets) - 1);
              view->selection_.preset_index =
                  view->selection_.preset_index >= max_index ? 0
                                                             : view->selection_.preset_index + 1;
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"›", u"Next color", kThemeChevronButtonSize,
        kThemeChevronButtonSize / 2);

    auto* lower_row = right_panel->AddChildView(std::make_unique<views::View>());
    auto* lower_layout = lower_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(10, 18, 4, 18), 24));
    lower_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kEnd);

    wave_view_ = lower_row->AddChildView(std::make_unique<ThemeWaveView>());
    wave_view_->SetPreferredSize(gfx::Size(0, 84));
    lower_layout->SetFlexForView(wave_view_, 1);

    grain_dial_button_ = CreateCircularGlyphButton(
        lower_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              view->selection_.grain = view->selection_.grain >= 100
                                           ? 0
                                           : ClampGrain(view->selection_.grain + 10);
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"", u"Adjust grain", kThemeGrainDialSize, kThemeGrainDialSize / 2);
    grain_dial_button_->SetBackground(nullptr);
    grain_dial_button_->SetBorder(nullptr);
    auto* dial_layout = grain_dial_button_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets(), 0));
    dial_layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
    dial_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    grain_dial_view_ = grain_dial_button_->AddChildView(std::make_unique<ThemeDialView>());
    grain_dial_view_->SetPreferredSize(
        gfx::Size(kThemeGrainDialSize, kThemeGrainDialSize));

    UpdateUi();
  }

  MahoCreateSpaceThemePickerDialogView(
      const MahoCreateSpaceThemePickerDialogView&) = delete;
  MahoCreateSpaceThemePickerDialogView& operator=(
      const MahoCreateSpaceThemePickerDialogView&) = delete;

  std::u16string GetWindowTitle() const override { return std::u16string(); }

  bool Accept() override {
    if (chosen_callback_) {
      std::move(chosen_callback_).Run(selection_);
    }
    return true;
  }

  bool Cancel() override { return true; }

  void WindowClosing() override {
    g_theme_picker_widget = nullptr;
    g_theme_picker_dialog_view = nullptr;
  }

  void SetThemeSelectionForTesting(
      const MahoSidebarCreateSpaceThemeSelection& selection) {
    selection_ = selection;
    UpdateUi();
  }

  ~MahoCreateSpaceThemePickerDialogView() override = default;

 private:
  void OnThemePressed() {
    // The theme card acts as a passive left-side affordance in the split
    // composition, so keep focus on the live composer rather than re-opening
    // nested dialogs.
    if (preview_view_) {
      preview_view_->RequestFocus();
    }
  }

  void OnConfirmPressed() {
    if (Accept() && GetWidget()) {
      GetWidget()->Close();
    }
  }

  void OnDismissPressed() {
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  void UpdateUi() {
    selection_.preset_index = ClampPresetIndex(selection_.preset_index);
    selection_.brightness = ClampBrightness(selection_.brightness);
    selection_.grain = ClampGrain(selection_.grain);
    const SkColor accent = BuildSkColorForSelection(selection_);
    const SkColor chip_background = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
    const SkColor chip_border = GetThemeDialogCardBorderColor();
    const SkColor selected_ring = SkColorSetRGB(0xE8, 0xDD, 0xD2);

    if (preview_view_) {
      preview_view_->SetBackground(views::CreateRoundedRectBackground(
          GetThemeCanvasBackgroundColor(selection_), kThemeCanvasCornerRadius));
      preview_view_->SetBorder(views::CreateRoundedRectBorder(
          1, kThemeCanvasCornerRadius, GetThemeDialogCardBorderColor()));
      preview_view_->SetSelection(selection_);
    }
    if (theme_summary_label_) {
      theme_summary_label_->SetText(BuildThemeSummaryForSelection(selection_));
      theme_summary_label_->SetEnabledColor(GetThemeDialogSoftTextColor());
    }
    if (theme_preview_dot_) {
      theme_preview_dot_->SetBackground(views::CreateRoundedRectBackground(
          accent, 9));
      theme_preview_dot_->SetBorder(views::CreateRoundedRectBorder(
          2, 9, selected_ring));
      theme_preview_dot_->SetProperty(views::kMarginsKey,
                                      gfx::Insets::TLBR(0, 8, 0, 0));
    }
    if (solid_mode_button_) {
      const bool selected =
          selection_.mode == MahoSidebarCreateSpaceThemeMode::kSolid;
      solid_mode_button_->SetStyle(ui::ButtonStyle::kText);
      solid_mode_button_->SetBackground(views::CreateRoundedRectBackground(
          selected ? accent : chip_background,
          kThemeChipSize / 2));
      solid_mode_button_->SetBorder(views::CreateRoundedRectBorder(
          1, kThemeChipSize / 2, selected ? accent : chip_border));
      solid_mode_button_->SetEnabledTextColors(
          selected ? SK_ColorWHITE : GetThemeDialogSoftTextColor());
    }
    if (zen_mode_button_) {
      const bool selected =
          selection_.mode == MahoSidebarCreateSpaceThemeMode::kZen;
      zen_mode_button_->SetStyle(ui::ButtonStyle::kText);
      zen_mode_button_->SetBackground(views::CreateRoundedRectBackground(
          selected ? accent : chip_background,
          kThemeChipSize / 2));
      zen_mode_button_->SetBorder(views::CreateRoundedRectBorder(
          1, kThemeChipSize / 2, selected ? accent : chip_border));
      zen_mode_button_->SetEnabledTextColors(
          selected ? SK_ColorWHITE : GetThemeDialogSoftTextColor());
    }
    if (passive_mode_chip_) {
      passive_mode_chip_->SetBackground(views::CreateRoundedRectBackground(
          chip_background, kThemeChipSize / 2));
      passive_mode_chip_->SetBorder(views::CreateRoundedRectBorder(
          1, kThemeChipSize / 2, chip_border));
    }
    if (swatch_row_) {
      int swatch_index = 0;
      for (views::View* child : swatch_row_->children()) {
        child->SetBorder(views::CreateRoundedRectBorder(
            swatch_index == selection_.preset_index ? 3 : 1, kSwatchCornerRadius,
            swatch_index == selection_.preset_index ? selected_ring
                                                    : GetThemeDialogMutedStrokeColor()));
        ++swatch_index;
      }
      swatch_row_->InvalidateLayout();
    }
    if (brightness_minus_button_) {
      brightness_minus_button_->SetEnabled(selection_.brightness > 15);
    }
    if (brightness_plus_button_) {
      brightness_plus_button_->SetEnabled(selection_.brightness < 100);
    }
    if (previous_swatch_button_) {
      previous_swatch_button_->SetBackground(nullptr);
      previous_swatch_button_->SetBorder(nullptr);
      previous_swatch_button_->SetEnabledTextColors(GetThemeDialogSoftTextColor());
    }
    if (next_swatch_button_) {
      next_swatch_button_->SetBackground(nullptr);
      next_swatch_button_->SetBorder(nullptr);
      next_swatch_button_->SetEnabledTextColors(GetThemeDialogSoftTextColor());
    }
    if (brightness_minus_button_) {
      brightness_minus_button_->SetBackground(nullptr);
      brightness_minus_button_->SetBorder(nullptr);
      brightness_minus_button_->SetEnabledTextColors(GetThemeDialogSoftTextColor());
    }
    if (brightness_plus_button_) {
      brightness_plus_button_->SetBackground(nullptr);
      brightness_plus_button_->SetBorder(nullptr);
      brightness_plus_button_->SetEnabledTextColors(GetThemeDialogSoftTextColor());
    }
    if (wave_view_) {
      wave_view_->SetGrain(selection_.grain);
    }
    if (grain_dial_view_) {
      grain_dial_view_->SetSelection(selection_);
    }
    if (dismiss_button_) {
      dismiss_button_->SetStyle(ui::ButtonStyle::kText);
      dismiss_button_->SetBackground(views::CreateRoundedRectBackground(
          SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF), 16));
      dismiss_button_->SetBorder(views::CreateRoundedRectBorder(
          1, 16, GetThemeDialogCardBorderColor()));
      dismiss_button_->SetEnabledTextColors(SK_ColorWHITE);
    }
    if (apply_button_) {
      apply_button_->SetStyle(ui::ButtonStyle::kText);
      apply_button_->SetBackground(
          views::CreateRoundedRectBackground(accent, 16));
      apply_button_->SetBorder(views::CreateRoundedRectBorder(
          1, 16, accent));
      apply_button_->SetEnabledTextColors(SK_ColorWHITE);
    }
  }

  MahoSidebarCreateSpaceThemeSelection selection_;
  ThemeChosenCallback chosen_callback_;
  raw_ptr<views::Button> theme_button_ = nullptr;
  raw_ptr<ThemeCanvasView> preview_view_ = nullptr;
  raw_ptr<views::Label> preview_message_label_ = nullptr;
  raw_ptr<views::Label> theme_summary_label_ = nullptr;
  raw_ptr<views::View> theme_preview_dot_ = nullptr;
  raw_ptr<views::View> swatch_row_ = nullptr;
  raw_ptr<views::MdTextButton> solid_mode_button_ = nullptr;
  raw_ptr<views::MdTextButton> zen_mode_button_ = nullptr;
  raw_ptr<views::View> passive_mode_chip_ = nullptr;
  raw_ptr<views::MdTextButton> brightness_minus_button_ = nullptr;
  raw_ptr<views::MdTextButton> brightness_plus_button_ = nullptr;
  raw_ptr<views::MdTextButton> previous_swatch_button_ = nullptr;
  raw_ptr<views::MdTextButton> next_swatch_button_ = nullptr;
  raw_ptr<ThemeWaveView> wave_view_ = nullptr;
  raw_ptr<views::MdTextButton> grain_dial_button_ = nullptr;
  raw_ptr<ThemeDialView> grain_dial_view_ = nullptr;
  raw_ptr<views::MdTextButton> dismiss_button_ = nullptr;
  raw_ptr<views::MdTextButton> apply_button_ = nullptr;
  base::WeakPtrFactory<MahoCreateSpaceThemePickerDialogView> weak_factory_{this};
};

void ShowThemePickerDialog(
    Browser* browser,
    const MahoSidebarCreateSpaceThemeSelection& initial_selection,
    MahoCreateSpaceThemePickerDialogView::ThemeChosenCallback chosen_callback) {
  if (!browser || !browser->window()) {
    return;
  }

  if (g_theme_picker_widget) {
    g_theme_picker_widget->CloseNow();
  }

  auto* dialog = new MahoCreateSpaceThemePickerDialogView(
      initial_selection, std::move(chosen_callback));
  auto* weak_dialog = dialog;
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      dialog, browser->window()->GetNativeWindow(), gfx::NativeView());
  if (!widget) {
    return;
  }

  g_theme_picker_dialog_view = weak_dialog;
  g_theme_picker_widget = widget;
  widget->Show();
  widget->Activate();
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

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  layout->SetFlexForView(scroll_view_, 1, true);

  auto* content = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* content_layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kContentInsets, 12));
  content_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

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
  title_label->SetMaximumWidth(360);

  auto* subtitle_label = AddLabel(
      hero,
      u"Set up a focused home for tabs, notes, and future personalizations.",
      views::style::STYLE_BODY_5, ui::kColorSysOnSurfaceSubtle, true);
  subtitle_label->SetMaximumWidth(360);

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

  theme_button_ = card_stack->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoSidebarCreateSpaceView::OnThemePressed,
                          weak_factory_.GetWeakPtr()),
      std::u16string()));
  auto* theme_card_layout = theme_button_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         kCardInsets, 10));
  theme_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  theme_button_->SetAccessibleName(u"Choose theme");
  theme_button_->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  theme_button_->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  CreateGlyphBadge(theme_button_, u"◐");

  auto* theme_header_text =
      theme_button_->AddChildView(std::make_unique<views::View>());
  auto* theme_header_text_layout = theme_header_text->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 2));
  theme_header_text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  theme_card_layout->SetFlexForView(theme_header_text, 1);
  AddLabel(theme_header_text, u"Theme", views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  theme_summary_label_ = AddLabel(theme_header_text, u"",
                                  views::style::STYLE_BODY_5,
                                  ui::kColorSysOnSurfaceSubtle, true);

  theme_preview_dot_ =
      theme_button_->AddChildView(std::make_unique<views::View>());
  theme_preview_dot_->SetPreferredSize(
      gfx::Size(kThemePreviewDotSize, kThemePreviewDotSize));

  auto* trailing = AddLabel(theme_button_, u"›",
                            views::style::STYLE_BODY_3_MEDIUM,
                            ui::kColorSysOnSurfaceSubtle);
  trailing->SetHorizontalAlignment(gfx::ALIGN_CENTER);

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
  create_button_->SetCornerRadius(16);
  create_button_->SetMinSize(gfx::Size(0, 46));
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

  ResetFormState();
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

void MahoSidebarCreateSpaceView::PrepareForOpen() {
  ResetFormState();
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

void MahoSidebarCreateSpaceView::ResetFormState() {
  theme_selection_ = DefaultThemeSelection();
  if (name_field_) {
    name_field_->SetText(u"");
  }
  if (error_label_) {
    error_label_->SetVisible(false);
  }
  UpdateThemeSummary();
  UpdateCreateButtonState();
}

void MahoSidebarCreateSpaceView::UpdateThemeSummary() {
  if (theme_summary_label_) {
    theme_summary_label_->SetText(BuildThemeSummaryForSelection(theme_selection_));
  }
  if (theme_preview_dot_) {
    theme_preview_dot_->SetBackground(views::CreateRoundedRectBackground(
        BuildSkColorForSelection(theme_selection_),
        kThemePreviewDotCornerRadius));
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
                     theme_selection_),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
             CreateResult result) {
            if (view) {
              view->OnSpaceCreated(result.success, result.space_id);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarCreateSpaceView::OnThemePressed() {
  if (!browser_) {
    return;
  }

  ShowThemePickerDialog(
      browser_, theme_selection_,
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
             MahoSidebarCreateSpaceThemeSelection selection) {
            if (view) {
              view->ApplyThemeSelection(selection);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarCreateSpaceView::OnCancelPressed() {
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  if (GetWidget()) {
    GetWidget()->Close();
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

// static
views::Widget* MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() {
  return g_theme_picker_widget;
}

// static
void MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  if (g_theme_picker_dialog_view) {
    g_theme_picker_dialog_view->SetThemeSelectionForTesting(selection);
  }
}

// static
bool MahoSidebarCreateSpaceView::AcceptThemePickerDialogForTesting() {
  if (!g_theme_picker_dialog_view || !g_theme_picker_widget) {
    return false;
  }
  if (!g_theme_picker_dialog_view->Accept()) {
    return false;
  }
  g_theme_picker_widget->CloseNow();
  return true;
}

void MahoSidebarCreateSpaceView::ApplyThemeSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  theme_selection_ = selection;
  UpdateThemeSummary();
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

  ResetFormState();
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  SetVisible(false);
}

}  // namespace maho
