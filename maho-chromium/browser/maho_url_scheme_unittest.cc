// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_url_scheme.h"

#include <algorithm>

#include "maho/components/constants/url_constants.h"
#include "maho/components/constants/webui_url_constants.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/browser_url_handler.h"
#include "content/public/common/origin_util.h"
#include "content/public/common/url_utils.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/scheme_registry.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace maho {

class MahoUrlSchemeMappingTest : public ::testing::Test {};
class MahoUrlSchemeCanonicalEquivalenceTest : public ::testing::Test {};
class MahoUrlSchemeValidationTest : public ::testing::Test {};
class MahoUrlSchemeHandlerTest : public ::testing::Test {};

namespace {

bool MahoRewriteNewTabToBlankForTest(GURL* url,
                                       content::BrowserContext* /*context*/) {
  if (url->SchemeIs("chrome") && url->host() == "newtab") {
    *url = GURL("about:blank");
    return true;
  }
  return false;
}

bool MahoUrlAliasForwardHandlerForTest(GURL* url,
                                       content::BrowserContext* /*context*/) {
  return MapMahoUrlAliasToActualUrl(*url, url);
}

bool MahoUrlAliasReverseHandlerForTest(GURL* url,
                                       content::BrowserContext* /*context*/) {
  GURL alias;
  if (ResolveActualUrlToMahoAlias(*url, &alias)) {
    *url = alias;
  }
  return true;
}

}  // namespace

TEST_F(MahoUrlSchemeMappingTest, MapsAllNinePublicAliasesWithSubpaths) {
  for (const auto& record : kMahoUrlAliases) {
    std::string public_str = std::string("maho://") + record.alias_host + "/path?x=1#frag";
    GURL public_url(public_str);

    EXPECT_TRUE(IsValidMahoUrlAlias(public_url)) << "Failed for: " << public_str;

    GURL mapped_actual;
    EXPECT_TRUE(MapMahoUrlAliasToActualUrl(public_url, &mapped_actual))
        << "Failed mapping for: " << public_str;

    std::string expected_actual_str =
        std::string("chrome://") + record.actual_host + "/path?x=1#frag";
    EXPECT_EQ(mapped_actual.spec(), expected_actual_str);

    GURL resolved_alias;
    EXPECT_TRUE(ResolveActualUrlToMahoAlias(mapped_actual, &resolved_alias))
        << "Failed resolving for: " << expected_actual_str;
    EXPECT_EQ(resolved_alias.spec(), public_url.spec());
  }
}

TEST_F(MahoUrlSchemeCanonicalEquivalenceTest, AcceptsUppercaseAndCanonicalEquivalents) {
  // Uppercase scheme and host
  GURL upper_url("MAHO://SETTINGS/path?x=1#frag");
  EXPECT_TRUE(IsValidMahoUrlAlias(upper_url));

  GURL mapped;
  EXPECT_TRUE(MapMahoUrlAliasToActualUrl(upper_url, &mapped));
  EXPECT_EQ(mapped.spec(), "chrome://maho-settings/path?x=1#frag");
}

TEST_F(MahoUrlSchemeValidationTest, RejectsInvalidAndHostileUrls) {
  const std::string initial_spec =
      GURL("https://initial.state.should.not.be.mutated.com").spec();
  const auto expect_rejected = [&](const GURL& rejected_url) {
    GURL mapped(initial_spec);
    EXPECT_FALSE(IsValidMahoUrlAlias(rejected_url))
        << rejected_url.possibly_invalid_spec();
    EXPECT_FALSE(MapMahoUrlAliasToActualUrl(rejected_url, &mapped))
        << rejected_url.possibly_invalid_spec();
    EXPECT_EQ(mapped.spec(), initial_spec);
  };

  // Trailing dot in host (settings. vs settings)
  expect_rejected(GURL("maho://settings./"));

  // Lookalike/different canonical host
  expect_rejected(GURL("maho://settings.evil.com/"));

  // Empty authority / host
  expect_rejected(GURL("maho:///"));

  // maho://maho-settings (using actual host as alias host)
  expect_rejected(GURL("maho://maho-settings/"));

  // maho://test / unlisted host
  expect_rejected(GURL("maho://test/"));

  // Malformed URL
  expect_rejected(GURL("maho://"));

  // Reverse mapping rejects unmapped actual destination and does not mutate output
  GURL unmapped_actual("chrome://maho-test/");
  GURL resolved(initial_spec);
  EXPECT_FALSE(ResolveActualUrlToMahoAlias(unmapped_actual, &resolved));
  EXPECT_EQ(resolved.spec(), initial_spec);

  GURL non_maho_actual("chrome://settings/");
  EXPECT_FALSE(ResolveActualUrlToMahoAlias(non_maho_actual, &resolved));
  EXPECT_EQ(resolved.spec(), initial_spec);
}

TEST_F(MahoUrlSchemeHandlerTest, ApprovedForwardAndReverseCallbacks) {
  GURL newtab("chrome://newtab");
  EXPECT_TRUE(MahoRewriteNewTabToBlankForTest(&newtab, nullptr));
  EXPECT_EQ("about:blank", newtab.spec());

  GURL actual("maho://settings/path?x=1#fragment");
  EXPECT_TRUE(MahoUrlAliasForwardHandlerForTest(&actual, nullptr));
  EXPECT_EQ("chrome://maho-settings/path?x=1#fragment", actual.spec());

  EXPECT_TRUE(MahoUrlAliasReverseHandlerForTest(&actual, nullptr));
  EXPECT_EQ("maho://settings/path?x=1#fragment", actual.spec());
}

TEST_F(MahoUrlSchemeHandlerTest, ExternalOrUnlistedRedirectClearsAlias) {
  GURL original("maho://settings/");
  ASSERT_TRUE(MahoUrlAliasForwardHandlerForTest(&original, nullptr));

  GURL external("https://example.test/final");
  EXPECT_TRUE(MahoUrlAliasReverseHandlerForTest(&external, nullptr));
  EXPECT_EQ("https://example.test/final", external.spec());

  GURL unlisted("chrome://maho-test/final");
  EXPECT_TRUE(MahoUrlAliasReverseHandlerForTest(&unlisted, nullptr));
  EXPECT_EQ("chrome://maho-test/final", unlisted.spec());
}

TEST_F(MahoUrlSchemeHandlerTest, DirectLegacyBypassesPair) {
  GURL legacy("chrome://maho-settings/path?x=1#fragment");
  EXPECT_FALSE(MahoUrlAliasForwardHandlerForTest(&legacy, nullptr));
  EXPECT_EQ("chrome://maho-settings/path?x=1#fragment", legacy.spec());
}

TEST_F(MahoUrlSchemeHandlerTest, InvalidAliasRemainsUnmapped) {
  GURL invalid("maho://settings.evil/");
  EXPECT_FALSE(MahoUrlAliasForwardHandlerForTest(&invalid, nullptr));
  EXPECT_EQ("maho://settings.evil/", invalid.spec());
}

class MahoUrlSchemeNavigationTest : public ::testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler rvh_test_enabler_;
  TestingProfile browser_context_;
  std::unique_ptr<content::WebContents> web_contents_ =
      content::WebContentsTester::CreateTestWebContents(&browser_context_,
                                                         nullptr);
};

TEST_F(MahoUrlSchemeNavigationTest, ApprovedRedirectPreservesFinalAlias) {
  GURL actual("maho://settings/");
  ASSERT_TRUE(MahoUrlAliasForwardHandlerForTest(&actual, &browser_context_));

  auto navigation = content::NavigationSimulator::CreateBrowserInitiated(
      actual, web_contents_.get());
  navigation->Start();
  navigation->Redirect(GURL("chrome://maho-ai/final"));
  navigation->Commit();

  GURL virtual_url("chrome://maho-ai/final");
  EXPECT_TRUE(
      MahoUrlAliasReverseHandlerForTest(&virtual_url, &browser_context_));
  EXPECT_EQ("maho://ai/final", virtual_url.spec());
}

TEST_F(MahoUrlSchemeNavigationTest, ExternalOrUnlistedRedirectClearsAlias) {
  GURL actual("maho://settings/");
  ASSERT_TRUE(MahoUrlAliasForwardHandlerForTest(&actual, &browser_context_));

  auto external_navigation = content::NavigationSimulator::CreateBrowserInitiated(
      actual, web_contents_.get());
  external_navigation->Start();
  external_navigation->Redirect(GURL("https://example.test/final"));
  external_navigation->Commit();

  GURL external_virtual("https://example.test/final");
  EXPECT_TRUE(MahoUrlAliasReverseHandlerForTest(&external_virtual,
                                                &browser_context_));
  EXPECT_EQ("https://example.test/final", external_virtual.spec());

  auto unlisted_navigation = content::NavigationSimulator::CreateBrowserInitiated(
      actual, web_contents_.get());
  unlisted_navigation->Start();
  unlisted_navigation->Redirect(GURL("chrome://maho-test/final"));
  unlisted_navigation->Commit();

  GURL unlisted_virtual("chrome://maho-test/final");
  EXPECT_TRUE(MahoUrlAliasReverseHandlerForTest(&unlisted_virtual,
                                                &browser_context_));
  EXPECT_EQ("chrome://maho-test/final", unlisted_virtual.spec());
}

}  // namespace maho

namespace maho {

class MahoUrlSchemeRegistrationTest : public ::testing::Test {};
class MahoUrlSchemeClassifierTest : public ::testing::Test {};

TEST_F(MahoUrlSchemeRegistrationTest, VerifiesSchemeAndIsolationProperties) {
  EXPECT_TRUE(url::IsStandard(kMahoUIScheme));
  EXPECT_TRUE(GURL("maho://settings/").IsStandard());

  EXPECT_TRUE(ProfileIOData::IsHandledProtocol(kMahoUIScheme));

  EXPECT_FALSE(std::ranges::contains(url::GetSecureSchemes(), kMahoUIScheme));
  EXPECT_FALSE(std::ranges::contains(url::GetLocalSchemes(), kMahoUIScheme));
  EXPECT_FALSE(
      std::ranges::contains(url::GetNoAccessSchemes(), kMahoUIScheme));
  EXPECT_FALSE(
      std::ranges::contains(url::GetCorsEnabledSchemes(), kMahoUIScheme));
  EXPECT_FALSE(
      std::ranges::contains(url::GetWebStorageSchemes(), kMahoUIScheme));
  EXPECT_FALSE(
      std::ranges::contains(url::GetCSPBypassingSchemes(), kMahoUIScheme));
  EXPECT_FALSE(
      std::ranges::contains(url::GetEmptyDocumentSchemes(), kMahoUIScheme));
  EXPECT_FALSE(url::IsReferrerScheme(kMahoUIScheme));
  EXPECT_FALSE(blink::CommonSchemeRegistry::IsExtensionScheme(kMahoUIScheme));
  EXPECT_FALSE(
      blink::CommonSchemeRegistry::IsIsolatedAppScheme(kMahoUIScheme));
  const auto predefined_handler_schemes = url::GetPredefinedHandlerSchemes();
  EXPECT_EQ(std::ranges::find_if(predefined_handler_schemes,
                                 [](const auto& handler) {
                                   return handler.first == kMahoUIScheme;
                                 }),
            predefined_handler_schemes.end());

  GURL sample_url("maho://settings/");
  EXPECT_FALSE(content::IsSavableURL(sample_url));
  EXPECT_FALSE(content::OriginCanAccessServiceWorkers(sample_url));

  GURL unknown_maho("maho://not-listed/");
  ASSERT_TRUE(unknown_maho.is_valid());
  EXPECT_TRUE(unknown_maho.SchemeIs(kMahoUIScheme));
  EXPECT_TRUE(ProfileIOData::IsHandledProtocol(unknown_maho.GetScheme()));
  EXPECT_FALSE(IsValidMahoUrlAlias(unknown_maho));
  const std::string initial_spec =
      GURL("https://initial.state.should.not.be.mutated.com").spec();
  GURL mapped_unknown(initial_spec);
  EXPECT_FALSE(MapMahoUrlAliasToActualUrl(unknown_maho, &mapped_unknown));
  EXPECT_EQ(mapped_unknown.spec(), initial_spec);
}

TEST_F(MahoUrlSchemeClassifierTest, ClassifiesMahoSchemeAsUrl) {
  ChromeAutocompleteSchemeClassifier classifier(nullptr);

  EXPECT_EQ(classifier.GetInputTypeForScheme(kMahoUIScheme),
            metrics::OmniboxInputType::URL);

  GURL unknown_maho("maho://not-listed/");
  EXPECT_EQ(classifier.GetInputTypeForScheme(unknown_maho.GetScheme()),
            metrics::OmniboxInputType::URL);
}

}  // namespace maho
