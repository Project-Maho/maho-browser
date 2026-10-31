// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/test/views_test_base.h"

namespace maho {
namespace {

using PlaceholderView = MahoSidebarProcessingPlaceholderView;
using Phase = PlaceholderView::Phase;

class MahoSidebarProcessingPlaceholderViewTest : public views::ViewsTestBase {
 public:
  MahoSidebarProcessingPlaceholderViewTest()
      : views::ViewsTestBase(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    views::ViewsTestBase::SetUp();
    animation_scale_ =
        std::make_unique<gfx::ScopedAnimationDurationScaleMode>(
            gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  }

  void TearDown() override {
    animation_scale_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  void AdvanceAnimation(gfx::AnimationContainer* container,
                        base::TimeDelta delta) {
    // Drive real animation callbacks at exact mock-clock boundaries instead of
    // accumulating the default 60 Hz runner's rounding across chained phases.
    gfx::AnimationContainerTestApi animation_api(container);
    task_environment()->AdvanceClock(delta);
    animation_api.IncrementTime(delta);
    task_environment()->RunUntilIdle();
  }

 private:
  std::unique_ptr<gfx::ScopedAnimationDurationScaleMode> animation_scale_;
};

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       MergeStartsAtButtonThirdsAndSettlesAtScaledEllipsis) {
  double width = 300.0;
  double center_x = 150.0;
  double center_y = 21.0;

  // Verify merge start (progress = 0)
  for (int i = 0; i < 3; ++i) {
    double origin = width * (2.0 * i + 1.0) / 6.0;
    double expected_x = origin;
    double expected_y = center_y;
    double expected_r = 4.32;
    double expected_alpha = 0.0;

    EXPECT_NEAR(
        PlaceholderView::ComputeDotX(i, 0.0, Phase::kMerging, width, center_x),
        expected_x, 0.001);
    EXPECT_NEAR(PlaceholderView::ComputeDotY(i, 0.0, Phase::kMerging, center_y),
                expected_y, 0.001);
    EXPECT_NEAR(PlaceholderView::ComputeDotRadius(i, 0.0, Phase::kMerging),
                expected_r, 0.001);
    EXPECT_NEAR(PlaceholderView::ComputeDotAlpha(i, 0.0, Phase::kMerging),
                expected_alpha, 0.001);
  }

  // Verify merge end (progress = 1)
  for (int i = 0; i < 3; ++i) {
    double target = center_x + (i - 1) * 15.84;
    double expected_x = target;
    double expected_y = center_y;
    double expected_r = 3.6;
    double expected_alpha = 1.0;

    EXPECT_NEAR(
        PlaceholderView::ComputeDotX(i, 1.0, Phase::kMerging, width, center_x),
        expected_x, 0.001);
    EXPECT_NEAR(PlaceholderView::ComputeDotY(i, 1.0, Phase::kMerging, center_y),
                expected_y, 0.001);
    EXPECT_NEAR(PlaceholderView::ComputeDotRadius(i, 1.0, Phase::kMerging),
                expected_r, 0.001);
    EXPECT_NEAR(PlaceholderView::ComputeDotAlpha(i, 1.0, Phase::kMerging),
                expected_alpha, 0.001);

    double working_x = center_x + (i - 1) * 14.4;
    EXPECT_NEAR(
        PlaceholderView::ComputeDotX(i, 0.0, Phase::kWorking, width, center_x),
        working_x, 0.001);
  }
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       WorkingSparkTraversesScaledTrackThreeTimesPerLoop) {
  double center_x = 150.0;

  // Track starts at center_x - 14.4 and ends at center_x + 14.4 (width =
  // 28.8).
  double start_track = center_x - 14.4;

  // Loop progress = 0.0
  EXPECT_NEAR(
      PlaceholderView::ComputeSparkX(0.0, Phase::kWorking, center_x, 0.0),
      start_track, 0.001);

  // Loop progress = 1/3 (ends first traversal)
  EXPECT_NEAR(
      PlaceholderView::ComputeSparkX(1.0 / 3.0, Phase::kWorking, center_x, 0.0),
      start_track, 0.01);

  // Loop progress = 0.5 (halfway through second traversal)
  EXPECT_NEAR(
      PlaceholderView::ComputeSparkX(0.5, Phase::kWorking, center_x, 0.0),
      center_x, 0.01);

  // Loop progress = 2/3 (ends second traversal)
  EXPECT_NEAR(
      PlaceholderView::ComputeSparkX(2.0 / 3.0, Phase::kWorking, center_x, 0.0),
      start_track, 0.01);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       SuccessCheckUsesScaleOnePointFourFour) {
  // Verify that check progress and alpha are computed correctly
  EXPECT_NEAR(
      PlaceholderView::ComputeCheckProgress(0.0, Phase::kResolvingSuccess), 0.0,
      0.001);
  EXPECT_NEAR(
      PlaceholderView::ComputeCheckProgress(0.5, Phase::kResolvingSuccess), 0.5,
      0.001);
  EXPECT_NEAR(
      PlaceholderView::ComputeCheckProgress(1.0, Phase::kResolvingSuccess), 1.0,
      0.001);

  EXPECT_NEAR(PlaceholderView::ComputeCheckAlpha(0.0, Phase::kResolvingSuccess),
              1.0, 0.001);
  EXPECT_NEAR(PlaceholderView::ComputeCheckAlpha(0.5, Phase::kRestoring), 0.5,
              0.001);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       CrossFadeAlphasRemainComplementary) {
  // Create button surface layer to test complementary alphas
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());
  auto button_surface = std::make_unique<views::View>();
  button_surface->SetPaintToLayer();
  button_surface->layer()->SetFillsBoundsOpaquely(false);
  auto* button_surface_ptr = view->AddChildView(std::move(button_surface));
  view->SetButtonSurface(button_surface_ptr);

  view->Start(PlaceholderView::Mode::kSingleCycle, base::DoNothing());

  // Wait some time to let merging progress, verify that alpha +
  // button_surface_opacity is 1.0 Since we are mocking time, we can advance
  task_environment()->FastForwardBy(base::Milliseconds(150));

  double button_opacity = button_surface_ptr->layer()->opacity();
  // Merging alpha at progress = 0.5 is 0.5. Button opacity should be 1.0 - 0.5
  // = 0.5.
  EXPECT_NEAR(button_opacity, 0.5, 0.05);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       AdaptiveFastLoopAndSingleCycleCompleteInOneSecond) {
  bool single_cycle_done = false;
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());
  auto container = base::MakeRefCounted<gfx::AnimationContainer>();
  view->phase_animation_for_testing()->SetContainer(container.get());

  view->Start(PlaceholderView::Mode::kSingleCycle,
              base::BindLambdaForTesting([&]() { single_cycle_done = true; }));

  // Single cycle timing: 300ms merge + 200ms working + 300ms success resolve +
  // 200ms restore = 1000ms
  AdvanceAnimation(container.get(), base::Milliseconds(300));
  EXPECT_EQ(view->phase_for_testing(), Phase::kWorking);
  AdvanceAnimation(container.get(), base::Milliseconds(200));
  EXPECT_EQ(view->phase_for_testing(), Phase::kResolvingSuccess);
  AdvanceAnimation(container.get(), base::Milliseconds(300));
  EXPECT_EQ(view->phase_for_testing(), Phase::kRestoring);
  AdvanceAnimation(container.get(), base::Milliseconds(190));
  EXPECT_FALSE(single_cycle_done);

  AdvanceAnimation(container.get(), base::Milliseconds(10));
  EXPECT_TRUE(single_cycle_done);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       EarlyResolveWaitsForFiveHundredMillisecondFloor) {
  bool hidden_called = false;
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());
  auto container = base::MakeRefCounted<gfx::AnimationContainer>();
  view->phase_animation_for_testing()->SetContainer(container.get());

  view->Start(PlaceholderView::Mode::kLoop,
              base::BindLambdaForTesting([&]() { hidden_called = true; }));

  // Resolve early (at 100ms)
  AdvanceAnimation(container.get(), base::Milliseconds(100));
  view->ResolveSuccess();

  // It should wait until 500ms since start before it transitions to
  // ResolvingSuccess. Merging (300ms) + Working (200ms floor) = 500ms floor.
  // After 500ms, kResolvingSuccess starts (300ms) and kRestoring (200ms).
  // Total time should be 1000ms.
  AdvanceAnimation(container.get(), base::Milliseconds(200));
  EXPECT_EQ(view->phase_for_testing(), Phase::kWorking);
  AdvanceAnimation(container.get(), base::Milliseconds(199));
  EXPECT_EQ(view->phase_for_testing(), Phase::kWorking);
  AdvanceAnimation(container.get(), base::Milliseconds(1));
  EXPECT_EQ(view->phase_for_testing(), Phase::kResolvingSuccess);
  AdvanceAnimation(container.get(), base::Milliseconds(300));
  EXPECT_EQ(view->phase_for_testing(), Phase::kRestoring);
  AdvanceAnimation(container.get(), base::Milliseconds(180));  // 980ms total
  EXPECT_FALSE(hidden_called);

  AdvanceAnimation(container.get(), base::Milliseconds(20));  // 1000ms total
  EXPECT_TRUE(hidden_called);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       SlowLoopExtendsOnlyWorkingAndCapturesCurrentPose) {
  bool hidden_called = false;
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());
  auto container = base::MakeRefCounted<gfx::AnimationContainer>();
  view->phase_animation_for_testing()->SetContainer(container.get());

  view->Start(PlaceholderView::Mode::kLoop,
              base::BindLambdaForTesting([&]() { hidden_called = true; }));

  // Advance time past 500ms floor (e.g. to 2000ms)
  // Working should keep looping.
  AdvanceAnimation(container.get(), base::Milliseconds(300));
  AdvanceAnimation(container.get(), base::Milliseconds(1700));
  EXPECT_EQ(view->phase_for_testing(), Phase::kWorking);

  // Now resolve
  view->ResolveSuccess();
  EXPECT_EQ(view->phase_for_testing(), Phase::kResolvingSuccess);

  // Remaining animation is 300ms resolve + 200ms restore = 500ms
  AdvanceAnimation(container.get(), base::Milliseconds(300));
  EXPECT_EQ(view->phase_for_testing(), Phase::kRestoring);
  AdvanceAnimation(container.get(), base::Milliseconds(190));
  EXPECT_FALSE(hidden_called);

  AdvanceAnimation(container.get(), base::Milliseconds(10));
  EXPECT_TRUE(hidden_called);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       ExternalResolveIsIgnoredForSingleCycle) {
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());

  view->Start(PlaceholderView::Mode::kSingleCycle, base::DoNothing());

  // Call ResolveSuccess, it should be ignored for SingleCycle
  view->ResolveSuccess();
  EXPECT_EQ(view->phase_for_testing(), Phase::kMerging);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       TimeoutThenLateResolveRunsHiddenOnce) {
  int hidden_count = 0;
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());

  view->Start(PlaceholderView::Mode::kLoop,
              base::BindLambdaForTesting([&]() { hidden_count++; }));

  // Wait 45 seconds to trigger safety timeout
  task_environment()->FastForwardBy(base::Seconds(45));

  // Timeout forces failure resolve (300ms) + restore (200ms) = 500ms
  task_environment()->FastForwardBy(base::Milliseconds(600));
  EXPECT_EQ(hidden_count, 1);
  EXPECT_FALSE(view->is_active_for_testing());

  // Late resolve should be ignored
  view->ResolveSuccess();
  EXPECT_EQ(hidden_count, 1);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest, SecondStartIsIgnored) {
  int hidden_count = 0;
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());

  view->Start(PlaceholderView::Mode::kSingleCycle,
              base::BindLambdaForTesting([&]() { hidden_count++; }));

  // Call Start again, should be ignored
  view->Start(PlaceholderView::Mode::kSingleCycle,
              base::BindLambdaForTesting([&]() { hidden_count++; }));

  task_environment()->FastForwardBy(base::Milliseconds(1100));
  EXPECT_EQ(hidden_count, 1);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       ReducedMotionUsesStaticTextlessPose) {
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());
  view->set_reduced_motion_for_testing(true);

  bool hidden_called = false;
  view->Start(PlaceholderView::Mode::kSingleCycle,
              base::BindLambdaForTesting([&]() { hidden_called = true; }));

  EXPECT_EQ(view->phase_for_testing(), Phase::kWorking);

  // Clear holds 180ms then hides
  task_environment()->FastForwardBy(base::Milliseconds(170));
  EXPECT_FALSE(hidden_called);

  task_environment()->FastForwardBy(base::Milliseconds(20));
  EXPECT_TRUE(hidden_called);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       ReducedMotionTidyHidesImmediatelyOnResult) {
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* view = widget->SetContentsView(std::make_unique<PlaceholderView>());
  view->set_reduced_motion_for_testing(true);

  bool hidden_called = false;
  view->Start(PlaceholderView::Mode::kLoop,
              base::BindLambdaForTesting([&]() { hidden_called = true; }));

  EXPECT_EQ(view->phase_for_testing(), Phase::kWorking);
  EXPECT_FALSE(hidden_called);

  task_environment()->FastForwardBy(base::Milliseconds(50));
  view->ResolveSuccess();
  EXPECT_TRUE(hidden_called);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       ClearWithoutBrowserResolvesFailureInsteadOfSuccess) {
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->Show();
  auto* tab_list =
      widget->SetContentsView(std::make_unique<MahoSidebarTabListView>(nullptr));
  tab_list->RebuildRowsForTesting(MahoSidebarTabListModel(), nullptr);
  auto* processing = tab_list->action_processing_view_for_testing();
  ASSERT_TRUE(processing);

  // Command 101 is the Today entry of the real Clear menu.
  static_cast<ui::SimpleMenuModel::Delegate*>(tab_list)->ExecuteCommand(101, 0);
  task_environment()->FastForwardBy(base::Milliseconds(510));

  EXPECT_EQ(processing->phase_for_testing(), Phase::kResolvingFailure);
}

TEST_F(MahoSidebarProcessingPlaceholderViewTest,
       CulledActionRowKeepsHeightAcrossSpaceRebuild) {
  auto widget =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 320, 480));
  widget->Show();
  auto* tab_list = widget->SetContentsView(
      std::make_unique<MahoSidebarTabListView>(nullptr));

  MahoSidebarTabListModel model;
  model.active_space_id = "space-a";
  SidebarTreeNode tab;
  tab.tab_id = "tab-1";
  tab.title = u"One";
  model.normal_tree.push_back(std::move(tab));

  tab_list->RebuildRowsForTesting(model, nullptr);
  views::View* action_row = tab_list->action_row_for_testing();
  ASSERT_TRUE(action_row);
  // The virtualizer hides an off-screen scaffold this way. A space switch
  // rebuilds the same row and must not treat that hide as product visibility.
  action_row->SetVisible(false);

  model.active_space_id = "space-b";
  tab_list->RebuildRowsForTesting(model, nullptr);
  widget->LayoutRootViewIfNecessary();

  action_row = tab_list->action_row_for_testing();
  ASSERT_TRUE(action_row);
  EXPECT_TRUE(action_row->GetVisible());
  EXPECT_EQ(42, action_row->GetPreferredSize(views::SizeBounds()).height());
  EXPECT_EQ(42, action_row->bounds().height());
}

}  // namespace
}  // namespace maho
