// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/components/maho_importer/maho_importer_constants.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {
namespace {

int g_select_file_call_count = 0;

// A fake SelectFileDialog that synchronously reports a fixed path as selected.
class SelectingSelectFileDialog : public ui::SelectFileDialog {
 public:
  SelectingSelectFileDialog(Listener* listener,
                            std::unique_ptr<ui::SelectFilePolicy> policy,
                            const base::FilePath& forced_path)
      : ui::SelectFileDialog(listener, std::move(policy)),
        forced_path_(forced_path) {}

  SelectingSelectFileDialog(const SelectingSelectFileDialog&) = delete;
  SelectingSelectFileDialog& operator=(const SelectingSelectFileDialog&) =
      delete;

 protected:
  ~SelectingSelectFileDialog() override = default;

  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      const GURL* caller) override {
    ++g_select_file_call_count;
    listener_->FileSelected(ui::SelectedFileInfo(forced_path_), file_type_index);
  }
  bool IsRunning(gfx::NativeWindow owning_window) const override {
    return false;
  }
  void ListenerDestroyed() override { listener_ = nullptr; }
  bool HasMultipleFileTypeChoicesImpl() override { return false; }

 private:
  base::FilePath forced_path_;
};

class SelectingSelectFileDialogFactory : public ui::SelectFileDialogFactory {
 public:
  explicit SelectingSelectFileDialogFactory(const base::FilePath& forced_path)
      : forced_path_(forced_path) {}

  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new SelectingSelectFileDialog(listener, std::move(policy),
                                         forced_path_);
  }

 private:
  base::FilePath forced_path_;
};

// A fake SelectFileDialog that synchronously reports a cancelled selection.
class CancellingSelectFileDialog : public ui::SelectFileDialog {
 public:
  CancellingSelectFileDialog(Listener* listener,
                             std::unique_ptr<ui::SelectFilePolicy> policy)
      : ui::SelectFileDialog(listener, std::move(policy)) {}

  CancellingSelectFileDialog(const CancellingSelectFileDialog&) = delete;
  CancellingSelectFileDialog& operator=(const CancellingSelectFileDialog&) =
      delete;

 protected:
  ~CancellingSelectFileDialog() override = default;

  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      const GURL* caller) override {
    ++g_select_file_call_count;
    listener_->FileSelectionCanceled();
  }
  bool IsRunning(gfx::NativeWindow owning_window) const override {
    return false;
  }
  void ListenerDestroyed() override { listener_ = nullptr; }
  bool HasMultipleFileTypeChoicesImpl() override { return false; }
};

class CancellingSelectFileDialogFactory : public ui::SelectFileDialogFactory {
 public:
  CancellingSelectFileDialogFactory() = default;

  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new CancellingSelectFileDialog(listener, std::move(policy));
  }
};

DetectedBrowser MakeSafari(uint32_t services) {
  DetectedBrowser safari;
  safari.type = BrowserType::kSafari;
  safari.display_name = "Safari";
  safari.services_supported = services;
  safari.requires_full_disk_access = true;
  return safari;
}

class MahoMigrationDialogSafariCsvTest : public views::ViewsTestBase {
 protected:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    g_select_file_call_count = 0;
  }

  void TearDown() override {
    ui::SelectFileDialog::SetFactory(nullptr);
    if (widget_) {
      widget_->CloseNow();
      widget_ = nullptr;
    }
    views::ViewsTestBase::TearDown();
  }

  // Builds and shows a dialog for a single Safari source and selects its card.
  MahoMigrationDialogView* ShowSafariDialog(uint32_t services) {
    std::vector<DetectedBrowser> browsers = {MakeSafari(services)};
    auto dialog = std::make_unique<MahoMigrationDialogView>(
        nullptr, browsers, base::BindOnce([](bool, uint32_t) {}));
    MahoMigrationDialogView* view = dialog.get();
    widget_ = views::DialogDelegate::CreateDialogWidget(
        std::move(dialog), GetContext(), gfx::NativeView());
    widget_->Show();
    view->SelectBrowserForTesting(0);
    return view;
  }

  raw_ptr<views::Widget> widget_ = nullptr;
};

// Safari + Passwords → Import opens the CSV picker; selecting a file forwards
// the path to the orchestrator and starts the import.
TEST_F(MahoMigrationDialogSafariCsvTest, FileSelectedForwardsCsvAndStartsImport) {
  const base::FilePath fake_csv(
      FILE_PATH_LITERAL("/tmp/maho_fake_safari_passwords.csv"));
  ui::SelectFileDialog::SetFactory(
      std::make_unique<SelectingSelectFileDialogFactory>(fake_csv));

  MahoMigrationDialogView* view =
      ShowSafariDialog(mojom::kImportBookmarks | mojom::kImportPasswords);

  view->ClickImportForTesting();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(g_select_file_call_count, 1) << "CSV picker must be invoked";
  EXPECT_EQ(view->last_password_csv_path_for_testing(),
            fake_csv.AsUTF8Unsafe());
  EXPECT_NE(view->state(), MahoMigrationDialogView::DialogState::kIdle)
      << "import must have started after the CSV was supplied";
}

// Cancelling the CSV picker strips the Passwords bit but still imports the
// other selected types.
TEST_F(MahoMigrationDialogSafariCsvTest,
       FileSelectionCanceledStripsPasswordsButImportsRest) {
  ui::SelectFileDialog::SetFactory(
      std::make_unique<CancellingSelectFileDialogFactory>());

  MahoMigrationDialogView* view =
      ShowSafariDialog(mojom::kImportBookmarks | mojom::kImportPasswords);

  view->ClickImportForTesting();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(g_select_file_call_count, 1) << "CSV picker must be invoked";
  EXPECT_TRUE(view->last_password_csv_path_for_testing().empty())
      << "no CSV path must be forwarded when the picker is cancelled";
  EXPECT_NE(view->state(), MahoMigrationDialogView::DialogState::kIdle)
      << "remaining (non-password) types must still import";
}

// Cancelling the CSV picker when Passwords was the ONLY selected type leaves
// the dialog idle so the user can retry.
TEST_F(MahoMigrationDialogSafariCsvTest,
       FileSelectionCanceledPasswordsOnlyStaysIdle) {
  ui::SelectFileDialog::SetFactory(
      std::make_unique<CancellingSelectFileDialogFactory>());

  MahoMigrationDialogView* view = ShowSafariDialog(mojom::kImportPasswords);

  view->ClickImportForTesting();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(g_select_file_call_count, 1) << "CSV picker must be invoked";
  EXPECT_TRUE(view->last_password_csv_path_for_testing().empty());
  EXPECT_EQ(view->state(), MahoMigrationDialogView::DialogState::kIdle);
  EXPECT_TRUE(view->import_button_enabled_for_testing());
}

}  // namespace
}  // namespace maho
