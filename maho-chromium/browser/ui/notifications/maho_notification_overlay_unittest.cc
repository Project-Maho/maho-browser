// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_notification_overlay.h"

#include <memory>

#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "maho/browser/ui/notifications/maho_toast_view.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {

class MahoNotificationOverlayTest : public views::ViewsTestBase {
 protected:
  MahoNotificationOverlayTest()
      : views::ViewsTestBase(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  ~MahoNotificationOverlayTest() override = default;

  void SetUp() override {
    views::ViewsTestBase::SetUp();
    // Clean up any remaining static instances from previous tests
    MahoNotificationOverlay::RemoveFromBrowser(nullptr);
  }

  void TearDown() override {
    MahoNotificationOverlay::RemoveFromBrowser(nullptr);
    views::ViewsTestBase::TearDown();
  }

  std::unique_ptr<MahoNotificationOverlay> CreateOverlay() {
    auto overlay = std::unique_ptr<MahoNotificationOverlay>(new MahoNotificationOverlay(nullptr));
    overlay->set_context_for_testing(GetContext());
    return overlay;
  }
};

TEST_F(MahoNotificationOverlayTest, ShowCreatesWidget) {
  auto overlay = CreateOverlay();
  EXPECT_FALSE(overlay->IsVisible());
  EXPECT_EQ(overlay->widget_for_testing(), nullptr);

  overlay->Show(u"Test Title", u"Test Body", base::Seconds(4));
  EXPECT_TRUE(overlay->IsVisible());
  ASSERT_NE(overlay->widget_for_testing(), nullptr);
  EXPECT_TRUE(overlay->widget_for_testing()->IsVisible());
  ASSERT_NE(overlay->toast_view_for_testing(), nullptr);

  // Widget should stay open while timer is running
  task_environment()->FastForwardBy(base::Seconds(3));
  EXPECT_TRUE(overlay->IsVisible());
}

TEST_F(MahoNotificationOverlayTest, HideClosesWidget) {
  auto overlay = CreateOverlay();
  overlay->Show(u"Test Title", u"Test Body", base::Seconds(4));
  EXPECT_TRUE(overlay->IsVisible());

  overlay->Hide();
  // Animation should run (slide-out: 200ms)
  task_environment()->FastForwardBy(base::Milliseconds(300));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(overlay->IsVisible());
  EXPECT_EQ(overlay->widget_for_testing(), nullptr);
}

TEST_F(MahoNotificationOverlayTest, ShowReplacesExistingToast) {
  auto overlay = CreateOverlay();
  overlay->Show(u"First Title", u"First Body", base::Seconds(4));
  views::Widget* first_widget = overlay->widget_for_testing();
  ASSERT_NE(first_widget, nullptr);

  overlay->Show(u"Second Title", u"Second Body", base::Seconds(4));
  views::Widget* second_widget = overlay->widget_for_testing();
  ASSERT_NE(second_widget, nullptr);

  // The first widget should be closed and replaced
  EXPECT_NE(first_widget, second_widget);
  EXPECT_TRUE(overlay->IsVisible());
}

TEST_F(MahoNotificationOverlayTest, AutoDismissAfterDuration) {
  auto overlay = CreateOverlay();
  overlay->Show(u"Test Title", u"Test Body", base::Seconds(4));
  EXPECT_TRUE(overlay->IsVisible());

  // Fast forward past the 4 seconds duration + slide-out animation time (200ms)
  task_environment()->FastForwardBy(base::Seconds(4) + base::Milliseconds(300));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(overlay->IsVisible());
  EXPECT_EQ(overlay->widget_for_testing(), nullptr);
}

TEST_F(MahoNotificationOverlayTest, ClickDismisses) {
  auto overlay = CreateOverlay();
  overlay->Show(u"Test Title", u"Test Body", base::Seconds(4));
  EXPECT_TRUE(overlay->IsVisible());

  views::Widget* widget = overlay->widget_for_testing();
  MahoToastView* toast_view = overlay->toast_view_for_testing();
  ASSERT_NE(toast_view, nullptr);

  // Simulate mouse click on the toast view
  ui::test::EventGenerator generator(GetContext(), widget->GetNativeWindow());
  generator.MoveMouseTo(toast_view->GetBoundsInScreen().CenterPoint());
  generator.ClickLeftButton();

  // Clicking triggers slide-out animation (200ms)
  task_environment()->FastForwardBy(base::Milliseconds(300));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(overlay->IsVisible());
  EXPECT_EQ(overlay->widget_for_testing(), nullptr);
}


TEST_F(MahoNotificationOverlayTest, BrowserCloseCleansUp) {
  // Use GetOrCreateForBrowser to place it in the global map
  MahoNotificationOverlay* overlay = MahoNotificationOverlay::GetOrCreateForBrowser(nullptr);
  overlay->set_context_for_testing(GetContext());
  EXPECT_NE(MahoNotificationOverlay::FromBrowser(nullptr), nullptr);

  overlay->Show(u"Test Title", u"Test Body", base::Seconds(4));
  EXPECT_TRUE(overlay->IsVisible());

  // Destroying the widget should trigger OnWidgetDestroying and remove the map entry
  overlay->widget_for_testing()->CloseNow();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(MahoNotificationOverlay::FromBrowser(nullptr), nullptr);
}

class MahoToastViewTest : public views::ViewsTestBase {
 protected:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget_->Show();
    auto toast = std::make_unique<MahoToastView>();
    toast_ = widget_->SetContentsView(std::move(toast));
  }

  void TearDown() override {
    toast_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoToastView> toast_ = nullptr;
};

TEST_F(MahoToastViewTest, RendersTitleAndBody) {
  toast_->SetTitleText(u"Welcome to Maho");
  toast_->SetBodyText(u"Import complete. Start exploring.");

  EXPECT_EQ(toast_->title_label_for_testing()->GetText(), u"Welcome to Maho");
  EXPECT_EQ(toast_->body_label_for_testing()->GetText(),
            u"Import complete. Start exploring.");
}

TEST_F(MahoToastViewTest, RespectsMaxWidth) {
  toast_->SetTitleText(u"Title");
  toast_->SetBodyText(
      u"This is a very long body text that exceeds the maximum allowed width "
      u"of the toast and should be clamped to the configured upper bound so "
      u"the toast does not stretch arbitrarily wide on screen.");

  const gfx::Size preferred = toast_->GetPreferredSize();
  EXPECT_LE(preferred.width(), 360);
  EXPECT_GE(preferred.width(), 240);
}

TEST_F(MahoToastViewTest, ThemeChangeUpdatesColors) {
  toast_->SetTitleText(u"Title");
  toast_->SetBodyText(u"Body");

  toast_->OnThemeChanged();

  const auto* cp = toast_->GetColorProvider();
  ASSERT_NE(cp, nullptr);
  EXPECT_EQ(toast_->title_label_for_testing()->GetEnabledColor(),
            cp->GetColor(kMahoColorToastForeground));
  EXPECT_EQ(toast_->body_label_for_testing()->GetEnabledColor(),
            cp->GetColor(kMahoColorToastBodyForeground));
}

TEST_F(MahoToastViewTest, ShowsLeadingIcon) {
  toast_->SetTitleText(u"Link copied");

  toast_->OnThemeChanged();

  ASSERT_NE(toast_->icon_view_for_testing(), nullptr);
  EXPECT_FALSE(toast_->icon_view_for_testing()->GetImageModel().IsEmpty());
}

}  // namespace maho


