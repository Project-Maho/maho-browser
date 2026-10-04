// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_handler.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_mail_command_catalog.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoCommandActionHandlerTest : public BrowserWithTestWindowTest {
 public:
  MahoCommandActionHandlerTest() = default;
  ~MahoCommandActionHandlerTest() override = default;

  void TearDown() override {
    MahoAiIngressCoordinator::RemoveFromBrowser(browser());
    MahoNotificationOverlay::RemoveFromBrowser(browser());
    BrowserWithTestWindowTest::TearDown();
  }
};

TEST_F(MahoCommandActionHandlerTest, ForgedActionIdsAreDenied) {
  for (const std::string& id : MahoCommandModel::PrivateActionAllowlist()) {
    EXPECT_TRUE(IsPrimaryIncognitoAllowedActionId(id)) << id;
  }

  for (const char* forged :
        {"new_tab", "new_incognito", "open_history", "open_bookmarks",
         "next_space", "select_tab_1", "add_split_view", "remove_split_view",
         "ai_panel", "settings", "maho_mini", "", "toggle_sidebarX",
         "CLOSE_TAB", "print", "action:close_tab"}) {
    EXPECT_FALSE(IsPrimaryIncognitoAllowedActionId(forged)) << forged;
  }
}

TEST_F(MahoCommandActionHandlerTest, PrivateSplitAndSidebarAreLocal) {
  EXPECT_FALSE(MahoSplitViewController::ShouldDispatchPersistentSplitEvent(
      MahoSplitPersistence::kPrivateLocal));
  EXPECT_FALSE(ShouldPersistSidebarToggle(
      MahoPrivateContextClass::kPrimaryIncognito));
  EXPECT_FALSE(ShouldPersistSidebarToggle(MahoPrivateContextClass::kOtherOtr));
  EXPECT_FALSE(ShouldPersistSidebarToggle(MahoPrivateContextClass::kGuest));
}

TEST_F(MahoCommandActionHandlerTest, RegularSplitAndSearchEnginePersist) {
  EXPECT_TRUE(MahoSplitViewController::ShouldDispatchPersistentSplitEvent(
      MahoSplitPersistence::kPersistent));
  EXPECT_TRUE(ShouldPersistSidebarToggle(MahoPrivateContextClass::kRegular));
  EXPECT_FALSE(MahoCommandModel::PrivateSearchUsesDefaultProviderOnly(
      MahoPrivateContextClass::kRegular));
  EXPECT_TRUE(MahoCommandModel::ShouldReadSavedSourcesForContext(
      MahoPrivateContextClass::kRegular));
}

TEST_F(MahoCommandActionHandlerTest, CopyUrlCommandCreatesLinkCopiedToast) {
  // copy_url only acts on an active tab; the harness starts with none.
  AddTab(browser(), GURL("https://example.com/"));
  EXPECT_EQ(nullptr, MahoNotificationOverlay::FromBrowser(browser()));

  ExecuteCommandAction(browser(), "copy_url");

  EXPECT_NE(nullptr, MahoNotificationOverlay::FromBrowser(browser()))
      << "Cmd+Shift+C must route its copy_url action through the link-copied "
         "toast surface";
}

TEST_F(MahoCommandActionHandlerTest, SplitActiveTracksActiveTabMembership) {
  auto* model = browser()->GetTabStripModel();
  MahoSplitViewController controller(browser());
  ASSERT_EQ(TabStripModel::kNoTab, model->active_index());
  EXPECT_FALSE(controller.IsSplitActive());
  ExecuteCommandAction(browser(), "next_split_view");
  EXPECT_EQ(TabStripModel::kNoTab, model->active_index());

  AddTab(browser(), GURL("about:blank"));
  EXPECT_FALSE(controller.IsSplitActive());
  AddTab(browser(), GURL("about:blank"));
  const int partner = model->active_index() == 0 ? 1 : 0;
  const auto split_id = model->AddToNewSplit(
      {partner},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide,
                                     0.5),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  EXPECT_TRUE(controller.IsSplitActive());
  model->RemoveSplit(split_id);
  EXPECT_FALSE(controller.IsSplitActive());
}

TEST_F(MahoCommandActionHandlerTest, MailAvailabilityRequiresEveryGate) {
  EXPECT_TRUE(CanExecuteMailCommandForTesting(
      /*feature_enabled=*/true, /*helper_available=*/true,
      /*platform_supported=*/true));
  EXPECT_FALSE(CanExecuteMailCommandForTesting(
      /*feature_enabled=*/false, /*helper_available=*/true,
      /*platform_supported=*/true));
  EXPECT_FALSE(CanExecuteMailCommandForTesting(
      /*feature_enabled=*/true, /*helper_available=*/false,
      /*platform_supported=*/true));
  EXPECT_FALSE(CanExecuteMailCommandForTesting(
      /*feature_enabled=*/true, /*helper_available=*/true,
      /*platform_supported=*/false));
}

TEST_F(MahoCommandActionHandlerTest, MailCommandCatalogHasRequiredDeepLinks) {
  constexpr std::array expected = {
      MahoMailCommand{"open_mail", ""},
      MahoMailCommand{"compose_mail", "?view=compose"},
      MahoMailCommand{"search_mail", "?view=search"},
      MahoMailCommand{"mail_inbox", "?folder=inbox"},
      MahoMailCommand{"mail_sent", "?folder=sent"},
      MahoMailCommand{"mail_drafts", "?folder=drafts"},
      MahoMailCommand{"mail_starred", "?folder=starred"},
  };
  static_assert(expected.size() == kMahoMailCommands.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(kMahoMailCommands[i].action_id, expected[i].action_id);
    EXPECT_EQ(kMahoMailCommands[i].path, expected[i].path);
  }
}

TEST_F(MahoCommandActionHandlerTest,
       UnavailableMailCommandDoesNotOpenATab) {
  const int initial_tab_count = browser()->GetTabStripModel()->count();

  ExecuteCommandAction(browser(), "open_mail");

  EXPECT_EQ(initial_tab_count, browser()->GetTabStripModel()->count());
}

// A single dispatch from a regular window carries the full command-palette
// Ask Maho contract: exactly one delivery, command-palette source, submit
// requested, Assistant interaction mode, no attached context, no target
// session, and a non-empty request id. The dispatch itself must never carry
// kCurrentPage — panel opening / context attachment live in the overlay, not
// in this dispatch-only entry point.
TEST_F(MahoCommandActionHandlerTest,
       OnQuerySubmittedRegularInputDispatchesOnceWithAssistantNoneContext) {
  // Given: a regular browser with a live coordinator observing deliveries.
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
  auto sub = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](std::vector<maho_ai::mojom::AskMahoDispatch>* out,
             const maho_ai::mojom::AskMahoDispatch& d, uint64_t /*delivery_id*/,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            out->push_back(d);
            std::move(accept_cb).Run("dummy_session_id");
          },
          &dispatches));

  // When: a regular, non-empty query is submitted from the command palette.
  bool accepted = MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "hello", PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular);

  // Then: it is accepted and dispatched exactly once with the full contract.
  EXPECT_TRUE(accepted);
  ASSERT_EQ(1u, dispatches.size());
  EXPECT_EQ("hello", dispatches[0].query);
  EXPECT_EQ(maho_ai::mojom::AskMahoSource::kCommandPalette,
            dispatches[0].source);
  EXPECT_TRUE(dispatches[0].submit);
  EXPECT_EQ(maho_ai::mojom::InteractionMode::kAssistant, dispatches[0].mode);
  EXPECT_EQ(maho_ai::mojom::AskMahoContextIntent::kNone,
            dispatches[0].context_intent);
  EXPECT_FALSE(dispatches[0].target_session_id.has_value());
  EXPECT_FALSE(dispatches[0].request_id.empty());
}

// The attach-browser-context pref governs overlay behavior, not this dispatch.
// With the pref ON the dispatched context intent must still be kNone.
TEST_F(MahoCommandActionHandlerTest,
       OnQuerySubmittedAttachContextPrefTrueStillNoneContext) {
  // Given: the attach-context pref is enabled on a regular profile.
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::ai_prefs::kAttachBrowserContext, true);
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
  auto sub = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](std::vector<maho_ai::mojom::AskMahoDispatch>* out,
             const maho_ai::mojom::AskMahoDispatch& d, uint64_t /*delivery_id*/,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            out->push_back(d);
            std::move(accept_cb).Run("dummy_session_id");
          },
          &dispatches));

  // When: a regular, non-empty query is submitted.
  bool accepted = MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "hello", PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular);

  // Then: it dispatches once, and the intent is still unconditionally kNone.
  EXPECT_TRUE(accepted);
  ASSERT_EQ(1u, dispatches.size());
  EXPECT_EQ(maho_ai::mojom::AskMahoContextIntent::kNone,
            dispatches[0].context_intent);
}

// With the attach-context pref OFF the dispatched context intent is kNone too,
// proving the pref does not flip the dispatch-level intent either way.
TEST_F(MahoCommandActionHandlerTest,
       OnQuerySubmittedAttachContextPrefFalseStillNoneContext) {
  // Given: the attach-context pref is disabled on a regular profile.
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::ai_prefs::kAttachBrowserContext, false);
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
  auto sub = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](std::vector<maho_ai::mojom::AskMahoDispatch>* out,
             const maho_ai::mojom::AskMahoDispatch& d, uint64_t /*delivery_id*/,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            out->push_back(d);
            std::move(accept_cb).Run("dummy_session_id");
          },
          &dispatches));

  // When: a regular, non-empty query is submitted.
  bool accepted = MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "hello", PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular);

  // Then: it dispatches once, and the intent is still unconditionally kNone.
  EXPECT_TRUE(accepted);
  ASSERT_EQ(1u, dispatches.size());
  EXPECT_EQ(maho_ai::mojom::AskMahoContextIntent::kNone,
            dispatches[0].context_intent);
}

// Whitespace-only input carries no query; it must be authoritatively rejected
// with no delivery reaching the coordinator.
TEST_F(MahoCommandActionHandlerTest,
       OnQuerySubmittedWhitespaceOnlyInputIsRejected) {
  // Given: a regular browser with a live coordinator observing deliveries.
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
  auto sub = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](std::vector<maho_ai::mojom::AskMahoDispatch>* out,
             const maho_ai::mojom::AskMahoDispatch& d, uint64_t /*delivery_id*/,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            out->push_back(d);
            std::move(accept_cb).Run("dummy_session_id");
          },
          &dispatches));

  // When: a whitespace-only query is submitted.
  bool accepted = MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "   \t ", PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular);

  // Then: it is rejected and nothing is dispatched.
  EXPECT_FALSE(accepted);
  EXPECT_TRUE(dispatches.empty());
}

// Off-the-record (incognito) windows must never dispatch Ask Maho; the call is
// authoritatively rejected regardless of the reported context class.
TEST_F(MahoCommandActionHandlerTest, OnQuerySubmittedIncognitoIsRejected) {
  // Given: an incognito browser with a live coordinator observing deliveries.
  Profile* otr_profile =
      profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  std::unique_ptr<Browser> incognito_browser =
      CreateBrowser(otr_profile, Browser::TYPE_NORMAL, false);
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(incognito_browser.get());
  std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
  auto sub = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](std::vector<maho_ai::mojom::AskMahoDispatch>* out,
             const maho_ai::mojom::AskMahoDispatch& d, uint64_t /*delivery_id*/,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            out->push_back(d);
            std::move(accept_cb).Run("dummy_session_id");
          },
          &dispatches));

  // When: a non-empty query is submitted from the incognito window.
  bool accepted = MahoCommandActionHandler::OnQuerySubmitted(
      incognito_browser.get(), "hello", PaletteAction::kAskMaho,
      MahoPrivateContextClass::kRegular);

  // Then: it is rejected and nothing is dispatched.
  EXPECT_FALSE(accepted);
  EXPECT_TRUE(dispatches.empty());

  // Reset subscription before coordinator destruction to prevent a UAF inside
  // its destructor.
  sub = {};
  MahoAiIngressCoordinator::RemoveFromBrowser(incognito_browser.get());
}

// A non-regular context class (e.g. primary Incognito) must be authoritatively
// rejected with no delivery reaching the coordinator.
TEST_F(MahoCommandActionHandlerTest,
       OnQuerySubmittedNonRegularContextIsRejected) {
  // Given: a regular browser with a live coordinator observing deliveries.
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
  auto sub = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](std::vector<maho_ai::mojom::AskMahoDispatch>* out,
             const maho_ai::mojom::AskMahoDispatch& d, uint64_t /*delivery_id*/,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            out->push_back(d);
            std::move(accept_cb).Run("dummy_session_id");
          },
          &dispatches));

  // When: a non-empty query is submitted with a non-regular context class.
  bool accepted = MahoCommandActionHandler::OnQuerySubmitted(
      browser(), "hello", PaletteAction::kAskMaho,
      MahoPrivateContextClass::kPrimaryIncognito);

  // Then: it is rejected and nothing is dispatched.
  EXPECT_FALSE(accepted);
  EXPECT_TRUE(dispatches.empty());
}

}  // namespace
}  // namespace maho
