// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/media_start_stop_observer.h"
#include "maho/browser/ui/views/maho_action_marker_service.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view_observer.h"
#include "url/gurl.h"

namespace maho {

class WebContentsDestructionSignal : public content::WebContentsObserver {
 public:
  explicit WebContentsDestructionSignal(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}

  void WebContentsDestroyed() override { destroyed_.SetValue(); }
  bool Wait() { return destroyed_.Wait(); }

 private:
  base::test::TestFuture<void> destroyed_;
};

class WebContentsVisibilitySignal : public content::WebContentsObserver {
 public:
  WebContentsVisibilitySignal(content::WebContents* contents,
                              content::Visibility expected_visibility)
      : content::WebContentsObserver(contents),
        expected_visibility_(expected_visibility) {}

  bool Wait() {
    if (web_contents() &&
        web_contents()->GetVisibility() == expected_visibility_) {
      return true;
    }
    return reached_.Wait();
  }

  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == expected_visibility_ && !reached_.IsReady()) {
      reached_.SetValue();
    }
  }

 private:
  content::Visibility expected_visibility_;
  base::test::TestFuture<void> reached_;
};

class ViewWidthShrinkSignal : public views::ViewObserver {
 public:
  explicit ViewWidthShrinkSignal(views::View* view);
  ~ViewWidthShrinkSignal() override;

  bool Wait() { return changed_.Wait(); }

  void OnViewBoundsChanged(views::View* view) override;

 private:
  int initial_width_;
  base::test::TestFuture<void> changed_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

ViewWidthShrinkSignal::ViewWidthShrinkSignal(views::View* view)
    : initial_width_(view->width()) {
  observation_.Observe(view);
}

ViewWidthShrinkSignal::~ViewWidthShrinkSignal() = default;

void ViewWidthShrinkSignal::OnViewBoundsChanged(views::View* view) {
  if (view->width() < initial_width_ && !changed_.IsReady()) {
    changed_.SetValue();
  }
}

class MahoControlActivityBrowserTest : public InProcessBrowserTest {
 public:
  void SetUp() override {
    EnablePixelOutput();
    InProcessBrowserTest::SetUp();
  }

 protected:
  static bool BitmapsEqual(const SkBitmap& lhs, const SkBitmap& rhs) {
    if (lhs.width() != rhs.width() || lhs.height() != rhs.height()) {
      return false;
    }
    for (int y = 0; y < lhs.height(); ++y) {
      for (int x = 0; x < lhs.width(); ++x) {
        if (lhs.getColor(x, y) != rhs.getColor(x, y)) {
          return false;
        }
      }
    }
    return true;
  }

  SkBitmap CaptureRendererSurface(content::WebContents* contents) {
    content::WaitForCopyableViewInFrame(contents->GetPrimaryMainFrame());
    SkBitmap bitmap;
    base::RunLoop run_loop;
    contents->GetRenderWidgetHostView()->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::Seconds(5),
        base::BindLambdaForTesting(
            [&](const content::CopyFromSurfaceResult& result) {
              if (result.has_value()) {
                bitmap = result->bitmap;
              }
              run_loop.Quit();
            }));
    run_loop.Run();
    EXPECT_FALSE(bitmap.empty());
    return bitmap;
  }
};

IN_PROC_BROWSER_TEST_F(MahoControlActivityBrowserTest,
                       MarkerTargetsForegroundAndStaysOutOfPageCapture) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<body style='margin:0;background:%23123456'>")));
  content::WebContents* target =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(target);
  const int target_index = browser()->GetTabStripModel()->active_index();

  MahoActionMarkerService::Request request;
  request.viewport_point = gfx::PointF(120.0f, 80.0f);
  request.kind = MahoActionMarkerService::Kind::kClick;
  request.sensitive = true;
  request.primary_frame_id = target->GetPrimaryMainFrame()->GetGlobalId();
  const int element_count =
      content::EvalJs(target, "document.querySelectorAll('*').length")
          .ExtractInt();
  const SkBitmap before = CaptureRendererSurface(target);

  WebContentsVisibilitySignal hidden(target, content::Visibility::HIDDEN);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(hidden.Wait());
  EXPECT_FALSE(
      MahoActionMarkerService::ShowForRevalidatedTarget(target, request));

  WebContentsVisibilitySignal visible(target, content::Visibility::VISIBLE);
  browser()->GetTabStripModel()->ActivateTabAt(target_index);
  ASSERT_TRUE(visible.Wait());
  ASSERT_TRUE(
      MahoActionMarkerService::ShowForRevalidatedTarget(target, request));
  auto* marker = MahoActionMarkerService::FromWebContents(target);
  ASSERT_TRUE(marker);
  marker->FreezeForTesting();
  ASSERT_TRUE(marker->IsVisibleForTesting());
  EXPECT_TRUE(marker->IsSensitiveForTesting());
  EXPECT_TRUE(
      marker->MarkerBoundsInContentsForTesting().Contains(gfx::Point(120, 80)));
  EXPECT_EQ(element_count,
            content::EvalJs(target, "document.querySelectorAll('*').length")
                .ExtractInt());

  const SkBitmap after = CaptureRendererSurface(target);
  EXPECT_TRUE(BitmapsEqual(before, after));
  EXPECT_TRUE(marker->IsVisibleForTesting());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>next</title>")));
  EXPECT_FALSE(marker->IsVisibleForTesting());
}

class MahoRoutineHandoffBrowserTest : public InProcessBrowserTest {
 protected:
  void NavigateLegacyRoutinesAndWait() {
    const int initial_tab_count = browser()->GetTabStripModel()->count();
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
    content::WebContents* legacy =
        browser()->GetTabStripModel()->GetActiveWebContents();
    WebContentsDestructionSignal destroyed(legacy);
    legacy->GetController().LoadURL(GURL(maho::kMahoRoutinesURL),
                                    content::Referrer(),
                                    ui::PAGE_TRANSITION_TYPED, std::string());

    if (!destroyed.Wait()) {
      ADD_FAILURE()
          << "legacy Routines navigation did not close its handoff tab";
      return;
    }
    EXPECT_EQ(initial_tab_count, browser()->GetTabStripModel()->count());
  }
};

IN_PROC_BROWSER_TEST_F(MahoRoutineHandoffBrowserTest,
                       LegacyNavigationHandsOffColdAndOpenPanel) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  const int cold_width = browser_view->contents_container()->width();
  ViewWidthShrinkSignal panel_opened(browser_view->contents_container());

  NavigateLegacyRoutinesAndWait();
  ASSERT_TRUE(panel_opened.Wait());
  ASSERT_LT(browser_view->contents_container()->width(), cold_width);
  auto* side_panel_ui = browser()->GetFeatures().side_panel_ui();
  ASSERT_TRUE(side_panel_ui);
  EXPECT_TRUE(side_panel_ui->IsSidePanelShowing());
  EXPECT_EQ(SidePanelEntryId::kMahoAiPanel,
            side_panel_ui->GetCurrentEntryId());
  const int64_t cold_generation = browser()->GetProfile()->GetPrefs()->GetInt64(
      ai_prefs::kPendingSurfaceGeneration);
  ASSERT_GT(cold_generation, 0);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->GetProfile()->GetPrefs()->GetInteger(
               ai_prefs::kPendingSurface) ==
           static_cast<int>(maho_ai::mojom::CompactSurface::kChat);
  }));
  const int open_width = browser_view->contents_container()->width();

  NavigateLegacyRoutinesAndWait();
  EXPECT_EQ(open_width, browser_view->contents_container()->width());
  EXPECT_EQ(cold_generation + 1,
            browser()->GetProfile()->GetPrefs()->GetInt64(
                ai_prefs::kPendingSurfaceGeneration));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return browser()->GetProfile()->GetPrefs()->GetInteger(
               ai_prefs::kPendingSurface) ==
           static_cast<int>(maho_ai::mojom::CompactSurface::kChat);
  }));
}

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
    return browser->GetTabStripModel()->GetActiveWebContents();
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
  Browser* r1 = static_cast<Browser*>(browser());
  Browser* r2 = static_cast<Browser*>(CreateBrowser(r1->GetProfile()));
  Browser* r3 = static_cast<Browser*>(CreateBrowser(r1->GetProfile()));

  Profile* otr_profile = r1->GetProfile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  Browser* i1 = static_cast<Browser*>(CreateBrowser(otr_profile));
  Browser* i2 = static_cast<Browser*>(CreateBrowser(otr_profile));
  ASSERT_TRUE(r2);
  ASSERT_TRUE(i1);

  base::RunLoop().RunUntilIdle();

  auto* reg_coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(r1->GetProfile());
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
  Browser* r1 = static_cast<Browser*>(browser());
  Browser* r2 = static_cast<Browser*>(CreateBrowser(r1->GetProfile()));

  base::RunLoop().RunUntilIdle();

  auto* reg_coordinator = MahoNowPlayingCoordinatorFactory::GetForProfile(r1->GetProfile());
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
  content::WebContents* video_contents = r2->GetTabStripModel()->GetWebContentsAt(0);
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
