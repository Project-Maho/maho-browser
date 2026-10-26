// Copyright 2026 Maho Browser. All rights reserved.
//
// Pins the repair for "Cmd+T opens a blank tab". BrowserView registers the
// Maho accelerators once, in AddedToWidget(), roughly a second before
// maho-core is ready; with an empty registry only Ctrl+1..9 got registered, so
// Cmd+T and Ctrl+Tab fell through to the native menu path. After core is ready
// RegisterMissingAccelerators() must add the rest at high priority, and must be
// safe to call again.

#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"

#include <memory>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/test_accelerator_target.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

class MahoShortcutAcceleratorRegistrationTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  }

  void TearDown() override {
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  views::FocusManager* focus_manager() { return widget_->GetFocusManager(); }

 private:
  std::unique_ptr<views::Widget> widget_;
};

const ui::Accelerator kCmdT(ui::VKEY_T, ui::EF_COMMAND_DOWN);
const ui::Accelerator kCtrlTab(ui::VKEY_TAB, ui::EF_CONTROL_DOWN);
const ui::Accelerator kCtrl1(ui::VKEY_1, ui::EF_CONTROL_DOWN);

TEST_F(MahoShortcutAcceleratorRegistrationTest, RegistersMissingAtHighPriority) {
  ui::TestAcceleratorTarget target;
  EXPECT_EQ(2u, MahoShortcutInterceptor::RegisterMissingAccelerators(
                    focus_manager(), &target, {kCmdT, kCtrlTab}));
  EXPECT_TRUE(focus_manager()->HasPriorityHandler(kCmdT));
  EXPECT_TRUE(focus_manager()->HasPriorityHandler(kCtrlTab));
}

// The window already holds the Ctrl+1..9 fallback; completing the set after
// core is ready must not re-register those (the AcceleratorManager DCHECKs on a
// second high-priority handler for the same key).
TEST_F(MahoShortcutAcceleratorRegistrationTest, SkipsAlreadyRegistered) {
  ui::TestAcceleratorTarget target;
  focus_manager()->RegisterAccelerator(
      kCtrl1, ui::AcceleratorManager::kHighPriority, &target);

  EXPECT_EQ(2u, MahoShortcutInterceptor::RegisterMissingAccelerators(
                    focus_manager(), &target, {kCtrl1, kCmdT, kCtrlTab}));
  EXPECT_TRUE(focus_manager()->HasPriorityHandler(kCtrl1));
  EXPECT_TRUE(focus_manager()->HasPriorityHandler(kCmdT));
}

TEST_F(MahoShortcutAcceleratorRegistrationTest, IsIdempotent) {
  ui::TestAcceleratorTarget target;
  const std::vector<ui::Accelerator> all = {kCmdT, kCtrlTab};
  EXPECT_EQ(2u, MahoShortcutInterceptor::RegisterMissingAccelerators(
                    focus_manager(), &target, all));
  EXPECT_EQ(0u, MahoShortcutInterceptor::RegisterMissingAccelerators(
                    focus_manager(), &target, all));
}

TEST_F(MahoShortcutAcceleratorRegistrationTest, NullArgumentsRegisterNothing) {
  ui::TestAcceleratorTarget target;
  EXPECT_EQ(0u, MahoShortcutInterceptor::RegisterMissingAccelerators(
                    nullptr, &target, {kCmdT}));
  EXPECT_EQ(0u, MahoShortcutInterceptor::RegisterMissingAccelerators(
                    focus_manager(), nullptr, {kCmdT}));
  EXPECT_FALSE(focus_manager()->IsAcceleratorRegistered(kCmdT));
}

}  // namespace
}  // namespace maho
