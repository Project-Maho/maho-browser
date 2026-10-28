// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_mini/maho_mini_shortcut.h"

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/base/accelerators/command.h"

namespace maho {
namespace {

TEST(MahoMiniShortcutTest, ParsesAcceleratorWithModifier) {
#if BUILDFLAG(IS_MAC)
  const auto parsed = ParseMiniShortcut("Alt+Command+N");
  EXPECT_EQ(parsed.accelerator.key_code(), ui::VKEY_N);
  EXPECT_TRUE(parsed.accelerator.IsAltDown());
  EXPECT_TRUE(parsed.accelerator.IsCmdDown());
#else
  const auto parsed = ParseMiniShortcut("Ctrl+Alt+N");
  EXPECT_EQ(parsed.accelerator.key_code(), ui::VKEY_N);
  EXPECT_TRUE(parsed.accelerator.IsCtrlDown());
  EXPECT_TRUE(parsed.accelerator.IsAltDown());
#endif
  EXPECT_TRUE(parsed.error.empty());
  EXPECT_EQ(ui::Command::AcceleratorToString(parsed.accelerator),
#if BUILDFLAG(IS_MAC)
            "Alt+Command+N"
#else
            "Ctrl+Alt+N"
#endif
  );
}

TEST(MahoMiniShortcutTest, RejectsEmptyAndModifierlessAccelerators) {
  EXPECT_FALSE(ParseMiniShortcut("").error.empty());
  EXPECT_FALSE(ParseMiniShortcut("N").error.empty());
}

TEST(MahoMiniShortcutTest, RejectsReservedQuitAccelerator) {
#if BUILDFLAG(IS_MAC)
  EXPECT_FALSE(ParseMiniShortcut("Command+Q").error.empty());
#else
  EXPECT_FALSE(ParseMiniShortcut("Ctrl+Q").error.empty());
#endif
}

}  // namespace
}  // namespace maho
