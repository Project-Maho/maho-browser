// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "ui/events/event.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace maho {
namespace {

using MahoSidebarUpdateNotificationViewTest = views::ViewsTestBase;

MahoSidebarUpdateNotificationModel MakeModel(int action_count = 2) {
  MahoSidebarUpdateNotificationModel model;
  model.visible = true;
  model.heading = u"Maho is ready to update";
  for (int i = 0; i < action_count; ++i) {
    MahoSidebarUpdateActionModel a;
    a.label = i == 0 ? u"Restart now" : u"What's new";
    a.special = i == 0;
    a.on_activate = base::DoNothing();
    model.actions.push_back(std::move(a));
  }
  return model;
}

TEST_F(MahoSidebarUpdateNotificationViewTest, RendersHeading) {
  MahoSidebarUpdateNotificationView view;
  view.Update(MakeModel());

  ASSERT_TRUE(view.heading_for_testing());
  EXPECT_EQ(view.heading_for_testing()->GetText(), u"Maho is ready to update");
}

TEST_F(MahoSidebarUpdateNotificationViewTest, RendersAllActionRows) {
  MahoSidebarUpdateNotificationView view;
  view.Update(MakeModel(/*action_count=*/3));
  EXPECT_EQ(view.action_row_count_for_testing(), 3u);
}

TEST_F(MahoSidebarUpdateNotificationViewTest,
       ClickingActionRowInvokesOnActivate) {
  MahoSidebarUpdateNotificationView view;
  int restart_count = 0;
  MahoSidebarUpdateNotificationModel model = MakeModel();
  model.actions[0].on_activate =
      base::BindLambdaForTesting([&] { ++restart_count; });
  view.Update(model);

  views::test::ButtonTestApi(view.action_row_for_testing(0))
      .NotifyClick(ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(),
                                   gfx::Point(), base::TimeTicks::Now(), 0,
                                   0));
  EXPECT_EQ(restart_count, 1);
}

TEST_F(MahoSidebarUpdateNotificationViewTest, EmptyModelHidesView) {
  MahoSidebarUpdateNotificationView view;
  view.Update(MahoSidebarUpdateNotificationModel());
  EXPECT_FALSE(view.GetVisible());
}

TEST_F(MahoSidebarUpdateNotificationViewTest, RebuildsChildrenOnUpdate) {
  MahoSidebarUpdateNotificationView view;
  view.Update(MakeModel(/*action_count=*/2));
  EXPECT_EQ(view.action_row_count_for_testing(), 2u);

  view.Update(MakeModel(/*action_count=*/3));
  EXPECT_EQ(view.action_row_count_for_testing(), 3u);

  view.Update(MakeModel(/*action_count=*/1));
  EXPECT_EQ(view.action_row_count_for_testing(), 1u);
}

}  // namespace
}  // namespace maho
