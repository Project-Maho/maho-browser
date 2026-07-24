// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/accessibility/view_accessibility.h"
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

}  // namespace
}  // namespace maho
