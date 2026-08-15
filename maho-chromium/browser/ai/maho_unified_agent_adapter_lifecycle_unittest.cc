// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_unified_agent_adapter.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/test/task_environment.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "components/prefs/testing_pref_service.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Records the maho-agent FFI interactions the adapter drives, and lets tests
// deliver token/complete/error callbacks the way the real Rust runtime would.
struct FakeAgentState {
  struct Send {
    std::string message;
    uint64_t generation = 0;
    raw_ptr<void> user_data = nullptr;

    void (*token_cb)(void*, const char*) = nullptr;
    void (*complete_cb)(void*, const char*, const char*) = nullptr;
    void (*error_cb)(void*, const char*) = nullptr;
    MahoAgentReleaseCallback release_cb = nullptr;
    raw_ptr<void> release_user_data = nullptr;
    mutable bool released = false;

    void DeliverToken(const std::string& token) const {
      if (!released && token_cb) {
        token_cb(user_data, token.c_str());
      }
    }

    void DeliverComplete(const std::string& full_text) const {
      if (!released && complete_cb) {
        complete_cb(user_data, full_text.c_str(), "[]");
      }
      Release();
    }

    void DeliverError(const std::string& error) const {
      if (!released && error_cb) {
        error_cb(user_data, error.c_str());
      }
      Release();
    }

    void Release() const {
      if (!released && release_cb) {
        released = true;
        release_cb(release_user_data);
      }
    }
  };

  bool created = false;
  bool destroyed = false;
  bool cancel_called = false;
  std::string last_session_id;
  std::string last_message;
  MahoAgentReleaseCallback session_release_cb = nullptr;
  raw_ptr<void> session_release_user_data = nullptr;
  bool session_released = false;
  MahoAgentSecureKey (*secure_storage_cb)(void*, const char*) = nullptr;
  raw_ptr<void> secure_storage_user_data = nullptr;
  MahoAgentToolResult (*browser_tool_cb)(void*, const char*, const char*) =
      nullptr;
  raw_ptr<void> browser_tool_user_data = nullptr;
  bool set_preferred_provider_result = true;
  std::vector<std::optional<std::string>> preferred_providers;
  std::vector<std::string> artifact_roots;
  std::vector<std::string> lifecycle_calls;

  std::vector<Send> sends;

  void DeliverToken(const std::string& token) {
    if (!sends.empty()) {
      sends.back().DeliverToken(token);
    }
  }

  void DeliverComplete(const std::string& full_text) {
    if (!sends.empty()) {
      sends.back().DeliverComplete(full_text);
    }
  }

  void DeliverError(const std::string& error) {
    if (!sends.empty()) {
      sends.back().DeliverError(error);
    }
  }
};

FakeAgentState* g_fake_agent = nullptr;

// File-local fakes (NOT extern "C", NOT named maho_agent_*) assembled into a
// MahoAgentFfi table and injected via set_agent_ffi_for_testing(). Because they
// do not override the real symbols, they link cleanly alongside the real
// maho-core prebuilt.
MahoAgentSession* FakeCreateSession(
    MahoCore*,
    const char* session_id,
    const char*,
    bool,
    MahoAgentPermissionDecision (*)(void*, const char*, const char*),
    void*,
    MahoAgentSecureKey (*secure_storage_cb)(void*, const char*),
    void* secure_storage_user_data,
    MahoAgentToolResult (*browser_tool_cb)(void*, const char*, const char*),
    void* browser_tool_user_data,
    const char*,
    MahoAgentReleaseCallback on_session_release,
    void* session_release_user_data) {
  if (g_fake_agent) {
    g_fake_agent->created = true;
    g_fake_agent->last_session_id = session_id ? session_id : "";
    g_fake_agent->session_release_cb = on_session_release;
    g_fake_agent->session_release_user_data = session_release_user_data;
    g_fake_agent->secure_storage_cb = secure_storage_cb;
    g_fake_agent->secure_storage_user_data = secure_storage_user_data;
    g_fake_agent->browser_tool_cb = browser_tool_cb;
    g_fake_agent->browser_tool_user_data = browser_tool_user_data;
  }
  return reinterpret_cast<MahoAgentSession*>(0x2);
}

void FakeSetApprovalPolicy(MahoAgentSession*, const char*) {}

void FakeSetMailAuthorizationState(MahoAgentSession*, bool, bool, bool, bool) {}

bool FakeSetPreferredProvider(MahoAgentSession*, const char* provider) {
  if (!g_fake_agent) {
    return false;
  }
  g_fake_agent->preferred_providers.emplace_back(
      provider ? std::optional<std::string>(provider) : std::nullopt);
  return g_fake_agent->set_preferred_provider_result;
}

bool FakeSendMessage(MahoAgentSession*,
                     const char* message,
                     void (*on_token)(void*, const char*),
                     void (*on_thinking)(void*, const char*),
                     void (*on_tool_call)(void*, const char*, const char*, const char*),
                     void (*on_tool_result)(void*, const char*, const char*, const char*, bool),
                     void (*on_complete)(void*, const char*, const char*),
                      void (*on_error)(void*, const char*),
                      void* user_data,
                      MahoAgentReleaseCallback on_release,
                      void* release_user_data) {
  if (g_fake_agent) {
    g_fake_agent->last_message = message ? message : "";
    auto& send = g_fake_agent->sends.emplace_back();
    send.message = g_fake_agent->last_message;
    send.generation =
        maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>::FromUserData(
            user_data)
            ->generation;
    send.user_data = user_data;
    send.token_cb = on_token;
    send.complete_cb = on_complete;
    send.error_cb = on_error;
    send.release_cb = on_release;
    send.release_user_data = release_user_data;
  }
  return true;
}

bool FakeCancel(MahoAgentSession*) {
  if (g_fake_agent) {
    g_fake_agent->cancel_called = true;
    g_fake_agent->lifecycle_calls.push_back("cancel");
  }
  return true;
}

void FakeSessionFree(MahoAgentSession*) {
  if (g_fake_agent) {
    g_fake_agent->destroyed = true;
    g_fake_agent->lifecycle_calls.push_back("session_free");
    if (!g_fake_agent->session_released && g_fake_agent->session_release_cb) {
      g_fake_agent->session_released = true;
      g_fake_agent->session_release_cb(g_fake_agent->session_release_user_data);
    }
  }
}

void FakeSetArtifactRoot(MahoAgentSession*, const char* path) {
  if (g_fake_agent) {
    g_fake_agent->artifact_roots.emplace_back(path ? path : "");
  }
}
void FakeSetArtifactCreatedCallback(MahoAgentSession*,
                                    void (*)(void*, const char*),
                                    void*) {}

const MahoAgentFfi kFakeFfi = {
    &FakeCreateSession, &FakeSetApprovalPolicy, &FakeSetMailAuthorizationState,
    &FakeSetPreferredProvider, &FakeSendMessage, &FakeCancel, &FakeSessionFree,
    &FakeSetArtifactRoot, &FakeSetArtifactCreatedCallback,
};

}  // namespace

class MahoUnifiedAgentAdapterLifecycleTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_agent_ = std::make_unique<FakeAgentState>();
    g_fake_agent = fake_agent_.get();
    adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(nullptr, nullptr);
    adapter_->set_agent_ffi_for_testing(&kFakeFfi);
    // SubmitMessage bails early unless maho::GetCore() is non-null. The fake
    // create_session ignores the core pointer, so a dummy is sufficient. Set
    // AFTER construction so the ctor's tool-availability registration (which
    // would call the real FFI with this pointer) stays skipped.
    maho::SetCore(reinterpret_cast<MahoCore*>(0x1));
  }

  void TearDown() override {
    adapter_.reset();
    maho::SetCore(nullptr);
    g_fake_agent = nullptr;
    fake_agent_.reset();
  }

  MahoAiRuntimeAdapter::RuntimeEventCallback callback() {
    return base::BindRepeating(
        &MahoUnifiedAgentAdapterLifecycleTest::OnEvent, base::Unretained(this));
  }

  void OnEvent(MahoAiRuntimeEvent event) { events_.push_back(std::move(event)); }

  // TEST_F bodies run in a derived class that does not inherit the fixture's
  // friend grant, so private-member access must go through a fixture helper.
  void SetAgentFfiForTesting(MahoUnifiedAgentAdapter* adapter,
                             const MahoAgentFfi* ffi) {
    adapter->set_agent_ffi_for_testing(ffi);
  }

  MahoAgentPermissionDecision CallOnAgentPermission(void* user_data,
                                                    const char* tool,
                                                    const char* args) {
    return MahoUnifiedAgentAdapter::OnAgentPermission(user_data, tool, args);
  }

  MahoAgentToolResult CallOnAgentBrowserTool(void* user_data,
                                             const char* tool,
                                             const char* args_json) {
    return MahoUnifiedAgentAdapter::OnAgentBrowserTool(user_data, tool,
                                                       args_json);
  }

  void SetAgentCallWaitTimeout(base::TimeDelta timeout) {
    MahoUnifiedAgentAdapter::SetAgentCallWaitTimeoutForTesting(timeout);
  }

  uint64_t ActiveGenerationForTesting(MahoUnifiedAgentAdapter* adapter) {
    return adapter->ActiveGenerationForTesting();
  }

  std::optional<MahoAiRuntimeErrorCode> SecureStorageOutcomeForTesting(
      MahoUnifiedAgentAdapter* adapter,
      const std::string& provider) {
    return adapter->SecureStorageOutcomeForTesting(provider);
  }

  void ClearSecureStorageEncryptorForTesting(
      MahoUnifiedAgentAdapter* adapter) {
    adapter->ClearSecureStorageEncryptorForTesting();
  }

  void OnOsCryptReadyForTesting(
      MahoUnifiedAgentAdapter* adapter,
      os_crypt_async::Encryptor encryptor) {
    adapter->OnOsCryptReady(std::move(encryptor));
  }

  void DeliverQueuedTokenForTesting(MahoUnifiedAgentAdapter* adapter,
                                    uint64_t generation,
                                    const std::string& token) {
    adapter->DeliverQueuedTokenForTesting(generation, token);
  }

  // Drives one permission decision through the same path OnAgentPermission's
  // posted task uses, minus the cross-thread wait. Returns the shared call
  // state so the test can observe the resolved decision.
  std::shared_ptr<PermissionCallState> DriveDecision(
      MahoUnifiedAgentAdapter* adapter,
      const std::string& tool,
      const std::string& extension_id,
      bool page_derived_justification = false,
      std::string consequence = "unknown") {
    auto state = std::make_shared<PermissionCallState>();
    adapter->DecidePermissionOnUiThread(
        adapter->ActiveGenerationForTesting(), tool, extension_id, state,
        page_derived_justification, std::move(consequence));
    return state;
  }

  base::OnceClosure CapturePermissionDecisionTask(
      MahoUnifiedAgentAdapter* adapter,
      void* user_data,
      const std::string& tool,
      std::shared_ptr<PermissionCallState> state) {
    return adapter->CreatePermissionDecisionTaskForTesting(
        user_data, tool, std::move(state));
  }

  base::OnceClosure CaptureBrowserToolTask(
      MahoUnifiedAgentAdapter* adapter,
      void* user_data,
      const std::string& tool,
      std::shared_ptr<BrowserToolCallState> state) {
    return adapter->CreateBrowserToolTaskForTesting(
        user_data, tool, "{}", std::move(state));
  }

  size_t PendingApprovalCount(MahoUnifiedAgentAdapter* adapter) {
    return adapter->PendingApprovalCountForTesting();
  }

  std::string FirstApprovalId() const {
    for (const auto& ev : events_) {
      if (ev.type == MahoAiRuntimeEventType::kApprovalRequest) {
        const std::string* id = ev.payload.FindString("approval_id");
        if (id) {
          return *id;
        }
      }
    }
    return std::string();
  }

  const MahoAiRuntimeEvent* FirstApprovalRequestEvent() const {
    for (const auto& ev : events_) {
      if (ev.type == MahoAiRuntimeEventType::kApprovalRequest) {
        return &ev;
      }
    }
    return nullptr;
  }

  size_t CountEvents(const std::vector<MahoAiRuntimeEvent>& events,
                     MahoAiRuntimeEventType type) const {
    size_t count = 0;
    for (const auto& ev : events) {
      if (ev.type == type) {
        ++count;
      }
    }
    return count;
  }

  size_t CountEvents(MahoAiRuntimeEventType type) const {
    return CountEvents(events_, type);
  }

  const MahoAiRuntimeEvent* LastEventOfType(MahoAiRuntimeEventType type) const {
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
      if (it->type == type) {
        return &*it;
      }
    }
    return nullptr;
  }

  std::unique_ptr<TestingPrefServiceSimple> CreateSecurePrefs() {
    auto prefs = std::make_unique<TestingPrefServiceSimple>();
    prefs->registry()->RegisterStringPref(maho::ai_prefs::kProvider, "");
    prefs->registry()->RegisterStringPref(maho::ai_prefs::kApiKey, "");
    prefs->registry()->RegisterStringPref(maho::ai_prefs::kBaseUrl, "");
    prefs->registry()->RegisterStringPref(maho::ai_prefs::kModel, "");
    prefs->registry()->RegisterStringPref(
        maho::ai_prefs::kByokOpenAIEncryptedB64, "");
    prefs->registry()->RegisterStringPref(
        maho::ai_prefs::kByokAnthropicEncryptedB64, "");
    prefs->registry()->RegisterDictionaryPref(maho::ai_prefs::kMcpCredentials);
    prefs->registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy,
                                          "prompt");
    prefs->registry()->RegisterBooleanPref(
        maho::ai_prefs::kMailReadAllowed, false);
    prefs->registry()->RegisterDictionaryPref(
        maho::ai_prefs::kAgentToolGrants);
    return prefs;
  }

  std::string ClassifyActionConsequence(const std::string& tool,
                                        const std::string& arguments) {
    return MahoUnifiedAgentAdapter::ClassifyActionConsequenceForTesting(
        tool, arguments);
  }

  const MahoAiRuntimeEvent* ExpectCredentialFailure(
      MahoAiRuntimeErrorCode expected_code) const {
    const MahoAiRuntimeEvent* event =
        LastEventOfType(MahoAiRuntimeEventType::kError);
    EXPECT_NE(event, nullptr);
    if (!event) {
      return nullptr;
    }
    EXPECT_EQ(event->text, "Your saved AI credential could not be used.");
    EXPECT_TRUE(event->runtime_error_code.has_value());
    if (!event->runtime_error_code.has_value()) {
      return nullptr;
    }
    EXPECT_EQ(*event->runtime_error_code, expected_code);
    return event;
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeAgentState> fake_agent_;
  std::unique_ptr<MahoUnifiedAgentAdapter> adapter_;
  std::vector<MahoAiRuntimeEvent> events_;
};

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ArtifactRootFailureStopsBeforeSessionAndSend) {
  adapter_.reset();
  adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(
      nullptr, nullptr, base::RepeatingCallback<bool()>(),
      base::RepeatingCallback<Browser*()>(),
      base::BindRepeating([]() -> base::expected<base::FilePath, std::string> {
        return base::unexpected("Artifact root could not be created");
      }));
  SetAgentFfiForTesting(adapter_.get(), &kFakeFfi);

  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  EXPECT_FALSE(fake_agent_->created);
  EXPECT_TRUE(fake_agent_->sends.empty());
  EXPECT_TRUE(fake_agent_->artifact_roots.empty());
  const MahoAiRuntimeEvent* error =
      LastEventOfType(MahoAiRuntimeEventType::kError);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->text, "Artifact root could not be created");
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ArtifactRootConfiguredBeforeFirstSend) {
  adapter_.reset();
  const base::FilePath root(FILE_PATH_LITERAL("/tmp/maho-artifacts"));
  adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(
      nullptr, nullptr, base::RepeatingCallback<bool()>(),
      base::RepeatingCallback<Browser*()>(),
      base::BindRepeating(
          [](base::FilePath path)
              -> base::expected<base::FilePath, std::string> { return path; },
          root));
  SetAgentFfiForTesting(adapter_.get(), &kFakeFfi);

  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(fake_agent_->created);
  ASSERT_EQ(fake_agent_->artifact_roots.size(), 1u);
  EXPECT_EQ(fake_agent_->artifact_roots[0], root.AsUTF8Unsafe());
  ASSERT_EQ(fake_agent_->sends.size(), 1u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, FullTurnFlow) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(fake_agent_->created);
  EXPECT_EQ(fake_agent_->last_message, "Hello");

  fake_agent_->DeliverToken("Hi ");
  task_environment_.RunUntilIdle();
  fake_agent_->DeliverToken("there!");
  task_environment_.RunUntilIdle();
  fake_agent_->DeliverComplete("Hi there!");
  task_environment_.RunUntilIdle();

  bool found_complete = false;
  std::string complete_text;
  for (const auto& ev : events_) {
    if (ev.type == MahoAiRuntimeEventType::kTurnComplete) {
      found_complete = true;
      complete_text = ev.text;
    }
  }
  EXPECT_TRUE(found_complete);
  EXPECT_EQ(complete_text, "Hi there!");
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 1u);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kError), 0u);
  EXPECT_TRUE(fake_agent_->sends[0].released);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
        LeasedSessionRetainsUsableSecureStorageBridgeAfterTurnRelease) {
  TestingPrefServiceSimple secure_prefs;
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kProvider,
                                              "");
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kApiKey, "");
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kBaseUrl, "");
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kModel, "");
  secure_prefs.registry()->RegisterStringPref(
      maho::ai_prefs::kByokOpenAIEncryptedB64, "");
  secure_prefs.registry()->RegisterStringPref(
      maho::ai_prefs::kByokAnthropicEncryptedB64, "");
  secure_prefs.registry()->RegisterDictionaryPref(
      maho::ai_prefs::kMcpCredentials);
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy,
                                              "prompt");
  secure_prefs.registry()->RegisterDictionaryPref(
      maho::ai_prefs::kAgentToolGrants);
  secure_prefs.SetString(maho::ai_prefs::kProvider, "openai");
  secure_prefs.SetString(maho::ai_prefs::kApiKey, "test-only-credential");

  auto secure_adapter =
      std::make_unique<MahoUnifiedAgentAdapter>(&secure_prefs, nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper,
      base::BindRepeating([](MahoAiRuntimeEvent) {}));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(fake_agent_->secure_storage_cb);
  ASSERT_TRUE(fake_agent_->secure_storage_user_data);
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  fake_agent_->sends[0].DeliverComplete("first response");
  task_environment_.RunUntilIdle();

  MahoAgentSecureKey secure_key = fake_agent_->secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  ASSERT_TRUE(secure_key.ptr);
  ASSERT_GT(secure_key.len, 0u);
  ASSERT_TRUE(secure_key.free_fn);

  UNSAFE_BUFFERS(std::memset(secure_key.ptr, 0, secure_key.len));
  secure_key.free_fn(secure_key.ptr, secure_key.len);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       LeasedSessionRepublishesEncryptedPrefWhenOsCryptBecomesReady) {
  TestingPrefServiceSimple secure_prefs;
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kProvider, "");
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kApiKey, "");
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kBaseUrl, "");
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kModel, "");
  secure_prefs.registry()->RegisterStringPref(
      maho::ai_prefs::kByokOpenAIEncryptedB64, "");
  secure_prefs.registry()->RegisterStringPref(
      maho::ai_prefs::kByokAnthropicEncryptedB64, "");
  secure_prefs.registry()->RegisterDictionaryPref(
      maho::ai_prefs::kMcpCredentials);
  secure_prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy,
                                              "prompt");
  secure_prefs.registry()->RegisterDictionaryPref(
      maho::ai_prefs::kAgentToolGrants);

  auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  std::string encrypted_test_credential;
  ASSERT_TRUE(encryptor.EncryptString("test-only-credential",
                                      &encrypted_test_credential));
  secure_prefs.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64,
                         base::Base64Encode(encrypted_test_credential));

  auto secure_adapter =
      std::make_unique<MahoUnifiedAgentAdapter>(&secure_prefs, nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper,
      base::BindRepeating([](MahoAiRuntimeEvent) {}));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(fake_agent_->secure_storage_cb);
  ASSERT_TRUE(fake_agent_->secure_storage_user_data);
  const auto secure_storage_cb = fake_agent_->secure_storage_cb;
  const void* secure_storage_user_data = fake_agent_->secure_storage_user_data;

  MahoAgentSecureKey unavailable_key = secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  EXPECT_FALSE(unavailable_key.ptr);
  EXPECT_EQ(unavailable_key.len, 0u);
  EXPECT_FALSE(unavailable_key.free_fn);

  OnOsCryptReadyForTesting(secure_adapter.get(), std::move(encryptor));

  EXPECT_EQ(fake_agent_->secure_storage_cb, secure_storage_cb);
  EXPECT_EQ(fake_agent_->secure_storage_user_data, secure_storage_user_data);
  MahoAgentSecureKey secure_key = secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  ASSERT_TRUE(secure_key.ptr);
  ASSERT_GT(secure_key.len, 0u);
  ASSERT_TRUE(secure_key.free_fn);
  UNSAFE_BUFFERS(std::memset(secure_key.ptr, 0, secure_key.len));
  secure_key.free_fn(secure_key.ptr, secure_key.len);

  MahoUnifiedAgentAdapter::ClearActiveBYOKKeys("openai");
  task_environment_.RunUntilIdle();
  MahoAgentSecureKey cleared_key = secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  EXPECT_FALSE(cleared_key.ptr);
  EXPECT_EQ(cleared_key.len, 0u);
  EXPECT_FALSE(cleared_key.free_fn);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, ErrorEmitsExactlyOneTerminalEvent) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  fake_agent_->DeliverError("agent failed");
  task_environment_.RunUntilIdle();

  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kError), 1u);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 0u);
  const MahoAiRuntimeEvent* error =
      LastEventOfType(MahoAiRuntimeEventType::kError);
  ASSERT_TRUE(error);
  EXPECT_EQ(error->text, "agent failed");
  EXPECT_FALSE(error->runtime_error_code.has_value());
  EXPECT_TRUE(fake_agent_->sends[0].released);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       MissingCredentialMapsToTypedProviderNotConfiguredError) {
  auto prefs = CreateSecurePrefs();
  auto secure_adapter = std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(fake_agent_->secure_storage_cb);
  const MahoAgentSecureKey key = fake_agent_->secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  EXPECT_FALSE(key.ptr);
  EXPECT_EQ(key.len, 0u);
  fake_agent_->DeliverError(
      R"({"version":1,"kind":"credential_error","code":"provider_not_configured"})");
  task_environment_.RunUntilIdle();

  ExpectCredentialFailure(MahoAiRuntimeErrorCode::kProviderNotConfigured);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       UnavailableEncryptorMapsToTypedSecureStoreError) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kProvider, "openai");
  prefs->SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "YW55LWNpcGhlcnRleHQ=");
  auto secure_adapter = std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->preferred_providers.size(), 1u);
  ASSERT_TRUE(fake_agent_->preferred_providers.back().has_value());
  EXPECT_EQ(*fake_agent_->preferred_providers.back(), "openai");
  ClearSecureStorageEncryptorForTesting(secure_adapter.get());
  const MahoAgentSecureKey key = fake_agent_->secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  EXPECT_FALSE(key.ptr);
  EXPECT_EQ(SecureStorageOutcomeForTesting(secure_adapter.get(), "openai"),
            MahoAiRuntimeErrorCode::kSecureStoreUnavailable);
  const MahoAgentSecureKey mcp_key = fake_agent_->secure_storage_cb(
      fake_agent_->secure_storage_user_data, "mcp:test-only-lookup");
  EXPECT_FALSE(mcp_key.ptr);
  EXPECT_EQ(SecureStorageOutcomeForTesting(secure_adapter.get(),
                                           "mcp:test-only-lookup"),
            MahoAiRuntimeErrorCode::kProviderNotConfigured);
  fake_agent_->DeliverError(
      R"({"version":1,"kind":"credential_error","code":"provider_not_configured"})");
  task_environment_.RunUntilIdle();

  ExpectCredentialFailure(MahoAiRuntimeErrorCode::kSecureStoreUnavailable);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       BadCredentialEncodingMapsToTypedDecryptError) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kProvider, "openai");
  prefs->SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "not base64");
  auto secure_adapter = std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  const MahoAgentSecureKey key = fake_agent_->secure_storage_cb(
      fake_agent_->secure_storage_user_data, "openai");
  EXPECT_FALSE(key.ptr);
  fake_agent_->DeliverError(
      R"({"version":1,"kind":"credential_error","code":"provider_not_configured"})");
  task_environment_.RunUntilIdle();

  ExpectCredentialFailure(MahoAiRuntimeErrorCode::kCredentialDecryptFailed);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ManagedAuthEnvelopeMapsToTypedErrorWithoutDiagnostics) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
  auto secure_adapter = std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->preferred_providers.size(), 1u);
  ASSERT_TRUE(fake_agent_->preferred_providers.back().has_value());
  EXPECT_EQ(*fake_agent_->preferred_providers.back(), "managed");
  fake_agent_->DeliverError(
      R"({"version":1,"kind":"credential_error","code":"managed_auth_unavailable"})");
  task_environment_.RunUntilIdle();

  ExpectCredentialFailure(MahoAiRuntimeErrorCode::kManagedAuthUnavailable);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       RemainingAllowlistedCredentialEnvelopesMapToTypedErrors) {
  const std::pair<const char*, MahoAiRuntimeErrorCode> cases[] = {
      {"credential_unusable", MahoAiRuntimeErrorCode::kCredentialUnusable},
      {"unsupported_provider", MahoAiRuntimeErrorCode::kUnsupportedProvider},
  };
  for (const auto& [code, expected_code] : cases) {
    adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                            false,
                            maho_ai::mojom::InteractionMode::kDeveloper,
                            callback());
    task_environment_.RunUntilIdle();

    fake_agent_->DeliverError(
        std::string(R"({"version":1,"kind":"credential_error","code":")") +
        code + R"("})");
    task_environment_.RunUntilIdle();
    ExpectCredentialFailure(expected_code);
    events_.clear();
  }
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       UnsupportedPreferredProviderFailsBeforeSendingAndIsTyped) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kProvider, "not-allowlisted");
  fake_agent_->set_preferred_provider_result = false;
  auto secure_adapter = std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);
  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->preferred_providers.size(), 1u);
  ASSERT_TRUE(fake_agent_->preferred_providers.back().has_value());
  EXPECT_EQ(*fake_agent_->preferred_providers.back(), "not-allowlisted");
  EXPECT_TRUE(fake_agent_->sends.empty());
  ExpectCredentialFailure(MahoAiRuntimeErrorCode::kUnsupportedProvider);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       MalformedCredentialEnvelopeUsesSafeFallbackWithoutDiagnosticLeakage) {
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  constexpr char kSyntheticDiagnostic[] =
      "SYNTHETIC-OS-CRYPT-DIAGNOSTIC-must-not-leak";
  fake_agent_->DeliverError(
      R"({"version":1,"kind":"credential_error","code":"provider_not_configured","diagnostic":"SYNTHETIC-OS-CRYPT-DIAGNOSTIC-must-not-leak"})");
  task_environment_.RunUntilIdle();

  const MahoAiRuntimeEvent* error =
      LastEventOfType(MahoAiRuntimeEventType::kError);
  ASSERT_TRUE(error);
  EXPECT_EQ(error->text, "Your saved AI credential could not be used.");
  EXPECT_FALSE(error->runtime_error_code.has_value());
  EXPECT_EQ(error->text.find(kSyntheticDiagnostic), std::string::npos);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       CompletedTurnAllowsSecondTurnWithFreshFfiState) {
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->sends.size(), 1u);
  const uint64_t first_generation = fake_agent_->sends[0].generation;
  ASSERT_NE(first_generation, 0u);
  ASSERT_TRUE(fake_agent_->sends[0].token_cb);
  ASSERT_TRUE(fake_agent_->sends[0].complete_cb);
  ASSERT_TRUE(fake_agent_->sends[0].error_cb);
  EXPECT_EQ(fake_agent_->sends[0].message, "first");

  fake_agent_->sends[0].DeliverComplete("first response");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 1u);

  adapter_->SubmitMessage("second", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->sends.size(), 2u);
  const FakeAgentState::Send& second_send = fake_agent_->sends[1];
  EXPECT_EQ(second_send.message, "second");
  EXPECT_NE(second_send.generation, first_generation);
  EXPECT_TRUE(second_send.token_cb);
  EXPECT_TRUE(second_send.complete_cb);
  EXPECT_TRUE(second_send.error_cb);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ProducerReleaseWithoutTerminalCallbackAllowsSecondTurn) {
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  fake_agent_->sends[0].Release();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 0u);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kError), 0u);

  adapter_->SubmitMessage("second", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->sends.size(), 2u);
  EXPECT_EQ(fake_agent_->sends[1].message, "second");
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       StaleQueuedPermissionTaskDeniesAfterSuccessorStarts) {
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  auto state = std::make_shared<PermissionCallState>();
  base::OnceClosure queued_task = CapturePermissionDecisionTask(
      adapter_.get(), fake_agent_->sends[0].user_data, "fs_read", state);
  EXPECT_EQ(state->generation, fake_agent_->sends[0].generation);

  fake_agent_->sends[0].DeliverComplete("first response");
  task_environment_.RunUntilIdle();
  adapter_->SubmitMessage("second", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 2u);
  events_.clear();

  std::move(queued_task).Run();

  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kApprovalRequest), 0u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       StaleQueuedBrowserToolTaskDoesNotResolveBrowserAfterReset) {
  int browser_resolution_count = 0;
  adapter_.reset();
  adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(
      nullptr, nullptr, base::RepeatingCallback<bool()>(),
      base::BindRepeating(
          [](int* count) -> Browser* {
            ++*count;
            return nullptr;
          },
          &browser_resolution_count));
  SetAgentFfiForTesting(adapter_.get(), &kFakeFfi);

  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  auto state = std::make_shared<BrowserToolCallState>();
  base::OnceClosure queued_task = CaptureBrowserToolTask(
      adapter_.get(), fake_agent_->sends[0].user_data, "get_active_tab", state);

  adapter_->Reset();
  browser_resolution_count = 0;

  std::move(queued_task).Run();

  EXPECT_TRUE(state->done.IsSignaled());
  EXPECT_EQ(state->result_error, "Agent browser tool callback is stale");
  EXPECT_TRUE(state->result_json.empty());
  EXPECT_EQ(browser_resolution_count, 0);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       SessionBrowserToolTaskUsesActiveTurnGeneration) {
  adapter_.reset();
  adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(
      nullptr, nullptr, base::RepeatingCallback<bool()>(),
      base::BindRepeating([]() -> Browser* { return nullptr; }));
  SetAgentFfiForTesting(adapter_.get(), &kFakeFfi);

  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(fake_agent_->created);
  ASSERT_NE(nullptr, fake_agent_->browser_tool_cb);
  ASSERT_NE(nullptr, fake_agent_->browser_tool_user_data);

  auto state = std::make_shared<BrowserToolCallState>();
  base::OnceClosure queued_task = CaptureBrowserToolTask(
      adapter_.get(), fake_agent_->browser_tool_user_data, "get_active_tab",
      state);
  std::move(queued_task).Run();

  EXPECT_TRUE(state->done.IsSignaled());
  EXPECT_EQ(state->result_error, "No active browser window");
  EXPECT_TRUE(state->result_json.empty());
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       OverlappingTurnIsRejectedWithoutStealingFirstTurnCallback) {
  std::vector<MahoAiRuntimeEvent> first_events;
  std::vector<MahoAiRuntimeEvent> second_events;
  auto first_callback = base::BindRepeating(
      [](std::vector<MahoAiRuntimeEvent>* events, MahoAiRuntimeEvent event) {
        events->push_back(std::move(event));
      },
      &first_events);
  auto second_callback = base::BindRepeating(
      [](std::vector<MahoAiRuntimeEvent>* events, MahoAiRuntimeEvent event) {
        events->push_back(std::move(event));
      },
      &second_events);

  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          first_callback);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  adapter_->SubmitMessage("second", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          second_callback);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(fake_agent_->sends.size(), 1u);
  EXPECT_EQ(CountEvents(second_events, MahoAiRuntimeEventType::kError), 1u);

  fake_agent_->sends[0].DeliverToken("first token");
  fake_agent_->sends[0].DeliverComplete("first response");
  task_environment_.RunUntilIdle();

  EXPECT_EQ(CountEvents(first_events, MahoAiRuntimeEventType::kAssistantToken),
            1u);
  EXPECT_EQ(CountEvents(first_events, MahoAiRuntimeEventType::kTurnComplete),
            1u);
  EXPECT_EQ(CountEvents(second_events,
                        MahoAiRuntimeEventType::kAssistantToken),
            0u);
  EXPECT_EQ(CountEvents(second_events, MahoAiRuntimeEventType::kTurnComplete),
            0u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       StaleQueuedGenerationCallbackDoesNotMutateNewTurn) {
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  const uint64_t first_generation = ActiveGenerationForTesting(adapter_.get());
  ASSERT_NE(first_generation, 0u);

  fake_agent_->sends[0].DeliverComplete("first response");
  task_environment_.RunUntilIdle();

  adapter_->SubmitMessage("second", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 2u);
  events_.clear();

  DeliverQueuedTokenForTesting(adapter_.get(), first_generation, "stale");
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kAssistantToken), 0u);

  fake_agent_->sends[1].DeliverToken("second token");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kAssistantToken), 1u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, CancelTurn) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  adapter_->CancelCurrentTurn();
  EXPECT_TRUE(fake_agent_->cancel_called);

  adapter_->SubmitMessage("After cancellation",
                          maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(fake_agent_->sends.size(), 1u);

  fake_agent_->sends[0].Release();
  task_environment_.RunUntilIdle();

  adapter_->SubmitMessage("After cancellation",
                          maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(fake_agent_->sends.size(), 2u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, DestructorFreesSession) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  adapter_.reset();
  EXPECT_TRUE(fake_agent_->destroyed);
  EXPECT_TRUE(fake_agent_->session_released);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ResetCancelsFfiBeforeFreeingSession) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  fake_agent_->lifecycle_calls.clear();

  adapter_->Reset();

  ASSERT_EQ(fake_agent_->lifecycle_calls.size(), 2u);
  EXPECT_EQ(fake_agent_->lifecycle_calls[0], "cancel");
  EXPECT_EQ(fake_agent_->lifecycle_calls[1], "session_free");
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       DestructorCancelsFfiBeforeFreeingSession) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  fake_agent_->lifecycle_calls.clear();

  adapter_.reset();

  ASSERT_EQ(fake_agent_->lifecycle_calls.size(), 2u);
  EXPECT_EQ(fake_agent_->lifecycle_calls[0], "cancel");
  EXPECT_EQ(fake_agent_->lifecycle_calls[1], "session_free");
}

// R-9: once the bound kAI token becomes invalid (async profile/window switch),
// late FFI token/complete/error callbacks are revalidated on the owning
// sequence and dropped before any session-state mutation or Mojo emission.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, OtrTokenCancelsLateFfiCallbacks) {
  auto allowed = std::make_shared<bool>(true);
  auto gated_adapter = std::make_unique<MahoUnifiedAgentAdapter>(
      nullptr, nullptr,
      base::BindRepeating([](std::shared_ptr<bool> a) { return *a; }, allowed));
  SetAgentFfiForTesting(gated_adapter.get(), &kFakeFfi);

  gated_adapter->SubmitMessage(
      "Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(fake_agent_->created);

  events_.clear();

  *allowed = false;
  fake_agent_->DeliverToken("late token");
  fake_agent_->DeliverComplete("late complete");
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(events_.empty());
}

// Todo 6: an approval that needs the user is published as a kApprovalRequest
// event and parked; WebUI approve resolves the waiting permission exactly once.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, WebUiApproveResolvesOnce) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 1u);
  const std::string approval_id = FirstApprovalId();
  EXPECT_FALSE(approval_id.empty());
  EXPECT_FALSE(state->event.IsSignaled());

  adapter_->RespondToApproval(approval_id, /*approved=*/true);
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kAllowOnce);
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);

  // Duplicate / stale reply does nothing: no second resolution, no crash.
  adapter_->RespondToApproval(approval_id, /*approved=*/true);
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);
}

// Under the "prompt" (ask) policy, a sensitive desktop-agent tool with an empty
// extension id (mail_send) must route to the interactive approval broker (chat
// card) instead of a blanket deny, so the user can approve it from chat.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       PromptPolicyRoutesDesktopMailSendToApprovalBroker) {
  auto prefs = CreateSecurePrefs();  // kApprovalPolicy defaults to "prompt".
  auto adapter =
      std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(adapter.get(), &kFakeFfi);
  adapter->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                         maho_ai::mojom::InteractionMode::kDeveloper,
                         callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter.get(), "mail_send", std::string());
  // Not hard-denied: parked as a pending approval and published as a card.
  EXPECT_EQ(PendingApprovalCount(adapter.get()), 1u);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kApprovalRequest), 1u);
  EXPECT_FALSE(state->event.IsSignaled());

  const std::string approval_id = FirstApprovalId();
  ASSERT_FALSE(approval_id.empty());
  adapter->RespondToApproval(approval_id, /*approved=*/true);
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kAllowOnce);
  EXPECT_EQ(PendingApprovalCount(adapter.get()), 0u);
}

// The chat approval card for a desktop-agent mail_send is fail-closed: a deny
// reply resolves the parked request to a denial and never dispatches.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       PromptPolicyDesktopMailSendDenyIsFailClosed) {
  auto prefs = CreateSecurePrefs();  // kApprovalPolicy defaults to "prompt".
  auto adapter =
      std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(adapter.get(), &kFakeFfi);
  adapter->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                         maho_ai::mojom::InteractionMode::kDeveloper,
                         callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter.get(), "mail_send", std::string());
  EXPECT_EQ(PendingApprovalCount(adapter.get()), 1u);
  const std::string approval_id = FirstApprovalId();
  ASSERT_FALSE(approval_id.empty());

  adapter->RespondToApproval(approval_id, /*approved=*/false);
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
  EXPECT_EQ(PendingApprovalCount(adapter.get()), 0u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ConsequenceClassificationUsesCanonicalDescriptorAndValidatedContext) {
  EXPECT_EQ(ClassifyActionConsequence("input.hover", R"({"ref":1})"),
            "ordinary");
  EXPECT_EQ(ClassifyActionConsequence("navigation.navigate",
                                      R"({"url":"https://other.example"})"),
            "new_origin");
  EXPECT_EQ(ClassifyActionConsequence("input.click", R"({"ref":2})"),
            "unknown");
  EXPECT_EQ(ClassifyActionConsequence(
                "input.click",
                R"({"ref":2,"validated_action_context":{"validated":true,"consequence":"submit"}})"),
            "unknown");
  EXPECT_EQ(ClassifyActionConsequence("not.a.capability", R"({})"),
            "unclassified");
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       YoloDoesNotBypassConsequentialApprovalOrExactReceiptState) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kApprovalPolicy, "allow");
  auto adapter = std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(adapter.get(), &kFakeFfi);
  adapter->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform,
                         false, maho_ai::mojom::InteractionMode::kDeveloper,
                         callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto consequential = DriveDecision(adapter.get(), "browser_click",
                                     "browser-agent", false, "submit");
  EXPECT_FALSE(consequential->event.IsSignaled());
  ASSERT_EQ(PendingApprovalCount(adapter.get()), 1u);
  const MahoAiRuntimeEvent* request = FirstApprovalRequestEvent();
  ASSERT_TRUE(request);
  EXPECT_EQ("submit", *request->payload.FindString("consequence"));
  EXPECT_EQ("allow_all", *request->payload.FindString("approval_policy"));

  const std::string approval_id = FirstApprovalId();
  adapter->RespondToApproval(approval_id, /*approved=*/true);
  EXPECT_TRUE(consequential->event.IsSignaled());
  EXPECT_EQ(consequential->decision, PromptDecision::kAllowOnce);
  const MahoAiRuntimeEvent* result =
      LastEventOfType(MahoAiRuntimeEventType::kApprovalResult);
  ASSERT_TRUE(result);
  EXPECT_EQ("approved", *result->payload.FindString("approval_state"));
  EXPECT_EQ("allow_once", *result->payload.FindString("approval_decision"));
  EXPECT_EQ("submit", *result->payload.FindString("consequence"));

  auto ordinary = DriveDecision(adapter.get(), "browser_scroll",
                                "browser-agent", false, "ordinary");
  EXPECT_TRUE(ordinary->event.IsSignaled());
  EXPECT_EQ(ordinary->decision, PromptDecision::kAllow);
  EXPECT_EQ(PendingApprovalCount(adapter.get()), 0u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ApprovalRequestCarriesTypedMetadataAndNoRawArguments) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "browser_click", "ext_sample");
  EXPECT_FALSE(state->event.IsSignaled());

  const MahoAiRuntimeEvent* event = FirstApprovalRequestEvent();
  ASSERT_TRUE(event);
  EXPECT_EQ(event->text, "browser_click");
  const std::string* policy = event->payload.FindString("approval_policy");
  ASSERT_TRUE(policy);
  EXPECT_EQ(*policy, "prompt");
  const std::string* sensitivity = event->payload.FindString("sensitivity");
  ASSERT_TRUE(sensitivity);
  EXPECT_EQ(*sensitivity, "sensitive");
  const std::string* state_text = event->payload.FindString("approval_state");
  ASSERT_TRUE(state_text);
  EXPECT_EQ(*state_text, "pending");
  EXPECT_EQ(*event->payload.FindString("consequence"), "unknown");
  EXPECT_FALSE(
      event->payload.FindBool("page_derived_justification").value_or(true));
  EXPECT_FALSE(event->payload.FindString("related_tool_arguments"));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       PageDerivedJustificationFlagReachesApprovalRequest) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "browser_click", "ext_sample",
                             /*page_derived_justification=*/true);
  EXPECT_FALSE(state->event.IsSignaled());

  const MahoAiRuntimeEvent* event = FirstApprovalRequestEvent();
  ASSERT_TRUE(event);
  EXPECT_TRUE(
      event->payload.FindBool("page_derived_justification").value_or(false));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       WebUiApprovalResultCarriesTypedDecisionAndEmitsOnce) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  const std::string approval_id = FirstApprovalId();
  ASSERT_FALSE(approval_id.empty());

  adapter_->RespondToApproval(approval_id, /*approved=*/true);
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kAllowOnce);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kApprovalResult), 1u);
  const MahoAiRuntimeEvent& result = events_.back();
  const std::string* state_text = result.payload.FindString("approval_state");
  ASSERT_TRUE(state_text);
  EXPECT_EQ(*state_text, "approved");
  const std::string* decision = result.payload.FindString("approval_decision");
  ASSERT_TRUE(decision);
  EXPECT_EQ(*decision, "allow_once");

  adapter_->RespondToApproval(approval_id, /*approved=*/true);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kApprovalResult), 1u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       WebUiApprovalRequestCarriesTypedContractMetadataWithoutArgs) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "browser_click", "browser-agent");
  ASSERT_EQ(PendingApprovalCount(adapter_.get()), 1u);
  EXPECT_FALSE(state->event.IsSignaled());

  const MahoAiRuntimeEvent* request =
      LastEventOfType(MahoAiRuntimeEventType::kApprovalRequest);
  ASSERT_TRUE(request);
  EXPECT_EQ("browser_click", request->text);
  EXPECT_EQ("browser_click",
            *request->payload.FindString("related_tool_name"));
  EXPECT_EQ("prompt", *request->payload.FindString("approval_policy"));
  EXPECT_EQ("sensitive", *request->payload.FindString("sensitivity"));
  EXPECT_EQ("pending", *request->payload.FindString("approval_state"));
  EXPECT_EQ("unknown", *request->payload.FindString("consequence"));
  EXPECT_FALSE(request->payload.FindBool("page_derived_justification")
                   .value_or(true));
  EXPECT_FALSE(request->payload.FindString("related_tool_arguments"));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       WebUiApprovalResultCarriesDecisionMetadata) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "browser_click", "browser-agent");
  const std::string approval_id = FirstApprovalId();
  ASSERT_FALSE(approval_id.empty());

  adapter_->RespondToApproval(approval_id, /*approved=*/false);
  EXPECT_TRUE(state->event.IsSignaled());

  const MahoAiRuntimeEvent* result =
      LastEventOfType(MahoAiRuntimeEventType::kApprovalResult);
  ASSERT_TRUE(result);
  EXPECT_EQ(approval_id, *result->payload.FindString("approval_id"));
  EXPECT_FALSE(result->payload.FindBool("approved").value_or(true));
  EXPECT_EQ("denied", *result->payload.FindString("approval_state"));
  EXPECT_EQ("deny", *result->payload.FindString("approval_decision"));
  EXPECT_EQ("prompt", *result->payload.FindString("approval_policy"));
  EXPECT_EQ("sensitive", *result->payload.FindString("sensitivity"));
  EXPECT_EQ("unknown", *result->payload.FindString("consequence"));
  EXPECT_FALSE(result->payload.FindBool("page_derived_justification")
                   .value_or(true));
}

// Todo 6: WebUI deny resolves the waiting permission as deny exactly once.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, WebUiDenyResolvesAsDeny) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  const std::string approval_id = FirstApprovalId();
  ASSERT_FALSE(approval_id.empty());

  adapter_->RespondToApproval(approval_id, /*approved=*/false);
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);
}

// Todo 6: an unknown/stale approval id is a no-op and never resolves a waiter.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, StaleApprovalIdIsNoOp) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  ASSERT_EQ(PendingApprovalCount(adapter_.get()), 1u);

  adapter_->RespondToApproval("not-a-real-approval-id", /*approved=*/true);
  EXPECT_FALSE(state->event.IsSignaled());
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 1u);
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kApprovalResult), 0u);
}

// Todo 6: cancelling the turn fails a pending approval closed (deny) and
// signals the blocked worker exactly once with no execution.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, CancelTurnDeniesPendingApproval) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  ASSERT_EQ(PendingApprovalCount(adapter_.get()), 1u);

  adapter_->CancelCurrentTurn();
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       MailReadConsentChangeReachesOnlyMatchingProfileAdapters) {
  auto prefs = CreateSecurePrefs();
  auto other_prefs = CreateSecurePrefs();
  MahoUnifiedAgentAdapter adapter(prefs.get(), nullptr);
  MahoUnifiedAgentAdapter other_adapter(other_prefs.get(), nullptr);

  EXPECT_FALSE(adapter.IsMailReadAllowedForTesting());
  EXPECT_FALSE(other_adapter.IsMailReadAllowedForTesting());
  prefs->SetBoolean(maho::ai_prefs::kMailReadAllowed, true);
  MahoUnifiedAgentAdapter::NotifyMailReadConsentChanged(prefs.get());
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(adapter.IsMailReadAllowedForTesting());
  EXPECT_FALSE(other_adapter.IsMailReadAllowedForTesting());
}

// Todo 6: Reset() (panel close / session teardown) fails pending approvals
// closed so a blocked worker never hangs.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, ResetDeniesPendingApproval) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  ASSERT_EQ(PendingApprovalCount(adapter_.get()), 1u);

  adapter_->Reset();
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, DestructorDeniesPendingApproval) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  events_.clear();

  auto state = DriveDecision(adapter_.get(), "browser_click", "browser-agent");
  ASSERT_EQ(PendingApprovalCount(adapter_.get()), 1u);

  adapter_.reset();
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
}

// Todo 6: with no runtime event consumer attached, the broker is not used and
// the native dialog fallback denies (no bound browser in the test), so a
// decision still resolves closed.
TEST_F(MahoUnifiedAgentAdapterLifecycleTest, NoEventConsumerFallsBackClosed) {
  auto state = DriveDecision(adapter_.get(), "fs_read", std::string());
  EXPECT_EQ(PendingApprovalCount(adapter_.get()), 0u);
  EXPECT_TRUE(state->event.IsSignaled());
  EXPECT_EQ(state->decision, PromptDecision::kDeny);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, SummarizeIntentShapesOutboundMessage) {
  const std::string page_context =
      "[Active Page Context]\nURL: https://example.com\nTitle: Example\n\nContent:\nExample Page Content";

  adapter_->SubmitMessage(page_context,
                          maho_ai::mojom::ChatIntent::kSummarizeCurrentPage,
                          true,
                          maho_ai::mojom::InteractionMode::kAssistant,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->sends.size(), 1u);
  EXPECT_NE(fake_agent_->sends.back().message, page_context);
  EXPECT_TRUE(base::StartsWith(fake_agent_->sends.back().message,
                               "[Instructions:"));
  EXPECT_TRUE(base::EndsWith(fake_agent_->sends.back().message, page_context));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, QuizIntentShapesInitialMessageAndReusesSessionForAnswer) {
  const std::string page_context =
      "[Active Page Context]\nURL: https://example.com\nTitle: Example\n\nContent:\nExample Page Content";

  adapter_->SubmitMessage(page_context,
                          maho_ai::mojom::ChatIntent::kQuizCurrentPage,
                          true,
                          maho_ai::mojom::InteractionMode::kAssistant,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->sends.size(), 1u);
  const std::string& quiz_message = fake_agent_->sends.back().message;
  EXPECT_NE(quiz_message, page_context);
  EXPECT_TRUE(base::StartsWith(quiz_message, "[Instructions:"));
  EXPECT_TRUE(base::EndsWith(quiz_message, page_context));
  // The directive must demand question 1 outright. Telling the model to "wait"
  // made it announce the quiz and stop without ever asking anything.
  EXPECT_EQ(std::string::npos, quiz_message.find("Wait for user responses"));
  EXPECT_NE(std::string::npos, quiz_message.find("MUST contain the full text"));

  fake_agent_->DeliverComplete("Question 1: What is the topic?");
  task_environment_.RunUntilIdle();

  const std::string user_answer = "The topic is Example.";
  adapter_->SubmitMessage(user_answer,
                          maho_ai::mojom::ChatIntent::kFreeform,
                          false,
                          maho_ai::mojom::InteractionMode::kAssistant,
                          callback());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(fake_agent_->sends.size(), 2u);
  EXPECT_EQ(fake_agent_->sends.back().message, user_answer);
  EXPECT_TRUE(fake_agent_->created);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       ManagedAuthExpiresAfterSuccessfulTurnSurfacesTypedErrorOnNextTurn) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
  auto secure_adapter =
      std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);

  secure_adapter->SubmitMessage(
      "first", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();
  fake_agent_->DeliverComplete("managed ok");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 1u);

  secure_adapter->SubmitMessage(
      "second", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();
  fake_agent_->DeliverError(
      R"({"version":1,"kind":"credential_error","code":"managed_auth_unavailable"})");
  task_environment_.RunUntilIdle();

  ExpectCredentialFailure(MahoAiRuntimeErrorCode::kManagedAuthUnavailable);

  secure_adapter->Reset();
  task_environment_.RunUntilIdle();
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       PermissionWaitTimeoutReturnsDenyAndLateResolutionCannotMutate) {
  SetAgentCallWaitTimeout(base::Milliseconds(50));
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  // The posted UI-thread decision task cannot run while this synchronous call
  // blocks the same thread, so the wait elapses and permission is denied.
  MahoAgentPermissionDecision decision =
      CallOnAgentPermission(fake_agent_->sends[0].user_data, "fs_read", nullptr);
  EXPECT_EQ(decision, MahoAgentPermissionDecision_Deny);

  // Cancel the turn before draining so the stale decision task is a no-op and
  // cannot mutate the already-denied result or touch torn-down state.
  adapter_->Reset();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(decision, MahoAgentPermissionDecision_Deny);

  SetAgentCallWaitTimeout(base::Seconds(30));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       BrowserToolWaitTimeoutReturnsErrorAndLateResultCannotMutate) {
  SetAgentCallWaitTimeout(base::Milliseconds(50));
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  MahoAgentToolResult result = CallOnAgentBrowserTool(
      fake_agent_->sends[0].user_data, "read_current_page", "{}");
  ASSERT_TRUE(result.error_ptr);
  EXPECT_STREQ(result.error_ptr, "Browser tool execution timed out after 30s");
  ASSERT_TRUE(result.error_code_ptr);
  EXPECT_STREQ(result.error_code_ptr, "transport_timeout");
  EXPECT_TRUE(result.error_retryable);
  EXPECT_FALSE(result.json_ptr);
  if (result.free_fn) {
    result.free_fn(result.error_ptr);
    result.free_fn(result.error_code_ptr);
  }

  adapter_->Reset();
  task_environment_.RunUntilIdle();
  SetAgentCallWaitTimeout(base::Seconds(30));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       BrowserToolPolicyDenialIsPermanentAndOwnsTypedErrorStrings) {
  adapter_->SubmitMessage("first", maho_ai::mojom::ChatIntent::kFreeform,
                          false, maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(fake_agent_->sends.size(), 1u);

  // The callback blocks a Rust worker while the owning sequence resolves the
  // approval. This direct same-sequence test uses the bounded timeout to drive
  // the exact fail-closed denial path without waiting for wall-clock seconds.
  SetAgentCallWaitTimeout(base::Milliseconds(50));
  MahoAgentToolResult result = CallOnAgentBrowserTool(
      fake_agent_->sends[0].user_data, "browser.action.click", "{}");
  ASSERT_TRUE(result.error_ptr);
  EXPECT_STREQ(result.error_ptr,
               "Browser action denied by approval policy");
  ASSERT_TRUE(result.error_code_ptr);
  EXPECT_STREQ(result.error_code_ptr, "policy_denied");
  EXPECT_FALSE(result.error_retryable);
  EXPECT_FALSE(result.json_ptr);
  ASSERT_TRUE(result.free_fn);
  result.free_fn(result.error_ptr);
  result.free_fn(result.error_code_ptr);

  adapter_->Reset();
  task_environment_.RunUntilIdle();
  SetAgentCallWaitTimeout(base::Seconds(30));
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       StreamAccumulationExceeding2MBCapTerminatesWithSurfacedError) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  const std::string oversized(2 * 1024 * 1024 + 1, 'x');
  fake_agent_->DeliverToken(oversized);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kError), 1u);
  const MahoAiRuntimeEvent* error =
      LastEventOfType(MahoAiRuntimeEventType::kError);
  ASSERT_TRUE(error);
  EXPECT_EQ(error->text, "response exceeded 2 MB cap");
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 0u);

  fake_agent_->DeliverComplete("late");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(CountEvents(MahoAiRuntimeEventType::kTurnComplete), 0u);
  adapter_->Reset();
  task_environment_.RunUntilIdle();
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest,
       KeyDeletionRacingWithInFlightSecureStorageCallbackReturnsNullKeyNoLeakNoCrash) {
  auto prefs = CreateSecurePrefs();
  prefs->SetString(maho::ai_prefs::kProvider, "openai");
  prefs->SetString(maho::ai_prefs::kApiKey, "secret-to-be-deleted");

  auto secure_adapter =
      std::make_unique<MahoUnifiedAgentAdapter>(prefs.get(), nullptr);
  SetAgentFfiForTesting(secure_adapter.get(), &kFakeFfi);

  secure_adapter->SubmitMessage(
      "turn start", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(fake_agent_->secure_storage_cb);
  ASSERT_TRUE(fake_agent_->secure_storage_user_data);

  void* secure_storage_user_data = fake_agent_->secure_storage_user_data;

  MahoUnifiedAgentAdapter::ClearActiveBYOKKeys("openai-compatible");
  task_environment_.RunUntilIdle();

  MahoAgentSecureKey key_after_deletion = fake_agent_->secure_storage_cb(
      secure_storage_user_data, "openai");
  EXPECT_FALSE(key_after_deletion.ptr);
  EXPECT_EQ(key_after_deletion.len, 0u);
  EXPECT_FALSE(key_after_deletion.free_fn);

  secure_adapter.reset();
}
