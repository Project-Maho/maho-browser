// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_row_modifiers.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"

namespace maho {
namespace {

TEST(MahoSidebarTabRowModifiersTest, MacTreatsOnlyCommandAsMultiSelect) {
  EXPECT_TRUE(IsTabMultiSelectModifierForPlatform(ui::EF_COMMAND_DOWN, true));
  EXPECT_FALSE(IsTabMultiSelectModifierForPlatform(ui::EF_CONTROL_DOWN, true));
  EXPECT_FALSE(IsTabMultiSelectModifierForPlatform(ui::EF_NONE, true));
  EXPECT_FALSE(IsTabMultiSelectModifierForPlatform(
      ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN, true));
}

TEST(MahoSidebarTabRowModifiersTest, NonMacAcceptsCommandOrControl) {
  EXPECT_TRUE(IsTabMultiSelectModifierForPlatform(ui::EF_COMMAND_DOWN, false));
  EXPECT_TRUE(IsTabMultiSelectModifierForPlatform(ui::EF_CONTROL_DOWN, false));
  EXPECT_FALSE(IsTabMultiSelectModifierForPlatform(ui::EF_NONE, false));
  EXPECT_FALSE(IsTabMultiSelectModifierForPlatform(ui::EF_SHIFT_DOWN, false));
}

TEST(MahoSidebarTabRowModifiersTest, MacCtrlLeftClickIsNotASelectionModifier) {
  const int ctrl_left_click = ui::EF_CONTROL_DOWN | ui::EF_LEFT_MOUSE_BUTTON;
  EXPECT_FALSE(IsTabMultiSelectModifierForPlatform(ctrl_left_click, true));
  EXPECT_TRUE(IsTabMultiSelectModifierForPlatform(ctrl_left_click, false));
}

}  // namespace
}  // namespace maho
