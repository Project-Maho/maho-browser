// Copyright 2026 Maho Browser. All rights reserved.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "chrome/test/base/browser_with_test_window_test.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

constexpr size_t kLeadingClusterIndex = 1;

class MahoSidebarTopBarCharacterizationTest
    : public BrowserWithTestWindowTest {
 public:
  MahoSidebarTopBarCharacterizationTest() = default;
  ~MahoSidebarTopBarCharacterizationTest() override = default;

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    top_bar_ = std::make_unique<MahoSidebarTopBarView>(browser());
    top_bar_->SetBounds(0, 0, sidebar_layout::kDefaultRailWidthDp, 40);
  }

  void TearDown() override {
    top_bar_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  MahoSidebarTopBarView* top_bar() { return top_bar_.get(); }

 private:
  std::unique_ptr<MahoSidebarTopBarView> top_bar_;
};

TEST_F(MahoSidebarTopBarCharacterizationTest,
       TopBarContainsExpectedClustersAndActions) {
  const auto& children = top_bar()->children();
  ASSERT_GE(children.size(), 3u);
  EXPECT_EQ(children[kLeadingClusterIndex],
            top_bar()->leading_cluster_for_testing());

  views::View* nav = top_bar()->nav_cluster_for_testing();
  ASSERT_EQ(nav->children().size(), 2u);
  EXPECT_EQ(nav->children()[1], top_bar()->ai_button_for_testing());
  EXPECT_EQ(top_bar()->leading_cluster_for_testing()->children()[0],
            top_bar()->sidebar_toggle_button_for_testing());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       ControlledTabCallbackPropagatesTarget) {
  std::string controlled_tab;
  top_bar()->SetControlledTabChangedCallback(base::BindRepeating(
      [](std::string* out, std::string tab_id) { *out = std::move(tab_id); },
      &controlled_tab));

  maho::ai::ControlActivity activity;
  activity.observer_revision = 1;
  activity.session_id = "test-session";
  activity.state = maho::ai::ControlActivityState::kActing;
  activity.target = maho::ai::ControlTarget{
      .window_id = browser()->GetSessionID().id(),
      .tab_id = 999,
      .title = u"Controlled Tab",
  };

  top_bar()->OnControlActivityChanged(activity);
}

}
}
