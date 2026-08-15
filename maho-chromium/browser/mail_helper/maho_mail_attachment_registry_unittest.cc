// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_attachment_registry.h"

#include <memory>
#include <string>
#include <tuple>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoMailAttachmentRegistryTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    helper_root_ = temp_dir_.GetPath().AppendASCII("helper");
    staging_root_ = temp_dir_.GetPath().AppendASCII("profile-stage");
    launch_root_ = temp_dir_.GetPath().AppendASCII("browser-launch");
    ASSERT_TRUE(base::CreateDirectory(helper_root_));
    registry_ = std::make_unique<MahoMailAttachmentRegistry>(
        "profile-a", helper_root_, staging_root_, launch_root_);
    registry_->SetGeneration(1);
    registry_->SetTokenCallbackForTesting(
        base::BindRepeating([] { return std::string("opaque-token"); }));
    registry_->SetNowCallbackForTesting(
        base::BindRepeating([] { return base::Time::UnixEpoch(); }));
  }

  base::FilePath WriteHelperFile(const std::string& name,
                                 const std::string& contents) {
    base::FilePath path = helper_root_.AppendASCII(name);
    EXPECT_TRUE(base::WriteFile(path, contents));
    return path;
  }

  std::tuple<bool, std::string, std::string> Stage(
      const base::FilePath& source) {
    base::RunLoop loop;
    std::tuple<bool, std::string, std::string> result;
    registry_->StageDownloadedFile(
        MailAttachmentMessageIdentity{"account", 42, "inbox", "1.2"},
        source,
        base::BindOnce(
            [](base::RunLoop* loop,
               std::tuple<bool, std::string, std::string>* result, bool ok,
               std::string value, std::string error) {
              *result = {ok, std::move(value), std::move(error)};
              loop->Quit();
            },
            &loop, &result));
    loop.Run();
    return result;
  }

  std::pair<std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment>,
            std::string>
  Consume(
      const std::string& token,
      const std::string& profile = "profile-a") {
    base::RunLoop loop;
    std::pair<
        std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment>,
        std::string>
        result;
    registry_->Consume(
        token, profile,
        base::BindOnce(
            [](base::RunLoop* loop,
               std::pair<std::unique_ptr<
                             MahoMailAttachmentRegistry::ConsumedAttachment>,
                         std::string>* result,
               std::unique_ptr<
                   MahoMailAttachmentRegistry::ConsumedAttachment>
                   attachment,
               std::string error) {
              *result = {std::move(attachment), std::move(error)};
              loop->Quit();
            },
            &loop, &result));
    loop.Run();
    return result;
  }

  bool DeleteConsumed(
      std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment>
          attachment) {
    base::RunLoop loop;
    bool deleted = false;
    MahoMailAttachmentRegistry::DeleteConsumedAttachment(
        std::move(attachment),
        base::BindOnce(
            [](base::RunLoop* loop, bool* deleted, bool result) {
              *deleted = result;
              loop->Quit();
            },
            &loop, &deleted));
    loop.Run();
    return deleted;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  base::FilePath helper_root_;
  base::FilePath staging_root_;
  base::FilePath launch_root_;
  std::unique_ptr<MahoMailAttachmentRegistry> registry_;
};

TEST_F(MahoMailAttachmentRegistryTest, ValidOpenReachesLauncherOnce) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("message.txt", "trusted payload"));
  ASSERT_TRUE(staged) << stage_error;
  EXPECT_EQ("opaque-token", token);

  int launch_count = 0;
  base::FilePath launched_path;
  auto [attachment, open_error] = Consume(token);
  if (attachment) {
    ++launch_count;
    launched_path = attachment->path();
  }
  ASSERT_TRUE(attachment) << open_error;
  EXPECT_EQ(1, launch_count);
  EXPECT_TRUE(base::MakeAbsoluteFilePath(launch_root_).IsParent(launched_path));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(launched_path, &contents));
  EXPECT_EQ("trusted payload", contents);
  EXPECT_TRUE(DeleteConsumed(std::move(attachment)));
}

TEST_F(MahoMailAttachmentRegistryTest,
       ReplacementAtValidatedHandleHandoffCannotChangeConsumedFile) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("handoff.txt", "trusted payload"));
  ASSERT_TRUE(staged) << stage_error;
  const base::FilePath staged_path = staging_root_.AppendASCII(token);
  registry_->SetValidatedHandleCallbackForTesting(base::BindRepeating(
      [](base::FilePath staged_path) {
        EXPECT_TRUE(base::DeleteFile(staged_path));
        EXPECT_TRUE(base::WriteFile(staged_path, "attacker replacement"));
      },
      staged_path));

  auto [attachment, open_error] = Consume(token);
  ASSERT_TRUE(attachment) << open_error;
  std::string consumed_contents;
  ASSERT_TRUE(base::ReadFileToString(attachment->path(), &consumed_contents));
  EXPECT_EQ("trusted payload", consumed_contents);
  EXPECT_TRUE(DeleteConsumed(std::move(attachment)));
}

TEST_F(MahoMailAttachmentRegistryTest,
       ReplacementAtLaunchHandoffCannotChangeValidatedHandleBytes) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("launch-handoff.txt", "trusted payload"));
  ASSERT_TRUE(staged) << stage_error;

  auto [attachment, open_error] = Consume(token);
  ASSERT_TRUE(attachment) << open_error;
  base::File launch_file = attachment->DuplicateProtectionFile();
  ASSERT_TRUE(launch_file.IsValid());

  // This is the exact platform-launch handoff: an adversary replaces the
  // pathname after the launcher has captured the validated descriptor but
  // before the desktop consumes it.
  ASSERT_TRUE(base::DeleteFile(attachment->path()));
  ASSERT_TRUE(base::WriteFile(attachment->path(), "attacker replacement"));

  ASSERT_GE(launch_file.Seek(base::File::FROM_BEGIN, 0), 0);
  std::string launched_bytes(15, '\0');
  const std::optional<size_t> read = launch_file.ReadAtCurrentPos(
      base::as_writable_byte_span(launched_bytes));
  ASSERT_TRUE(read.has_value());
  launched_bytes.resize(*read);
  EXPECT_EQ("trusted payload", launched_bytes);
  EXPECT_TRUE(DeleteConsumed(std::move(attachment)));
  EXPECT_FALSE(base::PathExists(launch_root_.AppendASCII(token)));
}

TEST_F(MahoMailAttachmentRegistryTest, ConsumedFileDeletedAfterOpenCompletion) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("cleanup.txt", "payload"));
  ASSERT_TRUE(staged) << stage_error;

  auto [attachment, open_error] = Consume(token);
  ASSERT_TRUE(attachment) << open_error;
  const base::FilePath consumed_path = attachment->path();
  ASSERT_TRUE(base::PathExists(consumed_path));

  EXPECT_TRUE(DeleteConsumed(std::move(attachment)));
  EXPECT_FALSE(base::PathExists(consumed_path));
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsAbsolutePathAndTraversalTokens) {
  int launch_count = 0;
  for (const std::string& token : {"/tmp/arbitrary", "../opaque-token"}) {
    auto [attachment, error] = Consume(token);
    launch_count += attachment ? 1 : 0;
    EXPECT_FALSE(attachment);
    EXPECT_EQ("invalid attachment capability", error);
  }
  EXPECT_EQ(0, launch_count);
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsSourceOutsideHelperRoot) {
  base::FilePath outside = temp_dir_.GetPath().AppendASCII("outside.txt");
  ASSERT_TRUE(base::WriteFile(outside, "outside"));
  auto [ok, token, error] = Stage(outside);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(token.empty());
  EXPECT_EQ("attachment source is not trusted", error);
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsSymlinkEscapingHelperRoot) {
  base::FilePath outside = temp_dir_.GetPath().AppendASCII("outside.txt");
  ASSERT_TRUE(base::WriteFile(outside, "outside"));
  base::FilePath link = helper_root_.AppendASCII("escape.txt");
  ASSERT_TRUE(base::CreateSymbolicLink(outside, link));
  auto [ok, token, error] = Stage(link);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(token.empty());
  EXPECT_EQ("attachment source is not trusted", error);
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsReplay) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("message.txt", "payload"));
  ASSERT_TRUE(staged) << stage_error;
  auto [attachment, open_error] = Consume(token);
  ASSERT_TRUE(attachment) << open_error;
  EXPECT_TRUE(DeleteConsumed(std::move(attachment)));
  auto [replayed, error] = Consume(token);
  EXPECT_FALSE(replayed);
  EXPECT_EQ("invalid attachment capability", error);
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsCrossProfileToken) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("message.txt", "payload"));
  ASSERT_TRUE(staged) << stage_error;
  auto [attachment, error] = Consume(token, "profile-b");
  EXPECT_FALSE(attachment);
  EXPECT_EQ("attachment capability profile mismatch", error);
  EXPECT_FALSE(Consume(token).first);
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsGenerationAndAccountRevocation) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("generation.txt", "payload"));
  ASSERT_TRUE(staged) << stage_error;
  registry_->SetGeneration(2);
  EXPECT_FALSE(Consume(token).first);

  registry_->SetTokenCallbackForTesting(
      base::BindRepeating([] { return std::string("account-token"); }));
  auto [restaged, account_token, restage_error] =
      Stage(WriteHelperFile("account.txt", "payload"));
  ASSERT_TRUE(restaged) << restage_error;
  registry_->RevokeAccount("account");
  EXPECT_FALSE(Consume(account_token).first);
}

TEST_F(MahoMailAttachmentRegistryTest, RevocationRejectsPendingStageReply) {
  base::RunLoop loop;
  bool callback_ok = true;
  std::string callback_error;
  registry_->StageDownloadedFile(
      MailAttachmentMessageIdentity{"account", 42, "inbox", "1.2"},
      WriteHelperFile("pending.txt", "payload"),
      base::BindOnce(
          [](base::RunLoop* loop, bool* callback_ok,
             std::string* callback_error, bool ok, std::string value,
             std::string error) {
            *callback_ok = ok;
            *callback_error = std::move(error);
            loop->Quit();
          },
          &loop, &callback_ok, &callback_error));
  registry_->RevokeAll();
  loop.Run();
  EXPECT_FALSE(callback_ok);
  EXPECT_EQ("invalid attachment capability", callback_error);
  EXPECT_EQ(0u, registry_->capability_count_for_testing());
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsExpiryAndDeletesStagedData) {
  base::Time now = base::Time::UnixEpoch();
  registry_->SetNowCallbackForTesting(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  registry_->SetCapabilityLifetimeForTesting(base::Seconds(30));
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("message.txt", "payload"));
  ASSERT_TRUE(staged) << stage_error;
  base::FilePath staged_path = staging_root_.AppendASCII(token);
  ASSERT_TRUE(base::PathExists(staged_path));

  now += base::Seconds(31);
  auto [attachment, error] = Consume(token);
  EXPECT_FALSE(attachment);
  EXPECT_EQ("attachment capability expired", error);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(staged_path));
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsMissingAndReplacedFiles) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("first.txt", "first payload"));
  ASSERT_TRUE(staged) << stage_error;
  base::FilePath staged_path = staging_root_.AppendASCII(token);
  ASSERT_TRUE(base::DeleteFile(staged_path));
  auto [missing_attachment, missing_error] = Consume(token);
  EXPECT_FALSE(missing_attachment);
  EXPECT_EQ("staged attachment changed", missing_error);

  registry_->SetTokenCallbackForTesting(
      base::BindRepeating([] { return std::string("replacement-token"); }));
  auto [restaged, replacement_token, replacement_stage_error] =
      Stage(WriteHelperFile("second.txt", "second payload"));
  ASSERT_TRUE(restaged) << replacement_stage_error;
  base::FilePath replacement_path =
      staging_root_.AppendASCII(replacement_token);
  ASSERT_TRUE(base::WriteFile(replacement_path, "attacker replacement"));
  auto [replaced_attachment, replaced_error] = Consume(replacement_token);
  EXPECT_FALSE(replaced_attachment);
  EXPECT_EQ("staged attachment changed", replaced_error);
}

TEST_F(MahoMailAttachmentRegistryTest, RejectsStagedSymlinkEscape) {
  auto [staged, token, stage_error] =
      Stage(WriteHelperFile("message.txt", "payload"));
  ASSERT_TRUE(staged) << stage_error;
  base::FilePath staged_path = staging_root_.AppendASCII(token);
  ASSERT_TRUE(base::DeleteFile(staged_path));
  base::FilePath outside = temp_dir_.GetPath().AppendASCII("outside.txt");
  ASSERT_TRUE(base::WriteFile(outside, "payload"));
  ASSERT_TRUE(base::CreateSymbolicLink(outside, staged_path));

  auto [attachment, error] = Consume(token);
  EXPECT_FALSE(attachment);
  EXPECT_EQ("staged attachment changed", error);
}

}  // namespace
}  // namespace maho
