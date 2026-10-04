// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_runtime_event_persistence.h"

#include <string>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

constexpr char kSentinel[] = "S3NTINEL-maho-vault-9F4C";

TEST(MahoAiEgressRedactionTest,
     PageContextToValueRedactsCredentialsAndPreservesOrdinaryText) {
  MahoAiPageContextExtractor::PageContextResult result;
  result.extraction_status =
      MahoAiPageContextExtractor::PageContextResult::Status::kSuccess;
  result.title = "ordinary title";
  result.url = "https://example.test/page?token=S3NTINEL-maho-vault-9F4C&keep=1";
  result.selected_text = "Authorization: Basic S3NTINEL-maho-vault-9F4C";
  result.main_text = "password=S3NTINEL-maho-vault-9F4C ordinary marker";
  result.headings = {"Bearer S3NTINEL-maho-vault-9F4C"};
  result.meta_description = "Cookie: session=S3NTINEL-maho-vault-9F4C";
  result.links = {
      "https://example.test/link?access_token=S3NTINEL-maho-vault-9F4C&keep=1"};
  result.extraction_warnings = {"token=S3NTINEL-maho-vault-9F4C"};

  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(result.ToValue(), &serialized));

  EXPECT_EQ(serialized.find(kSentinel), std::string::npos);
  EXPECT_NE(serialized.find("ordinary title"), std::string::npos);
  EXPECT_NE(serialized.find("ordinary marker"), std::string::npos);
}

TEST(MahoAiEgressRedactionTest,
      PersistedRuntimeEventRedactsNestedToolOutputAndPreservesIdentifiers) {
  auto event = maho_ai::mojom::RuntimeEvent::New();
  event->kind = maho_ai::mojom::RuntimeEventKind::kToolResult;
  event->session_id = "ses_ordinary_123";
  event->request_id = "req_ordinary_456";
  event->sequence = 7;
  event->timestamp = 9;
  event->tool_result = maho_ai::mojom::ToolResultInfo::New();
  event->tool_result->call_id = "call_ordinary_789";
  event->tool_result->success = true;
  event->tool_result->output =
      R"({"nested":{"password":"S3NTINEL-maho-vault-9F4C"},"headers":[{"name":"Authorization","value":"Basic S3NTINEL-maho-vault-9F4C"}],"marker":"ordinary-marker"})";

  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(
      base::Value(SerializeRuntimeEventForPersistence(*event)), &serialized));

  EXPECT_EQ(serialized.find(kSentinel), std::string::npos);
  EXPECT_NE(serialized.find("ordinary-marker"), std::string::npos);
  EXPECT_NE(serialized.find("ses_ordinary_123"), std::string::npos);
  EXPECT_NE(serialized.find("req_ordinary_456"), std::string::npos);
  EXPECT_NE(serialized.find("call_ordinary_789"), std::string::npos);
}

TEST(MahoAiEgressRedactionTest,
     PersistedCredentialFailureStoresOnlyAllowlistedSymbolAndSafeText) {
  auto event = maho_ai::mojom::RuntimeEvent::New();
  event->kind = maho_ai::mojom::RuntimeEventKind::kError;
  event->session_id = "session-credential";
  event->sequence = 8;
  event->timestamp = 10;
  event->text = "S3NTINEL-raw-credential-diagnostic";
  event->credential_error_code =
      maho_ai::mojom::CredentialErrorCode::kSecureStoreUnavailable;

  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(
      base::Value(SerializeRuntimeEventForPersistence(*event)), &serialized));

  EXPECT_EQ(serialized.find("S3NTINEL-raw-credential-diagnostic"),
            std::string::npos);
  EXPECT_NE(serialized.find("secure_store_unavailable"), std::string::npos);
  EXPECT_NE(serialized.find("Your saved AI credential could not be used."),
            std::string::npos);
}

}  // namespace
}  // namespace maho::ai
