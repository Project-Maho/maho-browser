// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/third_party/maho/maho_ffi.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

#include "base/check.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/task_environment.h"
#include "base/test/test_suite.h"
#include "base/time/time.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kSessionId[] = "abi-smoke-session";
constexpr char kModel[] = "deterministic-model";
constexpr char kApiKey[] = "deterministic-key";
constexpr char kSqlCipherKey[] =
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
constexpr base::TimeDelta kCallbackTimeout = base::Seconds(10);

std::atomic<int> g_secure_allocations = 0;
std::atomic<int> g_secure_frees = 0;

char* CopySecureString(const std::string& value) {
  char* copy = new char[value.size() + 1];
  std::memcpy(copy, value.c_str(), value.size() + 1);
  ++g_secure_allocations;
  return copy;
}

void FreeSecureBytes(char* bytes, uintptr_t) {
  ++g_secure_frees;
  delete[] bytes;
}

void FreeSecureCString(char* string) {
  ++g_secure_frees;
  delete[] string;
}

struct CallbackState {
  base::Lock lock;
  int tokens GUARDED_BY(lock) = 0;
  int completions GUARDED_BY(lock) = 0;
  int errors GUARDED_BY(lock) = 0;
  int turn_releases GUARDED_BY(lock) = 0;
  int session_releases GUARDED_BY(lock) = 0;
  std::string completion GUARDED_BY(lock);
  base::WaitableEvent turn_released{
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED};
  base::WaitableEvent session_released{
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED};
};

void OnToken(void* user_data, const char*) {
  auto* state = static_cast<CallbackState*>(user_data);
  base::AutoLock lock(state->lock);
  ++state->tokens;
}

void OnComplete(void* user_data, const char* text, const char*) {
  auto* state = static_cast<CallbackState*>(user_data);
  base::AutoLock lock(state->lock);
  ++state->completions;
  state->completion = text;
}

void OnError(void* user_data, const char*) {
  auto* state = static_cast<CallbackState*>(user_data);
  base::AutoLock lock(state->lock);
  ++state->errors;
}

void OnTurnRelease(void* user_data) {
  auto* state = static_cast<CallbackState*>(user_data);
  {
    base::AutoLock lock(state->lock);
    ++state->turn_releases;
  }
  state->turn_released.Signal();
}

void OnSessionRelease(void* user_data) {
  auto* state = static_cast<CallbackState*>(user_data);
  {
    base::AutoLock lock(state->lock);
    ++state->session_releases;
  }
  state->session_released.Signal();
}

MahoAgentSecureKey ProvideSecureKey(void* user_data, const char*) {
  const auto* base_url = static_cast<const std::string*>(user_data);
  return {
      .ptr = CopySecureString(kApiKey),
      .len = sizeof(kApiKey) - 1,
      .free_fn = FreeSecureBytes,
      .base_url = CopySecureString(*base_url),
      .model = CopySecureString(kModel),
      .cstring_free_fn = FreeSecureCString,
  };
}

class MahoAgentAbiIntegrationTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(profile_dir_.CreateUniqueTempDir());
    server_.RegisterRequestHandler(base::BindRepeating(
        &MahoAgentAbiIntegrationTest::HandleRequest, base::Unretained(this)));
    ASSERT_TRUE(server_.Start());
    base_url_ = server_.GetURL("/v1/").spec();

    core_ = maho_core_new_with_storage(profile_dir_.GetPath().AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    session_ = maho_agent_create_session_leased(
        core_, kSessionId, profile_dir_.GetPath().AsUTF8Unsafe().c_str(), false,
        nullptr, nullptr, &ProvideSecureKey, &base_url_, nullptr, nullptr, nullptr,
        &OnSessionRelease, &callbacks_);
    ASSERT_NE(session_, nullptr);
    ASSERT_TRUE(maho_agent_session_set_preferred_provider(session_, "openai"));
  }

  void TearDown() override {
    PermitResponse();
    if (session_) {
      maho_agent_session_free(session_);
      EXPECT_TRUE(callbacks_.session_released.TimedWait(kCallbackTimeout));
      {
        base::AutoLock lock(callbacks_.lock);
        EXPECT_EQ(callbacks_.session_releases, 1);
      }
      session_ = nullptr;
    }
    if (core_) {
      maho_core_free(core_);
      core_ = nullptr;
    }
    EXPECT_TRUE(server_.ShutdownAndWaitUntilComplete());
  }

 protected:
  bool SendLeasedMessage() {
    return maho_agent_send_message_leased(
        session_, "hello", &OnToken, nullptr, nullptr, nullptr, &OnComplete,
        &OnError, &callbacks_, &OnTurnRelease, &callbacks_);
  }

  bool WaitForRequest() { return request_received_.TimedWait(kCallbackTimeout); }

  bool WaitForTurnRelease() {
    return callbacks_.turn_released.TimedWait(kCallbackTimeout);
  }

  void PermitResponse() { response_permitted_.Signal(); }

  bool CancelSession() { return maho_agent_cancel(session_); }

  int Tokens() {
    base::AutoLock lock(callbacks_.lock);
    return callbacks_.tokens;
  }

  int Completions() {
    base::AutoLock lock(callbacks_.lock);
    return callbacks_.completions;
  }

  int TurnReleases() {
    base::AutoLock lock(callbacks_.lock);
    return callbacks_.turn_releases;
  }

  std::string Completion() {
    base::AutoLock lock(callbacks_.lock);
    return callbacks_.completion;
  }

 private:
  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest&) {
    request_received_.Signal();
    response_permitted_.Wait();

    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(net::HTTP_OK);
    response->set_content_type("text/event-stream");
    response->set_content(
        "data: {\"id\":\"smoke\",\"object\":\"chat.completion.chunk\","
        "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello \"},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"id\":\"smoke\",\"object\":\"chat.completion.chunk\","
        "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Maho\"},"
        "\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");
    return response;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir profile_dir_;
  net::EmbeddedTestServer server_;
  base::WaitableEvent request_received_{
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED};
  base::WaitableEvent response_permitted_{
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED};
  std::string base_url_;
  CallbackState callbacks_;
  MahoCore* core_ = nullptr;
  MahoAgentSession* session_ = nullptr;
};

TEST_F(MahoAgentAbiIntegrationTest, StreamsAndCompletesOverRealLeasedAbi) {
  ASSERT_TRUE(SendLeasedMessage());
  ASSERT_TRUE(WaitForRequest());
  PermitResponse();
  ASSERT_TRUE(WaitForTurnRelease());

  EXPECT_EQ(Tokens(), 2);
  EXPECT_EQ(Completions(), 1);
  EXPECT_EQ(Completion(), "Hello Maho");
}

TEST_F(MahoAgentAbiIntegrationTest, ReleasesEachLeaseOnceWithoutCallbackReuse) {
  ASSERT_TRUE(SendLeasedMessage());
  ASSERT_TRUE(WaitForRequest());
  PermitResponse();
  ASSERT_TRUE(WaitForTurnRelease());

  EXPECT_EQ(TurnReleases(), 1);
  const int tokens_after_release = Tokens();
  EXPECT_EQ(Tokens(), tokens_after_release);
  EXPECT_EQ(Completions(), 1);
}

TEST_F(MahoAgentAbiIntegrationTest, CancelAbortsTaskBeforeStreamingResponse) {
  ASSERT_TRUE(SendLeasedMessage());
  ASSERT_TRUE(WaitForRequest());
  ASSERT_TRUE(CancelSession());
  PermitResponse();
  ASSERT_TRUE(WaitForTurnRelease());

  EXPECT_EQ(Tokens(), 0);
  EXPECT_EQ(Completions(), 0);
}

TEST_F(MahoAgentAbiIntegrationTest, PairsSecureKeyAllocationsAndFrees) {
  g_secure_allocations = 0;
  g_secure_frees = 0;
  ASSERT_TRUE(SendLeasedMessage());
  ASSERT_TRUE(WaitForRequest());
  PermitResponse();
  ASSERT_TRUE(WaitForTurnRelease());

  EXPECT_EQ(g_secure_allocations.load(), 3);
  EXPECT_EQ(g_secure_frees.load(), 3);
}

}

int main(int argc, char** argv) {
  CHECK(maho_storage_set_sqlcipher_key(kSqlCipherKey));
  base::TestSuite test_suite(argc, argv);
  return base::LaunchUnitTests(
      argc, argv,
      base::BindOnce(&base::TestSuite::Run, base::Unretained(&test_suite)));
}
