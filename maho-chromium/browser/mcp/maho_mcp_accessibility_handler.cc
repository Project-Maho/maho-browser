// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"

#include <string>
#include <string_view>

#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"

namespace maho {

namespace {

// Approximate byte overhead per JSON node (braces, commas, field names).
constexpr size_t kNodeOverheadBytes = 64;

}  // namespace

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
    case ax::mojom::Role::kArticle:
      return "article";
    case ax::mojom::Role::kSection:
      return "section";
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
         role == ax::mojom::Role::kTextFieldWithComboBox ||
         role == ax::mojom::Role::kListBox;
}

}  // namespace maho
