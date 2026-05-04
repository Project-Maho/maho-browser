// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_

#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
class StyledLabel;
class View;
}  // namespace views

namespace maho {

struct CommandSuggestion;
enum class CommandSuggestionType;

class MahoCommandResultRowView : public views::View {
  METADATA_HEADER(MahoCommandResultRowView, views::View)

 public:
  explicit MahoCommandResultRowView(const CommandSuggestion& suggestion);
  MahoCommandResultRowView(const MahoCommandResultRowView&) = delete;
  MahoCommandResultRowView& operator=(const MahoCommandResultRowView&) = delete;
  ~MahoCommandResultRowView() override;

  void SetSelected(bool selected);

 private:
  static std::u16string TypeText(CommandSuggestionType type);
  static std::u16string GlyphText(CommandSuggestionType type);
  static SkColor GlyphBackgroundColor(CommandSuggestionType type);

  void ApplyTitleStyles(SkColor text_color);
  void UpdateColors();

  const CommandSuggestionType type_;
  std::vector<std::pair<int, int>> match_ranges_;
  raw_ptr<views::View> glyph_container_ = nullptr;
  raw_ptr<views::Label> glyph_label_ = nullptr;
  raw_ptr<views::StyledLabel> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::Label> metadata_label_ = nullptr;
  bool selected_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
