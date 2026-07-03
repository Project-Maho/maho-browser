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

class MahoCommandResultRowView : public views::View {
  METADATA_HEADER(MahoCommandResultRowView, views::View)

 public:
  using HoverCallback = base::RepeatingCallback<void(int)>;
  using ActivateCallback = base::RepeatingCallback<void(int)>;

  enum class PresentationStyle {
    kDefault,
    kCurrentTabSeeded,
  };

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

  static gfx::Insets GetSelectionInsetsForTesting(PresentationStyle style);
  static int GetSelectionCornerRadiusForTesting(PresentationStyle style);
  static gfx::Font::Weight GetSeededRowTitleFontWeightForTesting();
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

  const CommandSuggestionType suggestion_type_;
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
  bool uses_fallback_glyph_ = false;
  bool selected_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
