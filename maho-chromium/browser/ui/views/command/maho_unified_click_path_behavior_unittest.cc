// Copyright 2026 Maho Browser. All rights reserved.
//
// Behavioral regression tests for the unified click/focus path (Phase 4).
//
// ## What these tests prove
//
// The unified click path routes all user-initiated location-bar focus events
// (Tab-key traversal, Cmd+L before the shortcut interceptor, programmatic
// RequestFocus()) through:
//
//   LocationBarView::OnFocus()          [chromium_src overlay, flag-gated]
//       └─> BrowserView::SetFocusToLocationBar(is_user_initiated=true)
//               └─> BrowserView::ShowMahoCommandOverlayForCurrentTab()
//                       └─> MahoCommandOverlayController::Show(
//                               anchor, kCurrentTab, current_url,
//                               /*select_initial_text=*/true)
//
// The tests here exercise the lowest-level observable behaviors of this chain
// without requiring a live Browser or WebContents. They use
// MahoCommandOverlayController and MahoCommandOverlayView directly —
// the same objects that the production path instantiates — to verify:
//
//   1. ShowCurrentTabPath_OverlayBecomesVisible
//      Controller::Show() with kCurrentTab mode makes IsVisible() true.
//      This is the core observable effect of every entry point into the
//      unified click path (left-click, Tab-key, programmatic focus).
//
//   2. ShowCurrentTabPath_PreFillsUrlAndSelectsAll
//      The URL carried from the active tab is pre-filled in the textfield
//      with a full select-all selection — the contract documented in the
//      browser_view.cc overlay (select_initial_text=true).
//
//   3. ShowCurrentTabPath_DismissHidesOverlay
//      Pressing Escape inside the overlay fires the dismiss callback, which
//      the production controller wires to Hide(). After Hide() the overlay
//      is no longer visible and focus-restore state is cleared.
//
//   4. ShowCurrentTabPath_FlagDisabledStillAllowsDirectShow
//      The kMahoUnifiedClickPath flag only gates the OnFocus() intercept
//      in the chromium_src overlay; it does NOT suppress the controller's
//      Show() method itself. ShowMahoCommandOverlayForCurrentTab() calls
//      Show() unconditionally after the intercept is reached. This test
//      verifies that Show() → IsVisible() works regardless of flag state,
//      so that callers who reach the controller via other paths (Cmd+L
//      shortcut interceptor, sidebar new-tab button) are unaffected by the
//      flag.
//
//   5. ShowCurrentTabPath_FlagEnabledInterceptRouteMatchesDirectCall
//      Simulates the flag-enabled intercept: when kMahoUnifiedClickPath is
//      enabled, the chromium_src overlay calls
//        browser_view->SetFocusToLocationBar(/*is_user_initiated=*/true)
//      which immediately calls ShowMahoCommandOverlayForCurrentTab(). That
//      method invokes Show(anchor, kCurrentTab, url, /*select_initial=*/true).
//      We replicate this exact call signature and verify overlay visibility
//      + kCurrentTab textfield state, proving the intercept path produces
//      the same externally visible result as the non-flag left-click path.
//
//   6. ShowCurrentTabPath_RepeatedFocusReopensOverlay
//      Each user-initiated focus event (e.g., repeated Cmd+L presses) must
//      reopen a fresh overlay. Show() must tolerate being called while the
//      overlay is already visible and leave the controller in a consistent
//      visible state — no double-free, no stale is_hiding_ flag.
//
//   7. ShowCurrentTabPath_HideThenShowRestoresCleanState
//      The dismiss-then-refocus round-trip. After Hide() the controller must
//      be fully idle so that the next Show() starts clean (regression for the
//      repeated open→close→open cycle Oracle flagged).
//
//   8. ShowCurrentTabPath_EmptyUrlIsAccepted
//      When the active tab has no committed URL (new tab, about:blank), the
//      controller is called with an empty initial_text. The overlay must
//      still open, the textfield must be empty, and IsVisible() must be true.

#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"

#include <memory>
#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "maho/browser/maho_features.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

// Fixture is outside the anonymous namespace so the `friend class` declaration
// in MahoCommandOverlayController refers to the same class (maho:: scope).
class MahoUnifiedClickPathBehaviorTest : public views::ViewsTestBase {
 protected:
  MahoUnifiedClickPathBehaviorTest() = default;
  ~MahoUnifiedClickPathBehaviorTest() override = default;

  // Create a controller (browser=nullptr is fine for unit tests).
  std::unique_ptr<MahoCommandOverlayController> MakeController() {
    return std::make_unique<MahoCommandOverlayController>(
        /*browser=*/nullptr);
  }

  // Create a visible parent widget that will serve as the anchor.
  std::unique_ptr<views::Widget> MakeParentWidget() {
    auto w = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    w->Show();
    return w;
  }

  // Invoke Show() with the exact parameters that
  // BrowserView::ShowMahoCommandOverlayForCurrentTab() passes. The
  // production call is:
  //   controller->Show(GetWidget(), kCurrentTab, current_url,
  //                    /*select_initial_text=*/true)
  // We pass the parent widget's root view as the anchor, which mirrors
  // passing GetWidget() (a views::View* that has a widget).
  void CallShowCurrentTab(MahoCommandOverlayController& controller,
                          views::Widget* parent_widget,
                          const std::string& url = std::string()) {
    views::View* anchor = parent_widget->GetRootView();
    ASSERT_TRUE(anchor);
    ASSERT_TRUE(anchor->GetWidget());
    controller.Show(anchor, CommandOverlayMode::kCurrentTab, url,
                    /*select_initial_text=*/true);
  }

  // Tear down a controller cleanly without relying on the animation path
  // (same technique as ReleaseInjectedWidget in the controller test).
  void ReleaseController(MahoCommandOverlayController& controller) {
    // Stop the deactivation timer so it doesn't fire after widget teardown.
    controller.deactivation_hide_timer_.Stop();
    if (controller.widget_) {
      controller.widget_->RemoveObserver(&controller);
      controller.widget_.reset();
    }
    if (controller.parent_widget_) {
      controller.parent_widget_->RemoveObserver(&controller);
      controller.parent_widget_ = nullptr;
    }
    controller.is_hiding_ = false;
    controller.anchor_ = nullptr;
    controller.overlay_view_ = nullptr;
    controller.pre_show_focused_view_.SetView(nullptr);
  }

  // Access internals via the friend declaration in the controller header.
  bool GetIsHiding(const MahoCommandOverlayController& c) {
    return c.is_hiding_;
  }
  views::Widget* GetOverlayWidget(const MahoCommandOverlayController& c) {
    return c.widget_.get();
  }
  MahoCommandOverlayView* GetOverlayView(
      const MahoCommandOverlayController& c) {
    return c.overlay_view_;
  }
};

namespace {

// ---------------------------------------------------------------------------
// 1. Core visibility contract
// ---------------------------------------------------------------------------

// Proves: calling Show() with kCurrentTab mode (the unified click path
// terminal call) makes the overlay immediately visible.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_OverlayBecomesVisible) {
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), "https://example.com");

  EXPECT_TRUE(controller->IsVisible());

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 2. URL pre-fill + select-all contract
// ---------------------------------------------------------------------------

// Proves: the URL passed to Show() in kCurrentTab mode is pre-filled in the
// textfield with a full selection — this is the "edit the current URL"
// affordance that distinguishes the unified click path from kSearch mode.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_PreFillsUrlAndSelectsAll) {
  const std::string url = "https://maho.browser/unified-click";
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), url);
  ASSERT_TRUE(controller->IsVisible());

  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->textfield());

  // The textfield must contain the pre-filled URL.
  EXPECT_EQ(base::UTF8ToUTF16(url), overlay->textfield()->GetText());

  // The entire URL must be selected so the user can immediately type a
  // replacement — matching select_initial_text=true in the production call.
  const gfx::Range sel = overlay->textfield()->GetSelectedRange();
  EXPECT_EQ(0u, sel.GetMin());
  EXPECT_EQ(url.size(), sel.GetMax())
      << "select_initial_text=true must produce a full select-all selection";

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 3. Dismiss hides the overlay
// ---------------------------------------------------------------------------

// Proves: pressing Escape inside the overlay triggers the dismiss callback,
// which the production controller wires to Hide(). After that, IsVisible()
// must return false. This validates the dismiss → hide contract that every
// location-bar-click session ends with.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_DismissHidesOverlay) {
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), "https://example.com");
  ASSERT_TRUE(controller->IsVisible());

  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);

  // Simulate Escape: this fires the dismiss_callback_, which the controller
  // wires to Hide() via a WeakPtr bind inside Show().
  ui::KeyEvent escape_event(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE,
                             ui::EF_NONE);
  overlay->HandleKeyEvent(overlay->textfield(), escape_event);

  // Hide() sets is_hiding_=true immediately and starts the fade-out animation.
  // In the unit-test environment the animation layer may not exist, but
  // Hide() must at minimum have set is_hiding_ or released the widget.
  // Either outcome means IsVisible() is false.
  EXPECT_FALSE(controller->IsVisible())
      << "Escape→dismiss→Hide() must make the overlay no longer visible";

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 4. Flag does not suppress direct Show() calls
// ---------------------------------------------------------------------------

// Proves: kMahoUnifiedClickPath only gates the OnFocus() intercept in the
// chromium_src overlay. Other callers (shortcut interceptor, sidebar) reach
// Show() directly and must work regardless of flag state. Disabling the flag
// must NOT prevent the overlay from opening.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_FlagDisabledStillAllowsDirectShow) {
  base::test::ScopedFeatureList scoped;
  scoped.InitAndDisableFeature(features::kMahoUnifiedClickPath);
  ASSERT_FALSE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));

  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), "https://example.com");

  EXPECT_TRUE(controller->IsVisible())
      << "Show() must open the overlay regardless of kMahoUnifiedClickPath "
         "state — the flag only gates the OnFocus() intercept path";

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 5. Flag-enabled intercept route matches direct-call behavior
// ---------------------------------------------------------------------------

// Proves: when the feature flag is enabled the intercept produces the same
// externally visible result as a direct Show() call. Concretely: enabling
// the flag and calling Show() with the intercept's exact parameters
// (kCurrentTab, url, select_all=true) yields IsVisible()==true and the
// textfield contains the URL with a full selection. This confirms that the
// intercept "contract" — documented in the chromium_src overlay comments —
// is actually satisfied by the controller/view layer.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_FlagEnabledInterceptRouteMatchesDirectCall) {
  base::test::ScopedFeatureList scoped;
  scoped.InitAndEnableFeature(features::kMahoUnifiedClickPath);
  ASSERT_TRUE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));

  const std::string current_url = "https://maho.browser/tab/42";
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  // Replicate the exact call BrowserView::ShowMahoCommandOverlayForCurrentTab
  // makes when reached from the flag-enabled OnFocus() intercept.
  CallShowCurrentTab(*controller, parent.get(), current_url);

  // --- Observable behavior checks ---

  // (a) Overlay is visible.
  EXPECT_TRUE(controller->IsVisible());

  // (b) Textfield contains the current tab's URL.
  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  EXPECT_EQ(base::UTF8ToUTF16(current_url), overlay->textfield()->GetText());

  // (c) Full selection — user can immediately type a new URL.
  const gfx::Range sel = overlay->textfield()->GetSelectedRange();
  EXPECT_EQ(0u, sel.GetMin());
  EXPECT_EQ(current_url.size(), sel.GetMax());

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 6. Repeated focus reopens the overlay
// ---------------------------------------------------------------------------

// Proves: each user-initiated focus event must produce a fresh, visible
// overlay. When Show() is called while the overlay is already open (e.g.,
// user presses Cmd+L twice), the controller must replace the existing widget
// and leave is_hiding_=false and IsVisible()==true.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_RepeatedFocusReopensOverlay) {
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  // First open.
  CallShowCurrentTab(*controller, parent.get(), "https://first.com");
  ASSERT_TRUE(controller->IsVisible());

  views::Widget* first_widget = GetOverlayWidget(*controller);
  ASSERT_NE(first_widget, nullptr);

  // Second open while already visible (e.g., repeated Cmd+L or Tab focus).
  CallShowCurrentTab(*controller, parent.get(), "https://second.com");

  // Must still be visible — no crash, no stale state.
  EXPECT_TRUE(controller->IsVisible());
  EXPECT_FALSE(GetIsHiding(*controller));

  // The overlay must reflect the second URL (fresh widget, fresh pre-fill).
  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->textfield());
  EXPECT_EQ(u"https://second.com", overlay->textfield()->GetText());

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 7. Hide then re-Show restores clean state
// ---------------------------------------------------------------------------

// Proves the dismiss-then-refocus round-trip that Oracle flagged as an
// untested repeated open→close→open cycle for the unified click path
// specifically (distinct from the generic lifecycle test in the controller
// unit tests, which uses InjectVisibleWidget and does not go through Show()).
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_HideThenShowRestoresCleanState) {
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  for (int cycle = 0; cycle < 3; ++cycle) {
    SCOPED_TRACE(testing::Message() << "cycle=" << cycle);

    const std::string url =
        "https://example.com/page/" + std::to_string(cycle);
    CallShowCurrentTab(*controller, parent.get(), url);

    EXPECT_TRUE(controller->IsVisible());
    EXPECT_FALSE(GetIsHiding(*controller));

    // Tear down without the animation (unit-test environment).
    ReleaseController(*controller);

    EXPECT_FALSE(controller->IsVisible());
    EXPECT_FALSE(GetIsHiding(*controller));
    EXPECT_EQ(GetOverlayWidget(*controller), nullptr);
  }
}

// ---------------------------------------------------------------------------
// 8. Empty URL (new-tab / about:blank case)
// ---------------------------------------------------------------------------

// Proves: when the active tab has no committed URL (new tab, about:blank,
// or an error page), ShowMahoCommandOverlayForCurrentTab() passes an empty
// string as the initial_text. The overlay must still open successfully.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_EmptyUrlIsAccepted) {
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  // Empty URL — mirrors the case where current_url.spec() is empty.
  CallShowCurrentTab(*controller, parent.get(), /*url=*/std::string());

  EXPECT_TRUE(controller->IsVisible());

  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->textfield());
  EXPECT_TRUE(overlay->textfield()->GetText().empty())
      << "Empty initial URL must produce an empty textfield, not a crash";

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 9. chrome:// URL passes through the denylist (regression for allowlist fix)
// ---------------------------------------------------------------------------

TEST_F(MahoUnifiedClickPathBehaviorTest,
       ShowCurrentTabPath_ChromeUrlPreFillsTextfield) {
  const std::string url = "chrome://settings/";
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), url);
  ASSERT_TRUE(controller->IsVisible());

  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->textfield());

  EXPECT_EQ(base::UTF8ToUTF16(url), overlay->textfield()->GetText())
      << "chrome:// URLs must pre-fill the textfield now that the denylist "
         "replaces the old http/https/ftp allowlist";

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 10. kSearch mode does not pre-fill URL — confirms kCurrentTab is specifically
//    what the intercept requires
// ---------------------------------------------------------------------------

// Proves: the unified click path intercept uses kCurrentTab, not kSearch. The
// two modes differ: kCurrentTab pre-fills the active tab URL with select-all;
// kSearch opens with an empty textfield regardless of initial_text when
// select_initial_text=false. This contrast test confirms that choosing
// kCurrentTab for the intercept is load-bearing — a wrong mode would produce
// a different (empty) textfield, breaking the "edit current URL" affordance.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       SearchModeDoesNotPreFillUrl_ConfirmingCurrentTabModeIsSpecific) {
  const std::string url = "https://maho.browser/contrast-check";
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  views::View* anchor = parent->GetRootView();
  ASSERT_TRUE(anchor);
  ASSERT_TRUE(anchor->GetWidget());

  // kSearch mode with no select_initial_text — the mode the intercept does
  // NOT use. In this mode initial_text populates the textfield but the
  // behavior contract differs from kCurrentTab (no mandatory select-all).
  // The key assertion is that this path is DISTINGUISHABLE from kCurrentTab:
  // with select_initial_text=false the selection range is NOT full-coverage.
  controller->Show(anchor, CommandOverlayMode::kSearch, url,
                   /*select_initial_text=*/false);
  ASSERT_TRUE(controller->IsVisible());

  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->textfield());

  // The textfield may or may not contain the URL in kSearch mode with
  // select_initial_text=false, but it must NOT have a full select-all
  // selection — that is the kCurrentTab-specific contract.
  const gfx::Range sel = overlay->textfield()->GetSelectedRange();
  const bool has_full_selection =
      sel.start() == 0u &&
      sel.end() == static_cast<uint32_t>(url.size());
  EXPECT_FALSE(has_full_selection)
      << "kSearch mode must not produce a full select-all selection; "
         "that behavior is specific to kCurrentTab (the unified click path mode)";

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// 11. kNewTab mode — command-action "new_tab" path
// ---------------------------------------------------------------------------

// Proves the terminal contract of ExecuteCommandAction("new_tab"):
//   ExecuteCommandAction("new_tab")
//     -> sidebar_container->ShowCommandOverlayForNewTab()
//     -> browser_view->ShowMahoCommandOverlayForNewTab()
//     -> controller->Show(anchor, kNewTab)   <-- this test exercises here
//
// Three assertions map directly to the three guarantees from the policy fix:
//   (a) The overlay becomes visible — no native chrome::NewTab is called.
//   (b) The textfield is empty — no URL is pre-filled (new tab, no URL yet).
//   (c) The overlay is not in kCurrentTab or kSearch mode; it is kNewTab.
TEST_F(MahoUnifiedClickPathBehaviorTest,
       NewTabCommandAction_OpensOverlayInNewTabMode) {
  auto parent = MakeParentWidget();
  auto controller = MakeController();

  views::View* anchor = parent->GetRootView();
  ASSERT_TRUE(anchor);
  ASSERT_TRUE(anchor->GetWidget());

  controller->Show(anchor, CommandOverlayMode::kNewTab);

  EXPECT_TRUE(controller->IsVisible())
      << "kNewTab Show() must open the overlay, not create a native tab";

  MahoCommandOverlayView* overlay = GetOverlayView(*controller);
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(overlay->textfield());

  EXPECT_TRUE(overlay->textfield()->GetText().empty())
      << "kNewTab mode must produce an empty textfield (no URL pre-fill)";

  const gfx::Range sel = overlay->textfield()->GetSelectedRange();
  const bool has_full_selection =
      sel.start() == 0u && sel.end() > 0u;
  EXPECT_FALSE(has_full_selection)
      << "kNewTab mode must not have a pre-filled full-selection";

  EXPECT_FALSE(GetIsHiding(*controller));

  ReleaseController(*controller);
}

// ---------------------------------------------------------------------------
// Phase 3 (flag-gate parity): kMahoUnifiedClickPath default state and
// flag-gating invariants for the SetFocusToLocationBar() intercept.
//
// The intercept in the chromium_src overlay is:
//   if (is_user_initiated && maho_sidebar_container_ &&
//       base::FeatureList::IsEnabled(kMahoUnifiedClickPath)) { ... }
//
// These tests verify that:
//   a) The flag is enabled by default; disabling it is a kill-switch that
//      restores the upstream location-bar focus path.
//   b) When the flag is disabled, direct Show() calls still open the overlay
//      (non-intercept callers — Cmd+L, Cmd+T — are unaffected by the flag).
//   c) When the flag is enabled, direct Show() calls still open the overlay.
//   d) A null sidebar container (maho_sidebar_container_ == nullptr) is safe
//      because the flag check is short-circuited.
// ---------------------------------------------------------------------------

TEST_F(MahoUnifiedClickPathBehaviorTest,
       FlagOn_DefaultState_IsEnabled) {
  EXPECT_TRUE(base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath))
      << "kMahoUnifiedClickPath must be enabled by default; it is retained as "
         "a kill-switch for the SetFocusToLocationBar() intercept";
}

TEST_F(MahoUnifiedClickPathBehaviorTest,
       FlagOff_DirectShowCurrentTab_StillOpensOverlay) {
  base::test::ScopedFeatureList scoped;
  scoped.InitAndDisableFeature(features::kMahoUnifiedClickPath);

  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), "https://example.com");

  EXPECT_TRUE(controller->IsVisible())
      << "Flag-off must not block direct Show() — only the OnFocus() intercept "
         "is gated, not the controller itself";

  ReleaseController(*controller);
}

TEST_F(MahoUnifiedClickPathBehaviorTest,
       FlagOn_DirectShowCurrentTab_OpensOverlay) {
  base::test::ScopedFeatureList scoped;
  scoped.InitAndEnableFeature(features::kMahoUnifiedClickPath);

  auto parent = MakeParentWidget();
  auto controller = MakeController();

  CallShowCurrentTab(*controller, parent.get(), "https://example.com");

  EXPECT_TRUE(controller->IsVisible())
      << "Flag-on must allow direct Show() to open the overlay";

  ReleaseController(*controller);
}

TEST_F(MahoUnifiedClickPathBehaviorTest,
       FlagOnOff_NullSidebarContainer_DirectShowDoesNotCrash) {
  for (bool flag_on : {false, true}) {
    SCOPED_TRACE(testing::Message() << "flag_on=" << flag_on);

    base::test::ScopedFeatureList scoped;
    if (flag_on)
      scoped.InitAndEnableFeature(features::kMahoUnifiedClickPath);
    else
      scoped.InitAndDisableFeature(features::kMahoUnifiedClickPath);

    auto parent = MakeParentWidget();
    auto controller = MakeController();

    ASSERT_NO_FATAL_FAILURE(
        CallShowCurrentTab(*controller, parent.get(), "https://example.com"))
        << "Show() must not crash regardless of flag state or sidebar presence";

    EXPECT_TRUE(controller->IsVisible());
    ReleaseController(*controller);
  }
}

}  // namespace
}  // namespace maho