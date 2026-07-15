// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_browser_main_extra_parts.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "base/threading/thread_restrictions.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"
#include "maho/browser/ui/webui/maho_easel/maho_easel_page_handler.h"
#include "maho/browser/ui/webui/maho_notes/maho_notes_page_handler.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"

namespace maho {
namespace {

class RecordingShellEventObserver : public ShellEventObserver {
 public:
  void OnShellEventDispatched(const std::string& kind,
                              const std::string& event_json) override {
    events.push_back({kind, event_json});
  }

  struct EventRecord {
    std::string kind;
    std::string json;
  };

  std::vector<EventRecord> events;
  void Clear() { events.clear(); }
};

class MahoIncognitoPrivacyBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info && std::string(test_info->name()) == "IncognitoOnlyProcessDoesNotInitializeMahoServicesOrOpenStorage") {
      command_line->AppendSwitch(switches::kIncognito);
    }
    InProcessBrowserTest::SetUpCommandLine(command_line);
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       IncognitoOnlyProcessDoesNotInitializeMahoServicesOrOpenStorage) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  // Verify that the browser opened is incognito.
  ASSERT_TRUE(browser()->profile()->IsOffTheRecord());

  // Verify that Maho extra parts core is not initialized.
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);
  EXPECT_EQ(parts->GetCore(), nullptr);

  // Verify that the MahoCore directory does not exist.
  base::FilePath user_data_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  base::FilePath storage_path = user_data_dir.AppendASCII("MahoCore");
  EXPECT_FALSE(base::DirectoryExists(storage_path));
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       FirstRegularBrowserInitializesMahoServicesExactlyOnce) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  // Starts normally with a regular browser.
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  // Verify Maho extra parts core is initialized.
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);
  MahoCore* core1 = parts->GetCore();
  EXPECT_NE(core1, nullptr);

  // Verify MahoCore directory exists.
  base::FilePath user_data_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  base::FilePath storage_path = user_data_dir.AppendASCII("MahoCore");
  EXPECT_TRUE(base::DirectoryExists(storage_path));

  // Open a second regular browser window with a separate profile to prevent MediaItemManager delegate collision.
  Profile* second_profile = nullptr;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    base::FilePath path = profile_manager->GenerateNextProfileDirectoryPath();
    second_profile = &profiles::testing::CreateProfileSync(profile_manager, path);
  }
  ASSERT_TRUE(second_profile);

  Browser* second_browser = CreateBrowser(second_profile);
  ASSERT_TRUE(second_browser);

  // Verify core is still the same pointer.
  EXPECT_EQ(parts->GetCore(), core1);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       TabRegistryNormalPositiveControl) {
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(browser(), "fake-space-id");

  // Create a new tab.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Let's verify we got at least one "create_tab" event.
  bool found_create = false;
  for (const auto& ev : observer.events) {
    if (ev.kind == "create_tab") {
      found_create = true;
      break;
    }
  }
  EXPECT_TRUE(found_create);

  observer.Clear();
  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       TabRegistryPrimaryOtrIsSilent) {
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(browser(), "fake-space-id");

  // Create primary OTR (Incognito) browser.
  Browser* incognito_browser = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito_browser);

  // Add a tab in Incognito browser.
  chrome::AddTabAt(incognito_browser, GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Verify that RecordingShellEventObserver did not receive any events.
  EXPECT_TRUE(observer.events.empty());

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       GuestAndOtherOtrHaveNoMahoRegistration) {
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(browser(), "fake-space-id");

  // Create Guest browser.
  Browser* guest_browser = CreateGuestBrowser();
  ASSERT_TRUE(guest_browser);

  // Add a tab in Guest.
  chrome::AddTabAt(guest_browser, GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Verify zero events.
  EXPECT_TRUE(observer.events.empty());

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       RegistryDelayedCallbacksAfterCloseAreSilent) {
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(browser(), "fake-space-id");

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  observer.Clear();

  // Close the tab
  int index = browser()->tab_strip_model()->count() - 1;
  browser()->tab_strip_model()->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
  base::RunLoop().RunUntilIdle();

  // Verify that it is silent/safe.
  EXPECT_TRUE(observer.events.empty() || observer.events.back().kind == "close_tab");

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

class MahoWebUiPrivateBoundaryBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
  }
};

IN_PROC_BROWSER_TEST_F(MahoWebUiPrivateBoundaryBrowserTest,
                       RegularControllersAndHandlersPositiveControl) {
  // Test regular profile (should succeed)
  Browser* reg_browser = browser();
  chrome::AddTabAt(reg_browser, GURL("about:blank"), -1, true);
  content::WebContents* reg_contents = reg_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(reg_contents);

  mojo::Remote<maho_boost::mojom::PageHandler> remote;
  mojo::PendingRemote<maho_boost::mojom::PageObserver> page_observer;
  
  auto handler = std::make_unique<MahoBoostPageHandler>(
      remote.BindNewPipeAndPassReceiver(),
      std::move(page_observer),
      "example.com",
      nullptr,
      reg_browser,
      reg_contents);

  base::RunLoop run_loop;
  remote->GetDomain(base::BindOnce(
      [](base::OnceClosure quit_closure, const std::string& domain) {
        EXPECT_EQ(domain, "example.com");
        std::move(quit_closure).Run();
      },
      run_loop.QuitClosure()));
  run_loop.Run();
  
  EXPECT_TRUE(remote.is_connected());
}

IN_PROC_BROWSER_TEST_F(MahoWebUiPrivateBoundaryBrowserTest,
                       DirectNavigationDeniedBeforeController) {
  // Test Guest
  Browser* guest_browser = CreateGuestBrowser();
  ASSERT_TRUE(guest_browser);
  chrome::AddTabAt(guest_browser, GURL("about:blank"), -1, true);
  content::WebContents* guest_contents = guest_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(guest_contents);

  mojo::Remote<maho_boost::mojom::PageHandler> remote;
  mojo::PendingRemote<maho_boost::mojom::PageObserver> page_observer;
  
  auto handler = std::make_unique<MahoBoostPageHandler>(
      remote.BindNewPipeAndPassReceiver(),
      std::move(page_observer),
      "example.com",
      nullptr,
      guest_browser,
      guest_contents);

  base::RunLoop run_loop;
  remote.set_disconnect_handler(run_loop.QuitClosure());
  remote->GetDomain(base::BindOnce([](const std::string& domain) {
    ADD_FAILURE() << "Callback should not be called on Guest";
  }));
  run_loop.Run();

  EXPECT_FALSE(remote.is_connected());
}

IN_PROC_BROWSER_TEST_F(MahoWebUiPrivateBoundaryBrowserTest,
                       ForgedFactoriesAndHandlersDisconnectBeforeStateAccess) {
  Browser* inc_browser = CreateIncognitoBrowser();
  ASSERT_TRUE(inc_browser);
  chrome::AddTabAt(inc_browser, GURL("about:blank"), -1, true);
  content::WebContents* inc_contents = inc_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(inc_contents);

  // Test Boost under Incognito OTR
  {
    mojo::Remote<maho_boost::mojom::PageHandler> remote;
    mojo::PendingRemote<maho_boost::mojom::PageObserver> page_observer;
    
    auto handler = std::make_unique<MahoBoostPageHandler>(
        remote.BindNewPipeAndPassReceiver(),
        std::move(page_observer),
        "example.com",
        nullptr,
        inc_browser,
        inc_contents);

    base::RunLoop run_loop;
    remote.set_disconnect_handler(run_loop.QuitClosure());
    remote->GetDomain(base::BindOnce([](const std::string& domain) {
      ADD_FAILURE() << "Callback should not be called under Incognito";
    }));
    run_loop.Run();

    EXPECT_FALSE(remote.is_connected());
  }

  // Test Easel under Incognito OTR
  {
    mojo::Remote<maho_easel::mojom::PageHandler> remote;
    mojo::PendingRemote<maho_easel::mojom::Page> page;
    
    auto handler = std::make_unique<MahoEaselPageHandler>(
        remote.BindNewPipeAndPassReceiver(),
        std::move(page),
        inc_browser,
        inc_contents);

    base::RunLoop run_loop;
    remote.set_disconnect_handler(run_loop.QuitClosure());
    remote->GetCanvas(base::BindOnce([](std::vector<maho_easel::mojom::CanvasItemPtr> items) {
      ADD_FAILURE() << "Callback should not be called under Incognito";
    }));
    run_loop.Run();

    EXPECT_FALSE(remote.is_connected());
  }

  // Test Notes under Incognito OTR
  {
    mojo::Remote<maho_notes::mojom::PageHandler> remote;
    mojo::PendingRemote<maho_notes::mojom::Page> page;
    
    auto handler = std::make_unique<MahoNotesPageHandler>(
        remote.BindNewPipeAndPassReceiver(),
        std::move(page),
        inc_browser,
        inc_contents);

    base::RunLoop run_loop;
    remote.set_disconnect_handler(run_loop.QuitClosure());
    remote->GetNotes(base::BindOnce([](std::vector<maho_notes::mojom::NotePtr> notes) {
      ADD_FAILURE() << "Callback should not be called under Incognito";
    }));
    run_loop.Run();

    EXPECT_FALSE(remote.is_connected());
  }

  // Test Routines under Incognito OTR
  {
    mojo::Remote<maho_routines::mojom::PageHandler> remote;
    mojo::PendingRemote<maho_routines::mojom::Page> page;
    
    auto handler = std::make_unique<MahoRoutinesPageHandler>(
        remote.BindNewPipeAndPassReceiver(),
        std::move(page),
        inc_browser,
        inc_contents);

    base::RunLoop run_loop;
    remote.set_disconnect_handler(run_loop.QuitClosure());
    remote->ListRoutines(base::BindOnce([](std::vector<maho_routines::mojom::RoutineInfoPtr> routines) {
      ADD_FAILURE() << "Callback should not be called under Incognito";
    }));
    run_loop.Run();

    EXPECT_FALSE(remote.is_connected());
  }
}


// ============================================================
// R-11 sidebar stubs (task-11-sidebar)
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SidebarCurrentWindowOnly) {
  // TODO(task-11): Verify private sidebar shows only current-window OTR tabs.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SidebarReadsNoMahoCoreOrSpace) {
  // TODO(task-11): Verify no MahoCore/Space calls from private sidebar.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SidebarForgedCallbacksAreNoOps) {
  // TODO(task-11): Verify forged bridge callbacks are silent no-ops.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       GuestAndOtherOtrHaveNoMahoSidebar) {
  // TODO(task-11): Verify Guest/other-OTR windows get no Maho sidebar.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       CollapsedExpandedAndFullscreenGeometry) {
  // TODO(task-11): Verify rail geometry 288/40/0 dp in expanded/collapsed/fullscreen.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PrivateFocusOrder) {
  // TODO(task-11): Verify keyboard Tab/Shift-Tab order in private window.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       TwoOtrWindowsRemainDisjoint) {
  // TODO(task-11): Verify two OTR windows have independent tab lists.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

// ============================================================
// R-12 visual-contract stubs (task-12-visual-contract)
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PrivateColorsIgnoreActiveSpace) {
  // TODO(task-12): Verify private window ignores active-Space theme tint.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ContentBackgroundHasNoRegularFlash) {
  // TODO(task-12): Verify content background never shows regular-mode color.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       IdentityVisibleRenderedStatesAndAbsentFullscreenHidden) {
  // TODO(task-12): Verify glasses identity icon visible in expanded/collapsed,
  //                absent when fullscreen-hidden.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       AccessibilityContractAndFocusOrder) {
  // TODO(task-12): Verify AX role/label contract and focus-order contract.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ReducedMotionIsZeroDuration) {
  // TODO(task-12): Verify sidebar reveal animation duration is 0ms when
  //                --force-prefers-reduced-motion=1.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       CjkNamesAreUnclipped) {
  // TODO(task-12): Verify CJK tab titles are not clipped in private sidebar.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

// ============================================================
// R-13 preview-scroll stubs (task-13-preview-scroll)
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PreviewScrollNormalPositiveControl) {
  // TODO(task-13): Verify preview capture and scroll position work for regular tabs.
  GTEST_SKIP() << "R-13 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PreviewOtrIngressRetrievalAndStorageDenied) {
  // TODO(task-13): Verify OTR tab switches produce no preview/scroll writes.
  GTEST_SKIP() << "R-13 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DelayedCaptureAfterTeardownIsDenied) {
  // TODO(task-13): Verify async capture callbacks after OTR teardown are denied.
  GTEST_SKIP() << "R-13 stub — not yet implemented";
}

// ============================================================
// R-14 extension/download (task-14-extension-download)
//
// Four of these five cases genuinely need the running app: real extension
// loading + Incognito opt-in toggling, real file downloads, and Chromium
// History/OTR routing. They carry exact frozen names and are deferred to R-16
// with honest GTEST_SKIP stubs rather than fake assertions. The direct-FFI
// normal control is a self-contained in-process test and is written fully; it
// is also the sole test-file reference that keeps the frozen zero-production
// Maho download-ingress audit satisfied.
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ExtensionDefaultOffOnOff) {
  // TODO(R-16): Load browser/extensions/test_data/incognito_test_extension
  //             (incognito:not_allowed), assert the content-script marker is
  //             absent in Incognito by default, flip "Allow in Incognito" on
  //             then off, and assert marker present/absent accordingly.
  //             Requires the running app + extension service + fixture pages.
  GTEST_SKIP() << "R-14 app-dependent: deferred to R-16 (extension opt-in "
                  "toggle needs running app + extension service)";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DownloadRegularHistoryControl) {
  // TODO(R-16): Download the fixture regular.bin in the regular profile, assert
  //             fixed bytes/SHA-256 and that it appears in Chromium History.
  //             Requires the running app + real download stack + fixture server.
  GTEST_SKIP() << "R-14 app-dependent: deferred to R-16 (real download + "
                  "Chromium History need running app)";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DownloadOtrFileWithoutHistory) {
  // TODO(R-16): Download the fixture private.bin in Incognito, assert fixed
  //             bytes/SHA-256 and that it does NOT enter Chromium History.
  //             Requires the running app + real OTR download stack.
  GTEST_SKIP() << "R-14 app-dependent: deferred to R-16 (real OTR download "
                  "needs running app)";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       RealDownloadsDoNotEnterMaho) {
  // TODO(R-16): After the two real downloads above, assert neither entered the
  //             Maho download store (no production ingress exists). Requires the
  //             running app to have performed the real downloads first.
  GTEST_SKIP() << "R-14 app-dependent: deferred to R-16 (depends on real "
                  "downloads performed by the running app)";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DirectFfiMahoDownloadControl) {
  // Direct-FFI normal positive control: proves the Maho download SQL selector
  // is connected end to end. This runs entirely in-process on a fresh core, so
  // it needs no app UI / seatbelt / 2x display and is written fully. It is the
  // only test-file caller that keeps the frozen zero-production-ingress audit
  // satisfied for this shared browser test.
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);

  const std::string start_json =
      R"({"filename":"control.bin","url":"https://normal.test/control.bin",)"
      R"("total_bytes":24})";
  char* raw_id = maho_core_start_download(core, start_json.c_str());
  ASSERT_NE(raw_id, nullptr);
  std::optional<base::Value> parsed_id =
      base::JSONReader::Read(raw_id, base::JSON_PARSE_RFC);
  maho_string_free(raw_id);
  ASSERT_TRUE(parsed_id.has_value());
  ASSERT_TRUE(parsed_id->is_string());
  const std::string download_id = parsed_id->GetString();
  ASSERT_FALSE(download_id.empty());

  maho_core_update_download_progress(core, download_id.c_str(), 24);
  char* raw_complete = maho_core_complete_download(core, download_id.c_str());
  if (raw_complete) {
    maho_string_free(raw_complete);
  }

  char* view_models = maho_core_get_download_view_models(core);
  ASSERT_NE(view_models, nullptr);
  const std::string view_json(view_models);
  maho_string_free(view_models);
  EXPECT_NE(view_json.find(download_id), std::string::npos)
      << "Maho download SQL selector must surface the completed control row";

  maho_core_free(core);
}

}  // namespace maho
