#include "maho/browser/ui/views/frame/maho_tab_controlled_banner_view.h"

#include <memory>
#include <string>

#include "chrome/test/views/chrome_views_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

class MahoTabControlledBannerViewTest : public ChromeViewsTestBase {
 public:
  MahoTabControlledBannerViewTest() = default;
  ~MahoTabControlledBannerViewTest() override = default;

  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    widget_ = CreateTestWidget();
    banner_ = widget_->SetContentsView(
        std::make_unique<MahoTabControlledBannerView>(base::BindRepeating(
            &MahoTabControlledBannerViewTest::OnStop, base::Unretained(this))));
  }

  void TearDown() override {
    widget_.reset();
    ChromeViewsTestBase::TearDown();
  }

  void OnStop(int64_t tab_id) {
    stopped_tab_id_ = tab_id;
  }

 protected:
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoTabControlledBannerView> banner_ = nullptr;
  int64_t stopped_tab_id_ = 0;
};

TEST_F(MahoTabControlledBannerViewTest, StartsHiddenWhenNotControlling) {
  EXPECT_FALSE(banner_->is_controlling());
  EXPECT_FALSE(banner_->GetVisible());
}

TEST_F(MahoTabControlledBannerViewTest, ShowsWhenControlledTargetSet) {
  banner_->SetControlledTarget(42, u"Maho CLI");
  EXPECT_TRUE(banner_->is_controlling());
  EXPECT_EQ(banner_->active_tab_id(), 42);
  EXPECT_TRUE(banner_->GetVisible());
  EXPECT_EQ(banner_->label_for_testing()->GetText(),
            u"This tab is controlled by Maho CLI");
}

TEST_F(MahoTabControlledBannerViewTest, ClearControlledTargetHidesBanner) {
  banner_->SetControlledTarget(42, u"Maho CLI");
  EXPECT_TRUE(banner_->is_controlling());

  banner_->ClearControlledTarget();
  EXPECT_FALSE(banner_->is_controlling());
  EXPECT_EQ(banner_->active_tab_id(), 0);
}

TEST_F(MahoTabControlledBannerViewTest, StopButtonTriggersCallback) {
  banner_->SetControlledTarget(101, u"Claude Agent");
  EXPECT_EQ(stopped_tab_id_, 0);

  banner_->stop_button_for_testing()->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_SPACE, ui::EF_NONE));
  EXPECT_EQ(stopped_tab_id_, 101);
  EXPECT_FALSE(banner_->is_controlling());
}

}
}
