// Copyright 2026 Maho Browser. All rights reserved.

#import <AppKit/AppKit.h>

#import "chrome/browser/app_controller_mac.h"

#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "net/base/apple/url_conversions.h"
#include "url/gurl.h"

namespace {

constexpr char kNativeUrl[] = "https://example.test/";

class MahoLoginGateNativeUrlBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    ProfileManager* profile_manager = g_browser_process->profile_manager();
    ASSERT_TRUE(profile_manager);
    profile_ = &profiles::testing::CreateProfileSync(
        profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
    ASSERT_TRUE(profile_);

    // Exercise an ordinary Browser lifecycle first, then retain this loaded
    // regular profile as the AppController destination without a Browser.
    Browser* fixture_browser = CreateBrowser(profile_);
    ASSERT_TRUE(fixture_browser);
    std::ignore = AppController.sharedController;
    [AppController.sharedController setLastProfile:profile_];
    CloseBrowserSynchronously(fixture_browser);
    EXPECT_TRUE(GlobalBrowserCollection::GetInstance()->IsEmpty());

    ASSERT_EQ(profile_, [AppController.sharedController lastProfileIfLoaded]);
    profile_->GetPrefs()->SetBoolean(maho::welcome::kLoginGateActive, true);
  }

  size_t BrowserCount() const {
    return GlobalBrowserCollection::GetInstance()->GetSize();
  }

  void InjectNativeUrl() {
    GURL url(kNativeUrl);
    ASSERT_TRUE(url.is_valid());
    id<NSApplicationDelegate> delegate = NSApp.delegate;
    ASSERT_EQ(AppController.sharedController, delegate);
    ASSERT_TRUE([delegate respondsToSelector:@selector(application:openURLs:)]);
    [delegate application:NSApp openURLs:@[ net::NSURLWithGURL(url) ]];
  }

  raw_ptr<Profile> profile_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(MahoLoginGateNativeUrlBrowserTest,
                       GatedNativeUrlDoesNotCreateBrowser) {
  const size_t browser_count_before_injection = BrowserCount();
  EXPECT_EQ(0u, browser_count_before_injection);

  InjectNativeUrl();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(browser_count_before_injection, BrowserCount());
  EXPECT_EQ(std::vector<GURL>{GURL(kNativeUrl)},
            app_controller_mac::GetQueuedNativeUrlsForTesting(profile_));
}

IN_PROC_BROWSER_TEST_F(MahoLoginGateNativeUrlBrowserTest,
                       GatedNativeUrlDrainsAfterSuccessfulCompletion) {
  const size_t browser_count_before_injection = BrowserCount();

  InjectNativeUrl();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(browser_count_before_injection, BrowserCount())
      << "A gated native URL must not create a Browser before completion.";

  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);
  EXPECT_EQ(1u,
            app_controller_mac::GetQueuedNativeUrlCountForTesting(profile_));

  profile_->GetPrefs()->SetBoolean(maho::welcome::kLoginGateActive, false);
  ui_test_utils::BrowserCreatedObserver browser_created_observer;
  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);
  EXPECT_TRUE(browser_created_observer.Wait());
  EXPECT_EQ(0u,
            app_controller_mac::GetQueuedNativeUrlCountForTesting(profile_));

  base::RunLoop().RunUntilIdle();
  const size_t browser_count_after_first_drain = BrowserCount();
  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(browser_count_after_first_drain, BrowserCount());
}

IN_PROC_BROWSER_TEST_F(MahoLoginGateNativeUrlBrowserTest,
                       QueuedNativeUrlsRemainWhileGateIsActive) {
  // Given: an active login gate and a regular profile with no queued URLs.
  const std::vector<GURL> first_batch = {
      GURL("https://example.test/first"),
      GURL("https://example.test/second"),
  };
  const std::vector<GURL> second_batch = {
      GURL("https://example.test/third"),
  };
  ASSERT_TRUE(first_batch[0].is_valid());
  ASSERT_TRUE(first_batch[1].is_valid());
  ASSERT_TRUE(second_batch[0].is_valid());
  EXPECT_EQ(0u,
            app_controller_mac::GetQueuedNativeUrlCountForTesting(profile_));

  // When: native URLs are explicitly retained for this profile.
  app_controller_mac::QueueNativeUrlsWhileMahoLoginGated(profile_, first_batch);
  app_controller_mac::QueueNativeUrlsWhileMahoLoginGated(profile_, second_batch);
  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);

  // Then: all FIFO entries remain queued until a later Todo owns dispatch.
  const std::vector<GURL> expected_urls = {
      first_batch[0],
      first_batch[1],
      second_batch[0],
  };
  EXPECT_EQ(expected_urls,
            app_controller_mac::GetQueuedNativeUrlsForTesting(profile_));
  EXPECT_EQ(expected_urls.size(),
            app_controller_mac::GetQueuedNativeUrlCountForTesting(profile_));
}

IN_PROC_BROWSER_TEST_F(MahoLoginGateNativeUrlBrowserTest,
                       QueuedNativeUrlsRemainProfileIsolated) {
  // Given: two distinct regular profiles, both with an active login gate.
  Profile& profile_b = profiles::testing::CreateProfileSync(
      g_browser_process->profile_manager(),
      g_browser_process->profile_manager()->GenerateNextProfileDirectoryPath());
  profile_b.GetPrefs()->SetBoolean(maho::welcome::kLoginGateActive, true);
  const std::vector<GURL> profile_a_urls = {
      GURL("https://example.test/profile-a"),
  };
  const std::vector<GURL> profile_b_urls = {
      GURL("https://example.test/profile-b-first"),
      GURL("https://example.test/profile-b-second"),
  };

  // When: profile A receives a real native URL, then both profiles receive
  // additional FIFO entries while gated.
  InjectNativeUrl();
  base::RunLoop().RunUntilIdle();
  app_controller_mac::QueueNativeUrlsWhileMahoLoginGated(profile_,
                                                           profile_a_urls);
  app_controller_mac::QueueNativeUrlsWhileMahoLoginGated(&profile_b,
                                                            profile_b_urls);
  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);

  // Then: gate-active selection cannot take profile A or profile B entries.
  const std::vector<GURL> expected_profile_a_urls = {
      GURL(kNativeUrl),
      profile_a_urls[0],
  };
  EXPECT_EQ(expected_profile_a_urls,
            app_controller_mac::GetQueuedNativeUrlsForTesting(profile_));
  EXPECT_EQ(profile_b_urls,
            app_controller_mac::GetQueuedNativeUrlsForTesting(&profile_b));

  // When: releasing profile A's login gate and draining profile A...
  profile_->GetPrefs()->SetBoolean(maho::welcome::kLoginGateActive, false);
  ui_test_utils::BrowserCreatedObserver browser_created_observer;
  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);
  EXPECT_TRUE(browser_created_observer.Wait());

  // Then: profile A's queue is consumed while profile B's FIFO queued URLs
  // remain exactly unchanged.
  EXPECT_EQ(0u,
            app_controller_mac::GetQueuedNativeUrlCountForTesting(profile_));
  EXPECT_EQ(profile_b_urls,
            app_controller_mac::GetQueuedNativeUrlsForTesting(&profile_b));
}

}  // namespace
