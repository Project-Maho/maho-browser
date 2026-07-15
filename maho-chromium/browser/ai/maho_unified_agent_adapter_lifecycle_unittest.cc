// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_unified_agent_adapter.h"

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Records the maho-agent FFI interactions the adapter drives, and lets tests
// deliver token/complete/error callbacks the way the real Rust runtime would.
struct FakeAgentState {
  bool created = false;
  bool destroyed = false;
  bool cancel_called = false;
  std::string last_session_id;
  std::string last_message;

  raw_ptr<void> token_cb_user_data = nullptr;
  void (*token_cb)(void*, const char*) = nullptr;

  raw_ptr<void> complete_cb_user_data = nullptr;
  void (*complete_cb)(void*, const char*, const char*) = nullptr;

  raw_ptr<void> error_cb_user_data = nullptr;
  void (*error_cb)(void*, const char*) = nullptr;

  void DeliverToken(const std::string& token) {
    if (token_cb) {
      token_cb(token_cb_user_data, token.c_str());
    }
  }

  void DeliverComplete(const std::string& full_text) {
    if (complete_cb) {
      complete_cb(complete_cb_user_data, full_text.c_str(), "[]");
    }
  }

  void DeliverError(const std::string& error) {
    if (error_cb) {
      error_cb(error_cb_user_data, error.c_str());
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
    MahoAgentSecureKey (*)(void*, const char*),
    void*,
    MahoAgentToolResult (*)(void*, const char*, const char*),
    void*,
    const char*) {
  if (g_fake_agent) {
    g_fake_agent->created = true;
    g_fake_agent->last_session_id = session_id ? session_id : "";
  }
  return reinterpret_cast<MahoAgentSession*>(0x2);
}

void FakeSetApprovalPolicy(MahoAgentSession*, const char*) {}

bool FakeSendMessage(MahoAgentSession*,
                     const char* message,
                     void (*on_token)(void*, const char*),
                     void (*on_complete)(void*, const char*, const char*),
                     void (*on_error)(void*, const char*),
                     void* user_data) {
  if (g_fake_agent) {
    g_fake_agent->last_message = message ? message : "";
    g_fake_agent->token_cb = on_token;
    g_fake_agent->token_cb_user_data = user_data;
    g_fake_agent->complete_cb = on_complete;
    g_fake_agent->complete_cb_user_data = user_data;
    g_fake_agent->error_cb = on_error;
    g_fake_agent->error_cb_user_data = user_data;
  }
  return true;
}

bool FakeCancel(MahoAgentSession*) {
  if (g_fake_agent) {
    g_fake_agent->cancel_called = true;
  }
  return true;
}

void FakeSessionFree(MahoAgentSession*) {
  if (g_fake_agent) {
    g_fake_agent->destroyed = true;
  }
}

const MahoAgentFfi kFakeFfi = {
    &FakeCreateSession, &FakeSetApprovalPolicy, &FakeSendMessage,
    &FakeCancel,        &FakeSessionFree,
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

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeAgentState> fake_agent_;
  std::unique_ptr<MahoUnifiedAgentAdapter> adapter_;
  std::vector<MahoAiRuntimeEvent> events_;
};

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
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, CancelTurn) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  adapter_->CancelCurrentTurn();
  EXPECT_TRUE(fake_agent_->cancel_called);
}

TEST_F(MahoUnifiedAgentAdapterLifecycleTest, DestructorFreesSession) {
  adapter_->SubmitMessage("Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
                          maho_ai::mojom::InteractionMode::kDeveloper,
                          callback());
  task_environment_.RunUntilIdle();

  adapter_.reset();
  EXPECT_TRUE(fake_agent_->destroyed);
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
