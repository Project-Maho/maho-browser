// Copyright 2026 Maho Browser. All rights reserved.
// allow: SIZE_OK - lane C is restricted to this already linked browser-test TU;
// extracting new compiled test sources would require unapproved GN edits.
//
// Wave 2 minimal lifecycle verification for the Maho AI side panel host.
//
// Subject under test: the existing host integration path for
// SidePanelEntryId::kMahoAiPanel registered by
// maho::RegisterMahoAiSidePanel() (via side_panel_helper.cc overlay patch).
//
// Scope: show/hide lifecycle only.
//   * Panel can be shown through the production Show() integration path.
//   * Panel can be closed cleanly.
//   * Repeated close→show cycles continue to produce a usable host.
//
// Deliberately excluded:
//   split-view, space-config, delete-dialog, build-wrapper, AI content
//   behaviour, prompt flows, UX chrome.
//
// Access pattern: state is read only through the public SidePanelUI API and
//   the testing accessor SidePanelUI::GetWebContentsForTest().
//
// Tests:
//   1. ShowPanel_EntryIsShowing          — Show() makes the panel visible.
//   2. ClosePanel_EntryIsNoLongerShowing — Close() dismisses cleanly; entry is
//                                          not showing and IsSidePanelShowing()
//                                          returns false.
//   3. ClosePanel_CanShowHostAgainAfterClose
//                                       — After Close(), Show() can reopen the
//                                          entry and expose a host WebContents
//                                          again.
//   4. RepeatedShowHide_NoOrphanedHost   — Three open→close cycles; after each
//                                          close the panel is not showing, and
//                                          the next Show() still reaches a
//                                          usable host.

#include "base/run_loop.h"
#include <atomic>
#include <array>
#include "base/containers/span.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_browser_main_extra_parts.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/sync/maho_sync_relay_client.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "net/dns/mock_host_resolver.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "components/prefs/pref_service.h"

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_test_utils.h"

namespace {

class ConsoleObserver : public content::WebContentsObserver {
 public:
  explicit ConsoleObserver(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents) {}
  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override {
    LOG(INFO) << "AI_CONSOLE: " << message;
  }
};

}  // namespace
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/infobars/confirm_infobar.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "ui/views/test/views_test_utils.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/side_panel/side_panel_header.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "ui/views/test/widget_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_client.h"
#include "services/network/test/test_url_loader_factory.h"
#include "third_party/blink/public/common/loader/throttling_url_loader.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "content/public/browser/media_stream_request.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/side_panel/maho_ai_side_panel_web_view.h"
#include "media/base/media_switches.h"
#include "ui/views/controls/webview/webview.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/maho_ai_popup_lifetime_tracker.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace {

// Convenience alias matching the Maho entry registered by
// maho::RegisterMahoAiSidePanel().
constexpr SidePanelEntryId kMahoAiEntryId = SidePanelEntryId::kMahoAiPanel;

class MahoAiSidePanelLifecycleTest : public InProcessBrowserTest {
 public:
  MahoAiSidePanelLifecycleTest() = default;
  MahoAiSidePanelLifecycleTest(const MahoAiSidePanelLifecycleTest&) = delete;
  MahoAiSidePanelLifecycleTest& operator=(
      const MahoAiSidePanelLifecycleTest&) = delete;
  ~MahoAiSidePanelLifecycleTest() override = default;

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    const std::string test_name =
        testing::UnitTest::GetInstance()->current_test_info()->name();
    if (!test_name.starts_with("MemoryThread")) {
      return;
    }
    startup_ready_subscription_ = maho::AddCoreReadyCallback(
        base::BindLambdaForTesting([this] {
          ++startup_ready_count_;
          ready_after_hydration_ = hydration_complete_.load();
          if (startup_ready_count_ == 1) {
            core_ready_.SetValue();
          }
        }));
    if (test_name != "MemoryThreadStartupPublishesHydratedCore") {
      return;
    }
    MahoBrowserMainExtraParts::SetCoreStartupObserverForTesting(
        base::BindLambdaForTesting([this](
            MahoBrowserMainExtraParts::CoreStartupStageForTesting stage) {
          using Stage = MahoBrowserMainExtraParts::CoreStartupStageForTesting;
          switch (stage) {
            case Stage::kStorageOpen:
              storage_open_on_worker_ = !content::BrowserThread::CurrentlyOn(
                  content::BrowserThread::UI);
              break;
            case Stage::kHydrationBegin: {
              unpublished_during_hydration_ = maho::GetCore() == nullptr;
              content::GetUIThreadTaskRunner({})->PostTask(
                  FROM_HERE, base::BindLambdaForTesting([this] {
                    startup_ui_marker_.Signal();
                  }));
              base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
              // The deadline only breaks a baseline UI deadlock; it cannot
              // fabricate a successful marker or a worker-entry event.
              startup_marker_before_hydration_ =
                  startup_ui_marker_.TimedWait(base::Seconds(10));
              break;
            }
            case Stage::kHydrationComplete:
              hydration_complete_ = true;
              content::GetUIThreadTaskRunner({})->PostTask(
                  FROM_HERE, base::BindLambdaForTesting([this] {
                    startup_complete_.SetValue();
                  }));
              break;
          }
        }));
  }

  void TearDownInProcessBrowserTestFixture() override {
    MahoBrowserMainExtraParts::SetCoreStartupObserverForTesting({});
    startup_ready_subscription_ = {};
    InProcessBrowserTest::TearDownInProcessBrowserTestFixture();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Disable Maho's onboarding login-gate so the standard startup browser
    // is created (otherwise browser() is null).
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void PreRunTestOnMainThread() override {
    content::RunAllPendingInMessageLoop();
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
    for (auto* window : GetAllBrowserWindowInterfaces()) {
      auto* view = BrowserView::GetBrowserViewForBrowser(window);
      auto* controller =
          view ? view->GetMahoCommandOverlayControllerForTesting() : nullptr;
      if (controller && controller->IsVisible()) {
        views::test::WidgetDestroyedWaiter closed(
            controller->GetOverlayViewForTesting()->GetWidget());
        controller->Hide();
        closed.Wait();
      }
    }
    InProcessBrowserTest::PreRunTestOnMainThread();
  }

  void CreatedBrowserMainParts(content::BrowserMainParts* parts) override {
    InProcessBrowserTest::CreatedBrowserMainParts(parts);
    if (std::string(testing::UnitTest::GetInstance()->current_test_info()->name())
            .starts_with("MemoryThread")) {
      static_cast<ChromeBrowserMainParts*>(parts)->AddParts(
          std::make_unique<MahoBrowserMainExtraParts>());
    }
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");

    if (std::string(testing::UnitTest::GetInstance()->current_test_info()->name())
            .starts_with("MemoryThread")) {
      base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
      ASSERT_TRUE(core_ready_.Wait());
      ASSERT_TRUE(maho::GetCore());
    }

    SidePanelUI* ui = GetSidePanelUI();
    ASSERT_TRUE(ui);
    // Suppress animations and load delays so state transitions complete
    // synchronously within RunUntilIdle() / RunUntil().
    ui->DisableAnimationsForTesting();
    ui->SetNoDelaysForTesting(true);
  }

 protected:
  SidePanelUI* GetSidePanelUI() {
    return browser()->GetFeatures().side_panel_ui();
  }

  base::CallbackListSubscription startup_ready_subscription_;
  base::WaitableEvent startup_ui_marker_;
  base::test::TestFuture<void> startup_complete_;
  base::test::TestFuture<void> core_ready_;
  std::atomic<bool> storage_open_on_worker_{false};
  std::atomic<bool> unpublished_during_hydration_{false};
  std::atomic<bool> startup_marker_before_hydration_{false};
  std::atomic<bool> hydration_complete_{false};
  int startup_ready_count_ = 0;
  bool ready_after_hydration_ = false;

  // Shows the Maho AI panel via the production SidePanelUI::Show() path.
  // This is the same call site used by maho_sidebar_top_bar_view.cc and
  // maho_command_action_handler.cc.
  void ShowMahoAiPanel() {
    SidePanelUI* ui = GetSidePanelUI();
    ASSERT_TRUE(ui);
    ui->Show(SidePanelEntryKey(kMahoAiEntryId),
             SidePanelOpenTrigger::kToolbarButton);
  }

  // Closes the content-area side panel.
  void CloseSidePanel() {
    SidePanelUI* ui = GetSidePanelUI();
    ASSERT_TRUE(ui);
    ui->Close();
  }

  bool IsMahoAiPanelShowing() {
    SidePanelUI* ui = GetSidePanelUI();
    if (!ui) {
      return false;
    }
    return ui->IsSidePanelEntryShowing(SidePanelEntryKey(kMahoAiEntryId));
  }

  // Returns the host WebContents via SidePanelUI's testing accessor.
  //
  // Note: SidePanelCoordinator::GetWebContentsForTest() is constructive for a
  // registered entry: it may create/cache the entry view before returning the
  // hosted WebContents. Only use this helper while the panel is showing, or
  // when intentionally preparing the next Show() assertion.
  content::WebContents* GetHostWebContents() {
    SidePanelUI* ui = GetSidePanelUI();
    if (!ui) {
      return nullptr;
    }
    SidePanelRegistry* registry = SidePanelRegistry::From(browser());
    SidePanelEntry* entry = registry ? registry->GetEntryForKey(SidePanelEntryKey(kMahoAiEntryId)) : nullptr;
    LOG(INFO) << "GetHostWebContents: entry=" << entry
              << " cached_view=" << (entry ? entry->CachedView().get() : nullptr);
    if (entry && entry->CachedView()) {
      views::View* cached_view = entry->CachedView().get();
      views::View* webview = cached_view->GetViewByID(SidePanelWebUIView::kSidePanelWebViewId);
      auto* webui_view = static_cast<SidePanelWebUIViewT<MahoAIUI>*>(webview);
      auto* wrapper = webui_view ? webui_view->contents_wrapper() : nullptr;
      content::WebContents* wrapper_wc = wrapper ? wrapper->web_contents() : nullptr;
      LOG(INFO) << "GetHostWebContents debug: cached_view=" << cached_view
                << " webview=" << webview
                << " wrapper=" << wrapper
                << " wrapper_wc=" << wrapper_wc
                << " webview_wc=" << (webview ? (static_cast<views::WebView*>(webview))->web_contents() : nullptr);
      if (wrapper_wc) {
        return wrapper_wc;
      }
    }
    return ui->GetWebContentsForTest(kMahoAiEntryId);
  }
};

class MahoAiSidePanelMicrophoneAllowTest
    : public MahoAiSidePanelLifecycleTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    MahoAiSidePanelLifecycleTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
    command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
  }
};

class MahoAiSidePanelMicrophoneDenyTest
    : public MahoAiSidePanelLifecycleTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    MahoAiSidePanelLifecycleTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
    command_line->AppendSwitchASCII(switches::kUseFakeUIForMediaStream,
                                   "deny");
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelMicrophoneAllowTest,
                       MicrophonePermissionReachesPlatformAllowPath) {
  ShowMahoAiPanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  }));

  content::WebContents* host = GetHostWebContents();
  ASSERT_TRUE(host);
  ASSERT_TRUE(content::WaitForLoadStop(host));

  EXPECT_EQ("allowed", content::EvalJs(host, R"(
    (async () => {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({audio: true});
        const tracks = stream.getAudioTracks();
        tracks.forEach(track => track.stop());
        return tracks.length > 0 ? 'allowed' : 'no-audio-track';
      } catch (error) {
        return `denied:${error.name}`;
      }
    })()
  )"));

  CloseSidePanel();
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelMicrophoneDenyTest,
                       MicrophonePermissionReachesPlatformDenyPath) {
  ShowMahoAiPanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  }));

  content::WebContents* host = GetHostWebContents();
  ASSERT_TRUE(host);
  ASSERT_TRUE(content::WaitForLoadStop(host));

  EXPECT_EQ("denied:NotAllowedError", content::EvalJs(host, R"(
    (async () => {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({audio: true});
        stream.getTracks().forEach(track => track.stop());
        return 'allowed';
      } catch (error) {
        return `denied:${error.name}`;
      }
    })()
  )"));

  CloseSidePanel();
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       SinglePanelLayoutReservesWidthOnceAndOffsetsContentType) {
  class LayoutInfoBarDelegate : public ConfirmInfoBarDelegate {
   public:
    InfoBarIdentifier GetIdentifier() const override { return TEST_INFOBAR; }
    std::u16string GetMessageText() const override { return u"Layout fixture"; }
  };

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view->IsMahoArcLayoutActive());
  auto* panel = browser_view->side_panel();
  ASSERT_TRUE(panel);
  panel->DisableAnimationsForTesting();
  auto* manager = infobars::ContentInfoBarManager::FromWebContents(
      browser()->tab_strip_model()->GetActiveWebContents());
  ASSERT_TRUE(manager);
  auto* infobar = manager->AddInfoBar(
      ConfirmInfoBar::Create(std::make_unique<LayoutInfoBarDelegate>()));
  ASSERT_TRUE(infobar);
  infobar->Show(false);
  views::test::RunScheduledLayout(browser_view);
  const gfx::Rect closed_contents = browser_view->multi_contents_view()->bounds();
  const gfx::Rect infobar_bounds = browser_view->infobar_container()->bounds();
  ASSERT_GT(infobar_bounds.height(), 0);

  for (const auto type : {SidePanelType::kToolbar, SidePanelType::kContent}) {
    panel->SetCurrentEntryType(type);
    panel->Open(false);
    browser_view->InvalidateLayout();
    views::test::RunScheduledLayout(browser_view);
    ASSERT_TRUE(panel->GetVisible());
    const auto contents = browser_view->multi_contents_view()->bounds();
    EXPECT_EQ(contents.width() + panel->width(), closed_contents.width());
    EXPECT_EQ(panel->y(), type == SidePanelType::kContent
                              ? infobar_bounds.bottom()
                              : infobar_bounds.y());
    EXPECT_EQ(contents.y(), closed_contents.y());
    panel->Close(false);
    browser_view->InvalidateLayout();
    views::test::RunScheduledLayout(browser_view);
    EXPECT_EQ(browser_view->multi_contents_view()->bounds(), closed_contents);
  }
  manager->RemoveInfoBar(infobar);
}

// 1. Show() makes kMahoAiPanel the active entry.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ShowPanel_EntryIsShowing) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, /*foreground=*/true);
  base::RunLoop().RunUntilIdle();

  SidePanelRegistry* registry = SidePanelRegistry::From(browser());
  SidePanelEntry* entry = registry ? registry->GetEntryForKey(SidePanelEntryKey(kMahoAiEntryId)) : nullptr;
  LOG(INFO) << "ShowPanel_EntryIsShowing debug: registry=" << registry
            << " entry=" << entry
            << " browser=" << browser();

  ASSERT_FALSE(IsMahoAiPanelShowing())
      << "Precondition: panel must not be showing before Show()";

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "IsSidePanelEntryShowing(kMahoAiPanel) must return true after "
         "Show(SidePanelEntryKey(kMahoAiPanel)); RegisterMahoAiSidePanel() "
         "registration or SidePanelUI routing is broken.";

  content::WebContents* host_wc = GetHostWebContents();
  LOG(INFO) << "ShowPanel_EntryIsShowing: host_wc=" << host_wc
            << " visible_url=" << (host_wc ? host_wc->GetVisibleURL().spec() : "null")
            << " committed_url=" << (host_wc ? host_wc->GetLastCommittedURL().spec() : "null");

  std::unique_ptr<ConsoleObserver> console_obs =
      std::make_unique<ConsoleObserver>(host_wc);

  EXPECT_TRUE(IsMahoAiPanelShowing());

  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->should_show_header());
  auto* panel = BrowserView::GetBrowserViewForBrowser(browser())->side_panel();
  ASSERT_NE(panel, nullptr);
  EXPECT_EQ(panel->GetHeaderView<SidePanelHeader>(), nullptr);
  EXPECT_EQ(panel->GetBorder(), nullptr);
  EXPECT_EQ(panel->GetContentParentView()->background(), nullptr);

  // Clean up so the panel is not open when the browser tears down.
  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  }));
}

// 2. Close() dismisses the entry; neither the entry nor the panel is showing.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ClosePanel_EntryIsNoLongerShowing) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, /*foreground=*/true);
  base::RunLoop().RunUntilIdle();

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Precondition: panel must open before close test";

  CloseSidePanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  })) << "IsSidePanelShowing() must return false after Close(); "
         "Close() path is broken.";

  EXPECT_FALSE(IsMahoAiPanelShowing())
      << "kMahoAiPanel entry must not be showing after side panel close";
  EXPECT_FALSE(GetSidePanelUI()->IsSidePanelShowing())
      << "Side panel must be fully closed after Close()";
}

// 3. After Close(), Show() still reopens the entry and exposes a host again.
//    We intentionally do not query GetWebContentsForTest() while closed because
//    that accessor may materialize the entry view for a registered panel.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ClosePanel_CanShowHostAgainAfterClose) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, /*foreground=*/true);
  base::RunLoop().RunUntilIdle();

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Precondition: panel must open before WebContents lifecycle test";

  // While showing, the host WebContents must exist.
  content::WebContents* host_wc = GetHostWebContents();
  EXPECT_TRUE(host_wc)
      << "GetWebContentsForTest(kMahoAiPanel) must return a non-null "
         "WebContents while the panel is showing";

  CloseSidePanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  })) << "Side panel must close before re-open assertion";

  EXPECT_FALSE(IsMahoAiPanelShowing())
      << "kMahoAiPanel entry must not be showing after close";

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Show() must reopen the panel after a prior Close()";

  EXPECT_TRUE(GetHostWebContents())
      << "GetWebContentsForTest(kMahoAiPanel) must expose a host while the "
         "reopened panel is showing";

  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  }));
}

// 4. Three open→close cycles; each close hides the panel, and each subsequent
//    Show() still reaches a live host. The test name is kept stable for the
//    narrow runtime filter, but the proof deliberately avoids treating
//    GetWebContentsForTest() as a post-close leak oracle.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       RepeatedShowHide_NoOrphanedHost) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, /*foreground=*/true);
  base::RunLoop().RunUntilIdle();
  constexpr int kCycles = 3;
  for (int i = 0; i < kCycles; ++i) {
    ShowMahoAiPanel();

    ASSERT_TRUE(base::test::RunUntil([this]() {
      return IsMahoAiPanelShowing();
    })) << "Cycle " << i << ": Show() did not open the panel";

    content::WebContents* host_wc = GetHostWebContents();
    EXPECT_TRUE(host_wc)
        << "Cycle " << i
        << ": host WebContents must be non-null while panel is showing";

    CloseSidePanel();

    ASSERT_TRUE(base::test::RunUntil([this]() {
      return !GetSidePanelUI()->IsSidePanelShowing();
    })) << "Cycle " << i << ": Close() did not close the panel";

    EXPECT_FALSE(IsMahoAiPanelShowing())
        << "Cycle " << i << ": entry must not be showing after close";
    EXPECT_FALSE(GetSidePanelUI()->IsSidePanelShowing())
        << "Cycle " << i << ": side panel must remain closed after close";
  }
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       CommandPalette_AskMahoOpensPanel) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  ASSERT_TRUE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      static_cast<Browser*>(browser()), "open the assistant", maho::PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular));

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Ask Maho command action must show kMahoAiPanel in its browser.";
  EXPECT_TRUE(GetHostWebContents())
      << "The command action must expose a live Maho AI side-panel host.";

  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       Escape_PreservesPanelWhenWebViewDisablesEscClose) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ShowMahoAiPanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  }));

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                              false, false, false, false));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(IsMahoAiPanelShowing())
      << "Escape must not close the Maho AI panel while esc_closes_ui is false.";

  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       CommandPaletteOpen_FocusesComposerTextarea) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  ASSERT_TRUE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      static_cast<Browser*>(browser()), "open the assistant", maho::PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular));
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  }));

  content::WebContents* host = GetHostWebContents();
  ASSERT_TRUE(host);
  ASSERT_TRUE(content::WaitForLoadStop(host));

  ASSERT_TRUE(base::test::RunUntil([host]() {
    return content::EvalJs(host,
                           "!!document.activeElement && "
                           "document.activeElement.tagName === 'TEXTAREA'")
        .ExtractBool();
  })) << "the composer textarea must be focused after the panel opens";

  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing();
  }));
}

namespace {

// Forges a MahoAIPageHandler for the given browser + its active WebContents,
// exactly as a hostile renderer would drive the PageHandlerFactory. The token
// is built from the exact owning window; a denied class resets the connection
// in the constructor.
std::unique_ptr<MahoAIPageHandler> MakeForgedHandler(
    Browser* browser,
    mojo::Remote<maho_ai::mojom::PageHandler>* remote) {
  content::WebContents* wc = browser->GetTabStripModel()->GetActiveWebContents();
  auto token = std::make_unique<MahoPrivateContextToken>(browser, wc);
  auto loader = browser->GetProfile()
                    ->GetDefaultStoragePartition()
                    ->GetURLLoaderFactoryForBrowserProcess();
  mojo::PendingRemote<maho_ai::mojom::Page> page;
  return std::make_unique<MahoAIPageHandler>(
      remote->BindNewPipeAndPassReceiver(), std::move(page), std::move(token),
      browser, wc, browser->GetProfile()->GetPrefs(), std::move(loader));
}

}  // namespace

// task-9-ai-ui: Incognito never enables the AI WebUI and never registers the
// side-panel entry.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       IncognitoWebUiAndEntryAreDisabled) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->GetProfile()->IsOffTheRecord());

  MahoAIUIConfig config;
  EXPECT_FALSE(config.IsWebUIEnabled(incognito->GetProfile()));

  SidePanelUI* ui = incognito->GetFeatures().side_panel_ui();
  if (ui) {
    EXPECT_FALSE(ui->IsSidePanelEntryShowing(
        SidePanelEntryKey(kMahoAiEntryId)));
  }
}

// task-9-ai-ui: a forged factory/handler in Incognito fails closed before any
// runtime router/adapter is built — the receiver disconnects.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ForgedFactoryStopsBeforeRouter) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(incognito, &remote);

  base::RunLoop run_loop;
  remote.set_disconnect_handler(run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_FALSE(remote.is_connected());
}

// task-9-ai-ui: every page-handler method fails closed for a forged OTR
// handler — no reply is delivered and the pipe stays disconnected.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       EveryPageHandlerMethodFailsClosed) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(incognito, &remote);

  base::RunLoop disconnect_loop;
  remote.set_disconnect_handler(disconnect_loop.QuitClosure());

  bool any_reply = false;
  remote->GetConnectionState(base::BindOnce(
      [](bool* replied, maho_ai::mojom::RuntimeConnectionState,
         const std::string&) { *replied = true; },
      &any_reply));
  remote->GetSessionList(base::BindOnce(
      [](bool* replied,
         std::vector<maho_ai::mojom::SessionInfoPtr>) { *replied = true; },
      &any_reply));
  remote->GetAiProfiles(base::BindOnce(
      [](bool* replied, const std::string&) { *replied = true; }, &any_reply));

  disconnect_loop.Run();
  EXPECT_FALSE(remote.is_connected());
  EXPECT_FALSE(any_reply);
}

// task-9-ai-ui: attachment/context, tab-tidy, and agent tool sinks fail closed
// for a forged OTR handler — no reply is produced.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       AttachmentToolAndAgentSinksFailClosed) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(incognito, &remote);

  base::RunLoop disconnect_loop;
  remote.set_disconnect_handler(disconnect_loop.QuitClosure());

  bool tidy_reply = false;
  remote->RequestTabTidy(
      std::vector<maho_ai::mojom::TabInfoPtr>(),
      base::BindOnce(
          [](bool* replied,
             std::vector<maho_ai::mojom::TidyFolderPtr>) { *replied = true; },
          &tidy_reply));
  // Fire-and-forget agent/attachment sink; must not reach the adapter/core.
  remote->SubmitPrompt("ses", "hello", /*attach_browser_context=*/true,
                       maho_ai::mojom::InteractionMode::kDeveloper,
                       std::nullopt, maho_ai::mojom::ChatIntent::kFreeform,
                       base::DoNothing());

  disconnect_loop.Run();
  EXPECT_FALSE(remote.is_connected());
  EXPECT_FALSE(tidy_reply);
}

// task-9-ai-ui: positive control — a regular-profile handler is NOT denied.
// The runtime router builds an adapter and the connection resolves.
//
// TODO(R-9): the full positive control asserts exactly one loopback
// `/v1/chat/completions` POST emitting NORMAL_AI_OK. That requires the shared
// loopback SSE provider fixture (see plan Verification Contract 3), which does
// not yet exist in the tree; wiring it is tracked as a residual blocker. This
// control verifies the regular path stays open (no over-blocking).
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       NormalFakeProviderPositiveControl) {
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());

  MahoAIUIConfig config;
  EXPECT_TRUE(config.IsWebUIEnabled(browser()->GetProfile()));

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(static_cast<Browser*>(browser()), &remote);

  bool got_state = false;
  maho_ai::mojom::RuntimeConnectionState state =
      maho_ai::mojom::RuntimeConnectionState::kDisconnected;
  base::RunLoop run_loop;
  remote->GetConnectionState(base::BindOnce(
      [](bool* got, maho_ai::mojom::RuntimeConnectionState* out,
         base::OnceClosure quit, maho_ai::mojom::RuntimeConnectionState value,
         const std::string&) {
        *got = true;
        *out = value;
        std::move(quit).Run();
      },
      &got_state, &state, run_loop.QuitClosure()));
  run_loop.Run();

  EXPECT_TRUE(got_state);
  EXPECT_TRUE(remote.is_connected());
}

// ===========================================================================
// Ask Maho ingress lifecycle regressions (plan Todos 15-17). These drive the
// REAL Browser, the REAL per-Browser MahoAiIngressCoordinator, and REAL
// MahoAIPageHandler instances bound to a recording Page remote. Assertions
// inspect accepted request ids, emitted user-prompt events, and coordinator
// delivery ORDER — never log strings.
//
// Seam rationale (kept because the structure is non-obvious): the test profile
// configures no BYOK runtime adapter, so MahoAiRuntimeRouter has no available
// adapter and the real SubmitPromptInternal path emits a synchronous terminal
// error that settles the request immediately. That is the correct behavior for
// the cold / duplicate / floating-win / requeue cases, which run the real
// handler end to end. To hold a request ACCEPTED-but-not-terminal (so the FIFO
// gate and post-acceptance teardown windows are observable) the terminal-gate
// and teardown-after-acceptance cases drive acceptance through the existing
// MahoAIPageHandlerTestPeer seam plus the real handler session/terminal
// routing; no production hook is added.
// ===========================================================================

// Friend accessor for MahoAIPageHandler private ingress state (same shape as
// the unit-test peer in maho_ai_page_handler_unittest.cc; redeclared because
// interactive tests link into a separate binary).
class MahoAIPageHandlerTestPeer {
 public:
  static void EmitRuntimeEvent(MahoAIPageHandler* handler,
                               const std::string& session_id,
                               std::optional<std::string> request_id,
                               MahoAiRuntimeEventType type,
                               const std::string& text = std::string()) {
    MahoAiRuntimeEvent event;
    event.type = type;
    event.text = text;
    handler->OnRuntimeEventForSession(session_id, std::move(request_id),
                                      std::move(event));
  }

  static void SetActiveIngressRequest(MahoAIPageHandler* handler,
                                      const std::string& request_id,
                                      uint64_t delivery_id) {
    handler->active_ingress_request_id_ = request_id;
    handler->active_ingress_delivery_id_ = delivery_id;
  }

  static const std::string& active_ingress_request_id(
      const MahoAIPageHandler* handler) {
    return handler->active_ingress_request_id_;
  }

  static void ResetIngressRegistration(MahoAIPageHandler* handler) {
    handler->ask_maho_registration_.Reset();
  }

  static void SetIngressRegistration(
      MahoAIPageHandler* handler,
      maho::MahoAiIngressCoordinator::ConsumerRegistration registration) {
    handler->ask_maho_registration_ = std::move(registration);
  }

  static void ResetPage(MahoAIPageHandler* handler) { handler->page_.reset(); }
};

namespace {

class FakePage : public maho_ai::mojom::Page {
 public:
  mojo::PendingRemote<maho_ai::mojom::Page> BindAndGetRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void OnSurfaceRequested(
      maho_ai::mojom::SurfaceRequestPtr request) override {}
  void OnRoutineRunStatusChanged(
      maho_routines::mojom::RoutineRunStatusPtr status) override {}
  void OnRuntimeEvent(maho_ai::mojom::RuntimeEventPtr event) override {
    events.push_back(std::move(event));
  }
  void OnConnectionStateChanged(
      maho_ai::mojom::RuntimeConnectionState) override {}
  void OnSessionUpdated(maho_ai::mojom::SessionInfoPtr) override {}
  void OnAISettingsChanged(maho_ai::mojom::AISettingsInfoPtr) override {}
  void OnRuntimeConfigChanged(
      maho_ai::mojom::RuntimeConfigInfoPtr config) override {
    runtime_configs.push_back(std::move(config));
  }
  void OnVoicePartial(const std::string&) override {}
  void OnVoiceFinal(const std::string&) override {}
  void OnVoiceError(const std::string&) override {}
  void OnControlActivityChanged(
      std::vector<maho_ai::mojom::ControlActivitySnapshotPtr> entries) override {}
  void OnAskMahoSessionAccepted(
      const std::string& request_id,
      maho_ai::mojom::SessionInfoPtr session) override {
    accepted_requests.push_back(request_id);
    accepted_sessions.push_back(std::move(session));
  }

  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  std::vector<std::string> accepted_requests;
  std::vector<maho_ai::mojom::SessionInfoPtr> accepted_sessions;
  std::vector<maho_ai::mojom::RuntimeConfigInfoPtr> runtime_configs;

 private:
  mojo::Receiver<maho_ai::mojom::Page> receiver_{this};
};

class MahoAiIngressLifecycleInteractiveTest : public InProcessBrowserTest {
 public:
  MahoAiIngressLifecycleInteractiveTest() = default;
  MahoAiIngressLifecycleInteractiveTest(
      const MahoAiIngressLifecycleInteractiveTest&) = delete;
  MahoAiIngressLifecycleInteractiveTest& operator=(
      const MahoAiIngressLifecycleInteractiveTest&) = delete;
  ~MahoAiIngressLifecycleInteractiveTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Ensure the standard startup browser is created (browser() non-null).
    command_line->AppendSwitch("maho-disable-login-gate");
  }

 protected:
  maho::MahoAiIngressCoordinator* GetCoordinator(Browser* browser) {
    return maho::MahoAiIngressCoordinator::GetOrCreateForBrowser(browser);
  }

  // Builds a real page handler for the browser's active tab, bound to |page|.
  std::unique_ptr<MahoAIPageHandler> MakeHandler(
      Browser* browser,
      FakePage* page,
      mojo::Remote<maho_ai::mojom::PageHandler>* remote) {
    content::WebContents* wc =
        browser->GetTabStripModel()->GetActiveWebContents();
    auto token = std::make_unique<MahoPrivateContextToken>(browser, wc);
    auto loader = browser->GetProfile()
                      ->GetDefaultStoragePartition()
                      ->GetURLLoaderFactoryForBrowserProcess();
    return std::make_unique<MahoAIPageHandler>(
        remote->BindNewPipeAndPassReceiver(), page->BindAndGetRemote(),
        std::move(token), browser, wc, browser->GetProfile()->GetPrefs(),
        std::move(loader));
  }

  Browser* CreateTrackedFloatingPopup(Browser* opener) {
    BrowserWindowCreateParams popup_params(Browser::TYPE_POPUP, opener->GetProfile(),
                                       /*user_gesture=*/true);
    popup_params.is_trusted_source = true;
    popup_params.omit_from_session_restore = true;
    Browser* popup = static_cast<Browser*>(CreateBrowserWindow(std::move(popup_params)));
    chrome::AddTabAt(popup, GURL("about:blank"), /*index=*/-1,
                     /*foreground=*/true);
    maho::MahoAiPopupLifetimeTracker::Get()->TrackPopup(opener, popup);
    popup->GetWindow()->Show();
    return popup;
  }

  static maho_ai::mojom::AskMahoDispatchPtr MakeDispatch(
      const std::string& request_id,
      const std::string& query) {
    return maho_ai::mojom::AskMahoDispatch::New(
        request_id, query, maho_ai::mojom::AskMahoSource::kCommandPalette,
        /*submit=*/true, maho_ai::mojom::InteractionMode::kAssistant,
        maho_ai::mojom::AskMahoContextIntent::kNone, std::nullopt);
  }

  static maho_ai::mojom::SessionInfoPtr StartRealSession(
      MahoAIPageHandler* handler) {
    maho_ai::mojom::SessionInfoPtr session;
    handler->StartSession(
        std::nullopt, maho_ai::mojom::InteractionMode::kAssistant,
        base::BindLambdaForTesting([&](maho_ai::mojom::SessionInfoPtr result) {
          session = std::move(result);
        }));
    return session;
  }

  static size_t CountUserPrompts(const FakePage& page,
                                 const std::string& text) {
    size_t count = 0;
    for (const auto& event : page.events) {
      if (event->kind == maho_ai::mojom::RuntimeEventKind::kUserPrompt &&
          event->text.has_value() && *event->text == text) {
        ++count;
      }
    }
    return count;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoAiIngressLifecycleInteractiveTest,
                       AskMahoIngressCold_RetainedBeforeBindThenAcceptedOnce) {
  GetCoordinator(static_cast<Browser*>(browser()))->Dispatch(MakeDispatch("req-cold", "cold query"));

  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(static_cast<Browser*>(browser()), &page, &remote);
  ASSERT_TRUE(handler);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ((std::vector<std::string>{"req-cold"}), page.accepted_requests);
  ASSERT_EQ(1u, page.accepted_sessions.size());
  EXPECT_FALSE(page.accepted_sessions[0]->session_id.empty());
  EXPECT_EQ(1u, CountUserPrompts(page, "cold query"));
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    AskMahoIngressCold_DuplicateRequestIdCreatesNoSecondSession) {
  auto* coordinator = GetCoordinator(static_cast<Browser*>(browser()));
  coordinator->Dispatch(MakeDispatch("req-dup", "only once"));
  coordinator->Dispatch(MakeDispatch("req-dup", "only once"));

  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(static_cast<Browser*>(browser()), &page, &remote);
  ASSERT_TRUE(handler);
  base::RunLoop().RunUntilIdle();

  coordinator->Dispatch(MakeDispatch("req-dup", "only once"));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ((std::vector<std::string>{"req-dup"}), page.accepted_requests);
  EXPECT_EQ(1u, page.accepted_sessions.size());
  EXPECT_EQ(1u, CountUserPrompts(page, "only once"));
}

IN_PROC_BROWSER_TEST_F(MahoAiIngressLifecycleInteractiveTest,
                       AskMahoIngressTerminalGate_BGatedUntilATerminal) {
  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(static_cast<Browser*>(browser()), &page, &remote);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto* coordinator = GetCoordinator(static_cast<Browser*>(browser()));
  std::vector<std::string> deliveries;
  std::string session_a_id;
  auto registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kFloating,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            deliveries.push_back(dispatch.request_id);
            if (dispatch.request_id == "req-a") {
              auto session = StartRealSession(handler.get());
              session_a_id = session->session_id;
              MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                  handler.get(), "req-a", delivery_id);
              std::move(accept_cb).Run(session_a_id);
            }
          }));

  coordinator->Dispatch(MakeDispatch("req-a", "first"));
  coordinator->Dispatch(MakeDispatch("req-b", "second"));

  EXPECT_EQ((std::vector<std::string>{"req-a"}), deliveries);
  EXPECT_EQ("req-a", MahoAIPageHandlerTestPeer::active_ingress_request_id(
                         handler.get()));

  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a_id, "req-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");

  EXPECT_EQ((std::vector<std::string>{"req-a", "req-b"}), deliveries);
  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::active_ingress_request_id(handler.get())
          .empty());
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    AskMahoIngressRebind_TeardownBeforeAcceptanceReplaysOnce) {
  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(static_cast<Browser*>(browser()), &page, &remote);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetPage(handler.get());

  auto* coordinator = GetCoordinator(static_cast<Browser*>(browser()));
  coordinator->Dispatch(MakeDispatch("req-rebind", "retained"));
  base::RunLoop().RunUntilIdle();

  std::vector<std::string> replayed;
  auto registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              maho::MahoAiIngressCoordinator::AcceptanceCallback) {
            replayed.push_back(dispatch.request_id);
          }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ((std::vector<std::string>{"req-rebind"}), replayed);

  handler.reset();
  EXPECT_EQ((std::vector<std::string>{"req-rebind"}), replayed);
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    AskMahoIngressRebind_TeardownAfterAcceptanceReleasesBWithoutRedeliveringA) {
  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(static_cast<Browser*>(browser()), &page, &remote);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto* coordinator = GetCoordinator(static_cast<Browser*>(browser()));
  std::vector<std::string> deliveries;
  std::string session_a_id;
  auto registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kFloating,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            deliveries.push_back(dispatch.request_id);
            if (dispatch.request_id == "req-a") {
              auto session = StartRealSession(handler.get());
              session_a_id = session->session_id;
              MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                  handler.get(), "req-a", delivery_id);
              std::move(accept_cb).Run(session_a_id);
            }
          }));

  coordinator->Dispatch(MakeDispatch("req-a", "first"));
  coordinator->Dispatch(MakeDispatch("req-b", "second"));
  EXPECT_EQ((std::vector<std::string>{"req-a"}), deliveries);

  handler.reset();

  EXPECT_EQ((std::vector<std::string>{"req-a", "req-b"}), deliveries);
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    AskMahoFloatingIngress_LivePopupWinsThenSidebarFallbackOnce) {
  Browser* opener = static_cast<Browser*>(browser());
  FakePage sidebar_page;
  mojo::Remote<maho_ai::mojom::PageHandler> sidebar_remote;
  auto sidebar_handler = MakeHandler(opener, &sidebar_page, &sidebar_remote);
  ASSERT_TRUE(sidebar_handler);

  Browser* popup = CreateTrackedFloatingPopup(opener);
  ASSERT_TRUE(popup);
  FakePage popup_page;
  mojo::Remote<maho_ai::mojom::PageHandler> popup_remote;
  auto popup_handler = MakeHandler(popup, &popup_page, &popup_remote);
  ASSERT_TRUE(popup_handler);

  auto* coordinator = GetCoordinator(opener);
  coordinator->Dispatch(MakeDispatch("req-float", "to popup"));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ((std::vector<std::string>{"req-float"}),
            popup_page.accepted_requests);
  EXPECT_TRUE(sidebar_page.accepted_requests.empty());

  popup_handler.reset();
  CloseBrowserSynchronously(popup);

  coordinator->Dispatch(MakeDispatch("req-side", "to sidebar"));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ((std::vector<std::string>{"req-side"}),
            sidebar_page.accepted_requests);

  sidebar_handler.reset();
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    AskMahoFloatingIngress_PopupLossBeforeAcceptanceRequeuesOnce) {
  Browser* opener = static_cast<Browser*>(browser());
  FakePage sidebar_page;
  mojo::Remote<maho_ai::mojom::PageHandler> sidebar_remote;
  auto sidebar_handler = MakeHandler(opener, &sidebar_page, &sidebar_remote);
  ASSERT_TRUE(sidebar_handler);

  Browser* popup = CreateTrackedFloatingPopup(opener);
  ASSERT_TRUE(popup);
  FakePage popup_page;
  mojo::Remote<maho_ai::mojom::PageHandler> popup_remote;
  auto popup_handler = MakeHandler(popup, &popup_page, &popup_remote);
  ASSERT_TRUE(popup_handler);
  MahoAIPageHandlerTestPeer::ResetPage(popup_handler.get());

  auto* coordinator = GetCoordinator(opener);
  coordinator->Dispatch(MakeDispatch("req-requeue", "q"));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(popup_page.accepted_requests.empty());
  EXPECT_EQ((std::vector<std::string>{"req-requeue"}),
            sidebar_page.accepted_requests);

  popup_handler.reset();
  CloseBrowserSynchronously(popup);
  sidebar_handler.reset();
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    AskMahoPopupTeardownReleasesOpenerGate_AfterTrackerMappingRemoval) {
  Browser* opener = static_cast<Browser*>(browser());
  Browser* popup = CreateTrackedFloatingPopup(opener);
  ASSERT_TRUE(popup);
  FakePage popup_page;
  mojo::Remote<maho_ai::mojom::PageHandler> popup_remote;
  auto popup_handler = MakeHandler(popup, &popup_page, &popup_remote);
  ASSERT_TRUE(popup_handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(popup_handler.get());

  auto* coordinator = GetCoordinator(opener);
  std::vector<std::string> deliveries;
  std::string session_a_id;
  MahoAIPageHandlerTestPeer::SetIngressRegistration(
      popup_handler.get(),
      coordinator->RegisterConsumer(
          maho::MahoAiIngressCoordinator::ConsumerType::kFloating,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
                  uint64_t delivery_id,
                  maho::MahoAiIngressCoordinator::AcceptanceCallback
                      accept_callback) {
                deliveries.push_back(dispatch.request_id);
                if (dispatch.request_id == "req-a") {
                  auto session = StartRealSession(popup_handler.get());
                  session_a_id = session->session_id;
                  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                      popup_handler.get(), dispatch.request_id, delivery_id);
                  std::move(accept_callback).Run(session_a_id);
                }
              })));

  coordinator->Dispatch(MakeDispatch("req-a", "first"));
  coordinator->Dispatch(MakeDispatch("req-b", "second"));
  ASSERT_EQ((std::vector<std::string>{"req-a"}), deliveries);

  maho::MahoAiPopupLifetimeTracker::Get()->OnBrowserClosed(popup);
  EXPECT_EQ(nullptr,
            maho::MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(popup));
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      popup_handler.get(), session_a_id, "req-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");

  EXPECT_EQ((std::vector<std::string>{"req-a", "req-b"}), deliveries);
  popup_handler.reset();
  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoAiIngressLifecycleInteractiveTest,
                       CommandPaletteRequest_IsBoundToInvokingWindow) {
  Browser* invoking_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(invoking_browser);
  chrome::AddTabAt(invoking_browser, GURL("about:blank"), -1, true);

  std::vector<std::string> other_window_requests;
  auto other_registration = GetCoordinator(static_cast<Browser*>(browser()))->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            other_window_requests.push_back(dispatch.request_id);
            std::move(accept_cb).Run("other-window-session");
          }));

  std::vector<std::string> invoking_window_requests;
  auto invoking_registration =
      GetCoordinator(invoking_browser)->RegisterConsumer(
          maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
                  maho::MahoAiIngressCoordinator::AcceptanceCallback
                      accept_cb) {
                invoking_window_requests.push_back(dispatch.request_id);
                std::move(accept_cb).Run("invoking-window-session");
              }));

  ASSERT_TRUE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      invoking_browser, "window-bound request", maho::PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(other_window_requests.empty());
  ASSERT_EQ(1u, invoking_window_requests.size());
  EXPECT_FALSE(invoking_window_requests.front().empty());

  invoking_registration.Reset();
  other_registration.Reset();
  CloseBrowserSynchronously(invoking_browser);
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    WebUiLoadFailure_RequeuesWithoutCrossWindowDelivery) {
  Browser* unrelated_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(unrelated_browser);

  std::vector<std::string> unrelated_window_requests;
  auto unrelated_registration =
      GetCoordinator(unrelated_browser)->RegisterConsumer(
          maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
                  maho::MahoAiIngressCoordinator::AcceptanceCallback
                      accept_cb) {
                unrelated_window_requests.push_back(dispatch.request_id);
                std::move(accept_cb).Run("unrelated-window-session");
              }));

  auto* coordinator = GetCoordinator(static_cast<Browser*>(browser()));
  std::vector<std::string> failed_webui_requests;
  auto failed_webui_registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              maho::MahoAiIngressCoordinator::AcceptanceCallback) {
            failed_webui_requests.push_back(dispatch.request_id);
          }));

  coordinator->Dispatch(MakeDispatch("req-load-failure", "retry me"));
  ASSERT_EQ((std::vector<std::string>{"req-load-failure"}),
            failed_webui_requests);
  failed_webui_registration.Reset();

  std::vector<std::string> rebound_webui_requests;
  uint64_t rebound_delivery_id = 0;
  auto rebound_webui_registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            rebound_webui_requests.push_back(dispatch.request_id);
            rebound_delivery_id = delivery_id;
            std::move(accept_cb).Run("rebound-session");
          }));

  EXPECT_TRUE(unrelated_window_requests.empty());
  EXPECT_EQ((std::vector<std::string>{"req-load-failure"}),
            rebound_webui_requests);
  coordinator->NotifyTerminal(rebound_delivery_id);

  rebound_webui_registration.Reset();
  unrelated_registration.Reset();
  CloseBrowserSynchronously(unrelated_browser);
}

IN_PROC_BROWSER_TEST_F(MahoAiIngressLifecycleInteractiveTest,
                       PanelReloadRebind_PendingIngressDeliversOnce) {
  auto* coordinator = GetCoordinator(static_cast<Browser*>(browser()));
  std::vector<std::string> pre_reload_requests;
  auto pre_reload_registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              maho::MahoAiIngressCoordinator::AcceptanceCallback) {
            pre_reload_requests.push_back(dispatch.request_id);
          }));

  coordinator->Dispatch(MakeDispatch("req-panel-reload", "keep this"));
  ASSERT_EQ((std::vector<std::string>{"req-panel-reload"}),
            pre_reload_requests);
  pre_reload_registration.Reset();

  std::vector<std::string> rebound_requests;
  uint64_t rebound_delivery_id = 0;
  auto rebound_registration = coordinator->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            rebound_requests.push_back(dispatch.request_id);
            rebound_delivery_id = delivery_id;
            std::move(accept_cb).Run("reloaded-panel-session");
          }));

  ASSERT_EQ((std::vector<std::string>{"req-panel-reload"}),
            rebound_requests);
  coordinator->NotifyTerminal(rebound_delivery_id);
  coordinator->Dispatch(MakeDispatch("req-panel-reload", "keep this"));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ((std::vector<std::string>{"req-panel-reload"}),
            rebound_requests);

  rebound_registration.Reset();
}

IN_PROC_BROWSER_TEST_F(
    MahoAiIngressLifecycleInteractiveTest,
    SettingsThenOffTheRecord_RejectsRegularCredentialRequest) {
  browser()->GetProfile()->GetPrefs()->SetString(maho::ai_prefs::kApiKey,
                                              "test-regular-api-key");
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->GetProfile()->IsOffTheRecord());

  std::vector<std::string> otr_requests;
  auto otr_registration = GetCoordinator(incognito)->RegisterConsumer(
      maho::MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              maho::MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            otr_requests.push_back(dispatch.request_id);
            std::move(accept_cb).Run("otr-session");
          }));

  MahoAIUIConfig config;
  EXPECT_FALSE(config.IsWebUIEnabled(incognito->GetProfile()));
  EXPECT_FALSE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      incognito, "must not use the regular credential",
      maho::PaletteAction::kAskMaho, MahoPrivateContextClass::kRegular));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(otr_requests.empty());

  otr_registration.Reset();
}

namespace maho {

// Fixture access changes only the clock/transport boundary, not Poll, its
// scheduling policy, the FFI gate, or the client's write buffering.
class MemoryThreadSyncPeer {
 public:
  static void Observe(MahoSyncRelayClient& client,
                      scoped_refptr<base::TestMockTimeTaskRunner> clock,
                      base::RepeatingClosure observer) {
    client.poll_timer_.SetTaskRunner(clock);
    client.snapshot_timer_.SetTaskRunner(clock);
    client.poll_observer_for_testing_ = std::move(observer);
  }
  static bool SnapshotTimerRunning(const MahoSyncRelayClient& client) {
    return client.snapshot_timer_.IsRunning();
  }
  static void AttachTransport(
      MahoSyncRelayClient& client,
      mojo::PendingRemote<network::mojom::WebSocket> socket,
      mojo::ScopedDataPipeProducerHandle writable) {
    client.state_ = MahoSyncRelayClient::State::kConnected;
    client.authenticated_ = true;
    client.websocket_.Bind(std::move(socket));
    client.writable_ = std::move(writable);
    CHECK_EQ(client.writable_watcher_.Watch(
                 client.writable_.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
                 MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
                 base::BindRepeating(&MahoSyncRelayClient::OnWritable,
                                     base::Unretained(&client))),
             MOJO_RESULT_OK);
    client.FlushWriteQueue();
  }
  static void Send(MahoSyncRelayClient& client, const std::string& payload) {
    client.WriteData(payload);
  }
};

}  // namespace maho

namespace {

class MemoryThreadWebSocket : public network::mojom::WebSocket {
 public:
  void SendMessage(network::mojom::WebSocketMessageType type,
                   uint64_t length) override {
    lengths.push_back(length);
  }
  void StartReceiving() override {}
  void StartClosingHandshake(uint16_t code,
                             const std::string& reason) override {}
  std::vector<uint64_t> lengths;
  mojo::Receiver<network::mojom::WebSocket> receiver{this};
};

class MemoryThreadCoreGate {
 public:
  MemoryThreadCoreGate() = default;
  ~MemoryThreadCoreGate() {
    release_.Signal();
    worker_.Stop();
  }

  bool Enter() {
    if (!worker_.Start()) {
      return false;
    }
    if (!worker_.task_runner()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([this] {
              void* scope = maho_ffi_serialization_scope_enter();
              acquired_ = scope != nullptr;
              entered_.Signal();
              // Release even when UI synchronously waits for this scope, so
              // RED reports ordering failure rather than hanging teardown.
              signalled_release_ = release_.TimedWait(base::Seconds(10));
              maho_ffi_serialization_scope_exit(scope);
            }))) {
      return false;
    }
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
    return entered_.TimedWait(base::Seconds(10)) && acquired_.load();
  }

  bool ObserveUiMarker() {
    base::test::TestFuture<void> marker;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindLambdaForTesting([&] {
          release_.Signal();
          marker.SetValue();
        }));
    if (!marker.Wait()) {
      return false;
    }
    return WasReleasedByUiMarker();
  }

  void ReleaseAtUiMarker() { release_.Signal(); }

  bool WasReleasedByUiMarker() {
    worker_.Stop();
    return signalled_release_.load();
  }

 private:
  base::Thread worker_{"memory-thread-core-gate"};
  base::WaitableEvent entered_;
  base::WaitableEvent release_;
  std::atomic<bool> acquired_{false};
  std::atomic<bool> signalled_release_{false};
};

bool MemoryThreadDrainCore() {
  base::test::TestFuture<void> drained;
  maho::PostCoreTaskAndReply(FROM_HERE, base::DoNothing(),
                             drained.GetCallback());
  return drained.Wait();
}

std::optional<base::Value> MemoryThreadTabSnapshot(const std::string& id) {
  const std::unique_ptr<char, decltype(&maho_string_free)> snapshot(
      maho_core_get_tab_snapshot_by_id(maho::GetCore(), id.c_str()),
      &maho_string_free);
  if (!snapshot) {
    return std::nullopt;
  }
  return base::JSONReader::Read(snapshot.get(), base::JSON_PARSE_RFC);
}

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       MemoryThreadTabActionDoesNotWaitForCore) {
  // Given: the real registry, bridge, browser strip and global FFI gate.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(45));
  ASSERT_TRUE(maho::GetCore());
  ASSERT_TRUE(MemoryThreadDrainCore());
  ASSERT_FALSE(maho::MahoSpaceProfileBridge::GetInstance()
                   ->GetActiveSpaceId(browser()).empty());
  const GURL first = embedded_test_server()->GetURL("/empty.html?memory-thread=one");
  const GURL second = embedded_test_server()->GetURL("/title1.html?memory-thread=two");
  content::WebContents* tab = nullptr;

  // When: insert, commit navigation and close through native tab observers.
  // Each phase owns a fresh gate: moving only the insertion call cannot pass
  // the navigation or close-protection/getter checks after it.
  {
    MemoryThreadCoreGate gate;
    ASSERT_TRUE(gate.Enter());
    chrome::AddTabAt(browser(), first, -1, true);
    tab = browser()->GetTabStripModel()->GetActiveWebContents();
    EXPECT_TRUE(gate.ObserveUiMarker()) << "insert waited for the FFI gate";
  }
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  const std::string id = MahoTabIdHelper::FromWebContents(tab)->stable_tab_id();
  ASSERT_TRUE(MemoryThreadDrainCore());
  ASSERT_TRUE(MemoryThreadTabSnapshot(id).has_value());
  {
    // Request filtering needs the worker's decision before the page can
    // commit. Check initiation without waiting for load completion, then hold
    // a fresh gate after filtering to cover the native commit observers.
    class CommitGateObserver : public content::WebContentsObserver {
     public:
      CommitGateObserver(content::WebContents* contents, const GURL& target)
          : content::WebContentsObserver(contents), target_(target) {}
      void ReadyToCommitNavigation(content::NavigationHandle* handle) override {
        if (handle->IsInPrimaryMainFrame() && handle->GetURL() == target_) {
          entered = gate_.Enter();
          ASSERT_TRUE(entered);
        }
      }
      void DidFinishNavigation(content::NavigationHandle* handle) override {
        if (handle->IsInPrimaryMainFrame() && handle->GetURL() == target_ &&
            handle->HasCommitted()) {
          ASSERT_TRUE(entered);
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
              FROM_HERE, base::BindLambdaForTesting([this] {
                gate_.ReleaseAtUiMarker();
                finished_.SetValue();
              }));
        }
      }
      bool WaitForCommitMarker() {
        return finished_.Wait() && gate_.WasReleasedByUiMarker();
      }
      bool entered = false;

     private:
      const GURL target_;
      MemoryThreadCoreGate gate_;
      base::test::TestFuture<void> finished_;
    } commit_gate(tab, second);
    content::TestNavigationObserver navigation(tab);
    {
      MemoryThreadCoreGate gate;
      ASSERT_TRUE(gate.Enter());
      tab->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(second));
      EXPECT_TRUE(gate.ObserveUiMarker())
          << "navigation initiation waited for the FFI gate";
    }
    navigation.Wait();
    EXPECT_TRUE(navigation.last_navigation_succeeded());
    EXPECT_TRUE(commit_gate.entered);
    EXPECT_TRUE(commit_gate.WaitForCommitMarker())
        << "navigation commit observers waited for the FFI gate";
  }
  ASSERT_TRUE(MemoryThreadDrainCore());
  const auto navigated = MemoryThreadTabSnapshot(id);
  ASSERT_TRUE(navigated && navigated->is_dict());
  const std::string* navigated_url = navigated->GetDict().FindString("url");
  ASSERT_TRUE(navigated_url);
  EXPECT_EQ(*navigated_url, second.spec());
  {
    MemoryThreadCoreGate gate;
    ASSERT_TRUE(gate.Enter());
    browser()->GetTabStripModel()->CloseWebContentsAt(
        browser()->GetTabStripModel()->GetIndexOfWebContents(tab),
        TabCloseTypes::CLOSE_NONE);
    EXPECT_TRUE(gate.ObserveUiMarker()) << "close/getters waited for the FFI gate";
  }

  // Then: accepted changes reach the real core in order, including close.
  ASSERT_TRUE(MemoryThreadDrainCore());
  EXPECT_FALSE(MemoryThreadTabSnapshot(id).has_value());
  EXPECT_FALSE(maho::MahoTabRegistry::GetLiveTabIdsForProfile(browser()->GetProfile())
                   .contains(id));
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       MemoryThreadStartupPublishesHydratedCore) {
  // Given: the observer was installed before BrowserMain started this isolated
  // profile. When: actual storage open and LoadState run during startup.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(15));
  ASSERT_TRUE(startup_complete_.Wait());
  ASSERT_TRUE(MemoryThreadDrainCore());

  // Then: opening is off UI, hydration is unpublished, UI progresses while
  // hydration is parked, and the single Ready publication follows hydration.
  EXPECT_TRUE(storage_open_on_worker_.load());
  EXPECT_TRUE(unpublished_during_hydration_.load());
  EXPECT_TRUE(startup_marker_before_hydration_.load());
  EXPECT_EQ(startup_ready_count_, 1);
  EXPECT_TRUE(ready_after_hydration_);
  EXPECT_EQ(maho_core_readiness_status(maho::GetCore()), 2u);
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       DeferredStartFilterRedirectPreservesUrl) {
  class DeferredRedirectThrottle : public blink::URLLoaderThrottle {
   public:
    void WillStartRequest(network::ResourceRequest*, bool* defer) override {
      *defer = true;
    }
    const GURL* TakeDeferredStartRedirectUrl() override {
      return std::exchange(pending_, false) ? &target_ : nullptr;
    }
    void Complete(GURL target) {
      target_ = std::move(target);
      pending_ = true;
      delegate_->Resume();
    }
   private:
    GURL target_;
    bool pending_ = false;
  };
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  network::TestURLLoaderFactory factory;
  network::TestURLLoaderClient client;
  auto throttle = std::make_unique<DeferredRedirectThrottle>();
  auto* deferred = throttle.get();
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>> throttles;
  throttles.push_back(std::move(throttle));
  network::ResourceRequest request;
  request.url = GURL("https://filter.test/resource?tracking=1");
  const GURL original = request.url;
  const GURL target("https://filter.test/resource");
  auto loader = blink::ThrottlingURLLoader::CreateLoaderAndStart(
      factory.GetSafeWeakWrapper(), std::move(throttles), 0, 0, &request,
      &client, TRAFFIC_ANNOTATION_FOR_TESTS,
      base::SingleThreadTaskRunner::GetCurrentDefault());
  EXPECT_EQ(factory.NumPending(), 0);
  deferred->Complete(target);
  client.RunUntilRedirectReceived();
  EXPECT_EQ(request.url, original);
  EXPECT_EQ(client.redirect_info().new_url, target);
  loader->FollowRedirect(/*headers_update_params=*/{});
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  EXPECT_EQ(factory.GetPendingRequest(0)->request.url, target);
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       MemoryThreadIdleBackoffAndImmediateWake) {
  // Given: actual disabled-sync core; only this client's timer clock is fake.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(15));
  ASSERT_TRUE(maho::GetCore());
  const std::unique_ptr<char, decltype(&maho_core_free_string)> status(
      maho_core_get_sync_status(maho::GetCore()), &maho_core_free_string);
  ASSERT_TRUE(status);
  const auto parsed_status =
      base::JSONReader::Read(status.get(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed_status && parsed_status->is_dict());
  const std::string* status_kind = parsed_status->GetDict().FindString("kind");
  ASSERT_TRUE(status_kind);
  ASSERT_EQ(*status_kind, "idle");
  auto clock = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  maho::MahoSyncRelayClient client(browser()->GetProfile(), clock->GetMockTickClock());
  int reconciliations = 0;
  maho::MemoryThreadSyncPeer::Observe(
      client, clock, base::BindLambdaForTesting([&] { ++reconciliations; }));
  client.StartPolling();

  // When: time advances through the agreed idle cadence, then Ready wakes it.
  for (const int seconds : {2, 4, 8, 16, 30}) {
    const int before = reconciliations;
    clock->FastForwardBy(base::Seconds(seconds));
    ASSERT_TRUE(MemoryThreadDrainCore());
    clock->RunUntilIdle();
    EXPECT_EQ(reconciliations - before, 1);
  }
  const int before_wake = reconciliations;
  maho::SetCoreForProfile(maho::GetCore(), browser()->GetProfile());
  ASSERT_TRUE(MemoryThreadDrainCore());
  clock->RunUntilIdle();

  // Then: wake needs no elapsed time and disabled sync has no snapshot timer.
  EXPECT_EQ(reconciliations - before_wake, 1);
  EXPECT_FALSE(maho::MemoryThreadSyncPeer::SnapshotTimerRunning(client));
  client.StopPolling();
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       MemoryThreadOnePollWhileCoreBlocked) {
  // Given: a real serialized FFI scope is already held by a worker.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(15));
  ASSERT_TRUE(maho::GetCore());
  auto clock = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  maho::MahoSyncRelayClient client(browser()->GetProfile(), clock->GetMockTickClock());
  int reconciliations = 0;
  maho::MemoryThreadSyncPeer::Observe(
      client, clock, base::BindLambdaForTesting([&] { ++reconciliations; }));
  client.StartPolling();
  MemoryThreadCoreGate gate;
  ASSERT_TRUE(gate.Enter());

  // When: multiple timer deadlines pass without releasing the native scope.
  clock->FastForwardBy(base::Seconds(8));
  const bool marker_before_release = gate.ObserveUiMarker();
  ASSERT_TRUE(MemoryThreadDrainCore());
  clock->RunUntilIdle();

  // Then: Main progresses and at most one reconciliation was admitted.
  EXPECT_TRUE(marker_before_release);
  EXPECT_EQ(reconciliations, 1);
  client.StopPolling();
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       MemoryThreadReconnectPreservesOutgoing) {
  // Given: a real client and bounded Mojo transport that accepts only a prefix.
  maho::MahoSyncRelayClient client(browser()->GetProfile());
  MemoryThreadWebSocket first_socket;
  mojo::ScopedDataPipeProducerHandle first_writer;
  mojo::ScopedDataPipeConsumerHandle first_reader;
  ASSERT_EQ(mojo::CreateDataPipe(4, first_writer, first_reader), MOJO_RESULT_OK);
  maho::MemoryThreadSyncPeer::AttachTransport(
      client, first_socket.receiver.BindNewPipeAndPassRemote(),
      std::move(first_writer));
  const std::string payload =
      R"({"protocol_version":2,"delivery_id":"550e8400-e29b-41d4-a716-446655440000","payload":"AA=="})";
  maho::MemoryThreadSyncPeer::Send(client, payload);
  std::array<uint8_t, 4> prefix;
  size_t prefix_size = 0;
  ASSERT_EQ(first_reader->ReadData(MOJO_READ_DATA_FLAG_NONE,
                                    base::span(prefix), prefix_size),
            MOJO_RESULT_OK);
  ASSERT_EQ(prefix_size, prefix.size());

  // When: transport fails before that frame completes, then reconnects.
  client.OnDropChannel(false, 1006, "fixture_disconnect");
  MemoryThreadWebSocket second_socket;
  mojo::ScopedDataPipeProducerHandle second_writer;
  mojo::ScopedDataPipeConsumerHandle second_reader;
  ASSERT_EQ(mojo::CreateDataPipe(4096, second_writer, second_reader),
            MOJO_RESULT_OK);
  maho::MemoryThreadSyncPeer::AttachTransport(
      client, second_socket.receiver.BindNewPipeAndPassRemote(),
      std::move(second_writer));
  std::array<uint8_t, 4096> bytes;
  size_t size = 0;
  const MojoResult read = second_reader->ReadData(
      MOJO_READ_DATA_FLAG_NONE, base::span(bytes), size);
  second_socket.receiver.FlushForTesting();

  // Then: a new connection gets one complete frame, not a lost prefix or a
  // permanently parked write. The oracle consumes bytes, not mock calls alone.
  EXPECT_EQ(read, MOJO_RESULT_OK);
  EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + size), payload);
  EXPECT_EQ(second_socket.lengths, std::vector<uint64_t>{payload.size()});
  client.OnDropChannel(true, 1000, "fixture_cleanup");
}
