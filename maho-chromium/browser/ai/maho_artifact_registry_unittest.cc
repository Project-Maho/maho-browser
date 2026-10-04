// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_artifact_registry.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/simple_test_tick_clock.h"
#include "build/build_config.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

class MahoArtifactRegistryTest : public ChromeRenderViewHostTestHarness {
protected:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    tick_clock_.SetNowTicks(base::TimeTicks() + base::Hours(1));
    registry_ = std::make_unique<MahoArtifactRegistry>(
        profile(), MahoArtifactRegistry::kDefaultStorageLimitBytes,
        &tick_clock_);
  }

  void TearDown() override {
    other_contents_.reset();
    registry_.reset();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  base::FilePath ArtifactPath(std::string_view name) const {
    return registry_->artifact_root().Append(
        base::FilePath::FromUTF8Unsafe(std::string(name)));
  }

  ArtifactId Register(std::string session_id, std::string display_name,
                      std::string storage_name, uint64_t size,
                      int64_t created_at_ms) {
    EXPECT_TRUE(base::CreateDirectory(ArtifactPath(storage_name).DirName()));
    EXPECT_TRUE(
        base::WriteFile(ArtifactPath(storage_name), std::string(size, 'x')));
    auto result = registry_->RegisterArtifact(
        std::move(session_id), std::move(display_name), "text/plain", size,
        std::move(storage_name), created_at_ms);
    EXPECT_TRUE(result.has_value()) << result.error();
    return result.has_value() ? *result : ArtifactId();
  }

  base::SimpleTestTickClock tick_clock_;
  std::unique_ptr<MahoArtifactRegistry> registry_;
  std::unique_ptr<content::WebContents> other_contents_;
};

TEST_F(MahoArtifactRegistryTest, RegisterListGetResolveAndJsonReloadRoundTrip) {
  const ArtifactId first = Register("session-a", "one.txt", "stored-one", 3, 1);
  const ArtifactId second =
      Register("session-a", "two.txt", "stored-two", 4, 2);
  const ArtifactId third =
      Register("session-b", "three.txt", "stored-three", 5, 3);

  EXPECT_NE(first, second);
  EXPECT_NE(second, third);
  ASSERT_EQ(registry_->ListArtifacts("session-a").size(), 2u);
  EXPECT_EQ(registry_->GetArtifact(first)->display_name, "one.txt");
  EXPECT_EQ(registry_->ResolvePath(second), ArtifactPath("stored-two"));

  // Persistence is asynchronous: flush the pending index write, rebuild the
  // service, then flush its async initial load before asserting the reload.
  task_environment()->RunUntilIdle();
  registry_ = std::make_unique<MahoArtifactRegistry>(
      profile(), MahoArtifactRegistry::kDefaultStorageLimitBytes, &tick_clock_);
  task_environment()->RunUntilIdle();
  const auto reloaded_a = registry_->ListArtifacts("session-a");
  const auto reloaded_b = registry_->ListArtifacts("session-b");
  ASSERT_EQ(reloaded_a.size(), 2u);
  ASSERT_EQ(reloaded_b.size(), 1u);
  EXPECT_EQ(reloaded_a[0].artifact_id, first);
  EXPECT_EQ(reloaded_a[1].artifact_id, second);
  EXPECT_EQ(reloaded_b[0].artifact_id, third);
}

TEST_F(MahoArtifactRegistryTest,
       RegisterIsIdempotentPerSessionAndStoragePath) {
  const ArtifactId first = Register("session", "one.txt", "stored", 3, 1);
  const ArtifactId again =
      Register("session", "one-renamed.txt", "stored", 7, 5);
  EXPECT_EQ(first, again);
  ASSERT_EQ(registry_->ListArtifacts("session").size(), 1u);
  auto got = registry_->GetArtifact(first);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ("one-renamed.txt", got->display_name);
  EXPECT_EQ(7u, got->size_bytes);

  // A different storage path in the same session is a distinct artifact.
  const ArtifactId other = Register("session", "two.txt", "stored-two", 4, 2);
  EXPECT_NE(first, other);
  EXPECT_EQ(registry_->ListArtifacts("session").size(), 2u);
}

TEST_F(MahoArtifactRegistryTest,
       RenamePreservesExtensionAndRejectsUnsafeNames) {
  const ArtifactId id = Register("session", "report.html", "stored", 3, 1);

  auto renamed = registry_->RenameArtifact(id, "weekly-report");
  ASSERT_TRUE(renamed.has_value()) << renamed.error();
  EXPECT_EQ(renamed->display_name, "weekly-report.html");

  EXPECT_FALSE(registry_->RenameArtifact(id, "a/b").has_value());
  EXPECT_FALSE(registry_->RenameArtifact(id, "..").has_value());
  EXPECT_EQ(registry_->GetArtifact(id)->display_name, "weekly-report.html");
}

TEST_F(MahoArtifactRegistryTest,
       RegistersAndResolvesSafelyContainedNestedStoragePath) {
  const ArtifactId id =
      Register("session", "plan.html", "reports/plan.html", 3, 1);
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(registry_->ResolvePath(id), ArtifactPath("reports/plan.html"));

  task_environment()->RunUntilIdle();
  registry_ = std::make_unique<MahoArtifactRegistry>(
      profile(), MahoArtifactRegistry::kDefaultStorageLimitBytes, &tick_clock_);
  task_environment()->RunUntilIdle();
  ASSERT_EQ(registry_->ListArtifacts("session").size(), 1u);
  EXPECT_EQ(registry_->ResolvePath(id), ArtifactPath("reports/plan.html"));
}

TEST_F(MahoArtifactRegistryTest, RejectsUnsafeStoragePaths) {
  EXPECT_FALSE(registry_
                   ->RegisterArtifact("session", "safe.txt", "text/plain", 1,
                                      "../escape", 1)
                   .has_value());
  EXPECT_FALSE(registry_
                   ->RegisterArtifact("session", "safe.txt", "text/plain", 1,
                                      "dir/../escape", 1)
                   .has_value());
  EXPECT_FALSE(registry_
                   ->RegisterArtifact("session", "safe.txt", "text/plain", 1,
                                      "/absolute", 1)
                   .has_value());
}

#if !BUILDFLAG(IS_WIN)
TEST_F(MahoArtifactRegistryTest, RejectsSymlinkEscape) {
  base::ScopedTempDir outside;
  ASSERT_TRUE(outside.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(registry_->artifact_root()));
  const base::FilePath link = registry_->artifact_root().AppendASCII("escape");
  if (!base::CreateSymbolicLink(outside.GetPath(), link)) {
    GTEST_SKIP() << "Symlinks unavailable on this platform";
  }
  ASSERT_TRUE(base::WriteFile(outside.GetPath().AppendASCII("plan.html"), "x"));

  // RegisterArtifact runs on the UI thread and cannot perform blocking realpath
  // resolution, so its containment check is lexical. Symlink-escape is rejected
  // authoritatively at resolve/serve time (ResolveContainedStoragePath, which
  // runs on a MayBlock sequence); every file reader/deleter routes through it.
  auto escaping = registry_->RegisterArtifact(
      "session", "plan.html", "text/html", 1, "escape/plan.html", 1);
  ASSERT_TRUE(escaping.has_value()) << escaping.error();
  EXPECT_FALSE(registry_->ResolvePath(*escaping).has_value());
}
#endif  // !BUILDFLAG(IS_WIN)

TEST_F(MahoArtifactRegistryTest, FreshProfileArtifactRootIsPreparedOffThread) {
  base::DeletePathRecursively(registry_->artifact_root());
  ASSERT_FALSE(base::PathExists(registry_->artifact_root()));

  registry_->PrepareArtifactRoot();
  EXPECT_FALSE(registry_->ArtifactRootForTurn().has_value());
  task_environment()->RunUntilIdle();

  auto root = registry_->ArtifactRootForTurn();
  ASSERT_TRUE(root.has_value()) << root.error();
  EXPECT_TRUE(base::DirectoryExists(registry_->artifact_root()));
  EXPECT_EQ(*root, base::MakeAbsoluteFilePath(registry_->artifact_root()));
}

TEST_F(MahoArtifactRegistryTest, DeleteRemovesFileAndRevokesCapabilities) {
  const ArtifactId id = Register("session", "delete.txt", "stored", 3, 1);
  auto token = registry_->IssueCapability(id, "preview", web_contents());
  ASSERT_TRUE(token);
  EXPECT_TRUE(registry_->VerifyCapability(*token, "preview", web_contents()));

  EXPECT_TRUE(registry_->DeleteArtifact(id));
  task_environment()->RunUntilIdle();  // async off-thread file deletion
  EXPECT_FALSE(base::PathExists(ArtifactPath("stored")));
  EXPECT_FALSE(registry_->GetArtifact(id));
  EXPECT_FALSE(registry_->VerifyCapability(*token, "preview", web_contents()));
}

TEST_F(MahoArtifactRegistryTest,
       CapabilityRequiresPurposeWebContentsAndUnexpiredBinding) {
  const ArtifactId id = Register("session", "bound.txt", "stored", 3, 1);
  other_contents_ =
      content::WebContentsTester::CreateTestWebContents(profile(), nullptr);

  auto token = registry_->IssueCapability(id, "preview", web_contents());
  ASSERT_TRUE(token);
  EXPECT_TRUE(registry_->VerifyCapability(*token, "preview", web_contents()));
  EXPECT_FALSE(registry_->VerifyCapability(*token, "export", web_contents()));
  EXPECT_FALSE(
      registry_->VerifyCapability(*token, "preview", other_contents_.get()));

  tick_clock_.Advance(MahoArtifactRegistry::kCapabilityTtl);
  EXPECT_FALSE(registry_->VerifyCapability(*token, "preview", web_contents()));
}

TEST_F(MahoArtifactRegistryTest, ExplicitRevokeDeniesCapability) {
  const ArtifactId id = Register("session", "revoke.txt", "stored", 3, 1);
  auto token = registry_->IssueCapability(id, "export", web_contents());
  ASSERT_TRUE(token);

  registry_->RevokeArtifactCapabilities(id);
  EXPECT_FALSE(registry_->VerifyCapability(*token, "export", web_contents()));
}

TEST_F(MahoArtifactRegistryTest, PrunesOldestArtifactsAndTheirFiles) {
  registry_ =
      std::make_unique<MahoArtifactRegistry>(profile(), 8, &tick_clock_);
  const ArtifactId oldest = Register("session", "old.txt", "old", 4, 1);
  const ArtifactId middle = Register("session", "middle.txt", "middle", 4, 2);
  const ArtifactId newest = Register("session", "new.txt", "new", 4, 3);

  EXPECT_FALSE(registry_->GetArtifact(oldest));
  EXPECT_TRUE(registry_->GetArtifact(middle));
  EXPECT_TRUE(registry_->GetArtifact(newest));
  task_environment()->RunUntilIdle();  // async off-thread pruning of files
  EXPECT_FALSE(base::PathExists(ArtifactPath("old")));
  EXPECT_TRUE(base::PathExists(ArtifactPath("middle")));
  EXPECT_TRUE(base::PathExists(ArtifactPath("new")));
}

TEST_F(MahoArtifactRegistryTest, PrivateProfileRefusesServiceAndOperations) {
  Profile *otr = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  EXPECT_EQ(MahoArtifactRegistry::GetForProfile(otr), nullptr);

  MahoArtifactRegistry private_registry(otr, 100, &tick_clock_);
  EXPECT_FALSE(private_registry
                   .RegisterArtifact("session", "private.txt", "text/plain", 1,
                                     "stored", 1)
                   .has_value());
  EXPECT_TRUE(private_registry.ListArtifacts("session").empty());
  EXPECT_FALSE(private_registry.GetArtifact("opaque"));
  EXPECT_FALSE(
      private_registry.RenameArtifact("opaque", "new.txt").has_value());
}

TEST_F(MahoArtifactRegistryTest, ProfileScopedFactoryReturnsStableInstance) {
  EnsureMahoArtifactRegistryFactoryBuilt();
  EXPECT_NE(MahoArtifactRegistry::GetForProfile(profile()), nullptr);
  EXPECT_EQ(MahoArtifactRegistry::GetForProfile(profile()),
            MahoArtifactRegistry::GetForProfile(profile()));
}

TEST_F(MahoArtifactRegistryTest, DeducesFormatTypedArtifactKinds) {
  auto html_id = registry_->RegisterArtifact(
      "session", "page.html", "text/html", 10, "page.html", 1);
  ASSERT_TRUE(html_id.has_value());
  auto html_art = registry_->GetArtifact(*html_id);
  ASSERT_TRUE(html_art.has_value());
  EXPECT_EQ(html_art->kind, MahoArtifactKind::kHtml);

  auto pdf_id = registry_->RegisterArtifact(
      "session", "document.pdf", "application/pdf", 20, "document.pdf", 2);
  ASSERT_TRUE(pdf_id.has_value());
  auto pdf_art = registry_->GetArtifact(*pdf_id);
  ASSERT_TRUE(pdf_art.has_value());
  EXPECT_EQ(pdf_art->kind, MahoArtifactKind::kPdf);

  auto xlsx_id = registry_->RegisterArtifact(
      "session", "sheet.xlsx",
      "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", 30,
      "sheet.xlsx", 3);
  ASSERT_TRUE(xlsx_id.has_value());
  auto xlsx_art = registry_->GetArtifact(*xlsx_id);
  ASSERT_TRUE(xlsx_art.has_value());
  EXPECT_EQ(xlsx_art->kind, MahoArtifactKind::kXlsx);

  auto csv_id = registry_->RegisterArtifact(
      "session", "data.csv", "text/csv", 15, "data.csv", 4);
  ASSERT_TRUE(csv_id.has_value());
  auto csv_art = registry_->GetArtifact(*csv_id);
  ASSERT_TRUE(csv_art.has_value());
  EXPECT_EQ(csv_art->kind, MahoArtifactKind::kXlsx);

  auto txt_id = registry_->RegisterArtifact(
      "session", "notes.txt", "text/plain", 5, "notes.txt", 5);
  ASSERT_TRUE(txt_id.has_value());
  auto txt_art = registry_->GetArtifact(*txt_id);
  ASSERT_TRUE(txt_art.has_value());
  EXPECT_EQ(txt_art->kind, MahoArtifactKind::kGeneric);
}

TEST_F(MahoArtifactRegistryTest, ExplicitKindPreservedAcrossReload) {
  auto id = registry_->RegisterArtifact(
      "session", "custom_data.bin", "application/octet-stream", 100,
      "custom_data.bin", 1, MahoArtifactKind::kXlsx);
  ASSERT_TRUE(id.has_value());
  auto art = registry_->GetArtifact(*id);
  ASSERT_TRUE(art.has_value());
  EXPECT_EQ(art->kind, MahoArtifactKind::kXlsx);

  // Flush write and reload from disk
  task_environment()->RunUntilIdle();
  registry_ = std::make_unique<MahoArtifactRegistry>(
      profile(), MahoArtifactRegistry::kDefaultStorageLimitBytes, &tick_clock_);
  task_environment()->RunUntilIdle();

  auto reloaded = registry_->GetArtifact(*id);
  ASSERT_TRUE(reloaded.has_value());
  EXPECT_EQ(reloaded->kind, MahoArtifactKind::kXlsx);
}

TEST_F(MahoArtifactRegistryTest, TokenGatedPreviewEnforcement) {
  const ArtifactId id = Register("session", "report.pdf", "report.pdf", 25, 1);
  auto token = registry_->IssueCapability(id, "preview", web_contents());
  ASSERT_TRUE(token.has_value());

  // Metadata retrieval for response header derivation
  auto meta = registry_->GetCapabilityArtifactForResponseMetadata(*token, "preview");
  ASSERT_TRUE(meta.has_value());
  EXPECT_EQ(meta->display_name, "report.pdf");
  EXPECT_EQ(meta->kind, MahoArtifactKind::kPdf);

  // Metadata check fails with wrong purpose
  EXPECT_FALSE(registry_->GetCapabilityArtifactForResponseMetadata(*token, "export"));

  // Body verification enforces web contents binding and purpose
  auto verified = registry_->VerifyCapability(*token, "preview", web_contents());
  ASSERT_TRUE(verified.has_value());
  EXPECT_EQ(verified->artifact_id, id);

  // Different purpose denied
  EXPECT_FALSE(registry_->VerifyCapability(*token, "export", web_contents()));

  // Different WebContents denied
  EXPECT_FALSE(registry_->VerifyCapability(*token, "preview", other_contents_.get()));

  // Invalid token denied
  EXPECT_FALSE(registry_->VerifyCapability("invalid-token", "preview", web_contents()));
}

} // namespace
} // namespace maho::ai
