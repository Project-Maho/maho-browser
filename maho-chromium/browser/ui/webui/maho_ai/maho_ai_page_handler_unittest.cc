// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/maho_ai_popup_lifetime_tracker.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_pending_surface.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_runtime_event_persistence.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gtest/include/gtest/gtest.h"

class MahoAIPageHandlerTestPeer {
 public:
  static size_t RetainedReplayBytes(MahoAIPageHandler* handler) {
    return handler->replay_budget_.bytes;
  }

  static void EmitRuntimeEvent(MahoAIPageHandler* handler,
                               const std::string& session_id,
                               std::optional<std::string> request_id,
                               MahoAiRuntimeEventType type,
                               const std::string& text = std::string()) {
    MahoAiRuntimeEvent event;
    event.type = type;
    event.text = text;
    handler->OnRuntimeEventForSession(session_id, std::move(request_id),
                                      std::move(event));
  }

  static void EmitRuntimeEventWithPayload(MahoAIPageHandler* handler,
                                          const std::string& session_id,
                                          MahoAiRuntimeEventType type,
                                          base::DictValue payload) {
    MahoAiRuntimeEvent event;
    event.type = type;
    event.payload = std::move(payload);
    handler->OnRuntimeEventForSession(session_id, std::nullopt,
                                      std::move(event));
  }

  static void EmitToolResult(MahoAIPageHandler* handler,
                             const std::string& session_id,
                             const std::string& tool_output) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kToolResult;
    event.payload.Set("call_id", "call-credential-redaction");
    event.payload.Set("success", true);
    event.payload.Set("output", tool_output);
    handler->OnRuntimeEventForSession(session_id, std::nullopt,
                                      std::move(event));
  }

  static void EmitCredentialFailure(MahoAIPageHandler* handler,
                                    const std::string& session_id,
                                    MahoAiRuntimeErrorCode error_code,
                                    const std::string& raw_diagnostic) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kError;
    event.text = raw_diagnostic;
    event.runtime_error_code = error_code;
    handler->OnRuntimeEventForSession(session_id, std::nullopt,
                                      std::move(event));
  }

  static void EmitApprovalRequest(MahoAIPageHandler* handler,
                                  const std::string& session_id,
                                  const std::string& raw_arguments) {
    MahoAiRuntimeEvent event;
    event.type = MahoAiRuntimeEventType::kApprovalRequest;
    event.text = "browser_click";
    event.payload.Set("approval_id", "approval-typed");
    event.payload.Set("description", "Approve browser_click");
    event.payload.Set("related_tool_name", "browser_click");
    event.payload.Set("related_tool_arguments", raw_arguments);
    event.payload.Set("approval_policy", "prompt");
    event.payload.Set("sensitivity", "sensitive");
    event.payload.Set("approval_state", "pending");
    event.payload.Set("page_derived_justification", true);
    handler->OnRuntimeEventForSession(session_id, std::nullopt,
                                      std::move(event));
  }

  static void SetActiveIngressRequest(MahoAIPageHandler* handler,
                                      const std::string& request_id,
                                      uint64_t delivery_id) {
    handler->active_ingress_request_id_ = request_id;
    handler->active_ingress_delivery_id_ = delivery_id;
  }

  static const std::string& active_ingress_request_id(
      const MahoAIPageHandler* handler) {
    return handler->active_ingress_request_id_;
  }

  static uint64_t active_ingress_delivery_id(const MahoAIPageHandler* handler) {
    return handler->active_ingress_delivery_id_;
  }

  static maho_ai::mojom::SessionStatus session_status(
      const MahoAIPageHandler* handler) {
    return handler->session_status_;
  }

  static void ResetPage(MahoAIPageHandler* handler) { handler->page_.reset(); }

  static void RevokePermission(MahoAIPageHandler* handler) {
    handler->DenyAndResetConnection();
  }

  static void ResetIngressRegistration(MahoAIPageHandler* handler) {
    handler->ask_maho_registration_.Reset();
  }

  static void SetIngressRegistration(
      MahoAIPageHandler* handler,
      maho::MahoAiIngressCoordinator::ConsumerRegistration registration) {
    handler->ask_maho_registration_ = std::move(registration);
  }

  static void PersistEventLog(MahoAIPageHandler* handler,
                              const std::string& session_id) {
    base::test::TestFuture<bool> committed;
    handler->PersistEventLogWithReplyForTesting(session_id,
                                                committed.GetCallback());
    ASSERT_TRUE(committed.Wait());
    EXPECT_TRUE(committed.Get());
  }

  static void SetPageContextExtractor(
      MahoAIPageHandler* handler,
      std::unique_ptr<MahoAiPageContextExtractor> extractor) {
    handler->page_context_extractor_ = std::move(extractor);
  }

  static void SetRuntimeRouter(MahoAIPageHandler* handler,
                               std::unique_ptr<MahoAiRuntimeRouter> router) {
    handler->runtime_router_ = std::move(router);
  }

  static void SetRuntimeAdapterForTesting(MahoAIPageHandler* handler,
                                          MahoAiRuntimeAdapter* adapter) {
    handler->runtime_adapter_for_testing_ = adapter;
  }

  static MahoRoutinesPageHandler* routine_handler(
      MahoAIPageHandler* handler) {
    return handler->routine_handler_.get();
  }

  static void DisconnectRoutineHandler(MahoAIPageHandler* handler) {
    handler->routine_handler_.reset();
  }

  static bool routine_remote_connected(const MahoAIPageHandler* handler) {
    return handler->routine_handler_remote_.is_connected();
  }

  static void EmitRoutineStatus(MahoAIPageHandler* handler,
                                const std::string& json) {
    handler->routine_handler_->OnRoutineRunStatusJson(json);
  }

  static MahoAiPageContextExtractor* page_context_extractor(
      MahoAIPageHandler* handler) {
    return handler->page_context_extractor_.get();
  }

  static MahoAiRuntimeRouter* runtime_router(MahoAIPageHandler* handler) {
    return handler->runtime_router_.get();
  }

  static void PrepareForViewModeHandoff(MahoAIPageHandler* handler) {
    handler->PrepareForViewModeHandoff();
  }

  static const auto& RetainedReplay(const MahoAIPageHandler* handler) {
    return handler->session_event_log_;
  }

  static void SetEventLogReplyGate(
      MahoAIPageHandler* handler,
      base::RepeatingCallback<void(base::OnceClosure)> gate) {
    handler->event_log_reply_gate_for_testing_ = std::move(gate);
  }

  static void PersistEventLogAsync(MahoAIPageHandler* handler,
                                  const std::string& session_id) {
    handler->PersistEventLog(session_id);
  }

  static void ClearEventLog(MahoAIPageHandler* handler,
                            const std::string& session_id) {
    handler->ClearPersistedEventLog(session_id);
  }

  static std::vector<maho_ai::mojom::RuntimeEventPtr> LoadEventLog(
      MahoAIPageHandler* handler, const std::string& session_id) {
    return handler->LoadPersistedEventLog(session_id);
  }
};

namespace maho {
namespace {

class MockPage : public maho_ai::mojom::Page {
 public:
  void FlushForTesting() { receiver_.FlushForTesting(); }

  mojo::PendingRemote<maho_ai::mojom::Page> BindAndGetRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void OnSurfaceRequested(maho_ai::mojom::SurfaceRequestPtr request) override {
    surface_requests.push_back(std::move(request));
  }
  void OnRoutineRunStatusChanged(
      maho_routines::mojom::RoutineRunStatusPtr status) override {
    routine_statuses.push_back(std::move(status));
  }
  void OnRuntimeEvent(maho_ai::mojom::RuntimeEventPtr event) override {
    events.push_back(std::move(event));
  }
  void OnConnectionStateChanged(
      maho_ai::mojom::RuntimeConnectionState state) override {}
  void OnSessionUpdated(maho_ai::mojom::SessionInfoPtr session) override {}
  void OnAISettingsChanged(maho_ai::mojom::AISettingsInfoPtr info) override {
    settings_updates.push_back(std::move(info));
  }
  void OnRuntimeConfigChanged(
      maho_ai::mojom::RuntimeConfigInfoPtr config) override {
    runtime_config_updates.push_back(std::move(config));
  }
  void OnAskMahoSessionAccepted(
      const std::string& request_id,
      maho_ai::mojom::SessionInfoPtr session) override {
    accepted_requests.push_back(request_id);
    accepted_sessions.push_back(std::move(session));
  }
  void OnVoicePartial(const std::string& transcript) override {}
  void OnVoiceFinal(const std::string& transcript) override {}
  void OnVoiceError(const std::string& message) override {}
  void OnControlActivityChanged(
      std::vector<maho_ai::mojom::ControlActivitySnapshotPtr> entries) override {}

  std::vector<maho_ai::mojom::SurfaceRequestPtr> surface_requests;
  std::vector<maho_routines::mojom::RoutineRunStatusPtr> routine_statuses;
  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  std::vector<maho_ai::mojom::AISettingsInfoPtr> settings_updates;
  std::vector<maho_ai::mojom::RuntimeConfigInfoPtr> runtime_config_updates;
  std::vector<std::string> accepted_requests;
  std::vector<maho_ai::mojom::SessionInfoPtr> accepted_sessions;

 private:
  mojo::Receiver<maho_ai::mojom::Page> receiver_{this};
};

class AcceptingRuntimeAdapter : public MahoAiRuntimeAdapter {
 public:
  std::string GetAdapterName() const override { return "test"; }
  bool IsAvailable() const override { return available; }
  void SubmitMessage(const std::string& message,
                     maho_ai::mojom::ChatIntent chat_intent,
                     bool attach_browser_context,
                     maho_ai::mojom::InteractionMode mode,
                     RuntimeEventCallback on_event) override {
    if (before_submit) {
      before_submit.Run();
    }
    submitted_messages.push_back(message);
  }
  void CancelCurrentTurn() override {}
  void RespondToApproval(const std::string& approval_id,
                         bool approved) override {}
  std::string GetRuntimeSessionId() const override { return std::string(); }
  void Reset() override {}

  bool available = true;
  base::RepeatingClosure before_submit;
  std::vector<std::string> submitted_messages;
};

// Mirrors the live adapter contract: no session means no live config. Record
// forwarded fields verbatim so handler-side normalization is tested.
class RecordingRuntimeConfigAdapter : public AcceptingRuntimeAdapter {
 public:
  bool GetRuntimeConfig(MahoAiRuntimeConfig* out_config) const override {
    if (!has_session) {
      return false;
    }
    *out_config = config;
    return true;
  }
  bool SetRuntimeConfig(const MahoAiRuntimeConfig& value) override {
    if (!has_session) {
      return false;
    }
    ++set_calls;
    config = value;
    return true;
  }

  bool has_session = false;
  int set_calls = 0;
  MahoAiRuntimeConfig config;
};

class MahoAIPageHandlerTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const base::FilePath db =
        temp_dir_.GetPath().AppendASCII("maho_ai_page_handler_test.sqlite");
    test_core_ = maho_core_new_with_storage(db.AsUTF8Unsafe().c_str());
    ASSERT_NE(test_core_, nullptr);
    saved_core_ = GetCore();
    SetCore(test_core_);
    if (!profile()->GetPrefs()->FindPreference(ai_prefs::kProvider)) {
      MahoAIPageHandler::RegisterProfilePrefs(
          profile()->GetTestingPrefService()->registry());
    }
    AddTab(browser(), GURL("chrome://newtab"));
  }

  void TearDown() override {
    MahoAiIngressCoordinator::RemoveFromBrowser(browser());
    SetCore(saved_core_);
    if (test_core_) {
      maho_core_free(test_core_);
      test_core_ = nullptr;
    }
    BrowserWithTestWindowTest::TearDown();
  }

  std::unique_ptr<MahoAIPageHandler> CreateHandler(MockPage* page) {
    return CreateHandlerForBrowser(browser(), page);
  }

  std::unique_ptr<MahoAIPageHandler> CreateHandlerForBrowser(
      Browser* target_browser,
      MockPage* page) {
    content::WebContents* web_contents =
        target_browser->GetTabStripModel()->GetWebContentsAt(0);
    auto token =
        std::make_unique<MahoPrivateContextToken>(target_browser, web_contents);
    mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver;
    return std::make_unique<MahoAIPageHandler>(
        std::move(receiver), page->BindAndGetRemote(), std::move(token),
        target_browser, web_contents, profile()->GetPrefs(), nullptr);
  }

  static maho_ai::mojom::AskMahoDispatchPtr MakeDispatch(
      const std::string& request_id,
      const std::string& query) {
    return maho_ai::mojom::AskMahoDispatch::New(
        request_id, query, maho_ai::mojom::AskMahoSource::kCommandPalette, true,
        maho_ai::mojom::InteractionMode::kAssistant,
        maho_ai::mojom::AskMahoContextIntent::kNone, std::nullopt);
  }

  static maho_ai::mojom::SessionInfoPtr StartSession(
      MahoAIPageHandler* handler) {
    maho_ai::mojom::SessionInfoPtr session;
    handler->StartSession(
        std::nullopt, maho_ai::mojom::InteractionMode::kAssistant,
        base::BindLambdaForTesting([&](maho_ai::mojom::SessionInfoPtr result) {
          session = std::move(result);
        }));
    return session;
  }

  static std::vector<maho_ai::mojom::SessionInfoPtr> GetSessionList(
      MahoAIPageHandler* handler) {
    std::vector<maho_ai::mojom::SessionInfoPtr> sessions;
    handler->GetSessionList(base::BindLambdaForTesting(
        [&](std::vector<maho_ai::mojom::SessionInfoPtr> result) {
          sessions = std::move(result);
        }));
    return sessions;
  }

  static std::vector<maho_ai::mojom::RuntimeEventPtr> GetSessionHistory(
      MahoAIPageHandler* handler,
      const std::string& session_id) {
    std::vector<maho_ai::mojom::RuntimeEventPtr> events;
    handler->GetSessionHistory(
        session_id, 0, 100,
        base::BindLambdaForTesting(
            [&](std::vector<maho_ai::mojom::RuntimeEventPtr> result,
                uint32_t total_count) {
              EXPECT_EQ(total_count, result.size());
              events = std::move(result);
            }));
    return events;
  }

  static const maho_ai::mojom::RuntimeEvent* FindEventOfKind(
      const std::vector<maho_ai::mojom::RuntimeEventPtr>& events,
      maho_ai::mojom::RuntimeEventKind kind) {
    const auto event = std::find_if(
        events.begin(), events.end(),
        [kind](const auto& candidate) { return candidate->kind == kind; });
    return event == events.end() ? nullptr : event->get();
  }

  base::ScopedTempDir temp_dir_;
  raw_ptr<MahoCore> saved_core_ = nullptr;
  raw_ptr<MahoCore> test_core_ = nullptr;
};

class MahoRoutineHandoffTest : public MahoAIPageHandlerTest {};

TEST_F(MahoAIPageHandlerTest, RoutineUserTierRespondsWhenRoutinePipeDisconnects) {
  MockPage page;
  auto handler = CreateHandler(&page);
  int32_t tier = -1;
  handler->GetRoutineUserTier(
      base::BindLambdaForTesting([&](int32_t value) { tier = value; }));
  MahoAIPageHandlerTestPeer::DisconnectRoutineHandler(handler.get());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(tier, 0);
}

TEST_F(MahoAIPageHandlerTest,
       OnePanelOwnsOneRoutineStatusSubscriptionAndRendererOperationsWork) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoRoutinesPageHandler* routine_handler =
      MahoAIPageHandlerTestPeer::routine_handler(handler.get());
  ASSERT_TRUE(routine_handler);
  EXPECT_TRUE(routine_handler->HasStatusSubscriptionForTesting());

  base::test::TestFuture<int32_t> tier_future;
  handler->GetRoutineUserTier(tier_future.GetCallback());
  EXPECT_EQ(tier_future.Get(), 0);

  base::test::TestFuture<std::vector<maho_routines::mojom::RoutineInfoPtr>>
      list_future;
  handler->ListAllRoutines(list_future.GetCallback());
  std::vector<maho_routines::mojom::RoutineInfoPtr> routines =
      list_future.Take();
  ASSERT_EQ(4u, routines.size());
  std::vector<std::string> routine_ids;
  for (const auto& routine : routines) {
    EXPECT_FALSE(routine->is_custom);
    EXPECT_TRUE(routine->schedule.has_value());
    routine_ids.push_back(routine->id);
  }
  std::sort(routine_ids.begin(), routine_ids.end());
  EXPECT_EQ(routine_ids,
            (std::vector<std::string>{"close_old_tabs", "morning_briefing",
                                      "summarize_reading_list",
                                      "weekly_tab_tidy"}));

  MahoAIPageHandlerTestPeer::EmitRoutineStatus(
      handler.get(),
      R"({"runId":"run-1","routineId":"daily","source":"manual","state":"running","revision":1,"startedAt":10,"updatedAt":11})");
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, page.routine_statuses.size());
  EXPECT_EQ("run-1", page.routine_statuses[0]->run_id);
  EXPECT_EQ(maho_routines::mojom::RoutineRunState::kRunning,
            page.routine_statuses[0]->state);

  EXPECT_TRUE(routine_handler->HasStatusSubscriptionForTesting());
  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::routine_remote_connected(handler.get()));
}

TEST_F(MahoRoutineHandoffTest,
       PendingSurfaceIsProfileScopedOneShotAndRejectsStaleGeneration) {
  MockPage page;
  auto handler = CreateHandler(&page);

  const uint64_t generation = maho::ai::MahoAiPendingSurface::Request(
      profile()->GetPrefs(), maho_ai::mojom::CompactSurface::kRoutines);
  ASSERT_EQ(1u, page.surface_requests.size());
  EXPECT_EQ(maho_ai::mojom::CompactSurface::kRoutines,
            page.surface_requests[0]->surface);
  EXPECT_EQ(generation, page.surface_requests[0]->generation);

  maho_ai::mojom::SurfaceRequestPtr consumed;
  handler->ConsumePendingSurface(
      0, base::BindLambdaForTesting(
             [&](maho_ai::mojom::SurfaceRequestPtr request) {
               consumed = std::move(request);
             }));
  ASSERT_TRUE(consumed);
  EXPECT_EQ(maho_ai::mojom::CompactSurface::kChat, consumed->surface);
  EXPECT_EQ(generation, consumed->generation);

  maho_ai::mojom::SurfaceRequestPtr repeated;
  handler->ConsumePendingSurface(
      generation, base::BindLambdaForTesting(
                      [&](maho_ai::mojom::SurfaceRequestPtr request) {
                        repeated = std::move(request);
                      }));
  ASSERT_TRUE(repeated);
  EXPECT_EQ(maho_ai::mojom::CompactSurface::kChat, repeated->surface);
  EXPECT_EQ(generation, repeated->generation);

  TestingProfile other_profile;
  MahoAIPageHandler::RegisterProfilePrefs(
      other_profile.GetTestingPrefService()->registry());
  const uint64_t other_generation = maho::ai::MahoAiPendingSurface::Request(
      other_profile.GetPrefs(), maho_ai::mojom::CompactSurface::kRoutines);
  EXPECT_EQ(1u, other_generation);
  EXPECT_EQ(1u, page.surface_requests.size());
}

TEST_F(MahoAIPageHandlerTest, ComposerDraftRoundtripOwnsReturnedFfiString) {
  MockPage page;
  auto handler = CreateHandler(&page);
  const std::string scope = R"({"kind":"new_task"})";

  bool set_ok = false;
  handler->ComposerDraftSet(
      scope, "desktop draft",
      base::BindLambdaForTesting([&](bool ok) { set_ok = ok; }));
  ASSERT_TRUE(set_ok);

  std::optional<std::string> draft_json;
  handler->ComposerDraftGet(
      scope,
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        draft_json = result;
      }));
  ASSERT_TRUE(draft_json.has_value());
  EXPECT_NE(draft_json->find("desktop draft"), std::string::npos);

  bool delete_ok = false;
  handler->ComposerDraftDelete(
      scope, base::BindLambdaForTesting([&](bool ok) { delete_ok = ok; }));
  EXPECT_TRUE(delete_ok);
  handler->ComposerDraftGet(
      scope,
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        draft_json = result;
      }));
  EXPECT_FALSE(draft_json.has_value());
}

TEST_F(MahoAIPageHandlerTest,
       ComposerDraftNullCoreAndMalformedScopeFailClosed) {
  MockPage page;
  auto handler = CreateHandler(&page);

  bool set_ok = true;
  handler->ComposerDraftSet(
      "not-json", "draft",
      base::BindLambdaForTesting([&](bool ok) { set_ok = ok; }));
  EXPECT_FALSE(set_ok);

  SetCore(nullptr);
  std::optional<std::string> draft_json = "unexpected";
  handler->ComposerDraftGet(
      R"({"kind":"new_task"})",
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        draft_json = result;
      }));
  EXPECT_FALSE(draft_json.has_value());
  bool delete_ok = true;
  handler->ComposerDraftDelete(
      R"({"kind":"new_task"})",
      base::BindLambdaForTesting([&](bool ok) { delete_ok = ok; }));
  EXPECT_FALSE(delete_ok);
  SetCore(test_core_);
}

// Closing the AI panel destroys the WebUI and this handler, so a reopened
// panel must continue the previous conversation instead of silently presenting
// an empty one. Only an explicit new chat should clear it.
TEST_F(MahoAIPageHandlerTest,
       ReopenedPanelAdoptsPreviousConversationAsActiveSession) {
  MockPage first_page;
  std::string original_session_id;
  {
    auto handler = CreateHandler(&first_page);
    auto session = StartSession(handler.get());
    ASSERT_TRUE(session);
    original_session_id = session->session_id;

    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), original_session_id, std::nullopt,
        MahoAiRuntimeEventType::kAssistantToken, "earlier answer");
    MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                               original_session_id);
  }

  // Simulates reopening the panel: a brand new handler over the same profile.
  MockPage second_page;
  auto reopened = CreateHandler(&second_page);

  auto sessions = GetSessionList(reopened.get());
  ASSERT_FALSE(sessions.empty());
  const auto& adopted = sessions.front();
  EXPECT_EQ(original_session_id, adopted->session_id);
  EXPECT_TRUE(adopted->is_active);
  EXPECT_FALSE(adopted->is_read_only);

  auto replay = GetSessionHistory(reopened.get(), original_session_id);
  EXPECT_TRUE(std::any_of(replay.begin(), replay.end(), [](const auto& event) {
    return event->text.has_value() && *event->text == "earlier answer";
  }));
}

TEST_F(MahoAIPageHandlerTest, DefaultAISettingsExposeSelectorOptions) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  maho_ai::mojom::AISettingsInfoPtr info;
  handler->GetAISettings(
      base::BindLambdaForTesting([&](maho_ai::mojom::AISettingsInfoPtr result) {
        info = std::move(result);
      }));

  ASSERT_TRUE(info);
  EXPECT_TRUE(info->active_provider_id.empty());
  EXPECT_EQ("gpt-4o-mini", info->active_model_id);
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kMedium,
            info->active_reasoning_effort);
  ASSERT_EQ(5u, info->provider_options.size());
  EXPECT_EQ("maho-managed", info->provider_options[0]->id);
  ASSERT_EQ(1u, info->provider_options[0]->model_options.size());
  EXPECT_EQ("google/gemini-3-flash-lite:free",
            info->provider_options[0]->model_options[0]->id);
  EXPECT_EQ("openai", info->provider_options[1]->id);
  EXPECT_EQ("gpt-4o", info->provider_options[1]->model_options[0]->id);
  EXPECT_EQ("anthropic", info->provider_options[2]->id);
  EXPECT_EQ("claude-sonnet-4-20250514",
            info->provider_options[2]->model_options[0]->id);
  EXPECT_EQ("openai-compatible", info->provider_options[3]->id);
  EXPECT_EQ("local-server", info->provider_options[4]->id);
  ASSERT_EQ(3u, info->reasoning_options.size());
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kLow,
            info->reasoning_options[0]->effort);
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kMedium,
            info->reasoning_options[1]->effort);
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kHigh,
            info->reasoning_options[2]->effort);
}

TEST_F(MahoAIPageHandlerTest, MailReadConsentDefaultsOffAndPersistsPerProfile) {
  PrefService* prefs = profile()->GetPrefs();
  ASSERT_TRUE(prefs->FindPreference(ai_prefs::kMailReadAllowed));
  EXPECT_FALSE(prefs->GetBoolean(ai_prefs::kMailReadAllowed));

  prefs->SetBoolean(ai_prefs::kMailReadAllowed, true);
  EXPECT_TRUE(prefs->GetBoolean(ai_prefs::kMailReadAllowed));

  ASSERT_TRUE(prefs->FindPreference(maho::sidebar_prefs::kMahoMailEnabled));
  prefs->SetBoolean(maho::sidebar_prefs::kMahoMailEnabled, true);
  EXPECT_TRUE(prefs->GetBoolean(ai_prefs::kMailReadAllowed));
  prefs->SetBoolean(maho::sidebar_prefs::kMahoMailEnabled, false);
  EXPECT_TRUE(prefs->GetBoolean(ai_prefs::kMailReadAllowed));

  TestingProfile existing_profile;
  if (!existing_profile.GetPrefs()->FindPreference(
          ai_prefs::kMailReadAllowed)) {
    MahoAIPageHandler::RegisterProfilePrefs(
        existing_profile.GetTestingPrefService()->registry());
  }
  EXPECT_FALSE(
      existing_profile.GetPrefs()->GetBoolean(ai_prefs::kMailReadAllowed));
}

TEST_F(MahoAIPageHandlerTest, SetDefaultAISelectionPersistsAndNotifiesPage) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  bool accepted = false;
  handler->SetDefaultAISelection(
      "anthropic", "claude-opus-4-20250514",
      maho_ai::mojom::ReasoningEffort::kHigh,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));

  EXPECT_TRUE(accepted);
  EXPECT_EQ("anthropic", profile()->GetPrefs()->GetString(ai_prefs::kProvider));
  EXPECT_EQ("claude-opus-4-20250514",
            profile()->GetPrefs()->GetString(ai_prefs::kModel));
  EXPECT_EQ("high",
            profile()->GetPrefs()->GetString(ai_prefs::kReasoningEffort));
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(page.settings_updates.empty());
  const auto& update = page.settings_updates.back();
  EXPECT_EQ("anthropic", update->active_provider_id);
  EXPECT_EQ("claude-opus-4-20250514", update->active_model_id);
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kHigh,
            update->active_reasoning_effort);
}

TEST_F(MahoAIPageHandlerTest, SetDefaultAISelectionRejectsInvalidValues) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  profile()->GetPrefs()->SetString(ai_prefs::kProvider, "openai");
  profile()->GetPrefs()->SetString(ai_prefs::kModel, "gpt-4o-mini");
  profile()->GetPrefs()->SetString(ai_prefs::kReasoningEffort, "medium");

  bool accepted = true;
  handler->SetDefaultAISelection(
      "invalid-provider", "gpt-4o-mini",
      maho_ai::mojom::ReasoningEffort::kMedium,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_FALSE(accepted);

  accepted = true;
  handler->SetDefaultAISelection(
      "anthropic", "gpt-4o-mini", maho_ai::mojom::ReasoningEffort::kMedium,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_FALSE(accepted);

  accepted = true;
  handler->SetDefaultAISelection(
      "openai", "gpt-4o-mini",
      static_cast<maho_ai::mojom::ReasoningEffort>(999),
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_FALSE(accepted);

  EXPECT_EQ("openai", profile()->GetPrefs()->GetString(ai_prefs::kProvider));
  EXPECT_EQ("gpt-4o-mini", profile()->GetPrefs()->GetString(ai_prefs::kModel));
  EXPECT_EQ("medium",
            profile()->GetPrefs()->GetString(ai_prefs::kReasoningEffort));
}

TEST_F(MahoAIPageHandlerTest,
       TerminalAReentrantlyDeliversBWithoutClearingBRequestId) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> deliveries;
  std::vector<uint64_t> delivery_ids;
  auto registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              MahoAiIngressCoordinator::AcceptanceCallback accept_callback) {
            deliveries.push_back(dispatch.request_id);
            delivery_ids.push_back(delivery_id);
            if (dispatch.request_id == "request-a") {
              std::move(accept_callback).Run(session_a->session_id);
              return;
            }
            auto session_b = StartSession(handler.get());
            MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                handler.get(), "request-b", delivery_id);
            std::move(accept_callback).Run(session_b->session_id);
          }));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));
  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(handler.get(), "request-a",
                                                     delivery_ids[0]);

  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");

  ASSERT_EQ((std::vector<std::string>{"request-a", "request-b"}), deliveries);
  EXPECT_EQ("request-b", MahoAIPageHandlerTestPeer::active_ingress_request_id(
                             handler.get()));
  EXPECT_EQ(
      delivery_ids[1],
      MahoAIPageHandlerTestPeer::active_ingress_delivery_id(handler.get()));
}

TEST_F(MahoAIPageHandlerTest,
       UnavailableAdapterSettlesAThenAcceptsBWithIndependentCorrelation) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));

  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ((std::vector<std::string>{"request-a", "request-b"}),
            page.accepted_requests);
  ASSERT_EQ(2u, page.accepted_sessions.size());
  EXPECT_NE(page.accepted_sessions[0]->session_id,
            page.accepted_sessions[1]->session_id);

  std::vector<std::string> error_requests;
  std::vector<std::string> error_sessions;
  for (const auto& event : page.events) {
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kError) {
      ASSERT_TRUE(event->request_id.has_value());
      error_requests.push_back(*event->request_id);
      error_sessions.push_back(event->session_id);
    }
  }
  ASSERT_EQ((std::vector<std::string>{"request-a", "request-b"}),
            error_requests);
  ASSERT_EQ(2u, error_sessions.size());
  EXPECT_EQ(page.accepted_sessions[0]->session_id, error_sessions[0]);
  EXPECT_EQ(page.accepted_sessions[1]->session_id, error_sessions[1]);
  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::active_ingress_request_id(handler.get())
          .empty());
  EXPECT_EQ(
      0u, MahoAIPageHandlerTestPeer::active_ingress_delivery_id(handler.get()));
}

TEST_F(MahoAIPageHandlerTest,
       UnboundPageRequeuesDeliveredUnacceptedRequestBeforeTeardown) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetPage(handler.get());

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  coordinator->Dispatch(MakeDispatch("request-a", "first"));

  std::vector<std::string> fallback_deliveries;
  auto fallback_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              MahoAiIngressCoordinator::AcceptanceCallback) {
            fallback_deliveries.push_back(dispatch.request_id);
          }));

  EXPECT_EQ((std::vector<std::string>{"request-a"}), fallback_deliveries);
  handler.reset();
  EXPECT_EQ((std::vector<std::string>{"request-a"}), fallback_deliveries);
}

TEST_F(MahoAIPageHandlerTest,
       HandlerDestructionRequeuesDeliveredUnacceptedRequest) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> initial_deliveries;
  MahoAIPageHandlerTestPeer::SetIngressRegistration(
      handler.get(),
      coordinator->RegisterConsumer(
          MahoAiIngressCoordinator::ConsumerType::kSidebar,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
                  MahoAiIngressCoordinator::AcceptanceCallback) {
                initial_deliveries.push_back(dispatch.request_id);
              })));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  ASSERT_EQ((std::vector<std::string>{"request-a"}), initial_deliveries);
  handler.reset();

  std::vector<std::string> fallback_deliveries;
  auto fallback_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch, uint64_t,
              MahoAiIngressCoordinator::AcceptanceCallback) {
            fallback_deliveries.push_back(dispatch.request_id);
          }));
  EXPECT_EQ((std::vector<std::string>{"request-a"}), fallback_deliveries);
}

TEST_F(MahoAIPageHandlerTest,
       TornDownFloatingStaleTerminalDoesNotReleaseQueuedRequest) {
  MockPage page;
  auto floating_handler = CreateHandler(&page);
  ASSERT_TRUE(floating_handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(floating_handler.get());

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> sidebar_deliveries;
  std::vector<uint64_t> sidebar_delivery_ids;
  auto sidebar_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              MahoAiIngressCoordinator::AcceptanceCallback) {
            sidebar_deliveries.push_back(dispatch.request_id);
            sidebar_delivery_ids.push_back(delivery_id);
          }));

  MahoAIPageHandlerTestPeer::SetIngressRegistration(
      floating_handler.get(),
      coordinator->RegisterConsumer(
          MahoAiIngressCoordinator::ConsumerType::kFloating,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
                  uint64_t delivery_id,
                  MahoAiIngressCoordinator::AcceptanceCallback) {
                MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                    floating_handler.get(), dispatch.request_id, delivery_id);
              })));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));
  EXPECT_TRUE(sidebar_deliveries.empty());

  floating_handler.reset();

  ASSERT_EQ((std::vector<std::string>{"request-a"}), sidebar_deliveries);
  ASSERT_EQ(1u, sidebar_delivery_ids.size());
  coordinator->NotifyTerminal(sidebar_delivery_ids[0]);
  ASSERT_EQ((std::vector<std::string>{"request-a", "request-b"}),
            sidebar_deliveries);
}

TEST_F(MahoAIPageHandlerTest,
       PermissionRevocationAfterAcceptancePersistsInterruptionAndReleasesOnce) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> deliveries;
  std::vector<uint64_t> delivery_ids;
  auto registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              MahoAiIngressCoordinator::AcceptanceCallback accept_callback) {
            deliveries.push_back(dispatch.request_id);
            delivery_ids.push_back(delivery_id);
            if (dispatch.request_id == "request-a") {
              std::move(accept_callback).Run(session_a->session_id);
            }
          }));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));
  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(handler.get(), "request-a",
                                                     delivery_ids[0]);

  base::test::TestFuture<void> persisted_cancelled;
  PrefChangeRegistrar persistence_observer;
  persistence_observer.Init(profile()->GetPrefs());
  persistence_observer.Add(
      ai_prefs::kSessionEventLogs, base::BindLambdaForTesting([&] {
        const auto* log = profile()->GetPrefs()
                              ->GetDict(ai_prefs::kSessionEventLogs)
                              .FindString(session_a->session_id);
        if (log && log->find("cancelled") != std::string::npos &&
            !persisted_cancelled.IsReady()) {
          persisted_cancelled.SetValue();
        }
      }));
  MahoAIPageHandlerTestPeer::RevokePermission(handler.get());
  MahoAIPageHandlerTestPeer::RevokePermission(handler.get());
  ASSERT_TRUE(persisted_cancelled.Wait());

  EXPECT_EQ((std::vector<std::string>{"request-a", "request-b"}), deliveries);
  EXPECT_EQ(maho_ai::mojom::SessionStatus::kCancelled,
            MahoAIPageHandlerTestPeer::session_status(handler.get()));
  const std::string* persisted_log = profile()
                                         ->GetPrefs()
                                         ->GetDict(ai_prefs::kSessionEventLogs)
                                         .FindString(session_a->session_id);
  ASSERT_TRUE(persisted_log);
  EXPECT_NE(std::string::npos, persisted_log->find("cancelled"));
}

TEST_F(MahoAIPageHandlerTest,
       HandlerDestructionAfterAcceptanceInterruptsWithoutRedelivery) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> deliveries;
  std::vector<uint64_t> delivery_ids;
  auto registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              MahoAiIngressCoordinator::AcceptanceCallback accept_callback) {
            deliveries.push_back(dispatch.request_id);
            delivery_ids.push_back(delivery_id);
            if (dispatch.request_id == "request-a") {
              std::move(accept_callback).Run(session_a->session_id);
            }
          }));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));
  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(handler.get(), "request-a",
                                                     delivery_ids[0]);
  base::test::TestFuture<void> persisted_cancelled;
  PrefChangeRegistrar persistence_observer;
  persistence_observer.Init(profile()->GetPrefs());
  persistence_observer.Add(
      ai_prefs::kSessionEventLogs, base::BindLambdaForTesting([&] {
        const auto* log = profile()->GetPrefs()
                              ->GetDict(ai_prefs::kSessionEventLogs)
                              .FindString(session_a->session_id);
        if (log && log->find("cancelled") != std::string::npos &&
            !persisted_cancelled.IsReady()) {
          persisted_cancelled.SetValue();
        }
      }));
  handler.reset();
  ASSERT_TRUE(persisted_cancelled.Wait());

  EXPECT_EQ((std::vector<std::string>{"request-a", "request-b"}), deliveries);
  const std::string* persisted_log = profile()
                                         ->GetPrefs()
                                         ->GetDict(ai_prefs::kSessionEventLogs)
                                         .FindString(session_a->session_id);
  ASSERT_TRUE(persisted_log);
  EXPECT_NE(std::string::npos, persisted_log->find("cancelled"));
}

TEST_F(MahoAIPageHandlerTest, CancelTurnReleasesIngressExactlyOnce) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());
  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> deliveries;
  std::vector<uint64_t> delivery_ids;
  auto registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      base::BindLambdaForTesting(
          [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
              uint64_t delivery_id,
              MahoAiIngressCoordinator::AcceptanceCallback accept_callback) {
            deliveries.push_back(dispatch.request_id);
            delivery_ids.push_back(delivery_id);
            if (dispatch.request_id == "request-a") {
              std::move(accept_callback).Run(session_a->session_id);
            }
          }));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));
  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(handler.get(), "request-a",
                                                     delivery_ids[0]);

  handler->CancelTurn(session_a->session_id);
  handler->CancelTurn(session_a->session_id);

  EXPECT_EQ((std::vector<std::string>{"request-a", "request-b"}), deliveries);
}

TEST_F(MahoAIPageHandlerTest,
       PopupTrackerRemovalStillSettlesExactOpenerCoordinator) {
  std::unique_ptr<Browser> popup =
      CreateBrowser(profile(), Browser::TYPE_POPUP, false);
  AddTab(popup.get(), GURL("chrome://newtab"));
  MahoAiPopupLifetimeTracker::Get()->TrackPopup(browser(), popup.get());

  MockPage page;
  auto handler = CreateHandlerForBrowser(popup.get(), &page);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());

  auto* opener_coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> deliveries;
  std::string session_a_id;
  MahoAIPageHandlerTestPeer::SetIngressRegistration(
      handler.get(),
      opener_coordinator->RegisterConsumer(
          MahoAiIngressCoordinator::ConsumerType::kFloating,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
                  uint64_t delivery_id,
                  MahoAiIngressCoordinator::AcceptanceCallback
                      accept_callback) {
                deliveries.push_back(dispatch.request_id);
                if (dispatch.request_id == "request-a") {
                  auto session_a = StartSession(handler.get());
                  session_a_id = session_a->session_id;
                  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                      handler.get(), dispatch.request_id, delivery_id);
                  std::move(accept_callback).Run(session_a_id);
                }
              })));

  opener_coordinator->Dispatch(MakeDispatch("request-a", "first"));
  opener_coordinator->Dispatch(MakeDispatch("request-b", "second"));
  ASSERT_EQ((std::vector<std::string>{"request-a"}), deliveries);

  MahoAiPopupLifetimeTracker::Get()->OnBrowserClosed(popup.get());
  EXPECT_EQ(nullptr,
            MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(popup.get()));
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a_id, "request-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");

  EXPECT_EQ((std::vector<std::string>{"request-a", "request-b"}), deliveries);
}

TEST_F(MahoAIPageHandlerTest,
       CoordinatorRemovalBeforeSettlementInvalidatesWeakPtrWithoutCrash) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);
  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(handler.get(), "request-a",
                                                     1);

  MahoAiIngressCoordinator::RemoveFromBrowser(browser());
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");

  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::active_ingress_request_id(handler.get())
          .empty());
  EXPECT_EQ(
      0u, MahoAIPageHandlerTestPeer::active_ingress_delivery_id(handler.get()));
}

TEST_F(MahoAIPageHandlerTest,
       ManualStartInterruptsIngressBeforeDeferredNextDelivery) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoAIPageHandlerTestPeer::ResetIngressRegistration(handler.get());
  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> deliveries;
  std::vector<uint64_t> delivery_ids;
  MahoAIPageHandlerTestPeer::SetIngressRegistration(
      handler.get(),
      coordinator->RegisterConsumer(
          MahoAiIngressCoordinator::ConsumerType::kFloating,
          base::BindLambdaForTesting(
              [&](const maho_ai::mojom::AskMahoDispatch& dispatch,
                  uint64_t delivery_id,
                  MahoAiIngressCoordinator::AcceptanceCallback
                      accept_callback) {
                deliveries.push_back(dispatch.request_id);
                delivery_ids.push_back(delivery_id);
                if (dispatch.request_id == "request-a") {
                  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(
                      handler.get(), dispatch.request_id, delivery_id);
                  std::move(accept_callback).Run(session_a->session_id);
                }
              })));

  coordinator->Dispatch(MakeDispatch("request-a", "first"));
  coordinator->Dispatch(MakeDispatch("request-b", "second"));
  ASSERT_EQ((std::vector<std::string>{"request-a"}), deliveries);

  page.FlushForTesting();
  const size_t event_start = page.events.size();
  base::test::TestFuture<void> persisted_cancelled;
  PrefChangeRegistrar persistence_observer;
  persistence_observer.Init(profile()->GetPrefs());
  persistence_observer.Add(
      ai_prefs::kSessionEventLogs, base::BindLambdaForTesting([&] {
        const auto* log = profile()->GetPrefs()
                              ->GetDict(ai_prefs::kSessionEventLogs)
                              .FindString(session_a->session_id);
        if (log && log->find("cancelled") != std::string::npos &&
            !persisted_cancelled.IsReady()) {
          persisted_cancelled.SetValue();
        }
      }));
  auto manual_session = StartSession(handler.get());
  ASSERT_TRUE(manual_session);
  EXPECT_NE(session_a->session_id, manual_session->session_id);
  EXPECT_EQ((std::vector<std::string>{"request-a"}), deliveries);
  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::active_ingress_request_id(handler.get())
          .empty());
  EXPECT_EQ(
      0u, MahoAIPageHandlerTestPeer::active_ingress_delivery_id(handler.get()));
  page.FlushForTesting();
  ASSERT_GT(page.events.size(), event_start);
  const auto& manual_started = page.events.back();
  EXPECT_EQ(maho_ai::mojom::RuntimeEventKind::kSessionStatus,
            manual_started->kind);
  EXPECT_EQ(manual_session->session_id, manual_started->session_id);
  EXPECT_FALSE(manual_started->request_id.has_value());

  ASSERT_TRUE(persisted_cancelled.Wait());
  const std::string* persisted_log = profile()
                                         ->GetPrefs()
                                         ->GetDict(ai_prefs::kSessionEventLogs)
                                         .FindString(session_a->session_id);
  ASSERT_TRUE(persisted_log);
  EXPECT_NE(std::string::npos, persisted_log->find("cancelled"));

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ((std::vector<std::string>{"request-a", "request-b"}), deliveries);
}

TEST_F(MahoAIPageHandlerTest, ManualStartWithoutIngressRemainsUncorrelated) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  const size_t event_start = page.events.size();
  auto session = StartSession(handler.get());
  page.FlushForTesting();
  ASSERT_TRUE(session);
  ASSERT_EQ(event_start + 1, page.events.size());
  EXPECT_EQ(session->session_id, page.events.back()->session_id);
  EXPECT_FALSE(page.events.back()->request_id.has_value());
  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::active_ingress_request_id(handler.get())
          .empty());
  EXPECT_EQ(
      0u, MahoAIPageHandlerTestPeer::active_ingress_delivery_id(handler.get()));
}

TEST_F(MahoAIPageHandlerTest,
       DelayedAEventsPersistAndEmitForAWithoutMutatingCurrentB) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session_a = StartSession(handler.get());
  auto session_b = StartSession(handler.get());
  ASSERT_TRUE(session_a);
  ASSERT_TRUE(session_b);
  MahoAIPageHandlerTestPeer::SetActiveIngressRequest(handler.get(), "request-b",
                                                     1);
  const auto status_b =
      MahoAIPageHandlerTestPeer::session_status(handler.get());
  page.FlushForTesting();
  const size_t event_start = page.events.size();

  base::test::TestFuture<void> persisted_terminal;
  PrefChangeRegistrar persistence_observer;
  persistence_observer.Init(profile()->GetPrefs());
  persistence_observer.Add(
      ai_prefs::kSessionEventLogs, base::BindLambdaForTesting([&] {
        const auto* log = profile()->GetPrefs()
                              ->GetDict(ai_prefs::kSessionEventLogs)
                              .FindString(session_a->session_id);
        if (log && log->find("token") != std::string::npos &&
            log->find("error") != std::string::npos &&
            log->find("done") != std::string::npos &&
            !persisted_terminal.IsReady()) {
          persisted_terminal.SetValue();
        }
      }));
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kAssistantToken, "token");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kError, "error");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");
  ASSERT_TRUE(persisted_terminal.Wait());
  page.FlushForTesting();

  ASSERT_EQ(event_start + 3, page.events.size());
  for (size_t i = event_start; i < page.events.size(); ++i) {
    EXPECT_EQ(session_a->session_id, page.events[i]->session_id);
    ASSERT_TRUE(page.events[i]->request_id.has_value());
    EXPECT_EQ("request-a", *page.events[i]->request_id);
  }
  EXPECT_EQ("request-b", MahoAIPageHandlerTestPeer::active_ingress_request_id(
                             handler.get()));
  EXPECT_EQ(status_b, MahoAIPageHandlerTestPeer::session_status(handler.get()));

  const std::string* persisted_log = profile()
                                         ->GetPrefs()
                                         ->GetDict(ai_prefs::kSessionEventLogs)
                                         .FindString(session_a->session_id);
  ASSERT_TRUE(persisted_log);
  EXPECT_NE(std::string::npos, persisted_log->find("token"));
  EXPECT_NE(std::string::npos, persisted_log->find("error"));
  EXPECT_NE(std::string::npos, persisted_log->find("done"));
}

TEST_F(MahoAIPageHandlerTest,
       PersistedRuntimeEventRoundTripsSessionAndRequestIds) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kAssistantToken, "token");
  MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                             session_a->session_id);
  auto session_b = StartSession(handler.get());
  ASSERT_TRUE(session_b);

  auto events = GetSessionHistory(handler.get(), session_a->session_id);
  auto event_it =
      std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event->text.has_value() && *event->text == "token";
      });
  ASSERT_NE(events.end(), event_it);
  EXPECT_EQ(session_a->session_id, (*event_it)->session_id);
  ASSERT_TRUE((*event_it)->request_id.has_value());
  EXPECT_EQ("request-a", *(*event_it)->request_id);
}

TEST_F(MahoRoutineHandoffTest,
       ViewModeHandoffPreservesActiveSessionAndReplayEvents) {
  MockPage source_page;
  auto source = CreateHandler(&source_page);
  ASSERT_TRUE(source);
  auto session = StartSession(source.get());
  ASSERT_TRUE(session);

  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      source.get(), session->session_id, "request-a",
      MahoAiRuntimeEventType::kAssistantToken, "token");
  MahoAIPageHandlerTestPeer::PrepareForViewModeHandoff(source.get());
  source.reset();

  MockPage destination_page;
  auto destination = CreateHandler(&destination_page);
  ASSERT_TRUE(destination);

  maho_ai::mojom::SessionInfoPtr resumed_session;
  std::vector<maho_ai::mojom::RuntimeEventPtr> replay_events;
  destination->ResumeSession(
      session->session_id,
      base::BindLambdaForTesting(
          [&](maho_ai::mojom::SessionInfoPtr result,
              std::vector<maho_ai::mojom::RuntimeEventPtr> replay) {
            resumed_session = std::move(result);
            replay_events = std::move(replay);
          }));

  ASSERT_TRUE(resumed_session);
  EXPECT_TRUE(resumed_session->is_active);
  EXPECT_FALSE(resumed_session->is_read_only);
  EXPECT_EQ(maho_ai::mojom::SessionStatus::kActive, resumed_session->status);
  ASSERT_EQ(2u, replay_events.size());
  EXPECT_EQ(session->session_id, replay_events[0]->session_id);
  EXPECT_EQ(session->session_id, replay_events[1]->session_id);
  ASSERT_TRUE(replay_events[1]->text.has_value());
  EXPECT_EQ("token", *replay_events[1]->text);
}

TEST_F(MahoAIPageHandlerTest,
       CredentialFailureUsesTypedMojoMetadataAndSafePersistedText) {
  constexpr char kDiagnostic[] = "S3NTINEL-credential-diagnostic";
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  MahoAIPageHandlerTestPeer::EmitCredentialFailure(
      handler.get(), session->session_id,
      MahoAiRuntimeErrorCode::kCredentialDecryptFailed, kDiagnostic);
  MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                             session->session_id);
  base::RunLoop().RunUntilIdle();

  const auto live_event_it = std::find_if(
      page.events.begin(), page.events.end(), [](const auto& event) {
        return event->kind == maho_ai::mojom::RuntimeEventKind::kError;
      });
  ASSERT_NE(live_event_it, page.events.end());
  const auto& live_event = *live_event_it;
  ASSERT_TRUE(live_event->credential_error_code.has_value());
  EXPECT_EQ(maho_ai::mojom::CredentialErrorCode::kCredentialDecryptFailed,
            *live_event->credential_error_code);
  ASSERT_TRUE(live_event->text.has_value());
  EXPECT_EQ("Your saved AI credential could not be used.", *live_event->text);
  EXPECT_EQ(live_event->text->find(kDiagnostic), std::string::npos);

  const std::string* persisted = profile()
                                     ->GetPrefs()
                                     ->GetDict(ai_prefs::kSessionEventLogs)
                                     .FindString(session->session_id);
  ASSERT_TRUE(persisted);
  EXPECT_NE(persisted->find("credential_decrypt_failed"), std::string::npos);
  EXPECT_EQ(persisted->find(kDiagnostic), std::string::npos);

  auto history = MahoAIPageHandlerTestPeer::LoadEventLog(
      handler.get(), session->session_id);
  const auto history_event_it =
      std::find_if(history.begin(), history.end(), [](const auto& event) {
        return event->kind == maho_ai::mojom::RuntimeEventKind::kError &&
               event->credential_error_code.has_value();
      });
  ASSERT_NE(history_event_it, history.end());
  const auto& history_event = *history_event_it;
  EXPECT_EQ(maho_ai::mojom::CredentialErrorCode::kCredentialDecryptFailed,
            *history_event->credential_error_code);
  ASSERT_TRUE(history_event->text.has_value());
  EXPECT_EQ("Your saved AI credential could not be used.",
            *history_event->text);
}

TEST_F(MahoAIPageHandlerTest, PersistedToolResultRedactsNestedJsonOutput) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  MahoAIPageHandlerTestPeer::EmitToolResult(
      handler.get(), session->session_id,
      R"({"nested":{"password":"S3NTINEL-password"},"headers":[{"name":"Authorization","value":"Basic S3NTINEL-auth"}],"marker":"ordinary-marker"})");
  MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                             session->session_id);

  const std::string* persisted = profile()
                                     ->GetPrefs()
                                     ->GetDict(ai_prefs::kSessionEventLogs)
                                     .FindString(session->session_id);
  ASSERT_TRUE(persisted);
  EXPECT_EQ(persisted->find("S3NTINEL-"), std::string::npos);
  EXPECT_NE(persisted->find("ordinary-marker"), std::string::npos);
}

TEST_F(MahoAIPageHandlerTest,
       ApprovalRequestRoundTripsTypedMetadataAndOmitsRawArguments) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  MahoAIPageHandlerTestPeer::EmitApprovalRequest(
      handler.get(), session->session_id,
      R"({"selector":"#pay","password":"S3NTINEL-raw"})");
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(page.events.empty());
  auto live_it =
      std::find_if(page.events.begin(), page.events.end(),
                   [](const auto& event) { return !!event->approval_request; });
  ASSERT_NE(page.events.end(), live_it);
  const auto& live_event = *live_it;
  ASSERT_TRUE(live_event->approval_request);
  EXPECT_EQ(maho_ai::mojom::ApprovalPolicy::kPrompt,
            live_event->approval_request->approval_policy);
  EXPECT_EQ(maho_ai::mojom::ApprovalSensitivity::kSensitive,
            live_event->approval_request->sensitivity);
  EXPECT_EQ(maho_ai::mojom::ApprovalState::kPending,
            live_event->approval_request->state);
  EXPECT_TRUE(live_event->approval_request->page_derived_justification);
  ASSERT_TRUE(live_event->approval_request->related_tool_call);
  EXPECT_EQ("browser_click",
            live_event->approval_request->related_tool_call->tool_name);
  EXPECT_TRUE(
      live_event->approval_request->related_tool_call->arguments_json.empty());

  MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                             session->session_id);
  const std::string* persisted = profile()
                                     ->GetPrefs()
                                     ->GetDict(ai_prefs::kSessionEventLogs)
                                     .FindString(session->session_id);
  ASSERT_TRUE(persisted);
  EXPECT_EQ(persisted->find("S3NTINEL-"), std::string::npos);

  auto events = GetSessionHistory(handler.get(), session->session_id);
  auto event_it =
      std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event->approval_request &&
               event->approval_request->approval_id == "approval-typed";
      });
  ASSERT_NE(events.end(), event_it);
  EXPECT_EQ(maho_ai::mojom::ApprovalPolicy::kPrompt,
            (*event_it)->approval_request->approval_policy);
  EXPECT_EQ(maho_ai::mojom::ApprovalSensitivity::kSensitive,
            (*event_it)->approval_request->sensitivity);
  EXPECT_EQ(maho_ai::mojom::ApprovalState::kPending,
            (*event_it)->approval_request->state);
  EXPECT_TRUE((*event_it)->approval_request->page_derived_justification);
  ASSERT_TRUE((*event_it)->approval_request->related_tool_call);
  EXPECT_TRUE(
      (*event_it)->approval_request->related_tool_call->arguments_json.empty());
}

TEST_F(MahoAIPageHandlerTest,
       ApprovalRequestRoundTripsTypedMetadataWithoutRawArguments) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  base::DictValue payload;
  payload.Set("approval_id", "approval-typed");
  payload.Set("description", "The agent is requesting browser control.");
  payload.Set("related_tool_name", "browser_click");
  payload.Set("related_tool_arguments", "raw-secret-selector");
  payload.Set("approval_policy", "prompt");
  payload.Set("sensitivity", "sensitive");
  payload.Set("approval_state", "pending");
  payload.Set("page_derived_justification", true);

  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), session->session_id,
      MahoAiRuntimeEventType::kApprovalRequest, std::move(payload));
  MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                             session->session_id);
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.events.empty());
  auto event_it =
      std::find_if(page.events.begin(), page.events.end(),
                   [](const auto& event) { return !!event->approval_request; });
  ASSERT_NE(page.events.end(), event_it);
  const auto& event = *event_it;
  ASSERT_TRUE(event->approval_request);
  EXPECT_EQ(maho_ai::mojom::ApprovalPolicy::kPrompt,
            event->approval_request->approval_policy);
  EXPECT_EQ(maho_ai::mojom::ApprovalSensitivity::kSensitive,
            event->approval_request->sensitivity);
  EXPECT_EQ(maho_ai::mojom::ApprovalState::kPending,
            event->approval_request->state);
  EXPECT_TRUE(event->approval_request->page_derived_justification);
  ASSERT_TRUE(event->approval_request->related_tool_call);
  EXPECT_EQ("browser_click",
            event->approval_request->related_tool_call->tool_name);
  EXPECT_TRUE(
      event->approval_request->related_tool_call->arguments_json.empty());

  const std::string* persisted = profile()
                                     ->GetPrefs()
                                     ->GetDict(ai_prefs::kSessionEventLogs)
                                     .FindString(session->session_id);
  ASSERT_TRUE(persisted);
  EXPECT_NE(persisted->find("approval_policy"), std::string::npos);
  EXPECT_NE(persisted->find("page_derived_justification"), std::string::npos);
  EXPECT_EQ(persisted->find("raw-secret-selector"), std::string::npos);

  auto history = GetSessionHistory(handler.get(), session->session_id);
  auto history_it =
      std::find_if(history.begin(), history.end(), [](auto& item) {
        return item->approval_request &&
               item->approval_request->approval_id == "approval-typed";
      });
  ASSERT_NE(history.end(), history_it);
  EXPECT_EQ(maho_ai::mojom::ApprovalPolicy::kPrompt,
            (*history_it)->approval_request->approval_policy);
  EXPECT_EQ(maho_ai::mojom::ApprovalSensitivity::kSensitive,
            (*history_it)->approval_request->sensitivity);
  EXPECT_TRUE((*history_it)->approval_request->page_derived_justification);
}

// Plan row 6: FFI MahoAgentEventKindV2 kind-8 (InteractionRequest) events must
// reach the WebUI bridge surface as a kInteractionRequest RuntimeEvent with a
// structured InteractionRequestInfo payload (not the kError fall-through).
TEST_F(MahoAIPageHandlerTest, InteractionRequestKind8MapsToStructuredMojomPayload) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  base::DictValue payload;
  payload.Set("request_id", "interaction-req-3");
  payload.Set("kind", "question");
  payload.Set("question", "Which deployment target should we use?");
  base::ListValue options;
  base::DictValue prod;
  prod.Set("id", "opt_prod");
  prod.Set("label", "Production");
  options.Append(std::move(prod));
  payload.Set("options", std::move(options));

  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), session->session_id,
      MahoAiRuntimeEventType::kInteractionRequest, std::move(payload));
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.events.empty());
  auto event_it =
      std::find_if(page.events.begin(), page.events.end(),
                   [](const auto& event) {
                     return event->kind == maho_ai::mojom::RuntimeEventKind::
                                                kInteractionRequest;
                   });
  ASSERT_NE(page.events.end(), event_it);
  EXPECT_EQ(maho_ai::mojom::RuntimeEventKind::kInteractionRequest,
            (*event_it)->kind);
  ASSERT_TRUE((*event_it)->interaction_request);
  EXPECT_EQ("interaction-req-3", (*event_it)->interaction_request->request_id);
  EXPECT_EQ(maho_ai::mojom::InteractionRequestKind::kQuestion,
            (*event_it)->interaction_request->kind);
  EXPECT_EQ("Which deployment target should we use?",
            (*event_it)->interaction_request->question);
  ASSERT_EQ(1u, (*event_it)->interaction_request->options.size());
  EXPECT_EQ("opt_prod", (*event_it)->interaction_request->options[0]->id);
  EXPECT_EQ("Production", (*event_it)->interaction_request->options[0]->label);
}

// The live FFI kind-8 wire carries the prompt fields inside a JSON-encoded
// `args` string; the mapping must surface the effect description and the
// optional review artifact reference.
TEST_F(MahoAIPageHandlerTest,
       InteractionRequestKind8ArgsStringCarriesEffectAndArtifactRef) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  base::DictValue payload;
  payload.Set("request_id", "interaction-req-4");
  payload.Set("kind", "confirmation");
  payload.Set(
      "args",
      R"({"effect_description":"Drop database production_db_v2","artifact_ref":"artifact-review-1"})");

  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), session->session_id,
      MahoAiRuntimeEventType::kInteractionRequest, std::move(payload));
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.events.empty());
  auto event_it =
      std::find_if(page.events.begin(), page.events.end(),
                   [](const auto& event) {
                     return event->kind == maho_ai::mojom::RuntimeEventKind::
                                                kInteractionRequest;
                   });
  ASSERT_NE(page.events.end(), event_it);
  ASSERT_TRUE((*event_it)->interaction_request);
  EXPECT_EQ("interaction-req-4", (*event_it)->interaction_request->request_id);
  EXPECT_EQ(maho_ai::mojom::InteractionRequestKind::kConfirmation,
            (*event_it)->interaction_request->kind);
  EXPECT_EQ("Drop database production_db_v2",
            (*event_it)->interaction_request->question);
  EXPECT_TRUE((*event_it)->interaction_request->options.empty());
  ASSERT_TRUE((*event_it)->interaction_request->artifact_ref.has_value());
  EXPECT_EQ("artifact-review-1",
            (*event_it)->interaction_request->artifact_ref.value());
}

// Profile permissions remain editable even when no runtime adapter exists.
TEST_F(MahoAIPageHandlerTest, GetRuntimeConfigDefaultsAndSetWithoutRuntime) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  MahoAIPageHandlerTestPeer::SetRuntimeRouter(handler.get(), nullptr);
  auto config = maho_ai::mojom::RuntimeConfigInfo::New();
  config->permission_tier = "sentinel-wrong";
  config->final_confirm = false;
  config->proactive_mode = true;
  handler->GetRuntimeConfig(base::BindLambdaForTesting(
      [&](maho_ai::mojom::RuntimeConfigInfoPtr result) {
        config = std::move(result);
      }));
  ASSERT_TRUE(config);
  EXPECT_EQ("guard", config->permission_tier);
  EXPECT_TRUE(config->final_confirm);
  EXPECT_FALSE(config->proactive_mode);

  auto requested = maho_ai::mojom::RuntimeConfigInfo::New();
  requested->permission_tier = "read_only";
  requested->final_confirm = false;
  requested->proactive_mode = true;
  bool accepted = true;
  handler->SetRuntimeConfig(
      std::move(requested),
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_TRUE(accepted);
  EXPECT_EQ("read_only", profile()->GetPrefs()->GetString(
                             maho::ai_prefs::kPermissionTier));
  EXPECT_FALSE(profile()->GetPrefs()->GetBoolean(maho::ai_prefs::kFinalConfirm));
  EXPECT_TRUE(profile()->GetPrefs()->GetBoolean(maho::ai_prefs::kProactiveMode));
}

// The active runtime receives the durable profile permissions.
TEST_F(MahoAIPageHandlerTest, RuntimeConfigRoundTripsThroughActiveRuntime) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  RecordingRuntimeConfigAdapter adapter;
  MahoAIPageHandlerTestPeer::SetRuntimeAdapterForTesting(handler.get(),
                                                         &adapter);

  // Before a session exists, Get uses registered defaults and Set persists.
  auto defaults = maho_ai::mojom::RuntimeConfigInfo::New();
  handler->GetRuntimeConfig(base::BindLambdaForTesting(
      [&](maho_ai::mojom::RuntimeConfigInfoPtr result) {
        defaults = std::move(result);
      }));
  EXPECT_EQ("guard", defaults->permission_tier);
  EXPECT_TRUE(defaults->final_confirm);
  EXPECT_FALSE(defaults->proactive_mode);

  auto initial = maho_ai::mojom::RuntimeConfigInfo::New();
  initial->permission_tier = "full_access";
  initial->final_confirm = false;
  initial->proactive_mode = true;
  bool accepted = false;
  handler->SetRuntimeConfig(
      std::move(initial),
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_TRUE(accepted);
  EXPECT_EQ(0, adapter.set_calls);

  // Session active: Set delegates with the fields intact and Get serves the
  // stored value back.
  adapter.has_session = true;
  auto requested = maho_ai::mojom::RuntimeConfigInfo::New();
  requested->permission_tier = "read_only";
  requested->final_confirm = false;
  requested->proactive_mode = true;
  handler->SetRuntimeConfig(
      std::move(requested),
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_TRUE(accepted);
  EXPECT_EQ(1, adapter.set_calls);
  EXPECT_EQ("read_only", adapter.config.permission_tier);
  EXPECT_FALSE(adapter.config.final_confirm);
  EXPECT_TRUE(adapter.config.proactive_mode);

  auto stored = maho_ai::mojom::RuntimeConfigInfo::New();
  handler->GetRuntimeConfig(base::BindLambdaForTesting(
      [&](maho_ai::mojom::RuntimeConfigInfoPtr result) {
        stored = std::move(result);
      }));
  EXPECT_EQ("read_only", stored->permission_tier);
  EXPECT_FALSE(stored->final_confirm);
  EXPECT_TRUE(stored->proactive_mode);
}

TEST_F(MahoAIPageHandlerTest, RuntimeConfigPersistsAndRoundTripsFromPrefs) {
  MockPage page;
  auto handler = CreateHandler(&page);
  RecordingRuntimeConfigAdapter adapter;
  adapter.has_session = true;
  MahoAIPageHandlerTestPeer::SetRuntimeAdapterForTesting(handler.get(), &adapter);

  auto requested = maho_ai::mojom::RuntimeConfigInfo::New();
  requested->permission_tier = "full_access";
  requested->final_confirm = false;
  requested->proactive_mode = true;
  // mail_read_allowed defaults to false, so true is a real state change and
  // this assertion cannot pass by inheriting the default.
  requested->mail_read_allowed = true;
  base::test::TestFuture<bool> accepted;
  handler->SetRuntimeConfig(std::move(requested), accepted.GetCallback());
  ASSERT_TRUE(accepted.Get());

  PrefService* prefs = profile()->GetPrefs();
  ASSERT_TRUE(prefs->FindPreference("maho.ai.permission_tier"));
  ASSERT_TRUE(prefs->FindPreference("maho.ai.final_confirm"));
  ASSERT_TRUE(prefs->FindPreference("maho.ai.proactive_mode"));
  EXPECT_EQ("full_access", prefs->GetString("maho.ai.permission_tier"));
  EXPECT_FALSE(prefs->GetBoolean("maho.ai.final_confirm"));
  EXPECT_TRUE(prefs->GetBoolean("maho.ai.proactive_mode"));
  // Mail read consent shares the SSOT pref with chrome://maho-settings.
  EXPECT_TRUE(prefs->GetBoolean("maho.ai.mail_read_allowed"));
  EXPECT_EQ("full_access", adapter.config.permission_tier);
  EXPECT_FALSE(adapter.config.final_confirm);
  EXPECT_TRUE(adapter.config.proactive_mode);

  // A stale live cache must not override the durable profile choice.
  adapter.config = MahoAiRuntimeConfig{};
  base::test::TestFuture<maho_ai::mojom::RuntimeConfigInfoPtr> stored;
  handler->GetRuntimeConfig(stored.GetCallback());
  EXPECT_EQ("full_access", stored.Get()->permission_tier);
  EXPECT_FALSE(stored.Get()->final_confirm);
  EXPECT_TRUE(stored.Get()->proactive_mode);
  EXPECT_TRUE(stored.Get()->mail_read_allowed);
  EXPECT_EQ("full_access", adapter.config.permission_tier);

  // Settings-side writes push to both the page and the live runtime.
  page.FlushForTesting();
  page.runtime_config_updates.clear();
  prefs->SetString(maho::ai_prefs::kPermissionTier, "read_only");
  prefs->SetBoolean(maho::ai_prefs::kFinalConfirm, true);
  prefs->SetBoolean(maho::ai_prefs::kProactiveMode, false);
  page.FlushForTesting();
  ASSERT_EQ(3u, page.runtime_config_updates.size());
  const auto& update = page.runtime_config_updates.back();
  EXPECT_EQ("read_only", update->permission_tier);
  EXPECT_TRUE(update->final_confirm);
  EXPECT_FALSE(update->proactive_mode);
  EXPECT_EQ("read_only", adapter.config.permission_tier);
  EXPECT_TRUE(adapter.config.final_confirm);
  EXPECT_FALSE(adapter.config.proactive_mode);
}

TEST_F(MahoAIPageHandlerTest, RuntimeConfigPersistsWithoutSessionAndFailsClosed) {
  MockPage page;
  auto handler = CreateHandler(&page);
  RecordingRuntimeConfigAdapter adapter;
  adapter.has_session = false;
  MahoAIPageHandlerTestPeer::SetRuntimeAdapterForTesting(handler.get(), &adapter);
  PrefService* prefs = profile()->GetPrefs();

  for (const std::string tier : {"full_access", "root", "GUARD"}) {
    SCOPED_TRACE(tier);
    // Seed a non-default tier before each invalid input so fail-closure cannot
    // pass merely because the registered default already equals guard.
    if (tier != "full_access") {
      prefs->SetString("maho.ai.permission_tier", "full_access");
    }
    auto requested = maho_ai::mojom::RuntimeConfigInfo::New();
    requested->permission_tier = tier;
    requested->final_confirm = false;
    requested->proactive_mode = true;
    base::test::TestFuture<bool> accepted;
    handler->SetRuntimeConfig(std::move(requested), accepted.GetCallback());
    ASSERT_TRUE(accepted.Get());
    ASSERT_TRUE(prefs->FindPreference("maho.ai.permission_tier"));
    EXPECT_EQ(tier == "full_access" ? "full_access" : "guard",
              prefs->GetString("maho.ai.permission_tier"));
    EXPECT_FALSE(prefs->GetBoolean("maho.ai.final_confirm"));
    EXPECT_TRUE(prefs->GetBoolean("maho.ai.proactive_mode"));
  }
  EXPECT_EQ(0, adapter.set_calls);
}

TEST_F(MahoAIPageHandlerTest, ApprovalResultRoundTripsDecisionMetadata) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  base::DictValue payload;
  payload.Set("approval_id", "approval-result");
  payload.Set("approved", false);
  payload.Set("approval_policy", "prompt");
  payload.Set("sensitivity", "sensitive");
  payload.Set("approval_decision", "deny");
  payload.Set("approval_state", "denied");
  payload.Set("page_derived_justification", true);

  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), session->session_id,
      MahoAiRuntimeEventType::kApprovalResult, std::move(payload));
  MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(),
                                             session->session_id);
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.events.empty());
  const auto& event = page.events.back();
  ASSERT_TRUE(event->approval_result);
  EXPECT_EQ(maho_ai::mojom::ApprovalDecision::kDeny,
            event->approval_result->decision);
  EXPECT_EQ(maho_ai::mojom::ApprovalState::kDenied,
            event->approval_result->state);
  EXPECT_EQ(maho_ai::mojom::ApprovalPolicy::kPrompt,
            event->approval_result->approval_policy);
  EXPECT_EQ(maho_ai::mojom::ApprovalSensitivity::kSensitive,
            event->approval_result->sensitivity);
  EXPECT_TRUE(event->approval_result->page_derived_justification);

  auto history = GetSessionHistory(handler.get(), session->session_id);
  auto history_it =
      std::find_if(history.begin(), history.end(), [](auto& item) {
        return item->approval_result &&
               item->approval_result->approval_id == "approval-result";
      });
  ASSERT_NE(history.end(), history_it);
  EXPECT_EQ(maho_ai::mojom::ApprovalDecision::kDeny,
            (*history_it)->approval_result->decision);
  EXPECT_TRUE((*history_it)->approval_result->page_derived_justification);
}

TEST_F(MahoAIPageHandlerTest,
       LoadPersistedEventLogBackfillsLegacySessionIdFromOwnerKey) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  {
    ScopedDictPrefUpdate update(profile()->GetPrefs(),
                                ai_prefs::kSessionEventLogs);
    update->Set("legacy-session",
                R"([{"kind":1,"sequence":1,"timestamp":2,"text":"legacy"}])");
  }

  auto events = GetSessionHistory(handler.get(), "legacy-session");

  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("legacy-session", events[0]->session_id);
  EXPECT_FALSE(events[0]->request_id.has_value());
  EXPECT_FALSE(events[0]->credential_error_code.has_value());
}

TEST_F(MahoAIPageHandlerTest,
       LoadPersistedCredentialFailureUnknownCodeDowngradesToGeneric) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  {
    ScopedDictPrefUpdate update(profile()->GetPrefs(),
                                ai_prefs::kSessionEventLogs);
    update->Set(
        "credential-session",
        R"([{"kind":3,"sequence":1,"timestamp":2,"text":"S3NTINEL-unknown-credential-diagnostic","credential_error_code":"future_provider_code"}])");
  }

  auto events = GetSessionHistory(handler.get(), "credential-session");

  ASSERT_EQ(1u, events.size());
  ASSERT_TRUE(events[0]->credential_error_code.has_value());
  EXPECT_EQ(maho_ai::mojom::CredentialErrorCode::kGeneric,
            *events[0]->credential_error_code);
  ASSERT_TRUE(events[0]->text.has_value());
  EXPECT_EQ("Your saved AI credential could not be used.", *events[0]->text);
  EXPECT_EQ(events[0]->text->find("S3NTINEL-"), std::string::npos);
}

TEST_F(MahoAIPageHandlerTest,
       FirstSendPersistsConversationAndMessageBeforeAcceptance) {
  MockPage page;
  auto handler = CreateHandler(&page);
  AcceptingRuntimeAdapter adapter;
  MahoAIPageHandlerTestPeer::SetRuntimeAdapterForTesting(handler.get(),
                                                         &adapter);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  bool accepted = false;
  handler->SubmitPrompt(
      session->session_id, "Persist this first message.",
      /*attach_browser_context=*/false,
      maho_ai::mojom::InteractionMode::kAssistant,
      /*attachments=*/std::nullopt, maho_ai::mojom::ChatIntent::kFreeform,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));

  ASSERT_TRUE(accepted);
  ASSERT_EQ(1u, adapter.submitted_messages.size());
  char* conversations = maho_core_list_conversations(test_core_, 100);
  ASSERT_NE(conversations, nullptr);
  const std::string conversations_json(conversations);
  maho_string_free(conversations);
  EXPECT_NE(conversations_json.find(session->session_id), std::string::npos);

  char* messages = maho_core_get_conversation_messages(
      test_core_, session->session_id.c_str());
  ASSERT_NE(messages, nullptr);
  const std::string messages_json(messages);
  maho_string_free(messages);
  EXPECT_NE(messages_json.find("Persist this first message."),
            std::string::npos);
}

TEST_F(MahoAIPageHandlerTest,
       StartSessionInitialPromptPersistsBeforeRuntimeSubmission) {
  MockPage page;
  auto handler = CreateHandler(&page);
  AcceptingRuntimeAdapter adapter;
  MahoAIPageHandlerTestPeer::SetRuntimeAdapterForTesting(handler.get(),
                                                         &adapter);

  maho_ai::mojom::SessionInfoPtr session;
  bool persistence_observed_before_submit = false;
  handler->StartSession(
      "Persist this initial prompt.",
      maho_ai::mojom::InteractionMode::kAssistant,
      base::BindLambdaForTesting([&](maho_ai::mojom::SessionInfoPtr result) {
        session = std::move(result);
        adapter.before_submit = base::BindLambdaForTesting([&] {
          char* conversations = maho_core_list_conversations(test_core_, 100);
          char* messages = maho_core_get_conversation_messages(
              test_core_, session->session_id.c_str());
          if (conversations && messages) {
            persistence_observed_before_submit =
                std::string(conversations).find(session->session_id) !=
                    std::string::npos &&
                std::string(messages).find("Persist this initial prompt.") !=
                    std::string::npos;
          }
          maho_string_free(conversations);
          maho_string_free(messages);
        });
      }));
  ASSERT_TRUE(session);

  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(1u, adapter.submitted_messages.size());
  EXPECT_EQ("Persist this initial prompt.", adapter.submitted_messages[0]);
  EXPECT_TRUE(persistence_observed_before_submit);
}

TEST_F(MahoAIPageHandlerTest,
       RequiredPageIntentRejectsAcceptanceAndSkipsPersistence) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  bool accepted = true;

  handler->SubmitPrompt(
      session->session_id, "Summarize this page.",
      /*attach_browser_context=*/true,
      maho_ai::mojom::InteractionMode::kAssistant,
      /*attachments=*/std::nullopt,
      maho_ai::mojom::ChatIntent::kSummarizeCurrentPage,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(accepted);

  ASSERT_FALSE(page.events.empty());
  const auto* error_event =
      FindEventOfKind(page.events, maho_ai::mojom::RuntimeEventKind::kError);
  ASSERT_TRUE(error_event);
  ASSERT_TRUE(error_event->text.has_value());
  EXPECT_EQ("This quick action requires readable page content.",
            *error_event->text);
}

TEST_F(MahoAIPageHandlerTest,
       QuizIntentSurvivesCurrentPageAndAttachmentBatchCallbacks) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  handler->SubmitPrompt(session->session_id, "Quiz me on this page.",
                        /*attach_browser_context=*/true,
                        maho_ai::mojom::InteractionMode::kAssistant,
                        /*attachments=*/std::nullopt,
                        maho_ai::mojom::ChatIntent::kQuizCurrentPage,
                        base::DoNothing());

  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.events.empty());
  const auto* error_event =
      FindEventOfKind(page.events, maho_ai::mojom::RuntimeEventKind::kError);
  ASSERT_TRUE(error_event);
  ASSERT_TRUE(error_event->text.has_value());
  EXPECT_EQ("This quick action requires readable page content.",
            *error_event->text);
}

// Regression: submitting a prompt for a session that is no longer active used
// to return silently. Ask Maho creates a session and submits into it, so any
// intervening session change made the prompt vanish with no message and no
// error — the panel just showed a spinner that disappeared. The stale path must
// surface an error event instead of failing closed in silence.
TEST_F(MahoAIPageHandlerTest,
       StalePromptSubmissionSurfacesErrorInsteadOfSilence) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto first_session = StartSession(handler.get());
  ASSERT_TRUE(first_session);
  const std::string stale_session_id = first_session->session_id;

  // Starting a second session makes the first one stale.
  auto second_session = StartSession(handler.get());
  ASSERT_TRUE(second_session);
  ASSERT_NE(stale_session_id, second_session->session_id);

  page.FlushForTesting();
  page.events.clear();
  const auto current_status =
      MahoAIPageHandlerTestPeer::session_status(handler.get());
  base::test::TestFuture<bool> accepted;

  handler->SubmitPrompt(stale_session_id, "This must not vanish.",
                        /*attach_browser_context=*/false,
                        maho_ai::mojom::InteractionMode::kAssistant,
                        /*attachments=*/std::nullopt,
                        maho_ai::mojom::ChatIntent::kFreeform,
                        accepted.GetCallback());

  EXPECT_FALSE(accepted.Get());
  page.FlushForTesting();
  const auto sessions = GetSessionList(handler.get());
  const auto active = std::find_if(sessions.begin(), sessions.end(),
                                 [](const auto& item) { return item->is_active; });
  ASSERT_NE(active, sessions.end());
  EXPECT_EQ(second_session->session_id, (*active)->session_id);
  EXPECT_EQ(current_status,
            MahoAIPageHandlerTestPeer::session_status(handler.get()));

  const auto* error_event =
      FindEventOfKind(page.events, maho_ai::mojom::RuntimeEventKind::kError);
  ASSERT_TRUE(error_event)
      << "a stale-session submit must emit an error event, not fail silently";
  ASSERT_TRUE(error_event->text.has_value());
  EXPECT_NE(error_event->text->find("could not be delivered"),
            std::string::npos)
      << "actual: " << *error_event->text;
}

TEST_F(MahoAIPageHandlerTest,
       LoadPersistedEventLogRejectsConflictingSessionId) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  {
    ScopedDictPrefUpdate update(profile()->GetPrefs(),
                                ai_prefs::kSessionEventLogs);
    update->Set(
        "owner-session",
        R"([{"kind":1,"sequence":1,"timestamp":2,"text":"legacy"},{"kind":1,"session_id":"other-session","sequence":2,"timestamp":3,"text":"conflict"}])");
  }

  auto events = GetSessionHistory(handler.get(), "owner-session");

  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("legacy", events[0]->text);
  EXPECT_EQ("owner-session", events[0]->session_id);
}

TEST_F(
    MahoAIPageHandlerTest,
    DeferredContextExtractionWithQuickActionAndNavigationUsesCapturedDocumentOrRejects) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  AddTab(browser(), GURL("https://article.example.com/page1"));
  browser()->GetTabStripModel()->ActivateTabAt(1);

  handler->SubmitPrompt(
      session->session_id, "Summarize this page.", true,
      maho_ai::mojom::InteractionMode::kAssistant, std::nullopt,
      maho_ai::mojom::ChatIntent::kSummarizeCurrentPage, base::DoNothing());

  AddTab(browser(), GURL("https://article.example.com/page2"));
  browser()->GetTabStripModel()->ActivateTabAt(2);

  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.events.empty());
  for (const auto& event : page.events) {
    if (event->session_id != session->session_id) {
      continue;
    }
    if (event->browser_context && !event->browser_context->url.empty()) {
      EXPECT_EQ("https://article.example.com/page1",
                event->browser_context->url);
      EXPECT_NE("https://article.example.com/page2",
                event->browser_context->url);
    }
  }
}

TEST_F(MahoAIPageHandlerTest,
       NavigationBetweenSessionsPreventsContextLeakFromPreviousRequest) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  AddTab(browser(), GURL("https://page-a.example.com"));
  browser()->GetTabStripModel()->ActivateTabAt(1);

  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  handler->SubmitPrompt(session_a->session_id, "Explain page A", true,
                        maho_ai::mojom::InteractionMode::kAssistant,
                        std::nullopt, maho_ai::mojom::ChatIntent::kFreeform,
                        base::DoNothing());

  AddTab(browser(), GURL("https://page-b.example.com"));
  browser()->GetTabStripModel()->ActivateTabAt(2);

  auto session_b = StartSession(handler.get());
  ASSERT_TRUE(session_b);

  handler->SubmitPrompt(session_b->session_id, "Explain page B", true,
                        maho_ai::mojom::InteractionMode::kAssistant,
                        std::nullopt, maho_ai::mojom::ChatIntent::kFreeform,
                        base::DoNothing());

  base::RunLoop().RunUntilIdle();

  for (const auto& event : page.events) {
    if (event->session_id == session_b->session_id && event->browser_context &&
        !event->browser_context->url.empty()) {
      EXPECT_NE("https://page-a.example.com", event->browser_context->url);
    }
  }
}

TEST_F(MahoAIPageHandlerTest,
       EncryptedByokCredentialRestoredOnHandlerReinitWithinProfileBoundary) {
  constexpr char kEncryptedB64[] = "S3NTINEL_encrypted_byok_b64_token";

  profile()->GetPrefs()->SetString(ai_prefs::kByokOpenAIEncryptedB64,
                                   kEncryptedB64);
  profile()->GetPrefs()->SetString(ai_prefs::kProvider, "openai");
  profile()->GetPrefs()->SetString(ai_prefs::kModel, "gpt-4o");

  MockPage page1;
  auto handler1 = CreateHandler(&page1);
  ASSERT_TRUE(handler1);

  EXPECT_EQ(kEncryptedB64, profile()->GetPrefs()->GetString(
                               ai_prefs::kByokOpenAIEncryptedB64));

  maho_ai::mojom::AISettingsInfoPtr info1;
  handler1->GetAISettings(
      base::BindLambdaForTesting([&](maho_ai::mojom::AISettingsInfoPtr result) {
        info1 = std::move(result);
      }));
  ASSERT_TRUE(info1);
  EXPECT_EQ("openai", info1->active_provider_id);
  EXPECT_EQ("gpt-4o", info1->active_model_id);

  handler1.reset();

  MockPage page2;
  auto handler2 = CreateHandler(&page2);
  ASSERT_TRUE(handler2);

  EXPECT_EQ(kEncryptedB64, profile()->GetPrefs()->GetString(
                               ai_prefs::kByokOpenAIEncryptedB64));

  maho_ai::mojom::AISettingsInfoPtr info2;
  handler2->GetAISettings(
      base::BindLambdaForTesting([&](maho_ai::mojom::AISettingsInfoPtr result) {
        info2 = std::move(result);
      }));
  ASSERT_TRUE(info2);
  EXPECT_EQ("openai", info2->active_provider_id);
  EXPECT_EQ("gpt-4o", info2->active_model_id);
}

TEST_F(MahoAIPageHandlerTest,
       SetDefaultAISelectionAfterSessionCreationRefreshesStateAndNotifiesPage) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  bool accepted = false;
  handler->SetDefaultAISelection(
      "anthropic", "claude-sonnet-4-20250514",
      maho_ai::mojom::ReasoningEffort::kMedium,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));

  EXPECT_TRUE(accepted);
  EXPECT_EQ("anthropic", profile()->GetPrefs()->GetString(ai_prefs::kProvider));
  EXPECT_EQ("claude-sonnet-4-20250514",
            profile()->GetPrefs()->GetString(ai_prefs::kModel));
  EXPECT_EQ("medium",
            profile()->GetPrefs()->GetString(ai_prefs::kReasoningEffort));

  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(page.settings_updates.empty());
  const auto& update = page.settings_updates.back();
  EXPECT_EQ("anthropic", update->active_provider_id);
  EXPECT_EQ("claude-sonnet-4-20250514", update->active_model_id);
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kMedium,
            update->active_reasoning_effort);

  maho_ai::mojom::AISettingsInfoPtr info;
  handler->GetAISettings(
      base::BindLambdaForTesting([&](maho_ai::mojom::AISettingsInfoPtr result) {
        info = std::move(result);
      }));
  ASSERT_TRUE(info);
  EXPECT_EQ("anthropic", info->active_provider_id);
  EXPECT_EQ("claude-sonnet-4-20250514", info->active_model_id);
  EXPECT_EQ(maho_ai::mojom::ReasoningEffort::kMedium,
            info->active_reasoning_effort);
}

TEST_F(MahoAIPageHandlerTest, ListArtifactsReturnsRegisteredArtifacts) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto* registry = maho::ai::MahoArtifactRegistry::GetForProfile(profile());
  ASSERT_TRUE(registry);
  auto id = registry->RegisterArtifact("artifact-session", "plan.html",
                                       "text/html", 128, "plan.html", 1000);
  ASSERT_TRUE(id.has_value());

  std::vector<maho_ai::mojom::ArtifactInfoPtr> artifacts;
  handler->ListArtifacts(
      "artifact-session",
      base::BindLambdaForTesting(
          [&](std::vector<maho_ai::mojom::ArtifactInfoPtr> result) {
            artifacts = std::move(result);
          }));
  ASSERT_EQ(1u, artifacts.size());
  EXPECT_EQ(id.value(), artifacts[0]->artifact_id);
  EXPECT_EQ("plan.html", artifacts[0]->display_name);
  EXPECT_EQ("text/html", artifacts[0]->mime_type);
  EXPECT_EQ(128u, artifacts[0]->size_bytes);
}

TEST_F(MahoAIPageHandlerTest, RenameArtifactUpdatesDisplayNameOnValidName) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto* registry = maho::ai::MahoArtifactRegistry::GetForProfile(profile());
  ASSERT_TRUE(registry);
  auto id = registry->RegisterArtifact("s", "old.html", "text/html", 10,
                                       "old.html", 1);
  ASSERT_TRUE(id.has_value());

  bool success = false;
  std::optional<std::string> error = "unset";
  handler->RenameArtifact(
      id.value(), "weekly-plan",
      base::BindLambdaForTesting(
          [&](bool ok, const std::optional<std::string>& err) {
            success = ok;
            error = err;
          }));
  EXPECT_TRUE(success);
  EXPECT_FALSE(error.has_value());
  auto artifact = registry->GetArtifact(id.value());
  ASSERT_TRUE(artifact.has_value());
  EXPECT_EQ("weekly-plan.html", artifact->display_name);
}

TEST_F(MahoAIPageHandlerTest, RenameArtifactRejectsUnsafeName) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto* registry = maho::ai::MahoArtifactRegistry::GetForProfile(profile());
  ASSERT_TRUE(registry);
  auto id = registry->RegisterArtifact("s", "old.html", "text/html", 10,
                                       "old.html", 1);
  ASSERT_TRUE(id.has_value());

  bool success = true;
  std::optional<std::string> error;
  handler->RenameArtifact(
      id.value(), "../evil",
      base::BindLambdaForTesting(
          [&](bool ok, const std::optional<std::string>& err) {
            success = ok;
            error = err;
          }));
  EXPECT_FALSE(success);
  ASSERT_TRUE(error.has_value());
  EXPECT_FALSE(error->empty());
  auto artifact = registry->GetArtifact(id.value());
  ASSERT_TRUE(artifact.has_value());
  EXPECT_EQ("old.html", artifact->display_name);
}

TEST_F(MahoAIPageHandlerTest, DeleteArtifactRemovesFromRegistry) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto* registry = maho::ai::MahoArtifactRegistry::GetForProfile(profile());
  ASSERT_TRUE(registry);
  auto id = registry->RegisterArtifact("s", "doomed.html", "text/html", 10,
                                       "doomed.html", 1);
  ASSERT_TRUE(id.has_value());

  bool success = false;
  handler->DeleteArtifact(
      id.value(), base::BindLambdaForTesting([&](bool ok) { success = ok; }));
  EXPECT_TRUE(success);
  EXPECT_FALSE(registry->GetArtifact(id.value()).has_value());
}

TEST_F(MahoAIPageHandlerTest, GetArtifactPreviewUrlIssuesCapabilityUrl) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto* registry = maho::ai::MahoArtifactRegistry::GetForProfile(profile());
  ASSERT_TRUE(registry);
  auto id = registry->RegisterArtifact("s", "plan.html", "text/html", 10,
                                       "plan.html", 1);
  ASSERT_TRUE(id.has_value());

  std::optional<std::string> url = "unset";
  handler->GetArtifactPreviewUrl(
      id.value(),
      base::BindLambdaForTesting(
          [&](const std::optional<std::string>& result) { url = result; }));
  ASSERT_TRUE(url.has_value());
  EXPECT_TRUE(
      url->starts_with("chrome-untrusted://maho-ai-artifact-preview/?cap="));
}

TEST_F(MahoAIPageHandlerTest, GetArtifactExportUrlIssuesCapabilityUrl) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto* registry = maho::ai::MahoArtifactRegistry::GetForProfile(profile());
  ASSERT_TRUE(registry);
  auto id = registry->RegisterArtifact(
      "s", "data.bin", "application/octet-stream", 10, "data.bin", 1);
  ASSERT_TRUE(id.has_value());

  std::optional<std::string> url;
  handler->GetArtifactExportUrl(
      id.value(),
      base::BindLambdaForTesting(
          [&](const std::optional<std::string>& result) { url = result; }));
  ASSERT_TRUE(url.has_value());
  EXPECT_TRUE(
      url->starts_with("chrome-untrusted://maho-ai-artifact-export/?cap="));
}

TEST_F(MahoAIPageHandlerTest, GetArtifactPreviewUrlReturnsNullForUnknownId) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(maho::ai::MahoArtifactRegistry::GetForProfile(profile()));

  std::optional<std::string> url = "unset";
  handler->GetArtifactPreviewUrl(
      "no-such-artifact",
      base::BindLambdaForTesting(
          [&](const std::optional<std::string>& result) { url = result; }));
  EXPECT_FALSE(url.has_value());
}

TEST_F(MahoAIPageHandlerTest, ArtifactCreatedEventReplaysWithArtifactPayload) {
  std::string session_id;
  {
    MockPage first_page;
    auto handler = CreateHandler(&first_page);
    auto session = StartSession(handler.get());
    ASSERT_TRUE(session);
    session_id = session->session_id;
    const std::string info_json =
        "{\"artifact_id\":\"opaque-artifact-1\",\"session_id\":\"" +
        session_id +
        "\",\"display_name\":\"plan.html\",\"mime_type\":\"text/html\","
        "\"size_bytes\":128,\"created_at\":12.5}";
    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), session_id, std::nullopt,
        MahoAiRuntimeEventType::kArtifactCreated, info_json);
    MahoAIPageHandlerTestPeer::PersistEventLog(handler.get(), session_id);
  }

  // Reopen the panel over the same profile and confirm the persisted
  // kArtifactCreated event replays with its ArtifactInfo payload reconstructed
  // (paths never persisted; only the opaque id + metadata round-trip).
  MockPage second_page;
  auto reopened = CreateHandler(&second_page);
  auto replay = GetSessionHistory(reopened.get(), session_id);
  const auto* event = FindEventOfKind(
      replay, maho_ai::mojom::RuntimeEventKind::kArtifactCreated);
  ASSERT_TRUE(event);
  ASSERT_TRUE(event->artifact);
  EXPECT_EQ("opaque-artifact-1", event->artifact->artifact_id);
  EXPECT_EQ(session_id, event->artifact->session_id);
  EXPECT_EQ("plan.html", event->artifact->display_name);
  EXPECT_EQ("text/html", event->artifact->mime_type);
  EXPECT_EQ(128u, event->artifact->size_bytes);
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadReplayBoundsAndSemantics) {
  // Given: the real ingress, live replay owner, and Mojo resume path.
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  const auto& id = session->session_id;

  // When: a normally representable long turn crosses the raw event limit.
  for (int i = 0; i < 100000; ++i) {
    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), id, "mt-turn", MahoAiRuntimeEventType::kAssistantToken,
        "x");
  }
  base::DictValue call;
  call.Set("call_id", "mt-tool");
  call.Set("tool_name", "test.echo");
  call.Set("arguments_json", "{}");
  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), id, MahoAiRuntimeEventType::kToolRequest, std::move(call));
  base::DictValue result;
  result.Set("call_id", "mt-tool");
  result.Set("success", true);
  result.Set("output", "ok");
  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), id, MahoAiRuntimeEventType::kToolResult, std::move(result));
  MahoAIPageHandlerTestPeer::EmitApprovalRequest(handler.get(), id, "{}");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), id, "mt-turn", MahoAiRuntimeEventType::kArtifactCreated,
      "{\"artifact_id\":\"mt-artifact\",\"session_id\":\"" + id +
          "\",\"display_name\":\"mt.txt\",\"mime_type\":\"text/plain\","
          "\"size_bytes\":2,\"created_at\":1}");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), id, "mt-turn", MahoAiRuntimeEventType::kTurnComplete);
  base::test::TestFuture<maho_ai::mojom::SessionInfoPtr,
                        std::vector<maho_ai::mojom::RuntimeEventPtr>> resumed;
  handler->ResumeSession(id, resumed.GetCallback());

  // Then: both retained ownership and the clone are bounded, without loss.
  EXPECT_LE(MahoAIPageHandlerTestPeer::RetainedReplay(handler.get()).size(),
            2048u);
  const auto& replay = resumed.Get<1>();
  EXPECT_LE(replay.size(), 2048u);
  size_t bytes = 0;
  std::string text;
  std::vector<maho_ai::mojom::RuntimeEventKind> controls;
  uint64_t previous_sequence = 0;
  for (const auto& event : replay) {
    EXPECT_GT(event->sequence, previous_sequence);
    previous_sequence = event->sequence;
    std::string json;
    ASSERT_TRUE(base::JSONWriter::Write(
        maho::ai::SerializeRuntimeEventForPersistence(*event), &json));
    bytes += json.size();
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kAssistantToken) {
      text += event->text.value_or("");
      EXPECT_EQ(event->request_id, "mt-turn");
    } else if (event->kind != maho_ai::mojom::RuntimeEventKind::kSessionStatus) {
      controls.push_back(event->kind);
    }
  }
  EXPECT_LE(bytes, 4u * 1024 * 1024);
  EXPECT_EQ(text, std::string(100000, 'x'));
  EXPECT_EQ(controls, (std::vector<maho_ai::mojom::RuntimeEventKind>{
                         maho_ai::mojom::RuntimeEventKind::kToolRequest,
                         maho_ai::mojom::RuntimeEventKind::kToolResult,
                         maho_ai::mojom::RuntimeEventKind::kApprovalRequest,
                         maho_ai::mojom::RuntimeEventKind::kArtifactCreated,
                         maho_ai::mojom::RuntimeEventKind::kTurnComplete}));
  const auto* tool = FindEventOfKind(
      replay, maho_ai::mojom::RuntimeEventKind::kToolRequest);
  const auto* output = FindEventOfKind(
      replay, maho_ai::mojom::RuntimeEventKind::kToolResult);
  const auto* approval = FindEventOfKind(
      replay, maho_ai::mojom::RuntimeEventKind::kApprovalRequest);
  const auto* artifact = FindEventOfKind(
      replay, maho_ai::mojom::RuntimeEventKind::kArtifactCreated);
  ASSERT_TRUE(tool && tool->tool_call && output && output->tool_result);
  EXPECT_EQ(tool->tool_call->call_id, "mt-tool");
  EXPECT_EQ(output->tool_result->call_id, "mt-tool");
  EXPECT_EQ(output->tool_result->output, "ok");
  ASSERT_TRUE(approval && approval->approval_request);
  EXPECT_EQ(approval->approval_request->approval_id, "approval-typed");
  ASSERT_TRUE(artifact && artifact->artifact);
  EXPECT_EQ(artifact->artifact->artifact_id, "mt-artifact");
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadReplayWholeTurnEviction) {
  // Given: complete turns each have a correlated tool pair and terminal.
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  // When: completed-turn churn exceeds the retained record envelope.
  for (int i = 0; i < 800; ++i) {
    const std::string turn = "turn-" + std::to_string(i);
    base::DictValue call;
    call.Set("call_id", turn);
    call.Set("tool_name", "test.echo");
    call.Set("arguments_json", "{}");
    MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
        handler.get(), session->session_id, MahoAiRuntimeEventType::kToolRequest,
        std::move(call));
    base::DictValue result;
    result.Set("call_id", turn);
    result.Set("success", true);
    result.Set("output", turn);
    MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
        handler.get(), session->session_id, MahoAiRuntimeEventType::kToolResult,
        std::move(result));
    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), session->session_id, turn,
        MahoAiRuntimeEventType::kTurnComplete, turn);
  }

  // Then: only complete oldest turns disappear; the newest turn is intact.
  const auto& replay = MahoAIPageHandlerTestPeer::RetainedReplay(handler.get());
  EXPECT_LE(replay.size(), 2048u);
  std::set<std::string> calls;
  std::set<std::string> results;
  std::set<std::string> terminals;
  for (const auto& event : replay) {
    if (event->tool_call) calls.insert(event->tool_call->call_id);
    if (event->tool_result) results.insert(event->tool_result->call_id);
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kTurnComplete) {
      terminals.insert(event->request_id.value_or(""));
    }
  }
  EXPECT_EQ(calls, results);
  EXPECT_EQ(calls, terminals);
  EXPECT_TRUE(calls.contains("turn-799"));
  EXPECT_FALSE(calls.contains("turn-0"));
  EXPECT_LE(MahoAIPageHandlerTestPeer::RetainedReplayBytes(handler.get()),
            4u * 1024u * 1024u);
  ASSERT_TRUE(replay.front()->text);
  const auto window =
      base::JSONReader::ReadDict(*replay.front()->text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(window);
  const auto* metadata = window->FindDict("history_window");
  ASSERT_TRUE(metadata);
  EXPECT_GT(metadata->FindInt("omitted_turns").value_or(0), 0);
  std::vector<maho_ai::mojom::RuntimeEventPtr> restored;
  for (const auto& event : replay) {
    restored.push_back(event->Clone());
  }
  maho::ai::RuntimeReplayBudget restored_budget;
  maho::ai::NormalizeReplayWindow(restored, restored_budget);
  EXPECT_EQ(restored.size(), replay.size());
  EXPECT_EQ(restored_budget.bytes,
            MahoAIPageHandlerTestPeer::RetainedReplayBytes(handler.get()));
  EXPECT_EQ(restored_budget.omitted_turns,
            static_cast<uint64_t>(metadata->FindInt("omitted_turns").value()));
  ASSERT_TRUE(restored.front()->replay_window);
  EXPECT_EQ(restored.front()->replay_window->omitted_turns,
            restored_budget.omitted_turns);
  page.FlushForTesting();
  const auto notice = std::find_if(
      page.events.begin(), page.events.end(),
      [](const auto& event) { return static_cast<bool>(event->replay_window); });
  ASSERT_NE(notice, page.events.end());
  EXPECT_LT((*notice)->replay_window->omitted_through_sequence,
            (*notice)->sequence);
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadReplayOversizedTurnFailsExplicitly) {
  // Given: one turn that cannot fit even after delta coalescing.
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  const std::string chunk(64 * 1024, 'x');

  // When: the producer crosses the byte envelope and then claims completion.
  for (int i = 0; i < 65; ++i) {
    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), session->session_id, "oversized",
        MahoAiRuntimeEventType::kAssistantToken, chunk);
  }
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session->session_id, "oversized",
      MahoAiRuntimeEventType::kTurnComplete);

  // Then: one explicit capacity failure, no false success, bounded ownership.
  const auto& replay = MahoAIPageHandlerTestPeer::RetainedReplay(handler.get());
  size_t bytes = 0;
  size_t failures = 0;
  for (const auto& event : replay) {
    std::string json;
    ASSERT_TRUE(base::JSONWriter::Write(
        maho::ai::SerializeRuntimeEventForPersistence(*event), &json));
    bytes += json.size();
    EXPECT_NE(event->kind, maho_ai::mojom::RuntimeEventKind::kTurnComplete);
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kError) {
      ++failures;
      EXPECT_NE(event->text.value_or("").find("replay_capacity_exceeded"),
                std::string::npos);
    }
  }
  EXPECT_EQ(failures, 1u);
  EXPECT_LE(bytes, 4u * 1024 * 1024);
  EXPECT_EQ(MahoAIPageHandlerTestPeer::session_status(handler.get()),
            maho_ai::mojom::SessionStatus::kError);
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadLegacyReplayRejectsOversizedJson) {
  MockPage page;
  auto handler = CreateHandler(&page);
  base::DictValue record;
  record.Set("kind",
             static_cast<int>(maho_ai::mojom::RuntimeEventKind::kAssistantToken));
  record.Set("text", std::string(4 * 1024 * 1024, 'x'));
  base::ListValue records;
  records.Append(std::move(record));
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(records, &json));
  {
    ScopedDictPrefUpdate update(profile()->GetPrefs(), ai_prefs::kSessionEventLogs);
    update->Set("legacy-large", json);
  }
  auto events =
      MahoAIPageHandlerTestPeer::LoadEventLog(handler.get(), "legacy-large");
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events.front()->kind, maho_ai::mojom::RuntimeEventKind::kError);
  EXPECT_EQ(events.front()->text, "history_unavailable");
  const std::string* stored =
      profile()->GetPrefs()->GetDict(ai_prefs::kSessionEventLogs)
          .FindString("legacy-large");
  ASSERT_TRUE(stored);
  EXPECT_EQ(*stored, json);
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadLegacyReplayNormalizesRecordEnvelope) {
  MockPage page;
  auto handler = CreateHandler(&page);
  base::ListValue records;
  for (int i = 0; i < 3000; ++i) {
    base::DictValue record;
    record.Set("kind",
               static_cast<int>(maho_ai::mojom::RuntimeEventKind::kAssistantToken));
    record.Set("request_id", "legacy-turn");
    record.Set("sequence", static_cast<double>(i + 1));
    record.Set("text", "x");
    records.Append(std::move(record));
  }
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(records, &json));
  ASSERT_LT(json.size(), 4u * 1024u * 1024u);
  {
    ScopedDictPrefUpdate update(profile()->GetPrefs(), ai_prefs::kSessionEventLogs);
    update->Set("legacy-records", json);
  }
  auto events =
      MahoAIPageHandlerTestPeer::LoadEventLog(handler.get(), "legacy-records");
  ASSERT_LE(events.size(), 2048u);
  std::string text;
  for (const auto& event : events) {
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kAssistantToken) {
      text += event->text.value_or("");
    }
  }
  EXPECT_EQ(text, std::string(3000, 'x'));
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadPersistenceCoalescesPendingSnapshots) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  std::vector<base::OnceClosure> replies;
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(
      handler.get(), base::BindLambdaForTesting([&](base::OnceClosure reply) {
        replies.push_back(std::move(reply));
      }));
  for (int i = 0; i < 32; ++i) {
    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), session->session_id, "burst",
        MahoAiRuntimeEventType::kAssistantToken, "x");
    MahoAIPageHandlerTestPeer::PersistEventLogAsync(
        handler.get(), session->session_id);
  }
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(replies.size(), 1u);
  auto first = std::move(replies.front());
  replies.clear();
  std::move(first).Run();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(replies.size(), 1u);
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(handler.get(), {});
  std::move(replies.front()).Run();
  auto history = GetSessionHistory(handler.get(), session->session_id);
  std::string text;
  for (const auto& event : history) {
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kAssistantToken) {
      text += event->text.value_or("");
    }
  }
  EXPECT_EQ(text, std::string(32, 'x'));
}

TEST_F(MahoAIPageHandlerTest,
       MemoryThreadInactiveFlushSurvivesHandlerDestruction) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  std::vector<base::OnceClosure> replies;
  MockPage page;
  auto handler = CreateHandler(&page);
  auto a = StartSession(handler.get());
  ASSERT_TRUE(a);
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(
      handler.get(), base::BindLambdaForTesting([&](base::OnceClosure reply) {
        replies.push_back(std::move(reply));
      }));
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kAssistantToken,
      "a1");
  MahoAIPageHandlerTestPeer::PersistEventLogAsync(handler.get(), a->session_id);
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(replies.size(), 1u);
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kAssistantToken,
      "a2");
  auto b = StartSession(handler.get());
  ASSERT_TRUE(b);
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(handler.get(), {});
  handler.reset();
  for (auto& reply : replies) {
    std::move(reply).Run();
  }
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
  const auto* json = profile()->GetPrefs()->GetDict(ai_prefs::kSessionEventLogs)
                         .FindString(a->session_id);
  ASSERT_TRUE(json);
  EXPECT_NE(json->find("a1a2"), std::string::npos);
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadLateEventCannotUndoClear) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto a = StartSession(handler.get());
  ASSERT_TRUE(a);
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kAssistantToken,
      "before-clear");
  auto b = StartSession(handler.get());
  ASSERT_TRUE(b);
  MahoAIPageHandlerTestPeer::ClearEventLog(handler.get(), a->session_id);
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kAssistantToken,
      "after-clear");
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(profile()->GetPrefs()->GetDict(ai_prefs::kSessionEventLogs)
                   .contains(a->session_id));
}

TEST_F(MahoAIPageHandlerTest,
       MemoryThreadPersistenceDisableInvalidatesAdmittedWrite) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  base::test::TestFuture<base::OnceClosure> serialized;
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(
      handler.get(), serialized.GetRepeatingCallback());
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session->session_id, "a",
      MahoAiRuntimeEventType::kAssistantToken, "before-disable");
  MahoAIPageHandlerTestPeer::PersistEventLogAsync(handler.get(),
                                                session->session_id);
  ASSERT_TRUE(serialized.Wait());
  auto commit = serialized.Take();
  profile()->GetPrefs()->SetBoolean(ai_prefs::kSessionPersistenceEnabled, false);
  profile()->GetPrefs()->SetBoolean(ai_prefs::kSessionPersistenceEnabled, true);
  std::move(commit).Run();
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(handler.get(), {});
  EXPECT_FALSE(profile()->GetPrefs()->GetDict(ai_prefs::kSessionEventLogs)
                   .contains(session->session_id));
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadLatePersistenceOrdersPerSession) {
  // Given: subscribe to the actual serializer replies before any work starts.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  base::test::TestFuture<base::OnceClosure> a_serialized;
  base::test::TestFuture<base::OnceClosure> b_serialized;
  MockPage page;
  auto handler = CreateHandler(&page);
  auto a = StartSession(handler.get());
  ASSERT_TRUE(a);
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kAssistantToken,
      "a1");
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(
      handler.get(), a_serialized.GetRepeatingCallback());
  auto b = StartSession(handler.get());
  ASSERT_TRUE(b);
  ASSERT_TRUE(a_serialized.Wait());
  auto commit_a = a_serialized.Take();
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(
      handler.get(), b_serialized.GetRepeatingCallback());

  // When: A continues after switching to B; B's reply overtakes A's reply.
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kAssistantToken,
      "a2");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), a->session_id, "a", MahoAiRuntimeEventType::kTurnComplete);
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), b->session_id, "b", MahoAiRuntimeEventType::kAssistantToken,
      "b1");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), b->session_id, "b", MahoAiRuntimeEventType::kError);
  ASSERT_TRUE(b_serialized.Wait());
  std::move(b_serialized.Take()).Run();
  std::move(commit_a).Run();
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(handler.get(), {});

  // Then: real persisted history includes all accepted A text, not only a2.
  auto history = GetSessionHistory(handler.get(), a->session_id);
  std::string text;
  for (const auto& event : history) {
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kAssistantToken) {
      text += event->text.value_or("");
    }
  }
  EXPECT_EQ(text, "a1a2");
  EXPECT_TRUE(FindEventOfKind(history,
                            maho_ai::mojom::RuntimeEventKind::kTurnComplete));
  const auto* b_json = profile()->GetPrefs()->GetDict(ai_prefs::kSessionEventLogs)
                           .FindString(b->session_id);
  ASSERT_TRUE(b_json);
  EXPECT_NE(b_json->find("b1"), std::string::npos);
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadDeletePreventsResurrection) {
  // Given: a real serialized write is ready, but its commit has not run.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  base::test::TestFuture<base::OnceClosure> serialized;
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session->session_id, "deleted",
      MahoAiRuntimeEventType::kAssistantToken, "accepted-before-delete");
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(
      handler.get(), serialized.GetRepeatingCallback());
  MahoAIPageHandlerTestPeer::PersistEventLogAsync(handler.get(),
                                                session->session_id);
  ASSERT_TRUE(serialized.Wait());
  auto commit = serialized.Take();
  MahoAIPageHandlerTestPeer::ClearEventLog(handler.get(), session->session_id);

  // When: the already-admitted write completes after the clear transition.
  std::move(commit).Run();
  MahoAIPageHandlerTestPeer::SetEventLogReplyGate(handler.get(), {});

  // Then: the completion cannot resurrect the removed pref entry.
  EXPECT_FALSE(profile()->GetPrefs()->GetDict(ai_prefs::kSessionEventLogs)
                   .contains(session->session_id));
}

TEST_F(MahoAIPageHandlerTest,
       MemoryThreadReplayRespectsRequestAndControlBoundaries) {
  MockPage page;
  auto handler = CreateHandler(&page);
  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);
  for (const char* text : {"a", "b"}) {
    MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
        handler.get(), session->session_id, "request-a",
        MahoAiRuntimeEventType::kAssistantToken, text);
  }
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session->session_id, "request-b",
      MahoAiRuntimeEventType::kAssistantToken, "x");
  base::DictValue call;
  call.Set("call_id", "boundary-tool");
  call.Set("tool_name", "test.echo");
  call.Set("arguments_json", "{}");
  MahoAIPageHandlerTestPeer::EmitRuntimeEventWithPayload(
      handler.get(), session->session_id, MahoAiRuntimeEventType::kToolRequest,
      std::move(call));
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session->session_id, "request-b",
      MahoAiRuntimeEventType::kAssistantToken, "y");
  const auto& replay = MahoAIPageHandlerTestPeer::RetainedReplay(handler.get());
  std::vector<std::string> text;
  std::vector<std::string> requests;
  uint64_t previous_sequence = 0;
  for (const auto& event : replay) {
    EXPECT_GT(event->sequence, previous_sequence);
    EXPECT_GT(event->sequence_start, 0u);
    EXPECT_LE(event->sequence_start, event->sequence);
    previous_sequence = event->sequence;
    if (event->kind == maho_ai::mojom::RuntimeEventKind::kAssistantToken) {
      ASSERT_TRUE(event->text && event->request_id);
      text.push_back(*event->text);
      requests.push_back(*event->request_id);
    }
  }
  EXPECT_EQ(text, (std::vector<std::string>{"ab", "x", "y"}));
  EXPECT_EQ(requests,
            (std::vector<std::string>{"request-a", "request-b", "request-b"}));
  const auto* tool =
      FindEventOfKind(replay, maho_ai::mojom::RuntimeEventKind::kToolRequest);
  ASSERT_TRUE(tool && tool->tool_call);
  EXPECT_EQ(tool->tool_call->call_id, "boundary-tool");
}

TEST_F(MahoAIPageHandlerTest, MemoryThreadReplayRecoversAfterExhaustedTurn) {
  std::vector<maho_ai::mojom::RuntimeEventPtr> replay;
  maho::ai::RuntimeReplayBudget budget;
  auto event = maho_ai::mojom::RuntimeEvent::New();
  event->kind = maho_ai::mojom::RuntimeEventKind::kAssistantToken;
  event->session_id = "recovery-session";
  event->request_id = "failed-turn";
  event->sequence = 1;
  event->text = std::string(4 * 1024 * 1024, 'x');
  EXPECT_EQ(maho::ai::AppendBoundedReplayEvent(replay, budget, event),
            maho::ai::ReplayAdmission::kExhausted);
  ASSERT_EQ(replay.size(), 1u);
  EXPECT_EQ(replay.front()->kind, maho_ai::mojom::RuntimeEventKind::kError);
  event->kind = maho_ai::mojom::RuntimeEventKind::kAssistantToken;
  event->sequence = 2;
  event->text = "late";
  EXPECT_EQ(maho::ai::AppendBoundedReplayEvent(replay, budget, event),
            maho::ai::ReplayAdmission::kIgnored);
  EXPECT_EQ(replay.size(), 1u);
  event->kind = maho_ai::mojom::RuntimeEventKind::kUserPrompt;
  event->request_id = "next-turn";
  event->sequence = 3;
  event->text = "continue";
  EXPECT_EQ(maho::ai::AppendBoundedReplayEvent(replay, budget, event),
            maho::ai::ReplayAdmission::kAccepted);
  event->kind = maho_ai::mojom::RuntimeEventKind::kAssistantToken;
  event->sequence = 4;
  event->text = "answer";
  EXPECT_EQ(maho::ai::AppendBoundedReplayEvent(replay, budget, event),
            maho::ai::ReplayAdmission::kAccepted);
  EXPECT_FALSE(budget.exhausted);
  EXPECT_LE(budget.bytes, 4u * 1024u * 1024u);
  EXPECT_EQ(replay.back()->request_id, "next-turn");
  EXPECT_EQ(replay.back()->text, "answer");
}

TEST_F(MahoAIPageHandlerTest, ArtifactMutationsFailForUnknownIds) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(maho::ai::MahoArtifactRegistry::GetForProfile(profile()));

  bool rename_ok = true;
  std::optional<std::string> rename_err;
  handler->RenameArtifact(
      "no-such-id", "whatever",
      base::BindLambdaForTesting(
          [&](bool ok, const std::optional<std::string>& err) {
            rename_ok = ok;
            rename_err = err;
          }));
  EXPECT_FALSE(rename_ok);
  ASSERT_TRUE(rename_err.has_value());
  EXPECT_FALSE(rename_err->empty());

  bool delete_ok = true;
  handler->DeleteArtifact("no-such-id", base::BindLambdaForTesting(
                                            [&](bool ok) { delete_ok = ok; }));
  EXPECT_FALSE(delete_ok);

  std::optional<std::string> export_url = "unset";
  handler->GetArtifactExportUrl(
      "no-such-id",
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        export_url = result;
      }));
  EXPECT_FALSE(export_url.has_value());
}

}  // namespace
}  // namespace maho
