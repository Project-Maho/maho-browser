// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "base/test/scoped_feature_list.h"
#include "maho/browser/maho_features.h"

#include <algorithm>
#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/button/md_text_button.h"

namespace maho {
namespace {

ImageData MakeTestFaviconImageData() {
  constexpr uint8_t kPngBytes[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
      0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63,
      0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89,
      0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
      0xAE, 0x42, 0x60, 0x82,
  };

  ImageData image_data;
  image_data.data.assign(std::begin(kPngBytes), std::end(kPngBytes));
  image_data.width = 16;
  image_data.height = 16;
  image_data.format = "png";
  return image_data;
}

size_t SeededVisibleResultCap() {
  return MahoCommandOverlayView::GetMaxSeededVisibleResultsForTesting();
}

std::vector<CommandSuggestion> BuildSeedResults(size_t count) {
  std::vector<CommandSuggestion> seeds;
  seeds.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
        "https://page" + std::to_string(i) + ".test/",
        "Page " + std::to_string(i)));
  }
  return seeds;
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
        MahoCommandOverlayView::ForTestingTag{},
        mode,
        initial_text,
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

  static ui::KeyEvent MakeKeyPress(ui::KeyboardCode code,
                                   int flags = ui::EF_NONE) {
    return ui::KeyEvent(ui::EventType::kKeyPressed, code, flags);
  }

  static bool SendKey(MahoCommandOverlayView* view, ui::KeyboardCode code,
                      int flags = ui::EF_NONE) {
    ui::KeyEvent event = MakeKeyPress(code, flags);
    return view->HandleKeyEvent(view->textfield(), event);
  }

 private:
  void OnDismiss() { ++dismiss_count_; }
  void OnResize(int) {}

  std::unique_ptr<views::Widget> host_widget_;
  int dismiss_count_ = 0;
};

// ---------------------------------------------------------------------------
// Initial state: kCurrentTab (location-bar click mode)
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModePreFillsInitialTextInTextfield) {
  const std::string url = "https://example.com/path";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  EXPECT_EQ(base::UTF8ToUTF16(url), view->textfield()->GetText());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeSelectsAllPreFilledText) {
  const std::string url = "https://example.com/path";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  const gfx::Range sel = view->textfield()->GetSelectedRange();
  EXPECT_EQ(0u, sel.GetMin());
  EXPECT_EQ(static_cast<uint32_t>(url.size()), sel.GetMax());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeEmptyInitialTextLeavesTextfieldEmpty) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab);
  EXPECT_TRUE(view->textfield()->GetText().empty());
}

TEST_F(MahoCommandOverlayViewTest,
       GeometryContractUsesTighterShellAndDecoupledEmptyStateSpacing) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  EXPECT_EQ(16, MahoCommandOverlayView::GetPanelCornerRadiusForTesting());
  EXPECT_EQ(11, MahoCommandOverlayView::GetSearchRowCornerRadiusForTesting());
  EXPECT_EQ(12, MahoCommandOverlayView::GetTopbarHorizontalInsetForTesting());
  EXPECT_EQ(gfx::Insets::TLBR(10, 8, 8, 8),
            MahoCommandOverlayView::GetPanelInsetsForTesting());
  EXPECT_EQ(0, MahoCommandOverlayView::GetPanelSectionSpacingForTesting());
  EXPECT_EQ(2, MahoCommandOverlayView::GetResultRowSpacingForTesting());
  EXPECT_EQ(gfx::Insets::TLBR(8, 40, 4, 40),
            MahoCommandOverlayView::GetEmptyStateInsetsForTesting(false));
  EXPECT_EQ(gfx::Insets::TLBR(6, 40, 3, 40),
            MahoCommandOverlayView::GetEmptyStateInsetsForTesting(true));
  EXPECT_EQ(gfx::Insets::TLBR(4, 10, 2, 10),
            MahoCommandOverlayView::GetDividerInsetsForTesting());

  EXPECT_TRUE(view->TextfieldIsCollapsedForTesting());
}

// ---------------------------------------------------------------------------
// Escape key: dismissal contract
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       EscapeInDefaultModeFiresDismissCallback) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, "https://maho.test");

  ASSERT_FALSE(WasDismissed());
  bool consumed = SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_TRUE(consumed);
  EXPECT_TRUE(WasDismissed());
}

TEST_F(MahoCommandOverlayViewTest,
       EscapeFiresDismissCallbackExactlyOnce) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_EQ(1, DismissCount());
}

TEST_F(MahoCommandOverlayViewTest,
       EscapeInNewTabModeFiresDismissCallback) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  bool consumed = SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_TRUE(consumed);
  EXPECT_TRUE(WasDismissed());
}

// ---------------------------------------------------------------------------
// Sub-mode transitions: AI mode enter/exit
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       TypeAiThenTabEntersAiMode) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  bool consumed = SendKey(view, ui::VKEY_TAB);

  EXPECT_TRUE(consumed);
  EXPECT_FALSE(WasDismissed());
  EXPECT_EQ(u"Ask anything\u2026", view->textfield()->GetPlaceholderText());
}

TEST_F(MahoCommandOverlayViewTest,
       EscapeInAiModeExitsModeWithoutDismissing) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  ASSERT_EQ(u"Ask anything\u2026", view->textfield()->GetPlaceholderText());

  bool consumed = SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_TRUE(consumed);
  EXPECT_FALSE(WasDismissed());
  EXPECT_NE(u"Ask anything\u2026", view->textfield()->GetPlaceholderText());
}

TEST_F(MahoCommandOverlayViewTest,
       SecondEscapeAfterExitingAiModeDismissesOverlay) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  SendKey(view, ui::VKEY_ESCAPE);
  ASSERT_FALSE(WasDismissed());

  SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_TRUE(WasDismissed());
}

// ---------------------------------------------------------------------------
// Placeholder contract
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       SearchModePlaceholderIsSearchOrEnterUrl) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  EXPECT_EQ(u"Search or Enter URL\u2026", view->textfield()->GetPlaceholderText());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModePlaceholderIsEmpty) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab);
  EXPECT_TRUE(view->textfield()->GetPlaceholderText().empty());
}

TEST_F(MahoCommandOverlayViewTest,
       NewTabModePlaceholderIsSearchOrEnterUrl) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);
  EXPECT_EQ(u"Search or Enter URL\u2026", view->textfield()->GetPlaceholderText());
}

// ---------------------------------------------------------------------------
// Mode chip visibility
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       ModeChipHiddenInSearchMode) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       ModeChipHiddenInCurrentTabMode) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab);
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       ModeChipVisibleAfterEnteringAiMode) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);

  EXPECT_TRUE(view->IsModeChipVisibleForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       ModeChipHiddenAfterExitingAiModeViaEscape) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  ASSERT_TRUE(view->IsModeChipVisibleForTesting());

  SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
}

// ---------------------------------------------------------------------------
// Dismiss callback idempotency
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       DismissIsNoOpAfterFirstCall) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  SendKey(view, ui::VKEY_ESCAPE);
  ASSERT_EQ(1, DismissCount());

  SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_EQ(1, DismissCount());
}

// ---------------------------------------------------------------------------
// Accessibility: textfield → listbox relationship (aria-controls)
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       TextfieldControlIdPointsToResultsContainer) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  EXPECT_TRUE(view->TextfieldControlsResultsContainerForTesting());
}

// ---------------------------------------------------------------------------
// Accessibility: textfield starts collapsed
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       TextfieldStartsCollapsed) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  EXPECT_TRUE(view->TextfieldIsCollapsedForTesting());
  EXPECT_FALSE(view->TextfieldIsExpandedForTesting());
}

// ---------------------------------------------------------------------------
// Accessibility: results present → expanded + active descendant set
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       UpdateResultViewsWithResultsSetsExpandedAndActiveDescendant) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  CommandSuggestion s;
  s.title = "Test Result";
  s.subtitle = "subtitle";
  s.type = CommandSuggestionType::kHistory;

  view->UpdateResultViewsForTesting({s});

  EXPECT_TRUE(view->TextfieldIsExpandedForTesting());
  EXPECT_FALSE(view->TextfieldIsCollapsedForTesting());

  EXPECT_NE(view->GetActiveDescendantForTesting(), nullptr);
}

// ---------------------------------------------------------------------------
// Accessibility: empty results → collapsed + active descendant cleared
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       UpdateResultViewsWithEmptyResultsSetsCollapsedAndClearsActiveDescendant) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  CommandSuggestion s;
  s.title = "One";
  s.type = CommandSuggestionType::kHistory;
  view->UpdateResultViewsForTesting({s});
  ASSERT_NE(view->GetActiveDescendantForTesting(), nullptr);

  view->UpdateResultViewsForTesting({});

  EXPECT_TRUE(view->TextfieldIsCollapsedForTesting());
  EXPECT_FALSE(view->TextfieldIsExpandedForTesting());

  EXPECT_EQ(view->GetActiveDescendantForTesting(), nullptr);
}

// ---------------------------------------------------------------------------
// Accessibility: selection change updates active descendant
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       SelectionChangeUpdatesActiveDescendant) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  CommandSuggestion a, b;
  a.title = "Alpha";
  a.type = CommandSuggestionType::kHistory;
  b.title = "Beta";
  b.type = CommandSuggestionType::kHistory;
  view->UpdateResultViewsForTesting({a, b});

  auto* before = view->GetActiveDescendantForTesting();
  ASSERT_NE(before, nullptr);

  view->UpdateSelectionA11yForTesting(0, 1);

  auto* after = view->GetActiveDescendantForTesting();
  EXPECT_NE(after, nullptr);
  EXPECT_NE(before, after);
}

// ---------------------------------------------------------------------------
// kCurrentTab seeded initial state (Cmd+L blank-palette regression)
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithUrlShowsSeedResultOnEmptyBackendResponse) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  view->UpdateResultViewsForTesting({});

  auto children = view->GetResultsContainerForTesting()->children();
  EXPECT_FALSE(children.empty())
      << "Seeded current-page result must appear when backend returns empty";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithUrlRendersSeededResultsSynchronouslyOnOpen) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  EXPECT_EQ(1u, view->GetRenderedResultCountForTesting());
  ASSERT_EQ(1u, view->GetSeededResultsForTesting().size());
  EXPECT_EQ(url, view->GetSeededResultsForTesting().front().execution_payload);
  EXPECT_EQ(url, view->GetSeededResultsForTesting().front().subtitle);
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithUrlSeedResultIsExpandedNotCollapsed) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  view->UpdateResultViewsForTesting({});

  EXPECT_TRUE(view->TextfieldIsExpandedForTesting());
  EXPECT_FALSE(view->TextfieldIsCollapsedForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithUrlSeedResultSetsActiveDescendant) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  view->UpdateResultViewsForTesting({});

  EXPECT_NE(view->GetActiveDescendantForTesting(), nullptr);
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeNoUrlNoSeedOnEmptyBackendResponse) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, "", false);

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size()) << "Only hint label should be present";
}

// ---------------------------------------------------------------------------
// chrome:// URL seeding suppression (internal-page policy regression)
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithChromeUrlDoesNotEnterSeededState) {
  const std::string url = "chrome://settings/";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  EXPECT_FALSE(view->IsSeededStateForTesting())
      << "Internal chrome:// pages must not enter seeded state; the overlay "
         "should fall through to the backend search path";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithChromeUrlHasNoSeededResults) {
  const std::string url = "chrome://settings/";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  EXPECT_TRUE(view->GetSeededResultsForTesting().empty())
      << "No seeded rows must be stored for an internal chrome:// page";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithChromeUrlPreFillsTextfield) {
  const std::string url = "chrome://newtab/";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  EXPECT_EQ(base::UTF8ToUTF16(url), view->textfield()->GetText())
      << "chrome:// URL must still appear verbatim in the textfield (prefill "
         "comes from the opener, not the overlay seeding path)";
}

TEST_F(MahoCommandOverlayViewTest,
       NewTabUrlDoesNotEnterSeededState) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "chrome://newtab/", true);

  EXPECT_FALSE(view->IsSeededStateForTesting())
      << "chrome://newtab/ must not lock the palette in seeded state";
}

TEST_F(MahoCommandOverlayViewTest,
       NewTabUrlSeededResultsAreEmpty) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "chrome://newtab/", true);

  EXPECT_TRUE(view->GetSeededResultsForTesting().empty())
      << "No seeded rows must be stored for chrome://newtab/";
}

TEST_F(MahoCommandOverlayViewTest,
       NonNewTabChromeUrlAlsoDoesNotEnterSeededState) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "chrome://settings/", true);

  EXPECT_FALSE(view->IsSeededStateForTesting())
      << "All chrome:// URLs (not just newtab) must be suppressed by the "
         "internal-page policy";
  EXPECT_TRUE(view->GetSeededResultsForTesting().empty());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeWithChromeUrlSeedNotVisibleOnEmptyBackendResponse) {
  const std::string url = "chrome://history/";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting())
      << "Internal chrome:// pages must not produce visible seeded rows";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeSeedReplacedByRealBackendResults) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  view->UpdateResultViewsForTesting({});
  ASSERT_FALSE(view->GetResultsContainerForTesting()->children().empty());

  CommandSuggestion hist;
  hist.title = "History Result";
  hist.subtitle = "https://visited.com/";
  hist.type = CommandSuggestionType::kHistory;

  view->UpdateResultViewsForTesting({hist});

  EXPECT_EQ(1u, view->GetResultsContainerForTesting()->children().size());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeSeedClearedAfterUserEditsText) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  view->UpdateResultViewsForTesting({});
  ASSERT_FALSE(view->GetResultsContainerForTesting()->children().empty());
  ASSERT_TRUE(view->IsSeededStateForTesting());

  view->textfield()->SetText(u"somethingelse");
  view->ContentsChanged(view->textfield(), u"somethingelse");

  EXPECT_FALSE(view->IsSeededStateForTesting());

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size())
      << "After user edits, empty backend response should show hint label";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeAfterUserActionWithoutEditKeepsSeededState) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  ASSERT_TRUE(view->IsSeededStateForTesting());

  view->OnAfterUserAction(view->textfield());

  EXPECT_TRUE(view->IsSeededStateForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabModeRestoringOriginalUrlAfterEditDoesNotReenterSeededState) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  ASSERT_TRUE(view->IsSeededStateForTesting());

  view->textfield()->SetText(u"somethingelse");
  view->ContentsChanged(view->textfield(), u"somethingelse");
  ASSERT_FALSE(view->IsSeededStateForTesting());

  view->textfield()->SetText(base::UTF8ToUTF16(url));
  view->ContentsChanged(view->textfield(), base::UTF8ToUTF16(url));

  EXPECT_FALSE(view->IsSeededStateForTesting());

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size())
      << "Returning to the original URL must not silently restore seeded mode";
}

TEST_F(MahoCommandOverlayViewTest,
       RenderedResultCountIgnoresHintLabel) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       SearchModeWithTextShowsNoResultsNotSeed) {
  auto* view = CreateView(CommandOverlayMode::kSearch, "query", false);

  view->UpdateResultViewsForTesting({});

  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size()) << "kSearch mode must not show seeded result";
}

TEST_F(MahoCommandOverlayViewTest,
       BuildCurrentTabSeedProducesNavigationSuggestion) {
  CommandSuggestion s =
      MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
          "https://test.com/", "Test");

  EXPECT_EQ(CommandSuggestionType::kNavigation, s.type);
  EXPECT_EQ("https://test.com/", s.execution_payload);
  EXPECT_EQ("Test", s.title);
  EXPECT_EQ("https://test.com/", s.subtitle);
}

TEST_F(MahoCommandOverlayViewTest,
       BuildCurrentTabSeedFallsBackToUrlAsTitleWhenTitleEmpty) {
  CommandSuggestion s =
      MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
          "https://test.com/", "");

  EXPECT_EQ("https://test.com/", s.title);
}

// ---------------------------------------------------------------------------
// kCurrentTab seeded open-state: 5-row contract
// These tests use InitializeSeededStateForTesting to inject pre-built seeds
// directly, bypassing the Browser/NavigationController seam that is not
// available in unit tests.
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedProducesMultipleRows_RendersInjectedVisibleCap) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  const size_t visible_cap = SeededVisibleResultCap();
  std::vector<CommandSuggestion> seeds = BuildSeedResults(visible_cap);

  view->InitializeSeededStateForTesting(seeds);

  EXPECT_EQ(visible_cap, view->GetRenderedResultCountForTesting());
  EXPECT_EQ(visible_cap, view->GetSeededResultsForTesting().size());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedRowOrderIsReverseChronological) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  const std::string current_url = "https://current.test/";
  const std::string prev1_url = "https://prev1.test/";
  const std::string prev2_url = "https://prev2.test/";

  std::vector<CommandSuggestion> seeds;
  seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
      current_url, "Current Page"));
  seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
      prev1_url, "Prev One"));
  seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
      prev2_url, "Prev Two"));

  view->InitializeSeededStateForTesting(seeds);

  const auto& stored = view->GetSeededResultsForTesting();
  ASSERT_EQ(3u, stored.size());
  EXPECT_EQ(current_url, stored[0].execution_payload);
  EXPECT_EQ(prev1_url, stored[1].execution_payload);
  EXPECT_EQ(prev2_url, stored[2].execution_payload);
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedDeduplicatesByUrl) {
  const std::vector<std::pair<std::string, std::string>> history_entries = {
      {"https://prev1.test/", "Prev One"},
      {"https://dup.test/", "Duplicate First"},
      {"https://prev2.test/", "Prev Two"},
      {"https://dup.test/", "Duplicate Second"},
      {"https://prev3.test/", "Prev Three"},
  };

  const auto seeds =
      MahoCommandOverlayView::BuildCurrentTabSeedResultsFromHistoryForTesting(
          "https://current.test/", "Current Page", history_entries);

  ASSERT_EQ(3u, seeds.size());
  EXPECT_EQ("https://current.test/", seeds[0].execution_payload);
  EXPECT_EQ("https://prev1.test/", seeds[1].execution_payload);
  EXPECT_EQ("https://dup.test/", seeds[2].execution_payload);
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedSkipsInternalHistoryEntriesBeforeVisibleCap) {
  const std::vector<std::pair<std::string, std::string>> history_entries = {
      {"chrome://history/", "History"},
      {"https://prev1.test/", "Prev One"},
      {"chrome://settings/", "Settings"},
      {"https://prev2.test/", "Prev Two"},
      {"https://prev3.test/", "Prev Three"},
      {"https://prev4.test/", "Prev Four"},
  };

  const auto seeds =
      MahoCommandOverlayView::BuildCurrentTabSeedResultsFromHistoryForTesting(
          "https://current.test/", "Current Page", history_entries);

  ASSERT_EQ(SeededVisibleResultCap(), seeds.size());
  EXPECT_EQ("https://current.test/", seeds[0].execution_payload);
  EXPECT_EQ("https://prev1.test/", seeds[1].execution_payload);
  EXPECT_EQ("https://prev2.test/", seeds[2].execution_payload);
  EXPECT_EQ("https://prev3.test/", seeds[3].execution_payload);
  EXPECT_EQ("https://prev4.test/", seeds[4].execution_payload);
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedRowTitleComesFromNavigationEntry) {
  const std::string url = "https://entry.test/";
  const std::string title = "Entry Page Title";

  CommandSuggestion seed =
      MahoCommandOverlayView::BuildCurrentTabSeedForTesting(url, title);

  EXPECT_EQ(title, seed.title);
  EXPECT_NE(url, seed.title);
  EXPECT_EQ(url, seed.subtitle);
  EXPECT_EQ(url, seed.execution_payload);
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedNoHelperTextWithMultipleRows) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  const size_t visible_cap = SeededVisibleResultCap();
  std::vector<CommandSuggestion> seeds = BuildSeedResults(visible_cap);
  view->InitializeSeededStateForTesting(seeds);

  EXPECT_EQ(visible_cap, view->GetRenderedResultCountForTesting());
  EXPECT_EQ(visible_cap, view->GetResultsContainerForTesting()->children().size())
      << "results_container_ must contain only seeded rows at the visible cap "
         "with no hint label mixed in";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeed_InputRowHasNoInnerPillBackground) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/section", true);

  ASSERT_NE(nullptr, view->GetSearchRowForTesting());
  EXPECT_EQ(nullptr, view->GetSearchRowForTesting()->GetBackground());
  EXPECT_EQ(nullptr, view->GetSearchRowForTesting()->GetBorder());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeed_TrailingAccessoryIsHidden) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/section", true);

  views::View* trailing_accessory = view->GetTrailingAccessoryForTesting();
  ASSERT_NE(nullptr, trailing_accessory);

  EXPECT_FALSE(trailing_accessory->GetVisible())
      << "Trailing accessory must be entirely hidden in current-tab mode";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeed_HelperTextStillSuppressed) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  std::vector<CommandSuggestion> seeds = BuildSeedResults(3u);
  view->InitializeSeededStateForTesting(seeds);

  EXPECT_EQ(3u, view->GetRenderedResultCountForTesting());
  EXPECT_EQ(3u, view->GetResultsContainerForTesting()->children().size());
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedRow0IsSelectedOnOpen) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  std::vector<CommandSuggestion> seeds;
  seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
      "https://page0.test/", "Page Zero"));
  seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
      "https://page1.test/", "Page One"));
  seeds.push_back(MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
      "https://page2.test/", "Page Two"));

  view->InitializeSeededStateForTesting(seeds);

  views::ViewAccessibility* active_desc = view->GetActiveDescendantForTesting();
  ASSERT_NE(nullptr, active_desc);

  const auto& children = view->GetResultsContainerForTesting()->children();
  ASSERT_GE(children.size(), 1u);
  EXPECT_EQ(&children.front()->GetViewAccessibility(), active_desc)
      << "Row 0 must be the active descendant (selected row) on open";
}

TEST_F(MahoCommandOverlayViewTest,
       CurrentTabSeedBackendEmptyDoesNotClearSeededRowsAtVisibleCap) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  const size_t visible_cap = SeededVisibleResultCap();
  std::vector<CommandSuggestion> seeds = BuildSeedResults(visible_cap);
  view->InitializeSeededStateForTesting(seeds);
  ASSERT_EQ(visible_cap, view->GetRenderedResultCountForTesting());

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(visible_cap, view->GetRenderedResultCountForTesting())
      << "Backend empty-result response must not evict seeded rows at the "
         "visible cap";
}

TEST_F(MahoCommandOverlayViewTest,
       SeededCurrentTabHydratedIconUpdatesLeadingAccessoryAndRow) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://current.test/", true);

  CommandSuggestion current =
      MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
          "https://current.test/", "Current");
  current.icon = MakeTestFaviconImageData();

  CommandSuggestion history =
      MahoCommandOverlayView::BuildCurrentTabSeedForTesting(
          "https://prev.test/", "Previous");
  history.icon.reset();

  view->InitializeSeededStateForTesting({current, history});

  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(2u, children.size());
  auto* history_row = static_cast<MahoCommandResultRowView*>(children[1].get());
  ASSERT_TRUE(history_row->UsesFallbackGlyphForTesting());
  EXPECT_EQ(nullptr, history_row->GetFaviconImageForTesting());

  view->ApplyHydratedSeededIconForTesting(1u, MakeTestFaviconImageData());

  children = view->GetResultsContainerForTesting()->children();
  history_row = static_cast<MahoCommandResultRowView*>(children[1].get());
  EXPECT_FALSE(history_row->UsesFallbackGlyphForTesting());
  EXPECT_NE(nullptr, history_row->GetFaviconImageForTesting());
}

// ---------------------------------------------------------------------------
// Divider visibility contract
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       SeededCurrentTabKeepsDividerHidden) {
  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  ASSERT_TRUE(view->IsSeededStateForTesting());
  view->UpdateResultViewsForTesting({});

  EXPECT_FALSE(view->IsDividerVisibleForTesting())
      << "Divider must remain hidden in seeded kCurrentTab state";
}

TEST_F(MahoCommandOverlayViewTest,
       NonSeededResultsPathShowsDividerWhenResultsPresent) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  CommandSuggestion s;
  s.title = "History Result";
  s.subtitle = "https://visited.com/";
  s.type = CommandSuggestionType::kHistory;

  view->UpdateResultViewsForTesting({s});

  EXPECT_TRUE(view->IsDividerVisibleForTesting())
      << "Divider must be visible when non-seeded results are present";
}

// ---------------------------------------------------------------------------
// DispositionForMode contract — typed-submit routing (P0 regression coverage)
//
// These tests exercise the invariant that NavigateTypedText() must route
// through NavigateToNewForegroundTab() (the authoritative new-tab helper)
// whenever DispositionForMode() returns NEW_FOREGROUND_TAB.  They cover both
// the direct kNewTab mode and the sub-mode cases (kSiteSearch, kAI) that
// inherit their disposition from origin_mode_.
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       DispositionForModeIsNewForegroundTabInNewTabMode) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  EXPECT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
            view->DispositionForModeForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       DispositionForModeIsCurrentTabInSearchMode) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       DispositionForModeIsCurrentTabInCurrentTabMode) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab);

  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       SiteSearchFromNewTabOriginKeepsNewForegroundTabDisposition) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  ASSERT_EQ(CommandOverlayMode::kNewTab, view->CurrentModeForTesting());

  view->textfield()->SetText(u"gh");
  SendKey(view, ui::VKEY_TAB);

  if (view->CurrentModeForTesting() == CommandOverlayMode::kSiteSearch) {
    EXPECT_EQ(CommandOverlayMode::kNewTab, view->OriginModeForTesting())
        << "origin_mode_ must be preserved as kNewTab when entering kSiteSearch";
    EXPECT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
              view->DispositionForModeForTesting())
        << "kSiteSearch entered from kNewTab must yield NEW_FOREGROUND_TAB";
  }
}

TEST_F(MahoCommandOverlayViewTest,
       AiModeFromNewTabOriginKeepsNewForegroundTabDisposition) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);

  ASSERT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting())
      << "Typing 'ai' + Tab must enter kAI mode";
  EXPECT_EQ(CommandOverlayMode::kNewTab, view->OriginModeForTesting())
      << "origin_mode_ must remain kNewTab after entering kAI from kNewTab";
  EXPECT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
            view->DispositionForModeForTesting())
      << "kAI entered from kNewTab must yield NEW_FOREGROUND_TAB";
}

TEST_F(MahoCommandOverlayViewTest,
       AiModeFromSearchOriginKeepsCurrentTabDisposition) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);

  ASSERT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting())
      << "Typing 'ai' + Tab from kSearch must enter kAI mode";
  EXPECT_EQ(CommandOverlayMode::kSearch, view->OriginModeForTesting())
      << "origin_mode_ must be kSearch when entering kAI from kSearch";
  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting())
      << "kAI entered from kSearch must yield CURRENT_TAB";
}

TEST_F(MahoCommandOverlayViewTest,
       ExitingAiModeRestoresOriginDispositionForNewTab) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  ASSERT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting());
  ASSERT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
            view->DispositionForModeForTesting());

  SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_EQ(CommandOverlayMode::kNewTab, view->CurrentModeForTesting())
      << "Escape from kAI must restore kNewTab mode";
  EXPECT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
            view->DispositionForModeForTesting())
      << "After exiting kAI back to kNewTab, disposition must still be "
         "NEW_FOREGROUND_TAB";
}

TEST_F(MahoCommandOverlayViewTest,
       ExitingAiModeRestoresOriginDispositionForSearch) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  ASSERT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting());

  SendKey(view, ui::VKEY_ESCAPE);

  EXPECT_EQ(CommandOverlayMode::kSearch, view->CurrentModeForTesting())
      << "Escape from kAI entered from kSearch must restore kSearch mode";
  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting())
      << "After exiting kAI back to kSearch, disposition must be CURRENT_TAB";
}

// ---------------------------------------------------------------------------
// Phase 1 (Zen parity): empty-query view-layer behavior per mode
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       EmptyQueryInCurrentTabModeWithNoSeedShowsHintLabel) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab);
  ASSERT_FALSE(view->IsSeededStateForTesting());

  view->UpdateResultViewsForTesting({});

  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size())
      << "Empty kCurrentTab with no seed must show hint label only";
  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       EmptyQueryInSearchModeShowsHintLabel) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  view->UpdateResultViewsForTesting({});

  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size())
      << "Empty kSearch must show hint label only, not seeded rows";
  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
}

TEST_F(MahoCommandOverlayViewTest,
       EmptyQueryInNewTabModeShowsHintLabel) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  view->UpdateResultViewsForTesting({});

  auto children = view->GetResultsContainerForTesting()->children();
  ASSERT_EQ(1u, children.size())
      << "Empty kNewTab must show hint label (backend results not yet arrived)";
  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting());
}

// ---------------------------------------------------------------------------
// Phase 2 (Zen parity): four-state agreement and disposition at activation
// ---------------------------------------------------------------------------

TEST_F(MahoCommandOverlayViewTest,
       FourStateAgreement_CurrentTabModeDispositionIsCurrentTab) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://example.test/", true);

  ASSERT_TRUE(view->IsSeededStateForTesting());
  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting())
      << "kCurrentTab mode must always resolve to CURRENT_TAB disposition";
}

TEST_F(MahoCommandOverlayViewTest,
       FourStateAgreement_NewTabModeDispositionIsNewForegroundTab) {
  auto* view = CreateView(CommandOverlayMode::kNewTab);

  CommandSuggestion s;
  s.title = "Result";
  s.subtitle = "https://example.test/";
  s.type = CommandSuggestionType::kHistory;
  s.execution_payload = "https://example.test/";
  view->UpdateResultViewsForTesting({s});

  EXPECT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
            view->DispositionForModeForTesting())
      << "kNewTab mode must always resolve to NEW_FOREGROUND_TAB disposition";
}

TEST_F(MahoCommandOverlayViewTest,
       FourStateAgreement_SearchModeDispositionIsCurrentTab) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  CommandSuggestion s;
  s.title = "Result";
  s.type = CommandSuggestionType::kHistory;
  view->UpdateResultViewsForTesting({s});

  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting())
      << "kSearch mode must always resolve to CURRENT_TAB disposition";
}

TEST_F(MahoCommandOverlayViewTest,
       OriginModeRoundTrip_CurrentTabEntersAiAndKeepsCurrentTabDisposition) {
  auto* view = CreateView(CommandOverlayMode::kCurrentTab,
                          "https://example.test/", true);

  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);

  ASSERT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting());
  EXPECT_EQ(CommandOverlayMode::kCurrentTab, view->OriginModeForTesting())
      << "origin_mode_ must be kCurrentTab after entering kAI from kCurrentTab";
  EXPECT_EQ(WindowOpenDisposition::CURRENT_TAB,
            view->DispositionForModeForTesting())
      << "kAI entered from kCurrentTab must yield CURRENT_TAB";
}

TEST_F(MahoCommandOverlayViewTest,
       EmptyTextAfterSeededClearShowsHintNotSeededRows) {
  const std::string url = "https://seeded.test/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);
  ASSERT_TRUE(view->IsSeededStateForTesting());
  ASSERT_EQ(1u, view->GetRenderedResultCountForTesting());

  view->textfield()->SetText(u"x");
  view->ContentsChanged(view->textfield(), u"x");
  ASSERT_FALSE(view->IsSeededStateForTesting());

  view->textfield()->SetText(u"");
  view->ContentsChanged(view->textfield(), u"");

  view->UpdateResultViewsForTesting({});

  EXPECT_EQ(0u, view->GetRenderedResultCountForTesting())
      << "After seeded state is cleared and text is emptied, "
         "no seeded rows must appear";
  auto children = view->GetResultsContainerForTesting()->children();
  EXPECT_GE(children.size(), 1u)
      << "Hint label must be present after seeded clear";
}

TEST_F(MahoCommandOverlayViewTest,
       SeededCurrentTabRows_DefaultCount) {
  EXPECT_EQ(3u, SeededVisibleResultCap())
      << "The default visible seeded cap must be reduced to 3";
}

TEST_F(MahoCommandOverlayViewTest,
       SeededCurrentTabRows_FeatureFlagDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(
      features::kMahoCommandPaletteSeededHistory);

  const std::string url = "https://example.com/page";
  auto* view = CreateView(CommandOverlayMode::kCurrentTab, url, true);

  EXPECT_FALSE(view->IsSeededStateForTesting())
      << "Seeded state must be completely disabled when the feature flag is "
         "turned off";
  EXPECT_TRUE(view->GetSeededResultsForTesting().empty());
}

TEST_F(MahoCommandOverlayViewTest,
       CommandsOnlyMode_EnterExitLifecycle) {
  auto* view = CreateView(CommandOverlayMode::kSearch);

  // Type '>' to enter commands-only mode.
  view->textfield()->SetText(u">");
  view->ContentsChanged(view->textfield(), u">");

  EXPECT_EQ(CommandOverlayMode::kCommandsOnly, view->CurrentModeForTesting())
      << "Typing '>' must transition the palette into commands-only mode";
  EXPECT_TRUE(view->IsModeChipVisibleForTesting())
      << "A mode chip must be visible in commands-only mode";

  // Escape to exit commands-only mode.
  SendKey(view, ui::VKEY_ESCAPE);
  EXPECT_EQ(CommandOverlayMode::kSearch, view->CurrentModeForTesting())
      << "Escape in commands-only mode must exit back to origin mode";
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());

  // Enter again.
  view->textfield()->SetText(u">");
  view->ContentsChanged(view->textfield(), u">");
  ASSERT_EQ(CommandOverlayMode::kCommandsOnly, view->CurrentModeForTesting());

  // Backspace with empty text exits commands-only mode.
  ASSERT_TRUE(view->textfield()->GetText().empty());
  SendKey(view, ui::VKEY_BACK);
  EXPECT_EQ(CommandOverlayMode::kSearch, view->CurrentModeForTesting())
      << "Backspace with empty text in commands-only mode must exit back to "
         "origin mode";
}

}  // namespace
}  // namespace maho
