// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_file_attachment_registry.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "chrome/test/base/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

TEST(MahoAiFileAttachmentRegistryTest, IssuesAndConsumesOpaqueTokenOnce) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const auto path = dir.GetPath().AppendASCII("notes.md");
  ASSERT_EQ(4, base::WriteFile(path, "text"));
  MahoAiFileAttachmentRegistry registry;
  const std::string token = registry.IssueToken(path);
  ASSERT_FALSE(token.empty());
  EXPECT_NE(token, path.AsUTF8Unsafe());
  ASSERT_TRUE(registry.Resolve(token).has_value());
  EXPECT_FALSE(registry.Resolve(token).has_value());
}

TEST(MahoAiFileAttachmentRegistryTest, RejectsUnknownToken) {
  MahoAiFileAttachmentRegistry registry;
  EXPECT_FALSE(registry.Resolve("unknown-token").has_value());
}

TEST(MahoAiFileAttachmentRegistryTest, RejectsOversizeFileAboveTenMiB) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const auto path = dir.GetPath().AppendASCII("oversize.txt");
  ASSERT_TRUE(base::WriteFile(path, std::string(
      MahoAiFileAttachmentRegistry::kMaxFileBytes + 1, 'x')));
  MahoAiFileAttachmentRegistry registry;
  EXPECT_TRUE(registry.IssueToken(path).empty());
}

TEST(MahoAiFileAttachmentRegistryTest, RejectsExpiredTokenAfterThirtyMinutes) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const auto path = dir.GetPath().AppendASCII("notes.txt");
  ASSERT_EQ(1, base::WriteFile(path, "x"));
  MahoAiFileAttachmentRegistry registry;
  const std::string token = registry.IssueToken(path);
  ASSERT_FALSE(token.empty());
  EXPECT_TRUE(registry.CanResolve(token));
  task_environment.FastForwardBy(base::Minutes(30));
  EXPECT_FALSE(registry.CanResolve(token));
  EXPECT_FALSE(registry.Resolve(token).has_value());
}

TEST(MahoAiFileAttachmentRegistryTest, AllowsOnlyTextAttachmentExtensions) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  MahoAiFileAttachmentRegistry registry;
  for (const char* extension : {"txt", "md", "csv", "json"}) {
    const auto path = dir.GetPath().AppendASCII(std::string("file.") + extension);
    ASSERT_EQ(1, base::WriteFile(path, "x"));
    EXPECT_FALSE(registry.IssueToken(path).empty()) << extension;
  }
  for (const char* extension : {"pdf", "png", "jpg", "jpeg", "webp"}) {
    const auto path = dir.GetPath().AppendASCII(std::string("file.") + extension);
    ASSERT_EQ(1, base::WriteFile(path, "x"));
    EXPECT_TRUE(registry.IssueToken(path).empty()) << extension;
  }
}

TEST(MahoAiFileAttachmentRegistryTest, RejectsTokenFromAnotherProfile) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const auto path = dir.GetPath().AppendASCII("notes.txt");
  ASSERT_EQ(4, base::WriteFile(path, "text"));
  TestingProfile first_profile;
  TestingProfile second_profile;
  auto* first = GetMahoAiFileAttachmentRegistry(&first_profile);
  auto* second = GetMahoAiFileAttachmentRegistry(&second_profile);
  ASSERT_NE(first, second);
  const std::string token = first->IssueToken(path);
  ASSERT_FALSE(token.empty());
  EXPECT_FALSE(second->CanResolve(token));
  EXPECT_FALSE(second->Resolve(token).has_value());
  EXPECT_TRUE(first->CanResolve(token));
}

}  // namespace
}  // namespace maho::ai
