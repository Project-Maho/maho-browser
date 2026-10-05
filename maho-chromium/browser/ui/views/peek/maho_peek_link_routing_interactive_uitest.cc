// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/net/maho_atc_state.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/peek/maho_peek_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/page_transition_types.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/views/view_utils.h"

namespace maho {
namespace {

constexpr char kSourcePath[] = "/maho-peek-link-source";
constexpr char kTargetPath[] = "/maho-peek-link-target";
constexpr char kRedirectPath[] = "/maho-peek-link-redirect";
constexpr char kRedirectTargetPath[] = "/maho-peek-link-redirect-target";
constexpr char kAtcHost[] = "sub.peek-atc.test";
constexpr char kAtcPattern[] = "*.peek-atc.test";

std::unique_ptr<net::test_server::HttpResponse> HandleLinkRoutingRequest(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (request.relative_url == kSourcePath) {
    response->set_code(net::HTTP_OK);
    response->set_content_type("text/html");
    response->set_content(R"html(
      <!doctype html>
      <a id="same-tab" href="/maho-peek-link-target">same tab</a>
      <a id="redirect" href="/maho-peek-link-redirect">redirect</a>
    )html");
    return response;
  }
  if (request.relative_url == kTargetPath ||
      request.relative_url == kRedirectTargetPath) {
    response->set_code(net::HTTP_OK);
    response->set_content_type("text/html");
    response->set_content("<!doctype html><title>Peek target</title>");
    return response;
  }
  if (request.relative_url == kRedirectPath) {
    response->set_code(net::HTTP_FOUND);
    response->AddCustomHeader("Location", kRedirectTargetPath);
    return response;
  }
  return nullptr;
}

class MahoPeekLinkRoutingInteractiveUiTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleLinkRoutingRequest));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
    if (browser()->GetTabStripModel()->empty()) {
      chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                       /*foreground=*/true);
    }
    ASSERT_TRUE(browser()->GetTabStripModel()->GetActiveWebContents());
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    MahoAtcState::SetHasEnabledRules(false);
  }

  void TearDownOnMainThread() override {
    MahoAtcState::SetHasEnabledRules(false);
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  MahoPeekController* controller() {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    return view ? view->GetOrCreateMahoPeekController() : nullptr;
  }

  content::WebContents* active_contents() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }

  content::WebContents* PreparePinnedSource() {
    const GURL source_url = embedded_test_server()->GetURL(kSourcePath);
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), source_url));
    content::WebContents* source = active_contents();
    EXPECT_TRUE(source);
    const int index =
        browser()->GetTabStripModel()->GetIndexOfWebContents(source);
    EXPECT_NE(TabStripModel::kNoTab, index);
    if (index != TabStripModel::kNoTab) {
      browser()->GetTabStripModel()->SetTabPinned(index, true);
    }
    EXPECT_EQ(PeekSourceRole::kPinned,
              MahoSidebarContainerView::GetPeekSourceRole(static_cast<Browser*>(browser()), source));
    return source;
  }

  content::WebContents* PrepareFavoriteSource() {
    const GURL source_url = embedded_test_server()->GetURL(kSourcePath);
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), source_url));
    content::WebContents* favorite = active_contents();
    EXPECT_TRUE(favorite);
    if (!favorite) {
      return nullptr;
    }

    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    auto* container =
        browser_view
            ? views::AsViewClass<MahoSidebarContainerView>(
                  browser_view->maho_sidebar_container())
            : nullptr;
    auto* sidebar =
        container ? views::AsViewClass<MahoSidebarView>(container->sidebar_view())
                  : nullptr;
    MahoSidebarTabListView* tab_list = sidebar ? sidebar->tab_list_view() : nullptr;
    EXPECT_TRUE(tab_list);
    if (!tab_list) {
      return nullptr;
    }
    const int index =
        browser()->GetTabStripModel()->GetIndexOfWebContents(favorite);
    EXPECT_NE(TabStripModel::kNoTab, index);
    if (index == TabStripModel::kNoTab) {
      return nullptr;
    }
    browser()->GetTabStripModel()->SetTabPinned(index, false);

    const std::string tab_id = tab_list->FindCoreTabIdByWebContents(favorite);
    EXPECT_FALSE(tab_id.empty());
    if (tab_id.empty()) {
      return nullptr;
    }

    DispatchShellEvent("favorite_tab", {{"tab_id", tab_id}});
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return MahoSidebarContainerView::GetPeekSourceRole(static_cast<Browser*>(browser()), favorite) ==
             PeekSourceRole::kFavorite;
    }));
    EXPECT_EQ(source_url, favorite->GetLastCommittedURL());
    return favorite;
  }

  void OpenFromTab(content::WebContents* source,
                   const GURL& target,
                   WindowOpenDisposition disposition) {
    content::TestNavigationObserver navigation(target);
    navigation.StartWatchingNewWebContents();
    content::OpenURLParams params(target, content::Referrer(), disposition,
                                  ui::PAGE_TRANSITION_LINK,
                                  /*is_renderer_initiated=*/false);
    params.user_gesture = true;
    BrowserWebContentsDelegate::From(browser())->OpenURLFromTab(
        source, params, base::NullCallback());
    navigation.Wait();
    EXPECT_TRUE(navigation.last_navigation_succeeded());
  }

  content::WebContents* WaitForPeekAt(const GURL& target) {
    MahoPeekController* peek = controller();
    EXPECT_TRUE(peek);
    if (!peek) {
      return nullptr;
    }
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return peek->state_for_testing() == MahoPeekController::State::Open &&
             peek->peek_view_for_testing() &&
             peek->peek_view_for_testing()->hosted_contents() &&
             peek->peek_view_for_testing()
                     ->hosted_contents()
                     ->GetLastCommittedURL() == target;
    }));
    return peek->peek_view_for_testing()
               ? peek->peek_view_for_testing()->hosted_contents()
               : nullptr;
  }

  std::string RegisterAtcTargetSpace() {
    MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
    EXPECT_TRUE(bridge);
    if (!bridge) {
      return std::string();
    }
    constexpr char kTargetSpace[] = "peek-routing-atc-space";
    bridge->RegisterSpace(kTargetSpace, browser()->GetProfile()->GetPath().BaseName());
    return kTargetSpace;
  }

  void AddAtcRule(const std::string& id, const std::string& target_space) {
    MahoCore* core = GetCore();
    ASSERT_TRUE(core);
    const std::string rule_json =
        R"({"id":")" + id + R"(","urlPattern":")" + kAtcPattern +
        R"(","matchType":"glob","targetSpaceId":")" + target_space +
        R"(","enabled":true})";
    char* result = maho_core_add_traffic_rule(core, rule_json.c_str());
    ASSERT_TRUE(result);
    maho_string_free(result);
    MahoAtcState::SetHasEnabledRules(true);
  }

  void RemoveAtcRule(const std::string& id) {
    if (MahoCore* core = GetCore()) {
      if (char* result =
              maho_core_remove_traffic_rule_persisted(core, id.c_str())) {
        maho_string_free(result);
      }
    }
    MahoAtcState::SetHasEnabledRules(false);
  }

  void HidePeek() {
    MahoPeekController* peek = controller();
    ASSERT_TRUE(peek);
    peek->Hide();
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return peek->state_for_testing() == MahoPeekController::State::Closed;
    }));
  }
};

IN_PROC_BROWSER_TEST_F(MahoPeekLinkRoutingInteractiveUiTest,
                       SeamA_DispositionsPrecedenceAndBusy) {
  const GURL target = embedded_test_server()->GetURL(kTargetPath);
  for (bool favorite_source : {false, true}) {
    content::WebContents* routed_source =
        favorite_source ? PrepareFavoriteSource() : PreparePinnedSource();
    ASSERT_TRUE(routed_source);
    const GURL routed_source_url = routed_source->GetLastCommittedURL();
    for (WindowOpenDisposition disposition :
         {WindowOpenDisposition::NEW_FOREGROUND_TAB,
          WindowOpenDisposition::NEW_BACKGROUND_TAB,
          WindowOpenDisposition::NEW_WINDOW}) {
      OpenFromTab(routed_source, target, disposition);
      ASSERT_TRUE(WaitForPeekAt(target));
      EXPECT_EQ(routed_source_url, routed_source->GetLastCommittedURL());
      HidePeek();
    }
  }

  content::WebContents* source = PreparePinnedSource();
  ASSERT_TRUE(source);
  const GURL source_url = source->GetLastCommittedURL();
  OpenFromTab(source, embedded_test_server()->GetURL(kRedirectTargetPath),
              WindowOpenDisposition::NEW_FOREGROUND_TAB);
  ASSERT_TRUE(
      WaitForPeekAt(embedded_test_server()->GetURL(kRedirectTargetPath)));
  const int tab_count = browser()->GetTabStripModel()->count();
  ui_test_utils::TabAddedWaiter tab_added(browser());
  OpenFromTab(source, target, WindowOpenDisposition::NEW_BACKGROUND_TAB);
  content::WebContents* added = tab_added.Wait();
  EXPECT_EQ(tab_count + 1, browser()->GetTabStripModel()->count());
  EXPECT_EQ(added, active_contents());
  EXPECT_EQ(target, added->GetLastCommittedURL());
  EXPECT_EQ(source_url, source->GetLastCommittedURL());

  HidePeek();
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  const std::string origin_space = bridge->GetActiveSpaceId(browser());
  const std::string atc_target_space = RegisterAtcTargetSpace();
  ASSERT_FALSE(atc_target_space.empty());
  AddAtcRule("peek-routing-precedence", atc_target_space);
  const GURL atc_target =
      embedded_test_server()->GetURL(kAtcHost, kTargetPath);

  browser()->GetProfile()->GetPrefs()->SetBoolean(
      sidebar_prefs::kMahoMiniClickOverrideEnabled, true);
  ui_test_utils::BrowserCreatedObserver mini_created;
  content::OpenURLParams mini_params(
      atc_target, content::Referrer(), WindowOpenDisposition::NEW_SPLIT_VIEW,
      ui::PAGE_TRANSITION_LINK, /*is_renderer_initiated=*/false);
  mini_params.user_gesture = true;
  BrowserWebContentsDelegate::From(browser())->OpenURLFromTab(
      source, mini_params, base::NullCallback());
  Browser* mini = static_cast<Browser*>(mini_created.Wait());
  ASSERT_TRUE(mini);
  EXPECT_TRUE((mini->GetType() == BrowserWindowInterface::TYPE_POPUP));
  EXPECT_EQ(origin_space, bridge->GetActiveSpaceId(browser()));
  EXPECT_FALSE(controller()->SlotBusy());
  CloseBrowserSynchronously(mini);

  OpenFromTab(source, atc_target,
              WindowOpenDisposition::NEW_BACKGROUND_TAB);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return bridge->GetActiveSpaceId(browser()) == atc_target_space;
  }));
  EXPECT_FALSE(controller()->SlotBusy());
  RemoveAtcRule("peek-routing-precedence");
}

IN_PROC_BROWSER_TEST_F(MahoPeekLinkRoutingInteractiveUiTest,
                       SameTabNavigationStaysInTab) {
  content::WebContents* source = PreparePinnedSource();
  ASSERT_TRUE(source);
  const GURL target = embedded_test_server()->GetURL(kTargetPath);

  // Same-tab navigations are never rerouted into Peek, with or without ATC
  // rules: an in-page (location.href / same-tab) navigation stays in its tab.
  for (bool with_atc : {false, true}) {
    MahoAtcState::SetHasEnabledRules(with_atc);
    content::TestNavigationObserver navigation(source);
    content::SimulateMouseClickOrTapElementWithId(source, "same-tab");
    navigation.Wait();
    EXPECT_EQ(target, source->GetLastCommittedURL());
    EXPECT_FALSE(controller()->SlotBusy());
    EXPECT_EQ(with_atc, MahoAtcState::HasEnabledRules());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(kSourcePath)));
    source = active_contents();
    ASSERT_TRUE(source);
  }
}

IN_PROC_BROWSER_TEST_F(MahoPeekLinkRoutingInteractiveUiTest,
                       SameTabRedirectDoesNotReevaluatePeek) {
  content::WebContents* source = PreparePinnedSource();
  ASSERT_TRUE(source);
  const GURL redirect_target =
      embedded_test_server()->GetURL(kRedirectTargetPath);
  const int tab_count = browser()->GetTabStripModel()->count();

  // A same-tab link that redirects must not re-trigger Peek evaluation on the
  // redirect hop; it stays in the source tab the whole way.
  content::TestNavigationObserver navigation(source);
  content::SimulateMouseClickOrTapElementWithId(source, "redirect");
  navigation.Wait();

  EXPECT_EQ(redirect_target, source->GetLastCommittedURL());
  EXPECT_EQ(redirect_target, navigation.last_navigation_url());
  EXPECT_EQ(tab_count, browser()->GetTabStripModel()->count());
  EXPECT_FALSE(controller()->SlotBusy());
}

}  // namespace
}  // namespace maho
