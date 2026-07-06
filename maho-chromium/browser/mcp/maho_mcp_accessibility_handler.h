// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_ACCESSIBILITY_HANDLER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_ACCESSIBILITY_HANDLER_H_

#include <string>
#include <string_view>
#include <unordered_map>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"

namespace maho {

// Accessibility snapshot handler for the MCP server.
//
// Walks a ui::AXTree, serializes it to a JSON structure with @ref integer
// labels on interactive elements (Playwright-MCP parity). The ref table maps
// from ref integers to AXNodeIDs, enabling click/type/select by ref.
//
// Thread-safety: all methods must be called on the same sequence as the
// MahoMcpSession that owns the ref table.
class MahoMcpAccessibilityHandler {
 public:
  // Ref table type: maps @ref integer → AXNodeID.
  using RefTable = std::unordered_map<int, ui::AXNodeID>;

  // Default maximum serialized snapshot size in bytes.
  static constexpr size_t kDefaultMaxBytes = 128 * 1024;

  // Roles considered interactive (receive @ref labels).
  static bool IsInteractiveRole(ax::mojom::Role role);

  // Build a JSON snapshot of the accessibility tree. Assigns monotonically
  // increasing @ref labels to interactive elements and populates |out_refs|.
  //
  // Returns a base::Value dict representing the tree root. If the serialized
  // JSON exceeds |max_bytes|, the tree is truncated with a marker node.
  static base::Value BuildSnapshot(const ui::AXTree& tree,
                                   RefTable& out_refs,
                                   size_t max_bytes = kDefaultMaxBytes);

  // Resolve a @ref to an AXNodeID and simulate a click (default action).
  // Returns true if the ref was found and the action could be performed.
  static bool ClickByRef(ui::AXTree& tree,
                         const RefTable& refs,
                         int ref);

  // Resolve a @ref and simulate typing text into the element.
  // Returns true if the ref was found and the element is a text field.
  static bool TypeByRef(ui::AXTree& tree,
                        const RefTable& refs,
                        int ref,
                        std::string_view text);

  // Resolve a @ref and simulate selecting a value on a <select> element.
  // Returns true if the ref was found and the element supports selection.
  static bool SelectByRef(ui::AXTree& tree,
                          const RefTable& refs,
                          int ref,
                          std::string_view value);

 private:
  // Recursive DFS serialization helper.
  struct SerializeContext {
    raw_ptr<RefTable> refs;
    int next_ref = 1;
    size_t estimated_bytes = 0;
    size_t max_bytes;
    bool truncated = false;
  };

  static base::Value SerializeNode(const ui::AXNode* node,
                                   SerializeContext& ctx);

  static std::string RoleToString(ax::mojom::Role role);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_ACCESSIBILITY_HANDLER_H_
