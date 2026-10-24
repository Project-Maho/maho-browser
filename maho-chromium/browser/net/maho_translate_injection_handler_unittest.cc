// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_translate_injection_handler.h"

#include <string>
#include <utility>

#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {

class MahoTranslateInjectionHandlerTest
    : public content::RenderViewHostTestHarness {
 protected:
  std::unique_ptr<content::BrowserContext> CreateBrowserContext() override {
    return TestingProfile::Builder().Build();
  }

  MahoTranslateTabHelper* CreateHelperAndNavigate(const GURL& url) {
    MahoTranslateTabHelper::CreateForWebContents(web_contents());
    content::WebContentsTester::For(web_contents())->NavigateAndCommit(url);
    return MahoTranslateTabHelper::FromWebContents(web_contents());
  }

  // The state machine's transition points are private; the fixture is a
  // friend so the tests drive SetState() exactly the way the production
  // callers do (request dispatch and the content script's state messages).
  static void SetHelperState(MahoTranslateTabHelper* helper,
                             MahoTranslateState state,
                             std::string source_language = std::string()) {
    helper->SetState(state, std::move(source_language));
  }
};

// CS-R2-01 guard: a failure reported after the swap leaves the translated
// document on screen, so the restore affordance must survive kError.
TEST_F(MahoTranslateInjectionHandlerTest, PostSwapErrorKeepsTranslatedContent) {
  MahoTranslateTabHelper* helper =
      CreateHelperAndNavigate(GURL("https://a.example/"));
  ASSERT_NE(helper, nullptr);

  SetHelperState(helper, MahoTranslateState::kTranslating);
  EXPECT_FALSE(helper->has_translated_content());
  SetHelperState(helper, MahoTranslateState::kTranslated, "fr");
  EXPECT_TRUE(helper->has_translated_content());

  // A dynamic batch fails after the swap: the page still shows the
  // translation, so the flag and the public predicate stay set.
  SetHelperState(helper, MahoTranslateState::kError);
  EXPECT_TRUE(helper->has_translated_content());
  EXPECT_TRUE(HasMahoTranslatedContent(web_contents()));
}

// CS-R3-01: every new request restores the original text before it swaps
// (content_script.ts restoreUnits() runs before report('translating')), so
// kTranslating must clear the latch; a retry that then fails ends with the
// original on screen and the UI must not claim the page is translated.
TEST_F(MahoTranslateInjectionHandlerTest, NewRequestClearsTranslatedContent) {
  MahoTranslateTabHelper* helper =
      CreateHelperAndNavigate(GURL("https://a.example/"));
  ASSERT_NE(helper, nullptr);

  SetHelperState(helper, MahoTranslateState::kTranslated, "fr");
  ASSERT_TRUE(helper->has_translated_content());

  // A re-target or retry begins: the script restored the original text, so
  // the document no longer shows translated content from this point on.
  SetHelperState(helper, MahoTranslateState::kTranslating);
  EXPECT_EQ(helper->state(), MahoTranslateState::kTranslating);
  EXPECT_FALSE(helper->has_translated_content());

  // The retry fails: the page shows the original text, and the restore
  // affordance must go away instead of offering a no-op "Show original".
  SetHelperState(helper, MahoTranslateState::kError);
  EXPECT_FALSE(helper->has_translated_content());
  EXPECT_FALSE(HasMahoTranslatedContent(web_contents()));
}

// A cross-document commit drops the old document's translated text with it.
TEST_F(MahoTranslateInjectionHandlerTest,
       CrossDocumentCommitClearsTranslatedContent) {
  MahoTranslateTabHelper* helper =
      CreateHelperAndNavigate(GURL("https://a.example/"));
  ASSERT_NE(helper, nullptr);

  SetHelperState(helper, MahoTranslateState::kTranslated, "fr");
  ASSERT_TRUE(helper->has_translated_content());

  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://b.example/"));
  EXPECT_FALSE(helper->has_translated_content());
  EXPECT_EQ(helper->state(), MahoTranslateState::kIdle);
  EXPECT_FALSE(HasMahoTranslatedContent(web_contents()));
}

}  // namespace maho
