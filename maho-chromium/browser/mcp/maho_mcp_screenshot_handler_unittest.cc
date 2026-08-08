// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_screenshot_handler.h"

#include <vector>

#include "base/functional/callback.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/gfx/geometry/rect.h"

namespace maho {
namespace {

SkBitmap MakeTestBitmap(int width, int height, SkColor fill_color) {
  SkBitmap bitmap;
  SkImageInfo info =
      SkImageInfo::MakeN32(width, height, kOpaque_SkAlphaType);
  bitmap.allocPixels(info);
  bitmap.eraseColor(fill_color);
  return bitmap;
}

ui::AXTreeUpdate MakeTreeWithPasswordField(int password_x,
                                           int password_y,
                                           int password_w,
                                           int password_h) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(3);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2, 3};
  update.nodes[0].relative_bounds.bounds =
      gfx::RectF(0, 0, 800, 600);

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kTextField;
  update.nodes[1].SetName("Username");
  update.nodes[1].relative_bounds.bounds =
      gfx::RectF(10, 10, 200, 30);

  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kTextField;
  update.nodes[2].SetName("Password");
  update.nodes[2].AddState(ax::mojom::State::kProtected);
  update.nodes[2].relative_bounds.bounds = gfx::RectF(
      static_cast<float>(password_x), static_cast<float>(password_y),
      static_cast<float>(password_w), static_cast<float>(password_h));

  return update;
}

ui::AXTreeUpdate MakeTreeNoPassword() {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[0].relative_bounds.bounds =
      gfx::RectF(0, 0, 800, 600);

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kButton;
  update.nodes[1].SetName("Submit");
  update.nodes[1].relative_bounds.bounds =
      gfx::RectF(50, 50, 100, 40);

  return update;
}

TEST(MahoMcpScreenshotHandlerTest, CaptureFullPage_NoPassword_PngMagicBytes) {
  SkBitmap bitmap = MakeTestBitmap(100, 100, SK_ColorWHITE);
  ui::AXTreeUpdate update = MakeTreeNoPassword();
  ui::AXTree tree(update);

  std::vector<uint8_t> result_png;
  MahoMcpScreenshotHandler::CaptureFullPage(
      bitmap, &tree,
      base::BindOnce(
          [](std::vector<uint8_t>* out, std::vector<uint8_t> png) {
            *out = std::move(png);
          },
          &result_png));

  ASSERT_GE(result_png.size(), 8u);
  EXPECT_EQ(result_png[0], 0x89u);
  EXPECT_EQ(result_png[1], 0x50u);  // 'P'
  EXPECT_EQ(result_png[2], 0x4Eu);  // 'N'
  EXPECT_EQ(result_png[3], 0x47u);  // 'G'
  EXPECT_EQ(result_png[4], 0x0Du);
  EXPECT_EQ(result_png[5], 0x0Au);
  EXPECT_EQ(result_png[6], 0x1Au);
  EXPECT_EQ(result_png[7], 0x0Au);
}

TEST(MahoMcpScreenshotHandlerTest,
     CaptureFullPage_WithPasswordField_HasBlackRect) {
  constexpr int kPwX = 10;
  constexpr int kPwY = 50;
  constexpr int kPwW = 200;
  constexpr int kPwH = 30;

  SkBitmap bitmap = MakeTestBitmap(400, 200, SK_ColorWHITE);
  ui::AXTreeUpdate update =
      MakeTreeWithPasswordField(kPwX, kPwY, kPwW, kPwH);
  ui::AXTree tree(update);

  std::vector<uint8_t> result_png;
  MahoMcpScreenshotHandler::CaptureFullPage(
      bitmap, &tree,
      base::BindOnce(
          [](std::vector<uint8_t>* out, std::vector<uint8_t> png) {
            *out = std::move(png);
          },
          &result_png));

  ASSERT_GE(result_png.size(), 8u);
  EXPECT_EQ(result_png[0], 0x89u);

  // Verify redaction: directly check the bitmap after redaction.
  SkBitmap redacted = MakeTestBitmap(400, 200, SK_ColorWHITE);
  std::vector<gfx::Rect> rects =
      MahoMcpScreenshotHandler::FindPasswordFieldBounds(&tree);
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_EQ(rects[0].x(), kPwX);
  EXPECT_EQ(rects[0].y(), kPwY);
  EXPECT_EQ(rects[0].width(), kPwW);
  EXPECT_EQ(rects[0].height(), kPwH);

  MahoMcpScreenshotHandler::ApplyPasswordRedaction(redacted, rects);

  // Sample pixels inside the password field bounds — should be black.
  int center_x = kPwX + kPwW / 2;
  int center_y = kPwY + kPwH / 2;
  SkColor pixel = redacted.getColor(center_x, center_y);
  EXPECT_EQ(pixel, SK_ColorBLACK);

  // Sample pixel outside password field — should remain white.
  SkColor outside_pixel = redacted.getColor(0, 0);
  EXPECT_EQ(outside_pixel, SK_ColorWHITE);
}

TEST(MahoMcpScreenshotHandlerTest, CaptureElement_ByAxRef) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(3);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2, 3};
  update.nodes[0].relative_bounds.bounds =
      gfx::RectF(0, 0, 400, 300);

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kButton;
  update.nodes[1].SetName("Click Me");
  update.nodes[1].relative_bounds.bounds =
      gfx::RectF(50, 50, 100, 40);

  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kTextField;
  update.nodes[2].SetName("Email");
  update.nodes[2].relative_bounds.bounds =
      gfx::RectF(50, 100, 200, 30);

  ui::AXTree tree(update);

  SkBitmap bitmap = MakeTestBitmap(400, 300, SK_ColorBLUE);

  // Capture the button element (node ID 2).
  std::vector<uint8_t> result_png;
  MahoMcpScreenshotHandler::CaptureElement(
      bitmap, &tree, 2,
      base::BindOnce(
          [](std::vector<uint8_t>* out, std::vector<uint8_t> png) {
            *out = std::move(png);
          },
          &result_png));

  ASSERT_GE(result_png.size(), 8u);
  EXPECT_EQ(result_png[0], 0x89u);
  EXPECT_EQ(result_png[1], 0x50u);
  EXPECT_EQ(result_png[2], 0x4Eu);
  EXPECT_EQ(result_png[3], 0x47u);
}

TEST(MahoMcpScreenshotHandlerTest, FindPasswordFieldBounds_DetectsProtected) {
  ui::AXTreeUpdate update =
      MakeTreeWithPasswordField(20, 60, 180, 25);
  ui::AXTree tree(update);

  std::vector<gfx::Rect> rects =
      MahoMcpScreenshotHandler::FindPasswordFieldBounds(&tree);
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_EQ(rects[0].x(), 20);
  EXPECT_EQ(rects[0].y(), 60);
  EXPECT_EQ(rects[0].width(), 180);
  EXPECT_EQ(rects[0].height(), 25);
}

ui::AXTreeUpdate MakeTreeWithAutocompleteField(const char* autocomplete_token) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kTextField;
  update.nodes[1].SetName("Sensitive field");
  update.nodes[1].AddStringAttribute(
      ax::mojom::StringAttribute::kAutoComplete, autocomplete_token);
  update.nodes[1].relative_bounds.bounds = gfx::RectF(10, 10, 200, 30);

  return update;
}

TEST(MahoMcpScreenshotHandlerTest, FindBounds_OneTimeCodeAutocompleteDetected) {
  ui::AXTreeUpdate update = MakeTreeWithAutocompleteField("one-time-code");
  ui::AXTree tree(update);
  std::vector<gfx::Rect> rects =
      MahoMcpScreenshotHandler::FindPasswordFieldBounds(&tree);
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_EQ(rects[0].width(), 200);
}

TEST(MahoMcpScreenshotHandlerTest, FindBounds_CreditCardAutocompleteDetected) {
  ui::AXTreeUpdate update = MakeTreeWithAutocompleteField("cc-number");
  ui::AXTree tree(update);
  std::vector<gfx::Rect> rects =
      MahoMcpScreenshotHandler::FindPasswordFieldBounds(&tree);
  ASSERT_EQ(rects.size(), 1u);
}

TEST(MahoMcpScreenshotHandlerTest, FindBounds_OrdinarySearchNotDetected) {
  ui::AXTreeUpdate update = MakeTreeWithAutocompleteField("off");
  update.nodes[1].role = ax::mojom::Role::kSearchBox;
  ui::AXTree tree(update);
  std::vector<gfx::Rect> rects =
      MahoMcpScreenshotHandler::FindPasswordFieldBounds(&tree);
  EXPECT_EQ(rects.size(), 0u);
}

TEST(MahoMcpScreenshotHandlerTest, FindBounds_CustomRecoveryFieldDetected) {
  ui::AXTreeUpdate update = MakeTreeWithAutocompleteField("off");
  update.nodes[1].role = ax::mojom::Role::kGenericContainer;
  update.nodes[1].SetName("Recovery code");
  update.nodes[1].html_attributes.emplace_back("aria-label", "Recovery code");
  ui::AXTree tree(update);
  const std::vector<gfx::Rect> rects =
      MahoMcpScreenshotHandler::FindPasswordFieldBounds(&tree);
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_EQ(rects[0].x(), 10);
  EXPECT_EQ(rects[0].y(), 10);
  EXPECT_EQ(rects[0].width(), 200);
  EXPECT_EQ(rects[0].height(), 30);
}

TEST(MahoMcpScreenshotHandlerTest, EncodePng_EmptyBitmap_ReturnsEmpty) {
  SkBitmap empty;
  std::vector<uint8_t> result = MahoMcpScreenshotHandler::EncodePng(empty);
  EXPECT_TRUE(result.empty());
}

}  // namespace
}  // namespace maho
