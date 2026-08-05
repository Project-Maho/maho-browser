// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/peek/maho_peek_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace maho {
namespace {

constexpr char kPopupOpenerPath[] = "/maho-peek-popup-opener";
constexpr char kPopupPath[] = "/maho-peek-popup";

std::unique_ptr<net::test_server::HttpResponse> HandlePopupRequest(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  if (request.relative_url == kPopupOpenerPath) {
    response->set_content(R"html(
      <!doctype html>
      <button id="open-popup" onclick="window.popupRef =
          window.open('/maho-peek-popup', 'peekPopup',
                      'popup,width=640,height=480')">Open popup</button>
      <button id="open-noopener" onclick="window.open(
          '/maho-peek-popup?noopener=1', '_blank',
          'popup,noopener,width=640,height=480')">Open noopener</button>
      <script>
        window.popupMessages = [];
        addEventListener('message', event => popupMessages.push(event.data));
        window.sendToPopup = () => popupRef.postMessage('opener-to-popup', '*');
      </script>
    )html");
    return response;
  }
  if (request.relative_url == kPopupPath ||
      request.relative_url == std::string(kPopupPath) + "?noopener=1") {
    response->set_content(R"html(
      <!doctype html>
      <button id="close-popup" onclick="window.close()">Close popup</button>
      <script>
        window.receivedFromOpener = false;
        addEventListener('message', event => {
          if (event.data === 'opener-to-popup') {
            receivedFromOpener = true;
            opener.postMessage('popup-ack', '*');
          }
        });
        if (opener) opener.postMessage('popup-ready', '*');
      </script>
    )html");
    return response;
  }
  return nullptr;
}

class WidgetBoundsChangeWaiter : public views::WidgetObserver {
 public:
  explicit WidgetBoundsChangeWaiter(views::Widget* widget) {
    observation_.Observe(widget);
  }

  WidgetBoundsChangeWaiter(const WidgetBoundsChangeWaiter&) = delete;
  WidgetBoundsChangeWaiter& operator=(const WidgetBoundsChangeWaiter&) = delete;
  ~WidgetBoundsChangeWaiter() override = default;

  void Wait() {
    if (!changed_) {
      run_loop_.Run();
    }
  }

  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override {
    changed_ = true;
    run_loop_.Quit();
  }

 private:
  bool changed_ = false;
  base::RunLoop run_loop_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

class MahoPeekInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoPeekInteractiveUiTest() = default;
  MahoPeekInteractiveUiTest(const MahoPeekInteractiveUiTest&) = delete;
  MahoPeekInteractiveUiTest& operator=(const MahoPeekInteractiveUiTest&) =
      delete;
  ~MahoPeekInteractiveUiTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandlePopupRequest));
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
    if (browser()->tab_strip_model()->empty()) {
      chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                       /*foreground=*/true);
    }
    ASSERT_TRUE(browser()->tab_strip_model()->GetActiveWebContents());
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  }

 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  views::Widget* browser_widget() {
    BrowserView* view = browser_view();
    return view ? view->GetWidget() : nullptr;
  }

  MahoPeekController* ControllerFor(Browser* target_browser) {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(target_browser);
    return view ? view->GetOrCreateMahoPeekController() : nullptr;
  }

  MahoPeekController* controller() { return ControllerFor(browser()); }

  content::WebContents* OpenPeekIn(Browser* target_browser,
                                   content::WebContents* source = nullptr) {
    MahoPeekController* peek = ControllerFor(target_browser);
    EXPECT_TRUE(peek);
    if (!peek) {
      return nullptr;
    }

    const GURL url = embedded_test_server()->GetURL("/title1.html");
    content::TestNavigationObserver navigation(url);
    navigation.StartWatchingNewWebContents();
    content::WebContents* contents = peek->ShowUrl(source, url);
    EXPECT_TRUE(contents);
    if (!contents) {
      return nullptr;
    }
    navigation.Wait();
    EXPECT_TRUE(navigation.last_navigation_succeeded());
    EXPECT_EQ(url, contents->GetLastCommittedURL());
    return contents;
  }

  content::WebContents* OpenPeek(content::WebContents* source = nullptr) {
    return OpenPeekIn(browser(), source);
  }

  content::WebContents* NavigateToPopupOpener() {
    const GURL url = embedded_test_server()->GetURL(kPopupOpenerPath);
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    content::WebContents* source =
        browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(source);
    EXPECT_EQ(url, source ? source->GetLastCommittedURL() : GURL());
    return source;
  }

  content::WebContents* ClickPopupButton(content::WebContents* source,
                                         std::string_view button_id) {
    const std::vector<content::WebContents*> existing_contents =
        content::GetAllWebContents();
    content::WebContents* created = nullptr;
    content::SimulateMouseClickOrTapElementWithId(source, button_id);
    if (!base::test::RunUntil([&]() {
          for (content::WebContents* contents :
               content::GetAllWebContents()) {
            if (std::ranges::find(existing_contents, contents) ==
                existing_contents.end()) {
              created = contents;
              return true;
            }
          }
          return false;
        })) {
      return nullptr;
    }
    return created;
  }

  void SendEscape() {
    base::RunLoop sent;
    ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
        browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE, false, false,
        false, false, sent.QuitClosure()));
    sent.Run();
  }

  void ClickScreenPoint(const gfx::Point& point) {
    base::RunLoop moved;
    ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(),
                                                         moved.QuitClosure()));
    moved.Run();

    base::RunLoop clicked;
    ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
        clicked.QuitClosure()));
    clicked.Run();
  }
};

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       OpensVisibleOverlayHostingWebContents) {
  content::WebContents* contents = OpenPeek();
  ASSERT_TRUE(contents);

  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return peek->state_for_testing() == MahoPeekController::State::Open;
  }));

  views::Widget* overlay = peek->widget_for_testing();
  ASSERT_TRUE(overlay);
  EXPECT_TRUE(overlay->IsVisible());
  ASSERT_TRUE(peek->peek_view_for_testing());
  EXPECT_EQ(contents, peek->peek_view_for_testing()->hosted_contents());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest, EscapeHidesOverlay) {
  ASSERT_TRUE(OpenPeek());
  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  views::Widget* overlay = peek->widget_for_testing();
  ASSERT_TRUE(overlay);

  views::test::WidgetDestroyedWaiter destroyed(overlay);
  SendEscape();
  destroyed.Wait();

  EXPECT_EQ(MahoPeekController::State::Closed, peek->state_for_testing());
  EXPECT_FALSE(peek->widget_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest, ScrimClickHidesOverlay) {
  ASSERT_TRUE(OpenPeek());
  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  views::Widget* overlay = peek->widget_for_testing();
  ASSERT_TRUE(overlay);

  views::test::WidgetDestroyedWaiter destroyed(overlay);
  ClickScreenPoint(overlay->GetWindowBoundsInScreen().origin() +
                   gfx::Vector2d(8, 8));
  destroyed.Wait();

  EXPECT_EQ(MahoPeekController::State::Closed, peek->state_for_testing());
  EXPECT_FALSE(peek->widget_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       PromoteButtonMovesLivePageToTab) {
  const int initial_tab_count = browser()->tab_strip_model()->count();
  content::WebContents* contents = OpenPeek();
  ASSERT_TRUE(contents);
  EXPECT_FALSE(sessions::SessionTabHelper::FromWebContents(contents));

  ASSERT_TRUE(content::ExecJs(
      contents,
      "window.mahoPeekState = 'preserved'; "
      "history.pushState({peek: true}, '', 'title1.html?promoted');"));
  ASSERT_TRUE(contents->GetController().CanGoBack());
  const GURL promoted_url = contents->GetLastCommittedURL();

  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  MahoPeekView* peek_view = peek->peek_view_for_testing();
  ASSERT_TRUE(peek_view);
  views::MdTextButton* promote_button =
      peek_view->open_as_tab_button_for_testing();
  ASSERT_TRUE(promote_button);
  views::Widget* overlay = peek->widget_for_testing();
  ASSERT_TRUE(overlay);
  views::test::WidgetDestroyedWaiter overlay_destroyed(overlay);

  views::test::ButtonTestApi(promote_button)
      .NotifyClick(ui::MouseEvent(ui::EventType::kMouseReleased, gfx::Point(),
                                  gfx::Point(), ui::EventTimeForNow(),
                                  ui::EF_LEFT_MOUSE_BUTTON,
                                  ui::EF_LEFT_MOUSE_BUTTON));
  overlay_destroyed.Wait();

  EXPECT_EQ(initial_tab_count + 1, browser()->tab_strip_model()->count());
  EXPECT_EQ(contents, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(static_cast<content::WebContentsDelegate*>(browser()),
            contents->GetDelegate());
  EXPECT_TRUE(sessions::SessionTabHelper::FromWebContents(contents));
  EXPECT_EQ(promoted_url, contents->GetLastCommittedURL());
  EXPECT_TRUE(contents->GetController().CanGoBack());
  EXPECT_EQ("preserved",
            content::EvalJs(contents, "window.mahoPeekState").ExtractString());
  EXPECT_EQ(MahoPeekController::State::Closed, peek->state_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       SourceCloseDestroysTransientPeek) {
  content::WebContents* source =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(source);
  content::WebContents* contents = OpenPeek(source);
  ASSERT_TRUE(contents);
  EXPECT_FALSE(sessions::SessionTabHelper::FromWebContents(contents));

  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  views::Widget* overlay = peek->widget_for_testing();
  ASSERT_TRUE(overlay);
  content::WebContentsDestroyedWatcher contents_destroyed(contents);
  views::test::WidgetDestroyedWaiter overlay_destroyed(overlay);

  const int source_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(source);
  ASSERT_NE(TabStripModel::kNoTab, source_index);
  browser()->tab_strip_model()->CloseWebContentsAt(source_index,
                                                   TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return contents_destroyed.IsDestroyed(); }));
  overlay_destroyed.Wait();

  EXPECT_EQ(MahoPeekController::State::Closed, peek->state_for_testing());
  EXPECT_FALSE(peek->widget_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       AdoptsPopupPreservingOpenerMessagingIdentityAndClose) {
  content::WebContents* source = NavigateToPopupOpener();
  ASSERT_TRUE(source);
  const int initial_tab_count = browser()->tab_strip_model()->count();

  content::WebContents* created = ClickPopupButton(source, "open-popup");
  ASSERT_TRUE(created);
  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return peek->state_for_testing() == MahoPeekController::State::Open &&
           peek->peek_view_for_testing() &&
           peek->peek_view_for_testing()->hosted_contents() == created &&
           created->GetLastCommittedURL().path() == kPopupPath;
  }));

  EXPECT_EQ(initial_tab_count, browser()->tab_strip_model()->count());
  EXPECT_EQ(created, peek->peek_view_for_testing()->hosted_contents());
  EXPECT_EQ(true, content::EvalJs(created, "window.opener !== null"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(source,
                           "window.popupMessages.includes('popup-ready')")
        .ExtractBool();
  }));

  ASSERT_TRUE(content::ExecJs(source, "window.sendToPopup()"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(created, "window.receivedFromOpener").ExtractBool() &&
           content::EvalJs(source,
                           "window.popupMessages.includes('popup-ack')")
               .ExtractBool();
  }));

  views::Widget* overlay = peek->widget_for_testing();
  ASSERT_TRUE(overlay);
  content::WebContentsDestroyedWatcher contents_destroyed(created);
  views::test::WidgetDestroyedWaiter overlay_destroyed(overlay);
  content::SimulateMouseClickOrTapElementWithId(created, "close-popup");
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return contents_destroyed.IsDestroyed(); }));
  overlay_destroyed.Wait();
  EXPECT_EQ(MahoPeekController::State::Closed, peek->state_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       AdoptsNoopenerPopupWithoutInventingOpener) {
  content::WebContents* source = NavigateToPopupOpener();
  ASSERT_TRUE(source);

  content::WebContents* created = ClickPopupButton(source, "open-noopener");
  ASSERT_TRUE(created);
  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return peek->peek_view_for_testing() &&
           peek->peek_view_for_testing()->hosted_contents() == created &&
           created->GetLastCommittedURL().path() == kPopupPath;
  }));
  EXPECT_EQ(true, content::EvalJs(created, "window.opener === null"));
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       OccupiedPeekRoutesSecondPopupToForegroundTab) {
  content::WebContents* source = NavigateToPopupOpener();
  ASSERT_TRUE(source);
  ASSERT_TRUE(OpenPeek());
  const int initial_tab_count = browser()->tab_strip_model()->count();

  content::WebContents* created = ClickPopupButton(source, "open-popup");
  ASSERT_TRUE(created);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() == initial_tab_count + 1 &&
           browser()->tab_strip_model()->GetActiveWebContents() == created &&
           created->GetLastCommittedURL().path() == kPopupPath;
  }));
  EXPECT_FALSE(BrowserView::GetBrowserViewForBrowser(browser())
                   ->GetOrCreateMahoPeekController()
                   ->peek_view_for_testing()
                   ->hosted_contents() == created);
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       DisabledPopupRoutingUsesNormalPopupWindow) {
  browser()->profile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kPeekPopupRoutingEnabled, false);
  content::WebContents* source = NavigateToPopupOpener();
  ASSERT_TRUE(source);

  ui_test_utils::BrowserCreatedObserver browser_created_observer;
  content::SimulateMouseClickOrTapElementWithId(source, "open-popup");
  Browser* popup_browser = browser_created_observer.Wait();
  ASSERT_TRUE(popup_browser);
  EXPECT_TRUE(popup_browser->is_type_popup());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    content::WebContents* created =
        popup_browser->tab_strip_model()->GetActiveWebContents();
    return created && created->GetLastCommittedURL().path() == kPopupPath;
  }));
  EXPECT_FALSE(controller()->SlotBusy());
  CloseBrowserSynchronously(popup_browser);
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       BrowserWindowShutdownDestroysPeekWithoutUnloadPrompt) {
  Browser* closing_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(closing_browser);
  content::WebContents* contents = OpenPeekIn(closing_browser);
  ASSERT_TRUE(contents);
  EXPECT_FALSE(sessions::SessionTabHelper::FromWebContents(contents));
  ASSERT_TRUE(content::ExecJs(
      contents, "window.onbeforeunload = () => 'keep peek open';"));

  content::WebContentsDestroyedWatcher contents_destroyed(contents);
  CloseBrowserSynchronously(closing_browser);
  EXPECT_TRUE(contents_destroyed.IsDestroyed());
}

IN_PROC_BROWSER_TEST_F(MahoPeekInteractiveUiTest,
                       BrowserResizeRepositionsOverlay) {
  ASSERT_TRUE(OpenPeek());
  MahoPeekController* peek = controller();
  ASSERT_TRUE(peek);
  views::Widget* overlay = peek->widget_for_testing();
  views::Widget* parent = browser_widget();
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(parent);

  const gfx::Rect old_overlay_bounds = overlay->GetWindowBoundsInScreen();
  gfx::Rect resized_parent_bounds = parent->GetWindowBoundsInScreen();
  resized_parent_bounds.Inset(gfx::Insets::TLBR(0, 0, 80, 100));
  ASSERT_FALSE(resized_parent_bounds.IsEmpty());

  WidgetBoundsChangeWaiter overlay_bounds_changed(overlay);
  parent->SetBounds(resized_parent_bounds);
  overlay_bounds_changed.Wait();

  const gfx::Rect new_overlay_bounds = overlay->GetWindowBoundsInScreen();
  EXPECT_NE(old_overlay_bounds, new_overlay_bounds);
  EXPECT_EQ(parent->GetClientAreaBoundsInScreen(), new_overlay_bounds);
}

}  // namespace
}  // namespace maho
