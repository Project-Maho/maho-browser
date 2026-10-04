// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/media_start_stop_observer.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/widget/widget.h"

namespace maho {

class MahoSidebarNowPlayingInteractiveTest : public MahoSidebarInteractiveTestBase {
 public:
  MahoSidebarNowPlayingInteractiveTest() = default;
  ~MahoSidebarNowPlayingInteractiveTest() override = default;

  void SetUpOnMainThread() override {
    MahoSidebarInteractiveTestBase::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};

IN_PROC_BROWSER_TEST_F(MahoSidebarNowPlayingInteractiveTest, DelayedCreationAndRouting) {
  ResolveViews();
  ASSERT_TRUE(container_);

  // Initially, before running run loop, registration should be deferred or completed
  base::RunLoop().RunUntilIdle();

  auto* reg_coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(reg_coordinator);

  // We should have at least 1 host registered (the default browser window)
  EXPECT_EQ(1u, reg_coordinator->GetHostsForTesting().size());

  // Let's create a second browser window
  Browser* browser2 = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  base::RunLoop().RunUntilIdle();

  // Now we should have 2 hosts registered
  EXPECT_EQ(2u, reg_coordinator->GetHostsForTesting().size());

  // Load media page in browser2
  GURL url = embedded_test_server()->GetURL("/media/session/video-with-metadata.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser2, url));

  // Add a blank tab in browser2 so video is in background tab
  chrome::AddTabAt(browser2, GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Start playback in background tab
  content::WebContents* video_contents = browser2->GetTabStripModel()->GetWebContentsAt(0);
  video_contents->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
      u"play()", base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);

  content::MediaStartStopObserver observer(video_contents, content::MediaStartStopObserver::Type::kStart);
  observer.Wait();
  base::RunLoop().RunUntilIdle();

  // Find host for browser2
  MahoNowPlayingCardHost* host2 = nullptr;
  for (const auto& host : reg_coordinator->GetHostsForTesting()) {
    if (host->GetBrowser() == browser2) {
      host2 = host.get();
      break;
    }
  }
  ASSERT_TRUE(host2);

  // Verification: card should show up in host2
  EXPECT_TRUE(host2->HasCard());
}

}  // namespace maho
