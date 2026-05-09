// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_

#include <string>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "third_party/skia/include/core/SkColor.h"
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

  MahoCommandResultRowView(const CommandSuggestion& suggestion,
                           int row_index,
                           HoverCallback hover_callback,
                           ActivateCallback activate_callback);
  MahoCommandResultRowView(const MahoCommandResultRowView&) = delete;
  MahoCommandResultRowView& operator=(const MahoCommandResultRowView&) = delete;
  ~MahoCommandResultRowView() override;

  // views::View:
  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;

  void SetSelected(bool selected);

 private:
  static SkColor GlyphBackgroundColor(CommandSuggestionType type);

  void ApplyTitleStyles(SkColor title_color);
  void UpdateColors();
  void UpdateLayout();

  const std::u16string title_text_;
  const std::u16string subtitle_text_;
  const std::optional<ImageData> icon_;
  const int row_index_;
  const HoverCallback hover_callback_;
  const ActivateCallback activate_callback_;
  std::vector<std::pair<int, int>> match_ranges_;
  raw_ptr<views::View> content_view_ = nullptr;
  raw_ptr<views::View> selection_indicator_ = nullptr;
  raw_ptr<views::View> glyph_container_ = nullptr;
  raw_ptr<views::View> glyph_image_ = nullptr;
  raw_ptr<views::ImageView> favicon_image_ = nullptr;
  raw_ptr<views::View> text_container_ = nullptr;
  raw_ptr<views::View> text_stack_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::Label> shortcut_label_ = nullptr;
  bool uses_fallback_glyph_ = false;
  bool selected_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
