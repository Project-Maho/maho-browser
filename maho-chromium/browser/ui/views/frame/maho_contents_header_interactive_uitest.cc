// Copyright 2026 Maho Browser. All rights reserved.

#include <string_view>
#include <vector>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "maho/browser/net/maho_translate_injection_handler.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/ui/views/frame/maho_contents_header_view.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace maho {
namespace {

void ClickScreenPoint(const gfx::Point& point) {
  ASSERT_TRUE(ui_controls::SendMouseMove(point.x(), point.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
}

views::View* FindViewWithAccessibleName(views::View* root,
                                        const std::u16string& name) {
  if (!root) {
    return nullptr;
  }
  if (root->GetViewAccessibility().GetCachedName().find(name) !=
      std::u16string::npos) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindViewWithAccessibleName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

views::MdTextButton* FindTextButtonWithText(views::View* root,
                                            const std::u16string& text) {
  if (!root) {
    return nullptr;
  }
  if (auto* button = views::AsViewClass<views::MdTextButton>(root);
      button && button->GetText() == text) {
    return button;
  }
  for (views::View* child : root->children()) {
    if (views::MdTextButton* found = FindTextButtonWithText(child, text)) {
      return found;
    }
  }
  return nullptr;
}

class MahoContentsHeaderInteractiveTest
    : public MahoSidebarInteractiveTestBase {
 protected:
  void SetUpOnMainThread() override {
    MahoSidebarInteractiveTestBase::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());

    // Maho interactive tests start with a zero-tab window, so the header has
    // no WebContents (utility/copy disabled) and ui_test_utils::NavigateToURL
    // would arm its CURRENT_TAB observer on a null active tab and hang.
    // Seed one offline tab and wait until the active pane header is bound.
    if (browser()->GetTabStripModel()->count() == 0) {
      ASSERT_TRUE(AddTabAtIndex(0, GURL("data:text/html,<title>header</title>"),
                                ui::PAGE_TRANSITION_TYPED));
    }
    ASSERT_TRUE(base::test::RunUntil([this] {
      MahoContentsHeaderView* header = ActiveHeader();
      return browser()->GetTabStripModel()->GetActiveWebContents() && header &&
             header->utility_icon_for_testing()->GetEnabled();
    }));
    FlushLayout();
  }

  MahoContentsHeaderView* ActiveHeader() {
    ContentsContainerView* container =
        multi_contents_view()->GetActiveContentsContainerView();
    return container ? container->maho_contents_header() : nullptr;
  }

  // Navigates the active tab to an on-device-translatable page and returns
  // the active pane header once its translate label is shown.
  MahoContentsHeaderView* NavigateToTranslatablePage(GURL* page_url_out) {
    const GURL page_url = embedded_test_server()->GetURL("/title1.html");
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
    content::WebContents* contents =
        browser()->GetTabStripModel()->GetActiveWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(contents));
    EXPECT_TRUE(maho::CanOnDeviceTranslate(contents));
    MahoContentsHeaderView* header = ActiveHeader();
    EXPECT_TRUE(header);
    if (!header) {
      return nullptr;
    }
    EXPECT_TRUE(base::test::RunUntil([header] {
      return header->translate_label_for_testing()->GetVisible();
    }));
    FlushLayout();
    if (page_url_out) {
      *page_url_out = page_url;
    }
    return header;
  }

  bool IsCommandOverlayVisible() {
    auto* controller =
        browser_view()->GetMahoCommandOverlayControllerForTesting();
    return controller && controller->IsVisible();
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  MultiContentsView* multi_contents_view() {
    return browser_view()->multi_contents_view();
  }

  // Headers of the visible contents panes, in pane order.
  std::vector<MahoContentsHeaderView*> VisiblePaneHeaders() {
    std::vector<MahoContentsHeaderView*> headers;
    for (ContentsContainerView* container :
         multi_contents_view()->contents_container_views()) {
      if (container->GetVisible()) {
        headers.push_back(container->maho_contents_header());
      }
    }
    return headers;
  }

  void FlushLayout() {
    views::test::RunScheduledLayout(browser_view()->GetWidget());
  }

  void CreateTwoPaneSplit() {
    AddTestTab(GURL("data:text/html,<title>second</title>"), u"second");
    TabStripModel* model = browser()->GetTabStripModel();
    ASSERT_GE(model->count(), 2);
    model->ActivateTabAt(
        0, TabStripUserGestureDetails(
               TabStripUserGestureDetails::GestureType::kOther));
    model->AddToNewSplit(
        {1},
        split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide,
                                       0.5),
        split_tabs::SplitTabCreatedSource::kToolbarButton);
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return multi_contents_view()->IsInSplitView(); }));
    FlushLayout();
  }
};

IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SplitShowsBothHeadersWithActiveState) {
  CreateTwoPaneSplit();

  const std::vector<MahoContentsHeaderView*> headers = VisiblePaneHeaders();
  ASSERT_EQ(headers.size(), 2u);
  ContentsContainerView* active =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(active);
  for (MahoContentsHeaderView* header : headers) {
    ASSERT_TRUE(header);
    EXPECT_TRUE(header->GetVisible());
    EXPECT_TRUE(header->IsDrawn());
    EXPECT_EQ(header->is_active_for_testing(),
              header == active->maho_contents_header());
  }
  EXPECT_NE(headers[0]->is_active_for_testing(),
            headers[1]->is_active_for_testing());

  // Moving activation to the other pane moves the active state with it.
  const bool first_was_active = headers[0]->is_active_for_testing();
  browser()->GetTabStripModel()->ActivateTabAt(
      first_was_active ? 1 : 0,
      TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kOther));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return headers[0]->is_active_for_testing() != first_was_active;
  }));
  EXPECT_EQ(headers[1]->is_active_for_testing(), first_was_active);
}

IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SidebarCollapseKeepsHeaderAndContentsOffset) {
  ContentsContainerView* container =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  MahoContentsHeaderView* header = container->maho_contents_header();
  ASSERT_TRUE(header);
  FlushLayout();
  ASSERT_TRUE(header->GetVisible());
  const int contents_y = container->contents_view()->y();
  EXPECT_GE(contents_y, kMahoContentsHeaderHeightDp);

  MahoSidebarContainerView* sidebar_container = GetContainerView();
  ASSERT_TRUE(sidebar_container);
  const bool was_expanded = sidebar_container->IsPanelExpanded();
  sidebar_container->TogglePanelExpanded();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return sidebar_container->IsPanelExpanded() != was_expanded;
  }));
  FlushLayout();

  EXPECT_TRUE(header->GetVisible());
  EXPECT_EQ(container->contents_view()->y(), contents_y);

  sidebar_container->TogglePanelExpanded();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return sidebar_container->IsPanelExpanded() == was_expanded;
  }));
  FlushLayout();
  EXPECT_TRUE(header->GetVisible());
  EXPECT_EQ(container->contents_view()->y(), contents_y);
}

IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       BrowserFullscreenHidesHeadersAndRestores) {
  CreateTwoPaneSplit();
  std::vector<MahoContentsHeaderView*> headers = VisiblePaneHeaders();
  ASSERT_EQ(headers.size(), 2u);
  for (MahoContentsHeaderView* header : headers) {
    ASSERT_TRUE(header->GetVisible());
  }

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !headers[0]->GetVisible() && !headers[1]->GetVisible();
  }));
  FlushLayout();
  for (ContentsContainerView* container :
       multi_contents_view()->contents_container_views()) {
    if (container->GetVisible()) {
      EXPECT_FALSE(container->maho_contents_header()->GetVisible());
      EXPECT_EQ(container->contents_view()->y(), 0);
    }
  }

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return headers[0]->GetVisible() && headers[1]->GetVisible();
  }));
  FlushLayout();
  for (ContentsContainerView* container :
       multi_contents_view()->contents_container_views()) {
    if (container->GetVisible()) {
      EXPECT_TRUE(container->maho_contents_header()->GetVisible());
      EXPECT_GE(container->contents_view()->y(), kMahoContentsHeaderHeightDp);
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SidebarLayoutPrefGatesHeader) {
  ContentsContainerView* container =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  MahoContentsHeaderView* header = container->maho_contents_header();
  ASSERT_TRUE(header);
  PrefService* prefs = browser()->GetProfile()->GetPrefs();

  // Stock top chrome is shown with the sidebar layout off, so the normal
  // window gets no header and contents start at the top of the pane.
  prefs->SetBoolean(sidebar_prefs::kSidebarLayoutEnabled, false);
  ASSERT_TRUE(base::test::RunUntil([&]() { return !header->GetVisible(); }));
  FlushLayout();
  EXPECT_EQ(container->contents_view()->y(), 0);

  prefs->SetBoolean(sidebar_prefs::kSidebarLayoutEnabled, true);
  ASSERT_TRUE(base::test::RunUntil([&]() { return header->GetVisible(); }));
  FlushLayout();
  EXPECT_GE(container->contents_view()->y(), kMahoContentsHeaderHeightDp);
}

IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SplitTagsOnlyActiveTranslateLabel) {
  CreateTwoPaneSplit();
  const std::vector<MahoContentsHeaderView*> headers = VisiblePaneHeaders();
  ASSERT_EQ(headers.size(), 2u);

  auto tagged_count = [&]() {
    int count = 0;
    for (MahoContentsHeaderView* header : headers) {
      if (header->translate_label_for_testing()->GetProperty(
              views::kElementIdentifierKey) == kTranslatePageActionElementId) {
        ++count;
      }
    }
    return count;
  };
  EXPECT_EQ(tagged_count(), 1);
  for (MahoContentsHeaderView* header : headers) {
    EXPECT_EQ(header->translate_label_for_testing()->GetProperty(
                  views::kElementIdentifierKey) ==
                  kTranslatePageActionElementId,
              header->is_active_for_testing());
  }

  // Activating the other pane moves the id with the active state.
  const bool first_was_active = headers[0]->is_active_for_testing();
  browser()->GetTabStripModel()->ActivateTabAt(
      first_was_active ? 1 : 0,
      TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kOther));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return headers[0]->is_active_for_testing() != first_was_active;
  }));
  EXPECT_EQ(tagged_count(), 1);
  EXPECT_EQ(headers[0]->translate_label_for_testing()->GetProperty(
                views::kElementIdentifierKey) == kTranslatePageActionElementId,
            !first_was_active);
}

// Real browser frame (BrowserFrameViewLinux on Linux, BrowserFrameViewWin /
// OpaqueBrowserFrameView on Windows): with the sidebar collapsed the header
// lays out under the frame's caption buttons without crashing, and no visible
// header control intersects any measured caption-button cluster.
#if !BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       CollapsedSidebarHeaderClearsCaptionButtons) {
  ContentsContainerView* container =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  MahoContentsHeaderView* header = container->maho_contents_header();
  ASSERT_TRUE(header);

  MahoSidebarContainerView* sidebar_container = GetContainerView();
  ASSERT_TRUE(sidebar_container);
  if (sidebar_container->IsPanelExpanded()) {
    sidebar_container->TogglePanelExpanded();
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return !sidebar_container->IsPanelExpanded(); }));
  }
  FlushLayout();
  ASSERT_TRUE(header->GetVisible());

  for (const gfx::Rect& controls : GetWindowControlsRectsInView(header)) {
    for (views::View* child : header->children()) {
      if (child->GetVisible()) {
        EXPECT_FALSE(child->bounds().Intersects(controls))
            << child->GetClassName() << " " << child->bounds().ToString()
            << " overlaps " << controls.ToString();
      }
    }
  }
}
#endif  // !BUILDFLAG(IS_MAC)

// F6 pane traversal reaches the contents header: MahoContentsHeaderView is
// an AccessiblePaneView listed in ContentsContainerView::GetAccessiblePanes()
// (maho-chromium/build/scripts/split_view_replacements.py), which
// BrowserView::GetAccessiblePanes() folds into the browser's F6 order.
//
// Mac has no F6 pane-cycling binding (global_keyboard_shortcuts_mac.mm uses
// Cmd+Opt+Down instead), so this test only applies on other platforms.
#if !BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       F6CyclesFocusIntoContentsHeader) {
  ContentsContainerView* container =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  MahoContentsHeaderView* header = container->maho_contents_header();
  ASSERT_TRUE(header);
  FlushLayout();

  views::FocusManager* focus_manager = browser_view()->GetFocusManager();
  ASSERT_TRUE(focus_manager);

  // Cycle F6 until the header (or one of its buttons) has the pane, waiting
  // for the FOCUS TO CHANGE before checking containment on each iteration.
  // A wait on containment alone only ever succeeds on the first F6 that
  // happens to land in the header, so it never actually cycles; waiting for
  // change first, bounded so a broken registration fails instead of hanging.
  views::View* focused_before = focus_manager->GetFocusedView();
  bool reached_header = focused_before && header->Contains(focused_before);
  for (int i = 0; i < 12 && !reached_header; ++i) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_F6, false,
                                                false, false, false));
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return focus_manager->GetFocusedView() != focused_before;
    }));
    focused_before = focus_manager->GetFocusedView();
    reached_header = focused_before && header->Contains(focused_before);
  }
  EXPECT_TRUE(reached_header);
}
#endif  // !BUILDFLAG(IS_MAC)

// Enter on the host button opens the command overlay for the pane's tab
// (MahoSidebarContainerView::ShowCommandOverlayForCurrentTab()).
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       EnterOnHostButtonOpensCommandOverlay) {
  ContentsContainerView* container =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  MahoContentsHeaderView* header = container->maho_contents_header();
  ASSERT_TRUE(header);
  FlushLayout();

  views::LabelButton* host_button = header->host_button_for_testing();
  ASSERT_TRUE(host_button);

  // On Mac, LabelButton defaults to FocusBehavior::ACCESSIBLE_ONLY, so plain
  // RequestFocus() is a no-op unless the FocusManager is in keyboard-
  // accessible mode (the state Full Keyboard Access puts it in). Enable it
  // here so the focus request is deterministic on every platform instead of
  // depending on a system accessibility setting.
  views::FocusManager* focus_manager = browser_view()->GetFocusManager();
  ASSERT_TRUE(focus_manager);
  focus_manager->SetKeyboardAccessible(true);

  host_button->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return host_button->HasFocus(); }));

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                              false, false, false, false));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    auto* c = browser_view()->GetMahoCommandOverlayControllerForTesting();
    return c && c->IsVisible();
  }));
  auto* c = browser_view()->GetMahoCommandOverlayControllerForTesting();
  ASSERT_TRUE(c);
  EXPECT_TRUE(c->IsVisible());
}

// The sidebar's nav cluster (back/forward/reload) was removed; navigation
// now lives only in MahoContentsHeaderView. This asserts the underlying
// chrome commands IDC_BACK/IDC_FORWARD/IDC_RELOAD -- which back the Cmd+[ /
// Cmd+] (Alt+Left/Alt+Right on non-Mac) and Cmd/Ctrl+R accelerators -- still
// drive real navigation and still keep the header's own buttons in sync.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       ChromeNavigationCommandsStillNavigateAndReload) {
  ContentsContainerView* container =
      multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  MahoContentsHeaderView* header = container->maho_contents_header();
  ASSERT_TRUE(header);
  FlushLayout();

  const GURL first_url("data:text/html,<title>first</title>");
  const GURL second_url("data:text/html,<title>second</title>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));

  TabStripModel* tab_strip = browser()->GetTabStripModel();
  content::WebContents* contents = tab_strip->GetActiveWebContents();
  ASSERT_TRUE(contents);

  ASSERT_TRUE(chrome::CanGoBack(browser()));
  EXPECT_TRUE(header->back_button_for_testing()->GetEnabled());

  content::TestNavigationObserver back_observer(contents);
  chrome::ExecuteCommand(browser(), IDC_BACK);
  back_observer.Wait();
  EXPECT_EQ(contents->GetLastCommittedURL(), first_url);

  ASSERT_TRUE(chrome::CanGoForward(browser()));
  EXPECT_TRUE(header->forward_button_for_testing()->GetEnabled());

  content::TestNavigationObserver forward_observer(contents);
  chrome::ExecuteCommand(browser(), IDC_FORWARD);
  forward_observer.Wait();
  EXPECT_EQ(contents->GetLastCommittedURL(), second_url);

  ASSERT_TRUE(chrome::CanReload(browser()));
  EXPECT_TRUE(header->reload_button_for_testing()->GetEnabled());

  content::TestNavigationObserver reload_observer(contents);
  chrome::ExecuteCommand(browser(), IDC_RELOAD);
  reload_observer.Wait();
  EXPECT_EQ(contents->GetLastCommittedURL(), second_url);
}

// --- Migrated from the removed sidebar search pill -------------------------
// Each test below replaces a pill behavior test; the mapping is recorded in
// .omo/evidence/content-pane-address-strip/task-5.log.

// Replaces SearchFieldAcceptsInput: address entry moved from the pill's inline
// textfield to the command overlay, opened from the header host button.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       HostClickOpensCommandOverlayAndEscapeCloses) {
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  FlushLayout();
  ASSERT_FALSE(IsCommandOverlayVisible());

  ClickScreenPoint(
      header->host_button_for_testing()->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([&] { return IsCommandOverlayVisible(); }))
      << "Clicking the header host must open the command overlay";

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                              false, false, false, false));
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsCommandOverlayVisible(); }))
      << "Escape must close the command overlay opened from the header";
}

// N2: in split, clicking the inactive pane's header host activates that pane
// and the command overlay's typed URL must navigate it, not the pane that
// held keyboard focus before the click. Without focus following the
// activated pane, the overlay's focus restore on dismiss re-focused the old
// pane's web view, MultiContentsView re-activated it, and the CURRENT_TAB
// navigation landed there.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SplitHostClickOverlayNavigatesClickedPane) {
  CreateTwoPaneSplit();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  const auto& containers =
      multi_contents_view()->contents_container_views();
  ASSERT_GE(containers.size(), 2u);
  ContentsContainerView* left = containers[0];
  ContentsContainerView* right = containers[1];
  ASSERT_TRUE(left->GetVisible());
  ASSERT_TRUE(right->GetVisible());
  content::WebContents* left_contents = left->contents_view()->web_contents();
  content::WebContents* right_contents =
      right->contents_view()->web_contents();
  ASSERT_TRUE(left_contents);
  ASSERT_TRUE(right_contents);
  ASSERT_NE(left_contents, right_contents);
  const GURL left_url = left_contents->GetLastCommittedURL();
  ASSERT_NE(left_url, right_contents->GetLastCommittedURL());

  // LEFT is the active pane and holds keyboard focus.
  TabStripModel* model = browser()->GetTabStripModel();
  const int left_index = model->GetIndexOfWebContents(left_contents);
  const int right_index = model->GetIndexOfWebContents(right_contents);
  ASSERT_NE(left_index, TabStripModel::kNoTab);
  ASSERT_NE(right_index, TabStripModel::kNoTab);
  model->ActivateTabAt(left_index,
                       TabStripUserGestureDetails(
                           TabStripUserGestureDetails::GestureType::kOther));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return multi_contents_view()->GetActiveContentsContainerView() == left;
  }));
  left->contents_view()->RequestFocus();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return left->contents_view()->HasFocus(); }));
  FlushLayout();

  MahoContentsHeaderView* right_header = right->maho_contents_header();
  ASSERT_TRUE(right_header);
  ClickScreenPoint(right_header->host_button_for_testing()
                       ->GetBoundsInScreen()
                       .CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([&] { return IsCommandOverlayVisible(); }))
      << "Clicking the right header host must open the command overlay";
  EXPECT_EQ(model->active_index(), right_index)
      << "Clicking the right header host must activate the right pane";

  MahoCommandOverlayController* controller =
      browser_view()->GetMahoCommandOverlayControllerForTesting();
  ASSERT_TRUE(controller);
  MahoCommandOverlayView* overlay_view = controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);
  // The overlay only navigates typed http(s)/chrome/file/about URLs directly
  // (data: would fall through to a search), so the target is served locally.
  const GURL target_url = embedded_test_server()->GetURL("/title3.html");
  overlay_view->textfield()->SetText(base::UTF8ToUTF16(target_url.spec()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                              false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsCommandOverlayVisible(); }))
      << "Enter must dismiss the command overlay";

  EXPECT_TRUE(base::test::RunUntil([&] {
    return right_contents->GetLastCommittedURL() == target_url;
  })) << "The clicked (right) pane must navigate; right URL is "
      << right_contents->GetLastCommittedURL().spec() << ", left URL is "
      << left_contents->GetLastCommittedURL().spec();
  EXPECT_EQ(left_contents->GetLastCommittedURL(), left_url)
      << "The previously focused (left) pane must not navigate";
  EXPECT_EQ(model->active_index(), right_index)
      << "Dismissing the overlay must not flip activation back to the left";
}

// Replaces TranslationLabelClickShowsAnchoredConfirmationPopover.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslateLabelClickShowsAnchoredConfirmationPopover) {
  MahoContentsHeaderView* header = NavigateToTranslatablePage(nullptr);
  ASSERT_TRUE(header);
  views::LabelButton* translate_label = header->translate_label_for_testing();
  ASSERT_EQ(translate_label->GetText(), u"Translation Available");
  ASSERT_FALSE(translate_label->GetBoundsInScreen().IsEmpty());

  ClickScreenPoint(translate_label->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([header] {
    views::Widget* widget = header->translate_popover_widget_for_testing();
    return widget && widget->IsVisible();
  })) << "Clicking the translate label must open its confirmation popover";

  views::Widget* popover = header->translate_popover_widget_for_testing();
  auto* bubble_delegate =
      static_cast<views::BubbleDialogDelegate*>(popover->widget_delegate());
  ASSERT_TRUE(bubble_delegate);
  EXPECT_EQ(translate_label, bubble_delegate->GetAnchorView());
  EXPECT_TRUE(FindViewWithAccessibleName(popover->GetContentsView(),
                                         u"Options"));
  EXPECT_TRUE(FindViewWithAccessibleName(popover->GetContentsView(),
                                         u"Translate"));
}

// F3 D1: clicking the header security icon opens page info for the pane,
// anchored under the icon.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SecurityIconClickOpensAnchoredPageInfo) {
  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  views::ImageButton* security = header->security_icon_for_testing();
  ASSERT_TRUE(base::test::RunUntil([security] { return security->IsDrawn(); }));
  FlushLayout();
  ASSERT_EQ(PageInfoBubbleView::BUBBLE_NONE,
            PageInfoBubbleView::GetShownBubbleType());

  ClickScreenPoint(security->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([] {
    return PageInfoBubbleView::GetShownBubbleType() ==
           PageInfoBubbleView::BUBBLE_PAGE_INFO;
  })) << "Clicking the header security icon must open page info";

  views::BubbleDialogDelegateView* bubble =
      PageInfoBubbleView::GetPageInfoBubbleForTesting();
  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetAnchorView(), security);
  EXPECT_EQ(bubble->GetAnchorView(), header->security_anchor());
  EXPECT_EQ(header->page_info_widget_for_testing(), bubble->GetWidget());
  const gfx::Rect icon_bounds = security->GetBoundsInScreen();
  const gfx::Rect bubble_bounds = bubble->GetWidget()->GetWindowBoundsInScreen();
  EXPECT_GE(bubble_bounds.y(), icon_bounds.y())
      << "page info must open below the icon";
}

// Replaces TranslationLabelAccentAndHoverUnderlineCapture: the header binds the
// translate label to the palette's secondary text (the pill's hover
// underline is not part of the header design).
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslateLabelUsesPaletteSecondaryText) {
  MahoContentsHeaderView* header = NavigateToTranslatablePage(nullptr);
  ASSERT_TRUE(header);
  const MahoSidebarPalette& palette = header->palette_for_testing();
  ASSERT_NE(palette.secondary_text, SK_ColorTRANSPARENT)
      << "Header must receive the sidebar palette";
  // Palette colors are bound for every button state (UpdateChrome), so the
  // drawn color matches regardless of window activation.
  EXPECT_EQ(header->translate_label_for_testing()->GetCurrentTextColor(),
            palette.secondary_text);
}

// Replaces TranslationPopoverTranslateMarksActiveHttpPageTranslated.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslatePopoverTranslateMarksPageTranslated) {
  MahoContentsHeaderView* header = NavigateToTranslatablePage(nullptr);
  ASSERT_TRUE(header);
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_FALSE(maho::IsMahoPageTranslated(contents));

  ClickScreenPoint(header->translate_label_for_testing()
                       ->GetBoundsInScreen()
                       .CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([header] {
    views::Widget* widget = header->translate_popover_widget_for_testing();
    return widget && widget->IsVisible();
  }));
  views::MdTextButton* translate_button = FindTextButtonWithText(
      header->translate_popover_widget_for_testing()->GetContentsView(),
      u"Translate");
  ASSERT_TRUE(translate_button);
  // Under the headless test display (Xvfb, no window manager), an OS-level
  // click on the bubble deactivates it first, and close_on_deactivate closes
  // the popover before the press lands (gdb evidence:
  // .omo/evidence/content-pane-address-strip/task-9-popover-click-gdb.log,
  // Widget::CloseWithReason from BubbleDialogDelegate::
  // OnBubbleWidgetActivationChanged, bubble_dialog_delegate_view.cc:671), so
  // the button is activated directly instead; the popover's own Translate
  // handling is still what is under test.
  views::test::ButtonTestApi(translate_button)
      .NotifyClick(ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(),
                                   gfx::Point(), ui::EventTimeForNow(),
                                   ui::EF_LEFT_MOUSE_BUTTON,
                                   ui::EF_LEFT_MOUSE_BUTTON));

  EXPECT_TRUE(base::test::RunUntil(
      [contents] { return maho::IsMahoPageTranslated(contents); }));
  EXPECT_TRUE(base::test::RunUntil([header] {
    return header->translate_popover_widget_for_testing() == nullptr;
  }));
  EXPECT_EQ(u"Translated", header->translate_label_for_testing()->GetText());
}

// Replaces TranslationOptionsChooseLanguagePersistsAndTranslates.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslateOptionsChooseLanguagePersistsAndTranslates) {
  browser()->GetProfile()->GetPrefs()->SetString(
      translate::prefs::kPrefTranslateRecentTarget, "zz");
  MahoContentsHeaderView* header = NavigateToTranslatablePage(nullptr);
  ASSERT_TRUE(header);
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();

  const MahoTranslateOptionsForTesting options =
      header->PrepareTranslateOptionsForTesting();
  const std::vector<std::u16string> expected_labels = {
      u"Choose Another Language", u"Never Translate This Site",
      u"Language Settings"};
  EXPECT_EQ(expected_labels, options.labels);
  ASSERT_NE(0, options.first_language_command_id);
  const std::string selected_language =
      browser()->GetProfile()->GetPrefs()->GetString(
          translate::prefs::kPrefTranslateRecentTarget);
  header->ExecuteCommand(options.first_language_command_id, 0);

  EXPECT_NE(selected_language,
            browser()->GetProfile()->GetPrefs()->GetString(
                translate::prefs::kPrefTranslateRecentTarget));
  EXPECT_TRUE(base::test::RunUntil(
      [contents] { return maho::IsMahoPageTranslated(contents); }));
}

// Replaces TranslationOptionsPinAndCheckCurrentTargetLanguage.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslateOptionsPinAndCheckCurrentTargetLanguage) {
  browser()->GetProfile()->GetPrefs()->SetString(
      translate::prefs::kPrefTranslateRecentTarget, "ko");
  MahoContentsHeaderView* header = NavigateToTranslatablePage(nullptr);
  ASSERT_TRUE(header);

  const MahoTranslateOptionsForTesting options =
      header->PrepareTranslateOptionsForTesting();
  ASSERT_NE(0, options.first_language_command_id);
  EXPECT_EQ("ko", options.first_language_code);
  EXPECT_TRUE(header->IsCommandIdChecked(options.first_language_command_id));
  EXPECT_FALSE(
      header->IsCommandIdChecked(options.first_language_command_id + 1));
}

// Replaces TranslationOptionsNeverTranslateSuppressesLabel.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslateOptionsNeverTranslateSuppressesLabel) {
  GURL page_url;
  MahoContentsHeaderView* header = NavigateToTranslatablePage(&page_url);
  ASSERT_TRUE(header);

  const MahoTranslateOptionsForTesting options =
      header->PrepareTranslateOptionsForTesting();
  ASSERT_NE(0, options.never_translate_command_id);
  header->ExecuteCommand(options.never_translate_command_id, 0);

  const std::string host(page_url.host());
  EXPECT_TRUE(base::test::RunUntil([profile = browser()->GetProfile(), &host] {
    return translate::TranslatePrefs(profile->GetPrefs())
        .IsSiteOnNeverPromptList(host);
  }));
  EXPECT_FALSE(header->translate_label_for_testing()->GetVisible());

  // A reload recomputes the header state; the site stays suppressed.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  EXPECT_FALSE(header->translate_label_for_testing()->GetVisible());
}

// Replaces TranslationOptionsLanguageSettingsOpensGeneralPane.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       TranslateOptionsLanguageSettingsOpensGeneralPane) {
  MahoContentsHeaderView* header = NavigateToTranslatablePage(nullptr);
  ASSERT_TRUE(header);

  const MahoTranslateOptionsForTesting options =
      header->PrepareTranslateOptionsForTesting();
  ASSERT_NE(0, options.language_settings_command_id);
  header->ExecuteCommand(options.language_settings_command_id, 0);

  EXPECT_TRUE(base::test::RunUntil([this] {
    content::WebContents* contents =
        browser()->GetTabStripModel()->GetActiveWebContents();
    return contents && contents->GetVisibleURL().query() == "pane=general";
  }));
}

// Replaces HoverRevealsUtilityIconAndClickOpensUtilityPanel,
// UtilitySlotCenterClickOpensUtilityPanel and
// TrailingInsetClickOpensUtilityPanelNotCommandOverlay: the header utility
// icon is always shown, and clicking it opens the utility panel without
// falling through to the command overlay.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       UtilityIconClickOpensUtilityPanelNotCommandOverlay) {
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  FlushLayout();
  MahoLocationBarUtilityIconView* icon = header->utility_icon_for_testing();
  ASSERT_TRUE(icon->GetVisible());
  ASSERT_TRUE(icon->GetEnabled());
  EXPECT_FALSE(icon->IsPanelOpenForTesting());

  ClickScreenPoint(icon->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(
      base::test::RunUntil([&] { return icon->IsPanelOpenForTesting(); }));
  EXPECT_TRUE(icon->GetVisible());
  EXPECT_FALSE(IsCommandOverlayVisible())
      << "Clicking the utility icon must not open the command overlay";
}

// Replaces SearchPillHoverHighlightsTrailingActions: the header's trailing
// copy action copies the pane URL (the pill's hover chip styling has no
// header counterpart).
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       CopyUrlButtonCopiesPaneUrl) {
  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  FlushLayout();
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(u"maho-clipboard-sentinel");
  }
  views::ImageButton* copy_button = header->copy_url_button_for_testing();
  ASSERT_TRUE(copy_button->GetEnabled());

  ClickScreenPoint(copy_button->GetBoundsInScreen().CenterPoint());
  EXPECT_TRUE(base::test::RunUntil([&page_url] {
    return ui::clipboard_test_util::ReadText(
               ui::Clipboard::GetForCurrentThread(),
               ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/nullptr) ==
           base::UTF8ToUTF16(page_url.spec());
  })) << "Copy URL must write the pane URL to the clipboard";
}

// Replaces EditingSuppressesUtilityIconPlaceholderDoesNot: the placeholder
// (blank page) state keeps the utility affordance available.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       PlaceholderPageKeepsUtilityIconAvailable) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  EXPECT_EQ(header->host_button_for_testing()->GetText(),
            u"Search or Enter URL...");
  EXPECT_TRUE(header->utility_icon_for_testing()->GetVisible());
  EXPECT_TRUE(header->utility_icon_for_testing()->GetEnabled());
}

// Replaces UtilitySlotVisibleAtStartupBeforeHover,
// UtilityIconVisibleOnFirstPaintWithoutSeed and
// BuildSearchModelWithTabStripEntryHasActiveTab: with the fixture's initial
// tab (Maho windows start with zero tabs, so SetUpOnMainThread seeds one) and
// no profile seed or hover, the utility affordance is already available.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       UtilityIconVisibleAtStartupWithoutSeed) {
  ASSERT_GE(browser()->GetTabStripModel()->count(), 1);
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  MahoLocationBarUtilityIconView* icon = header->utility_icon_for_testing();
  EXPECT_TRUE(icon->GetVisible());
  EXPECT_TRUE(icon->GetEnabled());
  EXPECT_TRUE(icon->IsAffordanceVisibleForTesting());
  EXPECT_FALSE(icon->IsHoverVisibleForTesting());
}

// Replaces OnThemeChangedDoesNotHideUtilityAffordance and
// UtilityAffordanceSurvivesTransientActiveIndexNoTab: theme changes and a
// sidebar refresh never hide the header's utility affordance.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       UtilityAffordanceSurvivesThemeChangeAndRefresh) {
  SeedProfile(2);
  ResolveViews();
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);
  MahoLocationBarUtilityIconView* icon = header->utility_icon_for_testing();
  ASSERT_TRUE(icon->GetVisible());

  ASSERT_TRUE(sidebar_);
  sidebar_->OnThemeChanged();
  header->OnThemeChanged();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(icon->GetVisible());
  EXPECT_TRUE(icon->GetEnabled());
}

// Replaces the pill half of
// PersistentShellConsumersReceiveAtomicOwningBrowserPaletteSnapshot: the
// header receives the owning sidebar's palette snapshot, including previews.
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       HeaderReceivesOwningSidebarPalette) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  MahoContentsHeaderView* header = ActiveHeader();
  ASSERT_TRUE(header);

  auto expect_same = [](const MahoSidebarPalette& expected,
                        const MahoSidebarPalette& actual) {
    EXPECT_EQ(expected.primary_text, actual.primary_text);
    EXPECT_EQ(expected.secondary_text, actual.secondary_text);
    EXPECT_EQ(expected.neutral_glyph, actual.neutral_glyph);
    EXPECT_EQ(expected.disabled_text, actual.disabled_text);
  };
  expect_same(sidebar_->sidebar_palette(), header->palette_for_testing());

  // Plumbing alone is not enough: the header must paint the palette surface
  // its text roles were resolved against, not the dark container behind it.
  auto expect_drawn_on_palette = [header]() {
    const MahoSidebarPalette& palette = header->palette_for_testing();
    ASSERT_FALSE(palette.opaque_contrast_stops.empty());
    const SkBitmap bitmap = views::test::PaintViewToBitmap(header);
    ASSERT_FALSE(bitmap.isNull());
    const SkColor drawn = bitmap.getColor(1, bitmap.height() / 2);
    const bool palette_dark =
        color_utils::IsDark(palette.opaque_contrast_stops.front());
    EXPECT_EQ(color_utils::IsDark(drawn), palette_dark)
        << "header background must share the sidebar surface's tone";
    EXPECT_GE(color_utils::GetContrastRatio(
                  header->host_button_for_testing()->GetCurrentTextColor(),
                  drawn),
              color_utils::kMinimumReadableContrastRatio);
  };
  expect_drawn_on_palette();

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      static_cast<Browser*>(browser()),
      R"({"type":"solid","color":{"hue":300.0,"saturation":0.7,"brightness":0.5,"grain":0.2}})"));
  sidebar_->OnThemeChanged();
  expect_same(sidebar_->sidebar_palette(), header->palette_for_testing());
  expect_drawn_on_palette();
}

// The sidebar no longer hosts any address surface (IS-6).
IN_PROC_BROWSER_TEST_F(MahoContentsHeaderInteractiveTest,
                       SidebarHasNoSearchPill) {
  ResolveViews();
  ASSERT_TRUE(sidebar_);
  std::vector<views::View*> pending = {sidebar_.get()};
  while (!pending.empty()) {
    views::View* view = pending.back();
    pending.pop_back();
    EXPECT_NE(std::string_view(view->GetClassName()),
              "MahoSidebar" "SearchView");
    EXPECT_FALSE(views::IsViewClass<MahoContentsHeaderView>(view))
        << "The contents header must live in the contents pane, not the "
           "sidebar";
    for (views::View* child : view->children()) {
      pending.push_back(child);
    }
  }
}

}  // namespace
}  // namespace maho
