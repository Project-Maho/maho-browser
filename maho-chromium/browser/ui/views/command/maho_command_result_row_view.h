// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
class StyledLabel;
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
  static std::u16string BadgeText(CommandSuggestionType type);
  static SkColor BadgeColor(CommandSuggestionType type);

  raw_ptr<views::Label> badge_ = nullptr;
  raw_ptr<views::StyledLabel> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  bool selected_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_RESULT_ROW_VIEW_H_
