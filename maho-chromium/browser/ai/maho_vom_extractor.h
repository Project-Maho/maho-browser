// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_AI_MAHO_VOM_EXTRACTOR_H_
#define MAHO_BROWSER_AI_MAHO_VOM_EXTRACTOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#if defined(MAHO_VOM_STANDALONE)
namespace gfx {
// Minimal stand-in matching Chromium's gfx::Rect accessor API (methods, not
// fields) so the extractor compiles identically in both modes.
struct Rect {
  int x_ = 0;
  int y_ = 0;
  int width_ = 0;
  int height_ = 0;
  int x() const { return x_; }
  int y() const { return y_; }
  int width() const { return width_; }
  int height() const { return height_; }
};
}  // namespace gfx
#else
#include "ui/gfx/geometry/rect.h"
#endif

namespace maho::ai {

// One candidate element for the visual-object model, gathered by the browser
// layer from the AX tree and layout. Pure data; no Chromium objects.
struct VomElementInput {
  VomElementInput();
  VomElementInput(const VomElementInput&);
  VomElementInput& operator=(const VomElementInput&);
  ~VomElementInput();

  uint64_t backend_node_id = 0;
  std::string role;
  std::string name;
  gfx::Rect bounds_css;
  bool is_actionable = false;
  bool is_hidden = false;
  bool is_occluded = false;
  bool draws_backdrop = false;
  int paint_order = 0;
  std::string hover_hint;
};

// A referenceable interactive element: @eN tag bound to its backend node id
// and viewport-relative CSS rect.
struct VomRef {
  uint64_t backend_node_id = 0;
  gfx::Rect rect;
  std::string tag;
};

// The serialized observation for one page (or one cursor page of it).
struct VomPage {
  VomPage();
  VomPage(const VomPage&);
  VomPage& operator=(const VomPage&);
  ~VomPage();

  std::string text;
  std::vector<VomRef> refs;
  std::optional<std::string> next_cursor;
};

// Native VOM core: turns element candidates into a compact @eN-tagged text
// observation with occlusion pruning and cursor pagination. Deterministic:
// the same inputs always produce byte-identical output.
class MahoVomExtractor {
 public:
  static constexpr uint32_t kDefaultMaxTokens = 4000;

  // |cursor| resumes a previous page: elements before the cursor's resume
  // index are excluded from tags, text, and counts. |max_tokens| caps the
  // emitted text at an approximate token budget (chars / 4).
  static VomPage Extract(const std::vector<VomElementInput>& elements,
                         const std::string& page_title,
                         const std::string& cursor,
                         uint32_t max_tokens = kDefaultMaxTokens);

  // Parses "<fnv1a64-hex>:<decimal index>" and returns the index, or nullopt
  // for malformed cursors.
  static std::optional<size_t> ResumeIndexFromCursor(const std::string& cursor);
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_VOM_EXTRACTOR_H_
