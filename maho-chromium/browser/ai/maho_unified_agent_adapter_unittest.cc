// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_unified_agent_adapter.h"

#include <cstring>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

// --- Fake FFI implementation for agent ---
namespace {

struct FakeAgentState {
  bool created = false;
  bool destroyed = false;
  bool cancel_called = false;
  std::string last_session_id;
  std::string last_message;

  void* token_cb_user_data = nullptr;
  void (*token_cb)(void*, const char*) = nullptr;

  void* complete_cb_user_data = nullptr;
  void (*complete_cb)(void*, const char*, const char*) = nullptr;

  void* error_cb_user_data = nullptr;
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

}  // namespace

extern "C" {

MahoAgentSession* maho_agent_create_session(
    MahoCore* core,
    const char* session_id,
    const char* workspace_root,
    bool allow_insecure_key_storage,
    void* permission_cb,
    void* permission_user_data,
    void* secure_storage_cb,
    void* secure_storage_user_data
) {
  if (g_fake_agent) {
    g_fake_agent->created = true;
    g_fake_agent->last_session_id = session_id ? session_id : "";
  }
  return reinterpret_cast<MahoAgentSession*>(0x2);
}

void maho_agent_session_free(MahoAgentSession* session) {
  if (g_fake_agent) {
    g_fake_agent->destroyed = true;
  }
}

bool maho_agent_send_message(
    MahoAgentSession* session,
    const char* message,
    void (*on_token)(void*, const char*),
    void (*on_complete)(void*, const char*, const char*),
    void (*on_error)(void*, const char*),
    void* user_data
) {
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

bool maho_agent_cancel(MahoAgentSession* session) {
  if (g_fake_agent) {
    g_fake_agent->cancel_called = true;
  }
  return true;
}

char* maho_agent_list_tools(MahoAgentSession* session) {
  return strdup("[]");
}

}  // extern "C"

class MahoUnifiedAgentAdapterTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_agent_ = std::make_unique<FakeAgentState>();
    g_fake_agent = fake_agent_.get();
    adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(nullptr, nullptr);
  }

  void TearDown() override {
    adapter_.reset();
    g_fake_agent = nullptr;
    fake_agent_.reset();
  }

  MahoAiRuntimeAdapter::RuntimeEventCallback callback() {
    return base::BindRepeating(
        &MahoUnifiedAgentAdapterTest::OnEvent, base::Unretained(this));
  }

  void OnEvent(MahoAiRuntimeEvent event) {
    events_.push_back(std::move(event));
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeAgentState> fake_agent_;
  std::unique_ptr<MahoUnifiedAgentAdapter> adapter_;
  std::vector<MahoAiRuntimeEvent> events_;
};

TEST_F(MahoUnifiedAgentAdapterTest, FullTurnFlow) {
  adapter_->SubmitMessage(
      "Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
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

TEST_F(MahoUnifiedAgentAdapterTest, CancelTurn) {
  adapter_->SubmitMessage(
      "Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  adapter_->CancelCurrentTurn();
  EXPECT_TRUE(fake_agent_->cancel_called);
}

TEST_F(MahoUnifiedAgentAdapterTest, DestructorFreesSession) {
  adapter_->SubmitMessage(
      "Hello", maho_ai::mojom::ChatIntent::kFreeform, false,
      maho_ai::mojom::InteractionMode::kDeveloper, callback());
  task_environment_.RunUntilIdle();

  adapter_.reset();
  EXPECT_TRUE(fake_agent_->destroyed);
}
