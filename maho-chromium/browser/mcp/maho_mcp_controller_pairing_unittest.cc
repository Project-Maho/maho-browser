// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_controller_pairing.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoMcpControllerPairingTest, IssueVerifyAndPersistDigestOnly) {
  base::DictValue prefs;
  const std::string token = MahoMcpControllerPairing::Issue(prefs);

  ASSERT_EQ(token.size(), 64u);
  EXPECT_TRUE(MahoMcpControllerPairing::Verify(prefs, token));
  const base::ListValue* hashes =
      prefs.FindList(MahoMcpControllerPairing::kPrefName);
  ASSERT_TRUE(hashes);
  ASSERT_EQ(hashes->size(), 1u);
  const std::string* stored_hash = hashes->front().GetIfString();
  ASSERT_TRUE(stored_hash);
  EXPECT_NE(*stored_hash, token);
  EXPECT_EQ(stored_hash->size(), 64u);
}

TEST(MahoMcpControllerPairingTest, RejectsForgedAndRevokedTokens) {
  base::DictValue prefs;
  const std::string token = MahoMcpControllerPairing::Issue(prefs);

  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, token + "00"));
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, "forged"));
  EXPECT_TRUE(MahoMcpControllerPairing::Revoke(prefs, token));
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, token));
  EXPECT_FALSE(MahoMcpControllerPairing::Revoke(prefs, token));
}

TEST(MahoMcpControllerPairingTest, MissingOrMalformedPreferenceFailsClosed) {
  base::DictValue prefs;
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, "token"));
  EXPECT_FALSE(MahoMcpControllerPairing::Revoke(prefs, "token"));

  prefs.Set(MahoMcpControllerPairing::kPrefName, "malformed");
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, "token"));
  EXPECT_FALSE(MahoMcpControllerPairing::Revoke(prefs, "token"));
}

using VerifyResult = MahoMcpControllerPairing::VerifyResult;

TEST(MahoMcpControllerPairingTest, ControllerRecordsStoreDigestAndTombstone) {
  base::DictValue controllers;
  const std::string id = MahoMcpControllerPairing::NewControllerId();
  const std::string token = MahoMcpControllerPairing::NewToken();
  ASSERT_EQ(id.size(), 32u);
  ASSERT_EQ(token.size(), 64u);
  MahoMcpControllerPairing::StoreController(controllers, id, "maho-cli", "cli",
                                            token, base::Time::Now());

  EXPECT_EQ(MahoMcpControllerPairing::VerifyController(controllers, id,
                                                       "maho-cli", token),
            VerifyResult::kPaired);
  // Wrong token, wrong kind, unknown or malformed id are all "unknown".
  EXPECT_EQ(MahoMcpControllerPairing::VerifyController(
                controllers, id, "maho-cli", token + "0"),
            VerifyResult::kUnknown);
  EXPECT_EQ(MahoMcpControllerPairing::VerifyController(
                controllers, id, "maho-cli-repl", token),
            VerifyResult::kUnknown);
  EXPECT_EQ(MahoMcpControllerPairing::VerifyController(
                controllers, MahoMcpControllerPairing::NewControllerId(),
                "maho-cli", token),
            VerifyResult::kUnknown);
  EXPECT_EQ(MahoMcpControllerPairing::VerifyController(controllers, "../x",
                                                       "maho-cli", token),
            VerifyResult::kUnknown);

  // Only the digest is stored; the list never carries token material.
  const base::DictValue* record = controllers.FindDict(id);
  ASSERT_TRUE(record);
  EXPECT_NE(*record->FindString("hash"), token);
  const base::ListValue list =
      MahoMcpControllerPairing::ListControllers(controllers);
  ASSERT_EQ(list.size(), 1u);
  const base::DictValue& item = list.front().GetDict();
  EXPECT_EQ(*item.FindString("controllerId"), id);
  EXPECT_EQ(*item.FindString("kind"), "maho-cli");
  EXPECT_EQ(*item.FindString("name"), "cli");
  EXPECT_FALSE(item.Find("hash"));
  EXPECT_EQ(list.front().DebugString().find(token), std::string::npos);

  EXPECT_TRUE(MahoMcpControllerPairing::RevokeController(controllers, id));
  EXPECT_FALSE(MahoMcpControllerPairing::RevokeController(controllers, id));
  EXPECT_EQ(MahoMcpControllerPairing::VerifyController(controllers, id,
                                                       "maho-cli", token),
            VerifyResult::kRevoked);
  EXPECT_FALSE(controllers.FindDict(id)->Find("hash"));
  EXPECT_TRUE(MahoMcpControllerPairing::ListControllers(controllers).empty());
}

TEST(MahoMcpControllerPairingTest, KindIsSanitizedForPaths) {
  EXPECT_EQ(MahoMcpControllerPairing::SanitizeKind("maho-cli"), "maho-cli");
  for (const char* bad : {"", "../etc", "Maho", "a/b", "x\\y",
                          "kind-with-way-too-many-characters-in-it"}) {
    EXPECT_EQ(MahoMcpControllerPairing::SanitizeKind(bad), "third-party")
        << bad;
  }
  const base::FilePath path = MahoMcpControllerPairing::TokenFilePath(
      base::FilePath(FILE_PATH_LITERAL("/u")), "maho-cli", "abc");
  EXPECT_EQ(path, base::FilePath(FILE_PATH_LITERAL("/u"))
                      .AppendASCII("controllers")
                      .AppendASCII("maho-cli")
                      .AppendASCII("abc.token"));
}

TEST(MahoMcpControllerPairingTest, TokenFileIsOwnerOnly) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path =
      MahoMcpControllerPairing::TokenFilePath(dir.GetPath(), "maho-cli", "id1");
  ASSERT_TRUE(MahoMcpControllerPairing::WriteTokenFile(path, "secret"));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents, "secret");
  EXPECT_FALSE(base::PathExists(path.AddExtensionASCII("tmp")));
#if BUILDFLAG(IS_POSIX)
  int mode = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(path, &mode));
  EXPECT_EQ(mode & 0777, 0600);
  ASSERT_TRUE(base::GetPosixFilePermissions(path.DirName(), &mode));
  EXPECT_EQ(mode & 0777, 0700);
#endif
  // Overwrite replaces atomically.
  ASSERT_TRUE(MahoMcpControllerPairing::WriteTokenFile(path, "next"));
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents, "next");
  EXPECT_TRUE(MahoMcpControllerPairing::DeleteTokenFile(path));
  EXPECT_FALSE(base::PathExists(path));
}

const MahoBrowserToolRegistry::CapabilityDescriptor& Capability(
    const char* name) {
  const auto* descriptor = MahoBrowserToolRegistry::FindCapability(name);
  CHECK(descriptor) << name;
  return *descriptor;
}

TEST(MahoMcpControllerPairingGateTest, DiscoveryAllowedControlNeedsPairing) {
  const auto* read_only = &Capability("browser_tab_list");
  const auto* control = &Capability("browser_navigate");
  using D = MahoMcpPairingGateDecision;
  // Gate disabled (unconfigured or the browser's own agent): always allow.
  EXPECT_EQ(DecideMahoMcpPairingGate(false, false, false, control), D::kAllow);
  // Discovery never needs pairing, even when revoked.
  EXPECT_EQ(DecideMahoMcpPairingGate(true, false, false, read_only),
            D::kAllow);
  EXPECT_EQ(DecideMahoMcpPairingGate(true, false, true, read_only), D::kAllow);
  // Control calls.
  EXPECT_EQ(DecideMahoMcpPairingGate(true, false, false, control),
            D::kPairingRequired);
  EXPECT_EQ(DecideMahoMcpPairingGate(true, true, false, control), D::kAllow);
  EXPECT_EQ(DecideMahoMcpPairingGate(true, false, true, control),
            D::kRevoked);
  // Unknown capabilities are treated as control (fail closed).
  EXPECT_EQ(DecideMahoMcpPairingGate(true, false, false, nullptr),
            D::kPairingRequired);
}

TEST(MahoMcpControllerPairingGateTest, BrowserProcessIsBrowserOwned) {
  EXPECT_TRUE(IsMahoMcpPeerBrowserOwned(base::GetCurrentProcId()));
  EXPECT_FALSE(IsMahoMcpPeerBrowserOwned(base::kNullProcessId));
}

class MahoMcpPairingCoordinatorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    MahoMcpPairingCoordinator::RegisterLocalStatePrefs(prefs_.registry());
    coordinator_.Configure(
        &prefs_, dir_.GetPath(),
        base::BindRepeating(&MahoMcpPairingCoordinatorTest::Present,
                            base::Unretained(this)));
  }

  void Present(const std::string& kind,
               const std::string& name,
               base::OnceCallback<void(bool)> decision) {
    presented_.push_back(kind + "/" + name);
    decision_ = std::move(decision);
  }

  MahoMcpPairingCoordinator::Outcome AwaitOutcome(const std::string& id) {
    MahoMcpPairingCoordinator::Outcome result;
    bool done = false;
    coordinator_.Await(id, base::BindOnce(
                               [](MahoMcpPairingCoordinator::Outcome* out,
                                  bool* done,
                                  const MahoMcpPairingCoordinator::Outcome& o) {
                                 *out = o;
                                 *done = true;
                               },
                               &result, &done));
    task_environment_.RunUntilIdle();
    EXPECT_TRUE(done);
    return result;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir dir_;
  TestingPrefServiceSimple prefs_;
  MahoMcpPairingCoordinator coordinator_;
  std::vector<std::string> presented_;
  base::OnceCallback<void(bool)> decision_;
};

TEST_F(MahoMcpPairingCoordinatorTest, ApprovedIssuesTokenFileAndRecord) {
  const std::string id = coordinator_.RequestPairing("maho-cli", "maho");
  // A second request from the same controller reuses the pending one.
  EXPECT_EQ(coordinator_.RequestPairing("maho-cli", "maho"), id);
  ASSERT_EQ(presented_.size(), 1u);
  EXPECT_EQ(coordinator_.GetOutcome(id).status,
            MahoMcpPairingCoordinator::Status::kPending);
  std::move(decision_).Run(true);
  const auto outcome = AwaitOutcome(id);
  ASSERT_EQ(outcome.status, MahoMcpPairingCoordinator::Status::kApproved);

  const base::FilePath token_path = MahoMcpControllerPairing::TokenFilePath(
      dir_.GetPath(), "maho-cli", outcome.controller_id);
  std::string token;
  ASSERT_TRUE(base::ReadFileToString(token_path, &token));
  EXPECT_EQ(coordinator_.Verify(outcome.controller_id, "maho-cli", token),
            VerifyResult::kPaired);
  ASSERT_EQ(coordinator_.List().size(), 1u);

  EXPECT_TRUE(coordinator_.Revoke(outcome.controller_id));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(token_path));
  EXPECT_EQ(coordinator_.Verify(outcome.controller_id, "maho-cli", token),
            VerifyResult::kRevoked);
  EXPECT_TRUE(coordinator_.List().empty());
}

TEST_F(MahoMcpPairingCoordinatorTest, DeniedIssuesNothing) {
  const std::string id = coordinator_.RequestPairing("maho-cli", "maho");
  std::move(decision_).Run(false);
  EXPECT_EQ(AwaitOutcome(id).status,
            MahoMcpPairingCoordinator::Status::kDenied);
  EXPECT_TRUE(coordinator_.List().empty());
  EXPECT_FALSE(base::PathExists(dir_.GetPath().AppendASCII("controllers")));
}

TEST_F(MahoMcpPairingCoordinatorTest, UnansweredRequestExpiresAfter120s) {
  const std::string id = coordinator_.RequestPairing("maho-cli", "maho");
  task_environment_.FastForwardBy(MahoMcpPairingCoordinator::kPendingTimeout -
                                  base::Seconds(1));
  EXPECT_EQ(coordinator_.GetOutcome(id).status,
            MahoMcpPairingCoordinator::Status::kPending);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(coordinator_.GetOutcome(id).status,
            MahoMcpPairingCoordinator::Status::kExpired);
  // A late approval after expiry issues nothing.
  std::move(decision_).Run(true);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(coordinator_.List().empty());
}

TEST_F(MahoMcpPairingCoordinatorTest, UnconfiguredCoordinatorFailsClosed) {
  MahoMcpPairingCoordinator unconfigured;
  EXPECT_FALSE(unconfigured.enabled());
  const std::string id = unconfigured.RequestPairing("maho-cli", "maho");
  EXPECT_EQ(unconfigured.GetOutcome(id).status,
            MahoMcpPairingCoordinator::Status::kDenied);
  EXPECT_EQ(unconfigured.Verify("x", "maho-cli", "t"), VerifyResult::kUnknown);
}

}  // namespace
}  // namespace maho
