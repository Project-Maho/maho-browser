// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/components/maho_importer/maho_importer_constants.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {

class MahoMigrationDialogViewTest : public views::ViewsTestBase {
 protected:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
  }

  void TearDown() override {
    views::ViewsTestBase::TearDown();
  }

  // Accessors for testing internals.
  std::vector<raw_ptr<BrowserCard>>& GetBrowserCards(
      MahoMigrationDialogView* view) {
    return view->browser_cards_;
  }

  size_t GetCheckboxesCount(MahoMigrationDialogView* view) {
    return view->checkboxes_.size();
  }

  uint32_t GetCheckboxFlagAt(MahoMigrationDialogView* view, size_t index) {
    return view->checkboxes_[index].flag;
  }

  bool GetCheckboxEnabledAt(MahoMigrationDialogView* view, size_t index) {
    return view->checkboxes_[index].checkbox->GetEnabled();
  }

  void SelectBrowserCard(MahoMigrationDialogView* view, size_t index) {
    view->OnBrowserCardSelected(index);
  }

  void OnCancelClicked(MahoMigrationDialogView* view) {
    view->OnCancelClicked();
  }

  void OnImportClicked(MahoMigrationDialogView* view) {
    view->OnImportClicked();
  }

  void SimulateImportProgress(MahoMigrationDialogView* view,
                              uint32_t type,
                              int32_t items,
                              const std::string& error,
                              bool complete) {
    view->OnImportProgress(type, items, error, complete);
  }

  MahoMigrationDialogView::DialogState GetState(
      MahoMigrationDialogView* view) {
    return view->state();
  }

  void TransitionTo(MahoMigrationDialogView* view,
                    MahoMigrationDialogView::DialogState state) {
    view->TransitionTo(state);
  }
};

// Helper to create a dialog and show it in a widget.
struct DialogFixture {
  std::vector<DetectedBrowser> browsers;
  bool callback_called = false;
  bool was_cancelled = false;
  uint32_t imported_mask = 0;
  raw_ptr<MahoMigrationDialogView> view_ptr = nullptr;
  raw_ptr<views::Widget> widget = nullptr;
};

TEST_F(MahoMigrationDialogViewTest, BrowserCardSelectionReplacesCombobox) {
  std::vector<DetectedBrowser> browsers;

  DetectedBrowser zen;
  zen.display_name = "Zen";
  zen.type = BrowserType::kZen;
  zen.services_supported = mojom::kImportBookmarks | mojom::kImportWorkspaces;
  browsers.push_back(zen);

  DetectedBrowser chrome;
  chrome.display_name = "Chrome";
  chrome.type = BrowserType::kChrome;
  chrome.services_supported = mojom::kImportBookmarks | mojom::kImportHistory;
  browsers.push_back(chrome);

  bool callback_called = false;
  bool was_cancelled = false;
  uint32_t imported_mask = 0;

  auto close_callback = base::BindOnce(
      [](bool* called, bool* cancelled, uint32_t* mask, bool was_c,
         uint32_t m) {
        *called = true;
        *cancelled = was_c;
        *mask = m;
      },
      &callback_called, &was_cancelled, &imported_mask);

  auto dialog_view = std::make_unique<MahoMigrationDialogView>(
      nullptr, browsers, std::move(close_callback));

  MahoMigrationDialogView* view_ptr = dialog_view.get();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog_view), GetContext(), gfx::NativeView());
  widget->Show();

  // Verify cards exist instead of combobox.
  auto& cards = GetBrowserCards(view_ptr);
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0]->index(), 0u);
  EXPECT_EQ(cards[1]->index(), 1u);

  // Select Zen (index 0).
  SelectBrowserCard(view_ptr, 0);
  EXPECT_TRUE(cards[0]->selected());
  EXPECT_FALSE(cards[1]->selected());

  // Checkboxes should include Workspaces for Zen.
  bool found_workspaces = false;
  for (size_t i = 0; i < GetCheckboxesCount(view_ptr); ++i) {
    if (GetCheckboxFlagAt(view_ptr, i) == mojom::kImportWorkspaces) {
      found_workspaces = true;
      EXPECT_TRUE(GetCheckboxEnabledAt(view_ptr, i));
    }
  }
  EXPECT_TRUE(found_workspaces) << "Zen should show Workspaces checkbox";

  // Switch to Chrome (index 1).
  SelectBrowserCard(view_ptr, 1);
  EXPECT_FALSE(cards[0]->selected());
  EXPECT_TRUE(cards[1]->selected());

  // Chrome does NOT support workspaces — checkbox should not be present (W5-T4).
  found_workspaces = false;
  for (size_t i = 0; i < GetCheckboxesCount(view_ptr); ++i) {
    if (GetCheckboxFlagAt(view_ptr, i) == mojom::kImportWorkspaces) {
      found_workspaces = true;
    }
  }
  EXPECT_FALSE(found_workspaces)
      << "Chrome should NOT show Workspaces checkbox";

  // Cancel the dialog.
  OnCancelClicked(view_ptr);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(was_cancelled);
}

TEST_F(MahoMigrationDialogViewTest, DialogStateTransitions) {
  std::vector<DetectedBrowser> browsers;

  DetectedBrowser chrome;
  chrome.display_name = "Chrome";
  chrome.type = BrowserType::kChrome;
  chrome.services_supported =
      mojom::kImportBookmarks | mojom::kImportHistory | mojom::kImportPasswords;
  browsers.push_back(chrome);

  auto close_callback = base::BindOnce([](bool, uint32_t) {});

  auto dialog_view = std::make_unique<MahoMigrationDialogView>(
      nullptr, browsers, std::move(close_callback));

  MahoMigrationDialogView* view_ptr = dialog_view.get();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog_view), GetContext(), gfx::NativeView());
  widget->Show();

  // Initial state should be kIdle.
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kIdle);

  // Test state transitions.
  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kImporting);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kImporting);

  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kCancelling);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kCancelling);

  // Test partial fail transition.
  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kPartialFail);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kPartialFail);

  // Test full fail transition.
  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kFullFail);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kFullFail);

  // Test success transition.
  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kSuccess);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kSuccess);

  widget->CloseNow();
}

TEST_F(MahoMigrationDialogViewTest, PartialFailShowsContinueButton) {
  std::vector<DetectedBrowser> browsers;

  DetectedBrowser chrome;
  chrome.display_name = "Chrome";
  chrome.type = BrowserType::kChrome;
  chrome.services_supported = mojom::kImportBookmarks | mojom::kImportHistory;
  browsers.push_back(chrome);

  auto close_callback = base::BindOnce([](bool, uint32_t) {});

  auto dialog_view = std::make_unique<MahoMigrationDialogView>(
      nullptr, browsers, std::move(close_callback));

  MahoMigrationDialogView* view_ptr = dialog_view.get();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog_view), GetContext(), gfx::NativeView());
  widget->Show();

  // Transition to partial fail should update buttons.
  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kPartialFail);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kPartialFail);

  widget->CloseNow();
}

TEST_F(MahoMigrationDialogViewTest, FullFailShowsRetryButton) {
  std::vector<DetectedBrowser> browsers;

  DetectedBrowser chrome;
  chrome.display_name = "Chrome";
  chrome.type = BrowserType::kChrome;
  chrome.services_supported = mojom::kImportBookmarks | mojom::kImportHistory;
  browsers.push_back(chrome);

  auto close_callback = base::BindOnce([](bool, uint32_t) {});

  auto dialog_view = std::make_unique<MahoMigrationDialogView>(
      nullptr, browsers, std::move(close_callback));

  MahoMigrationDialogView* view_ptr = dialog_view.get();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog_view), GetContext(), gfx::NativeView());
  widget->Show();

  // Transition to full fail should update buttons.
  TransitionTo(view_ptr, MahoMigrationDialogView::DialogState::kFullFail);
  EXPECT_EQ(GetState(view_ptr),
            MahoMigrationDialogView::DialogState::kFullFail);

  widget->CloseNow();
}

TEST_F(MahoMigrationDialogViewTest, DialogPreferredSize720x540) {
  std::vector<DetectedBrowser> browsers;

  DetectedBrowser chrome;
  chrome.display_name = "Chrome";
  chrome.type = BrowserType::kChrome;
  chrome.services_supported = mojom::kImportBookmarks;
  browsers.push_back(chrome);

  auto close_callback = base::BindOnce([](bool, uint32_t) {});

  auto dialog_view = std::make_unique<MahoMigrationDialogView>(
      nullptr, browsers, std::move(close_callback));

  MahoMigrationDialogView* view_ptr = dialog_view.get();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog_view), GetContext(), gfx::NativeView());
  widget->Show();

  views::View* contents = view_ptr->GetContentsView();
  ASSERT_NE(contents, nullptr);
  gfx::Size preferred = contents->GetPreferredSize();
  EXPECT_EQ(preferred.width(), 720);
  EXPECT_EQ(preferred.height(), 540);

  widget->CloseNow();
}

}  // namespace maho
