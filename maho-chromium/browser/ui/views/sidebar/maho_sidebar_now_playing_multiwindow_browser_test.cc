// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <vector>

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/media_start_stop_observer.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

class MahoNowPlayingMultiWindowBrowserTest : public InProcessBrowserTest {
 public:
  MahoNowPlayingMultiWindowBrowserTest() = default;
  ~MahoNowPlayingMultiWindowBrowserTest() override = default;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  content::WebContents* GetActiveWebContents(Browser* browser) {
    return browser->tab_strip_model()->GetActiveWebContents();
  }

  void StartPlayback(Browser* browser) {
    GetActiveWebContents(browser)->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"play()", base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void WaitForStart(Browser* browser) {
    content::MediaStartStopObserver observer(
        GetActiveWebContents(browser), content::MediaStartStopObserver::Type::kStart);
    observer.Wait();
  }
};

IN_PROC_BROWSER_TEST_F(MahoNowPlayingMultiWindowBrowserTest, FiveWindowsCoexistAndIsolate) {
  Browser* r1 = browser();
  Browser* r2 = CreateBrowser(r1->profile());
  Browser* r3 = CreateBrowser(r1->profile());

  Profile* otr_profile = r1->profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  Browser* i1 = CreateBrowser(otr_profile);
  Browser* i2 = CreateBrowser(otr_profile);

  base::RunLoop().RunUntilIdle();

  auto* reg_coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(r1->profile());
  auto* otr_coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(otr_profile);

  ASSERT_TRUE(reg_coordinator);
  ASSERT_TRUE(otr_coordinator);
  EXPECT_NE(reg_coordinator, otr_coordinator);

  EXPECT_EQ(3u, reg_coordinator->GetHostsForTesting().size());
  EXPECT_EQ(2u, otr_coordinator->GetHostsForTesting().size());

  CloseBrowserSynchronously(r3);
  CloseBrowserSynchronously(i2);

  EXPECT_EQ(2u, reg_coordinator->GetHostsForTesting().size());
  EXPECT_EQ(1u, otr_coordinator->GetHostsForTesting().size());
}

IN_PROC_BROWSER_TEST_F(MahoNowPlayingMultiWindowBrowserTest, SourceWindowRoutingAndTabMove) {
  Browser* r1 = browser();
  Browser* r2 = CreateBrowser(r1->profile());

  base::RunLoop().RunUntilIdle();

  auto* reg_coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(r1->profile());
  ASSERT_TRUE(reg_coordinator);
  ASSERT_EQ(2u, reg_coordinator->GetHostsForTesting().size());

  // Load media page in r2
  GURL url = embedded_test_server()->GetURL("/media/session/video-with-metadata.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(r2, url));

  // Initially active tab does not show Now Playing view on itself (it is hidden when tab is active)
  // Let's open a new tab so the video tab becomes background tab in r2, which allows the card to show
  chrome::AddTabAt(r2, GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Start playback
  // Note: Tab strip active tab is index 1. Video tab is index 0.
  content::WebContents* video_contents = r2->tab_strip_model()->GetWebContentsAt(0);
  video_contents->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
      u"play()", base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  
  content::MediaStartStopObserver observer(video_contents, content::MediaStartStopObserver::Type::kStart);
  observer.Wait();
  base::RunLoop().RunUntilIdle();

  // Find host for r2
  MahoNowPlayingCardHost* host_r1 = nullptr;
  MahoNowPlayingCardHost* host_r2 = nullptr;
  for (const auto& host : reg_coordinator->GetHostsForTesting()) {
    if (host->GetBrowser() == r1) {
      host_r1 = host.get();
    } else if (host->GetBrowser() == r2) {
      host_r2 = host.get();
    }
  }
  ASSERT_TRUE(host_r1);
  ASSERT_TRUE(host_r2);

  // Card should show up in r2 (since it's background tab in r2), and not in r1
  EXPECT_TRUE(host_r2->HasCard());
  EXPECT_FALSE(host_r1->HasCard());

  // Move the video tab from r2 (index 0) to r1 (target background)
  // Add a blank tab in r1 to keep it background when moved
  chrome::AddTabAt(r1, GURL("about:blank"), -1, true);
  chrome::MoveTabsToExistingWindow(r2, r1, {0});
  base::RunLoop().RunUntilIdle();

  // Verify that the card reparented to r1 and no longer exists in r2
  EXPECT_TRUE(host_r1->HasCard());
  EXPECT_FALSE(host_r2->HasCard());
}

}  // namespace maho
