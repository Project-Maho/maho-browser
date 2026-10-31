// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "maho/browser/ai/maho_credential_redaction.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"

namespace maho {

MahoMcpAccessibilityHandler::SnapshotV2Options::SnapshotV2Options() = default;
MahoMcpAccessibilityHandler::SnapshotV2Options::~SnapshotV2Options() = default;
MahoMcpAccessibilityHandler::SnapshotV2Options::SnapshotV2Options(
    const SnapshotV2Options&) = default;
MahoMcpAccessibilityHandler::SnapshotV2Options&
MahoMcpAccessibilityHandler::SnapshotV2Options::operator=(
    const SnapshotV2Options&) = default;
MahoMcpAccessibilityHandler::SnapshotV2Options::SnapshotV2Options(
    SnapshotV2Options&&) = default;
MahoMcpAccessibilityHandler::SnapshotV2Options&
MahoMcpAccessibilityHandler::SnapshotV2Options::operator=(
    SnapshotV2Options&&) = default;

MahoMcpAccessibilityHandler::NodeSummary::NodeSummary() = default;
MahoMcpAccessibilityHandler::NodeSummary::~NodeSummary() = default;
MahoMcpAccessibilityHandler::NodeSummary::NodeSummary(const NodeSummary&) =
    default;
MahoMcpAccessibilityHandler::NodeSummary&
MahoMcpAccessibilityHandler::NodeSummary::operator=(const NodeSummary&) =
    default;
MahoMcpAccessibilityHandler::NodeSummary::NodeSummary(NodeSummary&&) = default;
MahoMcpAccessibilityHandler::NodeSummary&
MahoMcpAccessibilityHandler::NodeSummary::operator=(NodeSummary&&) = default;

MahoMcpAccessibilityHandler::CachedSnapshot::CachedSnapshot() = default;
MahoMcpAccessibilityHandler::CachedSnapshot::~CachedSnapshot() = default;
MahoMcpAccessibilityHandler::CachedSnapshot::CachedSnapshot(
    const CachedSnapshot&) = default;
MahoMcpAccessibilityHandler::CachedSnapshot&
MahoMcpAccessibilityHandler::CachedSnapshot::operator=(
    const CachedSnapshot&) = default;
MahoMcpAccessibilityHandler::CachedSnapshot::CachedSnapshot(CachedSnapshot&&) =
    default;
MahoMcpAccessibilityHandler::CachedSnapshot&
MahoMcpAccessibilityHandler::CachedSnapshot::operator=(CachedSnapshot&&) =
    default;

namespace {

// Approximate byte overhead per JSON node (braces, commas, field names).
constexpr size_t kNodeOverheadBytes = 64;

std::string EscapeQuotes(std::string_view str) {
  std::string out;
  out.reserve(str.size());
  for (char c : str) {
    if (c == '"') {
      out.append("\\\"");
    } else if (c == '\\') {
      out.append("\\\\");
    } else if (c == '\n') {
      out.append("\\n");
    } else if (c == '\r') {
      // omit carriage return
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool HasCredentialSignal(const ui::AXNodeData& data) {
  if (data.HasState(ax::mojom::State::kProtected)) {
    return true;
  }
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kAutoComplete)) {
    const std::string& autocomplete =
        data.GetStringAttribute(ax::mojom::StringAttribute::kAutoComplete);
    if (credential_redaction::IsSensitiveAutocomplete(autocomplete)) {
      return true;
    }
  }
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kName)) {
    const std::string& name =
        data.GetStringAttribute(ax::mojom::StringAttribute::kName);
    if (credential_redaction::IsCredentialName(name)) {
      return true;
    }
  }
  for (const auto& attr : data.html_attributes) {
    if (attr.first == "type" && attr.second == "password") {
      return true;
    }
    if (attr.first == "autocomplete" &&
        credential_redaction::IsSensitiveAutocomplete(attr.second)) {
      return true;
    }
    if (credential_redaction::IsCredentialName(attr.second)) {
      return true;
    }
  }
  return false;
}

struct IntermediateNode {
  raw_ptr<const ui::AXNode> ax_node;
  std::string role_str;
  std::string name;
  std::string value;
  std::string states_str;
  std::optional<int> ref;
  std::string fingerprint;
  bool is_interactive = false;
  bool is_semantic_ancestor = false;
  std::vector<IntermediateNode> children;
};

// Hidden-ness has two different consequences for a snapshot. A genuinely
// invisible subtree (State::kInvisible, aria-hidden, hidden attribute) is
// pruned whole. An *ignored-but-present* container is never emitted itself,
// but its descendants still belong to the snapshot: Blink deliberately keeps
// the <html> element (and similar wrappers) in the tree as ignored-but-
// included (ui::AXNodeData::IsIgnored() == State::kIgnored || Role::kNone).
// Pruning the subtree at such a wrapper severed every document at <html>,
// collapsing real-page snapshots to a single serialized node.
bool IsInvisibleSubtree(const ui::AXNodeData& data) {
  if (data.IsInvisible()) {
    return true;
  }
  for (const auto& attr : data.html_attributes) {
    if (attr.first == "aria-hidden" && attr.second == "true") {
      return true;
    }
    if (attr.first == "hidden") {
      return true;
    }
  }
  return false;
}

// Ignored wrappers are traversed but never emitted; the keep/flatten decision
// splices their descendants up into the parent.
bool IsIgnoredButTraversable(const ui::AXNodeData& data) {
  return data.IsIgnored();
}

void FilterSubtreeRecursive(
    const ui::AXNode* node,
    int current_depth,
    const MahoMcpAccessibilityHandler::SnapshotV2Options& options,
    MahoMcpAccessibilityHandler::RefTable& refs,
    int& next_ref,
    size_t& captured_count,
    std::vector<IntermediateNode>& out_nodes) {
  if (!node) {
    return;
  }
  ++captured_count;

  bool traversable_only = false;
  if (!options.include_hidden) {
    const ui::AXNodeData& node_data = node->data();
    if (IsInvisibleSubtree(node_data)) {
      return;
    }
    traversable_only = IsIgnoredButTraversable(node_data);
  }

  if (options.max_depth.has_value() && current_depth > *options.max_depth) {
    return;
  }

  std::vector<IntermediateNode> collected_children;
  for (size_t i = 0; i < node->GetChildCount(); ++i) {
    const ui::AXNode* child = node->GetChildAtIndex(i);
    if (child) {
      FilterSubtreeRecursive(child, current_depth + 1, options, refs, next_ref,
                             captured_count, collected_children);
    }
  }

  const ui::AXNodeData& data = node->data();
  const ax::mojom::Role role = data.role;
  std::string role_str = MahoMcpAccessibilityHandler::RoleToString(role);

  std::string name;
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kName)) {
    name = data.GetStringAttribute(ax::mojom::StringAttribute::kName);
  }

  std::string value;
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kValue)) {
    value = data.GetStringAttribute(ax::mojom::StringAttribute::kValue);
  }

  // Redaction for credential signals
  if (HasCredentialSignal(data)) {
    if (!value.empty()) {
      value = MahoMcpFirewall::kRedacted;
    }
  } else if (!value.empty()) {
    value = credential_redaction::RedactCredentialText(value);
  }
  if (!name.empty()) {
    name = credential_redaction::RedactCredentialText(name);
  }

  std::string dom_id;
  std::string dom_class;
  std::string input_type;
  std::string href_url;
  for (const auto& attr : data.html_attributes) {
    if (attr.first == "id") {
      dom_id = attr.second;
    } else if (attr.first == "class") {
      dom_class = attr.second;
    } else if (attr.first == "type") {
      input_type = attr.second;
    } else if (attr.first == "href") {
      href_url = MahoMcpFirewall::RedactUrl(attr.second);
    }
  }

  std::vector<std::string> state_parts;
  if (data.HasCheckedState()) {
    ax::mojom::CheckedState checked = data.GetCheckedState();
    if (checked == ax::mojom::CheckedState::kTrue) {
      state_parts.push_back("checked=true");
    } else if (checked == ax::mojom::CheckedState::kFalse) {
      state_parts.push_back("checked=false");
    } else if (checked == ax::mojom::CheckedState::kMixed) {
      state_parts.push_back("checked=mixed");
    }
  }
  if (data.HasState(ax::mojom::State::kExpanded)) {
    state_parts.push_back("expanded=true");
  } else if (data.HasState(ax::mojom::State::kCollapsed)) {
    state_parts.push_back("expanded=false");
  }
  if (data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected)) {
    state_parts.push_back("selected=true");
  }
  if (data.GetRestriction() == ax::mojom::Restriction::kDisabled) {
    state_parts.push_back("disabled=true");
  }
  if (data.HasIntAttribute(ax::mojom::IntAttribute::kAriaCurrentState)) {
    const auto cur = static_cast<ax::mojom::AriaCurrentState>(
        data.GetIntAttribute(ax::mojom::IntAttribute::kAriaCurrentState));
    if (cur != ax::mojom::AriaCurrentState::kNone &&
        cur != ax::mojom::AriaCurrentState::kFalse) {
      state_parts.push_back("current=true");
    }
  }
  if (data.HasIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel)) {
    int level = data.GetIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel);
    if (level > 0) {
      state_parts.push_back("level=" + std::to_string(level));
    }
  }
  if (!value.empty() && role != ax::mojom::Role::kStaticText) {
    state_parts.push_back("value=\"" + EscapeQuotes(value) + "\"");
  }
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kPlaceholder)) {
    const std::string& ph =
        data.GetStringAttribute(ax::mojom::StringAttribute::kPlaceholder);
    if (!ph.empty()) {
      state_parts.push_back("placeholder=\"" + EscapeQuotes(ph) + "\"");
    }
  }

  std::string states_str;
  if (!state_parts.empty()) {
    states_str = "[" + base::JoinString(state_parts, ", ") + "]";
  }

  bool is_interactive = MahoMcpAccessibilityHandler::IsInteractiveRole(role);
  bool is_semantic_ancestor =
      MahoMcpAccessibilityHandler::IsSemanticAncestorRole(role) ||
      role == ax::mojom::Role::kRootWebArea;

  std::optional<int> assigned_ref;
  if (is_interactive) {
    int ref = next_ref++;
    refs[ref] = data.id;
    assigned_ref = ref;
  }

  std::string fingerprint =
      role_str + "|" + name + "|" + dom_id + "|" + dom_class + "|" + input_type;

  // Decide whether to keep or flatten
  bool keep = false;
  if (traversable_only) {
    // Ignored wrapper: never emitted; children flatten up to the parent.
    keep = false;
  } else if (options.mode == MahoMcpAccessibilityHandler::SnapshotMode::kFull) {
    keep = true;
  } else if (options.mode ==
             MahoMcpAccessibilityHandler::SnapshotMode::kCompact) {
    if (is_interactive || is_semantic_ancestor ||
        MahoMcpAccessibilityHandler::IsDocumentStructureRole(role) ||
        (role == ax::mojom::Role::kStaticText && !name.empty())) {
      keep = true;
    }
  } else {
    // kInteractive mode
    if (is_interactive) {
      keep = true;
    } else if (is_semantic_ancestor && (!collected_children.empty() || !name.empty())) {
      keep = true;
    } else if ((role == ax::mojom::Role::kStaticText ||
                role == ax::mojom::Role::kLabelText) &&
               !name.empty()) {
      keep = true;
    }
  }

  if (keep) {
    IntermediateNode item;
    item.ax_node = node;
    item.role_str = std::move(role_str);
    item.name = std::move(name);
    item.value = std::move(value);
    item.states_str = std::move(states_str);
    item.ref = assigned_ref;
    item.fingerprint = std::move(fingerprint);
    item.is_interactive = is_interactive;
    item.is_semantic_ancestor = is_semantic_ancestor;
    item.children = std::move(collected_children);
    out_nodes.push_back(std::move(item));
  } else {
    // Flatten children up to parent
    for (auto& child : collected_children) {
      out_nodes.push_back(std::move(child));
    }
  }
}

void RenderIntermediateNodes(
    const std::vector<IntermediateNode>& nodes,
    int depth,
    size_t max_bytes,
    std::string& out_text,
    std::vector<MahoMcpAccessibilityHandler::NodeSummary>& out_summaries,
    bool& truncated) {
  for (const auto& node : nodes) {
    if (truncated) {
      return;
    }
    std::string line = node.role_str;
    if (node.ref.has_value()) {
      line += " @e" + std::to_string(*node.ref);
    }
    if (!node.name.empty()) {
      line += " \"" + EscapeQuotes(node.name) + "\"";
    }
    if (!node.states_str.empty()) {
      line += " " + node.states_str;
    }

    std::string indent(depth * 2, ' ');
    std::string full_line = indent + line + "\n";

    if (out_text.size() + full_line.size() > max_bytes) {
      truncated = true;
      std::string trunc_indent((depth + 1) * 2, ' ');
      out_text += trunc_indent + "[TRUNCATED — snapshot too large]\n";
      return;
    }

    out_text += full_line;

    MahoMcpAccessibilityHandler::NodeSummary summary;
    summary.ax_id = node.ax_node ? node.ax_node->id() : 0;
    summary.role = node.role_str;
    summary.name = node.name;
    summary.value = node.value;
    summary.states = node.states_str;
    summary.ref = node.ref;
    summary.fingerprint = node.fingerprint;
    summary.rendered_line = line;
    summary.depth = depth;
    out_summaries.push_back(std::move(summary));

    RenderIntermediateNodes(node.children, depth + 1, max_bytes, out_text,
                            out_summaries, truncated);
  }
}

const ui::AXNode* FindScopedRoot(
    const ui::AXTree& tree,
    const MahoMcpAccessibilityHandler::SnapshotV2Options& options,
    const MahoMcpAccessibilityHandler::RefTable& refs) {
  if (options.scope_ref.has_value()) {
    auto it = refs.find(*options.scope_ref);
    if (it != refs.end()) {
      const ui::AXNode* scoped = tree.GetFromId(it->second);
      if (scoped) {
        return scoped;
      }
    }
  }

  if (options.scope_selector.has_value() && !options.scope_selector->empty()) {
    const std::string& sel = *options.scope_selector;
    std::vector<const ui::AXNode*> stack;
    if (tree.root()) {
      stack.push_back(tree.root());
    }
    while (!stack.empty()) {
      const ui::AXNode* curr = stack.back();
      stack.pop_back();
      const ui::AXNodeData& data = curr->data();

      // Selector checks: [role=...], #id, tag
      if (sel.rfind("[role=", 0) == 0 && sel.back() == ']') {
        std::string target_role = sel.substr(6, sel.size() - 7);
        if (base::EqualsCaseInsensitiveASCII(
                MahoMcpAccessibilityHandler::RoleToString(data.role),
                target_role)) {
          return curr;
        }
      } else if (sel.rfind("#", 0) == 0) {
        std::string target_id = sel.substr(1);
        for (const auto& attr : data.html_attributes) {
          if (attr.first == "id" && attr.second == target_id) {
            return curr;
          }
        }
      }

      for (size_t i = 0; i < curr->GetChildCount(); ++i) {
        if (curr->GetChildAtIndex(i)) {
          stack.push_back(curr->GetChildAtIndex(i));
        }
      }
    }
  }

  return tree.root();
}

}  // namespace

// ObservationCache
MahoMcpAccessibilityHandler::ObservationCache::ObservationCache() = default;
MahoMcpAccessibilityHandler::ObservationCache::~ObservationCache() = default;

void MahoMcpAccessibilityHandler::ObservationCache::Store(CachedSnapshot snapshot) {
  int tab_id = snapshot.tab_id;
  current_total_bytes_ += snapshot.total_bytes;
  auto& deque = tab_snapshots_[tab_id];
  deque.push_back(std::move(snapshot));
  while (deque.size() > kMaxSnapshotsPerTab) {
    current_total_bytes_ -= deque.front().total_bytes;
    deque.pop_front();
  }
  EvictIfOverBudget();
}

const MahoMcpAccessibilityHandler::CachedSnapshot*
MahoMcpAccessibilityHandler::ObservationCache::Find(
    int tab_id,
    std::string_view token) const {
  auto it = tab_snapshots_.find(tab_id);
  if (it == tab_snapshots_.end()) {
    return nullptr;
  }
  for (const auto& snap : it->second) {
    if (snap.token == token) {
      return &snap;
    }
  }
  return nullptr;
}

void MahoMcpAccessibilityHandler::ObservationCache::ClearTab(int tab_id) {
  auto it = tab_snapshots_.find(tab_id);
  if (it != tab_snapshots_.end()) {
    for (const auto& snap : it->second) {
      current_total_bytes_ -= snap.total_bytes;
    }
    tab_snapshots_.erase(it);
  }
}

void MahoMcpAccessibilityHandler::ObservationCache::Clear() {
  tab_snapshots_.clear();
  current_total_bytes_ = 0;
}

void MahoMcpAccessibilityHandler::ObservationCache::EvictIfOverBudget() {
  while (current_total_bytes_ > kMaxTotalBytes && !tab_snapshots_.empty()) {
    auto oldest_tab_it = tab_snapshots_.begin();
    if (!oldest_tab_it->second.empty()) {
      current_total_bytes_ -= oldest_tab_it->second.front().total_bytes;
      oldest_tab_it->second.pop_front();
    }
    if (oldest_tab_it->second.empty()) {
      tab_snapshots_.erase(oldest_tab_it);
    }
  }
}

// SnapshotV2Result
MahoMcpAccessibilityHandler::SnapshotV2Result::SnapshotV2Result() = default;
MahoMcpAccessibilityHandler::SnapshotV2Result::~SnapshotV2Result() = default;
MahoMcpAccessibilityHandler::SnapshotV2Result::SnapshotV2Result(
    const SnapshotV2Result&) = default;
MahoMcpAccessibilityHandler::SnapshotV2Result::SnapshotV2Result(
    SnapshotV2Result&&) noexcept = default;
MahoMcpAccessibilityHandler::SnapshotV2Result&
MahoMcpAccessibilityHandler::SnapshotV2Result::operator=(
    const SnapshotV2Result&) = default;
MahoMcpAccessibilityHandler::SnapshotV2Result&
MahoMcpAccessibilityHandler::SnapshotV2Result::operator=(
    SnapshotV2Result&&) noexcept = default;

// static
bool MahoMcpAccessibilityHandler::IsInteractiveRole(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kButton:
    case ax::mojom::Role::kLink:
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
    case ax::mojom::Role::kCheckBox:
    case ax::mojom::Role::kRadioButton:
    case ax::mojom::Role::kComboBoxGrouping:
    case ax::mojom::Role::kComboBoxMenuButton:
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuItem:
    case ax::mojom::Role::kMenuItemCheckBox:
    case ax::mojom::Role::kMenuItemRadio:
    case ax::mojom::Role::kSlider:
    case ax::mojom::Role::kSpinButton:
    case ax::mojom::Role::kSwitch:
    case ax::mojom::Role::kTab:
    case ax::mojom::Role::kTree:
    case ax::mojom::Role::kTreeItem:
    case ax::mojom::Role::kSearchBox:
      return true;
    default:
      return false;
  }
}

// static
bool MahoMcpAccessibilityHandler::IsSemanticAncestorRole(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kRootWebArea:
    case ax::mojom::Role::kDialog:
    case ax::mojom::Role::kAlertDialog:
    case ax::mojom::Role::kForm:
    case ax::mojom::Role::kNavigation:
    case ax::mojom::Role::kMain:
    case ax::mojom::Role::kRegion:
    case ax::mojom::Role::kList:
    case ax::mojom::Role::kListBox:
    case ax::mojom::Role::kTable:
    case ax::mojom::Role::kGrid:
    case ax::mojom::Role::kTreeGrid:
    case ax::mojom::Role::kRow:
    case ax::mojom::Role::kRowGroup:
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kGridCell:
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kRowHeader:
    case ax::mojom::Role::kArticle:
    case ax::mojom::Role::kSection:
    case ax::mojom::Role::kBanner:
    case ax::mojom::Role::kContentInfo:
    case ax::mojom::Role::kComplementary:
    case ax::mojom::Role::kTabList:
    case ax::mojom::Role::kTabPanel:
    case ax::mojom::Role::kToolbar:
    case ax::mojom::Role::kMenu:
    case ax::mojom::Role::kMenuBar:
      return true;
    default:
      return false;
  }
}

// static
bool MahoMcpAccessibilityHandler::IsDocumentStructureRole(ax::mojom::Role role) {
  if (IsSemanticAncestorRole(role)) {
    return true;
  }
  switch (role) {
    case ax::mojom::Role::kHeading:
    case ax::mojom::Role::kParagraph:
    case ax::mojom::Role::kListItem:
    case ax::mojom::Role::kImage:
    case ax::mojom::Role::kStaticText:
    case ax::mojom::Role::kLabelText:
      return true;
    default:
      return false;
  }
}

// static
bool MahoMcpAccessibilityHandler::IsNodeHidden(const ui::AXNode* node) {
  if (!node) {
    return false;
  }
  const ui::AXNodeData& data = node->data();
  if (data.IsInvisibleOrIgnored()) {
    return true;
  }
  if (data.HasState(ax::mojom::State::kInvisible) ||
      data.HasState(ax::mojom::State::kIgnored) ||
      data.role == ax::mojom::Role::kNone) {
    return true;
  }
  for (const auto& attr : data.html_attributes) {
    if (attr.first == "aria-hidden" && attr.second == "true") {
      return true;
    }
    if (attr.first == "hidden") {
      return true;
    }
  }
  return false;
}

// static
std::string MahoMcpAccessibilityHandler::RoleToString(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kRootWebArea:
      return "WebArea";
    case ax::mojom::Role::kButton:
      return "button";
    case ax::mojom::Role::kLink:
      return "link";
    case ax::mojom::Role::kTextField:
      return "textbox";
    case ax::mojom::Role::kTextFieldWithComboBox:
      return "combobox";
    case ax::mojom::Role::kCheckBox:
      return "checkbox";
    case ax::mojom::Role::kRadioButton:
      return "radio";
    case ax::mojom::Role::kComboBoxGrouping:
      return "combobox";
    case ax::mojom::Role::kComboBoxMenuButton:
      return "combobox";
    case ax::mojom::Role::kComboBoxSelect:
      return "combobox";
    case ax::mojom::Role::kListBox:
      return "listbox";
    case ax::mojom::Role::kMenu:
      return "menu";
    case ax::mojom::Role::kMenuItem:
      return "menuitem";
    case ax::mojom::Role::kMenuItemCheckBox:
      return "menuitem";
    case ax::mojom::Role::kMenuItemRadio:
      return "menuitem";
    case ax::mojom::Role::kSlider:
      return "slider";
    case ax::mojom::Role::kSpinButton:
      return "spinbutton";
    case ax::mojom::Role::kSwitch:
      return "switch";
    case ax::mojom::Role::kTab:
      return "tab";
    case ax::mojom::Role::kTree:
      return "tree";
    case ax::mojom::Role::kTreeItem:
      return "treeitem";
    case ax::mojom::Role::kSearchBox:
      return "searchbox";
    case ax::mojom::Role::kHeading:
      return "heading";
    case ax::mojom::Role::kParagraph:
      return "paragraph";
    case ax::mojom::Role::kImage:
      return "image";
    case ax::mojom::Role::kStaticText:
      return "staticText";
    case ax::mojom::Role::kGroup:
      return "group";
    case ax::mojom::Role::kRegion:
      return "region";
    case ax::mojom::Role::kGenericContainer:
      return "generic";
    case ax::mojom::Role::kList:
      return "list";
    case ax::mojom::Role::kListItem:
      return "listitem";
    case ax::mojom::Role::kTable:
      return "table";
    case ax::mojom::Role::kRow:
      return "row";
    case ax::mojom::Role::kCell:
      return "cell";
    case ax::mojom::Role::kNavigation:
      return "navigation";
    case ax::mojom::Role::kMain:
      return "main";
    case ax::mojom::Role::kBanner:
      return "banner";
    case ax::mojom::Role::kContentInfo:
      return "contentinfo";
    case ax::mojom::Role::kForm:
      return "form";
    case ax::mojom::Role::kDialog:
      return "dialog";
    case ax::mojom::Role::kAlertDialog:
      return "alertdialog";
    case ax::mojom::Role::kArticle:
      return "article";
    case ax::mojom::Role::kSection:
      return "section";
    case ax::mojom::Role::kComplementary:
      return "complementary";
    case ax::mojom::Role::kTabList:
      return "tablist";
    case ax::mojom::Role::kTabPanel:
      return "tabpanel";
    case ax::mojom::Role::kToolbar:
      return "toolbar";
    case ax::mojom::Role::kMenuBar:
      return "menubar";
    case ax::mojom::Role::kGrid:
      return "grid";
    case ax::mojom::Role::kTreeGrid:
      return "treegrid";
    case ax::mojom::Role::kRowGroup:
      return "rowgroup";
    case ax::mojom::Role::kGridCell:
      return "gridcell";
    case ax::mojom::Role::kColumnHeader:
      return "columnheader";
    case ax::mojom::Role::kRowHeader:
      return "rowheader";
    default:
      return "generic";
  }
}

// static
base::Value MahoMcpAccessibilityHandler::SerializeNode(
    const ui::AXNode* node,
    SerializeContext& ctx) {
  if (ctx.truncated) {
    return base::Value();
  }

  const ui::AXNodeData& data = node->data();
  base::DictValue dict;

  // Role (always present).
  std::string role_str = RoleToString(data.role);
  dict.Set("role", role_str);

  // Name (omit if empty).
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kName)) {
    const std::string& name =
        data.GetStringAttribute(ax::mojom::StringAttribute::kName);
    if (!name.empty()) {
      dict.Set("name", name);
    }
  }

  // Value (omit if empty).
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kValue)) {
    const std::string& value =
        data.GetStringAttribute(ax::mojom::StringAttribute::kValue);
    if (!value.empty()) {
      dict.Set("value", value);
    }
  }

  // Browser-trusted credential signals; MahoMcpFirewall redacts values on these.
  if (data.HasState(ax::mojom::State::kProtected)) {
    dict.Set("protected", true);
  }
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kAutoComplete)) {
    const std::string& autocomplete =
        data.GetStringAttribute(ax::mojom::StringAttribute::kAutoComplete);
    if (!autocomplete.empty()) {
      dict.Set("autocomplete", autocomplete);
    }
  }
  if (data.HasStringAttribute(ax::mojom::StringAttribute::kHtmlId)) {
    const std::string& html_id =
        data.GetStringAttribute(ax::mojom::StringAttribute::kHtmlId);
    if (!html_id.empty()) {
      dict.Set("elementId", html_id);
    }
  }
  for (const auto& attr : data.html_attributes) {
    if (attr.second.empty()) {
      continue;
    }
    if (attr.first == "type") {
      dict.Set("inputType", attr.second);
    } else if (attr.first == "aria-label") {
      dict.Set("ariaLabel", attr.second);
    } else if (attr.first == "name") {
      dict.Set("fieldName", attr.second);
    } else if (attr.first == "id") {
      dict.Set("elementId", attr.second);
    } else if (attr.first == "placeholder") {
      dict.Set("placeholder", attr.second);
    } else if (attr.first == "data-maho-field") {
      dict.Set("dataField", attr.second);
    }
  }

  // Checked state (omit if not applicable).
  if (data.HasCheckedState()) {
    ax::mojom::CheckedState checked = data.GetCheckedState();
    if (checked == ax::mojom::CheckedState::kTrue) {
      dict.Set("checked", true);
    } else if (checked == ax::mojom::CheckedState::kFalse) {
      dict.Set("checked", false);
    } else if (checked == ax::mojom::CheckedState::kMixed) {
      dict.Set("checked", "mixed");
    }
  }

  // Expanded state (omit if not applicable).
  if (data.HasState(ax::mojom::State::kExpanded)) {
    dict.Set("expanded", true);
  } else if (data.HasState(ax::mojom::State::kCollapsed)) {
    dict.Set("expanded", false);
  }

  // Hierarchical level (omit if 0/not set).
  if (data.HasIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel)) {
    int level = data.GetIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel);
    if (level > 0) {
      dict.Set("level", level);
    }
  }

  // Assign @ref to interactive elements.
  if (IsInteractiveRole(data.role)) {
    int ref = ctx.next_ref++;
    dict.Set("ref", ref);
    (*ctx.refs)[ref] = data.id;
  }

  // Estimate size contribution.
  ctx.estimated_bytes += kNodeOverheadBytes + role_str.size();

  // Children.
  base::ListValue children;
  for (size_t i = 0; i < node->GetChildCount(); ++i) {
    if (ctx.estimated_bytes >= ctx.max_bytes) {
      ctx.truncated = true;
      base::DictValue truncation_marker;
      truncation_marker.Set("role", "generic");
      truncation_marker.Set("name", "[TRUNCATED — snapshot too large]");
      children.Append(base::Value(std::move(truncation_marker)));
      break;
    }
    ui::AXNode* child = node->GetChildAtIndex(i);
    if (child) {
      base::Value child_val = SerializeNode(child, ctx);
      if (!child_val.is_none()) {
        children.Append(std::move(child_val));
      }
    }
  }

  if (!children.empty()) {
    dict.Set("children", std::move(children));
  }

  return base::Value(std::move(dict));
}

// static
base::Value MahoMcpAccessibilityHandler::BuildSnapshot(
    const ui::AXTree& tree,
    RefTable& out_refs,
    size_t max_bytes) {
  out_refs.clear();

  const ui::AXNode* root = tree.root();
  if (!root) {
    // Empty tree — return minimal WebArea.
    base::DictValue empty;
    empty.Set("role", "WebArea");
    empty.Set("children", base::ListValue());
    return base::Value(std::move(empty));
  }

  SerializeContext ctx{&out_refs, /*next_ref=*/1, /*estimated_bytes=*/0,
                       max_bytes, /*truncated=*/false};
  return SerializeNode(root, ctx);
}

// static
MahoMcpAccessibilityHandler::SnapshotV2Result
MahoMcpAccessibilityHandler::BuildSnapshotV2(
    const ui::AXTree& tree,
    const SnapshotV2Options& options,
    RefTable& out_refs,
    const SnapshotV2Result::TabMetadata& tab_metadata,
    uint64_t navigation_epoch,
    const std::string& frame_signature,
    ObservationCache& observation_cache,
    uint64_t token_sequence) {
  out_refs.clear();

  SnapshotV2Result result;
  result.tab = tab_metadata;
  std::string snapshot_token = "s_" + std::to_string(tab_metadata.id) + "_" +
                               std::to_string(token_sequence);
  result.snapshot_token = snapshot_token;

  const ui::AXNode* scoped_root = FindScopedRoot(tree, options, out_refs);
  if (!scoped_root) {
    result.tree = "WebArea\n";
    result.stats.captured_nodes = 0;
    result.stats.serialized_nodes = 0;
    result.stats.bytes = result.tree.size();
    result.stats.truncated = false;
    return result;
  }

  int next_ref = 1;
  size_t captured_count = 0;
  std::vector<IntermediateNode> filtered_nodes;
  FilterSubtreeRecursive(scoped_root, /*current_depth=*/0, options, out_refs,
                         next_ref, captured_count, filtered_nodes);

  std::string tree_text;
  std::vector<NodeSummary> summaries;
  bool truncated = false;
  RenderIntermediateNodes(filtered_nodes, /*depth=*/0, options.max_bytes,
                          tree_text, summaries, truncated);

  if (tree_text.empty()) {
    tree_text = "WebArea\n";
  }

  result.tree = tree_text;
  result.stats.captured_nodes = captured_count;
  result.stats.serialized_nodes = summaries.size();
  result.stats.bytes = tree_text.size();
  result.stats.truncated = truncated;

  // Diff computation
  if (options.since_snapshot_token.has_value() &&
      !options.since_snapshot_token->empty()) {
    const CachedSnapshot* cached =
        observation_cache.Find(tab_metadata.id, *options.since_snapshot_token);
    if (cached) {
      if (cached->navigation_epoch != navigation_epoch ||
          cached->frame_signature != frame_signature) {
        result.diff = ComputeFullResetDiff(summaries);
      } else {
        result.diff = ComputeDiff(cached->nodes, summaries);
      }
    } else {
      result.diff = ComputeFullResetDiff(summaries);
    }
  } else {
    result.diff = std::nullopt;
  }

  // Store current snapshot in observation cache
  CachedSnapshot cached_snap;
  cached_snap.token = snapshot_token;
  cached_snap.tab_id = tab_metadata.id;
  cached_snap.navigation_epoch = navigation_epoch;
  cached_snap.frame_signature = frame_signature;
  cached_snap.timestamp = base::Time::Now();
  cached_snap.nodes = summaries;
  cached_snap.tree_text = tree_text;
  cached_snap.total_bytes = tree_text.size() + (summaries.size() * sizeof(NodeSummary));
  observation_cache.Store(std::move(cached_snap));

  return result;
}

// static
std::string MahoMcpAccessibilityHandler::ComputeDiff(
    const std::vector<NodeSummary>& prev_nodes,
    const std::vector<NodeSummary>& curr_nodes) {
  std::vector<bool> prev_matched(prev_nodes.size(), false);
  std::vector<bool> curr_matched(curr_nodes.size(), false);
  std::vector<std::string> diff_lines;

  // Step 1: Correlate by AXNodeID
  std::unordered_map<ui::AXNodeID, size_t> curr_ax_map;
  for (size_t j = 0; j < curr_nodes.size(); ++j) {
    if (curr_nodes[j].ax_id != 0) {
      curr_ax_map[curr_nodes[j].ax_id] = j;
    }
  }

  for (size_t i = 0; i < prev_nodes.size(); ++i) {
    if (prev_nodes[i].ax_id == 0) {
      continue;
    }
    auto it = curr_ax_map.find(prev_nodes[i].ax_id);
    if (it != curr_ax_map.end()) {
      size_t j = it->second;
      prev_matched[i] = true;
      curr_matched[j] = true;
      if (prev_nodes[i].rendered_line != curr_nodes[j].rendered_line) {
        diff_lines.push_back("- " + prev_nodes[i].rendered_line);
        diff_lines.push_back("+ " + curr_nodes[j].rendered_line);
      }
    }
  }

  // Step 2: Correlate remaining unmatched by bounded fingerprint
  std::unordered_map<std::string, std::vector<size_t>> prev_fp_map;
  for (size_t i = 0; i < prev_nodes.size(); ++i) {
    if (!prev_matched[i] && !prev_nodes[i].fingerprint.empty()) {
      prev_fp_map[prev_nodes[i].fingerprint].push_back(i);
    }
  }

  std::unordered_map<std::string, std::vector<size_t>> curr_fp_map;
  for (size_t j = 0; j < curr_nodes.size(); ++j) {
    if (!curr_matched[j] && !curr_nodes[j].fingerprint.empty()) {
      curr_fp_map[curr_nodes[j].fingerprint].push_back(j);
    }
  }

  for (const auto& pair : prev_fp_map) {
    const std::string& fp = pair.first;
    auto curr_it = curr_fp_map.find(fp);
    if (curr_it != curr_fp_map.end() && pair.second.size() == 1 &&
        curr_it->second.size() == 1) {
      size_t i = pair.second[0];
      size_t j = curr_it->second[0];
      prev_matched[i] = true;
      curr_matched[j] = true;
      if (prev_nodes[i].rendered_line != curr_nodes[j].rendered_line) {
        diff_lines.push_back("- " + prev_nodes[i].rendered_line);
        diff_lines.push_back("+ " + curr_nodes[j].rendered_line);
      }
    }
  }

  // Step 3: Unmatched previous are removals
  for (size_t i = 0; i < prev_nodes.size(); ++i) {
    if (!prev_matched[i]) {
      diff_lines.push_back("- " + prev_nodes[i].rendered_line);
    }
  }

  // Step 4: Unmatched current are additions
  for (size_t j = 0; j < curr_nodes.size(); ++j) {
    if (!curr_matched[j]) {
      diff_lines.push_back("+ " + curr_nodes[j].rendered_line);
    }
  }

  return base::JoinString(diff_lines, "\n");
}

// static
std::string MahoMcpAccessibilityHandler::ComputeFullResetDiff(
    const std::vector<NodeSummary>& curr_nodes) {
  std::vector<std::string> lines;
  lines.reserve(curr_nodes.size());
  for (const auto& node : curr_nodes) {
    lines.push_back("+ " + node.rendered_line);
  }
  return base::JoinString(lines, "\n");
}

// static
bool MahoMcpAccessibilityHandler::ClickByRef(ui::AXTree& tree,
                                             const RefTable& refs,
                                             int ref) {
  auto it = refs.find(ref);
  if (it == refs.end()) {
    return false;
  }
  ui::AXNode* node = tree.GetFromId(it->second);
  if (!node) {
    return false;
  }
  // In a real browser context, this would dispatch a click action via
  // the accessibility action API. For the MCP layer, we validate that
  // the node exists and is actionable.
  return node->data().HasAction(ax::mojom::Action::kDoDefault) ||
         IsInteractiveRole(node->data().role);
}

// static
bool MahoMcpAccessibilityHandler::TypeByRef(ui::AXTree& tree,
                                            const RefTable& refs,
                                            int ref,
                                            std::string_view text) {
  auto it = refs.find(ref);
  if (it == refs.end()) {
    return false;
  }
  ui::AXNode* node = tree.GetFromId(it->second);
  if (!node) {
    return false;
  }
  // Verify this is a text-entry element.
  return node->data().IsTextField();
}

// static
bool MahoMcpAccessibilityHandler::SelectByRef(ui::AXTree& tree,
                                              const RefTable& refs,
                                              int ref,
                                              std::string_view value) {
  auto it = refs.find(ref);
  if (it == refs.end()) {
    return false;
  }
  ui::AXNode* node = tree.GetFromId(it->second);
  if (!node) {
    return false;
  }
  // Valid select targets: combobox or listbox roles.
  ax::mojom::Role role = node->data().role;
  return role == ax::mojom::Role::kComboBoxGrouping ||
         role == ax::mojom::Role::kComboBoxMenuButton ||
         role == ax::mojom::Role::kComboBoxSelect ||
         role == ax::mojom::Role::kTextFieldWithComboBox ||
         role == ax::mojom::Role::kListBox;
}

}  // namespace maho

