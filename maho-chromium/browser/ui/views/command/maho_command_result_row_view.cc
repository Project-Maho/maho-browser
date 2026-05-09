// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_result_row_view.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_sparkle_geometry.h"
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/codec/SkWebpDecoder.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkStream.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/range/range.h"
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

constexpr int kRowHeightDp = 56;
constexpr int kSelectedRowHeightDp = 68;
constexpr int kRowCornerRadiusDp = 11;
constexpr int kRowHorizontalInsetDp = 2;
constexpr int kRowPaddingHDp = 10;
constexpr int kSelectedRowPaddingVDp = 0;
constexpr int kSelectionRailWidthDp = 3;
constexpr int kSelectionRailCornerRadiusDp = 2;
constexpr int kGlyphTileSizeDp = 30;
constexpr int kSelectedGlyphTileSizeDp = 30;
constexpr int kGlyphIconSizeDp = 21;
constexpr int kSelectedGlyphIconSizeDp = 21;
constexpr int kGlyphCornerRadiusDp = 8;
constexpr int kSelectedGlyphCornerRadiusDp = 8;
constexpr int kShortcutCornerRadiusDp = 8;
constexpr int kShortcutPaddingHDp = 6;
constexpr int kShortcutPaddingVDp = 3;
constexpr int kRowItemSpacingDp = 9;
constexpr int kTextSpacingDp = 3;
constexpr int kShortcutSpacingDp = 8;
constexpr int kSelectedSubtitleTopInsetDp = 1;

constexpr SkColor kSelectedSecondaryTextColor =
    SkColorSetARGB(0xA8, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSelectedTitleTextColor =
    SkColorSetARGB(0xF1, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSelectedGlyphBorderColor =
    SkColorSetARGB(0x16, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSelectedFaviconTileFill =
    SkColorSetARGB(0xB8, 0x0B, 0x0F, 0x16);
constexpr SkColor kSelectedFaviconTileBorder =
    SkColorSetARGB(0x32, 0xFF, 0xFF, 0xFF);

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
      const gfx::RectF sparkle_rect = command_palette::MakeSparkleRect(rect);
      const SkPath outer_path = command_palette::BuildSparklePath(
          sparkle_rect, command_palette::kSparkleOuterPoints);
      const SkPath inner_path = command_palette::BuildSparklePath(
          sparkle_rect, command_palette::kSparkleInnerPoints);

      cc::PaintFlags outer_flags;
      outer_flags.setAntiAlias(true);
      outer_flags.setStyle(cc::PaintFlags::kFill_Style);
      outer_flags.setColor(SkColorSetRGB(0x73, 0x7C, 0xFF));
      canvas->DrawPath(outer_path, outer_flags);

      cc::PaintFlags inner_flags;
      inner_flags.setAntiAlias(true);
      inner_flags.setStyle(cc::PaintFlags::kFill_Style);
      inner_flags.setColor(SkColorSetRGB(0xFF, 0xC7, 0x57));
      canvas->DrawPath(inner_path, inner_flags);
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
    DrawLineIcon(
        canvas,
        gfx::RectF(bounds.x(), bounds.y(), bounds.width(), bounds.height()),
        type_, selected_ ? GlyphForegroundColor(type_)
                         : GlyphForegroundColor(type_));
  }

 private:
  const CommandSuggestionType type_;
  bool selected_ = false;
};

}  // namespace

BEGIN_METADATA(CommandIconView)
END_METADATA

BEGIN_METADATA(MahoCommandResultRowView)
END_METADATA

MahoCommandResultRowView::MahoCommandResultRowView(
    const CommandSuggestion& suggestion,
    int row_index,
    HoverCallback hover_callback,
    ActivateCallback activate_callback)
    : title_text_(base::UTF8ToUTF16(suggestion.title)),
      subtitle_text_(base::UTF8ToUTF16(suggestion.subtitle)),
      icon_(suggestion.icon),
      row_index_(row_index),
      hover_callback_(std::move(hover_callback)),
      activate_callback_(std::move(activate_callback)),
      match_ranges_(suggestion.match_ranges) {
  SetPreferredSize(gfx::Size(0, kRowHeightDp));

  auto* outer_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  outer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  outer_layout->set_inside_border_insets(
      gfx::Insets::VH(0, kRowHorizontalInsetDp));

  content_view_ = AddChildView(std::make_unique<views::View>());
  auto* row_layout = content_view_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kRowPaddingHDp), kRowItemSpacingDp));
  row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  selection_indicator_ = content_view_->AddChildView(
      std::make_unique<views::View>());
  selection_indicator_->SetPreferredSize(
      gfx::Size(kSelectionRailWidthDp, kRowHeightDp));
  selection_indicator_->SetCanProcessEventsWithinSubtree(false);

  glyph_container_ = content_view_->AddChildView(std::make_unique<views::View>());
  glyph_container_->SetPreferredSize(
      gfx::Size(kGlyphTileSizeDp, kGlyphTileSizeDp));
  glyph_container_->SetLayoutManager(std::make_unique<views::FillLayout>());

  const gfx::Image favicon_image =
      icon_.has_value() ? DecodeFaviconImage(*icon_) : gfx::Image();
  if (!favicon_image.IsEmpty()) {
    uses_fallback_glyph_ = false;
    favicon_image_ = glyph_container_->AddChildView(
        std::make_unique<views::ImageView>());
    favicon_image_->SetImage(ui::ImageModel::FromImageSkia(
        favicon_image.AsImageSkia()));
    const int favicon_size = PreferredFaviconSize(icon_, kGlyphIconSizeDp);
    favicon_image_->SetImageSize(gfx::Size(favicon_size, favicon_size));
    favicon_image_->SetCanProcessEventsWithinSubtree(false);
    glyph_image_ = favicon_image_;
  } else {
    uses_fallback_glyph_ = true;
    glyph_image_ = glyph_container_->AddChildView(
        std::make_unique<CommandIconView>(suggestion.type));
    glyph_image_->SetCanProcessEventsWithinSubtree(false);
  }

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
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kTextSpacingDp));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  text_container_layout->SetFlexForView(text_stack_, 1);

  title_label_ = text_stack_->AddChildView(std::make_unique<views::Label>());
  title_label_->SetTextStyle(views::style::STYLE_BODY_4);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetMultiLine(false);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  subtitle_label_ = text_stack_->AddChildView(std::make_unique<views::Label>());
  subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  subtitle_label_->SetAutoColorReadabilityEnabled(false);
  subtitle_label_->SetTextStyle(views::style::STYLE_BODY_5);
  subtitle_label_->SetMultiLine(false);
  subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  subtitle_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kSelectedSubtitleTopInsetDp, 0, 0, 0)));

  if (!suggestion.shortcut_label.empty()) {
    shortcut_label_ = text_container_->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(suggestion.shortcut_label),
        views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
    shortcut_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    shortcut_label_->SetAutoColorReadabilityEnabled(false);
    shortcut_label_->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(
            1, kShortcutCornerRadiusDp,
            kMahoColorCommandBarKeycapBorder),
        gfx::Insets::VH(kShortcutPaddingVDp, kShortcutPaddingHDp)));
  }

  UpdateLayout();
}

MahoCommandResultRowView::~MahoCommandResultRowView() = default;

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
  title_label_->SetTextStyle(selected_ ? views::style::STYLE_BODY_4_MEDIUM
                                       : views::style::STYLE_BODY_4);

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
    title_label_->SetTextStyleRange(selected_ ? views::style::STYLE_BODY_4_BOLD
                                              : views::style::STYLE_BODY_4_EMPHASIS,
                                     gfx::Range(clamped_start, clamped_end));
  }
}

void MahoCommandResultRowView::SetSelected(bool selected) {
  if (selected_ == selected)
    return;
  selected_ = selected;
  UpdateLayout();
  UpdateColors();
}

void MahoCommandResultRowView::UpdateLayout() {
  SetPreferredSize(
      gfx::Size(0, selected_ ? kSelectedRowHeightDp : kRowHeightDp));

  const int glyph_tile_size =
      selected_ ? kSelectedGlyphTileSizeDp : kGlyphTileSizeDp;
  const int glyph_icon_size =
      selected_ ? kSelectedGlyphIconSizeDp : kGlyphIconSizeDp;
  glyph_container_->SetPreferredSize(
      gfx::Size(glyph_tile_size, glyph_tile_size));
  if (favicon_image_) {
    const int favicon_size = PreferredFaviconSize(icon_, glyph_icon_size);
    favicon_image_->SetImageSize(gfx::Size(favicon_size, favicon_size));
  } else {
    glyph_image_->SetPreferredSize(gfx::Size(glyph_icon_size, glyph_icon_size));
  }

  auto* outer_layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  outer_layout->set_inside_border_insets(
      gfx::Insets::TLBR(selected_ ? kSelectedRowPaddingVDp : 0,
                        kRowHorizontalInsetDp,
                        selected_ ? kSelectedRowPaddingVDp : 0,
                        kRowHorizontalInsetDp));

  if (selection_indicator_) {
    selection_indicator_->SetPreferredSize(
        gfx::Size(kSelectionRailWidthDp,
                  selected_ ? kSelectedRowHeightDp : kRowHeightDp));
  }

  title_label_->SetText(title_text_);
  subtitle_label_->SetText(subtitle_text_);
  subtitle_label_->SetVisible(!subtitle_text_.empty());

  if (shortcut_label_) {
    shortcut_label_->SetVisible(!shortcut_label_->GetText().empty());
  }

  PreferredSizeChanged();
}

void MahoCommandResultRowView::UpdateColors() {
  const auto* cp = GetColorProvider();
  if (!cp)
    return;

  const int glyph_corner_radius =
      selected_ ? kSelectedGlyphCornerRadiusDp : kGlyphCornerRadiusDp;
  const SkColor selected_fill = cp->GetColor(kMahoColorCommandBarSelectionFill);
  const SkColor selected_border =
      cp->GetColor(kMahoColorCommandBarSelectionBorder);

  if (selected_) {
    if (selection_indicator_) {
      selection_indicator_->SetBackground(views::CreateRoundedRectBackground(
          cp->GetColor(kMahoColorAccentBlueBright),
          kSelectionRailCornerRadiusDp));
    }
    content_view_->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetA(selected_fill, 0xE2), kRowCornerRadiusDp));
    content_view_->SetBorder(views::CreateRoundedRectBorder(
        1, kRowCornerRadiusDp, SkColorSetA(selected_border, 0xB4)));
  } else {
    if (selection_indicator_) {
      selection_indicator_->SetBackground(nullptr);
    }
    content_view_->SetBackground(nullptr);
    content_view_->SetBorder(nullptr);
  }

  if (selected_) {
    if (uses_fallback_glyph_) {
      glyph_container_->SetBackground(views::CreateRoundedRectBackground(
          SkColorSetARGB(0xED, 0xFF, 0xFF, 0xFF), glyph_corner_radius));
      glyph_container_->SetBorder(views::CreateRoundedRectBorder(
          1, glyph_corner_radius, kSelectedGlyphBorderColor));
    } else {
      glyph_container_->SetBackground(views::CreateRoundedRectBackground(
          kSelectedFaviconTileFill, glyph_corner_radius));
      glyph_container_->SetBorder(views::CreateRoundedRectBorder(
          1, glyph_corner_radius, kSelectedFaviconTileBorder));
    }
  } else {
    glyph_container_->SetBackground(nullptr);
    glyph_container_->SetBorder(nullptr);
  }

  if (uses_fallback_glyph_) {
    auto* command_icon = static_cast<CommandIconView*>(glyph_image_);
    command_icon->SetSelected(selected_);
  }

  const SkColor title_color =
      selected_ ? kSelectedTitleTextColor : cp->GetColor(kMahoColorPrimaryText);
  const SkColor subtitle_color =
      selected_ ? kSelectedSecondaryTextColor
                : cp->GetColor(kMahoColorTertiaryText);
  ApplyTitleStyles(title_color);
  subtitle_label_->SetEnabledColor(subtitle_color);

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
    shortcut_label_->SetEnabledColor(selected_
                                         ? kSelectedSecondaryTextColor
                                         : cp->GetColor(
                                               kMahoColorCommandBarShortcutText));
  }
}

// static
SkColor MahoCommandResultRowView::GlyphBackgroundColor(
    CommandSuggestionType type) {
  switch (type) {
    case CommandSuggestionType::kTab:
      return SkColorSetARGB(0x20, 0x79, 0x9A, 0xFF);
    case CommandSuggestionType::kBookmark:
      return SkColorSetARGB(0x20, 0xF0, 0xC6, 0x66);
    case CommandSuggestionType::kHistory:
      return SkColorSetARGB(0x20, 0xA8, 0xB3, 0xC7);
    case CommandSuggestionType::kAction:
      return SkColorSetARGB(0x20, 0xC0, 0x7D, 0xFF);
    case CommandSuggestionType::kNavigation:
      return SkColorSetARGB(0x20, 0x76, 0xDB, 0xC4);
    case CommandSuggestionType::kCalculator:
      return SkColorSetARGB(0x20, 0xFF, 0x9A, 0x74);
    case CommandSuggestionType::kUnitConversion:
      return SkColorSetARGB(0x20, 0xF1, 0x8D, 0x8D);
    case CommandSuggestionType::kArchivedTab:
      return SkColorSetARGB(0x20, 0x91, 0xA5, 0xC1);
    case CommandSuggestionType::kClosedTab:
      return SkColorSetARGB(0x20, 0xB7, 0x97, 0xE4);
    case CommandSuggestionType::kFolder:
      return SkColorSetARGB(0x20, 0x8C, 0xA6, 0xFF);
    case CommandSuggestionType::kRecentSearch:
      return SkColorSetARGB(0x20, 0x9A, 0xA8, 0xD0);
  }
  return SkColorSetARGB(0x20, 0xA8, 0xB3, 0xC7);
}

}  // namespace maho
