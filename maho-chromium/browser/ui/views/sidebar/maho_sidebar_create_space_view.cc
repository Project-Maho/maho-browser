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
#include "base/task/sequenced_task_runner.h"
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
#include "ui/display/screen.h"
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
constexpr int kThemeDialogWidth = 680;
constexpr int kThemeDialogSurfaceCornerRadius = 30;
constexpr int kThemeChipSize = 52;
constexpr int kThemeCanvasCornerRadius = 30;
constexpr int kThemeCanvasHeight = 596;
constexpr int kThemePreviewControlSize = 24;
constexpr int kThemeChevronButtonSize = 30;
constexpr int kThemeGrainDialSize = 98;
constexpr int kThemeCanvasDotSpacing = 11;
constexpr float kPi = 3.14159265f;

constexpr int kChooserShellInsetTop = 8;
constexpr int kChooserShellInsetHoriz = 8;
constexpr int kChooserShellInsetBottom = 10;
constexpr int kChooserShellSectionGap = 10;

constexpr int kChooserCanvasInsetTopLeft = 18;
constexpr int kChooserCanvasInsetBottom = 16;
constexpr int kChooserChipRowTopPad = 4;
constexpr int kChooserChipRowGap = 22;

constexpr int kChooserSwatchNavHorizPad = 20;
constexpr int kChooserSwatchNavBottomPad = 4;
constexpr int kChooserSwatchNavGap = 18;

constexpr int kChooserLowerTopPad = 14;
constexpr int kChooserLowerLeftPad = 18;
constexpr int kChooserLowerBottomPad = 12;
constexpr int kChooserLowerRightPad = 24;
constexpr int kChooserLowerGap = 24;
constexpr int kChooserWaveViewHeight = 84;

constexpr int kChooserBrightnessRowBottomPad = 20;
constexpr int kChooserBrightnessRowGap = 64;

constexpr int kChooserBrightnessMin = 15;
constexpr int kChooserBrightnessMax = 100;
constexpr int kChooserBrightnessStep = 5;

constexpr int kChooserGrainMin = 0;
constexpr int kChooserGrainMax = 100;
constexpr int kChooserGrainStep = 10;

constexpr int kChooserSwatchSelectedBorderWidth = 3;
constexpr int kChooserSwatchUnselectedBorderWidth = 1;

constexpr int kChooserFooterTopPadding = 4;
constexpr int kChooserFooterBottomPadding = 4;
constexpr int kChooserFooterHorizPadding = 16;
constexpr int kChooserFooterButtonSpacing = 8;
constexpr int kChooserDoneButtonCornerRadius = 12;
constexpr int kChooserPreviewMessageMaxWidth = 450;
// Fraction of the available work-area height the chooser is allowed to occupy.
// Keeps the footer reachable on smaller displays without capping the preferred
// height on typical screens.
constexpr double kChooserMaxWorkAreaHeightFraction = 0.90;

// ThemeWaveView paint geometry.
constexpr float kWaveHorizMargin        = 8.0f;   // px inset from left/right edge
constexpr float kWaveDotStep            = 6.0f;   // px between successive wave dots
constexpr float kWaveAngularFrequency   = 0.58f;  // radians per dot index
constexpr float kWaveDotRadius          = 1.9f;   // px radius of each wave dot
constexpr float kWaveAmplitudeBase      = 4.0f;   // px amplitude at grain=0
constexpr float kWaveAmplitudeScale     = 9.0f;   // additional px amplitude at grain=100

// ThemeDialView paint geometry.
constexpr float kDialOuterEdgeInset     = 6.0f;   // px gap between bounds edge and outer ring
constexpr float kDialRingTrackWidth     = 10.0f;  // px radial distance between outer and inner rings
constexpr float kDialRingStrokeWidth    = 1.2f;   // px stroke width for the inner ring
constexpr int   kDialDotCount           = 18;     // number of tick dots on the outer ring
constexpr float kDialDotRadius          = 1.5f;   // px radius of each tick dot
constexpr float kDialKnobArcFraction    = 1.8f;   // fraction of 2π swept by the knob
constexpr float kDialKnobDotRadius      = 4.0f;   // px radius of the knob indicator dot

// ThemeCanvasView ambient-glow circle layout fractions (relative to canvas
// bounds width/height).
constexpr float kCanvasAmbient1CenterXFrac = 0.77f;
constexpr float kCanvasAmbient1CenterYFrac = 0.27f;
constexpr float kCanvasAmbient1RadiusFrac  = 0.23f;
constexpr float kCanvasAmbient2CenterXFrac = 0.22f;
constexpr float kCanvasAmbient2CenterYFrac = 0.82f;
constexpr float kCanvasAmbient2RadiusFrac  = 0.18f;

// ThemeCanvasView dot-grid margins (pixels) and dot radius.
constexpr int   kCanvasDotGridMargin  = 16;
constexpr int   kCanvasDotGridInsetX  = 18;
constexpr float kCanvasDotRadius      = 1.2f;

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
  return std::clamp(brightness, kChooserBrightnessMin, kChooserBrightnessMax);
}

int ClampGrain(int grain) {
  return std::clamp(grain, kChooserGrainMin, kChooserGrainMax);
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
  theme_dict.Set("type", selection.mode == MahoSidebarCreateSpaceThemeMode::kSolid
                             ? "solid"
                             : "gradient");
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

SkColor GetThemeDialogBackgroundColor() {
  return SkColorSetARGB(0xEF, 0x22, 0x24, 0x2E);
}


SkColor GetThemeDialogShellBorderColor() {
  return SkColorSetARGB(0x22, 0xFF, 0xFF, 0xFF);
}

SkColor GetThemeDialogCanvasBorderColor() {
  return SkColorSetARGB(0x34, 0xFF, 0xFF, 0xFF);
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
        gfx::PointF(bounds.x() + bounds.width() * kCanvasAmbient1CenterXFrac,
                    bounds.y() + bounds.height() * kCanvasAmbient1CenterYFrac),
        bounds.height() * kCanvasAmbient1RadiusFrac, ambient_flags);

    ambient_flags.setColor(WithAlpha(accent, 0x10));
    canvas->DrawCircle(
        gfx::PointF(bounds.x() + bounds.width() * kCanvasAmbient2CenterXFrac,
                    bounds.y() + bounds.height() * kCanvasAmbient2CenterYFrac),
        bounds.height() * kCanvasAmbient2RadiusFrac, ambient_flags);

    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);
    dot_flags.setColor(WithAlpha(SK_ColorWHITE, 0x30));

    for (int row = 0, y = bounds.y() + kCanvasDotGridMargin;
         y < bounds.bottom() - kCanvasDotGridMargin;
         y += kThemeCanvasDotSpacing, ++row) {
      const int x_offset = row % 2 == 0 ? 0 : kThemeCanvasDotSpacing / 2;
      for (int x = bounds.x() + kCanvasDotGridInsetX + x_offset;
           x < bounds.right() - kCanvasDotGridInsetX;
           x += kThemeCanvasDotSpacing) {
        canvas->DrawCircle(gfx::PointF(x, y), kCanvasDotRadius, dot_flags);
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
    wave_flags.setColor(SkColorSetARGB(0x6E, 0x98, 0x9C, 0xA8));

    const float amplitude = kWaveAmplitudeBase + (grain_ / 100.0f) * kWaveAmplitudeScale;
    const float center_y = bounds.CenterPoint().y();
    const float start_x = bounds.x() + kWaveHorizMargin;
    const float step = kWaveDotStep;
    const int sample_count = std::max(
        1, (bounds.width() - static_cast<int>(kWaveHorizMargin * 2)) /
               static_cast<int>(kWaveDotStep));
    for (int i = 0; i <= sample_count; ++i) {
      const float x = start_x + i * step;
      const float y = center_y + std::sin(i * kWaveAngularFrequency) * amplitude;
      canvas->DrawCircle(gfx::PointF(x, y), kWaveDotRadius, wave_flags);
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
    const float outer_radius =
        std::min(bounds.width(), bounds.height()) / 2.0f - kDialOuterEdgeInset;
    const float inner_radius = outer_radius - kDialRingTrackWidth;
    const SkColor accent = BuildSkColorForSelection(selection_);

    cc::PaintFlags ring_flags;
    ring_flags.setAntiAlias(true);
    ring_flags.setStyle(cc::PaintFlags::kStroke_Style);
    ring_flags.setStrokeWidth(kDialRingStrokeWidth);
    ring_flags.setColor(SkColorSetARGB(0x56, 0xB2, 0xB7, 0xC2));
    canvas->DrawCircle(center, inner_radius, ring_flags);

    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);
    dot_flags.setColor(SkColorSetARGB(0x50, 0xB2, 0xB7, 0xC2));
    for (int i = 0; i < kDialDotCount; ++i) {
      const float theta =
          (kPi * 2.0f * i) / kDialDotCount - (kPi / 2.0f);
      canvas->DrawCircle(
          gfx::PointF(center.x() + std::cos(theta) * outer_radius,
                      center.y() + std::sin(theta) * outer_radius),
          kDialDotRadius, dot_flags);
    }

    const float knob_theta =
        (-kPi / 2.0f) + ((selection_.grain / 100.0f) * kPi * kDialKnobArcFraction);
    cc::PaintFlags knob_flags;
    knob_flags.setAntiAlias(true);
    knob_flags.setStyle(cc::PaintFlags::kFill_Style);
    knob_flags.setColor(accent);
    canvas->DrawCircle(
        gfx::PointF(center.x() + std::cos(knob_theta) * inner_radius,
                    center.y() + std::sin(knob_theta) * inner_radius),
        kDialKnobDotRadius, knob_flags);
  }

 private:
  MahoSidebarCreateSpaceThemeSelection selection_;
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

class ThemePickerContentsView : public views::View {
 public:
  ThemePickerContentsView() = default;
  ThemePickerContentsView(const ThemePickerContentsView&) = delete;
  ThemePickerContentsView& operator=(const ThemePickerContentsView&) = delete;
  ~ThemePickerContentsView() override = default;

  void SetAcceleratorHandlers(base::RepeatingCallback<bool()> on_accept,
                              base::RepeatingCallback<bool()> on_cancel) {
    on_accept_ = std::move(on_accept);
    on_cancel_ = std::move(on_cancel);
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (accelerator.key_code() == ui::VKEY_RETURN) {
      if (on_accept_ && on_accept_.Run()) {
        if (auto* widget = GetWidget()) {
          widget->Close();
        }
      }
      return true;
    }
    if (accelerator.key_code() == ui::VKEY_ESCAPE) {
      if (on_cancel_ && on_cancel_.Run()) {
        if (auto* widget = GetWidget()) {
          widget->Close();
        }
      }
      return true;
    }
    return false;
  }

 private:
  base::RepeatingCallback<bool()> on_accept_;
  base::RepeatingCallback<bool()> on_cancel_;
};

class MahoCreateSpaceThemePickerDialogView : public views::DialogDelegate {
 public:
  using ThemeChosenCallback =
      base::OnceCallback<void(MahoSidebarCreateSpaceThemeSelection)>;

  MahoCreateSpaceThemePickerDialogView(
      MahoSidebarCreateSpaceThemeSelection selection,
      ThemeChosenCallback chosen_callback)
      : selection_(selection), chosen_callback_(std::move(chosen_callback)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetShowCloseButton(false);
    SetShowTitle(false);
    SetCanResize(false);
    // kWindow: blocks interaction with the parent browser window while the
    // frameless chooser is open.  params.parent (not params.context) is set in
    // ShowThemePickerDialog() to satisfy the parent-widget relationship that
    // kWindow modality requires.
    SetModalType(ui::mojom::ModalType::kWindow);
    set_fixed_width(kThemeDialogWidth);

    auto* contents_view = SetContentsView(std::make_unique<ThemePickerContentsView>());
    contents_view->SetAcceleratorHandlers(
        base::BindRepeating(&MahoCreateSpaceThemePickerDialogView::Accept,
                            base::Unretained(this)),
        base::BindRepeating(&MahoCreateSpaceThemePickerDialogView::Cancel,
                            base::Unretained(this)));
    contents_view->AddAccelerator(
        ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
    contents_view->AddAccelerator(
        ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
    auto* layout = contents_view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(kChooserShellInsetTop, kChooserShellInsetHoriz,
                           kChooserShellInsetBottom, kChooserShellInsetHoriz),
        kChooserShellSectionGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    contents_view->SetBackground(views::CreateRoundedRectBackground(
        GetThemeDialogBackgroundColor(), kThemeDialogSurfaceCornerRadius));
    contents_view->SetBorder(views::CreateRoundedRectBorder(
        1, kThemeDialogSurfaceCornerRadius, GetThemeDialogShellBorderColor()));

    preview_view_ = contents_view->AddChildView(std::make_unique<ThemeCanvasView>());
    preview_view_->SetPreferredSize(gfx::Size(0, kThemeCanvasHeight));
    preview_view_->SetBorder(views::CreateRoundedRectBorder(
        1, kThemeCanvasCornerRadius, GetThemeDialogCanvasBorderColor()));
    preview_view_->SetBackground(views::CreateRoundedRectBackground(
        GetThemeCanvasBackgroundColor(selection_), kThemeCanvasCornerRadius));
    auto* canvas_layout = preview_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(kChooserCanvasInsetTopLeft, kChooserCanvasInsetTopLeft,
                           kChooserCanvasInsetBottom, kChooserCanvasInsetTopLeft),
        0));
    canvas_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    layout->SetFlexForView(preview_view_, 1);

    auto* chip_row = preview_view_->AddChildView(std::make_unique<views::View>());
    auto* chip_layout = chip_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kChooserChipRowTopPad, 0, 0, 0),
        kChooserChipRowGap));
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
    preview_message_label_->SetMaximumWidth(kChooserPreviewMessageMaxWidth);
    preview_message_label_->SetEnabledColor(SK_ColorWHITE);

    auto* middle_spacer = preview_view_->AddChildView(std::make_unique<views::View>());
    canvas_layout->SetFlexForView(middle_spacer, 1);

    auto* brightness_row = preview_view_->AddChildView(std::make_unique<views::View>());
    auto* brightness_layout = brightness_row->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::TLBR(0, 0, kChooserBrightnessRowBottomPad, 0),
                                           kChooserBrightnessRowGap));
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
                  ClampBrightness(view->selection_.brightness - kChooserBrightnessStep);
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
                  ClampBrightness(view->selection_.brightness + kChooserBrightnessStep);
              view->UpdateUi();
            },
            weak_factory_.GetWeakPtr()),
        u"+", u"Increase brightness", kThemePreviewControlSize,
        kThemePreviewControlSize / 2);

    auto* swatch_nav_row = contents_view->AddChildView(std::make_unique<views::View>());
    auto* swatch_nav_layout = swatch_nav_row->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::TLBR(0, kChooserSwatchNavHorizPad,
                                                              kChooserSwatchNavBottomPad,
                                                              kChooserSwatchNavHorizPad),
                                           kChooserSwatchNavGap));
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

    auto swatch_scroll = std::make_unique<views::ScrollView>();
    swatch_scroll->SetBackgroundColor(std::nullopt);
    swatch_scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kHiddenButEnabled);
    swatch_scroll->SetVerticalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    auto swatch_row_owned = std::make_unique<views::View>();
    swatch_row_ = swatch_row_owned.get();
    auto* swatch_layout = swatch_row_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
            kSwatchSpacing));
    swatch_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    swatch_scroll->SetContents(std::move(swatch_row_owned));
    auto* swatch_scroll_view =
        swatch_nav_row->AddChildView(std::move(swatch_scroll));
    swatch_nav_layout->SetFlexForView(swatch_scroll_view, 1);

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

    auto* lower_row = contents_view->AddChildView(std::make_unique<views::View>());
    auto* lower_layout = lower_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kChooserLowerTopPad, kChooserLowerLeftPad,
                           kChooserLowerBottomPad, kChooserLowerRightPad),
        kChooserLowerGap));
    lower_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kEnd);

    wave_view_ = lower_row->AddChildView(std::make_unique<ThemeWaveView>());
    wave_view_->SetPreferredSize(gfx::Size(0, kChooserWaveViewHeight));
    lower_layout->SetFlexForView(wave_view_, 1);

    grain_dial_button_ = CreateCircularGlyphButton(
        lower_row,
        base::BindRepeating(
            [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
              if (!view) {
                return;
              }
              view->selection_.grain = view->selection_.grain >= kChooserGrainMax
                                           ? kChooserGrainMin
                                           : ClampGrain(view->selection_.grain + kChooserGrainStep);
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

    auto* footer_row = contents_view->AddChildView(std::make_unique<views::View>());
    auto* footer_layout = footer_row->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(kChooserFooterTopPadding,
                               kChooserFooterHorizPadding,
                               kChooserFooterBottomPadding,
                               kChooserFooterHorizPadding),
            kChooserFooterButtonSpacing));
    footer_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kEnd);
    footer_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    cancel_footer_button_ = footer_row->AddChildView(
        std::make_unique<views::MdTextButton>(
            base::BindRepeating(
                [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
                  if (!view) {
                    return;
                  }
                  if (view->Cancel()) {
                    if (auto* widget = view->GetWidget()) {
                      widget->Close();
                    }
                  }
                },
                weak_factory_.GetWeakPtr()),
            u"Cancel"));
    cancel_footer_button_->SetStyle(ui::ButtonStyle::kText);
    cancel_footer_button_->SetAccessibleName(u"Cancel theme chooser");
    cancel_footer_button_->SetEnabledTextColors(GetThemeDialogSoftTextColor());

    done_button_ = footer_row->AddChildView(
        std::make_unique<views::MdTextButton>(
            base::BindRepeating(
                [](base::WeakPtr<MahoCreateSpaceThemePickerDialogView> view) {
                  if (!view) {
                    return;
                  }
                  if (view->Accept()) {
                    if (auto* widget = view->GetWidget()) {
                      widget->Close();
                    }
                  }
                },
                weak_factory_.GetWeakPtr()),
            u"Done"));
    done_button_->SetStyle(ui::ButtonStyle::kProminent);
    done_button_->SetCornerRadius(kChooserDoneButtonCornerRadius);
    done_button_->SetAccessibleName(u"Apply theme");

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
  void UpdateUi() {
    selection_.preset_index = ClampPresetIndex(selection_.preset_index);
    selection_.brightness = ClampBrightness(selection_.brightness);
    selection_.grain = ClampGrain(selection_.grain);
    const SkColor accent = BuildSkColorForSelection(selection_);
    const SkColor chip_background = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
    const SkColor chip_border = GetThemeDialogCanvasBorderColor();
    const SkColor selected_ring = SkColorSetRGB(0xE8, 0xDD, 0xD2);

    if (preview_view_) {
      preview_view_->SetBackground(views::CreateRoundedRectBackground(
          GetThemeCanvasBackgroundColor(selection_), kThemeCanvasCornerRadius));
      preview_view_->SetBorder(views::CreateRoundedRectBorder(
          1, kThemeCanvasCornerRadius, GetThemeDialogCanvasBorderColor()));
      preview_view_->SetSelection(selection_);
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
            swatch_index == selection_.preset_index ? kChooserSwatchSelectedBorderWidth
                                                    : kChooserSwatchUnselectedBorderWidth,
            kSwatchCornerRadius,
            swatch_index == selection_.preset_index ? selected_ring
                                                    : GetThemeDialogMutedStrokeColor()));
        ++swatch_index;
      }
      swatch_row_->InvalidateLayout();
    }
    if (brightness_minus_button_) {
      brightness_minus_button_->SetEnabled(selection_.brightness > kChooserBrightnessMin);
    }
    if (brightness_plus_button_) {
      brightness_plus_button_->SetEnabled(selection_.brightness < kChooserBrightnessMax);
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
  }

  MahoSidebarCreateSpaceThemeSelection selection_;
  ThemeChosenCallback chosen_callback_;
  raw_ptr<ThemeCanvasView> preview_view_ = nullptr;
  raw_ptr<views::Label> preview_message_label_ = nullptr;
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
  raw_ptr<views::MdTextButton> done_button_ = nullptr;
  raw_ptr<views::MdTextButton> cancel_footer_button_ = nullptr;
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

  const gfx::Rect browser_bounds = browser->window()->GetBounds();
  // NOTE: display::Screen::GetScreen() was renamed to display::Screen::Get()
  // in upstream Chromium (circa M130+). If rebasing onto an older Chromium
  // version, revert to GetScreen().
  const gfx::Rect work_area =
      display::Screen::Get()
          ->GetDisplayMatching(browser_bounds)
          .work_area();
  const int chooser_width =
      std::min(kThemeDialogWidth, work_area.width());
  const int max_height =
      static_cast<int>(work_area.height() * kChooserMaxWorkAreaHeightFraction);

  const gfx::Size preferred =
      dialog->GetContentsView()->GetPreferredSize(
          views::SizeBounds(chooser_width, {}));
  const int unclamped_height =
      preferred.height() > 0
          ? preferred.height()
          : dialog->GetContentsView()->GetPreferredSize().height();

  const gfx::Size dialog_size(chooser_width,
                               std::min(unclamped_height, max_height));

  const int raw_origin_x =
      browser_bounds.x() +
          (browser_bounds.width() - dialog_size.width()) / 2;
  const int clamped_origin_x =
      std::clamp(raw_origin_x, work_area.x(),
                 work_area.right() - dialog_size.width());
  const gfx::Point origin(
      clamped_origin_x,
      work_area.y() +
          (work_area.height() - dialog_size.height()) / 2);

  views::Widget::InitParams params(
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.delegate = dialog;
  params.context = browser->window()->GetNativeWindow();
  params.bounds = gfx::Rect(origin, dialog_size);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;

  auto* widget = new views::Widget();
  widget->Init(std::move(params));

  g_theme_picker_dialog_view = dialog;
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
