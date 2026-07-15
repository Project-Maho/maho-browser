// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)

#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "url/gurl.h"

namespace maho {
namespace {

// Minimal fake browser delegate. Network-capture and screenshot calls are
// captured (or completed immediately) so tests can drive the pipe server's
// deferred-delivery path without a live browser.
class FakePipeDelegate : public MahoMcpBrowserDelegate {
 public:
  struct PendingStart {
    std::string capture_id;
    int tab_id = 0;
    StartNetworkCaptureCallback callback;
  };

  FakePipeDelegate() = default;
  ~FakePipeDelegate() override = default;

  std::vector<MahoMcpSession::TabInfo> GetTabList() override { return {}; }
  std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(
      int tab_id) override {
    return {};
  }
  std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id,
      int64_t since_ms) override {
    return {};
  }
  std::string GetPageText(int tab_id) override { return std::string(); }
  base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs) override {
    return base::Value(base::Value::Dict());
  }

  // Completes synchronously with |screenshot_b64_|; used to exercise a large
  // ordered deferred write over the byte-mode pipe.
  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(screenshot_b64_);
  }

  bool Scroll(int tab_id,
              const std::string& direction,
              int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    return false;
  }
  bool Click(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    return false;
  }
  bool Select(int tab_id,
              ui::AXNodeID ax_id,
              const std::string& value) override {
    return false;
  }
  bool Hover(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool KeyPress(int tab_id,
                const std::string& key,
                const std::vector<std::string>& modifiers) override {
    return false;
  }
  bool ActivateTab(int tab_id) override { return false; }
  bool Navigate(int tab_id, const GURL& url) override { return false; }
  bool SetViewportSize(int tab_id, int width, int height) override {
    return false;
  }

  void StartNetworkCapture(const std::string& capture_id,
                           int tab_id,
                           StartNetworkCaptureCallback callback) override {
    pending_starts_.push_back(
        PendingStart{capture_id, tab_id, std::move(callback)});
  }
  void StopNetworkCapture(const std::string& capture_id,
                          StopNetworkCaptureCallback callback) override {
    pending_stop_callbacks_.push_back(std::move(callback));
  }
  void CancelNetworkCapture(const std::string& capture_id) override {}

  size_t pending_start_count() const { return pending_starts_.size(); }

  // Completes the pending start at |index| (does not remove others), letting
  // tests fire deferred responses in an arbitrary (e.g. reverse) order.
  void FireStart(size_t index, bool started) {
    ASSERT_LT(index, pending_starts_.size());
    std::move(pending_starts_[index].callback).Run(started);
  }

  void set_screenshot_b64(std::string value) {
    screenshot_b64_ = std::move(value);
  }

 private:
  std::vector<PendingStart> pending_starts_;
  std::vector<StopNetworkCaptureCallback> pending_stop_callbacks_;
  std::string screenshot_b64_;
};

std::string InitializeRequest(int id) {
  return base::StringPrintf(
      R"({"jsonrpc":"2.0","method":"initialize","id":%d,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test","version":"0.1.0"}}})"
      "\n",
      id);
}

std::string StartCaptureRequest(int id) {
  return base::StringPrintf(
      R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
      R"("params":{"name":"browser_network_start_capture","arguments":{}}})"
      "\n",
      id);
}

std::string ScreenshotRequest(int id) {
  return base::StringPrintf(
      R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
      R"("params":{"name":"browser_screenshot_full","arguments":{}}})"
      "\n",
      id);
}

class MahoMcpPipeServerWinTest : public testing::Test {
 protected:
  void SetUp() override {
    delegate_ = std::make_unique<FakePipeDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    // A null session token bypasses DPAPI token validation so the handshake
    // succeeds without an authToken in unit tests.
    server_ = std::make_unique<MahoMcpPipeServer>(/*session_token=*/nullptr);
  }

  void TearDown() override {
    server_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    delegate_.reset();
  }

  HANDLE ConnectToPipe() {
    return ::CreateFileW(server_->pipe_name().c_str(),
                         GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  }

  // Pumps the message loop (with a bounded real-time budget) until |predicate|
  // is satisfied. Returns the final predicate value.
  bool PumpUntil(base::FunctionRef<bool()> predicate) {
    for (int attempt = 0; attempt < 600; ++attempt) {
      task_environment_.RunUntilIdle();
      if (predicate())
        return true;
      base::PlatformThread::Sleep(base::Milliseconds(5));
    }
    return predicate();
  }

  void SendLine(HANDLE client, const std::string& line) {
    DWORD written = 0;
    ASSERT_TRUE(::WriteFile(client, line.data(),
                            static_cast<DWORD>(line.size()), &written, nullptr))
        << "WriteFile failed: " << ::GetLastError();
  }

  // Drains |client| while pumping the loop until a full nd-JSON line (ending in
  // '\n') is accumulated, or the attempt budget is exhausted. Reading in a loop
  // keeps the pipe buffer draining so the server can write multi-chunk
  // responses larger than the pipe buffer.
  std::string ReadLine(HANDLE client) {
    std::string acc;
    for (int attempt = 0; attempt < 600; ++attempt) {
      task_environment_.RunUntilIdle();
      DWORD avail = 0;
      if (::PeekNamedPipe(client, nullptr, 0, nullptr, &avail, nullptr) &&
          avail > 0) {
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (::ReadFile(client, buf.data(), avail, &read, nullptr) && read > 0) {
          acc.append(buf.data(), read);
          if (acc.find('\n') != std::string::npos)
            return acc;
          continue;
        }
      }
      base::PlatformThread::Sleep(base::Milliseconds(5));
    }
    return acc;
  }

  base::Value ParseLine(const std::string& line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value()) << "Failed to parse: " << line;
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  // Connects a client and completes the initialize handshake.
  HANDLE ConnectAndInitialize(int init_id) {
    HANDLE client = ConnectToPipe();
    EXPECT_NE(client, INVALID_HANDLE_VALUE)
        << "CreateFileW failed: " << ::GetLastError();
    if (client == INVALID_HANDLE_VALUE)
      return client;
    EXPECT_TRUE(PumpUntil([&] { return server_->active_session_count() >= 1u; }));
    SendLine(client, InitializeRequest(init_id));
    base::Value response = ParseLine(ReadLine(client));
    EXPECT_TRUE(response.is_dict());
    EXPECT_TRUE(response.GetDict().FindDict("result"));
    return client;
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<FakePipeDelegate> delegate_;
  std::unique_ptr<MahoMcpPipeServer> server_;
};

// Client can connect after Start().
TEST_F(MahoMcpPipeServerWinTest, AcceptsConnection) {
  ASSERT_TRUE(server_->Start());
  EXPECT_FALSE(server_->pipe_name().empty());

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE)
      << "CreateFileW failed: " << ::GetLastError();

  EXPECT_TRUE(PumpUntil([&] { return server_->active_session_count() >= 1u; }));

  ::CloseHandle(client);
}

// Full initialize handshake round-trips serverInfo.name = "maho-browser".
TEST_F(MahoMcpPipeServerWinTest, HandshakeReturnsServerInfo) {
  ASSERT_TRUE(server_->Start());

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE)
      << "CreateFileW failed: " << ::GetLastError();
  ASSERT_TRUE(PumpUntil([&] { return server_->active_session_count() >= 1u; }));

  SendLine(client, InitializeRequest(1));
  base::Value response = ParseLine(ReadLine(client));
  ASSERT_TRUE(response.is_dict());

  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* server_info = result->FindDict("serverInfo");
  ASSERT_TRUE(server_info);
  EXPECT_EQ(*server_info->FindString("name"), "maho-browser");

  ::CloseHandle(client);
}

// A single async network-capture start is delivered as exactly one deferred
// response carrying the original request id.
TEST_F(MahoMcpPipeServerWinTest, DeliversOneDeferredNetworkResponse) {
  ASSERT_TRUE(server_->Start());
  HANDLE client = ConnectAndInitialize(1);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);

  SendLine(client, StartCaptureRequest(42));
  ASSERT_TRUE(PumpUntil([&] { return delegate_->pending_start_count() == 1u; }));

  // No immediate response — the result is deferred.
  DWORD avail = 0;
  ::PeekNamedPipe(client, nullptr, 0, nullptr, &avail, nullptr);
  EXPECT_EQ(avail, 0u);

  delegate_->FireStart(0, /*started=*/true);

  base::Value response = ParseLine(ReadLine(client));
  ASSERT_TRUE(response.is_dict());
  EXPECT_EQ(response.GetDict().FindInt("id").value_or(-1), 42);
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("capturing").value_or(false));

  ::CloseHandle(client);
}

// Two connections start captures; firing them in reverse order routes each
// deferred response back to its originating connection only.
TEST_F(MahoMcpPipeServerWinTest, TwoConnectionReverseOrderRouting) {
  ASSERT_TRUE(server_->Start());

  HANDLE client_a = ConnectAndInitialize(1);
  ASSERT_NE(client_a, INVALID_HANDLE_VALUE);
  HANDLE client_b = ConnectAndInitialize(2);
  ASSERT_NE(client_b, INVALID_HANDLE_VALUE);
  ASSERT_EQ(server_->active_session_count(), 2u);

  SendLine(client_a, StartCaptureRequest(101));
  ASSERT_TRUE(PumpUntil([&] { return delegate_->pending_start_count() == 1u; }));
  SendLine(client_b, StartCaptureRequest(202));
  ASSERT_TRUE(PumpUntil([&] { return delegate_->pending_start_count() == 2u; }));

  // Fire B first, then A (reverse of arrival).
  delegate_->FireStart(1, /*started=*/true);
  delegate_->FireStart(0, /*started=*/true);

  base::Value resp_a = ParseLine(ReadLine(client_a));
  base::Value resp_b = ParseLine(ReadLine(client_b));
  ASSERT_TRUE(resp_a.is_dict());
  ASSERT_TRUE(resp_b.is_dict());
  EXPECT_EQ(resp_a.GetDict().FindInt("id").value_or(-1), 101);
  EXPECT_EQ(resp_b.GetDict().FindInt("id").value_or(-1), 202);

  ::CloseHandle(client_a);
  ::CloseHandle(client_b);
}

// A deferred callback that fires after its client disconnected is dropped
// without writing to another connection and without crashing.
TEST_F(MahoMcpPipeServerWinTest, LateCallbackAfterDisconnectIsDropped) {
  ASSERT_TRUE(server_->Start());

  HANDLE client = ConnectAndInitialize(1);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);

  SendLine(client, StartCaptureRequest(77));
  ASSERT_TRUE(PumpUntil([&] { return delegate_->pending_start_count() == 1u; }));

  // Disconnect and wait for the server to observe the broken pipe and evict
  // the connection.
  ::CloseHandle(client);
  ASSERT_TRUE(PumpUntil([&] { return server_->active_session_count() == 0u; }));

  // Firing the stored callback must be a safe no-op (connection is gone).
  delegate_->FireStart(0, /*started=*/true);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(server_->active_session_count(), 0u);
}

// A deferred response larger than the pipe buffer is delivered intact through
// the ordered multi-chunk write queue.
TEST_F(MahoMcpPipeServerWinTest, OrderedLargeMultiChunkResponse) {
  ASSERT_TRUE(server_->Start());

  // Larger than the 64 KiB pipe buffer, forcing multiple WriteFile chunks.
  const std::string large_payload(200000, 'A');
  delegate_->set_screenshot_b64(large_payload);

  HANDLE client = ConnectAndInitialize(1);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);

  SendLine(client, ScreenshotRequest(9));

  base::Value response = ParseLine(ReadLine(client));
  ASSERT_TRUE(response.is_dict());
  EXPECT_EQ(response.GetDict().FindInt("id").value_or(-1), 9);
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const std::string* data = result->FindString("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(data->size(), large_payload.size());
  EXPECT_EQ(*data, large_payload);

  ::CloseHandle(client);
}

// After an earlier connection is removed (which shifts the connections vector),
// a later connection's deferred response still routes correctly. This would
// fail if a callback had captured a vector index rather than the stable id.
TEST_F(MahoMcpPipeServerWinTest, DeferredResponseRoutesByStableIdAfterEviction) {
  ASSERT_TRUE(server_->Start());

  HANDLE client_a = ConnectAndInitialize(1);
  ASSERT_NE(client_a, INVALID_HANDLE_VALUE);
  HANDLE client_b = ConnectAndInitialize(2);
  ASSERT_NE(client_b, INVALID_HANDLE_VALUE);
  ASSERT_EQ(server_->active_session_count(), 2u);

  // Queue B's capture before removing A.
  SendLine(client_b, StartCaptureRequest(555));
  ASSERT_TRUE(PumpUntil([&] { return delegate_->pending_start_count() == 1u; }));

  // Evict A; B shifts to index 0 but keeps its stable connection id.
  ::CloseHandle(client_a);
  ASSERT_TRUE(PumpUntil([&] { return server_->active_session_count() == 1u; }));

  delegate_->FireStart(0, /*started=*/true);

  base::Value resp_b = ParseLine(ReadLine(client_b));
  ASSERT_TRUE(resp_b.is_dict());
  EXPECT_EQ(resp_b.GetDict().FindInt("id").value_or(-1), 555);

  ::CloseHandle(client_b);
}

// Server teardown releases the pipe and does not crash with a live connection.
TEST_F(MahoMcpPipeServerWinTest, PipeReleasedOnDestroy) {
  ASSERT_TRUE(server_->Start());
  std::wstring pipe_name = server_->pipe_name();

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(PumpUntil([&] { return server_->active_session_count() >= 1u; }));
  ::CloseHandle(client);

  server_.reset();

  HANDLE client2 =
      ::CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, 0, nullptr);
  EXPECT_EQ(client2, INVALID_HANDLE_VALUE);
  if (client2 != INVALID_HANDLE_VALUE) {
    ::CloseHandle(client2);
  }
}

// A non-initialize method before the handshake is rejected with -32600.
TEST_F(MahoMcpPipeServerWinTest, RejectsMethodBeforeInitialize) {
  ASSERT_TRUE(server_->Start());

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_TRUE(PumpUntil([&] { return server_->active_session_count() >= 1u; }));

  SendLine(client, R"({"jsonrpc":"2.0","method":"tools/list","id":1})" "\n");
  std::string response = ReadLine(client);
  EXPECT_NE(response.find("-32600"), std::string::npos);

  ::CloseHandle(client);
}

}  // namespace
}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)
