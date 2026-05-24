// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.h"

#include <array>
#include <string_view>
#include <memory>

#include "base/run_loop.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

constexpr std::array<std::u16string_view, 6> kExpectedRowLabels = {
    u"Copy", u"Save...", u"Retake", u"Edit",
    u"Send via iMessage...", u"Save to Library"};

class MahoDomScreenshotActionDialogViewTest : public views::ViewsTestBase {
 protected:
  std::unique_ptr<views::Widget> CreateAnchorWidget() {
    auto widget =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget->SetBounds(gfx::Rect(100, 100, 800, 600));
    widget->Show();
    return widget;
  }

  MahoDomScreenshotActionMenu CreateMenu(int* header_count,
                                         int* copy_count,
                                         int* save_count,
                                         int* retake_count,
                                         int* edit_count,
                                         int* imessage_count,
                                         int* library_count,
                                         int* dismiss_count,
                                         bool edit_enabled = false) {
    MahoDomScreenshotActionMenu menu;
    menu.header_label = u"Send To...";
    menu.header_sf_symbol_name = "square.and.arrow.up";
    menu.header_activate =
        base::BindOnce([](int* count) { ++(*count); }, header_count);
    menu.rows.push_back({u"Copy", "doc.on.doc", true, false,
                         base::BindOnce([](int* count) { ++(*count); },
                                        copy_count)});
    menu.rows.push_back({u"Save...", "arrow.down.to.line", true, false,
                         base::BindOnce([](int* count) { ++(*count); },
                                        save_count)});
    menu.rows.push_back({u"Retake", "arrow.clockwise", true, false,
                         base::BindOnce([](int* count) { ++(*count); },
                                        retake_count)});
    menu.rows.push_back({u"Edit", "pencil.tip.crop.circle", edit_enabled,
                         false,
                         edit_enabled
                             ? base::BindOnce([](int* count) { ++(*count); },
                                              edit_count)
                             : base::OnceClosure()});
    menu.rows.push_back({u"Send via iMessage...", "message", false, false,
                         imessage_count
                             ? base::BindOnce([](int* count) { ++(*count); },
                                              imessage_count)
                             : base::OnceClosure()});
    menu.rows.push_back({u"Save to Library", "tray.and.arrow.down", false,
                         false,
                         library_count
                             ? base::BindOnce([](int* count) { ++(*count); },
                                              library_count)
                             : base::OnceClosure()});
    menu.dismiss_callback =
        base::BindOnce([](int* count) { ++(*count); }, dismiss_count);
    return menu;
  }
};

TEST_F(MahoDomScreenshotActionDialogViewTest,
       UsesLeftTopArrowWhenSelectionFitsOnRight) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  const gfx::Rect selection_rect(120, 120, 120, 90);

  views::BubbleBorder::Arrow arrow =
      MahoDomScreenshotActionDialogView::GetArrowForSelection(
          anchor_widget->GetNativeView(), selection_rect);

  EXPECT_EQ(arrow, views::BubbleBorder::LEFT_TOP);
  EXPECT_EQ(MahoDomScreenshotActionDialogView::GetAnchorRectForSelection(
                anchor_widget->GetNativeView(), selection_rect)
                .x(),
            selection_rect.right() + 8);
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
       UsesRightTopArrowWhenSelectionWouldOverflowRight) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  const gfx::Rect work_area = anchor_widget->GetWorkAreaBoundsInScreen();
  const gfx::Rect selection_rect(work_area.right() - 40, work_area.y() + 40, 24,
                                 24);

  views::BubbleBorder::Arrow arrow =
      MahoDomScreenshotActionDialogView::GetArrowForSelection(
          anchor_widget->GetNativeView(), selection_rect);

  EXPECT_EQ(arrow, views::BubbleBorder::RIGHT_TOP);
  EXPECT_EQ(MahoDomScreenshotActionDialogView::GetAnchorRectForSelection(
                anchor_widget->GetNativeView(), selection_rect)
                .right(),
            selection_rect.x() - 8 + 1);
}

TEST_F(MahoDomScreenshotActionDialogViewTest, MenuRendersExpectedRowsInOrder) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);

  EXPECT_EQ(bubble->header_label_for_testing(), u"Send To...");
  ASSERT_NE(bubble->header_button_for_testing(), nullptr);
  EXPECT_EQ(bubble->header_button_for_testing()->GetViewAccessibility().GetCachedName(),
            u"Send To...");
  ASSERT_EQ(bubble->row_count_for_testing(), kExpectedRowLabels.size());
  for (size_t i = 0; i < kExpectedRowLabels.size(); ++i) {
    EXPECT_EQ(bubble->row_label_for_testing(i), kExpectedRowLabels[i]);
    ASSERT_NE(bubble->row_button_for_testing(i), nullptr);
    EXPECT_EQ(
        bubble->row_button_for_testing(i)->GetViewAccessibility().GetCachedName(),
        std::u16string(kExpectedRowLabels[i]));
  }
  EXPECT_EQ(bubble->GetViewAccessibility().GetCachedName(),
            u"Selected area screenshot result actions");

  bubble_widget->CloseNow();
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
       RetakeRowDismissesBubbleAndFiresActivate) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);
  bubble->PressRowForTesting(2);

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(retake_count, 1);
  EXPECT_EQ(dismiss_count, 1);
  EXPECT_EQ(copy_count, 0);
  EXPECT_EQ(save_count, 0);
  EXPECT_TRUE(bubble_widget->IsClosed());
}

TEST_F(MahoDomScreenshotActionDialogViewTest, DisabledRowDoesNotFireActivate) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);
  EXPECT_FALSE(bubble->row_enabled_for_testing(3));
  ASSERT_NE(bubble->row_button_for_testing(3), nullptr);
  EXPECT_FALSE(bubble->row_button_for_testing(3)->GetEnabled());
  bubble->PressRowForTesting(3);

  EXPECT_EQ(edit_count, 0);
  EXPECT_EQ(dismiss_count, 0);

  bubble_widget->CloseNow();
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
       EscapeStillDismissesAndFiresDismissCallback) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);

  EXPECT_TRUE(
      bubble->AcceleratorPressed(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(dismiss_count, 1);
  EXPECT_EQ(header_count, 0);
  EXPECT_EQ(copy_count, 0);
  EXPECT_EQ(retake_count, 0);
  EXPECT_TRUE(bubble_widget->IsClosed());
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
       HeaderAffordanceFiresActivateAndDismisses) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);
  bubble->PressHeaderForTesting();

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(header_count, 1);
  EXPECT_EQ(dismiss_count, 1);
  EXPECT_EQ(copy_count, 0);
  EXPECT_TRUE(bubble_widget->IsClosed());
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
        RequestFocusTargetsHeaderAffordance) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);
  ASSERT_NE(bubble->header_button_for_testing(), nullptr);

  bubble->RequestFocus();

  EXPECT_EQ(bubble_widget->GetFocusManager()->GetFocusedView(),
            static_cast<views::View*>(bubble->header_button_for_testing()));

  bubble_widget->CloseNow();
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
       ArrowKeysMoveFocusAcrossEnabledRows) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  auto* bubble = static_cast<MahoDomScreenshotActionDialogView*>(
      bubble_widget->widget_delegate()->GetContentsView());
  ASSERT_NE(bubble, nullptr);

  bubble->RequestFocus();
  EXPECT_EQ(bubble_widget->GetFocusManager()->GetFocusedView(),
            static_cast<views::View*>(bubble->header_button_for_testing()));

  EXPECT_TRUE(
      bubble->AcceleratorPressed(ui::Accelerator(ui::VKEY_DOWN, ui::EF_NONE)));
  EXPECT_EQ(bubble_widget->GetFocusManager()->GetFocusedView(),
            static_cast<views::View*>(bubble->row_button_for_testing(0)));

  EXPECT_TRUE(
      bubble->AcceleratorPressed(ui::Accelerator(ui::VKEY_DOWN, ui::EF_NONE)));
  EXPECT_EQ(bubble_widget->GetFocusManager()->GetFocusedView(),
            static_cast<views::View*>(bubble->row_button_for_testing(1)));

  EXPECT_TRUE(
      bubble->AcceleratorPressed(ui::Accelerator(ui::VKEY_UP, ui::EF_NONE)));
  EXPECT_EQ(bubble_widget->GetFocusManager()->GetFocusedView(),
            static_cast<views::View*>(bubble->row_button_for_testing(0)));

  bubble_widget->CloseNow();
}

TEST_F(MahoDomScreenshotActionDialogViewTest,
       WidgetCloseRunsDismissCallbackExactlyOnce) {
  std::unique_ptr<views::Widget> anchor_widget = CreateAnchorWidget();
  int header_count = 0;
  int copy_count = 0;
  int save_count = 0;
  int retake_count = 0;
  int edit_count = 0;
  int imessage_count = 0;
  int library_count = 0;
  int dismiss_count = 0;

  views::Widget* bubble_widget = MahoDomScreenshotActionDialogView::Show(
      anchor_widget->GetNativeView(), gfx::Rect(140, 140, 160, 120),
      CreateMenu(&header_count, &copy_count, &save_count, &retake_count,
                 &edit_count, &imessage_count, &library_count,
                 &dismiss_count));
  ASSERT_NE(bubble_widget, nullptr);

  base::RunLoop run_loop;
  bubble_widget->CloseWithReason(views::Widget::ClosedReason::kCloseButtonClicked);
  run_loop.RunUntilIdle();

  EXPECT_EQ(dismiss_count, 1);
}

}  // namespace
}  // namespace maho
