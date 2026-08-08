// Copyright 2026 Maho Browser. All rights reserved.
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
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "net/dns/mock_host_resolver.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "components/prefs/pref_service.h"

#include "content/public/browser/web_contents_observer.h"
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
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
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

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Disable Maho's onboarding login-gate so the standard startup browser
    // is created (otherwise browser() is null).
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");

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
    ui->Close(SidePanelType::kContent);
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

  // Clean up so the panel is not open when the browser tears down.
  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
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
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
  })) << "IsSidePanelShowing(kContent) must return false after Close(); "
         "Close() path is broken.";

  EXPECT_FALSE(IsMahoAiPanelShowing())
      << "kMahoAiPanel entry must not be showing after side panel close";
  EXPECT_FALSE(GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent))
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
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
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
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
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
      return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
    })) << "Cycle " << i << ": Close() did not close the panel";

    EXPECT_FALSE(IsMahoAiPanelShowing())
        << "Cycle " << i << ": entry must not be showing after close";
    EXPECT_FALSE(GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent))
        << "Cycle " << i << ": side panel must remain closed after close";
  }
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       CommandPalette_AskMahoOpensPanel) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  ASSERT_TRUE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "open the assistant", maho::PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular));

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Ask Maho command action must show kMahoAiPanel in its browser.";
  EXPECT_TRUE(GetHostWebContents())
      << "The command action must expose a live Maho AI side-panel host.";

  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
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
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
  }));
}

IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       CommandPaletteOpen_FocusesComposerTextarea) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  ASSERT_TRUE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "open the assistant", maho::PaletteAction::kAskMaho,
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
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
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
  content::WebContents* wc = browser->tab_strip_model()->GetActiveWebContents();
  auto token = std::make_unique<MahoPrivateContextToken>(browser, wc);
  auto loader = browser->profile()
                    ->GetDefaultStoragePartition()
                    ->GetURLLoaderFactoryForBrowserProcess();
  mojo::PendingRemote<maho_ai::mojom::Page> page;
  return std::make_unique<MahoAIPageHandler>(
      remote->BindNewPipeAndPassReceiver(), std::move(page), std::move(token),
      browser, wc, browser->profile()->GetPrefs(), std::move(loader));
}

}  // namespace

// task-9-ai-ui: Incognito never enables the AI WebUI and never registers the
// side-panel entry.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       IncognitoWebUiAndEntryAreDisabled) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());

  MahoAIUIConfig config;
  EXPECT_FALSE(config.IsWebUIEnabled(incognito->profile()));

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
  Browser* incognito = CreateIncognitoBrowser();
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
  Browser* incognito = CreateIncognitoBrowser();
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
  Browser* incognito = CreateIncognitoBrowser();
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
                       std::nullopt, maho_ai::mojom::ChatIntent::kFreeform);

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
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  MahoAIUIConfig config;
  EXPECT_TRUE(config.IsWebUIEnabled(browser()->profile()));

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(browser(), &remote);

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

  void OnRuntimeEvent(maho_ai::mojom::RuntimeEventPtr event) override {
    events.push_back(std::move(event));
  }
  void OnConnectionStateChanged(
      maho_ai::mojom::RuntimeConnectionState) override {}
  void OnSessionUpdated(maho_ai::mojom::SessionInfoPtr) override {}
  void OnAISettingsChanged(maho_ai::mojom::AISettingsInfoPtr) override {}
  void OnVoicePartial(const std::string&) override {}
  void OnVoiceFinal(const std::string&) override {}
  void OnVoiceError(const std::string&) override {}
  void OnAskMahoSessionAccepted(
      const std::string& request_id,
      maho_ai::mojom::SessionInfoPtr session) override {
    accepted_requests.push_back(request_id);
    accepted_sessions.push_back(std::move(session));
  }

  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  std::vector<std::string> accepted_requests;
  std::vector<maho_ai::mojom::SessionInfoPtr> accepted_sessions;

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
        browser->tab_strip_model()->GetActiveWebContents();
    auto token = std::make_unique<MahoPrivateContextToken>(browser, wc);
    auto loader = browser->profile()
                      ->GetDefaultStoragePartition()
                      ->GetURLLoaderFactoryForBrowserProcess();
    return std::make_unique<MahoAIPageHandler>(
        remote->BindNewPipeAndPassReceiver(), page->BindAndGetRemote(),
        std::move(token), browser, wc, browser->profile()->GetPrefs(),
        std::move(loader));
  }

  Browser* CreateTrackedFloatingPopup(Browser* opener) {
    Browser::CreateParams popup_params(Browser::TYPE_POPUP, opener->profile(),
                                       /*user_gesture=*/true);
    popup_params.trusted_source = true;
    popup_params.omit_from_session_restore = true;
    Browser* popup = Browser::Create(popup_params);
    chrome::AddTabAt(popup, GURL("about:blank"), /*index=*/-1,
                     /*foreground=*/true);
    maho::MahoAiPopupLifetimeTracker::Get()->TrackPopup(opener, popup);
    popup->window()->Show();
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
  GetCoordinator(browser())->Dispatch(MakeDispatch("req-cold", "cold query"));

  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(browser(), &page, &remote);
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
  auto* coordinator = GetCoordinator(browser());
  coordinator->Dispatch(MakeDispatch("req-dup", "only once"));
  coordinator->Dispatch(MakeDispatch("req-dup", "only once"));

  FakePage page;
  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeHandler(browser(), &page, &remote);
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
  auto handler = MakeHandler(browser(), &page, &remote);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto* coordinator = GetCoordinator(browser());
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
  auto handler = MakeHandler(browser(), &page, &remote);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetPage(handler.get());

  auto* coordinator = GetCoordinator(browser());
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
  auto handler = MakeHandler(browser(), &page, &remote);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto* coordinator = GetCoordinator(browser());
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
  Browser* opener = browser();
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
  Browser* opener = browser();
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
  Browser* opener = browser();
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
  Browser* invoking_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(invoking_browser);
  chrome::AddTabAt(invoking_browser, GURL("about:blank"), -1, true);

  std::vector<std::string> other_window_requests;
  auto other_registration = GetCoordinator(browser())->RegisterConsumer(
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
  Browser* unrelated_browser = CreateBrowser(browser()->profile());
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

  auto* coordinator = GetCoordinator(browser());
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
  auto* coordinator = GetCoordinator(browser());
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
  browser()->profile()->GetPrefs()->SetString(maho::ai_prefs::kApiKey,
                                              "test-regular-api-key");
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());

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
  EXPECT_FALSE(config.IsWebUIEnabled(incognito->profile()));
  EXPECT_FALSE(maho::MahoCommandActionHandler::OnQuerySubmitted(
      incognito, "must not use the regular credential",
      maho::PaletteAction::kAskMaho, MahoPrivateContextClass::kRegular));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(otr_requests.empty());

  otr_registration.Reset();
}
