// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_ACCESSIBILITY_HANDLER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_ACCESSIBILITY_HANDLER_H_

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"

namespace maho {

// Accessibility snapshot handler for the MCP server.
//
// Walks a ui::AXTree, serializes it to a JSON structure with @ref integer
// labels on interactive elements (Playwright-MCP parity) or a compact/differential
// V2 textual snapshot. The ref table maps from ref integers to AXNodeIDs,
// enabling click/type/select by ref.
//
// Thread-safety: all methods must be called on the same sequence as the
// MahoMcpSession that owns the ref table.
class MahoMcpAccessibilityHandler {
 public:
  // Ref table type: maps @ref integer → AXNodeID.
  using RefTable = std::unordered_map<int, ui::AXNodeID>;

  // Default maximum serialized snapshot size in bytes.
  static constexpr size_t kDefaultMaxBytes = 128 * 1024;
  static constexpr size_t kV2DefaultMaxBytes = 24000;

  enum class SnapshotMode {
    kInteractive,
    kCompact,
    kFull,
  };

  struct SnapshotV2Options {
    SnapshotV2Options();
    ~SnapshotV2Options();
    SnapshotV2Options(const SnapshotV2Options&);
    SnapshotV2Options& operator=(const SnapshotV2Options&);
    SnapshotV2Options(SnapshotV2Options&&);
    SnapshotV2Options& operator=(SnapshotV2Options&&);

    SnapshotMode mode = SnapshotMode::kInteractive;
    bool include_hidden = false;
    std::optional<std::string> scope_selector;
    std::optional<int> scope_ref;
    std::optional<std::string> since_snapshot_token;
    size_t max_bytes = kV2DefaultMaxBytes;
    std::optional<int> max_depth;
  };

  struct NodeSummary {
    NodeSummary();
    ~NodeSummary();
    NodeSummary(const NodeSummary&);
    NodeSummary& operator=(const NodeSummary&);
    NodeSummary(NodeSummary&&);
    NodeSummary& operator=(NodeSummary&&);

    ui::AXNodeID ax_id = 0;
    std::string role;
    std::string name;
    std::string value;
    std::string states;
    std::optional<int> ref;
    std::string fingerprint;
    std::string rendered_line;
    int depth = 0;
  };

  struct CachedSnapshot {
    CachedSnapshot();
    ~CachedSnapshot();
    CachedSnapshot(const CachedSnapshot&);
    CachedSnapshot& operator=(const CachedSnapshot&);
    CachedSnapshot(CachedSnapshot&&);
    CachedSnapshot& operator=(CachedSnapshot&&);

    std::string token;
    int tab_id = 0;
    uint64_t navigation_epoch = 0;
    std::string frame_signature;
    base::Time timestamp;
    std::vector<NodeSummary> nodes;
    std::string tree_text;
    size_t total_bytes = 0;
  };

  // Session-scoped bounded observation cache for V2 snapshots.
  class ObservationCache {
   public:
    static constexpr size_t kMaxSnapshotsPerTab = 4;
    static constexpr size_t kMaxTotalBytes = 512 * 1024;

    ObservationCache();
    ~ObservationCache();
    ObservationCache(const ObservationCache&) = delete;
    ObservationCache& operator=(const ObservationCache&) = delete;

    void Store(CachedSnapshot snapshot);
    const CachedSnapshot* Find(int tab_id, std::string_view token) const;
    void ClearTab(int tab_id);
    void Clear();

   private:
    void EvictIfOverBudget();
    std::unordered_map<int, std::deque<CachedSnapshot>> tab_snapshots_;
    size_t current_total_bytes_ = 0;
  };

  struct SnapshotV2Result {
    SnapshotV2Result();
    ~SnapshotV2Result();
    SnapshotV2Result(const SnapshotV2Result&);
    SnapshotV2Result(SnapshotV2Result&&) noexcept;
    SnapshotV2Result& operator=(const SnapshotV2Result&);
    SnapshotV2Result& operator=(SnapshotV2Result&&) noexcept;

    std::string snapshot_token;
    struct TabMetadata {
      int id = 0;
      std::string url;
      std::string title;
    } tab;
    std::string tree;
    std::optional<std::string> diff;
    struct Stats {
      size_t captured_nodes = 0;
      size_t serialized_nodes = 0;
      size_t bytes = 0;
      bool truncated = false;
      std::optional<double> renderer_snapshot_ms;
      std::optional<double> ax_serialize_ms;
      std::optional<size_t> wire_bytes;
      std::optional<double> total_ms;
    } stats;
  };

  // Roles considered interactive (receive @ref labels).
  static bool IsInteractiveRole(ax::mojom::Role role);

  // Semantic ancestor roles that change or provide structural context.
  static bool IsSemanticAncestorRole(ax::mojom::Role role);

  // Structural roles for compact document mode.
  static bool IsDocumentStructureRole(ax::mojom::Role role);

  // Check if a node is invisible, ignored, or aria-hidden.
  static bool IsNodeHidden(const ui::AXNode* node);

  // Build a JSON snapshot of the accessibility tree. Assigns monotonically
  // increasing @ref labels to interactive elements and populates |out_refs|.
  //
  // Returns a base::Value dict representing the tree root. If the serialized
  // JSON exceeds |max_bytes|, the tree is truncated with a marker node.
  static base::Value BuildSnapshot(const ui::AXTree& tree,
                                   RefTable& out_refs,
                                   size_t max_bytes = kDefaultMaxBytes);

  // Build a compact, scoped, or differential textual snapshot (V2).
  static SnapshotV2Result BuildSnapshotV2(
      const ui::AXTree& tree,
      const SnapshotV2Options& options,
      RefTable& out_refs,
      const SnapshotV2Result::TabMetadata& tab_metadata,
      uint64_t navigation_epoch,
      const std::string& frame_signature,
      ObservationCache& observation_cache,
      uint64_t token_sequence = 0);

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

  // Diff computation helpers.
  static std::string ComputeDiff(
      const std::vector<NodeSummary>& previous_nodes,
      const std::vector<NodeSummary>& current_nodes);

  static std::string ComputeFullResetDiff(
      const std::vector<NodeSummary>& current_nodes);

  static std::string RoleToString(ax::mojom::Role role);

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
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_ACCESSIBILITY_HANDLER_H_
