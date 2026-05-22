// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_opencode_acp_adapter.h"

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

// --- Fake FFI implementation (link-time stub) ---
// These replace the real maho_acp_* symbols at link time for unit tests.

// Local typedef matching the FFI header's EventCallback.
using EventCallback = void (*)(void*, const uint8_t*, uintptr_t);

namespace {

struct FakeAcpState {
  bool created = false;
  bool started = false;
  bool initialized = false;
  bool session_created = false;
  bool destroyed = false;
  bool shutdown_called = false;
  std::vector<std::string> call_order;
  std::string last_prompt;
  std::string last_session_id;
  int64_t last_permission_rpc_id = -1;
  uint32_t last_permission_outcome = 99;
  std::string last_permission_option;
  bool cancel_called = false;

  EventCallback event_cb = nullptr;
  RAW_PTR_EXCLUSION void* event_user_data = nullptr;

  void DeliverEvent(const std::string& kind, base::DictValue data) {
    if (!event_cb) return;

    base::DictValue envelope;
    envelope.Set("kind", kind);
    envelope.Set("data", std::move(data));

    std::string json;
    base::JSONWriter::Write(base::Value(std::move(envelope)), &json);

    uint32_t len = static_cast<uint32_t>(json.size());
    std::vector<uint8_t> blob(4 + json.size());
    UNSAFE_BUFFERS(std::memcpy(blob.data(), &len, 4));
    UNSAFE_BUFFERS(
        std::memcpy(blob.data() + 4, json.data(), json.size()));

    event_cb(event_user_data, blob.data(), blob.size());
  }
};

FakeAcpState* g_fake = nullptr;

}  // namespace

// C-linkage FFI stubs.
extern "C" {

MahoAcpHandle* maho_acp_create() {
  if (g_fake) g_fake->created = true;
  return reinterpret_cast<MahoAcpHandle*>(0x1);
}

void maho_acp_destroy(MahoAcpHandle*) {
  if (g_fake) {
    g_fake->destroyed = true;
    g_fake->call_order.push_back("destroy");
  }
}

int32_t maho_acp_set_event_callback(MahoAcpHandle*,
                                     EventCallback fn_ptr,
                                     void* user_data) {
  if (g_fake) {
    g_fake->event_cb = fn_ptr;
    g_fake->event_user_data = user_data;
  }
  return 0;
}

int32_t maho_acp_start(MahoAcpHandle*, const char*) {
  if (g_fake) g_fake->started = true;
  return 0;
}

int32_t maho_acp_shutdown(MahoAcpHandle*) {
  if (g_fake) {
    g_fake->shutdown_called = true;
    g_fake->call_order.push_back("shutdown");
  }
  return 0;
}

int32_t maho_acp_initialize(MahoAcpHandle*) {
  if (g_fake) g_fake->initialized = true;
  return 0;
}

int32_t maho_acp_create_session(MahoAcpHandle*) {
  if (g_fake) g_fake->session_created = true;
  return 0;
}

int32_t maho_acp_submit_prompt(MahoAcpHandle*,
                                const char* session_id,
                                const char* text) {
  if (g_fake) {
    g_fake->last_session_id = session_id ? session_id : "";
    g_fake->last_prompt = text ? text : "";
  }
  return 0;
}

int32_t maho_acp_cancel_turn(MahoAcpHandle*, const char*) {
  if (g_fake) g_fake->cancel_called = true;
  return 0;
}

int32_t maho_acp_close_session(MahoAcpHandle*, const char*) {
  return 0;
}

int32_t maho_acp_reply_permission(MahoAcpHandle*,
                                   int64_t json_rpc_id,
                                   uint32_t outcome_kind,
                                   const char* option_id) {
  if (g_fake) {
    g_fake->last_permission_rpc_id = json_rpc_id;
    g_fake->last_permission_outcome = outcome_kind;
    g_fake->last_permission_option = option_id ? option_id : "";
  }
  return 0;
}

}  // extern "C"

class MahoOpenCodeAcpAdapterTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_ = std::make_unique<FakeAcpState>();
    g_fake = fake_.get();
    adapter_ = std::make_unique<MahoOpenCodeAcpAdapter>(nullptr, nullptr);
  }

  void TearDown() override {
    adapter_.reset();
    g_fake = nullptr;
    fake_.reset();
  }

  void SubmitAndDriveToReady(const std::string& message) {
    adapter_->SubmitMessage(
        message, maho_ai::mojom::ChatIntent::kFreeform, false,
        maho_ai::mojom::InteractionMode::kDeveloper, callback());
    task_environment_.RunUntilIdle();

    base::DictValue init_data;
    fake_->DeliverEvent("InitializeOk", std::move(init_data));
    task_environment_.RunUntilIdle();

    base::DictValue session_data;
    session_data.Set("session_id", "test-session-1");
    fake_->DeliverEvent("SessionCreated", std::move(session_data));
    task_environment_.RunUntilIdle();
  }

  MahoAiRuntimeAdapter::RuntimeEventCallback callback() {
    return base::BindRepeating(
        &MahoOpenCodeAcpAdapterTest::OnEvent, base::Unretained(this));
  }

  void OnEvent(MahoAiRuntimeEvent event) {
    events_.push_back(std::move(event));
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeAcpState> fake_;
  std::unique_ptr<MahoOpenCodeAcpAdapter> adapter_;
  std::vector<MahoAiRuntimeEvent> events_;
};

TEST_F(MahoOpenCodeAcpAdapterTest, FullTurnFlow) {
  SubmitAndDriveToReady("Hello");

  EXPECT_TRUE(fake_->started);
  EXPECT_TRUE(fake_->initialized);
  EXPECT_TRUE(fake_->session_created);
  EXPECT_EQ(fake_->last_prompt, "Hello");

  base::DictValue chunk1;
  chunk1.Set("text", "Hi ");
  fake_->DeliverEvent("AgentMessageChunk", std::move(chunk1));
  task_environment_.RunUntilIdle();

  base::DictValue chunk2;
  chunk2.Set("text", "there!");
  fake_->DeliverEvent("AgentMessageChunk", std::move(chunk2));
  task_environment_.RunUntilIdle();

  base::DictValue complete;
  complete.Set("stop_reason", "end_turn");
  fake_->DeliverEvent("PromptComplete", std::move(complete));
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

TEST_F(MahoOpenCodeAcpAdapterTest, ToolCallUpdateMapsCorrectly) {
  SubmitAndDriveToReady("test");

  base::DictValue tool_data;
  tool_data.Set("tool_name", "read_file");
  tool_data.Set("call_id", "c1");
  tool_data.Set("status", "completed");
  tool_data.Set("output", "file contents");
  fake_->DeliverEvent("ToolCallUpdate", std::move(tool_data));
  task_environment_.RunUntilIdle();

  bool found = false;
  for (const auto& ev : events_) {
    if (ev.type == MahoAiRuntimeEventType::kToolResult) {
      found = true;
      EXPECT_EQ(*ev.payload.FindString("tool_name"), "read_file");
      EXPECT_EQ(*ev.payload.FindBool("success"), true);
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(MahoOpenCodeAcpAdapterTest, PermissionRequestAndReply) {
  SubmitAndDriveToReady("test");

  base::DictValue perm_data;
  perm_data.Set("json_rpc_id", 42);
  perm_data.Set("description", "Allow file write?");
  fake_->DeliverEvent("PermissionRequest", std::move(perm_data));
  task_environment_.RunUntilIdle();

  std::string approval_id;
  for (const auto& ev : events_) {
    if (ev.type == MahoAiRuntimeEventType::kApprovalRequest) {
      const std::string* aid = ev.payload.FindString("approval_id");
      ASSERT_TRUE(aid);
      approval_id = *aid;
    }
  }
  ASSERT_FALSE(approval_id.empty());

  adapter_->RespondToApproval(approval_id, true);

  EXPECT_EQ(fake_->last_permission_rpc_id, 42);
  EXPECT_EQ(fake_->last_permission_outcome, 1u);
}

TEST_F(MahoOpenCodeAcpAdapterTest, PromptErrProducesError) {
  SubmitAndDriveToReady("test");

  base::DictValue err_data;
  err_data.Set("message", "rate limited");
  fake_->DeliverEvent("PromptErr", std::move(err_data));
  task_environment_.RunUntilIdle();

  bool found = false;
  for (const auto& ev : events_) {
    if (ev.type == MahoAiRuntimeEventType::kError && ev.text == "rate limited") {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(MahoOpenCodeAcpAdapterTest, DestructMidTurnCallsShutdownAndDestroy) {
  SubmitAndDriveToReady("test");
  adapter_.reset();

  EXPECT_TRUE(fake_->shutdown_called);
  EXPECT_TRUE(fake_->destroyed);
}

TEST_F(MahoOpenCodeAcpAdapterTest, DestructorCallsShutdownBeforeDestroy) {
  SubmitAndDriveToReady("test");
  fake_->call_order.clear();
  adapter_.reset();

  ASSERT_GE(fake_->call_order.size(), 2u);
  EXPECT_EQ(fake_->call_order[0], "shutdown");
  EXPECT_EQ(fake_->call_order[1], "destroy");
}

TEST_F(MahoOpenCodeAcpAdapterTest, NoCallbackAfterDestroy) {
  // Verify that after destruction, firing the callback does not crash
  // and no events are delivered (weak_ptr is invalidated).
  SubmitAndDriveToReady("test");

  auto* cb = fake_->event_cb;
  auto* ud = fake_->event_user_data;
  size_t events_before = events_.size();

  adapter_.reset();
  ASSERT_TRUE(fake_->destroyed);

  // Fire a late event — must not crash and must not deliver.
  base::DictValue envelope;
  envelope.Set("kind", "AgentMessageChunk");
  base::DictValue data;
  data.Set("text", "ghost");
  envelope.Set("data", std::move(data));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(envelope)), &json);
  uint32_t len = static_cast<uint32_t>(json.size());
  std::vector<uint8_t> blob(4 + json.size());
  UNSAFE_BUFFERS(std::memcpy(blob.data(), &len, 4));
  UNSAFE_BUFFERS(
      std::memcpy(blob.data() + 4, json.data(), json.size()));

  cb(ud, blob.data(), blob.size());
  task_environment_.RunUntilIdle();

  EXPECT_EQ(events_.size(), events_before);
}


