// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_result_row_view.h"

#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"
#include "cc/paint/paint_flags.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_sparkle_geometry.h"
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/codec/SkWebpDecoder.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkStream.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/range/range.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

int PreferredFaviconSize(const std::optional<ImageData>& icon,
                         int glyph_icon_size) {
  if (!icon.has_value()) {
    return glyph_icon_size;
  }

  const int source_size = std::max(icon->width, icon->height);
  if (source_size <= 0) {
    return glyph_icon_size;
  }

  return std::min(glyph_icon_size, source_size);
}

gfx::Image DecodeFaviconImage(const ImageData& icon) {
  if (icon.data.empty()) {
    return gfx::Image();
  }

  if (icon.format == "png" || icon.format == "image/png") {
    return gfx::Image::CreateFrom1xPNGBytes(icon.data);
  }

  if (icon.format == "jpeg" || icon.format == "jpg" ||
      icon.format == "image/jpeg") {
    auto bitmap = gfx::JPEGCodec::Decode(base::as_byte_span(icon.data));
    if (bitmap.isNull()) {
      return gfx::Image();
    }
    return gfx::Image::CreateFrom1xBitmap(bitmap);
  }

  if (icon.format == "webp" || icon.format == "image/webp") {
    auto stream = std::make_unique<SkMemoryStream>(icon.data.data(),
                                                   icon.data.size(),
                                                   /*copyData=*/false);
    SkCodec::Result result;
    std::unique_ptr<SkCodec> codec = SkWebpDecoder::Decode(std::move(stream),
                                                           &result);
    if (!codec || result != SkCodec::kSuccess) {
      return gfx::Image();
    }

    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(codec->getInfo())) {
      return gfx::Image();
    }

    if (codec->getPixels(bitmap.pixmap()) != SkCodec::kSuccess) {
      return gfx::Image();
    }

    return gfx::Image::CreateFrom1xBitmap(bitmap);
  }

  return gfx::Image();
}

std::u16string HostCompactMetadataText(const std::u16string& subtitle_text) {
  const GURL url(base::UTF16ToUTF8(subtitle_text));
  if (url.is_valid() && !url.host().empty()) {
    return base::UTF8ToUTF16(url.host());
  }
  return subtitle_text;
}

std::u16string SeededCompactMetadataText(const std::u16string& subtitle_text) {
  const GURL url(base::UTF16ToUTF8(subtitle_text));
  if (!url.is_valid()) {
    return subtitle_text;
  }

  if (url.SchemeIs("about")) {
    return base::UTF8ToUTF16(url.spec());
  }

  const bool has_meaningful_path = !url.path().empty() && url.path() != "/";
  if (has_meaningful_path || url.has_query() || url.has_ref()) {
    std::string formatted = url.spec();
    if (url.SchemeIsHTTPOrHTTPS()) {
      const size_t scheme_delimiter = formatted.find("://");
      if (scheme_delimiter != std::string::npos) {
        formatted = formatted.substr(scheme_delimiter + 3);
      }
    }
    return base::UTF8ToUTF16(formatted);
  }

  if (!url.host().empty()) {
    return base::UTF8ToUTF16(url.host());
  }
  return subtitle_text;
}

constexpr int kDefaultRowHeightDp = 46;
constexpr int kCurrentTabSeededRowHeightDp = 36;
constexpr int kRowCornerRadiusDp = 12;
constexpr int kCurrentTabSeededRowCornerRadiusDp = 6;
constexpr int kRowHorizontalInsetDp = 4;
constexpr int kDefaultRowVerticalInsetDp = 0;
constexpr int kCurrentTabSeededRowVerticalInsetDp = 1;
constexpr int kDefaultRowPaddingHDp = 10;
constexpr int kCurrentTabSeededRowPaddingHDp = 12;
constexpr int kGlyphTileSizeDp = 28;
constexpr int kCurrentTabSeededGlyphTileSizeDp = 20;
constexpr int kGlyphIconSizeDp = 20;
constexpr int kCurrentTabSeededGlyphIconSizeDp = 14;
constexpr int kGlyphCornerRadiusDp = 0;
constexpr int kCurrentTabSeededGlyphCornerRadiusDp = 0;
constexpr int kCurrentTabSeededSelectedFaviconTileSizeDp = 16;
constexpr int kCurrentTabSeededSelectedFaviconImageSizeDp = 12;
constexpr int kCurrentTabSeededFaviconTileCornerRadiusDp = 5;
constexpr int kShortcutCornerRadiusDp = 8;
constexpr int kShortcutPaddingHDp = 6;
constexpr int kShortcutPaddingVDp = 3;
constexpr int kDefaultRowItemSpacingDp = 8;
constexpr int kCurrentTabSeededRowItemSpacingDp = 12;
constexpr int kCompactTextSpacingDp = 5;
constexpr int kShortcutSpacingDp = 8;
constexpr int kSelectedSubtitleTopInsetDp = 1;
constexpr gfx::Insets kDefaultRowSelectionInsets = gfx::Insets::VH(
    kDefaultRowVerticalInsetDp, kRowHorizontalInsetDp);
constexpr gfx::Insets kCurrentTabSeededRowSelectionInsets =
    gfx::Insets::TLBR(kCurrentTabSeededRowVerticalInsetDp,
                      kRowHorizontalInsetDp,
                      kCurrentTabSeededRowVerticalInsetDp,
                      kRowHorizontalInsetDp);
constexpr gfx::Font::Weight kSeededRowTitleFontWeight =
    gfx::Font::Weight::MEDIUM;

gfx::Insets SelectionInsetsForPresentation(
    MahoCommandResultRowView::PresentationStyle style) {
  return style == MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded
             ? kCurrentTabSeededRowSelectionInsets
             : kDefaultRowSelectionInsets;
}

int SelectionCornerRadiusForPresentation(
    MahoCommandResultRowView::PresentationStyle style) {
  return style == MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded
             ? kCurrentTabSeededRowCornerRadiusDp
             : kRowCornerRadiusDp;
}

SkColor GlyphForegroundColor(CommandSuggestionType type) {
  switch (type) {
    case CommandSuggestionType::kTab:
      return SkColorSetRGB(0xEE, 0xF1, 0xF8);
    case CommandSuggestionType::kBookmark:
      return SkColorSetRGB(0xF0, 0xC6, 0x66);
    case CommandSuggestionType::kHistory:
      return SkColorSetRGB(0xBE, 0xC7, 0xD9);
    case CommandSuggestionType::kAction:
      return SkColorSetRGB(0xC8, 0x92, 0xFF);
    case CommandSuggestionType::kNavigation:
      return SkColorSetRGB(0x86, 0xE0, 0xCB);
    case CommandSuggestionType::kSearch:
      return SkColorSetRGB(0xA8, 0xC4, 0xF0);  // soft blue (search)
    case CommandSuggestionType::kCalculator:
      return SkColorSetRGB(0xFF, 0xA3, 0x7E);
    case CommandSuggestionType::kUnitConversion:
      return SkColorSetRGB(0xF2, 0x9A, 0x9A);
    case CommandSuggestionType::kArchivedTab:
      return SkColorSetRGB(0xA5, 0xB7, 0xD2);
    case CommandSuggestionType::kClosedTab:
      return SkColorSetRGB(0xC3, 0xA6, 0xEB);
    case CommandSuggestionType::kFolder:
      return SkColorSetRGB(0x8C, 0xA6, 0xFF);
    case CommandSuggestionType::kRecentSearch:
      return SkColorSetRGB(0xB1, 0xBC, 0xD7);
  }
  return SkColorSetRGB(0xBE, 0xC7, 0xD9);
}

void DrawLineIcon(gfx::Canvas* canvas,
                  const gfx::RectF& rect,
                  CommandSuggestionType type,
                  SkColor color) {
  cc::PaintFlags stroke_flags;
  stroke_flags.setAntiAlias(true);
  stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
  stroke_flags.setStrokeWidth(1.6f);
  stroke_flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  stroke_flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  stroke_flags.setColor(color);

  cc::PaintFlags fill_flags;
  fill_flags.setAntiAlias(true);
  fill_flags.setStyle(cc::PaintFlags::kFill_Style);
  fill_flags.setColor(color);

  switch (type) {
    case CommandSuggestionType::kBookmark: {
      const SkPath path = SkPathBuilder()
                              .moveTo(rect.x() + rect.width() * 0.28f,
                                      rect.y() + rect.height() * 0.18f)
                              .lineTo(rect.x() + rect.width() * 0.72f,
                                      rect.y() + rect.height() * 0.18f)
                              .lineTo(rect.x() + rect.width() * 0.72f,
                                      rect.y() + rect.height() * 0.82f)
                              .lineTo(rect.x() + rect.width() * 0.50f,
                                      rect.y() + rect.height() * 0.64f)
                              .lineTo(rect.x() + rect.width() * 0.28f,
                                      rect.y() + rect.height() * 0.82f)
                              .close()
                              .detach();
      canvas->DrawPath(path, stroke_flags);
      break;
    }
    case CommandSuggestionType::kHistory:
    case CommandSuggestionType::kRecentSearch: {
      canvas->DrawCircle(rect.CenterPoint(), rect.width() * 0.34f, stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.CenterPoint().x(), rect.y() + rect.height() * 0.32f),
                       gfx::PointF(rect.CenterPoint().x(), rect.CenterPoint().y()),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.CenterPoint().x(), rect.CenterPoint().y()),
                       gfx::PointF(rect.x() + rect.width() * 0.68f,
                                   rect.y() + rect.height() * 0.58f),
                       stroke_flags);
      break;
    }
    case CommandSuggestionType::kAction: {
      const SkPath path = SkPathBuilder()
                              .moveTo(rect.x() + rect.width() * 0.56f,
                                      rect.y() + rect.height() * 0.12f)
                              .lineTo(rect.x() + rect.width() * 0.34f,
                                      rect.y() + rect.height() * 0.54f)
                              .lineTo(rect.x() + rect.width() * 0.50f,
                                      rect.y() + rect.height() * 0.54f)
                              .lineTo(rect.x() + rect.width() * 0.42f,
                                      rect.y() + rect.height() * 0.88f)
                              .lineTo(rect.x() + rect.width() * 0.68f,
                                      rect.y() + rect.height() * 0.42f)
                              .lineTo(rect.x() + rect.width() * 0.52f,
                                      rect.y() + rect.height() * 0.42f)
                              .close()
                              .detach();
      canvas->DrawPath(path, fill_flags);
      break;
    }
    case CommandSuggestionType::kNavigation: {
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.20f, rect.CenterPoint().y()),
                       gfx::PointF(rect.x() + rect.width() * 0.76f, rect.CenterPoint().y()),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.56f,
                                   rect.y() + rect.height() * 0.26f),
                       gfx::PointF(rect.x() + rect.width() * 0.78f, rect.CenterPoint().y()),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.56f,
                                   rect.y() + rect.height() * 0.74f),
                       gfx::PointF(rect.x() + rect.width() * 0.78f, rect.CenterPoint().y()),
                       stroke_flags);
      break;
    }
    case CommandSuggestionType::kSearch: {
      // Magnifying glass: circle + handle.
      const float r = rect.width() * 0.28f;
      const auto center = gfx::PointF(
          rect.x() + rect.width() * 0.42f,
          rect.y() + rect.height() * 0.42f);
      canvas->DrawCircle(center, r, stroke_flags);
      canvas->DrawLine(
          gfx::PointF(center.x() + r * 0.7f, center.y() + r * 0.7f),
          gfx::PointF(rect.x() + rect.width() * 0.78f,
                      rect.y() + rect.height() * 0.78f),
          stroke_flags);
      break;
    }
    case CommandSuggestionType::kCalculator: {
      canvas->DrawRoundRect(rect, rect.width() * 0.16f, fill_flags);
      cc::PaintFlags cutout_flags;
      cutout_flags.setAntiAlias(true);
      cutout_flags.setStyle(cc::PaintFlags::kStroke_Style);
      cutout_flags.setStrokeWidth(1.5f);
      cutout_flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
      cutout_flags.setColor(SK_ColorWHITE);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.33f,
                                   rect.y() + rect.height() * 0.37f),
                       gfx::PointF(rect.x() + rect.width() * 0.67f,
                                   rect.y() + rect.height() * 0.37f),
                       cutout_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.38f,
                                   rect.y() + rect.height() * 0.60f),
                       gfx::PointF(rect.x() + rect.width() * 0.62f,
                                   rect.y() + rect.height() * 0.60f),
                       cutout_flags);
      canvas->DrawLine(gfx::PointF(rect.CenterPoint().x(), rect.y() + rect.height() * 0.48f),
                       gfx::PointF(rect.CenterPoint().x(), rect.y() + rect.height() * 0.72f),
                       cutout_flags);
      break;
    }
    case CommandSuggestionType::kUnitConversion: {
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.18f,
                                   rect.y() + rect.height() * 0.38f),
                       gfx::PointF(rect.x() + rect.width() * 0.76f,
                                   rect.y() + rect.height() * 0.38f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.58f,
                                   rect.y() + rect.height() * 0.20f),
                       gfx::PointF(rect.x() + rect.width() * 0.78f,
                                   rect.y() + rect.height() * 0.38f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.58f,
                                   rect.y() + rect.height() * 0.56f),
                       gfx::PointF(rect.x() + rect.width() * 0.78f,
                                   rect.y() + rect.height() * 0.38f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.82f,
                                   rect.y() + rect.height() * 0.68f),
                       gfx::PointF(rect.x() + rect.width() * 0.24f,
                                   rect.y() + rect.height() * 0.68f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.42f,
                                   rect.y() + rect.height() * 0.50f),
                       gfx::PointF(rect.x() + rect.width() * 0.22f,
                                   rect.y() + rect.height() * 0.68f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.42f,
                                   rect.y() + rect.height() * 0.86f),
                       gfx::PointF(rect.x() + rect.width() * 0.22f,
                                   rect.y() + rect.height() * 0.68f),
                       stroke_flags);
      break;
    }
    case CommandSuggestionType::kArchivedTab: {
      const gfx::RectF box_rect(rect.x() + rect.width() * 0.20f,
                                rect.y() + rect.height() * 0.28f,
                                rect.width() * 0.60f,
                                rect.height() * 0.48f);
      canvas->DrawRoundRect(box_rect, rect.width() * 0.10f, stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.34f,
                                   rect.y() + rect.height() * 0.28f),
                       gfx::PointF(rect.x() + rect.width() * 0.34f,
                                   rect.y() + rect.height() * 0.18f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.66f,
                                   rect.y() + rect.height() * 0.28f),
                       gfx::PointF(rect.x() + rect.width() * 0.66f,
                                   rect.y() + rect.height() * 0.18f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.38f,
                                   rect.CenterPoint().y()),
                       gfx::PointF(rect.x() + rect.width() * 0.62f,
                                   rect.CenterPoint().y()),
                       stroke_flags);
      break;
    }
    case CommandSuggestionType::kClosedTab: {
      const SkPath path = SkPathBuilder()
                              .moveTo(rect.x() + rect.width() * 0.24f,
                                      rect.y() + rect.height() * 0.28f)
                              .lineTo(rect.x() + rect.width() * 0.76f,
                                      rect.y() + rect.height() * 0.28f)
                              .lineTo(rect.x() + rect.width() * 0.76f,
                                      rect.y() + rect.height() * 0.72f)
                              .lineTo(rect.x() + rect.width() * 0.24f,
                                      rect.y() + rect.height() * 0.72f)
                              .close()
                              .detach();
      canvas->DrawPath(path, stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.36f,
                                   rect.y() + rect.height() * 0.40f),
                       gfx::PointF(rect.x() + rect.width() * 0.64f,
                                   rect.y() + rect.height() * 0.60f),
                       stroke_flags);
      canvas->DrawLine(gfx::PointF(rect.x() + rect.width() * 0.64f,
                                   rect.y() + rect.height() * 0.40f),
                       gfx::PointF(rect.x() + rect.width() * 0.36f,
                                   rect.y() + rect.height() * 0.60f),
                       stroke_flags);
      break;
    }
    case CommandSuggestionType::kFolder: {
      const SkPath path = SkPathBuilder()
                              .moveTo(rect.x() + rect.width() * 0.18f,
                                      rect.y() + rect.height() * 0.34f)
                              .lineTo(rect.x() + rect.width() * 0.38f,
                                      rect.y() + rect.height() * 0.34f)
                              .lineTo(rect.x() + rect.width() * 0.46f,
                                      rect.y() + rect.height() * 0.24f)
                              .lineTo(rect.x() + rect.width() * 0.80f,
                                      rect.y() + rect.height() * 0.24f)
                              .lineTo(rect.x() + rect.width() * 0.80f,
                                      rect.y() + rect.height() * 0.76f)
                              .lineTo(rect.x() + rect.width() * 0.18f,
                                      rect.y() + rect.height() * 0.76f)
                              .close()
                              .detach();
      canvas->DrawPath(path, stroke_flags);
      break;
    }
    case CommandSuggestionType::kTab: {
      const float pad = rect.width() * 0.16f;
      gfx::RectF window_rect = rect;
      window_rect.Inset(gfx::InsetsF::TLBR(pad, pad, pad, pad));
      const float radius = window_rect.width() * 0.14f;
      canvas->DrawRoundRect(window_rect, radius, stroke_flags);
      const float bar_y = window_rect.y() + window_rect.height() * 0.28f;
      canvas->DrawLine(
          gfx::PointF(window_rect.x() + window_rect.width() * 0.18f, bar_y),
          gfx::PointF(window_rect.x() + window_rect.width() * 0.82f, bar_y),
          stroke_flags);
      break;
    }
  }
}

class CommandIconView : public views::View {
 public:
  METADATA_HEADER(CommandIconView, views::View)

 public:
  explicit CommandIconView(CommandSuggestionType type) : type_(type) {
    SetPreferredSize(gfx::Size(kGlyphIconSizeDp, kGlyphIconSizeDp));
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    const gfx::Rect bounds = GetContentsBounds();
    if (bounds.IsEmpty()) {
      return;
    }
    DrawLineIcon(canvas,
                 gfx::RectF(bounds.x(), bounds.y(), bounds.width(), bounds.height()),
                 type_, GlyphForegroundColor(type_));
  }

 private:
  const CommandSuggestionType type_;
  bool selected_ = false;
};

BEGIN_METADATA(CommandIconView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoCommandResultRowView)
END_METADATA

MahoCommandResultRowView::MahoCommandResultRowView(
    const CommandSuggestion& suggestion,
    int row_index,
    int set_size,
    HoverCallback hover_callback,
    ActivateCallback activate_callback,
    PresentationStyle presentation_style)
    : suggestion_type_(suggestion.type),
      is_tab_type_(suggestion.type == CommandSuggestionType::kTab),
      presentation_style_(presentation_style),
      title_text_(base::UTF8ToUTF16(suggestion.title)),
      subtitle_text_(base::UTF8ToUTF16(suggestion.subtitle)),
      compact_metadata_text_(presentation_style == PresentationStyle::kCurrentTabSeeded
                                 ? SeededCompactMetadataText(subtitle_text_)
                                 : HostCompactMetadataText(subtitle_text_)),
      icon_(suggestion.icon),
      row_index_(row_index),
      set_size_(set_size),
      hover_callback_(std::move(hover_callback)),
      activate_callback_(std::move(activate_callback)),
      match_ranges_(suggestion.match_ranges) {
  SetPreferredSize(gfx::Size(0, presentation_style_ == PresentationStyle::kCurrentTabSeeded
                                    ? kCurrentTabSeededRowHeightDp
                                    : kDefaultRowHeightDp));

  GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
  {
    std::u16string a11y_name = title_text_;
    if (!compact_metadata_text_.empty()) {
      a11y_name += u", " + compact_metadata_text_;
    }
    GetViewAccessibility().SetName(a11y_name);
  }
  GetViewAccessibility().SetPosInSet(row_index_ + 1);
  GetViewAccessibility().SetSetSize(set_size_);
  GetViewAccessibility().SetIsSelected(selected_);

  auto* outer_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  outer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  outer_layout->set_inside_border_insets(
      SelectionInsetsForPresentation(presentation_style_));

  content_view_ = AddChildView(std::make_unique<views::View>());
  auto* row_layout = content_view_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, presentation_style_ == PresentationStyle::kCurrentTabSeeded
                                 ? kCurrentTabSeededRowPaddingHDp
                                 : kDefaultRowPaddingHDp),
          presentation_style_ == PresentationStyle::kCurrentTabSeeded
              ? kCurrentTabSeededRowItemSpacingDp
              : kDefaultRowItemSpacingDp));
  row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  glyph_container_ = content_view_->AddChildView(std::make_unique<views::View>());
  glyph_container_->SetPreferredSize(
      gfx::Size(presentation_style_ == PresentationStyle::kCurrentTabSeeded
                    ? kCurrentTabSeededGlyphTileSizeDp
                    : kGlyphTileSizeDp,
                presentation_style_ == PresentationStyle::kCurrentTabSeeded
                    ? kCurrentTabSeededGlyphTileSizeDp
                    : kGlyphTileSizeDp));
  RebuildGlyph();

  text_container_ = content_view_->AddChildView(std::make_unique<views::View>());
  auto* text_container_layout = text_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kShortcutSpacingDp));
  text_container_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  row_layout->SetFlexForView(text_container_, 1);

  text_stack_ = text_container_->AddChildView(std::make_unique<views::View>());
  auto* text_layout = text_stack_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kCompactTextSpacingDp));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  text_container_layout->SetFlexForView(text_stack_, 1);

  const bool compact =
      presentation_style_ == PresentationStyle::kCurrentTabSeeded;

  title_label_ = text_stack_->AddChildView(std::make_unique<views::Label>());
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetTextStyle(compact ? views::style::STYLE_BODY_5
                                     : views::style::STYLE_BODY_4);
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetMultiLine(false);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetSkipSubpixelRenderingOpacityCheck(true);
  text_layout->SetFlexForView(title_label_, compact ? 0 : 1);

  compact_separator_label_ = text_stack_->AddChildView(
      std::make_unique<views::Label>(u"—", views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_5));
  compact_separator_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  compact_separator_label_->SetAutoColorReadabilityEnabled(false);
  compact_separator_label_->SetMultiLine(false);
  compact_separator_label_->SetSkipSubpixelRenderingOpacityCheck(true);
  compact_separator_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(
          presentation_style_ == PresentationStyle::kCurrentTabSeeded ? 0
                                                                       : kSelectedSubtitleTopInsetDp,
          0, 0, 0)));

  compact_metadata_label_ =
      text_stack_->AddChildView(std::make_unique<views::Label>());
  compact_metadata_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  compact_metadata_label_->SetAutoColorReadabilityEnabled(false);
  compact_metadata_label_->SetMultiLine(false);
  compact_metadata_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  compact_metadata_label_->SetSkipSubpixelRenderingOpacityCheck(true);
  compact_metadata_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(
          compact ? 0 : kSelectedSubtitleTopInsetDp,
          0, 0, 0)));
  text_layout->SetFlexForView(compact_metadata_label_, compact ? 1 : 0);

  if (!suggestion.shortcut_label.empty()) {
    shortcut_label_ = text_container_->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(suggestion.shortcut_label),
        views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
    shortcut_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    shortcut_label_->SetAutoColorReadabilityEnabled(false);
    shortcut_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    shortcut_label_->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, kShortcutCornerRadiusDp,
                                       kMahoColorCommandBarKeycapBorder),
        gfx::Insets::VH(kShortcutPaddingVDp, kShortcutPaddingHDp)));
  }

  if (is_tab_type_) {
    switch_tab_container_ = content_view_->AddChildView(std::make_unique<views::View>());
    auto* tab_affordance_layout = switch_tab_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                           gfx::Insets::TLBR(0, 8, 0, 0), 6));
    tab_affordance_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    switch_tab_label_ = switch_tab_container_->AddChildView(
        std::make_unique<views::Label>(u"Switch to Tab",
                                       views::style::CONTEXT_LABEL,
                                       views::style::STYLE_BODY_5));
    switch_tab_label_->SetAutoColorReadabilityEnabled(false);

    switch_tab_chip_ = switch_tab_container_->AddChildView(std::make_unique<views::View>());
    switch_tab_chip_->SetPreferredSize(gfx::Size(24, 24));
    auto* chip_layout = switch_tab_chip_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal));
    chip_layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
    chip_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

    switch_tab_icon_ = switch_tab_chip_->AddChildView(std::make_unique<views::ImageView>());
    switch_tab_icon_->SetCanProcessEventsWithinSubtree(false);
  }

  UpdateLayout();
}

MahoCommandResultRowView::~MahoCommandResultRowView() = default;

void MahoCommandResultRowView::SetIcon(std::optional<ImageData> icon) {
  icon_ = std::move(icon);
  RebuildGlyph();
  UpdateLayout();
  UpdateColors();
}

// static
gfx::Insets MahoCommandResultRowView::GetSelectionInsetsForTesting(
    PresentationStyle style) {
  return SelectionInsetsForPresentation(style);
}

// static
int MahoCommandResultRowView::GetSelectionCornerRadiusForTesting(
    PresentationStyle style) {
  return SelectionCornerRadiusForPresentation(style);
}

// static
gfx::Font::Weight
MahoCommandResultRowView::GetSeededRowTitleFontWeightForTesting() {
  return kSeededRowTitleFontWeight;
}

void MahoCommandResultRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void MahoCommandResultRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  if (hover_callback_) {
    hover_callback_.Run(row_index_);
  }
}

bool MahoCommandResultRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton()) {
    if (hover_callback_) {
      hover_callback_.Run(row_index_);
    }
    return true;
  }
  return views::View::OnMousePressed(event);
}

void MahoCommandResultRowView::OnMouseReleased(const ui::MouseEvent& event) {
  views::View::OnMouseReleased(event);
  if (event.IsOnlyLeftMouseButton() && HitTestPoint(event.location()) &&
      activate_callback_) {
    activate_callback_.Run(row_index_);
  }
}

void MahoCommandResultRowView::ApplyTitleStyles(SkColor title_color) {
  title_label_->SetText(title_text_);
  title_label_->SetEnabledColor(title_color);
  const bool compact =
      presentation_style_ == PresentationStyle::kCurrentTabSeeded;
  title_label_->SetTextStyle(selected_
                                   ? (compact ? views::style::STYLE_BODY_5_BOLD
                                              : views::style::STYLE_BODY_4_BOLD)
                                   : (compact ? views::style::STYLE_BODY_5_MEDIUM
                                              : views::style::STYLE_BODY_4_MEDIUM));
  if (compact) {
    title_label_->SetFontList(title_label_->font_list().Derive(
        0, gfx::Font::NORMAL, kSeededRowTitleFontWeight));
  }

  for (const auto& [start, end] : match_ranges_) {
    const size_t clamped_start =
        std::clamp(static_cast<size_t>(std::max(start, 0)), size_t{0},
                   title_text_.size());
    const size_t clamped_end =
        std::clamp(static_cast<size_t>(std::max(end, 0)), clamped_start,
                   title_text_.size());
    if (clamped_start == clamped_end) {
      continue;
    }
    title_label_->SetTextStyleRange(
        selected_ ? (compact ? views::style::STYLE_BODY_5_BOLD
                             : views::style::STYLE_BODY_4_BOLD)
                  : (compact ? views::style::STYLE_BODY_5_BOLD
                              : views::style::STYLE_BODY_4_EMPHASIS),
        gfx::Range(clamped_start, clamped_end));
  }
}

void MahoCommandResultRowView::SetSelected(bool selected) {
  if (selected_ == selected)
    return;
  selected_ = selected;
  GetViewAccessibility().SetIsSelected(selected_);
  UpdateLayout();
  UpdateColors();
  GetViewAccessibility().NotifyEvent(
      selected_ ? ax::mojom::Event::kSelectionAdd
                : ax::mojom::Event::kSelectionRemove,
      true);
}

void MahoCommandResultRowView::RebuildGlyph() {
  glyph_container_->RemoveAllChildViews();
  glyph_image_ = nullptr;
  favicon_image_ = nullptr;

  const gfx::Image favicon_image =
      icon_.has_value() ? DecodeFaviconImage(*icon_) : gfx::Image();
  if (!favicon_image.IsEmpty()) {
    uses_fallback_glyph_ = false;
    auto* glyph_layout = glyph_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal));
    glyph_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    glyph_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    favicon_image_ =
        glyph_container_->AddChildView(std::make_unique<views::ImageView>());
    favicon_image_->SetImage(
        ui::ImageModel::FromImageSkia(favicon_image.AsImageSkia()));
    const int favicon_size = PreferredFaviconSize(
        icon_, presentation_style_ == PresentationStyle::kCurrentTabSeeded
                   ? kCurrentTabSeededGlyphIconSizeDp
                   : kGlyphIconSizeDp);
    favicon_image_->SetImageSize(gfx::Size(favicon_size, favicon_size));
    favicon_image_->SetPreferredSize(gfx::Size(favicon_size, favicon_size));
    favicon_image_->SetCanProcessEventsWithinSubtree(false);
    glyph_image_ = favicon_image_;
    return;
  }

  uses_fallback_glyph_ = true;
  glyph_container_->SetLayoutManager(std::make_unique<views::FillLayout>());
  glyph_image_ = glyph_container_->AddChildView(
      std::make_unique<CommandIconView>(suggestion_type_));
  glyph_image_->SetCanProcessEventsWithinSubtree(false);
  static_cast<CommandIconView*>(glyph_image_)->SetSelected(selected_);
}

void MahoCommandResultRowView::UpdateLayout() {
  const bool compact = presentation_style_ == PresentationStyle::kCurrentTabSeeded;
  const int row_height = compact ? kCurrentTabSeededRowHeightDp : kDefaultRowHeightDp;
  const int glyph_tile_size = compact ? kCurrentTabSeededGlyphTileSizeDp : kGlyphTileSizeDp;
  const int glyph_icon_size = compact ? kCurrentTabSeededGlyphIconSizeDp : kGlyphIconSizeDp;
  const bool show_selected_seeded_favicon_tile =
      compact && selected_ && favicon_image_ != nullptr;

  SetPreferredSize(gfx::Size(0, row_height));
  glyph_container_->SetPreferredSize(gfx::Size(glyph_tile_size, glyph_tile_size));
  if (favicon_image_) {
    const int favicon_size = PreferredFaviconSize(
        icon_, show_selected_seeded_favicon_tile
                   ? kCurrentTabSeededSelectedFaviconImageSizeDp
                   : glyph_icon_size);
    favicon_image_->SetImageSize(gfx::Size(favicon_size, favicon_size));
    const int favicon_tile_size = show_selected_seeded_favicon_tile
                                      ? kCurrentTabSeededSelectedFaviconTileSizeDp
                                      : favicon_size;
    favicon_image_->SetPreferredSize(
        gfx::Size(favicon_tile_size, favicon_tile_size));
  } else {
    glyph_image_->SetPreferredSize(gfx::Size(glyph_icon_size, glyph_icon_size));
  }

  auto* outer_layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  outer_layout->set_inside_border_insets(
      SelectionInsetsForPresentation(presentation_style_));

  title_label_->SetText(title_text_);
  if (compact_metadata_label_) {
    compact_metadata_label_->SetText(compact_metadata_text_);
    const bool show_metadata = !compact_metadata_text_.empty();
    compact_metadata_label_->SetVisible(show_metadata);
    if (compact_separator_label_) {
      compact_separator_label_->SetVisible(show_metadata);
    }
  }
  if (shortcut_label_) {
    shortcut_label_->SetVisible(!shortcut_label_->GetText().empty());
  }

  PreferredSizeChanged();
}

void MahoCommandResultRowView::UpdateColors() {
  const auto* cp = GetColorProvider();
  if (!cp)
    return;

  const bool compact = presentation_style_ == PresentationStyle::kCurrentTabSeeded;
  const bool show_selected_seeded_favicon_tile =
      compact && selected_ && favicon_image_ != nullptr;
  const int glyph_corner_radius =
      compact ? kCurrentTabSeededGlyphCornerRadiusDp : kGlyphCornerRadiusDp;
  const SkColor selected_fill = cp->GetColor(kMahoColorCommandBarSelectionFill);
  const SkColor selected_title_color =
      cp->GetColor(kMahoColorCommandBarRowTitleSelected);
  const SkColor selected_subtitle_color =
      cp->GetColor(kMahoColorCommandBarRowSubtitleSelected);

  if (selected_) {
    content_view_->SetBackground(views::CreateRoundedRectBackground(
        selected_fill, SelectionCornerRadiusForPresentation(presentation_style_)));
    content_view_->SetBorder(nullptr);
  } else {
    content_view_->SetBackground(nullptr);
    content_view_->SetBorder(nullptr);
  }

  if (uses_fallback_glyph_) {
    glyph_container_->SetBackground(views::CreateRoundedRectBackground(
        selected_ ? SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF)
                  : cp->GetColor(kMahoColorCommandBarShortcutBadge),
        glyph_corner_radius));
    glyph_container_->SetBorder(views::CreateRoundedRectBorder(
        1, glyph_corner_radius,
        selected_ ? SkColorSetARGB(0x10, 0xFF, 0xFF, 0xFF)
                  : cp->GetColor(kMahoColorCommandBarKeycapBorder)));
  } else if (show_selected_seeded_favicon_tile) {
    glyph_container_->SetBackground(nullptr);
    glyph_container_->SetBorder(nullptr);
    favicon_image_->SetBackground(views::CreateRoundedRectBackground(
        selected_title_color, kCurrentTabSeededFaviconTileCornerRadiusDp));
    favicon_image_->SetBorder(nullptr);
  } else {
    glyph_container_->SetBackground(nullptr);
    glyph_container_->SetBorder(nullptr);
    favicon_image_->SetBackground(nullptr);
    favicon_image_->SetBorder(nullptr);
  }

  if (uses_fallback_glyph_) {
    auto* command_icon = static_cast<CommandIconView*>(glyph_image_);
    command_icon->SetSelected(selected_);
  }

  const SkColor title_color =
      selected_ ? selected_title_color : cp->GetColor(kMahoColorPrimaryText);
  const SkColor subtitle_color =
      selected_ ? selected_subtitle_color
                : cp->GetColor(kMahoColorTertiaryText);
  ApplyTitleStyles(title_color);
  if (compact_separator_label_) {
    compact_separator_label_->SetEnabledColor(subtitle_color);
  }
  if (compact_metadata_label_) {
    compact_metadata_label_->SetEnabledColor(subtitle_color);
    compact_metadata_label_->SetTextStyle(views::style::STYLE_BODY_5);
  }

  if (shortcut_label_) {
    shortcut_label_->SetBackground(views::CreateRoundedRectBackground(
        selected_ ? SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF)
                  : cp->GetColor(kMahoColorCommandBarShortcutBadge),
        kShortcutCornerRadiusDp));
    shortcut_label_->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(
            1, kShortcutCornerRadiusDp,
            selected_ ? SkColorSetARGB(0x10, 0xFF, 0xFF, 0xFF)
                      : cp->GetColor(kMahoColorCommandBarKeycapBorder)),
        gfx::Insets::VH(kShortcutPaddingVDp, kShortcutPaddingHDp)));
    shortcut_label_->SetEnabledColor(
        selected_ ? selected_subtitle_color
                  : cp->GetColor(kMahoColorCommandBarShortcutText));
  }

  if (is_tab_type_ && switch_tab_container_) {
    const bool dark_mode = SkColorGetR(cp->GetColor(kMahoColorCommandBarBackground)) < 0x80;
    const SkColor label_color = selected_
        ? cp->GetColor(kMahoColorCommandBarRowSubtitleSelected)
        : cp->GetColor(kMahoColorSecondaryText);
    switch_tab_label_->SetEnabledColor(label_color);

    const SkColor chip_bg = selected_
        ? (dark_mode ? SK_ColorWHITE : SkColorSetRGB(0xE0, 0xE3, 0xEB))
        : (dark_mode ? SkColorSetARGB(13, 255, 255, 255) : SkColorSetARGB(13, 0, 0, 0));
    switch_tab_chip_->SetBackground(views::CreateRoundedRectBackground(chip_bg, 12));

    const SkColor icon_color = selected_
        ? cp->GetColor(kMahoColorAccentBlue)
        : cp->GetColor(kMahoColorSecondaryText);
    switch_tab_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kChevronRightIcon, icon_color, 14));
  }
}

}  // namespace maho
