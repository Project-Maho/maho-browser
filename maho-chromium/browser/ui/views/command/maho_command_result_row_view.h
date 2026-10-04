// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace ui {
class MouseEvent;
}  // namespace ui

namespace views {
class ImageView;
class Label;
class View;
}  // namespace views

namespace maho {

struct RowLayoutElements {
  raw_ptr<views::View> content_view = nullptr;
  raw_ptr<views::View> glyph_container = nullptr;
  raw_ptr<views::View> text_container = nullptr;
};

class MahoCommandResultRowView : public views::View {
  METADATA_HEADER(MahoCommandResultRowView, views::View)

 public:
  enum class PresentationStyle {
    kDefault,
    kCurrentTabSeeded,
  };

  static constexpr int kRowHeightDp = 44;
  static constexpr int kGlyphTileSizeDp = 36;
  static constexpr int kGlyphIconSizeDp = 24;

  static RowLayoutElements BuildRowLayout(views::View* parent,
                                          std::unique_ptr<views::View> text_widget,
                                          PresentationStyle presentation_style);
  static gfx::Image DecodeFaviconImage(const ImageData& icon);
  static gfx::ImageSkia GetOrDecodeFavicon(const ImageData& icon);
  static int PreferredFaviconSize(const std::optional<ImageData>& icon,
                                  int glyph_icon_size);

  using HoverCallback = base::RepeatingCallback<void(int)>;
  using ActivateCallback = base::RepeatingCallback<void(int)>;

  MahoCommandResultRowView(const CommandSuggestion& suggestion,
                           int row_index,
                           int set_size,
                           HoverCallback hover_callback,
                           ActivateCallback activate_callback,
                           PresentationStyle presentation_style =
                               PresentationStyle::kDefault);
  MahoCommandResultRowView(const MahoCommandResultRowView&) = delete;
  MahoCommandResultRowView& operator=(const MahoCommandResultRowView&) = delete;
  ~MahoCommandResultRowView() override;

  // views::View:
  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;

  void SetSelected(bool selected);
  void SetIcon(std::optional<ImageData> icon);

  // Presents this row as the palette's "ask" affordance: speech-bubble glyph
  // plus a right-aligned explanation, reusing the Switch-to-Tab lane styling.
  void SetAskAffordance(bool ask_affordance);

  static gfx::Insets GetSelectionInsetsForTesting(PresentationStyle style);
  static int GetSelectionCornerRadiusForTesting(PresentationStyle style);
  static gfx::Font::Weight GetSeededRowTitleFontWeightForTesting();
  bool UsesAskGlyphForTesting() const { return ask_affordance_; }
  views::Label* GetAskExplanationLabelForTesting() const {
    return ask_explanation_label_;
  }
  views::View* GetSwitchTabContainerForTesting() const {
    return switch_tab_container_;
  }
  views::Label* GetShortcutLabelForTesting() const { return shortcut_label_; }
  std::u16string GetCompactMetadataTextForTesting() const {
    return compact_metadata_text_;
  }
  views::Label* GetTitleLabelForTesting() const { return title_label_; }
  views::Label* GetCompactMetadataLabelForTesting() const {
    return compact_metadata_label_;
  }
  views::View* GetContentViewForTesting() const { return content_view_; }
  bool UsesFallbackGlyphForTesting() const { return uses_fallback_glyph_; }
  views::ImageView* GetFaviconImageForTesting() const { return favicon_image_; }

 private:
  void ApplyTitleStyles(SkColor title_color);
  void RebuildGlyph();
  void UpdateColors();
  void UpdateLayout();

  [[maybe_unused]] const CommandSuggestionType suggestion_type_;
  const bool is_tab_type_;
  const PresentationStyle presentation_style_;
  const std::u16string title_text_;
  const std::u16string subtitle_text_;
  const std::u16string compact_metadata_text_;
  std::optional<ImageData> icon_;
  const int row_index_;
  const int set_size_;
  const HoverCallback hover_callback_;
  const ActivateCallback activate_callback_;
  std::vector<std::pair<int, int>> match_ranges_;
  raw_ptr<views::View> content_view_ = nullptr;
  raw_ptr<views::View> glyph_container_ = nullptr;
  raw_ptr<views::View> glyph_image_ = nullptr;
  raw_ptr<views::ImageView> favicon_image_ = nullptr;
  raw_ptr<views::View> text_container_ = nullptr;
  raw_ptr<views::View> text_stack_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> compact_separator_label_ = nullptr;
  raw_ptr<views::Label> compact_metadata_label_ = nullptr;
  raw_ptr<views::Label> shortcut_label_ = nullptr;
  raw_ptr<views::View> switch_tab_container_ = nullptr;
  raw_ptr<views::Label> switch_tab_label_ = nullptr;
  raw_ptr<views::View> switch_tab_chip_ = nullptr;
  raw_ptr<views::ImageView> switch_tab_icon_ = nullptr;
  raw_ptr<views::View> ask_affordance_container_ = nullptr;
  raw_ptr<views::Label> ask_explanation_label_ = nullptr;
  bool uses_fallback_glyph_ = false;
  bool ask_affordance_ = false;
  bool selected_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
