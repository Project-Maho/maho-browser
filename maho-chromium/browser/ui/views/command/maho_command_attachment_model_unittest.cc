// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_attachment_model.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoCommandAttachmentModelTest, AddsAndRemovesBoundedUniqueChips) {
  MahoCommandAttachmentModel model;
  EXPECT_TRUE(model.AddCurrentTab("Current tab"));
  EXPECT_FALSE(model.AddOpenTab(-1, "Invalid tab"));
  EXPECT_FALSE(model.AddOpenTab(-1, "Duplicate current tab"));
  EXPECT_TRUE(model.AddOpenTab(7, "Research"));
  EXPECT_FALSE(model.AddOpenTab(7, "Duplicate tab"));
  EXPECT_TRUE(model.AddLocalFile("opaque-token", "notes.md"));
  EXPECT_EQ(model.chips().size(), 3u);
  EXPECT_TRUE(model.Remove(1));
  EXPECT_FALSE(model.Remove(3));
}

TEST(MahoCommandAttachmentModelTest, BackspaceOnEmptyRemovesLastChipOnly) {
  MahoCommandAttachmentModel model;
  EXPECT_FALSE(model.RemoveLastOnBackspace(false));
  EXPECT_FALSE(model.RemoveLastOnBackspace(true));
  ASSERT_TRUE(model.AddCurrentTab("Current tab"));
  EXPECT_FALSE(model.RemoveLastOnBackspace(false));
  EXPECT_TRUE(model.RemoveLastOnBackspace(true));
  ASSERT_TRUE(model.RemoveLast());
  EXPECT_TRUE(model.chips().empty());
  EXPECT_FALSE(model.RemoveLastOnBackspace(true));
}

TEST(MahoCommandAttachmentModelTest, DeduplicatesFileTokens) {
  MahoCommandAttachmentModel model;
  EXPECT_TRUE(model.AddLocalFile("token-a", "notes.txt"));
  EXPECT_FALSE(model.AddLocalFile("token-a", "duplicate.txt"));
  EXPECT_TRUE(model.AddLocalFile("token-b", "other.txt"));
  EXPECT_EQ(2u, model.chips().size());
}

TEST(MahoCommandAttachmentModelTest, EnforcesLimitAndBackspacePolicy) {
  MahoCommandAttachmentModel model;
  EXPECT_FALSE(model.RemoveLastOnBackspace(true));
  for (size_t i = 0; i < MahoCommandAttachmentModel::kMaxChips; ++i)
    EXPECT_TRUE(model.AddLocalFile("token-" + std::to_string(i), "file"));
  EXPECT_FALSE(model.AddLocalFile("overflow", "file"));
  EXPECT_FALSE(model.RemoveLastOnBackspace(false));
  EXPECT_TRUE(model.RemoveLastOnBackspace(true));
  model.RemoveLast();
  EXPECT_EQ(model.chips().size(),
            MahoCommandAttachmentModel::kMaxChips - 1);
}

TEST(MahoCommandAttachmentModelTest, BackspaceRequiresEmptyFieldAndChip) {
  EXPECT_FALSE(MahoCommandAttachmentModel::ShouldRemoveLastOnBackspace(
      false, true));
  EXPECT_FALSE(MahoCommandAttachmentModel::ShouldRemoveLastOnBackspace(
      true, false));
  EXPECT_TRUE(MahoCommandAttachmentModel::ShouldRemoveLastOnBackspace(
      true, true));
}

TEST(MahoCommandAttachmentModelTest, ConvertsTwoTabsAndFileToken) {
  MahoCommandAttachmentModel model;
  ASSERT_TRUE(model.AddOpenTab(12, "Research"));
  ASSERT_TRUE(model.AddOpenTab(28, "Planning"));
  ASSERT_TRUE(model.AddLocalFile("opaque", "notes.md"));
  auto attachments = model.ToMojom();
  ASSERT_EQ(attachments.size(), 3u);
  EXPECT_EQ(attachments[0]->kind,
            maho_ai::mojom::ContextSourceKind::kOpenTab);
  EXPECT_EQ(attachments[0]->tab_id, 12);
  EXPECT_EQ(attachments[1]->tab_id, 28);
  EXPECT_EQ(attachments[2]->kind,
            maho_ai::mojom::ContextSourceKind::kLocalFile);
  ASSERT_TRUE(attachments[2]->file_token.has_value());
  EXPECT_EQ(*attachments[2]->file_token, "opaque");
}

}  // namespace maho
