// Copyright 2026 Maho Browser. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_IMPORTER_MAHO_MIGRATION_DIALOG_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_IMPORTER_MAHO_MIGRATION_DIALOG_VIEW_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/browser/importer/maho_import_session_bridge.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class Profile;

namespace views {
class BoxLayoutView;
class Combobox;
class ImageView;
class Label;
class MdTextButton;
class ScrollView;
}  // namespace views

namespace maho {

// A selectable card representing one detected source browser.
class BrowserCard : public views::View {
  METADATA_HEADER(BrowserCard, views::View)

 public:
  using SelectCallback = base::RepeatingCallback<void(size_t index)>;

  BrowserCard(size_t index,
              const DetectedBrowser& browser,
              SelectCallback on_select);
  ~BrowserCard() override;

  BrowserCard(const BrowserCard&) = delete;
  BrowserCard& operator=(const BrowserCard&) = delete;

  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  size_t index() const { return index_; }

  // Returns the selected profile index (0-based) if the browser has multiple
  // profiles. Returns std::nullopt for single-profile browsers.
  std::optional<size_t> GetSelectedProfileIndex() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  size_t index_;
  bool selected_ = false;
  bool hovered_ = false;
  SelectCallback on_select_;

  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Label> version_label_ = nullptr;
  raw_ptr<views::Combobox> profile_combobox_ = nullptr;
};

class MahoMigrationDialogView : public views::DialogDelegate,
                                public ui::SelectFileDialog::Listener {
 public:
  using CloseCallback = base::OnceCallback<void(bool was_cancelled, uint32_t imported_items_bitmask)>;

  // State machine for the import flow.
  enum class DialogState {
    kIdle,         // No import in flight
    kImporting,    // Import running
    kCancelling,   // User clicked Cancel during active import
    kPartialFail,  // Some types succeeded, some failed
    kFullFail,     // All types failed
    kSuccess,      // All types succeeded; auto-close in 1s
  };

  MahoMigrationDialogView(Profile* profile,
                          const std::vector<DetectedBrowser>& browsers,
                          CloseCallback callback);
  ~MahoMigrationDialogView() override;

  MahoMigrationDialogView(const MahoMigrationDialogView&) = delete;
  MahoMigrationDialogView& operator=(const MahoMigrationDialogView&) = delete;

  friend class MahoMigrationDialogViewTest;

  static void Show(views::Widget* parent,
                   Profile* profile,
                   const std::vector<DetectedBrowser>& browsers,
                   CloseCallback callback);

  // views::DialogDelegateView:
  bool ShouldShowCloseButton() const override;
  std::u16string GetWindowTitle() const override;
  void WindowClosing() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

  // Accessors for testing.
  DialogState state() const { return state_; }
  bool import_button_enabled_for_testing() const;
  bool import_button_visible_for_testing() const;
  bool cancel_button_visible_for_testing() const;
  std::optional<size_t> selected_browser_index_for_testing() const {
    return selected_browser_index_;
  }
  std::u16string status_label_text_for_testing() const;
  std::u16string import_button_text_for_testing() const;
  bool was_cancelled_for_testing() const { return was_cancelled_; }
  uint32_t current_import_bitmask_for_testing() const {
    return current_import_bitmask_;
  }
  // Test-only: last passwords-CSV path forwarded to the orchestrator in
  // FileSelected(); empty when the picker was cancelled. Never read in prod.
  const std::string& last_password_csv_path_for_testing() const {
    return last_password_csv_path_for_testing_;
  }
  size_t checkboxes_count_for_testing() const { return checkboxes_.size(); }
  uint32_t checkbox_flag_at_for_testing(size_t index) const {
    return checkboxes_[index].flag;
  }
  bool checkbox_enabled_at_for_testing(size_t index) const {
    return checkboxes_[index].checkbox->GetEnabled();
  }

  // Test seams: direct access to internal event handlers.
  void SelectBrowserForTesting(size_t index) { OnBrowserCardSelected(index); }
  void ClickImportForTesting() { OnImportClicked(); }
  void ClickCancelForTesting() { OnCancelClicked(); }
  void ClickRetryForTesting() { OnRetryClicked(); }
  void ClickContinueForTesting() { OnContinueClicked(); }
  void SimulateImportProgressForTesting(uint32_t type,
                                        int32_t items_imported,
                                        const std::string& error,
                                        bool complete) {
    OnImportProgress(type, items_imported, error, complete);
  }

 private:
  void InitLayout();
  void OnBrowserCardSelected(size_t index);
  void RebuildCheckboxes();
  void OnImportClicked();
  // Starts the import for the given data-type |mask|. Assumes any Safari
  // passwords CSV has already been supplied via the orchestrator.
  void StartImport(uint32_t mask);
  void OnCancelClicked();
  void OnRetryClicked();
  void OnContinueClicked();
  void OnAutoCloseAfterSuccess();
  void OnCancelTimeout();
  void OnImportProgress(uint32_t type,
                        int32_t items_imported,
                        const std::string& error,
                        bool complete);
  void TransitionTo(DialogState new_state);
  void UpdateButtonsForState();

  raw_ptr<Profile> profile_;
  std::vector<DetectedBrowser> browsers_;
  CloseCallback callback_;

  std::optional<size_t> selected_browser_index_;
  uint32_t current_import_bitmask_ = 0;
  DialogState state_ = DialogState::kIdle;
  bool was_cancelled_ = false;

  // Per-type completion tracking during import.
  std::map<uint32_t, bool> type_completed_;   // flag → completed?
  std::map<uint32_t, bool> type_succeeded_;   // flag → succeeded?

  // UI elements
  raw_ptr<views::ScrollView> browser_scroll_view_ = nullptr;
  raw_ptr<views::BoxLayoutView> cards_container_ = nullptr;
  raw_ptr<views::BoxLayoutView> checkboxes_container_ = nullptr;
  raw_ptr<views::BoxLayoutView> progress_container_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;

  raw_ptr<views::MdTextButton> import_button_ = nullptr;
  raw_ptr<views::MdTextButton> cancel_button_ = nullptr;

  std::vector<raw_ptr<BrowserCard>> browser_cards_;

  struct CheckboxRow {
    uint32_t flag;
    raw_ptr<views::Checkbox> checkbox;
  };
  std::vector<CheckboxRow> checkboxes_;

  struct PerTypeProgress {
    raw_ptr<views::ImageView> icon;
    raw_ptr<views::Label> type_label;
    raw_ptr<views::Label> count_label;
  };
  std::map<uint32_t, PerTypeProgress> progress_rows_;

  base::OneShotTimer cancel_timeout_timer_;
  std::unique_ptr<MahoImportSessionBridge> import_session_bridge_;

  // File picker for the Safari-exported passwords CSV. |pending_import_bitmask_|
  // holds the requested data types while the picker is open.
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  uint32_t pending_import_bitmask_ = 0;

  std::string last_password_csv_path_for_testing_;

  base::WeakPtrFactory<MahoMigrationDialogView> weak_factory_{this};
};

}  // namespace maho

#endif  // CHROME_BROWSER_UI_VIEWS_IMPORTER_MAHO_MIGRATION_DIALOG_VIEW_H_
