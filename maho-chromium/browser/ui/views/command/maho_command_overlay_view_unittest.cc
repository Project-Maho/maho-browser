// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "maho/browser/ui/views/command/maho_remote_search_suggestions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

struct ActionSelectorCounts {
  int total = 0;
  int visible = 0;
};

struct ActionSelectorAxCounts {
  int radio_groups = 0;
  int search_segments = 0;
  int ask_maho_segments = 0;
};

ActionSelectorCounts CountActionSelectors(const views::View* root,
                                          bool ancestor_visible = true) {
  ActionSelectorCounts counts;
  const bool visible = ancestor_visible && root->GetVisible();
  if (views::IsViewClass<MahoCommandActionSelectorView>(root)) {
    ++counts.total;
    if (visible) {
      ++counts.visible;
    }
  }

  for (const auto& child : root->children()) {
    const ActionSelectorCounts child_counts =
        CountActionSelectors(child.get(), visible);
    counts.total += child_counts.total;
    counts.visible += child_counts.visible;
  }
  return counts;
}

ActionSelectorAxCounts CountVisibleActionSelectorAxNodes(
    const views::View* root,
    bool ancestor_visible = true) {
  ActionSelectorAxCounts counts;
  const bool visible = ancestor_visible && root->GetVisible();
  if (visible) {
    ui::AXNodeData data;
    root->GetViewAccessibility().GetAccessibleNodeData(&data);
    const std::u16string name =
        data.GetString16Attribute(ax::mojom::StringAttribute::kName);
    if (data.role == ax::mojom::Role::kRadioGroup &&
        name == u"Command Palette Action Selector") {
      ++counts.radio_groups;
    }
    if (data.role == ax::mojom::Role::kRadioButton) {
      if (name == u"Search Mode") {
        ++counts.search_segments;
      } else if (name == u"Ask Maho Mode") {
        ++counts.ask_maho_segments;
      }
    }
  }

  for (const auto& child : root->children()) {
    const ActionSelectorAxCounts child_counts =
        CountVisibleActionSelectorAxNodes(child.get(), visible);
    counts.radio_groups += child_counts.radio_groups;
    counts.search_segments += child_counts.search_segments;
    counts.ask_maho_segments += child_counts.ask_maho_segments;
  }
  return counts;
}

class MahoCommandOverlayViewTest : public views::ViewsTestBase {
 protected:
  MahoCommandOverlayViewTest() = default;
  ~MahoCommandOverlayViewTest() override = default;

  MahoCommandOverlayView* CreateView(
      CommandOverlayMode mode,
      const std::string& initial_text = std::string(),
      bool select_initial_text = false) {
    host_widget_ = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget_->Show();

    auto view = std::make_unique<MahoCommandOverlayView>(
        MahoCommandOverlayView::ForTestingTag{}, mode, initial_text,
        select_initial_text,
        base::BindOnce(&MahoCommandOverlayViewTest::OnDismiss,
                       base::Unretained(this)),
        base::BindRepeating(&MahoCommandOverlayViewTest::OnResize,
                            base::Unretained(this)));

    auto* raw = view.get();
    host_widget_->SetContentsView(std::move(view));
    raw->textfield()->RequestFocus();
    return raw;
  }

  void TearDown() override {
    host_widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  bool WasDismissed() const { return dismiss_count_ > 0; }
  int DismissCount() const { return dismiss_count_; }

  static bool SendKey(MahoCommandOverlayView* view, ui::KeyboardCode code,
                      int flags = ui::EF_NONE) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, flags);
    return view->HandleKeyEvent(view->textfield(), event);
  }

 private:
  void OnDismiss() { ++dismiss_count_; }
  void OnResize(int) {}

  std::unique_ptr<views::Widget> host_widget_;
  int dismiss_count_ = 0;
};

TEST_F(MahoCommandOverlayViewTest, EscapeInDefaultModeFiresDismissCallback) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, "https://maho.test");
  ASSERT_FALSE(WasDismissed());
  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));
  EXPECT_TRUE(WasDismissed());
}

TEST_F(MahoCommandOverlayViewTest, SearchModePlaceholderIsSearchOrEnterUrl) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  EXPECT_EQ(u"Search or Enter URL\u2026",
            view->textfield()->GetPlaceholderText());
}

TEST_F(MahoCommandOverlayViewTest,
       NormalModesOwnExactlyOneVisibleActionSelector) {
  constexpr CommandOverlayMode kNormalModes[] = {
      CommandOverlayMode::kSearch,
      CommandOverlayMode::kNewTab,
      CommandOverlayMode::kCurrentTab,
  };

  for (const CommandOverlayMode mode : kNormalModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto* view = CreateView(mode);
    const ActionSelectorCounts counts = CountActionSelectors(view);
    const ActionSelectorAxCounts ax_counts =
        CountVisibleActionSelectorAxNodes(view);

    EXPECT_EQ(1, counts.total);
    EXPECT_EQ(1, counts.visible);
    EXPECT_EQ(1, ax_counts.radio_groups);
    EXPECT_EQ(1, ax_counts.search_segments);
    EXPECT_EQ(1, ax_counts.ask_maho_segments);
    ASSERT_NE(nullptr, view->GetActionSelectorForTesting());
    EXPECT_TRUE(view->GetActionSelectorForTesting()->GetVisible());
    EXPECT_FALSE(view->IsModeChipVisibleForTesting());
  }
}

TEST_F(MahoCommandOverlayViewTest, TypeAiThenTabTogglesToAskMahoInRegularContext) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  ASSERT_TRUE(view->AiIngressAllowedForTesting());

  view->textfield()->SetText(u"ai");
  EXPECT_TRUE(SendKey(view, ui::VKEY_TAB));

  EXPECT_EQ(PaletteAction::kAskMaho, view->GetPaletteActionForTesting());
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
  EXPECT_EQ(u"ai", view->textfield()->GetText()) << "Text must be preserved after toggle";
}

TEST_F(MahoCommandOverlayViewTest, EscapeInAskMahoDismissesWidget) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  ASSERT_EQ(PaletteAction::kAskMaho, view->GetPaletteActionForTesting());

  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));
  EXPECT_TRUE(WasDismissed());
}

TEST_F(MahoCommandOverlayViewTest, AiIngressDeniedBeforeMutationInIncognito) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  view->SetContextClassForTesting(MahoPrivateContextClass::kPrimaryIncognito);
  ASSERT_FALSE(view->AiIngressAllowedForTesting());

  // Programmatic setup of disabled action selector
  view->GetActionSelectorForTesting()->SetEnabled(false);

  view->textfield()->SetText(u"ai");
  // Sending Tab in disabled mode
  EXPECT_TRUE(SendKey(view, ui::VKEY_TAB));

  EXPECT_EQ(PaletteAction::kSearch, view->GetPaletteActionForTesting())
      << "Incognito context must block AI action toggling";
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
  EXPECT_FALSE(WasDismissed());
}

std::vector<CommandSuggestion> MakeAskModeSuggestions(int count) {
  std::vector<CommandSuggestion> suggestions;
  for (int i = 0; i < count; ++i) {
    CommandSuggestion suggestion;
    suggestion.type = CommandSuggestionType::kSearch;
    suggestion.key = "ask-mode:" + std::to_string(i);
    suggestion.title = "Result " + std::to_string(i);
    suggestion.subtitle = "https://ask" + std::to_string(i) + ".example/path";
    suggestion.execution_payload = suggestion.subtitle;
    suggestions.push_back(std::move(suggestion));
  }
  return suggestions;
}

TEST_F(MahoCommandOverlayViewTest,
       AskMahoFirstRowUsesQueryTitleWhileSearchTitleIsUnchanged) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  view->textfield()->SetText(u"weather seoul");
  ASSERT_TRUE(SendKey(view, ui::VKEY_TAB));
  ASSERT_EQ(PaletteAction::kAskMaho, view->GetPaletteActionForTesting());

  std::vector<CommandSuggestion> results = MakeAskModeSuggestions(3);
  results[0].title = "weather seoul \u2014 Search with Google";
  view->UpdateResultViewsForTesting(results);
  ASSERT_EQ(3u, view->GetRenderedResultCountForTesting());

  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(3u, children.size());

  auto* first_row = views::AsViewClass<MahoCommandResultRowView>(children[0]);
  ASSERT_NE(nullptr, first_row);
  EXPECT_TRUE(first_row->UsesAskGlyphForTesting());
  ASSERT_NE(nullptr, first_row->GetAskExplanationLabelForTesting());
  EXPECT_TRUE(first_row->GetAskExplanationLabelForTesting()->GetVisible());
  EXPECT_EQ(u"Ask Maho",
            first_row->GetAskExplanationLabelForTesting()->GetText());
  EXPECT_EQ(u"weather seoul",
            first_row->GetTitleLabelForTesting()->GetText());
  EXPECT_EQ(std::u16string::npos,
            first_row->GetTitleLabelForTesting()->GetText().find(
                u"Search with Google"));

  for (size_t i = 1; i < children.size(); ++i) {
    SCOPED_TRACE(i);
    auto* row = views::AsViewClass<MahoCommandResultRowView>(children[i]);
    ASSERT_NE(nullptr, row);
    EXPECT_FALSE(row->UsesAskGlyphForTesting());
    EXPECT_EQ(nullptr, row->GetAskExplanationLabelForTesting());
  }

  // Only the first Ask row changes; all following titles remain untouched.
  for (size_t i = 1; i < children.size(); ++i) {
    auto* row = views::AsViewClass<MahoCommandResultRowView>(children[i]);
    EXPECT_EQ(base::UTF8ToUTF16(results[i].title),
              row->GetTitleLabelForTesting()->GetText());
  }

  ASSERT_TRUE(SendKey(view, ui::VKEY_TAB));
  ASSERT_EQ(PaletteAction::kSearch, view->GetPaletteActionForTesting());
  view->UpdateResultViewsForTesting(results);
  auto* search_first_row = views::AsViewClass<MahoCommandResultRowView>(
      view->GetResultsContainerForTesting()->children().front().get());
  ASSERT_NE(nullptr, search_first_row);
  EXPECT_EQ(base::UTF8ToUTF16(results[0].title),
            search_first_row->GetTitleLabelForTesting()->GetText());
}

TEST_F(MahoCommandOverlayViewTest, NonAiModesNeverShowAskAffordanceOnAnyRow) {
  constexpr CommandOverlayMode kModes[] = {
      CommandOverlayMode::kSearch,
      CommandOverlayMode::kNewTab,
      CommandOverlayMode::kCurrentTab,
      CommandOverlayMode::kCommandsOnly,
  };

  for (const CommandOverlayMode mode : kModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto* view = CreateView(mode);
    ASSERT_EQ(PaletteAction::kSearch, view->GetPaletteActionForTesting());

    view->UpdateResultViewsForTesting(MakeAskModeSuggestions(3));
    ASSERT_EQ(3u, view->GetRenderedResultCountForTesting());

    for (const auto& child : view->GetResultsContainerForTesting()->children()) {
      auto* row = views::AsViewClass<MahoCommandResultRowView>(child.get());
      ASSERT_NE(nullptr, row);
      EXPECT_FALSE(row->UsesAskGlyphForTesting());
      EXPECT_EQ(nullptr, row->GetAskExplanationLabelForTesting());
    }
  }
}

struct OverlayRemoteSourceState {
  int start_count = 0;
  int cancel_count = 0;
  std::vector<std::string> queries;
  std::vector<MahoRemoteSearchSuggestionSource::SnapshotCallback> callbacks;
};

class OverlayFakeRemoteSource final
    : public MahoRemoteSearchSuggestionSource {
 public:
  explicit OverlayFakeRemoteSource(OverlayRemoteSourceState* state)
      : state_(state) {}

  void Start(const std::string& query, SnapshotCallback callback) override {
    ++state_->start_count;
    state_->queries.push_back(query);
    state_->callbacks.push_back(std::move(callback));
  }

  void Cancel() override { ++state_->cancel_count; }

 private:
  const raw_ptr<OverlayRemoteSourceState> state_;
};

CommandSuggestion MakeOverlayLocal(int index) {
  CommandSuggestion suggestion;
  suggestion.type = CommandSuggestionType::kHistory;
  suggestion.key = "local:" + std::to_string(index);
  suggestion.title = "Local " + std::to_string(index);
  suggestion.subtitle =
      "https://local" + std::to_string(index) + ".example/path";
  suggestion.execution_payload = suggestion.subtitle;
  return suggestion;
}

std::vector<CommandSuggestion> MakeOverlayLocals(int count) {
  std::vector<CommandSuggestion> suggestions;
  for (int i = 0; i < count; ++i) {
    suggestions.push_back(MakeOverlayLocal(i));
  }
  return suggestions;
}

RemoteSearchSuggestion MakeOverlayRemote(
    const std::string& terms,
    const std::string& title,
    const std::string& subtitle,
    const std::string& destination) {
  return RemoteSearchSuggestion(terms, title, subtitle, GURL(destination));
}

MahoCommandResultRowView* OverlayRowAt(MahoCommandOverlayView* view,
                                       size_t index) {
  auto children = view->GetResultsContainerForTesting()->children();
  if (index >= children.size()) {
    return nullptr;
  }
  return views::AsViewClass<MahoCommandResultRowView>(children[index]);
}

std::vector<std::string> RenderedTitles(MahoCommandOverlayView* view) {
  std::vector<std::string> titles;
  for (size_t i = 0; i < view->GetRenderedResultCountForTesting(); ++i) {
    auto* row = OverlayRowAt(view, i);
    if (row) {
      titles.push_back(base::UTF16ToUTF8(
          row->GetTitleLabelForTesting()->GetText()));
    }
  }
  return titles;
}

bool RowIsSelected(MahoCommandResultRowView* row) {
  ui::AXNodeData data;
  row->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected);
}

class MahoCommandOverlayRemoteViewTest : public BrowserWithTestWindowTest {
 public:
  MahoCommandOverlayRemoteViewTest()
      : BrowserWithTestWindowTest(
            content::BrowserTaskEnvironment::TimeSource::MOCK_TIME,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED) {}

  void TearDown() override {
    widget_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  MahoCommandOverlayView* CreateBrowserView() {
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    // TestBrowserWindow has no native window; use the harness root context.
    params.context = GetContext();
    widget_ = std::make_unique<views::Widget>();
    widget_->Init(std::move(params));

    auto view = std::make_unique<MahoCommandOverlayView>(
        browser(), CommandOverlayMode::kSearch, std::string(), false,
        base::BindOnce(&MahoCommandOverlayRemoteViewTest::OnDismiss,
                       base::Unretained(this)),
        base::DoNothing());
    auto* raw = view.get();
    widget_->SetContentsView(std::move(view));
    widget_->Show();
    raw->textfield()->RequestFocus();
    return raw;
  }

  void StartViewQuery(MahoCommandOverlayView* view,
                      const std::u16string& query) {
    view->GetModelForTesting()->Cancel();
    view->textfield()->SetText(query);
    view->ContentsChanged(view->textfield(), query);
    task_environment()->FastForwardBy(base::Milliseconds(40));
  }

  static bool SendKey(MahoCommandOverlayView* view,
                      ui::KeyboardCode code) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    return view->HandleKeyEvent(view->textfield(), event);
  }

  int dismiss_count() const { return dismiss_count_; }
  OverlayRemoteSourceState remote_state_;

 private:
  void OnDismiss() { ++dismiss_count_; }

  std::unique_ptr<views::Widget> widget_;
  int dismiss_count_ = 0;
};

TEST_F(MahoCommandOverlayRemoteViewTest,
       UnifiedRowsRestoreSelectionAndSuppressStaleOrDuplicateRebuilds) {
  auto* view = CreateBrowserView();
  // This mock-time test drains posted work while exercising result rebuilds.
  // Hide only its selector so the intentionally continuous visible-palette
  // animation does not make that drain non-terminating.
  view->GetActionSelectorForTesting()->SetVisible(false);
  view->GetModelForTesting()->SetRemoteSearchSourceForTesting(
      std::make_unique<OverlayFakeRemoteSource>(&remote_state_));
  StartViewQuery(view, u"weather seoul");

  MahoCommandModel* model = view->GetModelForTesting();
  const uint32_t weather_generation = model->search_generation_for_testing();
  model->AcceptLocalResultsForTesting(weather_generation,
                                      MakeOverlayLocals(5));
  model->StartRemoteSearchForTesting(weather_generation, "weather seoul");
  ASSERT_EQ(1u, remote_state_.callbacks.size());

  const RemoteSearchSuggestion remote_one = MakeOverlayRemote(
      "weather tomorrow", "Weather tomorrow", "Forecast metadata",
      "https://search.example/search?q=weather%20tomorrow&src=remote");
  const RemoteSearchSuggestion remote_two = MakeOverlayRemote(
      "weather hourly", "Weather hourly", "Hourly metadata",
      "https://search.example/search?q=weather%20hourly&src=remote");
  const RemoteSearchSuggestion hidden = MakeOverlayRemote(
      "weather weekend", "Weather weekend", "Weekend metadata",
      "https://search.example/search?q=weather%20weekend&src=remote");
  remote_state_.callbacks[0].Run({remote_one, remote_two, hidden});

  ASSERT_EQ(5u, view->GetRenderedResultCountForTesting());
  EXPECT_EQ((std::vector<std::string>{"Local 0", "Local 1", "Local 2",
                                      "Weather tomorrow", "Weather hourly"}),
            RenderedTitles(view));
  for (size_t i = 3; i < 5; ++i) {
    ASSERT_EQ(CommandSuggestionType::kSearch, model->results()[i].type);
  }

  auto* first_remote_row = OverlayRowAt(view, 3);
  ASSERT_NE(nullptr, first_remote_row);
  EXPECT_EQ(u"Weather tomorrow",
            first_remote_row->GetTitleLabelForTesting()->GetText());
  ASSERT_NE(nullptr,
            first_remote_row->GetCompactMetadataLabelForTesting());
  EXPECT_EQ(u"Forecast metadata",
            first_remote_row->GetCompactMetadataLabelForTesting()->GetText());
  EXPECT_TRUE(
      first_remote_row->GetCompactMetadataLabelForTesting()->GetVisible());
  ui::AXNodeData remote_ax;
  first_remote_row->GetViewAccessibility().GetAccessibleNodeData(&remote_ax);
  EXPECT_EQ(u"Weather tomorrow, Forecast metadata",
            remote_ax.GetString16Attribute(
                ax::mojom::StringAttribute::kName));

  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(SendKey(view, ui::VKEY_DOWN));
  }
  ASSERT_EQ("remote-search:weather hourly",
            model->results()[model->selected_index()].key);
  ASSERT_TRUE(RowIsSelected(OverlayRowAt(view, 4)));

  remote_state_.callbacks[0].Run({remote_two, remote_one, hidden});
  ASSERT_EQ(3, model->selected_index());
  EXPECT_EQ("remote-search:weather hourly",
            model->results()[model->selected_index()].key);
  EXPECT_TRUE(RowIsSelected(OverlayRowAt(view, 3)));

  const int rebuild_count = view->GetResultViewRebuildCountForTesting();
  const std::vector<std::string> stable_titles = RenderedTitles(view);
  remote_state_.callbacks[0].Run({remote_two, remote_one, hidden});
  remote_state_.callbacks[0].Run(
      {remote_two, remote_one,
       MakeOverlayRemote("hidden changed", "Hidden changed", "Changed",
                         "https://search.example/search?q=hidden-changed")});
  remote_state_.callbacks[0].Run({remote_two, remote_one, remote_two});
  EXPECT_EQ(rebuild_count, view->GetResultViewRebuildCountForTesting());
  EXPECT_EQ(stable_titles, RenderedTitles(view));
  EXPECT_EQ(3, model->selected_index());

  remote_state_.callbacks[0].Run({remote_one});
  ASSERT_EQ(0, model->selected_index());
  EXPECT_TRUE(RowIsSelected(OverlayRowAt(view, 0)));

  StartViewQuery(view, u"new query");
  const uint32_t new_generation = model->search_generation_for_testing();
  ASSERT_NE(weather_generation, new_generation);
  model->AcceptLocalResultsForTesting(new_generation, MakeOverlayLocals(2));
  const int post_query_rebuild_count =
      view->GetResultViewRebuildCountForTesting();
  const std::vector<std::string> post_query_titles = RenderedTitles(view);
  const int post_query_selection = model->selected_index();
  remote_state_.callbacks[0].Run({remote_two, remote_one});
  EXPECT_EQ(post_query_rebuild_count,
            view->GetResultViewRebuildCountForTesting());
  EXPECT_EQ(post_query_titles, RenderedTitles(view));
  EXPECT_EQ(post_query_selection, model->selected_index());
}

TEST_F(MahoCommandOverlayRemoteViewTest,
       EnterNavigatesExactEncodedRemoteDestination) {
  AddTab(browser(), GURL("about:blank"));
  auto* view = CreateBrowserView();
  // Same as above: stop the continuous selector animation so mock-time
  // draining terminates.
  view->GetActionSelectorForTesting()->SetVisible(false);
  view->GetModelForTesting()->SetRemoteSearchSourceForTesting(
      std::make_unique<OverlayFakeRemoteSource>(&remote_state_));
  StartViewQuery(view, u"weather seoul");

  MahoCommandModel* model = view->GetModelForTesting();
  const uint32_t generation = model->search_generation_for_testing();
  model->AcceptLocalResultsForTesting(generation, MakeOverlayLocals(1));
  model->StartRemoteSearchForTesting(generation, "weather seoul");
  ASSERT_EQ(1u, remote_state_.callbacks.size());
  const GURL destination(
      "https://search.example/search?q=weather%20seoul&src=remote");
  remote_state_.callbacks[0].Run({MakeOverlayRemote(
      "weather seoul forecast", "Weather Seoul forecast", "Remote result",
      destination.spec())});
  ASSERT_EQ(2u, model->results().size());
  ASSERT_TRUE(SendKey(view, ui::VKEY_DOWN));
  ASSERT_EQ("remote-search:weather seoul forecast",
            model->results()[model->selected_index()].key);

  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  EXPECT_TRUE(SendKey(view, ui::VKEY_RETURN));
  ASSERT_NE(nullptr, contents->GetController().GetPendingEntry());
  EXPECT_EQ(destination,
            contents->GetController().GetPendingEntry()->GetURL());
  CommitPendingLoad(&contents->GetController());

  EXPECT_EQ(destination, contents->GetLastCommittedURL());
  EXPECT_EQ(1, dismiss_count());
}

TEST(MahoCommandOverlayInlineCompletionTest, CompletesHostFromHistory) {
  CommandSuggestion history;
  history.type = CommandSuggestionType::kHistory;
  history.key = "history:gh";
  history.execution_payload = "https://www.github.com/maho/repo";
  const std::vector<CommandSuggestion> results = {history};

  EXPECT_EQ("github.com", MahoCommandOverlayView::ComputeInlineCompletion(
                              "git", results));
  EXPECT_EQ("www.github.com", MahoCommandOverlayView::ComputeInlineCompletion(
                                  "www.g", results));
  EXPECT_EQ("github.com/maho/repo",
            MahoCommandOverlayView::ComputeInlineCompletion("github.com/m",
                                                            results));
  // Case of the typed prefix is preserved.
  EXPECT_EQ("GIThub.com", MahoCommandOverlayView::ComputeInlineCompletion(
                              "GIT", results));
  EXPECT_FALSE(
      MahoCommandOverlayView::ComputeInlineCompletion("hub", results));
  EXPECT_FALSE(MahoCommandOverlayView::ComputeInlineCompletion("github.com",
                                                               results));
  EXPECT_FALSE(
      MahoCommandOverlayView::ComputeInlineCompletion("git hub", results));

  CommandSuggestion search;
  search.type = CommandSuggestionType::kSearch;
  search.execution_payload = "https://www.google.com/search?q=git";
  EXPECT_FALSE(MahoCommandOverlayView::ComputeInlineCompletion("goo",
                                                               {search}));
}

TEST(MahoCommandOverlayInlineCompletionTest, SelectionTextForSuggestions) {
  CommandSuggestion search;
  search.type = CommandSuggestionType::kSearch;
  search.key = "search:cats";
  search.execution_payload = "https://www.google.com/search?q=cats";
  EXPECT_EQ("cats", MahoCommandOverlayView::TextfieldTextForSuggestion(search));

  CommandSuggestion tab;
  tab.type = CommandSuggestionType::kTab;
  tab.execution_payload = "https://example.com/";
  EXPECT_EQ("https://example.com/",
            MahoCommandOverlayView::TextfieldTextForSuggestion(tab));

  CommandSuggestion action;
  action.type = CommandSuggestionType::kAction;
  action.execution_payload = "new_tab";
  EXPECT_EQ("", MahoCommandOverlayView::TextfieldTextForSuggestion(action));
}

}  // namespace
}  // namespace maho
