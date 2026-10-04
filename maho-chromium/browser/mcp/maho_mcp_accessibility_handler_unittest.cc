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

TEST(MahoMcpAccessibilityHandlerTest, NativeSelectHasComboboxRoleAndRef) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kComboBoxSelect;
  update.nodes[1].SetName("Category");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot = MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);
  ASSERT_EQ(refs.size(), 1u);
  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 1u);
  const base::DictValue& select = (*children)[0].GetDict();
  EXPECT_EQ(*select.FindString("role"), "combobox");
  EXPECT_EQ(select.FindInt("ref"), 1);
  EXPECT_TRUE(MahoMcpAccessibilityHandler::SelectByRef(tree, refs, 1, "mail"));
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

TEST(MahoMcpAccessibilityHandlerTest,
     BuildSnapshot_EmitsProtectedForPasswordField) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kTextField;
  update.nodes[1].SetName("Password");
  update.nodes[1].AddState(ax::mojom::State::kProtected);
  update.nodes[1].SetValue("S3NTINEL-maho-vault-9F4C");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 1u);
  const base::DictValue& field = (*children)[0].GetDict();
  ASSERT_TRUE(field.FindBool("protected").has_value());
  EXPECT_TRUE(field.FindBool("protected").value());
}

TEST(MahoMcpAccessibilityHandlerTest,
     BuildSnapshot_EmitsAutocompleteToken) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kTextField;
  update.nodes[1].SetName("One-time code");
  update.nodes[1].AddStringAttribute(
      ax::mojom::StringAttribute::kAutoComplete, "one-time-code");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 1u);
  const base::DictValue& field = (*children)[0].GetDict();
  const std::string* autocomplete = field.FindString("autocomplete");
  ASSERT_TRUE(autocomplete);
  EXPECT_EQ(*autocomplete, "one-time-code");
}

TEST(MahoMcpAccessibilityHandlerTest,
     BuildSnapshot_EmitsInputTypeFromHtmlAttributes) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kTextField;
  update.nodes[1].SetName("Password");
  update.nodes[1].html_attributes.emplace_back("type", "password");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 1u);
  const base::DictValue& field = (*children)[0].GetDict();
  const std::string* input_type = field.FindString("inputType");
  ASSERT_TRUE(input_type);
  EXPECT_EQ(*input_type, "password");
}

TEST(MahoMcpAccessibilityHandlerTest,
     BuildSnapshot_OrdinaryTextboxHasNoCredentialMetadata) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};

  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kSearchBox;
  update.nodes[1].SetName("Search");
  update.nodes[1].SetValue("pizza recipes");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  const base::ListValue* children = snapshot.GetDict().FindList("children");
  ASSERT_TRUE(children);
  ASSERT_EQ(children->size(), 1u);
  const base::DictValue& field = (*children)[0].GetDict();
  EXPECT_FALSE(field.FindBool("protected").has_value());
  EXPECT_FALSE(field.FindString("autocomplete"));
  EXPECT_FALSE(field.FindString("inputType"));
  EXPECT_EQ(*field.FindString("value"), "pizza recipes");
}

TEST(MahoMcpAccessibilityHandlerTest,
     BuildSnapshot_EmitsCredentialSemanticHtmlAttributes) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(2);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kGenericContainer;
  update.nodes[1].SetValue("S3NTINEL-recovery");
  update.nodes[1].html_attributes.emplace_back("aria-label", "Recovery code");
  update.nodes[1].html_attributes.emplace_back("name", "backup_code");
  update.nodes[1].AddStringAttribute(
      ax::mojom::StringAttribute::kHtmlId, "recovery-entry");
  update.nodes[1].html_attributes.emplace_back("placeholder", "Enter code");
  update.nodes[1].html_attributes.emplace_back("data-maho-field",
                                                "recovery-code");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  base::Value snapshot =
      MahoMcpAccessibilityHandler::BuildSnapshot(tree, refs);

  const base::DictValue& field =
      (*snapshot.GetDict().FindList("children"))[0].GetDict();
  const std::string* aria_label = field.FindString("ariaLabel");
  const std::string* field_name = field.FindString("fieldName");
  const std::string* element_id = field.FindString("elementId");
  const std::string* placeholder = field.FindString("placeholder");
  const std::string* data_field = field.FindString("dataField");
  ASSERT_TRUE(aria_label);
  ASSERT_TRUE(field_name);
  ASSERT_TRUE(element_id);
  ASSERT_TRUE(placeholder);
  ASSERT_TRUE(data_field);
  EXPECT_EQ(*aria_label, "Recovery code");
  EXPECT_EQ(*field_name, "backup_code");
  EXPECT_EQ(*element_id, "recovery-entry");
  EXPECT_EQ(*placeholder, "Enter code");
  EXPECT_EQ(*data_field, "recovery-code");
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

TEST(MahoMcpAccessibilityHandlerTest,
     InteractiveFilteringPreservesSemanticAncestors) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(15);

  // Root WebArea
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].SetName("Example App");
  update.nodes[0].child_ids = {2, 6, 10};

  // Generic container 1 wrapping navigation
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kGenericContainer;
  update.nodes[1].child_ids = {3};

  // Navigation semantic ancestor
  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kNavigation;
  update.nodes[2].child_ids = {4, 5};

  // Interactive links under navigation
  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kLink;
  update.nodes[3].SetName("Apply");

  update.nodes[4].id = 5;
  update.nodes[4].role = ax::mojom::Role::kLink;
  update.nodes[4].SetName("Support");

  // Generic container 2 wrapping main
  update.nodes[5].id = 6;
  update.nodes[5].role = ax::mojom::Role::kGenericContainer;
  update.nodes[5].child_ids = {7};

  // Main landmark
  update.nodes[6].id = 7;
  update.nodes[6].role = ax::mojom::Role::kMain;
  update.nodes[6].child_ids = {8};

  // Form semantic ancestor
  update.nodes[7].id = 8;
  update.nodes[7].role = ax::mojom::Role::kForm;
  update.nodes[7].SetName("Terms Agreement");
  update.nodes[7].child_ids = {9, 11, 12};

  // Layout-only group inside form
  update.nodes[8].id = 9;
  update.nodes[8].role = ax::mojom::Role::kGroup;
  update.nodes[8].child_ids = {13, 14};

  // Generic container 3 (empty footer)
  update.nodes[9].id = 10;
  update.nodes[9].role = ax::mojom::Role::kGenericContainer;
  update.nodes[9].child_ids = {15};

  // Button inside form
  update.nodes[10].id = 11;
  update.nodes[10].role = ax::mojom::Role::kButton;
  update.nodes[10].SetName("Continue");

  // Static text label inside form
  update.nodes[11].id = 12;
  update.nodes[11].role = ax::mojom::Role::kStaticText;
  update.nodes[11].SetName("Required field");

  // Radio buttons in layout group
  update.nodes[12].id = 13;
  update.nodes[12].role = ax::mojom::Role::kRadioButton;
  update.nodes[12].SetName("I agree");
  update.nodes[12].SetCheckedState(ax::mojom::CheckedState::kFalse);

  update.nodes[13].id = 14;
  update.nodes[13].role = ax::mojom::Role::kRadioButton;
  update.nodes[13].SetName("I do not agree");
  update.nodes[13].SetCheckedState(ax::mojom::CheckedState::kTrue);

  // Generic container inside empty footer
  update.nodes[14].id = 15;
  update.nodes[14].role = ax::mojom::Role::kGenericContainer;

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::RefTable refs;
  MahoMcpAccessibilityHandler::ObservationCache cache;
  MahoMcpAccessibilityHandler::SnapshotV2Options options;
  options.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
  options.include_hidden = false;

  MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab;
  tab.id = 7;
  tab.url = "https://example.com/form";
  tab.title = "Example App";

  auto res = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      tree, options, refs, tab, /*navigation_epoch=*/1, "sig1", cache, 1);

  // 5 interactive elements: 2 links, 2 radios, 1 button
  EXPECT_EQ(refs.size(), 5u);

  // Semantic ancestors (navigation, main, form) must be preserved in the tree
  EXPECT_NE(res.tree.find("WebArea \"Example App\""), std::string::npos);
  EXPECT_NE(res.tree.find("navigation"), std::string::npos);
  EXPECT_NE(res.tree.find("link @e1 \"Apply\""), std::string::npos);
  EXPECT_NE(res.tree.find("link @e2 \"Support\""), std::string::npos);
  EXPECT_NE(res.tree.find("main"), std::string::npos);
  EXPECT_NE(res.tree.find("form \"Terms Agreement\""), std::string::npos);
  EXPECT_NE(res.tree.find("radio @e3 \"I agree\" [checked=false]"),
            std::string::npos);
  EXPECT_NE(res.tree.find("radio @e4 \"I do not agree\" [checked=true]"),
            std::string::npos);
  EXPECT_NE(res.tree.find("button @e5 \"Continue\""), std::string::npos);

  // Meaningless generic container / layout group roles should NOT appear as separate container lines
  EXPECT_EQ(res.tree.find("generic"), std::string::npos);
  EXPECT_EQ(res.tree.find("group"), std::string::npos);
}

TEST(MahoMcpAccessibilityHandlerTest, HiddenFiltering) {
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(6);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2, 3, 4, 5};

  // Node 2: visible button
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kButton;
  update.nodes[1].SetName("Visible Action");

  // Node 3: invisible button (State::kInvisible)
  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kButton;
  update.nodes[2].SetName("Invisible Action");
  update.nodes[2].AddState(ax::mojom::State::kInvisible);

  // Node 4: hidden button (State::kInvisible)
  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kButton;
  update.nodes[3].SetName("Hidden Action");
  update.nodes[3].AddState(ax::mojom::State::kInvisible);

  // Node 5: container with aria-hidden="true" containing button (Node 6)
  update.nodes[4].id = 5;
  update.nodes[4].role = ax::mojom::Role::kGenericContainer;
  update.nodes[4].html_attributes.emplace_back("aria-hidden", "true");
  update.nodes[4].child_ids = {6};

  update.nodes[5].id = 6;
  update.nodes[5].role = ax::mojom::Role::kButton;
  update.nodes[5].SetName("Aria Hidden Action");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab{
      .id = 1, .url = "https://example.com/", .title = "Hidden Test"};

  // Case A: include_hidden = false (default)
  {
    MahoMcpAccessibilityHandler::RefTable refs;
    MahoMcpAccessibilityHandler::ObservationCache cache;
    MahoMcpAccessibilityHandler::SnapshotV2Options options;
    options.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
    options.include_hidden = false;

    auto res = MahoMcpAccessibilityHandler::BuildSnapshotV2(
        tree, options, refs, tab, /*navigation_epoch=*/1, "sig1", cache, 1);

    EXPECT_EQ(refs.size(), 1u);
    EXPECT_NE(res.tree.find("Visible Action"), std::string::npos);
    EXPECT_EQ(res.tree.find("Invisible Action"), std::string::npos);
    EXPECT_EQ(res.tree.find("Hidden Action"), std::string::npos);
    EXPECT_EQ(res.tree.find("Aria Hidden Action"), std::string::npos);
  }

  // Case B: include_hidden = true
  {
    MahoMcpAccessibilityHandler::RefTable refs;
    MahoMcpAccessibilityHandler::ObservationCache cache;
    MahoMcpAccessibilityHandler::SnapshotV2Options options;
    options.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
    options.include_hidden = true;

    auto res = MahoMcpAccessibilityHandler::BuildSnapshotV2(
        tree, options, refs, tab, /*navigation_epoch=*/1, "sig1", cache, 1);

    EXPECT_EQ(refs.size(), 4u);
    EXPECT_NE(res.tree.find("Visible Action"), std::string::npos);
    EXPECT_NE(res.tree.find("Invisible Action"), std::string::npos);
    EXPECT_NE(res.tree.find("Hidden Action"), std::string::npos);
    EXPECT_NE(res.tree.find("Aria Hidden Action"), std::string::npos);
  }
}

TEST(MahoMcpAccessibilityHandlerTest, IgnoredWrapperKeepsDescendants) {
  // Blink serializes <html> as ignored-but-included. The V2 filter must not
  // prune the whole document at that wrapper: the ignored node itself stays
  // out of the tree, but its descendants must survive. Regression guard: every
  // real page collapsed to captured_nodes==2 / serialized_nodes==1 before the
  // ignored-vs-invisible split.
  ui::AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(4);

  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};

  // Node 2: ignored wrapper (State::kIgnored) — the <html> shape.
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kGenericContainer;
  update.nodes[1].AddState(ax::mojom::State::kIgnored);
  update.nodes[1].child_ids = {3};

  // Node 3: visible button inside the ignored wrapper.
  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kButton;
  update.nodes[2].SetName("Inside Wrapper");
  update.nodes[2].child_ids = {4};

  // Node 4: static text inside the same wrapper.
  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kStaticText;
  update.nodes[3].SetName("Wrapper Label");

  ui::AXTree tree(update);
  MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab{
      .id = 1, .url = "https://example.com/", .title = "Wrapper Test"};

  MahoMcpAccessibilityHandler::RefTable refs;
  MahoMcpAccessibilityHandler::ObservationCache cache;
  MahoMcpAccessibilityHandler::SnapshotV2Options options;
  options.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
  options.include_hidden = false;

  auto res = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      tree, options, refs, tab, /*navigation_epoch=*/1, "sig1", cache, 1);

  EXPECT_EQ(refs.size(), 1u);
  EXPECT_NE(res.tree.find("Inside Wrapper"), std::string::npos);
  EXPECT_NE(res.tree.find("Wrapper Label"), std::string::npos);
  EXPECT_EQ(res.stats.captured_nodes, 4u);

  // Genuinely invisible subtrees are still pruned whole.
  ui::AXTreeUpdate hidden_update;
  hidden_update.root_id = 1;
  hidden_update.nodes.resize(4);

  hidden_update.nodes[0].id = 1;
  hidden_update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  hidden_update.nodes[0].child_ids = {2, 3};

  hidden_update.nodes[1].id = 2;
  hidden_update.nodes[1].role = ax::mojom::Role::kGenericContainer;
  hidden_update.nodes[1].AddState(ax::mojom::State::kInvisible);
  hidden_update.nodes[1].child_ids = {4};

  hidden_update.nodes[2].id = 3;
  hidden_update.nodes[2].role = ax::mojom::Role::kButton;
  hidden_update.nodes[2].SetName("Visible Outer");

  hidden_update.nodes[3].id = 4;
  hidden_update.nodes[3].role = ax::mojom::Role::kButton;
  hidden_update.nodes[3].SetName("Hidden Inner");

  ui::AXTree hidden_tree(hidden_update);
  MahoMcpAccessibilityHandler::RefTable hidden_refs;
  MahoMcpAccessibilityHandler::ObservationCache hidden_cache;
  auto hidden_res = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      hidden_tree, options, hidden_refs, tab, /*navigation_epoch=*/1, "sig2",
      hidden_cache, 1);

  EXPECT_EQ(hidden_refs.size(), 1u);
  EXPECT_EQ(hidden_res.tree.find("Hidden Inner"), std::string::npos);
  EXPECT_NE(hidden_res.tree.find("Visible Outer"), std::string::npos);
}

TEST(MahoMcpAccessibilityHandlerTest, DiffAddRemoveChange) {
  // Snapshot 1: Dialog with email textbox and sign in button
  ui::AXTreeUpdate update1;
  update1.root_id = 1;
  update1.nodes.resize(4);

  update1.nodes[0].id = 1;
  update1.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update1.nodes[0].child_ids = {2};

  update1.nodes[1].id = 2;
  update1.nodes[1].role = ax::mojom::Role::kDialog;
  update1.nodes[1].SetName("Sign in");
  update1.nodes[1].child_ids = {3, 4};

  update1.nodes[2].id = 3;
  update1.nodes[2].role = ax::mojom::Role::kTextField;
  update1.nodes[2].SetName("Email");
  update1.nodes[2].SetValue("user@example.com");

  update1.nodes[3].id = 4;
  update1.nodes[3].role = ax::mojom::Role::kButton;
  update1.nodes[3].SetName("Sign in");

  ui::AXTree tree1(update1);
  MahoMcpAccessibilityHandler::RefTable refs1;
  MahoMcpAccessibilityHandler::ObservationCache cache;
  MahoMcpAccessibilityHandler::SnapshotV2Options options1;
  options1.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;

  MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab{
      .id = 7, .url = "https://example.com/auth", .title = "Auth"};

  auto res1 = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      tree1, options1, refs1, tab, /*navigation_epoch=*/1, "sig1", cache, 1);

  ASSERT_FALSE(res1.diff.has_value());
  EXPECT_FALSE(res1.snapshot_token.empty());

  // Snapshot 2: Dashboard with heading, account link, and logout button
  ui::AXTreeUpdate update2;
  update2.root_id = 1;
  update2.nodes.resize(4);

  update2.nodes[0].id = 1;
  update2.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update2.nodes[0].child_ids = {5, 6, 7};

  update2.nodes[1].id = 5;
  update2.nodes[1].role = ax::mojom::Role::kHeading;
  update2.nodes[1].SetName("Dashboard");

  update2.nodes[2].id = 6;
  update2.nodes[2].role = ax::mojom::Role::kLink;
  update2.nodes[2].SetName("Account");

  update2.nodes[3].id = 7;
  update2.nodes[3].role = ax::mojom::Role::kButton;
  update2.nodes[3].SetName("Logout");

  ui::AXTree tree2(update2);
  MahoMcpAccessibilityHandler::RefTable refs2;
  MahoMcpAccessibilityHandler::SnapshotV2Options options2;
  options2.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
  options2.since_snapshot_token = res1.snapshot_token;

  auto res2 = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      tree2, options2, refs2, tab, /*navigation_epoch=*/1, "sig1", cache, 2);

  ASSERT_TRUE(res2.diff.has_value());
  const std::string& diff = *res2.diff;

  // Removals from previous snapshot
  EXPECT_NE(diff.find("- dialog \"Sign in\""), std::string::npos);
  EXPECT_NE(diff.find("- textbox @e1 \"Email\""), std::string::npos);
  EXPECT_NE(diff.find("- button @e2 \"Sign in\""), std::string::npos);

  // Additions in current snapshot
  EXPECT_NE(diff.find("+ link @e1 \"Account\""), std::string::npos);
  EXPECT_NE(diff.find("+ button @e2 \"Logout\""), std::string::npos);
}

TEST(MahoMcpAccessibilityHandlerTest, RedactionInDiffAndTree) {
  // Snapshot 1 with password and credential metadata
  ui::AXTreeUpdate update1;
  update1.root_id = 1;
  update1.nodes.resize(4);

  update1.nodes[0].id = 1;
  update1.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update1.nodes[0].child_ids = {2, 3, 4};

  update1.nodes[1].id = 2;
  update1.nodes[1].role = ax::mojom::Role::kTextField;
  update1.nodes[1].SetName("Password");
  update1.nodes[1].AddState(ax::mojom::State::kProtected);
  update1.nodes[1].SetValue("SecretP@ssword123!");

  update1.nodes[2].id = 3;
  update1.nodes[2].role = ax::mojom::Role::kTextField;
  update1.nodes[2].SetName("One-time code");
  update1.nodes[2].AddStringAttribute(
      ax::mojom::StringAttribute::kAutoComplete, "one-time-code");
  update1.nodes[2].SetValue("654321");

  update1.nodes[3].id = 4;
  update1.nodes[3].role = ax::mojom::Role::kTextField;
  update1.nodes[3].SetName("Username");
  update1.nodes[3].SetValue("regular_user");

  ui::AXTree tree1(update1);
  MahoMcpAccessibilityHandler::RefTable refs1;
  MahoMcpAccessibilityHandler::ObservationCache cache;
  MahoMcpAccessibilityHandler::SnapshotV2Options options1;
  options1.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;

  MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab{
      .id = 7, .url = "https://example.com/login", .title = "Login"};

  auto res1 = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      tree1, options1, refs1, tab, /*navigation_epoch=*/1, "sig1", cache, 1);

  // Password and OTP values must never appear unredacted in tree
  EXPECT_EQ(res1.tree.find("SecretP@ssword123!"), std::string::npos);
  EXPECT_EQ(res1.tree.find("654321"), std::string::npos);
  EXPECT_NE(res1.tree.find("[REDACTED]"), std::string::npos);
  EXPECT_NE(res1.tree.find("regular_user"), std::string::npos);

  // Snapshot 2: Password updated to a different secret
  ui::AXTreeUpdate update2 = update1;
  update2.nodes[1].SetValue("NewSecretP@ssword456!");

  ui::AXTree tree2(update2);
  MahoMcpAccessibilityHandler::RefTable refs2;
  MahoMcpAccessibilityHandler::SnapshotV2Options options2;
  options2.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
  options2.since_snapshot_token = res1.snapshot_token;

  auto res2 = MahoMcpAccessibilityHandler::BuildSnapshotV2(
      tree2, options2, refs2, tab, /*navigation_epoch=*/1, "sig1", cache, 2);

  // Raw new secret must never appear in tree or diff
  EXPECT_EQ(res2.tree.find("NewSecretP@ssword456!"), std::string::npos);
  if (res2.diff.has_value()) {
    EXPECT_EQ(res2.diff->find("NewSecretP@ssword456!"), std::string::npos);
    EXPECT_EQ(res2.diff->find("SecretP@ssword123!"), std::string::npos);
  }
}

}  // namespace
}  // namespace maho
