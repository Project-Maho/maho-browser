// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

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

TEST_F(MahoCommandOverlayViewTest, TypeAiThenTabEntersAiModeInRegularContext) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  ASSERT_TRUE(view->AiIngressAllowedForTesting());

  view->textfield()->SetText(u"ai");
  EXPECT_TRUE(SendKey(view, ui::VKEY_TAB));

  EXPECT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting());
  EXPECT_TRUE(view->IsModeChipVisibleForTesting());
}

TEST_F(MahoCommandOverlayViewTest, EscapeInAiModeExitsModeWithoutDismissing) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  view->textfield()->SetText(u"ai");
  SendKey(view, ui::VKEY_TAB);
  ASSERT_EQ(CommandOverlayMode::kAI, view->CurrentModeForTesting());

  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));
  EXPECT_FALSE(WasDismissed());
  EXPECT_EQ(CommandOverlayMode::kSearch, view->CurrentModeForTesting());
}

// task-10-command-unit: AI ingress must be denied before any mutation in exact
// primary Incognito. Typing "ai" + Tab must NOT enter AI mode and must not show
// the AI chip; the ingress predicate must report denial.
TEST_F(MahoCommandOverlayViewTest, AiIngressDeniedBeforeMutation) {
  auto* view = CreateView(CommandOverlayMode::kSearch);
  view->SetContextClassForTesting(MahoPrivateContextClass::kPrimaryIncognito);
  ASSERT_FALSE(view->AiIngressAllowedForTesting());

  view->textfield()->SetText(u"ai");
  EXPECT_TRUE(SendKey(view, ui::VKEY_TAB));

  EXPECT_EQ(CommandOverlayMode::kSearch, view->CurrentModeForTesting())
      << "primary Incognito must never enter AI mode via ai+Tab";
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
  EXPECT_FALSE(WasDismissed());
}

}  // namespace
}  // namespace maho
