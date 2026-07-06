// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"

#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"

namespace maho {
namespace {

ui::AXTreeUpdate MakeEmptyTree() {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  return update;
}

TEST(MahoMcpAccessibilityHandlerTest, BuildSnapshot_EmptyTree) {
  ui::AXTreeUpdate update = MakeEmptyTree();
  ui::AXTree tree(update);

  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  ASSERT_TRUE(snapshot.is_dict());
  const std::string* role = snapshot.GetDict().FindString("role");
  ASSERT_TRUE(role);
  EXPECT_EQ(*role, "WebArea");

  const base::ListValue* children = snapshot.GetDict().FindList("children");
  EXPECT_TRUE(!children || children->empty());
  EXPECT_TRUE(refs.empty());
}

TEST(MahoMcpAccessibilityHandlerTest, BuildSnapshot_InteractiveElementsGetRefs) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(4);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2, 3, 4};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kButton;
  update.nodes[1].SetName("Submit");

  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kLink;
  update.nodes[2].SetName("Home");

  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kTextField;
  update.nodes[3].SetName("Email");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  ASSERT_TRUE(snapshot.is_dict());
  EXPECT_EQ(refs.size(), 3u);

  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 3u);

  const base::DictValue& button = (*children)[0].GetDict();
  EXPECT_EQ(*button.FindString("role"), "button");
  EXPECT_EQ(*button.FindString("name"), "Submit");
  ASSERT_TRUE(button.FindInt("ref").has_value());
  EXPECT_EQ(button.FindInt("ref").value(), 1);

  const base::DictValue& link = (*children)[1].GetDict();
  EXPECT_EQ(*link.FindString("role"), "link");
  EXPECT_EQ(*link.FindString("name"), "Home");
  EXPECT_EQ(link.FindInt("ref").value(), 2);

  const base::DictValue& textbox = (*children)[2].GetDict();
  EXPECT_EQ(*textbox.FindString("role"), "textbox");
  EXPECT_EQ(*textbox.FindString("name"), "Email");
  EXPECT_EQ(textbox.FindInt("ref").value(), 3);
}

TEST(MahoMcpAccessibilityHandlerTest,
     BuildSnapshot_NonInteractiveElementsSkipped) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(4);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2, 3, 4};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kHeading;
  update.nodes[1].SetName("Page Title");

  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kParagraph;

  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kImage;
  update.nodes[3].SetName("Logo");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  EXPECT_TRUE(refs.empty());

  ASSERT_TRUE(snapshot.is_dict());
  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 3u);

  const base::DictValue& heading = (*children)[0].GetDict();
  EXPECT_EQ(*heading.FindString("role"), "heading");
  EXPECT_EQ(*heading.FindString("name"), "Page Title");
  EXPECT_FALSE(heading.FindInt("ref").has_value());

  const base::DictValue& paragraph = (*children)[1].GetDict();
  EXPECT_EQ(*paragraph.FindString("role"), "paragraph");
  EXPECT_FALSE(paragraph.FindInt("ref").has_value());

  const base::DictValue& image = (*children)[2].GetDict();
  EXPECT_EQ(*image.FindString("role"), "image");
  EXPECT_EQ(*image.FindString("name"), "Logo");
  EXPECT_FALSE(image.FindInt("ref").has_value());
}

TEST(MahoMcpAccessibilityHandlerTest, BuildSnapshot_TruncatesAtMaxBytes) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(101);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  for (int i = 1; i <= 100; ++i) {
    update.nodes[0].child_ids.push_back(i + 1);
    update.nodes[i].id = i + 1;
    update.nodes[i].role = ax::mojom::Role::kButton;
    update.nodes[i].SetName("Button " + std::to_string(i));
  }

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;

  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs, 256);

  ASSERT_TRUE(snapshot.is_dict());
  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  EXPECT_GT(children->size(), 0u);

  const base::DictValue& last = children->back().GetDict();
  const std::string* name = last.FindString("name");
  ASSERT_TRUE(name);
  EXPECT_NE(name->find("TRUNCATED"), std::string::npos);
}

TEST(MahoMcpAccessibilityHandlerTest, RefTableRoundTrip) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(3);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2, 3};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kButton;
  update.nodes[1].SetName("OK");
  update.nodes[1].AddAction(ax::mojom::Action::kDoDefault);

  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kTextField;
  update.nodes[2].SetName("Name");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  ASSERT_EQ(refs.size(), 2u);

  // ref=1 should map to button (id=2), ref=2 should map to textfield (id=3).
  EXPECT_EQ(refs[1], 2);
  EXPECT_EQ(refs[2], 3);

  // ClickByRef should succeed for the button.
  EXPECT_TRUE(MahoMcpAccessibilityHandler::ClickByRef(tree, refs, 1));

  // TypeByRef should succeed for the textfield.
  EXPECT_TRUE(MahoMcpAccessibilityHandler::TypeByRef(tree, refs, 2, "hello"));

  // ClickByRef with invalid ref should fail.
  EXPECT_FALSE(MahoMcpAccessibilityHandler::ClickByRef(tree, refs, 99));
}

}  // namespace
}  // namespace maho
