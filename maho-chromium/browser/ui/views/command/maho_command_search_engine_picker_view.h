#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_SEARCH_ENGINE_PICKER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_SEARCH_ENGINE_PICKER_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/favicon_base/favicon_types.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace views {
class Widget;
}  // namespace views

namespace maho {

class MahoCommandModel;

class MahoCommandSearchEnginePickerView : public views::View {
  METADATA_HEADER(MahoCommandSearchEnginePickerView, views::View)

 public:
  using SelectedCallback = base::RepeatingCallback<void(const std::string&)>;

  MahoCommandSearchEnginePickerView(SelectedCallback selected_callback,
                                    raw_ptr<MahoCommandModel> model);
  MahoCommandSearchEnginePickerView(const MahoCommandSearchEnginePickerView&) = delete;
  MahoCommandSearchEnginePickerView& operator=(const MahoCommandSearchEnginePickerView&) = delete;
  ~MahoCommandSearchEnginePickerView() override;

  static views::Widget* Show(views::View* anchor_view,
                             SelectedCallback selected_callback,
                             raw_ptr<MahoCommandModel> model);

  bool OnKeyPressed(const ui::KeyEvent& event) override;

 private:
  class SearchEngineRow;

  struct SearchEngineItem {
    SearchEngineItem();
    SearchEngineItem(const SearchEngineItem&);
    SearchEngineItem(SearchEngineItem&&);
    SearchEngineItem& operator=(const SearchEngineItem&);
    SearchEngineItem& operator=(SearchEngineItem&&);
    ~SearchEngineItem();

    std::string id;
    std::string name;
    bool is_default = false;
    GURL homepage_url;
  };

  void OnRowHovered(int index);
  void OnRowClicked(int index);
  void UpdateHighlightState();
  void ActivateHighlightedRow();
  void OnFaviconLoaded(int row_index, const favicon_base::LargeIconImageResult& result);

  SelectedCallback selected_callback_;
  raw_ptr<MahoCommandModel> model_;
  std::vector<SearchEngineItem> engines_;
  std::vector<raw_ptr<SearchEngineRow>> rows_;
  int highlighted_index_ = 0;
  base::CancelableTaskTracker favicon_task_tracker_;

  base::WeakPtrFactory<MahoCommandSearchEnginePickerView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_SEARCH_ENGINE_PICKER_VIEW_H_
