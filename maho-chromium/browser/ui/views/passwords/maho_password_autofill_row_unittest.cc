// Copyright 2026 Maho Browser. All rights reserved.

#include "chrome/browser/ui/views/autofill/popup/popup_row_view.h"

#include <algorithm>
#include <memory>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "chrome/browser/ui/autofill/mock_autofill_popup_controller.h"
#include "chrome/browser/ui/views/autofill/popup/mock_accessibility_selection_delegate.h"
#include "chrome/browser/ui/views/autofill/popup/mock_selection_delegate.h"
#include "chrome/browser/ui/views/autofill/popup/popup_row_factory_utils.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/autofill/core/browser/suggestions/suggestion_type.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_id.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace autofill {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

base::FilePath PasswordScreenshotPath(const char* filename) {
  base::FilePath source_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  const base::FilePath maho_root =
      base::MakeAbsoluteFilePath(source_root.AppendASCII("maho"));
  CHECK(!maho_root.empty());
  return maho_root.DirName()
      .AppendASCII("docs")
      .AppendASCII("screenshots")
      .AppendASCII(filename);
}

void CaptureAutofillRowScreenshot(PopupRowView* row, const char* filename) {
  ASSERT_TRUE(row);
  views::Widget* widget = row->GetWidget();
  ASSERT_TRUE(widget);
  widget->LayoutRootViewIfNecessary();

  const gfx::Size preferred = row->GetPreferredSize();
  const gfx::Size capture_size(std::max(preferred.width(), 360),
                               std::max(preferred.height(), 48));
  row->SetSize(capture_size);
  widget->SetSize(capture_size);
  widget->LayoutRootViewIfNecessary();

  SkBitmap bitmap = views::test::PaintViewToBitmap(widget->GetRootView());
  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);
  ASSERT_TRUE(png_data.has_value());
  ASSERT_TRUE(base::WriteFile(PasswordScreenshotPath(filename), *png_data));
}

class MahoPasswordAutofillRowTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    widget_ =
        CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
    web_contents_ =
        content::WebContentsTester::CreateTestWebContents(&profile_, nullptr);
    ON_CALL(controller_, GetWebContents()).WillByDefault(Return(web_contents_.get()));
  }

  void TearDown() override {
    row_view_ = nullptr;
    widget_.reset();
    web_contents_.reset();
    ChromeViewsTestBase::TearDown();
  }

  void ShowRow(SuggestionType type) {
    controller_.set_suggestions({Suggestion(u"Maho user", type)});
    row_view_ = widget_->SetContentsView(CreatePopupRowView(
        controller_.GetWeakPtr(), a11y_selection_delegate_, selection_delegate_,
        /*line_number=*/0));
    ASSERT_TRUE(row_view_);
    widget_->Show();
  }

 protected:
  PopupRowView& row_view() { return *row_view_; }

 private:
  content::RenderViewHostTestEnabler render_view_host_test_enabler_;
  TestingProfile profile_;
  std::unique_ptr<content::WebContents> web_contents_;
  std::unique_ptr<views::Widget> widget_;
  NiceMock<MockAccessibilitySelectionDelegate> a11y_selection_delegate_;
  NiceMock<MockSelectionDelegate> selection_delegate_;
  NiceMock<MockAutofillPopupController> controller_;
  raw_ptr<PopupRowView> row_view_ = nullptr;
};

TEST_F(MahoPasswordAutofillRowTest, PasswordEntryUsesCredentialCardSurface) {
  ShowRow(SuggestionType::kPasswordEntry);

  ASSERT_TRUE(row_view().GetBackground());
  EXPECT_EQ(row_view().GetBackground()->color(), kMahoColorCardBackground);
  EXPECT_TRUE(row_view().GetBorder());
  EXPECT_EQ(row_view().GetProperty(views::kMarginsKey)->top(), 4);
  EXPECT_EQ(row_view().GetProperty(views::kMarginsKey)->bottom(), 4);
}

TEST_F(MahoPasswordAutofillRowTest, PasswordSelectionHighlightsWholeCard) {
  ShowRow(SuggestionType::kPasswordEntry);

  row_view().SetSelectedCell(PopupRowView::CellType::kContent);

  ASSERT_TRUE(row_view().GetBackground());
  EXPECT_EQ(row_view().GetBackground()->color(),
            ui::kColorDropdownBackgroundSelected);
  EXPECT_TRUE(row_view().GetBorder());
}

TEST_F(MahoPasswordAutofillRowTest, AddressEntryKeepsChromiumSurfaceAndMargins) {
  ShowRow(SuggestionType::kAddressEntry);

  ASSERT_TRUE(row_view().GetBackground());
  EXPECT_EQ(row_view().GetBackground()->color(), ui::kColorDropdownBackground);
  EXPECT_EQ(row_view().GetProperty(views::kMarginsKey)->top(), 0);
  EXPECT_EQ(row_view().GetProperty(views::kMarginsKey)->bottom(), 0);
}

TEST_F(MahoPasswordAutofillRowTest, CaptureCredentialRowScreenshots) {
  ShowRow(SuggestionType::kPasswordEntry);
  CaptureAutofillRowScreenshot(&row_view(),
                               "08_maho_password_autofill_row.png");

  row_view().SetSelectedCell(PopupRowView::CellType::kContent);
  CaptureAutofillRowScreenshot(
      &row_view(), "09_maho_password_autofill_row_selected.png");
}

}  // namespace
}  // namespace autofill