// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SCREENSHOT_HANDLER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SCREENSHOT_HANDLER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace maho {

enum class MahoMcpScreenshotMode {
  kFullPage,
  kElement,
  kViewport,
  kArea,
};

enum class MahoMcpScreenshotError {
  kNone,
  kInvalidMode,
  kMissingArea,
  kEmptyArea,
  kOutsideViewport,
  kInvalidViewport,
  kCaptureFailed,
};

struct MahoMcpScreenshotResult {
  std::string png_base64;
  std::optional<MahoMcpScreenshotError> error;
};

bool ValidateAndClampScreenshotArea(MahoMcpScreenshotMode mode,
                                    const gfx::Rect* requested_area,
                                    const gfx::Size& viewport_size,
                                    gfx::Rect* clamped_area,
                                    MahoMcpScreenshotError* error = nullptr);

bool IsValidScreenshotArea(const gfx::Rect& area);

bool IsScreenshotAreaWithinViewport(const gfx::Rect& area,
                                    const gfx::Size& viewport_size);

// Screenshot capture handler for the MCP server.
//
// Provides full-page and element-level screenshot capabilities with mandatory
// password field redaction (OQ-4). Before PNG encoding, solid black rectangles
// are drawn over any password input field bounding boxes detected in the
// accessibility tree.
//
// Thread-safety: static methods; thread requirements depend on caller context.
// When used with content::RenderWidgetHostView::CopyFromSurface, the callback
// executes on the UI thread.
class MahoMcpScreenshotHandler {
 public:
  // Captures full page as PNG bytes. Applies password redaction overlay
  // (solid black rects on password field bounding boxes) before encoding.
  //
  // In the stub/test path, operates on a provided SkBitmap + AXTree.
  // In the browser path, uses RenderWidgetHostView::CopyFromSurface.
  static void CaptureFullPage(
      const SkBitmap& bitmap,
      const ui::AXTree* ax_tree,
      base::OnceCallback<void(std::vector<uint8_t> png)> callback);

  static void CaptureViewport(
      const SkBitmap& bitmap,
      const ui::AXTree* ax_tree,
      base::OnceCallback<void(std::vector<uint8_t> png)> callback);

  static bool CaptureArea(
      const SkBitmap& bitmap,
      const ui::AXTree* ax_tree,
      const gfx::Rect& requested_area,
      base::OnceCallback<void(std::vector<uint8_t> png)> callback,
      bool bitmap_is_cropped = false,
      MahoMcpScreenshotError* error = nullptr);

  // Captures an element identified by AXNodeID. Crops the bitmap to the
  // element's bounding box before encoding.
  static void CaptureElement(
      const SkBitmap& bitmap,
      const ui::AXTree* ax_tree,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::vector<uint8_t> png)> callback);

  // Finds bounding boxes of all password fields in the accessibility tree.
  // Returns rects in page coordinates (pixels).
  static std::vector<gfx::Rect> FindPasswordFieldBounds(
      const ui::AXTree* ax_tree);

  // Applies password redaction overlay: draws solid black rectangles over
  // all password field bounding boxes on the bitmap. Modifies bitmap in place.
  static void ApplyPasswordRedaction(SkBitmap& bitmap,
                                     const std::vector<gfx::Rect>& rects);

  // Encodes a SkBitmap as PNG bytes.
  static std::vector<uint8_t> EncodePng(const SkBitmap& bitmap);

 private:
  // Returns true if the given AXNode represents a password input field.
  // Checks for ax::mojom::State::kProtected or html-input-type == "password".
  static bool IsPasswordField(const ui::AXNode* node);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SCREENSHOT_HANDLER_H_
