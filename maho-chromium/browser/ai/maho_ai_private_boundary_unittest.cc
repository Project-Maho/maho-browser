// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/web_contents_tester.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/ai/maho_model_list_fetcher.h"
#include "maho/browser/ai/maho_tab_tidy_orchestrator.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_handler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Builds a kAI gate bound to a shared, exact-primary-Incognito token. The gate
// evaluates the real MahoPrivateContextToken::Revalidate(kAI) contract, which
// is false for every OTR/non-regular class.
base::RepeatingCallback<bool()> MakeTokenGate(
    std::shared_ptr<MahoPrivateContextToken> token) {
  return base::BindRepeating(
      [](std::shared_ptr<MahoPrivateContextToken> t) {
        return t->Revalidate(MahoPrivateCapability::kAI);
      },
      std::move(token));
}

class MahoAiPrivateBoundaryTestBase : public ChromeRenderViewHostTestHarness {
 protected:
  void TearDown() override {
    otr_contents_.reset();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  std::shared_ptr<MahoPrivateContextToken> MakeIncognitoToken() {
    Profile* incognito = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
    otr_contents_ =
        content::WebContentsTester::CreateTestWebContents(incognito, nullptr);
    return std::make_shared<MahoPrivateContextToken>(
        /*browser_window=*/nullptr, otr_contents_.get());
  }

  std::unique_ptr<content::WebContents> otr_contents_;
};

using MahoAiModelListFetcherTest = MahoAiPrivateBoundaryTestBase;
using MahoTabTidyOrchestratorTest = MahoAiPrivateBoundaryTestBase;
using MahoInlineEditHandlerTest = MahoAiPrivateBoundaryTestBase;
using MahoArtifactRegistryPrivateBoundaryTest = MahoAiPrivateBoundaryTestBase;

TEST_F(MahoAiModelListFetcherTest, OtrTokenStopsBeforeRequest) {
  auto token = MakeIncognitoToken();
  ASSERT_FALSE(token->Revalidate(MahoPrivateCapability::kAI));

  maho::ai::MahoModelListFetcher fetcher(profile()->GetPrefs(),
                                         /*url_loader_factory=*/nullptr,
                                         MakeTokenGate(token));

  bool called = false;
  std::vector<std::string> returned_models;
  fetcher.FetchModels(
      "openai",
      base::BindOnce(
          [](bool* called, std::vector<std::string>* out,
             const std::vector<std::string>& models, bool /*from_cache*/) {
            *called = true;
            *out = models;
          },
          &called, &returned_models));

  // Denial completes synchronously with an empty list and never reaches
  // PerformFetch / the network.
  EXPECT_TRUE(called);
  EXPECT_TRUE(returned_models.empty());
}

TEST_F(MahoArtifactRegistryPrivateBoundaryTest,
       IncognitoProfileHasNoArtifactRegistry) {
  // The artifact registry is regular-profile-only. An incognito/OTR profile
  // gets no service instance, so every mojo artifact handler method fails
  // closed (null registry) and no artifact metadata is ever exposed there.
  Profile* incognito =
      profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  EXPECT_EQ(maho::ai::MahoArtifactRegistry::GetForProfile(incognito), nullptr);
  EXPECT_NE(maho::ai::MahoArtifactRegistry::GetForProfile(profile()), nullptr);
}

TEST_F(MahoTabTidyOrchestratorTest, OtrTokenStopsBeforeClient) {
  auto token = MakeIncognitoToken();
  ASSERT_FALSE(token->Revalidate(MahoPrivateCapability::kAI));

  int folder_count = -1;
  std::string error;
  maho::MahoTabTidyOrchestrator::RunOneShot(
      /*browser=*/nullptr,
      base::BindOnce(
          [](int* count, std::string* err, int folders,
             const std::string& message) {
            *count = folders;
            *err = message;
          },
          &folder_count, &error),
      MakeTokenGate(token));

  // The denying gate stops the run before any MahoCore/LLM client is created.
  EXPECT_EQ(folder_count, 0);
  EXPECT_FALSE(error.empty());
}

TEST_F(MahoInlineEditHandlerTest, ForgedOtrHandlerStopsBeforeCoreAndClient) {
  profile()->GetPrefs()->SetString(maho::ai_prefs::kProvider, "");

  auto token = MakeIncognitoToken();
  ASSERT_FALSE(token->Revalidate(MahoPrivateCapability::kAI));

  // A forged OTR handler: token is denied, so no browser/tab is required —
  // every method must fail closed before core, JavaScript, or LLM client use.
  auto handler = std::make_unique<MahoInlineEditHandler>(
      std::make_unique<MahoPrivateContextToken>(
          /*browser_window=*/nullptr, otr_contents_.get()),
      /*browser=*/nullptr, profile()->GetPrefs(),
      /*url_loader_factory=*/nullptr);

  std::string selected = "unset";
  handler->GetSelectedText(base::BindOnce(
      [](std::string* out, const std::string& text) { *out = text; },
      &selected));
  EXPECT_EQ(selected, "");

  std::string edit_result = "unset";
  handler->RequestEdit(
      "some text", "make it better",
      base::BindOnce(
          [](std::string* out, const std::string& result) { *out = result; },
          &edit_result));
  EXPECT_EQ(edit_result, "");
}

TEST(MahoAiPageContextRedactionTest, ToValueRedactsEveryTextualEgressField) {
  MahoAiPageContextExtractor::PageContextResult result;
  result.extraction_status =
      MahoAiPageContextExtractor::PageContextResult::Status::kSuccess;
  result.title = "ordinary title";
  result.url =
      "https://example.test/page?token=S3NTINEL-url&keep=ordinary";
  result.selected_text = "Authorization: Basic S3NTINEL-auth";
  result.main_text = "password=S3NTINEL-password ordinary marker";
  result.headings = {"Bearer S3NTINEL-bearer"};
  result.meta_description = "Cookie: session=S3NTINEL-cookie";
  result.links = {
      "https://example.test/link?access_token=S3NTINEL-link&keep=1"};
  result.extraction_warnings = {"token=S3NTINEL-warning"};

  const std::string serialized = result.ToValue().DebugString();

  EXPECT_EQ(serialized.find("S3NTINEL-"), std::string::npos);
  EXPECT_NE(serialized.find("ordinary title"), std::string::npos);
  EXPECT_NE(serialized.find("ordinary marker"), std::string::npos);
}

}  // namespace
