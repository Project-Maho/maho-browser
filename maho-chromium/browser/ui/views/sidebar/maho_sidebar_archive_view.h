// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ARCHIVE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ARCHIVE_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class ImageView;
class Label;
class LabelButton;
class MenuRunner;
class ScrollView;
class Textfield;
}  // namespace views

namespace maho {

// Model for a single archived tab parsed from maho-core JSON.
struct ArchivedTabItem {
  ArchivedTabItem();
  ArchivedTabItem(const ArchivedTabItem&);
  ArchivedTabItem(ArchivedTabItem&&);
  ArchivedTabItem& operator=(const ArchivedTabItem&);
  ArchivedTabItem& operator=(ArchivedTabItem&&);
  ~ArchivedTabItem();

  std::string tab_id;
  std::string space_id;
  std::u16string title;
  std::string url;
  std::u16string host;
  base::Time archived_at;
  std::vector<uint8_t> favicon_png_data;
};

// A date-grouped section in the archive list.
struct ArchiveSection {
  ArchiveSection();
  ArchiveSection(const ArchiveSection&);
  ArchiveSection(ArchiveSection&&);
  ArchiveSection& operator=(const ArchiveSection&);
  ArchiveSection& operator=(ArchiveSection&&);
  ~ArchiveSection();

  std::u16string title;
  int sort_key = 0;
  std::vector<ArchivedTabItem> tabs;
};

enum class ArchiveFilterMode { kAll, kToday, kPastWeek, kOlder };

class MahoSidebarArchiveView : public views::View,
                                public views::TextfieldController,
                                public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MahoSidebarArchiveView, views::View)

 public:
  using RestoreCallback = base::RepeatingCallback<void(const std::string& tab_id, const std::string& space_id)>;
  using DeleteCallback = base::RepeatingCallback<void(const std::string& tab_id)>;

  explicit MahoSidebarArchiveView(Browser* browser);
  MahoSidebarArchiveView(const MahoSidebarArchiveView&) = delete;
  MahoSidebarArchiveView& operator=(const MahoSidebarArchiveView&) = delete;
  ~MahoSidebarArchiveView() override;

  // Fetches archived tabs from maho-core for the active space and rebuilds the list.
  void ReloadArchivedTabs();

  // Resets search text and filter state.
  void ResetState();
  void SetArchivedTabsForTesting(std::vector<ArchivedTabItem> tabs);

  void FocusSearchField();

  // Returns true if `point` (in this view's coordinates) is in a safe
  // archive-mode drag region rather than over an interactive control.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  void SetRestoreCallback(RestoreCallback callback);
  void SetDeleteCallback(DeleteCallback callback);
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  views::LabelButton* filter_button_for_testing() { return filter_button_; }
  SkColor filter_button_text_color_for_testing() const {
    return filter_button_text_color_;
  }
  ui::SimpleMenuModel* filter_menu_model_for_testing() {
    return filter_menu_model_.get();
  }
  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  views::View* list_container_for_testing() { return list_container_; }
  views::View* empty_state_view_for_testing() { return empty_state_view_; }
  ArchiveFilterMode filter_mode_for_testing() const { return filter_mode_; }
  std::u16string filter_accessibility_value_for_testing() const;
  int visible_result_count_for_testing() const { return visible_result_count_; }
  views::Textfield* search_field_for_testing() { return search_field_; }
  views::View* search_shell_for_testing() { return search_shell_; }
  const std::vector<ArchivedTabItem>& all_archived_tabs_for_testing() const {
    return all_archived_tabs_;
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;

  void OnThemeChanged() override;

 private:
  void BuildUi();
  void ApplyFilters();
  void RebuildList(const std::vector<ArchiveSection>& sections,
                   int visible_count);
  std::vector<ArchiveSection> GroupIntoSections(
      const std::vector<ArchivedTabItem>& items) const;
  static std::pair<std::u16string, int> SectionMetadata(int day_offset);
  static int DayOffset(base::Time archived_at);

  void OnRestorePressed(const std::string& tab_id, const std::string& space_id);
  void OnDeletePressed(const std::string& tab_id);
  void OnFilterPressed();
  void UpdateFilterButtonAppearance();

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> search_shell_ = nullptr;
  raw_ptr<views::ImageView> search_icon_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> list_container_ = nullptr;
  raw_ptr<views::View> header_surface_ = nullptr;
  raw_ptr<views::View> body_container_ = nullptr;
  raw_ptr<views::LabelButton> filter_button_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  MahoSidebarPalette palette_;
  SkColor filter_button_text_color_ = SK_ColorTRANSPARENT;

  ArchiveFilterMode filter_mode_ = ArchiveFilterMode::kAll;
  std::unique_ptr<ui::SimpleMenuModel> filter_menu_model_;
  std::unique_ptr<views::MenuRunner> filter_menu_runner_;

  std::vector<ArchivedTabItem> all_archived_tabs_;
  // When true, ReloadArchivedTabs() is suppressed so test-injected content
  // (via SetArchivedTabsForTesting) is not overwritten by async RefreshAll
  // callbacks that call ReloadArchivedTabs() with empty production FFI data.
  bool test_injection_active_ = false;
  int visible_result_count_ = 0;
  RestoreCallback restore_callback_;
  DeleteCallback delete_callback_;
  base::OneShotTimer search_debounce_timer_;

  void OnArchivedTabsLoaded(std::string json);

  base::WeakPtrFactory<MahoSidebarArchiveView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ARCHIVE_VIEW_H_
