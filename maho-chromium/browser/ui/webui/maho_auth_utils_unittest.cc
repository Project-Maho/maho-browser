// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_auth_utils.h"

#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/scoped_environment_variable_override.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho::auth {
namespace {

class HasValidRelaySessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayAccessTokenEncryptedB64, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayRefreshTokenEncryptedB64, "");
    prefs_.registry()->RegisterInt64Pref(
        account_prefs::kRelayAccessTokenExpiresAt, 0);
    prefs_.registry()->RegisterInt64Pref(
        account_prefs::kRelayRefreshTokenExpiresAt, 0);
    prefs_.registry()->RegisterStringPref(account_prefs::kRelayUserEmail, "");
    prefs_.registry()->RegisterStringPref(account_prefs::kRelayUserId, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayUserDisplayName, "");
    prefs_.registry()->RegisterStringPref(account_prefs::kRelayUserTier, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayOAuthProvider, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayOAuthProviderSub, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelaySubscriptionStatus, "");
    prefs_.registry()->RegisterInt64Pref(
        account_prefs::kRelaySubscriptionExpiresAt, 0);
    prefs_.registry()->RegisterStringPref(account_prefs::kRelaySyncKeyMode,
                                          "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelaySyncClientHeldRoomId, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelaySyncRetiredRoomId, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelaySyncClientHeldRelayPendingRoomId, "");
  }

  int64_t NowSec() const { return base::Time::Now().ToTimeT(); }
  int64_t FutureSec() const { return NowSec() + 3600; }
  int64_t PastSec() const { return NowSec() - 3600; }

  TestingPrefServiceSimple prefs_;
};

TEST_F(HasValidRelaySessionTest, ReturnsFalseWhenTokensMissing) {
  EXPECT_FALSE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest, ReturnsTrueWhenAccessTokenValid) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, FutureSec());
  EXPECT_TRUE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest,
       ReturnsTrueWhenAccessExpiredButRefreshValid) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, PastSec());
  prefs_.SetString(account_prefs::kRelayRefreshTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayRefreshTokenExpiresAt, FutureSec());
  EXPECT_TRUE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest, ReturnsFalseWhenBothExpired) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, PastSec());
  prefs_.SetString(account_prefs::kRelayRefreshTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayRefreshTokenExpiresAt, PastSec());
  EXPECT_FALSE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest,
       ReturnsFalseWhenAccessExpiredAndRefreshMissing) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, PastSec());
  EXPECT_FALSE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest, OldSessionWithoutOAuthMetadataRemainsValid) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, FutureSec());

  EXPECT_TRUE(HasValidRelaySession(&prefs_));
  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));
}

TEST_F(HasValidRelaySessionTest, GoogleMetadataBuildsExactMailOptions) {
  prefs_.SetString(account_prefs::kRelayUserEmail, " user@example.com ");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, " google ");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, " subject-123 ");

  EXPECT_EQ(
      R"({"expected_google_sub":"subject-123","login_hint":"user@example.com"})",
      BuildMailOAuthStartOptionsJson(&prefs_));
}

TEST_F(HasValidRelaySessionTest, NonGoogleOrMalformedMetadataIsIndependent) {
  prefs_.SetString(account_prefs::kRelayUserEmail, "user@example.com");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, "github");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "subject-123");
  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));

  prefs_.SetString(account_prefs::kRelayOAuthProvider, " google ");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "   ");
  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));
}

TEST_F(HasValidRelaySessionTest, RefreshPreservesOAuthMetadataPair) {
  prefs_.SetString(account_prefs::kRelayOAuthProvider, " google ");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, " subject-123 ");
  StoreTokensParams params;

  PreserveRelayIdentityMetadata(&prefs_, &params);

  EXPECT_EQ(" google ", params.oauth_provider);
  EXPECT_EQ(" subject-123 ", params.oauth_provider_sub);
}

// The relay rotates the refresh token on every /auth/refresh and invalidates
// the consumed one, so the rotated pair must be what gets persisted while the
// signed-in identity stays intact.
TEST_F(HasValidRelaySessionTest, RefreshedTokenParamsCarryRotationAndIdentity) {
  prefs_.SetString(account_prefs::kRelayUserEmail, "user@example.com");
  prefs_.SetString(account_prefs::kRelayUserId, "user-1");
  prefs_.SetString(account_prefs::kRelayUserDisplayName, "User");
  prefs_.SetString(account_prefs::kRelayUserTier, "pro");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, "google");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "subject-123");
  RefreshedTokens tokens;
  tokens.access_token = "new-access";
  tokens.refresh_token = "rotated-refresh";
  tokens.access_expires_at = FutureSec();
  tokens.refresh_expires_at = FutureSec() + 3600;

  const StoreTokensParams params =
      BuildRefreshedRelayTokenParams(&prefs_, tokens);

  EXPECT_EQ("new-access", params.access_token);
  EXPECT_EQ("rotated-refresh", params.refresh_token);
  EXPECT_EQ(tokens.access_expires_at, params.access_expires_at);
  EXPECT_EQ(tokens.refresh_expires_at, params.refresh_expires_at);
  EXPECT_EQ("user@example.com", params.user_email);
  EXPECT_EQ("user-1", params.user_id);
  EXPECT_EQ("User", params.user_display_name);
  EXPECT_EQ("pro", params.user_tier);
  EXPECT_EQ("google", params.oauth_provider);
  EXPECT_EQ("subject-123", params.oauth_provider_sub);
}

TEST_F(HasValidRelaySessionTest, LogoutClearsOAuthMetadataRepeatedly) {
  prefs_.SetString(account_prefs::kRelayUserEmail, "user@example.com");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, "google");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "subject-123");

  ClearRelayTokens(&prefs_);
  ClearRelayTokens(&prefs_);

  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));
  EXPECT_TRUE(prefs_.GetString(account_prefs::kRelayOAuthProvider).empty());
  EXPECT_TRUE(prefs_.GetString(account_prefs::kRelayOAuthProviderSub).empty());
}

TEST_F(HasValidRelaySessionTest, RecordsClientHeldKeyModeAndClearsOnLogout) {
  EXPECT_FALSE(IsClientHeldSyncAccount(&prefs_));
  RecordSyncKeyMode(&prefs_, account_prefs::kSyncKeyModeClientHeld, "room-2");
  EXPECT_TRUE(IsClientHeldSyncAccount(&prefs_));
  EXPECT_EQ("room-2",
            prefs_.GetString(account_prefs::kRelaySyncClientHeldRoomId));

  RecordSyncKeyMode(&prefs_, "escrow", "room-1");
  EXPECT_FALSE(IsClientHeldSyncAccount(&prefs_));
  EXPECT_TRUE(
      prefs_.GetString(account_prefs::kRelaySyncClientHeldRoomId).empty());

  RecordSyncKeyMode(&prefs_, account_prefs::kSyncKeyModeClientHeld, "room-2");
  prefs_.SetString(account_prefs::kRelaySyncRetiredRoomId, "room-1");
  ClearRelayTokens(&prefs_);
  EXPECT_FALSE(IsClientHeldSyncAccount(&prefs_));
  EXPECT_TRUE(
      prefs_.GetString(account_prefs::kRelaySyncRetiredRoomId).empty());
}

TEST_F(HasValidRelaySessionTest, ClientHeldJoinRejectsForeignOrInvalidPhrase) {
  // Escrow accounts are unaffected.
  EXPECT_EQ("", CheckClientHeldRecoveryPhrase(&prefs_, "not a phrase"));

  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  char* generated = maho_core_generate_sync_key(core);
  ASSERT_NE(generated, nullptr);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(generated, base::JSON_PARSE_RFC);
  maho_string_free(generated);
  maho_core_free(core);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const std::string* phrase = parsed->GetDict().FindString("recoveryPhrase");
  const std::string* room = parsed->GetDict().FindString("roomId");
  ASSERT_TRUE(phrase && room);

  RecordSyncKeyMode(&prefs_, account_prefs::kSyncKeyModeClientHeld, *room);
  EXPECT_EQ("", CheckClientHeldRecoveryPhrase(&prefs_, *phrase));
  EXPECT_EQ("That recovery phrase is not valid. Check all 24 words.",
            CheckClientHeldRecoveryPhrase(&prefs_, "not a phrase"));

  RecordSyncKeyMode(&prefs_, account_prefs::kSyncKeyModeClientHeld,
                    "another-account-room");
  EXPECT_EQ("That recovery phrase belongs to a different Sync account.",
            CheckClientHeldRecoveryPhrase(&prefs_, *phrase));
}

// An escrow device whose room was retired by another device's switch must
// not accept an arbitrary phrase (that would start a new room): until the
// account's new room is known every phrase is refused, and the retired room's
// own phrase is always refused.
TEST_F(HasValidRelaySessionTest, RetiredRoomFailsClosedUntilNewRoomKnown) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  auto generate = [core]() {
    char* raw = maho_core_generate_sync_key(core);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
    maho_string_free(raw);
    return std::move(parsed->GetDict());
  };
  base::DictValue old_key = generate();
  base::DictValue new_key = generate();
  base::DictValue foreign_key = generate();
  maho_core_free(core);
  const std::string old_room = *old_key.FindString("roomId");

  RecordSyncKeyMode(&prefs_, "escrow", old_room);
  MarkSyncRoomRetired(&prefs_, old_room);
  EXPECT_TRUE(IsClientHeldSyncAccount(&prefs_));
  EXPECT_TRUE(NeedsClientHeldRoomRefresh(&prefs_));
  EXPECT_NE("", CheckClientHeldRecoveryPhrase(
                    &prefs_, *foreign_key.FindString("recoveryPhrase")));
  EXPECT_NE("", CheckClientHeldRecoveryPhrase(
                    &prefs_, *new_key.FindString("recoveryPhrase")));

  // GET /sync/bootstrap reported the account's new room.
  RecordSyncKeyMode(&prefs_, account_prefs::kSyncKeyModeClientHeld,
                    *new_key.FindString("roomId"));
  EXPECT_FALSE(NeedsClientHeldRoomRefresh(&prefs_));
  EXPECT_EQ("", CheckClientHeldRecoveryPhrase(
                    &prefs_, *new_key.FindString("recoveryPhrase")));
  EXPECT_EQ("That recovery phrase belongs to a different Sync account.",
            CheckClientHeldRecoveryPhrase(
                &prefs_, *foreign_key.FindString("recoveryPhrase")));
  EXPECT_NE("", CheckClientHeldRecoveryPhrase(
                    &prefs_, *old_key.FindString("recoveryPhrase")));
}

class SyncBootstrapTest : public ::testing::Test {
 protected:
  using ResultFuture = base::test::TestFuture<bool, std::string>;

  SyncBootstrapTest()
      : relay_url_override_("MAHO_SYNC_RELAY_URL",
                            "https://relay.mahobrowser.com") {}

  void SetUp() override {
    core_ = maho_core_new();
    ASSERT_NE(core_, nullptr);
    saved_core_ = maho::GetCore();
    maho::SetCore(core_);
  }

  void TearDown() override {
    task_environment_.RunUntilIdle();
    maho::SetCore(saved_core_);
    maho_core_free(core_);
  }

  LoginResultCallback CountingCallback(ResultFuture* result, int* call_count) {
    return base::BindOnce(
        [](ResultFuture* result, int* call_count, bool ok,
           const std::string& error) {
          ++*call_count;
          result->SetValue(ok, error);
        },
        result, call_count);
  }

  void Start(ResultFuture* result, int* call_count) {
    testing::FetchAccountSyncBootstrapForTesting(
        url_loader_factory_.GetSafeWeakWrapper(), "access-token",
        CountingCallback(result, call_count),
        base::BindRepeating(
            [](std::string* mode, std::string* room, const std::string& m,
               const std::string& r) {
              *mode = m;
              *room = r;
            },
            &key_mode_, &key_mode_room_));
  }

  void WaitForRequest(std::string_view method) {
    url_loader_factory_.WaitForRequest(GURL(kBootstrapUrl));
    ASSERT_EQ(1, url_loader_factory_.NumPending());
    network::TestURLLoaderFactory::PendingRequest* pending =
        url_loader_factory_.GetPendingRequest(0);
    ASSERT_NE(pending, nullptr);
    EXPECT_EQ(method, pending->request.method);
  }

  std::string PendingUpload() {
    network::TestURLLoaderFactory::PendingRequest* pending =
        url_loader_factory_.GetPendingRequest(0);
    EXPECT_NE(pending, nullptr);
    return pending ? network::GetUploadData(pending->request) : std::string();
  }

  void Respond(std::string_view body, net::HttpStatusCode status) {
    ASSERT_TRUE(url_loader_factory_.SimulateResponseForPendingRequest(
        kBootstrapUrl, body, status));
  }

  void ExpectResult(ResultFuture* result,
                    int* call_count,
                    bool expected_ok,
                    std::string_view expected_error) {
    ASSERT_TRUE(result->Wait());
    EXPECT_EQ(expected_ok, result->Get<0>());
    EXPECT_EQ(expected_error, result->Get<1>());
    task_environment_.RunUntilIdle();
    EXPECT_EQ(1, *call_count);
    EXPECT_EQ(0, url_loader_factory_.NumPending());
  }

  static constexpr char kBootstrapUrl[] =
      "https://relay.mahobrowser.com/sync/bootstrap";

  base::ScopedEnvironmentVariableOverride relay_url_override_;
  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory url_loader_factory_;
  MahoCore* saved_core_ = nullptr;
  MahoCore* core_ = nullptr;
  std::string key_mode_;
  std::string key_mode_room_;
};

// A client-held account's bootstrap carries no seed. Sign-in must succeed
// without configuring Sync and without PUTting a fresh seed or escrow, and the
// mode must be reported so welcome/Settings ask for the recovery phrase.
TEST_F(SyncBootstrapTest, ClientHeldBootstrapRecordsModeWithoutPuttingSeed) {
  ResultFuture result;
  int call_count = 0;
  Start(&result, &call_count);

  WaitForRequest("GET");
  Respond(R"({"key_mode":"client_held","room_id":"client-room"})",
          net::HTTP_OK);

  ExpectResult(&result, &call_count, true, "");
  EXPECT_EQ(1u, url_loader_factory_.total_requests());
  EXPECT_EQ("client_held", key_mode_);
  EXPECT_EQ("client-room", key_mode_room_);
}

TEST_F(SyncBootstrapTest, ClientHeldBootstrapWithoutRoomIsInvalid) {
  ResultFuture result;
  int call_count = 0;
  Start(&result, &call_count);

  WaitForRequest("GET");
  Respond(R"({"key_mode":"client_held"})", net::HTTP_OK);

  ExpectResult(&result, &call_count, false,
               "Invalid account Sync bootstrap response.");
  EXPECT_EQ(1u, url_loader_factory_.total_requests());
  EXPECT_TRUE(key_mode_.empty());
}

TEST_F(SyncBootstrapTest, CreatedBootstrapReportsEscrowMode) {
  ResultFuture result;
  int call_count = 0;
  Start(&result, &call_count);

  WaitForRequest("GET");
  Respond("", net::HTTP_NOT_FOUND);
  WaitForRequest("PUT");
  const std::string generated = PendingUpload();
  ASSERT_FALSE(generated.empty());
  Respond(generated, net::HTTP_CREATED);

  ExpectResult(&result, &call_count, true, "");
  EXPECT_EQ("escrow", key_mode_);
  EXPECT_FALSE(key_mode_room_.empty());
}

class ClientHeldSwitchTest : public ::testing::Test {
 protected:
  using SwitchFuture = base::test::TestFuture<bool, int, std::string>;

  ClientHeldSwitchTest()
      : relay_url_override_("MAHO_SYNC_RELAY_URL",
                            "https://relay.mahobrowser.com") {}

  void Start(SwitchFuture* result, const std::string& room_id) {
    PostClientHeldSyncBootstrap(url_loader_factory_.GetSafeWeakWrapper(),
                                "access-token", room_id,
                                result->GetCallback<bool, int, const std::string&>());
  }

  static constexpr char kSwitchUrl[] =
      "https://relay.mahobrowser.com/sync/bootstrap/client-held";

  base::ScopedEnvironmentVariableOverride relay_url_override_;
  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory url_loader_factory_;
};

TEST_F(ClientHeldSwitchTest, PostsNewRoomAndAcceptsMatchingResponse) {
  SwitchFuture result;
  Start(&result, "new-room");
  url_loader_factory_.WaitForRequest(GURL(kSwitchUrl));
  network::TestURLLoaderFactory::PendingRequest* pending =
      url_loader_factory_.GetPendingRequest(0);
  ASSERT_NE(pending, nullptr);
  EXPECT_EQ("POST", pending->request.method);
  EXPECT_EQ(R"({"room_id":"new-room"})",
            network::GetUploadData(pending->request));
  ASSERT_TRUE(url_loader_factory_.SimulateResponseForPendingRequest(
      kSwitchUrl, R"({"key_mode":"client_held","room_id":"new-room"})",
      net::HTTP_OK));

  ASSERT_TRUE(result.Wait());
  EXPECT_TRUE(result.Get<0>());
  EXPECT_EQ(200, result.Get<1>());
  EXPECT_EQ("", result.Get<2>());
}

TEST_F(ClientHeldSwitchTest, RetiredRoomReportsRoomRetired) {
  SwitchFuture result;
  Start(&result, "old-room");
  url_loader_factory_.WaitForRequest(GURL(kSwitchUrl));
  ASSERT_TRUE(url_loader_factory_.SimulateResponseForPendingRequest(
      kSwitchUrl, R"({"error":"room_retired","code":"room_retired"})",
      net::HTTP_GONE));

  ASSERT_TRUE(result.Wait());
  EXPECT_FALSE(result.Get<0>());
  EXPECT_EQ(410, result.Get<1>());
  EXPECT_EQ("room_retired", result.Get<2>());
}

TEST_F(SyncBootstrapTest, MissingBootstrapIsCreatedAndConfigured) {
  ResultFuture result;
  int call_count = 0;
  Start(&result, &call_count);

  WaitForRequest("GET");
  Respond("", net::HTTP_NOT_FOUND);
  WaitForRequest("PUT");
  const std::string generated = PendingUpload();
  ASSERT_FALSE(generated.empty());
  Respond(generated, net::HTTP_CREATED);

  ExpectResult(&result, &call_count, true, "");
  EXPECT_EQ(2u, url_loader_factory_.total_requests());
}

TEST_F(SyncBootstrapTest, ConflictRefetchesAndConfiguresStoredBootstrap) {
  ResultFuture result;
  int call_count = 0;
  Start(&result, &call_count);

  WaitForRequest("GET");
  Respond("", net::HTTP_NOT_FOUND);
  WaitForRequest("PUT");
  const std::string generated = PendingUpload();
  ASSERT_FALSE(generated.empty());
  Respond("", net::HTTP_CONFLICT);
  WaitForRequest("GET");
  Respond(generated, net::HTTP_OK);

  ExpectResult(&result, &call_count, true, "");
  EXPECT_EQ(3u, url_loader_factory_.total_requests());
}

TEST_F(SyncBootstrapTest, MismatchedCreatedBootstrapFailsCleanly) {
  ResultFuture result;
  int call_count = 0;
  Start(&result, &call_count);

  WaitForRequest("GET");
  Respond("", net::HTTP_NOT_FOUND);
  WaitForRequest("PUT");
  Respond(R"({"version":1,"room_id":"other-room","seed":"other-seed"})",
          net::HTTP_OK);

  ExpectResult(&result, &call_count, false,
               "Invalid account Sync bootstrap response.");
  EXPECT_EQ(2u, url_loader_factory_.total_requests());
}

TEST_F(SyncBootstrapTest, InvalidGetBodiesFailCleanly) {
  for (std::string_view body : {"", "{", "[]"}) {
    ResultFuture result;
    int call_count = 0;
    Start(&result, &call_count);

    WaitForRequest("GET");
    Respond(body, net::HTTP_OK);

    ExpectResult(&result, &call_count, false,
                 "Invalid account Sync bootstrap response.");
  }
  EXPECT_EQ(3u, url_loader_factory_.total_requests());
}

TEST_F(SyncBootstrapTest, InvalidPutBodiesFailCleanly) {
  for (std::string_view body : {"", "{", "[]"}) {
    ResultFuture result;
    int call_count = 0;
    Start(&result, &call_count);

    WaitForRequest("GET");
    Respond("", net::HTTP_NOT_FOUND);
    WaitForRequest("PUT");
    Respond(body, net::HTTP_OK);

    ExpectResult(&result, &call_count, false,
                 "Invalid account Sync bootstrap response.");
  }
  EXPECT_EQ(6u, url_loader_factory_.total_requests());
}

}  // namespace
}  // namespace maho::auth
