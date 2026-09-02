// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
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
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gtest/include/gtest/gtest.h"

class MahoAIPageHandlerTestPeer {
 public:
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
    handler->PersistEventLog(session_id, true);
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
};

namespace maho {
namespace {

class MockPage : public maho_ai::mojom::Page {
 public:
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
  void OnAskMahoSessionAccepted(
      const std::string& request_id,
      maho_ai::mojom::SessionInfoPtr session) override {
    accepted_requests.push_back(request_id);
    accepted_sessions.push_back(std::move(session));
  }
  void OnVoicePartial(const std::string& transcript) override {}
  void OnVoiceFinal(const std::string& transcript) override {}
  void OnVoiceError(const std::string& message) override {}

  std::vector<maho_ai::mojom::SurfaceRequestPtr> surface_requests;
  std::vector<maho_routines::mojom::RoutineRunStatusPtr> routine_statuses;
  std::vector<maho_ai::mojom::RuntimeEventPtr> events;
  std::vector<maho_ai::mojom::AISettingsInfoPtr> settings_updates;
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

// Recording fake for the session runtime_config passthrough (plan row-1
// plumbing): mirrors the adapter-side contract — Get/Set report unsupported
// while no session is active, and Set records exactly what the handler
// forwards (tier normalization is the unified adapter's concern, covered by
// the broker's ParseRuntimeConfigTier unittests).
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
        target_browser->tab_strip_model()->GetWebContentsAt(0);
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

TEST_F(MahoAIPageHandlerTest,
       OnePanelOwnsOneRoutineStatusSubscriptionAndRendererOperationsWork) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  MahoRoutinesPageHandler* routine_handler =
      MahoAIPageHandlerTestPeer::routine_handler(handler.get());
  ASSERT_TRUE(routine_handler);
  EXPECT_TRUE(routine_handler->HasStatusSubscriptionForTesting());

  int32_t tier = -1;
  handler->GetRoutineUserTier(
      base::BindLambdaForTesting([&](int32_t value) { tier = value; }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(tier, 0);

  std::vector<maho_routines::mojom::RoutineInfoPtr> routines;
  handler->ListAllRoutines(base::BindLambdaForTesting(
      [&](std::vector<maho_routines::mojom::RoutineInfoPtr> result) {
        routines = std::move(result);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(routines.empty());

  MahoAIPageHandlerTestPeer::EmitRoutineStatus(
      handler.get(),
      R"({"runId":"run-1","routineId":"daily","source":"manual","state":"running","revision":1,"startedAt":10,"updatedAt":11})");
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, page.routine_statuses.size());
  EXPECT_EQ("run-1", page.routine_statuses[0]->run_id);
  EXPECT_EQ(maho_routines::mojom::RoutineRunState::kRunning,
            page.routine_statuses[0]->state);
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

TEST_F(MahoAIPageHandlerTest, SetAISelectionPersistsAndNotifiesPage) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  bool accepted = false;
  handler->SetAISelection(
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

TEST_F(MahoAIPageHandlerTest, SetAISelectionRejectsInvalidValues) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  profile()->GetPrefs()->SetString(ai_prefs::kProvider, "openai");
  profile()->GetPrefs()->SetString(ai_prefs::kModel, "gpt-4o-mini");
  profile()->GetPrefs()->SetString(ai_prefs::kReasoningEffort, "medium");

  bool accepted = true;
  handler->SetAISelection(
      "invalid-provider", "gpt-4o-mini",
      maho_ai::mojom::ReasoningEffort::kMedium,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_FALSE(accepted);

  accepted = true;
  handler->SetAISelection(
      "anthropic", "gpt-4o-mini", maho_ai::mojom::ReasoningEffort::kMedium,
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_FALSE(accepted);

  accepted = true;
  handler->SetAISelection(
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

  MahoAIPageHandlerTestPeer::RevokePermission(handler.get());
  MahoAIPageHandlerTestPeer::RevokePermission(handler.get());

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
  handler.reset();

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

  const size_t event_start = page.events.size();
  auto manual_session = StartSession(handler.get());
  ASSERT_TRUE(manual_session);
  EXPECT_NE(session_a->session_id, manual_session->session_id);
  EXPECT_EQ((std::vector<std::string>{"request-a"}), deliveries);
  EXPECT_TRUE(
      MahoAIPageHandlerTestPeer::active_ingress_request_id(handler.get())
          .empty());
  EXPECT_EQ(
      0u, MahoAIPageHandlerTestPeer::active_ingress_delivery_id(handler.get()));
  ASSERT_GT(page.events.size(), event_start);
  const auto& manual_started = page.events.back();
  EXPECT_EQ(maho_ai::mojom::RuntimeEventKind::kSessionStatus,
            manual_started->kind);
  EXPECT_EQ(manual_session->session_id, manual_started->session_id);
  EXPECT_FALSE(manual_started->request_id.has_value());

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
  const size_t event_start = page.events.size();

  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kAssistantToken, "token");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kError, "error");
  MahoAIPageHandlerTestPeer::EmitRuntimeEvent(
      handler.get(), session_a->session_id, "request-a",
      MahoAiRuntimeEventType::kTurnComplete, "done");
  base::RunLoop().RunUntilIdle();

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

  auto history = GetSessionHistory(handler.get(), session->session_id);
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

// Plan rows 1/4: the WebUI runtime-config surface is a thin passthrough to
// the active runtime adapter. Without an adapter (or before a session
// exists) Get reports the broker defaults and Set is refused, so the panel
// keeps its previous displayed state.
TEST_F(MahoAIPageHandlerTest, GetRuntimeConfigDefaultsAndSetRefusedWithoutRuntime) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  auto config = maho_ai::mojom::RuntimeConfigInfo::New();
  config->permission_tier = "sentinel-wrong";
  config->final_confirm = false;
  config->proactive_mode = true;
  handler->GetRuntimeConfig(base::BindLambdaForTesting(
      [&](maho_ai::mojom::RuntimeConfigInfoPtr result) { config = result; }));
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
  EXPECT_FALSE(accepted);
}

// With a runtime that owns a session, Set delegates (fields intact) and the
// same values round-trip through Get; unsupported adapters keep Get on the
// broker defaults.
TEST_F(MahoAIPageHandlerTest, RuntimeConfigRoundTripsThroughActiveRuntime) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);
  RecordingRuntimeConfigAdapter adapter;
  MahoAIPageHandlerTestPeer::SetRuntimeAdapterForTesting(handler.get(),
                                                         &adapter);

  // Adapter without a session: Get stays on broker defaults, Set refused.
  auto defaults = maho_ai::mojom::RuntimeConfigInfo::New();
  handler->GetRuntimeConfig(base::BindLambdaForTesting(
      [&](maho_ai::mojom::RuntimeConfigInfoPtr result) { defaults = result; }));
  EXPECT_EQ("guard", defaults->permission_tier);
  EXPECT_TRUE(defaults->final_confirm);
  EXPECT_FALSE(defaults->proactive_mode);

  auto refused = maho_ai::mojom::RuntimeConfigInfo::New();
  refused->permission_tier = "full_access";
  bool accepted = true;
  handler->SetRuntimeConfig(
      std::move(refused),
      base::BindLambdaForTesting([&](bool result) { accepted = result; }));
  EXPECT_FALSE(accepted);
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
      [&](maho_ai::mojom::RuntimeConfigInfoPtr result) { stored = result; }));
  EXPECT_EQ("read_only", stored->permission_tier);
  EXPECT_FALSE(stored->final_confirm);
  EXPECT_TRUE(stored->proactive_mode);
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

  page.events.clear();

  handler->SubmitPrompt(stale_session_id, "This must not vanish.",
                        /*attach_browser_context=*/false,
                        maho_ai::mojom::InteractionMode::kAssistant,
                        /*attachments=*/std::nullopt,
                        maho_ai::mojom::ChatIntent::kFreeform,
                        base::DoNothing());

  base::RunLoop().RunUntilIdle();

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
  browser()->tab_strip_model()->ActivateTabAt(1);

  handler->SubmitPrompt(
      session->session_id, "Summarize this page.", true,
      maho_ai::mojom::InteractionMode::kAssistant, std::nullopt,
      maho_ai::mojom::ChatIntent::kSummarizeCurrentPage, base::DoNothing());

  AddTab(browser(), GURL("https://article.example.com/page2"));
  browser()->tab_strip_model()->ActivateTabAt(2);

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
  browser()->tab_strip_model()->ActivateTabAt(1);

  auto session_a = StartSession(handler.get());
  ASSERT_TRUE(session_a);

  handler->SubmitPrompt(session_a->session_id, "Explain page A", true,
                        maho_ai::mojom::InteractionMode::kAssistant,
                        std::nullopt, maho_ai::mojom::ChatIntent::kFreeform,
                        base::DoNothing());

  AddTab(browser(), GURL("https://page-b.example.com"));
  browser()->tab_strip_model()->ActivateTabAt(2);

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
       SetAISelectionAfterSessionCreationRefreshesStateAndNotifiesPage) {
  MockPage page;
  auto handler = CreateHandler(&page);
  ASSERT_TRUE(handler);

  auto session = StartSession(handler.get());
  ASSERT_TRUE(session);

  bool accepted = false;
  handler->SetAISelection(
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
