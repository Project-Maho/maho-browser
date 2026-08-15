// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace maho {

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_NamedKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Enter"),
            ui::VKEY_RETURN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Tab"), ui::VKEY_TAB);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Escape"),
            ui::VKEY_ESCAPE);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Backspace"),
            ui::VKEY_BACK);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Delete"),
            ui::VKEY_DELETE);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_ArrowKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowUp"),
            ui::VKEY_UP);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowDown"),
            ui::VKEY_DOWN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowLeft"),
            ui::VKEY_LEFT);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowRight"),
            ui::VKEY_RIGHT);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_NavigationKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Home"),
            ui::VKEY_HOME);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("End"), ui::VKEY_END);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("PageUp"),
            ui::VKEY_PRIOR);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("PageDown"),
            ui::VKEY_NEXT);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_Space) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode(" "),
            ui::VKEY_SPACE);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Space"),
            ui::VKEY_SPACE);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_FunctionKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("F1"), ui::VKEY_F1);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("F5"), ui::VKEY_F5);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("F12"), ui::VKEY_F12);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_Alphanumeric) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("a"), ui::VKEY_A);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Z"), ui::VKEY_Z);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("0"), ui::VKEY_0);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("9"), ui::VKEY_9);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_Unknown) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode(""),
            ui::VKEY_UNKNOWN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("MadeUpKey"),
            ui::VKEY_UNKNOWN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("!"),
            ui::VKEY_UNKNOWN);
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_None) {
  EXPECT_EQ(MahoMcpInputSynthesizer::ModifierListToWebModifiers({}),
            static_cast<int>(blink::WebInputEvent::kNoModifiers));
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_Shift) {
  int result = MahoMcpInputSynthesizer::ModifierListToWebModifiers({"shift"});
  EXPECT_TRUE(result & blink::WebInputEvent::kShiftKey);
  EXPECT_FALSE(result & blink::WebInputEvent::kControlKey);
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_MultipleModifiers) {
  int result = MahoMcpInputSynthesizer::ModifierListToWebModifiers(
      {"shift", "ctrl", "alt", "meta"});
  EXPECT_TRUE(result & blink::WebInputEvent::kShiftKey);
  EXPECT_TRUE(result & blink::WebInputEvent::kControlKey);
  EXPECT_TRUE(result & blink::WebInputEvent::kAltKey);
  EXPECT_TRUE(result & blink::WebInputEvent::kMetaKey);
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_UnknownIgnored) {
  int result = MahoMcpInputSynthesizer::ModifierListToWebModifiers(
      {"shift", "hyper", "unknown"});
  EXPECT_TRUE(result & blink::WebInputEvent::kShiftKey);
  EXPECT_EQ(result,
            static_cast<int>(blink::WebInputEvent::kShiftKey));
}

TEST(MahoMcpInputSynthesizerTest, NullWebContentsNeverDispatchesOrMarks) {
  EXPECT_FALSE(MahoMcpInputSynthesizer::HoverAt(nullptr, 10, 20));
  EXPECT_FALSE(MahoMcpInputSynthesizer::ClickAt(
      nullptr, 10, 20, {}, /*sensitive=*/false));
}

}  // namespace maho
