// Copyright 2026 Maho Browser. All rights reserved.
//
// Restart regression for pinned splits. A split whose two panes are pinned
// must come back after a browser restart with each pane still bound to its
// maho-core record (same stable tab id, Pinned role), the panes still split
// together, and the split shown as one group in the sidebar's pinned section.
//
// Each scenario is a PRE_/main pair: PRE_ builds the state through the tab
// context menu and lets the harness quit; the main test starts on the same
// profile and asserts the restored state. The *AfterSessionRebuild scenarios
// also rebuild the session file before quitting, which Chromium does on its
// own every kWritesPerReset (250) commands and when a window reopens after
// all windows were closed.

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "url/gurl.h"

namespace maho {
namespace {

constexpr char kPaneA[] = "data:text/html,pinned-split-a";
constexpr char kPaneB[] = "data:text/html,pinned-split-b";
// Written by PRE_ into the profile dir so the restarted run can compare ids.
constexpr char kPaneIdsFile[] = "maho_pinned_split_pane_ids";

enum class Order { kPinThenSplit, kSplitThenPin };

class MahoPinnedSplitRestartTest : public MahoSidebarInteractiveTestBase {
 protected:
  void SetUpOnMainThread() override {
    MahoSidebarInteractiveTestBase::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }

  Browser* GetBrowser() { return static_cast<Browser*>(browser()); }
  TabStripModel* model() { return browser()->GetTabStripModel(); }

  int IndexForUrl(const char* url) {
    for (int i = 0; i < model()->count(); ++i) {
      content::WebContents* wc = model()->GetWebContentsAt(i);
      if (wc && wc->GetLastCommittedURL() == GURL(url)) {
        return i;
      }
    }
    return -1;
  }

  std::string StableIdAt(int index) {
    content::WebContents* wc =
        index >= 0 ? model()->GetWebContentsAt(index) : nullptr;
    auto* helper = wc ? MahoTabIdHelper::FromWebContents(wc) : nullptr;
    return helper ? helper->stable_tab_id() : std::string();
  }

  base::FilePath PaneIdsPath() {
    return browser()->GetProfile()->GetPath().AppendASCII(kPaneIdsFile);
  }

  void OpenPanes() {
    for (const char* url : {kPaneA, kPaneB}) {
      content::WebContents* wc =
          chrome::AddAndReturnTabAt(browser(), GURL(url), -1, false);
      ASSERT_TRUE(wc);
      ASSERT_TRUE(content::WaitForLoadStop(wc));
    }
    ASSERT_TRUE(base::test::RunUntil([&] {
      return GetCachedCoreTabFacts(StableIdAt(IndexForUrl(kPaneA))) &&
             GetCachedCoreTabFacts(StableIdAt(IndexForUrl(kPaneB)));
    })) << "panes were never registered with maho-core";
  }

  void PinViaContextMenu(const char* url) {
    const int index = IndexForUrl(url);
    ASSERT_GE(index, 0);
    const std::string id = StableIdAt(index);
    ::MahoTabContextMenu(GetBrowser(), index, nullptr, id)
        .ExecuteCommand(IDC_MAHO_TAB_PIN, 0);
    ASSERT_TRUE(base::test::RunUntil([&] { return IsCorePinnedTab(id); }))
        << "maho-core never recorded the pin for " << url;
  }

  void SplitViaContextMenu() {
    const int a = IndexForUrl(kPaneA);
    ASSERT_GE(a, 0);
    model()->ActivateTabAt(
        a, TabStripUserGestureDetails(
               TabStripUserGestureDetails::GestureType::kOther));
    const int b = IndexForUrl(kPaneB);
    ASSERT_GE(b, 0);
    ::MahoTabContextMenu(GetBrowser(), b, nullptr, StableIdAt(b))
        .ExecuteCommand(IDC_MAHO_TAB_OPEN_SPLIT, 0);
    ASSERT_TRUE(base::test::RunUntil([&] {
      const int ia = IndexForUrl(kPaneA);
      const int ib = IndexForUrl(kPaneB);
      return ia >= 0 && ib >= 0 && model()->GetSplitForTab(ia).has_value() &&
             model()->GetSplitForTab(ia) == model()->GetSplitForTab(ib);
    })) << "the split was never created";
  }

  bool PinnedSectionShowsSplit(const std::string& id_a,
                               const std::string& id_b) {
    const MahoSidebarViewStateModel state = GetViewState();
    for (const SidebarTreeNode& node : state.tab_list.pinned_tree) {
      if (node.kind != SidebarNodeKind::kSplitGroup ||
          node.children.size() != 2u) {
        continue;
      }
      const std::string& first = node.children[0].tab_id;
      const std::string& second = node.children[1].tab_id;
      if ((first == id_a && second == id_b) ||
          (first == id_b && second == id_a)) {
        return true;
      }
    }
    return false;
  }

  std::string DescribeSidebar() {
    const MahoSidebarViewStateModel state = GetViewState();
    std::string out;
    auto describe = [&out](const char* section,
                           const std::vector<SidebarTreeNode>& nodes) {
      for (const SidebarTreeNode& node : nodes) {
        if (node.kind == SidebarNodeKind::kSplitGroup) {
          out += base::StringPrintf("  %s split:", section);
          for (const SidebarTreeNode& child : node.children) {
            out += " " + child.tab_id;
          }
          out += "\n";
        } else if (node.kind == SidebarNodeKind::kTab) {
          out += base::StringPrintf(
              "  %s tab %s strip_index=%d suspended=%d url=%s\n", section,
              node.tab_id.c_str(), node.tab_strip_index, node.is_suspended,
              node.url.c_str());
        }
      }
    };
    describe("pinned", state.tab_list.pinned_tree);
    describe("normal", state.tab_list.normal_tree);
    return out;
  }

  std::string DescribeCoreFacts(const std::string& id) {
    const auto facts = GetCachedCoreTabFacts(id);
    if (!facts) {
      return id + " (no core facts)";
    }
    return base::StringPrintf("%s exists=%d pinned=%d", id.c_str(),
                              facts->exists, facts->pinned);
  }

  void BuildPinnedSplit(Order order, bool rebuild_session) {
    OpenPanes();
    if (order == Order::kPinThenSplit) {
      PinViaContextMenu(kPaneA);
      PinViaContextMenu(kPaneB);
      SplitViaContextMenu();
    } else {
      SplitViaContextMenu();
      PinViaContextMenu(kPaneA);
      PinViaContextMenu(kPaneB);
    }
    const std::string id_a = StableIdAt(IndexForUrl(kPaneA));
    const std::string id_b = StableIdAt(IndexForUrl(kPaneB));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return PinnedSectionShowsSplit(id_a, id_b); }))
        << "the pinned split never showed in the pinned section:\n"
        << DescribeSidebar();
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::WriteFile(PaneIdsPath(), id_a + "\n" + id_b));
    }
    if (rebuild_session) {
      SessionService* service =
          SessionServiceFactory::GetForProfile(browser()->GetProfile());
      ASSERT_TRUE(service);
      service->ResetFromCurrentBrowsers();
    }
  }

  void ExpectPinnedSplitRestored() {
    std::vector<std::string> pre_restart_ids;
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      std::string contents;
      ASSERT_TRUE(base::ReadFileToString(PaneIdsPath(), &contents));
      pre_restart_ids = base::SplitString(
          contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    }
    ASSERT_EQ(pre_restart_ids.size(), 2u);

    ASSERT_TRUE(base::test::RunUntil([&] {
      return IndexForUrl(kPaneA) >= 0 && IndexForUrl(kPaneB) >= 0;
    })) << "the split panes were not restored";
    const int a = IndexForUrl(kPaneA);
    const int b = IndexForUrl(kPaneB);
    const std::string id_a = StableIdAt(a);
    const std::string id_b = StableIdAt(b);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return GetCachedCoreTabFacts(id_a) && GetCachedCoreTabFacts(id_b);
    })) << "the restored panes never reached maho-core";

    EXPECT_EQ(id_a, pre_restart_ids[0])
        << "pane A was restored under a new tab id; pre-restart record: "
        << DescribeCoreFacts(pre_restart_ids[0]);
    EXPECT_EQ(id_b, pre_restart_ids[1])
        << "pane B was restored under a new tab id; pre-restart record: "
        << DescribeCoreFacts(pre_restart_ids[1]);
    EXPECT_TRUE(IsCorePinnedTab(id_a))
        << "pane A is not pinned: " << DescribeCoreFacts(id_a);
    EXPECT_TRUE(IsCorePinnedTab(id_b))
        << "pane B is not pinned: " << DescribeCoreFacts(id_b);
    ASSERT_TRUE(model()->GetSplitForTab(a).has_value())
        << "pane A is no longer in a split";
    EXPECT_EQ(model()->GetSplitForTab(a), model()->GetSplitForTab(b))
        << "the panes are no longer split together";
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return PinnedSectionShowsSplit(id_a, id_b); }))
        << "the sidebar does not show the split in the pinned section:\n"
        << DescribeSidebar();
  }
};

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       PRE_PinThenSplitSurvivesRestart) {
  BuildPinnedSplit(Order::kPinThenSplit, /*rebuild_session=*/false);
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       PinThenSplitSurvivesRestart) {
  ExpectPinnedSplitRestored();
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       PRE_SplitThenPinSurvivesRestart) {
  BuildPinnedSplit(Order::kSplitThenPin, /*rebuild_session=*/false);
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       SplitThenPinSurvivesRestart) {
  ExpectPinnedSplitRestored();
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       PRE_PinThenSplitSurvivesRestartAfterSessionRebuild) {
  BuildPinnedSplit(Order::kPinThenSplit, /*rebuild_session=*/true);
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       PinThenSplitSurvivesRestartAfterSessionRebuild) {
  ExpectPinnedSplitRestored();
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       PRE_SplitThenPinSurvivesRestartAfterSessionRebuild) {
  BuildPinnedSplit(Order::kSplitThenPin, /*rebuild_session=*/true);
}

IN_PROC_BROWSER_TEST_F(MahoPinnedSplitRestartTest,
                       SplitThenPinSurvivesRestartAfterSessionRebuild) {
  ExpectPinnedSplitRestored();
}

}  // namespace
}  // namespace maho
