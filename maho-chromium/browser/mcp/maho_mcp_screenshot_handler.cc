// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_screenshot_handler.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/strings/string_util.h"
#include "maho/browser/ai/maho_credential_redaction.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"

namespace maho {

// static
void MahoMcpScreenshotHandler::CaptureFullPage(
    const SkBitmap& bitmap,
    const ui::AXTree* ax_tree,
    base::OnceCallback<void(std::vector<uint8_t> png)> callback) {
  // Make a mutable copy for redaction.
  SkBitmap redacted_bitmap;
  redacted_bitmap.setInfo(bitmap.info());
  redacted_bitmap.allocPixels(bitmap.info());
  redacted_bitmap.writePixels(bitmap.pixmap());

  // Find password field bounding boxes and apply redaction overlay.
  if (ax_tree) {
    std::vector<gfx::Rect> password_rects = FindPasswordFieldBounds(ax_tree);
    ApplyPasswordRedaction(redacted_bitmap, password_rects);
  }

  // Encode to PNG.
  std::vector<uint8_t> png_bytes = EncodePng(redacted_bitmap);
  std::move(callback).Run(std::move(png_bytes));
}

// static
void MahoMcpScreenshotHandler::CaptureElement(
    const SkBitmap& bitmap,
    const ui::AXTree* ax_tree,
    ui::AXNodeID ax_id,
    base::OnceCallback<void(std::vector<uint8_t> png)> callback) {
  // Find the node's bounding box.
  gfx::Rect element_bounds;
  if (ax_tree) {
    const ui::AXNode* node = ax_tree->GetFromId(ax_id);
    if (node) {
      const ui::AXNodeData& data = node->data();
      element_bounds = gfx::Rect(
          static_cast<int>(data.relative_bounds.bounds.x()),
          static_cast<int>(data.relative_bounds.bounds.y()),
          static_cast<int>(data.relative_bounds.bounds.width()),
          static_cast<int>(data.relative_bounds.bounds.height()));
    }
  }

  // Clamp to bitmap bounds.
  gfx::Rect bitmap_rect(0, 0, bitmap.width(), bitmap.height());
  element_bounds.Intersect(bitmap_rect);

  if (element_bounds.IsEmpty()) {
    // Fallback: capture the entire bitmap if element bounds are invalid.
    CaptureFullPage(bitmap, ax_tree, std::move(callback));
    return;
  }

  // Extract the sub-region.
  SkBitmap cropped;
  SkIRect sk_rect = SkIRect::MakeXYWH(
      element_bounds.x(), element_bounds.y(),
      element_bounds.width(), element_bounds.height());
  bitmap.extractSubset(&cropped, sk_rect);

  // Deep copy so it owns its own pixels.
  SkBitmap owned_crop;
  owned_crop.setInfo(cropped.info());
  owned_crop.allocPixels(cropped.info());
  owned_crop.writePixels(cropped.pixmap());

  // Apply password redaction within the cropped region.
  if (ax_tree) {
    std::vector<gfx::Rect> password_rects = FindPasswordFieldBounds(ax_tree);
    // Translate rects to be relative to the cropped region.
    std::vector<gfx::Rect> translated_rects;
    for (const auto& rect : password_rects) {
      gfx::Rect translated = rect;
      translated.Offset(-element_bounds.x(), -element_bounds.y());
      gfx::Rect crop_bounds(0, 0, owned_crop.width(), owned_crop.height());
      translated.Intersect(crop_bounds);
      if (!translated.IsEmpty()) {
        translated_rects.push_back(translated);
      }
    }
    ApplyPasswordRedaction(owned_crop, translated_rects);
  }

  std::vector<uint8_t> png_bytes = EncodePng(owned_crop);
  std::move(callback).Run(std::move(png_bytes));
}

// static
std::vector<gfx::Rect> MahoMcpScreenshotHandler::FindPasswordFieldBounds(
    const ui::AXTree* ax_tree) {
  std::vector<gfx::Rect> rects;
  if (!ax_tree || !ax_tree->root()) {
    return rects;
  }

  // DFS walk of the accessibility tree.
  std::vector<const ui::AXNode*> stack;
  stack.push_back(ax_tree->root());

  while (!stack.empty()) {
    const ui::AXNode* node = stack.back();
    stack.pop_back();

    if (IsPasswordField(node)) {
      const ui::AXNodeData& data = node->data();
      gfx::Rect bounds(
          static_cast<int>(data.relative_bounds.bounds.x()),
          static_cast<int>(data.relative_bounds.bounds.y()),
          static_cast<int>(data.relative_bounds.bounds.width()),
          static_cast<int>(data.relative_bounds.bounds.height()));
      if (!bounds.IsEmpty()) {
        rects.push_back(bounds);
      }
    }

    // Push children in reverse order so left-to-right DFS order is preserved.
    for (size_t i = node->children().size(); i > 0; --i) {
      stack.push_back(node->children()[i - 1]);
    }
  }
  return rects;
}

// static
void MahoMcpScreenshotHandler::ApplyPasswordRedaction(
    SkBitmap& bitmap,
    const std::vector<gfx::Rect>& rects) {
  if (rects.empty()) {
    return;
  }

  SkCanvas canvas(bitmap);
  SkPaint paint;
  paint.setColor(SK_ColorBLACK);
  paint.setStyle(SkPaint::kFill_Style);

  for (const auto& rect : rects) {
    SkRect sk_rect = SkRect::MakeXYWH(
        static_cast<SkScalar>(rect.x()),
        static_cast<SkScalar>(rect.y()),
        static_cast<SkScalar>(rect.width()),
        static_cast<SkScalar>(rect.height()));
    canvas.drawRect(sk_rect, paint);
  }
}

// static
std::vector<uint8_t> MahoMcpScreenshotHandler::EncodePng(
    const SkBitmap& bitmap) {
  std::vector<uint8_t> png_bytes;
  if (bitmap.empty() || bitmap.isNull()) {
    return png_bytes;
  }

  // Ensure bitmap is in a format PNGCodec can handle.
  SkBitmap to_encode = bitmap;
  if (bitmap.colorType() != kN32_SkColorType) {
    SkBitmap converted;
    SkImageInfo info = SkImageInfo::MakeN32(bitmap.width(), bitmap.height(),
                                            kPremul_SkAlphaType);
    converted.allocPixels(info);
    bitmap.readPixels(info, converted.getPixels(), converted.rowBytes(), 0, 0);
    to_encode = converted;
  }

  auto encoded = gfx::PNGCodec::EncodeBGRASkBitmap(to_encode, /*discard_transparency=*/false);
  if (encoded) {
    png_bytes = std::move(*encoded);
  }
  return png_bytes;
}

// static
bool MahoMcpScreenshotHandler::IsPasswordField(const ui::AXNode* node) {
  if (!node) {
    return false;
  }
  const ui::AXNodeData& data = node->data();

  // Method 1: Check for kProtected state (most reliable — set on all
  // password fields regardless of platform).
  if (data.HasState(ax::mojom::State::kProtected)) {
    return true;
  }

  // Method 2: sensitive autocomplete token (one-time-code, cc-*, *password).
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kAutoComplete)) {
    std::string token =
        data.GetStringAttribute(ax::mojom::StringAttribute::kAutoComplete);
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (credential_redaction::IsSensitiveAutocomplete(token)) {
      return true;
    }
  }

  if (data.HasStringAttribute(ax::mojom::StringAttribute::kName) &&
      credential_redaction::IsCredentialName(
          data.GetStringAttribute(ax::mojom::StringAttribute::kName))) {
    return true;
  }

  for (const auto& attr : data.html_attributes) {
    if ((attr.first == "type" &&
         base::EqualsCaseInsensitiveASCII(attr.second, "password")) ||
        ((attr.first == "autocomplete" || attr.first == "aria-label" ||
          attr.first == "name" || attr.first == "id" ||
          attr.first == "placeholder" || attr.first == "data-maho-field") &&
         (credential_redaction::IsSensitiveAutocomplete(attr.second) ||
          credential_redaction::IsCredentialName(attr.second)))) {
      return true;
    }
  }

  return false;
}

}  // namespace maho
