// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_page_handler.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kPrivateExportDenial[] =
    R"({"error":{"code":"private_pgp_export_denied","message":"Private PGP key export is not available through CallBackend"}})";

class RecordingBackend : public MahoMailPageHandler::Backend {
 public:
  void ExportPgpKey(
      const std::string& key_id,
      bool include_private,
      base::OnceCallback<void(bool, std::string)> callback) override {
    ++typed_export_invocations;
    exported_key_id = key_id;
    exported_include_private = include_private;
    std::move(callback).Run(
        true, "-----BEGIN PGP PUBLIC KEY BLOCK-----\nredacted\n"
              "-----END PGP PUBLIC KEY BLOCK-----");
  }

  void CallBackend(
      const std::string& command,
      const std::string& args_json,
      base::OnceCallback<void(bool, std::string)> callback) override {
    ++generic_invocations;
    generic_command = command;
    generic_args = args_json;
    std::move(callback).Run(true, "[]");
  }

  void GetAccount(
      const std::string& account_id,
      base::OnceCallback<void(bool, std::string)> callback) override {
    lookup_ids.push_back(account_id);
    lookup_reply = std::move(callback);
  }

  void ReconnectAccount(
      const std::string& account_id,
      base::OnceCallback<void(bool, std::string)> callback) override {
    reconnect_ids.push_back(account_id);
    std::move(callback).Run(reconnect_ok, reconnect_result);
  }

  void OAuthLoopbackSignIn(
      const std::string& provider,
      const std::string& options_json,
      base::OnceCallback<void(bool, std::string)> callback) override {
    oauth_providers.push_back(provider);
    oauth_options.push_back(options_json);
    // Exercise the actual handler completion without navigation/network.
    std::move(callback).Run(false, "oauth_start_failed");
  }

  std::vector<std::string> lookup_ids;
  std::vector<std::string> reconnect_ids;
  std::vector<std::string> oauth_providers;
  std::vector<std::string> oauth_options;
  base::OnceCallback<void(bool, std::string)> lookup_reply;
  bool reconnect_ok = true;
  std::string reconnect_result = "null";

  int typed_export_invocations = 0;
  int generic_invocations = 0;
  std::string exported_key_id;
  bool exported_include_private = true;
  std::string generic_command;
  std::string generic_args;
};

void ExpectGenericExportDenied(MahoMailPageHandler* handler,
                               RecordingBackend* backend,
                               bool include_private) {
  bool ok = true;
  std::string result;
  handler->CallBackend(
      "ExportPgpKey",
      include_private
          ? R"({"key_id":"test-key","include_private":true})"
          : R"({"key_id":"test-key","include_private":false})",
      base::BindOnce(
          [](bool* out_ok, std::string* out_result, bool reply_ok,
             const std::string& reply_result) {
            *out_ok = reply_ok;
            *out_result = reply_result;
          },
          &ok, &result));

  EXPECT_FALSE(ok);
  EXPECT_EQ(kPrivateExportDenial, result);
  EXPECT_EQ(0, backend->generic_invocations);
  EXPECT_EQ(0, backend->typed_export_invocations);
}

TEST(MahoMailPageHandlerTest,
     CallBackendDeniesGenericPrivatePgpExportBeforeBackendInvocation) {
  RecordingBackend backend;
  MahoMailPageHandler handler(&backend);
  ExpectGenericExportDenied(&handler, &backend, true);
}

TEST(MahoMailPageHandlerTest,
     CallBackendDeniesGenericPublicPgpExportBeforeBackendInvocation) {
  RecordingBackend backend;
  MahoMailPageHandler handler(&backend);
  ExpectGenericExportDenied(&handler, &backend, false);
}

TEST(MahoMailPageHandlerTest, TypedPublicPgpExportForwardsFalse) {
  RecordingBackend backend;
  MahoMailPageHandler handler(&backend);
  bool ok = false;
  std::string result;

  handler.ExportPgpKey(
      "test-key", true,
      base::BindOnce(
          [](bool* out_ok, std::string* out_result, bool reply_ok,
             const std::string& reply_result) {
            *out_ok = reply_ok;
            *out_result = reply_result;
          },
          &ok, &result));

  EXPECT_TRUE(ok);
  EXPECT_EQ(1, backend.typed_export_invocations);
  EXPECT_EQ(0, backend.generic_invocations);
  EXPECT_EQ("test-key", backend.exported_key_id);
  EXPECT_FALSE(backend.exported_include_private);
  EXPECT_NE(std::string::npos, result.find("PGP PUBLIC KEY BLOCK"));
}

TEST(MahoMailPageHandlerTest, UnrelatedGenericCommandPassesThroughUnchanged) {
  RecordingBackend backend;
  MahoMailPageHandler handler(&backend);
  bool ok = false;

  handler.CallBackend(
      "ListAccounts", R"({"preserve":"exactly"})",
      base::BindOnce(
          [](bool* out_ok, bool reply_ok, const std::string&) {
            *out_ok = reply_ok;
          },
          &ok));

  EXPECT_TRUE(ok);
  EXPECT_EQ(1, backend.generic_invocations);
  EXPECT_EQ(0, backend.typed_export_invocations);
  EXPECT_EQ("ListAccounts", backend.generic_command);
  EXPECT_EQ(R"({"preserve":"exactly"})", backend.generic_args);
}

// The fake parks only the service reply; routing always runs in the real handler.
struct ReconnectReply {
  int calls = 0;
  bool ok = false;
  std::string result;

  MahoMailPageHandler::ReconnectAccountCallback callback() {
    return base::BindOnce(
        [](ReconnectReply* reply, bool ok, const std::string& result) {
          ++reply->calls;
          reply->ok = ok;
          reply->result = result;
        },
        this);
  }
};

TEST(MahoMailPageHandlerTest, ReconnectPasswordUsesStoredAuth) {
  RecordingBackend backend;
  ReconnectReply reply;
  MahoMailPageHandler handler(&backend);
  handler.ReconnectAccount("acc-password", reply.callback());
  EXPECT_EQ((std::vector<std::string>{"acc-password"}), backend.lookup_ids);
  ASSERT_TRUE(backend.lookup_reply);
  EXPECT_EQ(0, reply.calls);
  EXPECT_TRUE(backend.oauth_providers.empty());
  std::move(backend.lookup_reply).Run(
      true, R"({"id":"acc-password","email":"user@gmail.com","auth_type":"password"})");
  EXPECT_EQ((std::vector<std::string>{"acc-password"}), backend.reconnect_ids);
  EXPECT_TRUE(backend.oauth_providers.empty());
  EXPECT_EQ(1, reply.calls);
  EXPECT_TRUE(reply.ok);
  EXPECT_EQ("null", reply.result);
}

TEST(MahoMailPageHandlerTest, ReconnectPasswordForwardsFailure) {
  RecordingBackend backend;
  backend.reconnect_ok = false;
  backend.reconnect_result = "reconnect_failed";
  ReconnectReply reply;
  MahoMailPageHandler handler(&backend);
  handler.ReconnectAccount("acc-password", reply.callback());
  EXPECT_EQ((std::vector<std::string>{"acc-password"}), backend.lookup_ids);
  ASSERT_TRUE(backend.lookup_reply);
  std::move(backend.lookup_reply).Run(
      true, R"({"id":"acc-password","email":"user@gmail.com","auth_type":"password"})");
  EXPECT_EQ((std::vector<std::string>{"acc-password"}), backend.reconnect_ids);
  EXPECT_TRUE(backend.oauth_providers.empty());
  EXPECT_EQ(1, reply.calls);
  EXPECT_FALSE(reply.ok);
  EXPECT_EQ("reconnect_failed", reply.result);
}

void ExpectOAuthReconnect(const std::string& account_id,
                          const std::string& account_json,
                          const std::string& provider) {
  RecordingBackend backend;
  ReconnectReply reply;
  MahoMailPageHandler handler(&backend);
  handler.ReconnectAccount(account_id, reply.callback());
  EXPECT_EQ((std::vector<std::string>{account_id}), backend.lookup_ids);
  ASSERT_TRUE(backend.lookup_reply);
  EXPECT_TRUE(backend.oauth_providers.empty());
  EXPECT_EQ(0, reply.calls);
  std::move(backend.lookup_reply).Run(true, account_json);
  EXPECT_TRUE(backend.reconnect_ids.empty());
  EXPECT_EQ((std::vector<std::string>{provider}), backend.oauth_providers);
  ASSERT_EQ(1u, backend.oauth_options.size());
  auto options = base::JSONReader::ReadDict(backend.oauth_options.front(),
                                          base::JSON_PARSE_RFC);
  ASSERT_TRUE(options);
  const std::string* existing_id = options->FindString("reauthorize_account_id");
  ASSERT_TRUE(existing_id);
  EXPECT_EQ(account_id, *existing_id);
  EXPECT_EQ(1, reply.calls);
  EXPECT_FALSE(reply.ok);
  EXPECT_EQ("oauth_start_failed", reply.result);
}

TEST(MahoMailPageHandlerTest, ReconnectOutlookPreservesIdentity) {
  ExpectOAuthReconnect(
      "acc-outlook",
      R"({"id":"acc-outlook","email":"user@gmail.com","auth_type":"oauth2_outlook"})",
      "outlook");
}

TEST(MahoMailPageHandlerTest, ReconnectGmailUsesStoredAuth) {
  ExpectOAuthReconnect(
      "acc-gmail",
      R"({"id":"acc-gmail","email":"user@custom.example","auth_type":"oauth2_gmail"})",
      "gmail");
}

TEST(MahoMailPageHandlerTest, ReconnectLookupFailureDoesNotDispatch) {
  RecordingBackend backend;
  ReconnectReply reply;
  MahoMailPageHandler handler(&backend);
  handler.ReconnectAccount("acc-missing", reply.callback());
  EXPECT_EQ((std::vector<std::string>{"acc-missing"}), backend.lookup_ids);
  ASSERT_TRUE(backend.lookup_reply);
  std::move(backend.lookup_reply).Run(false, "lookup_failed");
  EXPECT_TRUE(backend.reconnect_ids.empty());
  EXPECT_TRUE(backend.oauth_providers.empty());
  EXPECT_EQ(1, reply.calls);
  EXPECT_FALSE(reply.ok);
  EXPECT_EQ("lookup_failed", reply.result);
}

TEST(MahoMailPageHandlerTest, ReconnectInvalidAccountDoesNotDispatch) {
  for (const char* account_json : {
           "not json", "[]", "null",
           R"({"id":"acc-invalid","email":"user@gmail.com"})",
           R"({"id":"acc-invalid","auth_type":"unsupported"})",
           R"({"id":"acc-invalid","auth_type":42})"}) {
    SCOPED_TRACE(account_json);
    RecordingBackend backend;
    ReconnectReply reply;
    MahoMailPageHandler handler(&backend);
    handler.ReconnectAccount("acc-invalid", reply.callback());
    EXPECT_EQ((std::vector<std::string>{"acc-invalid"}), backend.lookup_ids);
    ASSERT_TRUE(backend.lookup_reply);
    std::move(backend.lookup_reply).Run(true, account_json);
    EXPECT_TRUE(backend.reconnect_ids.empty());
    EXPECT_TRUE(backend.oauth_providers.empty());
    EXPECT_EQ(1, reply.calls);
    EXPECT_FALSE(reply.ok);
  }
}

TEST(MahoMailPageHandlerTest,
     ReconnectDestroyedBeforeLookupReplyDoesNotDispatch) {
  RecordingBackend backend;
  ReconnectReply reply;
  auto handler = std::make_unique<MahoMailPageHandler>(&backend);
  handler->ReconnectAccount("acc-late", reply.callback());
  EXPECT_EQ((std::vector<std::string>{"acc-late"}), backend.lookup_ids);
  ASSERT_TRUE(backend.lookup_reply);
  EXPECT_EQ(0, reply.calls);
  handler.reset();
  std::move(backend.lookup_reply).Run(
      true, R"({"id":"acc-late","email":"user@example.test","auth_type":"oauth2_outlook"})");
  EXPECT_TRUE(backend.reconnect_ids.empty());
  EXPECT_TRUE(backend.oauth_providers.empty());
  EXPECT_EQ(0, reply.calls);
}

}  // namespace
