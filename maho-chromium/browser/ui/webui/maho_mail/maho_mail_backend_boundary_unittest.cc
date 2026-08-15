// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_page_handler.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
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

}  // namespace
