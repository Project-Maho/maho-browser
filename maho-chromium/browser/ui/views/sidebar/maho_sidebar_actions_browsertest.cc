// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/scoped_observation.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/time/time_override.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ai/maho_tab_tidy_orchestrator.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/notifications/maho_toast_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace maho {
namespace {

base::Time clear_now;

base::Time ClearNow() {
  return clear_now;
}

MahoSidebarTabListView* FindTabList(views::View* view) {
  if (auto* tab_list = views::AsViewClass<MahoSidebarTabListView>(view)) {
    return tab_list;
  }
  for (const auto& child : view->children()) {
    if (auto* tab_list = FindTabList(child)) {
      return tab_list;
    }
  }
  return nullptr;
}

class ViewHiddenSignal : public views::ViewObserver {
 public:
  explicit ViewHiddenSignal(views::View* view) { observation_.Observe(view); }

  bool Wait() { return hidden_.Wait(); }

  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override {
    if (!visible && !hidden_.IsReady()) {
      hidden_.SetValue();
    }
  }

 private:
  base::test::TestFuture<void> hidden_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

class MahoSidebarActionsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("maho-disable-login-gate");
    command_line->AppendSwitch("no-proxy-server");
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(GetCore());
    ASSERT_TRUE(MahoSpaceProfileBridge::GetInstance());
    space_id_ =
        MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser());
    ASSERT_FALSE(space_id_.empty());
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        sidebar_prefs::kSidebarLayoutEnabled, true);
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        sidebar_prefs::kSidebarPanelExpanded, true);
  }

  base::ListValue Dispatch(base::DictValue event) {
    const std::string event_json = base::WriteJson(event).value();
    const std::string updates =
        core::HandleEvent(GetCore(), event_json.c_str());
    InvalidateSidebarCoreCacheForUpdatesJson(updates);
    auto parsed = base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed && parsed->is_list()) << updates;
    return parsed && parsed->is_list() ? std::move(*parsed).TakeList()
                                      : base::ListValue();
  }

  std::string SeedSuspendedTab(int index) {
    auto updates = Dispatch(
        base::DictValue()
            .Set("kind", "create_tab")
            .Set("space_id", space_id_)
            .Set("url", "https://example.com/sidebar-test-" +
                            base::NumberToString(index))
            .Set("is_private", false));
    for (const auto& update : updates) {
      const auto* dict = update.GetIfDict();
      const auto* tab = dict ? dict->FindDict("tab") : nullptr;
      const auto* id = tab ? tab->FindString("id") : nullptr;
      if (id) {
        std::string tab_id = *id;
        Dispatch(base::DictValue()
                     .Set("kind", "suspend_tab")
                     .Set("tab_id", tab_id));
        return tab_id;
      }
    }
    ADD_FAILURE() << "create_tab did not return a tab";
    return {};
  }

  bool HasTab(const std::string& id) {
    char* snapshot = maho_core_get_tab_snapshot_by_id(GetCore(), id.c_str());
    if (!snapshot) {
      return false;
    }
    maho_string_free(snapshot);
    return true;
  }

  void CheckSourceBeforeSplitOrdering(bool insert_before_first) {
    auto* model = browser()->GetTabStripModel();
    std::vector<content::WebContents*> original_tabs;
    for (int i = 0; i < model->count(); ++i) {
      original_tabs.push_back(model->GetWebContentsAt(i));
    }
    const int first_index = model->count();
    std::vector<content::WebContents*> created_tabs;
    for (int i = 0; i < 3; ++i) {
      auto contents = content::WebContents::Create(
          content::WebContents::CreateParams(browser()->GetProfile()));
      created_tabs.push_back(contents.get());
      model->InsertWebContentsAt(model->count(), std::move(contents),
                                 AddTabTypes::ADD_ACTIVE);
    }
    auto* source = created_tabs[0];
    auto* pane_a = created_tabs[1];
    auto* pane_b = created_tabs[2];
    ASSERT_EQ(model->count(), first_index + 3);
    ASSERT_EQ(model->GetWebContentsAt(first_index), source);
    ASSERT_EQ(model->GetWebContentsAt(first_index + 1), pane_a);
    ASSERT_EQ(model->GetWebContentsAt(first_index + 2), pane_b);

    model->ActivateTabAt(model->GetIndexOfWebContents(pane_a));
    const auto split_id = model->AddToNewSplit(
        {model->GetIndexOfWebContents(pane_b)},
        split_tabs::SplitTabVisualData::CreateTwoPane(
            split_tabs::SplitTabLayout::kSideBySide, 0.5),
        split_tabs::SplitTabCreatedSource::kDragAndDropTab);
    ASSERT_TRUE(model->ContainsSplit(split_id));
    auto* split_data = model->GetSplitData(split_id);
    ASSERT_TRUE(split_data);
    const auto initial_panes = split_data->ListTabs();
    ASSERT_EQ(initial_panes.size(), 2u);
    ASSERT_EQ(initial_panes[0]->GetContents(), pane_a);
    ASSERT_EQ(initial_panes[1]->GetContents(), pane_b);
    ASSERT_EQ(model->GetIndexOfWebContents(source), first_index);
    ASSERT_EQ(model->GetIndexOfWebContents(pane_a), first_index + 1);
    ASSERT_EQ(model->GetIndexOfWebContents(pane_b), first_index + 2);
    ASSERT_FALSE(model->GetSplitForTab(first_index).has_value());
    ASSERT_TRUE(split_data->visual_data());
    auto visual_data = *split_data->visual_data();
    ASSERT_TRUE(visual_data.InsertPaneAtLeaf(
        insert_before_first ? 0u : 1u,
        split_tabs::SplitTabLayout::kSideBySide, 0.5,
        insert_before_first ? split_tabs::SplitPaneInsertionSide::kBefore
                            : split_tabs::SplitPaneInsertionSide::kAfter));

    // Removing a source before the split shifts the destination left by one.
    // Insertion after the last pane also exercises the tab-strip end boundary.
    ASSERT_TRUE(model->AddToExistingSplit(
        split_id, model->GetIndexOfWebContents(source),
        insert_before_first ? 0u : 2u, visual_data,
        split_tabs::SplitTabCreatedSource::kDragAndDropTab));

    ASSERT_EQ(model->count(), first_index + 3);
    ASSERT_TRUE(model->ContainsSplit(split_id));
    split_data = model->GetSplitData(split_id);
    ASSERT_TRUE(split_data);
    const auto panes = split_data->ListTabs();
    ASSERT_EQ(panes.size(), 3u);
    ASSERT_TRUE(split_data->visual_data());
    EXPECT_TRUE(split_data->visual_data()->ValidateForPaneCount(3u));
    const std::vector<content::WebContents*> expected =
        insert_before_first
            ? std::vector<content::WebContents*>{source, pane_a, pane_b}
            : std::vector<content::WebContents*>{pane_a, pane_b, source};
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(model->GetWebContentsAt(first_index + i), expected[i]);
      EXPECT_EQ(panes[i]->GetContents(), expected[i]);
      const auto member_split = model->GetSplitForTab(first_index + i);
      ASSERT_TRUE(member_split.has_value());
      EXPECT_EQ(*member_split, split_id);
    }
    for (int i = 0; i < first_index; ++i) {
      EXPECT_EQ(model->GetWebContentsAt(i), original_tabs[i]);
    }
  }

  std::string space_id_;
};

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsBrowserTest,
                       ClearClosesLiveAndSuspendedTabsButPreservesPinnedTabs) {
  auto* model = browser()->GetTabStripModel();
  model->InsertWebContentsAt(
      model->count(),
      content::WebContents::Create(
          content::WebContents::CreateParams(browser()->GetProfile())),
      AddTabTypes::ADD_ACTIVE);
  std::vector<std::string> pinned_ids;
  for (int i = 0; i < model->count(); ++i) {
    auto* helper =
        MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    ASSERT_TRUE(helper);
    pinned_ids.push_back(helper->stable_tab_id());
    Dispatch(base::DictValue()
                 .Set("kind", "pin_tab")
                 .Set("tab_id", pinned_ids.back()));
    ASSERT_TRUE(maho_core_is_tab_close_protected(
        GetCore(), pinned_ids.back().c_str()));
  }

  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest&)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        response->set_content_type("text/html");
        response->set_content("<title>Clear live tab</title>");
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());
  model->InsertWebContentsAt(
      model->count(),
      content::WebContents::Create(
          content::WebContents::CreateParams(browser()->GetProfile())),
      AddTabTypes::ADD_ACTIVE);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/live")));
  auto* live_helper =
      MahoTabIdHelper::FromWebContents(model->GetActiveWebContents());
  ASSERT_TRUE(live_helper);
  const std::string live_id = live_helper->stable_tab_id();
  const std::string suspended_id = SeedSuspendedTab(1);
  ASSERT_FALSE(suspended_id.empty());
  ASSERT_TRUE(HasTab(live_id));
  ASSERT_TRUE(HasTab(suspended_id));
  const int live_count = model->count();

  auto* tab_list =
      FindTabList(BrowserView::GetBrowserViewForBrowser(browser()));
  ASSERT_TRUE(tab_list);
  auto* processing = tab_list->action_processing_view_for_testing();
  ASSERT_TRUE(processing);
  processing->set_disable_animation_for_testing(true);
  ViewHiddenSignal placeholder_hidden(processing);
  clear_now = base::Time::Now() + base::Hours(2);
  base::subtle::ScopedTimeClockOverrides clock(&ClearNow, nullptr, nullptr);
  constexpr int kLastHourCommand = 100;
  static_cast<ui::SimpleMenuModel::Delegate*>(tab_list)->ExecuteCommand(
      kLastHourCommand, 0);

  ASSERT_TRUE(placeholder_hidden.Wait());
  EXPECT_FALSE(tab_list->action_placeholder_active_for_testing());
  EXPECT_FALSE(processing->is_active_for_testing());
  EXPECT_EQ(processing->phase_for_testing(),
            MahoSidebarProcessingPlaceholderView::Phase::kIdle);
  EXPECT_EQ(model->count(), live_count - 1);
  EXPECT_FALSE(HasTab(live_id));
  EXPECT_FALSE(HasTab(suspended_id));
  for (const auto& id : pinned_ids) {
    EXPECT_TRUE(HasTab(id));
  }
  const int actual_closed_count =
      static_cast<int>(!HasTab(live_id)) +
      static_cast<int>(!HasTab(suspended_id));
  EXPECT_EQ(actual_closed_count, 2);
  auto* overlay = MahoNotificationOverlay::FromBrowser(static_cast<Browser*>(browser()));
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->toast_view_for_testing());
  EXPECT_EQ(overlay->toast_view_for_testing()
                ->title_label_for_testing()
                ->GetText(),
            u"Clear Tabs");
  EXPECT_EQ(
      overlay->toast_view_for_testing()->body_label_for_testing()->GetText(),
      u"Closed " + base::NumberToString16(actual_closed_count) + u" tabs.");
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsBrowserTest,
                       TidyAppliesFoldersFromConfiguredHttpProvider) {
  base::ListValue first_group;
  base::ListValue second_group;
  for (int i = 0; i < 6; ++i) {
    std::string id = SeedSuspendedTab(i);
    ASSERT_FALSE(id.empty());
    (i < 3 ? first_group : second_group).Append(std::move(id));
  }
  base::ListValue folders;
  folders.Append(base::DictValue()
                     .Set("name", "First group")
                     .Set("tab_ids", std::move(first_group)));
  folders.Append(base::DictValue()
                     .Set("name", "Second group")
                     .Set("tab_ids", std::move(second_group)));
  const std::string result = base::WriteJson(
      base::DictValue().Set("folders", std::move(folders))).value();
  base::ListValue choices;
  choices.Append(
      base::DictValue().Set("delta", base::DictValue().Set("content", result)));
  const std::string response_body =
      "data: " +
      base::WriteJson(base::DictValue().Set("choices", std::move(choices)))
          .value() +
      "\n\ndata: [DONE]\n\n";
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](const std::string& body, const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/v1/chat/completions") {
          return nullptr;
        }
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        response->set_content_type("text/event-stream");
        response->set_content(body);
        return response;
      },
      response_body));
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetString(ai_prefs::kProvider, "openai-compatible");
  prefs->SetString(ai_prefs::kBaseUrl,
                   embedded_test_server()->GetURL("/v1").spec());
  prefs->SetString(ai_prefs::kModel, "fixture-model");
  prefs->SetString(ai_prefs::kApiKey, "fixture-key");
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(20));
  base::test::TestFuture<int, std::string> finished;
  MahoTabTidyOrchestrator::RunOneShot(
      browser(),
      base::BindOnce(
          [](base::test::TestFuture<int, std::string>* future, int count,
             const std::string& error) { future->SetValue(count, error); },
          &finished),
      base::BindRepeating([] { return true; }));

  ASSERT_TRUE(finished.Wait());
  EXPECT_EQ(finished.Get<0>(), 2);
  EXPECT_TRUE(finished.Get<1>().empty()) << finished.Get<1>();
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsBrowserTest,
                       AddToExistingSplitSourceBeforeSplitBeforeFirstPane) {
  CheckSourceBeforeSplitOrdering(/*insert_before_first=*/true);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsBrowserTest,
                       AddToExistingSplitSourceBeforeSplitAfterLastPane) {
  CheckSourceBeforeSplitOrdering(/*insert_before_first=*/false);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsBrowserTest,
                       CoreShutdownDrainsWithoutPumpingUiTasks) {
  bool ui_task_ran = false;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([](bool* ran) { *ran = true; }, &ui_task_ran));
  QuiesceCoreTasksAndWait();
  EXPECT_FALSE(ui_task_ran);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(ui_task_ran);
}

}  // namespace
}  // namespace maho
