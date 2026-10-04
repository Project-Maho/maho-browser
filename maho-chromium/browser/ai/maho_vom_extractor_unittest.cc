// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ai/maho_vom_extractor.h"

#include <set>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {

namespace {

VomElementInput MakeElement(uint64_t id,
                            const std::string& role,
                            const std::string& name,
                            int paint_order,
                            bool actionable) {
  VomElementInput element;
  element.backend_node_id = id;
  element.role = role;
  element.name = name;
  element.bounds_css = gfx::Rect{10, 20, 100, 30};
  element.paint_order = paint_order;
  element.is_actionable = actionable;
  return element;
}

}  // namespace

TEST(MahoVomExtractorTest, TagsOnlyActionableVisible) {
  std::vector<VomElementInput> elements;
  elements.push_back(MakeElement(1, "button", "Submit", 2, true));
  elements.push_back(MakeElement(2, "textbox", "Search", 1, true));
  elements.push_back(MakeElement(3, "staticText", "Plain", 3, false));

  VomPage page = MahoVomExtractor::Extract(elements, "Test", "", 4000);

  ASSERT_EQ(page.refs.size(), 2u);
  EXPECT_EQ(page.refs[0].tag, "@e1");
  EXPECT_EQ(page.refs[0].backend_node_id, 2u);
  EXPECT_EQ(page.refs[1].tag, "@e2");
  EXPECT_EQ(page.refs[1].backend_node_id, 1u);
  EXPECT_NE(page.text.find("@e1 textbox \"Search\""), std::string::npos);
  EXPECT_NE(page.text.find("@e2 button \"Submit\""), std::string::npos);
  EXPECT_NE(page.text.find("text \"Plain\""), std::string::npos);
  EXPECT_EQ(page.text.find("text \"Plain\" [@e"), std::string::npos);
}

TEST(MahoVomExtractorTest, OccludedElementsDropped) {
  std::vector<VomElementInput> elements;
  elements.push_back(MakeElement(1, "button", "Hidden", 1, true));
  elements[0].is_occluded = true;
  elements.push_back(MakeElement(2, "button", "Visible", 2, true));

  VomPage page = MahoVomExtractor::Extract(elements, "Test", "", 4000);

  EXPECT_EQ(page.refs.size(), 1u);
  EXPECT_EQ(page.refs[0].backend_node_id, 2u);
  EXPECT_EQ(page.text.find("Hidden"), std::string::npos);
  EXPECT_NE(page.text.find("(1 elements, 1 refs)"), std::string::npos);
}

TEST(MahoVomExtractorTest, BackdropOccludesLowerPaintOrder) {
  std::vector<VomElementInput> elements;
  elements.push_back(MakeElement(1, "dialog", "Modal", 10, true));
  elements[0].draws_backdrop = true;
  elements.push_back(MakeElement(2, "button", "Beneath", 5, true));
  elements.push_back(MakeElement(3, "button", "Above", 11, true));

  VomPage page = MahoVomExtractor::Extract(elements, "Test", "", 4000);

  std::set<uint64_t> ref_ids;
  for (const VomRef& ref : page.refs) {
    ref_ids.insert(ref.backend_node_id);
  }
  EXPECT_EQ(ref_ids.count(2u), 0u);
  EXPECT_EQ(ref_ids.count(1u), 1u);
  EXPECT_EQ(ref_ids.count(3u), 1u);
  EXPECT_EQ(page.text.find("Beneath"), std::string::npos);
}

TEST(MahoVomExtractorTest, PaginationResumesStable) {
  std::vector<VomElementInput> elements;
  for (int i = 0; i < 500; ++i) {
    elements.push_back(MakeElement(1000 + i, "button", "el" + std::to_string(i),
                                   i, true));
  }

  std::set<uint64_t> seen;
  std::string cursor;
  int pages = 0;
  do {
    VomPage page = MahoVomExtractor::Extract(elements, "Big", cursor, 200);
    ASSERT_FALSE(page.refs.empty()) << "page " << pages;
    for (const VomRef& ref : page.refs) {
      EXPECT_EQ(seen.count(ref.backend_node_id), 0u) << "page " << pages;
      seen.insert(ref.backend_node_id);
    }
    if (page.next_cursor.has_value()) {
      auto resume = MahoVomExtractor::ResumeIndexFromCursor(*page.next_cursor);
      ASSERT_TRUE(resume.has_value());
      ASSERT_GT(*resume, seen.size() == page.refs.size() ? 0u : 0u);
      cursor = *page.next_cursor;
    } else {
      cursor.clear();
    }
    ++pages;
    ASSERT_LT(pages, 100) << "pagination did not terminate";
  } while (!cursor.empty());

  EXPECT_EQ(seen.size(), 500u);
  EXPECT_GE(pages, 2);
}


TEST(MahoVomExtractorTest, DeterministicOutput) {
  std::vector<VomElementInput> elements;
  elements.push_back(MakeElement(1, "button", "A \"quoted\"", 1, true));
  elements[0].hover_hint = "tooltip";
  elements.push_back(MakeElement(2, "textbox", "B", 2, true));

  VomPage a = MahoVomExtractor::Extract(elements, "Test", "", 4000);
  VomPage b = MahoVomExtractor::Extract(elements, "Test", "", 4000);
  EXPECT_EQ(a.text, b.text);
  EXPECT_EQ(a.refs.size(), b.refs.size());
  EXPECT_NE(a.text.find("\\\"quoted\\\""), std::string::npos);
  EXPECT_NE(a.text.find("[hover first: tooltip]"), std::string::npos);
}

TEST(MahoVomExtractorTest, EmptyListHeader) {
  std::vector<VomElementInput> elements;
  VomPage page = MahoVomExtractor::Extract(elements, "Empty", "", 4000);
  EXPECT_EQ(page.text, "VOM Empty (0 elements, 0 refs)\n");
  EXPECT_TRUE(page.refs.empty());
  EXPECT_FALSE(page.next_cursor.has_value());
}

TEST(MahoVomExtractorTest, ResumeIndexFromCursorRoundTrip) {
  EXPECT_EQ(MahoVomExtractor::ResumeIndexFromCursor(
                "deadbeefdeadbeef:123"),
            std::optional<size_t>(123u));
  EXPECT_EQ(MahoVomExtractor::ResumeIndexFromCursor("garbage"),
            std::nullopt);
  EXPECT_EQ(MahoVomExtractor::ResumeIndexFromCursor("abc:"), std::nullopt);
  EXPECT_EQ(MahoVomExtractor::ResumeIndexFromCursor("abc:x9"), std::nullopt);
}

}  // namespace maho::ai
