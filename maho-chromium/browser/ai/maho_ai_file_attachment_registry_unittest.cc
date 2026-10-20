// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_file_attachment_registry.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

TEST(MahoAiFileAttachmentRegistryTest, IssuesAndConsumesOpaqueTokenOnce) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const auto path = dir.GetPath().AppendASCII("notes.md");
  ASSERT_TRUE(base::WriteFile(path, "text"));
  MahoAiFileAttachmentRegistry registry;
  const std::string token = registry.IssueToken(path);
  ASSERT_FALSE(token.empty());
  EXPECT_NE(token, path.AsUTF8Unsafe());
  ASSERT_TRUE(registry.Resolve(token).has_value());
  EXPECT_FALSE(registry.Resolve(token).has_value());
}

TEST(MahoAiFileAttachmentRegistryTest, CapturedImageTokenIsSingleUsePngOnly) {
  const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n',
                                    0x1a, '\n', 0,    0,   0,    13};
  MahoAiFileAttachmentRegistry registry;
  const std::string token = registry.IssueCapturedImageToken(png);
  ASSERT_FALSE(token.empty());
  EXPECT_EQ(registry.TakeCapturedImage(token), png);
  EXPECT_FALSE(registry.TakeCapturedImage(token).has_value());
  EXPECT_FALSE(registry.TakeCapturedImage("unknown-token").has_value());
  EXPECT_TRUE(registry.IssueCapturedImageToken({'G', 'I', 'F', '8'}).empty());
  std::vector<uint8_t> oversize = png;
  oversize.resize(MahoAiFileAttachmentRegistry::kMaxFileBytes + 1);
  EXPECT_TRUE(registry.IssueCapturedImageToken(std::move(oversize)).empty());
}

TEST(MahoAiFileAttachmentRegistryTest, CapturedImageTokenExpires) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  MahoAiFileAttachmentRegistry registry;
  const std::string token = registry.IssueCapturedImageToken(
      {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0});
  ASSERT_FALSE(token.empty());
  task_environment.FastForwardBy(base::Minutes(30));
  EXPECT_FALSE(registry.TakeCapturedImage(token).has_value());
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
  ASSERT_TRUE(base::WriteFile(path, "x"));
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
    ASSERT_TRUE(base::WriteFile(path, "x"));
    EXPECT_FALSE(registry.IssueToken(path).empty()) << extension;
  }
  for (const char* extension : {"pdf", "png", "jpg", "jpeg", "webp"}) {
    const auto path = dir.GetPath().AppendASCII(std::string("file.") + extension);
    ASSERT_TRUE(base::WriteFile(path, "x"));
    EXPECT_TRUE(registry.IssueToken(path).empty()) << extension;
  }
}

TEST(MahoAiFileAttachmentRegistryTest, RejectsTokenFromAnotherProfile) {
  // Profiles live on browser threads and are owned by a profile manager, the
  // way the browser creates them.
  content::BrowserTaskEnvironment task_environment;
  TestingProfileManager profile_manager(TestingBrowserProcess::GetGlobal());
  ASSERT_TRUE(profile_manager.SetUp());
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const auto path = dir.GetPath().AppendASCII("notes.txt");
  ASSERT_TRUE(base::WriteFile(path, "text"));
  auto* first = GetMahoAiFileAttachmentRegistry(
      profile_manager.CreateTestingProfile("first"));
  auto* second = GetMahoAiFileAttachmentRegistry(
      profile_manager.CreateTestingProfile("second"));
  ASSERT_NE(first, second);
  const std::string token = first->IssueToken(path);
  ASSERT_FALSE(token.empty());
  EXPECT_FALSE(second->CanResolve(token));
  EXPECT_FALSE(second->Resolve(token).has_value());
  EXPECT_TRUE(first->CanResolve(token));
}

}  // namespace
}  // namespace maho::ai
