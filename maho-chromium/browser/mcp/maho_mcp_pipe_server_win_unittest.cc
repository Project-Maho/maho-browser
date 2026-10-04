// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)

#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"

#include <windows.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/uuid.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "url/gurl.h"

namespace maho {
namespace {

// allow: SIZE_OK - Existing platform-gated GN-linked transport TU; splitting
// requires unauthorized GN membership edits. New tests avoid legacy polling.

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

  std::vector<MahoMcpSession::TabInfo> GetTabList() override {
    MahoMcpSession::TabInfo tab;
    tab.id = 1;
    tab.url = "https://example.com/";
    tab.is_active = true;
    return {tab};
  }
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
    return base::Value(base::DictValue());
  }

  // Keep the existing Windows fixture concrete against the current delegate ABI.
  PageContentResult GetPageContent(int tab_id) override {
    PageContentResult result;
    result.url = "https://example.com/";
    return result;
  }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string& query) override { return {}; }
  QuerySelectorResult QuerySelector(int tab_id, const std::string& selector) override { return {}; }
  std::string GetElementText(int tab_id, const std::string& ref) override { return {}; }
  std::string GetElementAttribute(int tab_id, const std::string& ref,
                                  const std::string& attribute) override { return {}; }
  bool WaitForSelector(int tab_id, const std::string& selector, int timeout) override { return false; }
  int CreateNewTab(const GURL& url) override { return 0; }
  bool CloseTab(int tab_id) override { return false; }
  std::vector<BookmarkInfo> SearchBookmarks(const std::string& query) override { return {}; }
  BookmarkInfo CreateBookmark(const std::string& title, const GURL& url,
                              const std::string& folder) override { return {}; }
  std::vector<HistoryEntry> SearchHistory(const std::string& query, size_t maximum) override { return {}; }
  void CaptureElementPngBase64(int tab_id, ui::AXNodeID node,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run({});
  }

  // Completes synchronously with |screenshot_b64_|; used to exercise a large
  // ordered deferred write over the byte-mode pipe.
  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string,
                              std::optional<MahoMcpCaptureMetrics>)>
          callback) override {
    std::move(callback).Run(screenshot_b64_, std::nullopt);
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
                           const ResolvedMahoMcpTarget& target,
                           StartNetworkCaptureCallback callback) override {
    pending_starts_.push_back(
        PendingStart{capture_id, tab_id, std::move(callback)});
  }
  void StopNetworkCapture(const std::string& capture_id,
                          const ResolvedMahoMcpTarget& target,
                          StopNetworkCaptureCallback callback) override {
    pending_stop_callbacks_.push_back(std::move(callback));
  }
  void CancelNetworkCapture(const std::string& capture_id) override {}

  size_t pending_start_count() const { return pending_starts_.size(); }

  // Completes the pending start at |index| (does not remove others), letting
  // tests fire deferred responses in an arbitrary (e.g. reverse) order.
  void FireStart(size_t index, bool started) {
    ASSERT_LT(index, pending_starts_.size());
    StartNetworkCaptureResult result;
    result.success = started;
    result.tab_id = 1;
    std::move(pending_starts_[index].callback).Run(result);
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

class MemoryThreadPipeDelegate : public FakePipeDelegate {
 public:
  bool ConfirmBrowserActionApproval(
      std::string_view tool_name,
      const ResolvedMahoMcpTarget& target) override {
    return approve_fetch && tool_name == "browser_same_origin_fetch" &&
           target.valid && target.tab_id == 1;
  }
  void SameOriginFetch(int tab_id, const GURL& url, const std::string& method,
                       const std::string& body, const std::string& headers,
                       SameOriginFetchCallback callback) override {
    pending.push_back(std::move(callback));
  }
  void CompleteFirst(std::string text) {
    SameOriginFetchResult result;
    result.success = true;
    result.status = 200;
    result.final_url = "https://example.com/mt";
    result.text = std::move(text);
    auto callback = std::move(pending.front());
    pending.erase(pending.begin());
    std::move(callback).Run(std::move(result));
  }
  std::vector<SameOriginFetchCallback> pending;
  bool approve_fetch = false;
};

// Wait for the exact kernel event while allowing real server write completions
// on the test sequence. The timeout is only a deadlock guard.
class MemoryThreadPipeSignal : public base::win::ObjectWatcher::Delegate {
 public:
  bool Wait(HANDLE event) {
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
    if (!watcher_.StartWatchingOnce(event, this)) {
      return false;
    }
    loop_.Run();
    return signaled_;
  }
  void OnObjectSignaled(HANDLE object) override {
    signaled_ = true;
    loop_.Quit();
  }
 private:
  base::RunLoop loop_;
  base::win::ObjectWatcher watcher_;
  bool signaled_ = false;
};

}  // namespace

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
    for (const auto& [client, id] : connection_ids_) {
      ::CloseHandle(client);
    }
    connection_ids_.clear();
    server_.reset();
    pipe_client_.Close();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    memory_thread_delegate_.reset();
    delegate_.reset();
  }

  void SetUpMemoryThreadPipe() {
    memory_thread_delegate_ = std::make_unique<MemoryThreadPipeDelegate>();
    MahoMcpSession::SetBrowserDelegate(memory_thread_delegate_.get());
    server_ = std::make_unique<MahoMcpPipeServer>(nullptr);
    server_->pipe_name_ = L"\\\\.\\pipe\\maho-memory-thread-" +
        base::UTF8ToWide(base::Uuid::GenerateRandomV4().AsLowercaseString());
    ASSERT_TRUE(server_->Start());
    pipe_client_.Set(::CreateFileW(server_->pipe_name().c_str(),
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr));
    ASSERT_TRUE(pipe_client_.is_valid());
    server_->connect_watcher_.StopWatching();
    ASSERT_TRUE(MemoryThreadPipeSignal().Wait(server_->connect_event_.get()));
    server_->OnClientConnected();
    DeliverPipe(InitializeRequest(1));
    const auto init = base::JSONReader::Read(ReadPipeLine(), base::JSON_PARSE_RFC);
    ASSERT_TRUE(init && init->is_dict() && init->GetDict().FindDict("result"));
    const auto* session_id =
        init->GetDict().FindStringByDottedPath("result.sessionInfo.id");
    ASSERT_TRUE(session_id);
    const auto lease = server_->lease_registry_->Acquire(
        1, *session_id, base::Seconds(60), /*force_steal=*/false);
    ASSERT_TRUE(lease.ok) << lease.message;
    ASSERT_TRUE(server_->lease_registry_->IsHeldBy(1, *session_id));
    DeliverPipe(FetchPipeBatch(1));
    const std::string denied_line = ReadPipeLine();
    const auto denied =
        base::JSONReader::Read(denied_line, base::JSON_PARSE_RFC);
    ASSERT_TRUE(denied && denied->is_dict()) << denied_line;
    const auto* error = denied->GetDict().FindDict("error");
    ASSERT_TRUE(error) << denied_line;
    ASSERT_EQ(error->FindInt("code"), -32008) << denied_line;
    ASSERT_TRUE(memory_thread_delegate_->pending.empty());
    memory_thread_delegate_->approve_fetch = true;
  }

  void DeliverPipe(const std::string& bytes) {
    auto* connection = server_->FindConnection(1);
    ASSERT_TRUE(connection);
    connection->read_watcher.StopWatching();
    base::win::ScopedHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    ASSERT_TRUE(event.is_valid());
    OVERLAPPED operation = {};
    operation.hEvent = event.get();
    const BOOL immediate = ::WriteFile(pipe_client_.get(), bytes.data(),
        static_cast<DWORD>(bytes.size()), nullptr, &operation);
    ASSERT_TRUE(immediate || ::GetLastError() == ERROR_IO_PENDING);
    const bool completed = MemoryThreadPipeSignal().Wait(event.get());
    if (!completed) {
      ::CancelIoEx(pipe_client_.get(), &operation);
    }
    DWORD written = 0;
    ASSERT_TRUE(::GetOverlappedResult(pipe_client_.get(), &operation, &written, TRUE));
    ASSERT_TRUE(completed);
    ASSERT_EQ(written, bytes.size());
    ASSERT_TRUE(MemoryThreadPipeSignal().Wait(connection->read_event.get()));
    server_->OnReadComplete(1);
    task_environment_.RunUntilIdle();
  }

  std::string ReadPipeChunk(DWORD maximum, HANDLE client = INVALID_HANDLE_VALUE) {
    if (client == INVALID_HANDLE_VALUE) {
      client = pipe_client_.get();
    }
    base::win::ScopedHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    CHECK(event.is_valid());
    OVERLAPPED operation = {};
    operation.hEvent = event.get();
    std::string bytes(maximum, '\0');
    const BOOL immediate = ::ReadFile(client, bytes.data(), maximum, nullptr, &operation);
    if (!immediate && ::GetLastError() != ERROR_IO_PENDING) {
      ADD_FAILURE() << "ReadFile failed: " << ::GetLastError();
      return {};
    }
    const bool completed = MemoryThreadPipeSignal().Wait(event.get());
    if (!completed) {
      ::CancelIoEx(client, &operation);
    }
    DWORD received = 0;
    const BOOL success = ::GetOverlappedResult(client, &operation, &received, TRUE);
    EXPECT_TRUE(completed && success);
    bytes.resize(success ? received : 0);
    return bytes;
  }

  std::string ReadPipeLine(HANDLE client = INVALID_HANDLE_VALUE) {
    std::string result;
    while (result.find('\n') == std::string::npos) {
      std::string chunk = ReadPipeChunk(8192, client);
      if (chunk.empty()) {
        return result;
      }
      result += chunk;
    }
    return result;
  }

  std::string FetchPipeBatch(int count) {
    std::string result;
    for (int id = 100; id < 100 + count; ++id) {
      result += base::StringPrintf(
          R"({"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":"browser_same_origin_fetch","arguments":{"tab_id":1,"url":"https://example.com/mt"}}})"
          "\n",
          id);
    }
    return result;
  }

  size_t RetainedPipeOutputBytes() {
    const auto* connection = server_->FindConnection(1);
    if (!connection) {
      return 0;
    }
    size_t bytes = 0;
    for (const auto& response : connection->write_queue) {
      bytes += response.size();
    }
    return bytes - connection->write_offset;
  }

  base::win::ScopedHandle pipe_client_;

  HANDLE ConnectToPipe() {
    server_->connect_watcher_.StopWatching();
    const uint64_t connection_id = server_->next_connection_id_;
    HANDLE client = ::CreateFileW(server_->pipe_name().c_str(),
                                  GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (client == INVALID_HANDLE_VALUE) {
      return client;
    }
    EXPECT_TRUE(MemoryThreadPipeSignal().Wait(server_->connect_event_.get()));
    server_->OnClientConnected();
    connection_ids_[client] = connection_id;
    return client;
  }

  void CloseClient(HANDLE client) {
    const auto entry = connection_ids_.find(client);
    ASSERT_NE(entry, connection_ids_.end());
    const uint64_t id = entry->second;
    auto* connection = server_->FindConnection(id);
    if (!connection) {
      ASSERT_TRUE(::CloseHandle(client));
      connection_ids_.erase(entry);
      return;
    }
    connection->read_watcher.StopWatching();
    ASSERT_TRUE(::CloseHandle(client));
    ASSERT_TRUE(MemoryThreadPipeSignal().Wait(connection->read_event.get()));
    server_->OnReadComplete(id);
    connection_ids_.erase(entry);
  }

  void SendLine(HANDLE client, const std::string& line) {
    const auto entry = connection_ids_.find(client);
    ASSERT_NE(entry, connection_ids_.end());
    auto* connection = server_->FindConnection(entry->second);
    ASSERT_TRUE(connection);
    connection->read_watcher.StopWatching();
    base::win::ScopedHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    ASSERT_TRUE(event.is_valid());
    OVERLAPPED operation = {};
    operation.hEvent = event.get();
    const BOOL immediate = ::WriteFile(client, line.data(),
        static_cast<DWORD>(line.size()), nullptr, &operation);
    ASSERT_TRUE(immediate || ::GetLastError() == ERROR_IO_PENDING);
    const bool completed = MemoryThreadPipeSignal().Wait(event.get());
    if (!completed) {
      ::CancelIoEx(client, &operation);
    }
    DWORD written = 0;
    ASSERT_TRUE(::GetOverlappedResult(client, &operation, &written, TRUE));
    ASSERT_TRUE(completed);
    ASSERT_EQ(written, line.size());
    ASSERT_TRUE(MemoryThreadPipeSignal().Wait(connection->read_event.get()));
    server_->OnReadComplete(entry->second);
    task_environment_.RunUntilIdle();
  }

  std::string ReadLine(HANDLE client) {
    return ReadPipeLine(client);
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
    EXPECT_GE(server_->active_session_count(), 1u);
    SendLine(client, InitializeRequest(init_id));
    base::Value response = ParseLine(ReadLine(client));
    EXPECT_TRUE(response.is_dict());
    EXPECT_TRUE(response.GetDict().FindDict("result"));
    return client;
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<FakePipeDelegate> delegate_;
  std::unique_ptr<MahoMcpPipeServer> server_;
  std::map<HANDLE, uint64_t> connection_ids_;
  std::unique_ptr<MemoryThreadPipeDelegate> memory_thread_delegate_;
};

// Client can connect after Start().
TEST_F(MahoMcpPipeServerWinTest, AcceptsConnection) {
  ASSERT_TRUE(server_->Start());
  EXPECT_FALSE(server_->pipe_name().empty());

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE)
      << "CreateFileW failed: " << ::GetLastError();

  EXPECT_GE(server_->active_session_count(), 1u);

  CloseClient(client);
}

// Full initialize handshake round-trips serverInfo.name = "maho-browser".
TEST_F(MahoMcpPipeServerWinTest, HandshakeReturnsServerInfo) {
  ASSERT_TRUE(server_->Start());

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE)
      << "CreateFileW failed: " << ::GetLastError();
  ASSERT_GE(server_->active_session_count(), 1u);

  SendLine(client, InitializeRequest(1));
  base::Value response = ParseLine(ReadLine(client));
  ASSERT_TRUE(response.is_dict());

  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* server_info = result->FindDict("serverInfo");
  ASSERT_TRUE(server_info);
  EXPECT_EQ(*server_info->FindString("name"), "maho-browser");

  CloseClient(client);
}

// A single async network-capture start is delivered as exactly one deferred
// response carrying the original request id.
TEST_F(MahoMcpPipeServerWinTest, DeliversOneDeferredNetworkResponse) {
  ASSERT_TRUE(server_->Start());
  HANDLE client = ConnectAndInitialize(1);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);

  SendLine(client, StartCaptureRequest(42));
  ASSERT_EQ(delegate_->pending_start_count(), 1u);

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
  const std::string* output_json = result->FindString("outputJson");
  ASSERT_TRUE(output_json);
  const auto output = base::JSONReader::ReadDict(*output_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(output);
  EXPECT_TRUE(output->FindBool("capturing").value_or(false));

  CloseClient(client);
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
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  SendLine(client_b, StartCaptureRequest(202));
  ASSERT_EQ(delegate_->pending_start_count(), 2u);

  // Fire B first, then A (reverse of arrival).
  delegate_->FireStart(1, /*started=*/true);
  delegate_->FireStart(0, /*started=*/true);

  base::Value resp_a = ParseLine(ReadLine(client_a));
  base::Value resp_b = ParseLine(ReadLine(client_b));
  ASSERT_TRUE(resp_a.is_dict());
  ASSERT_TRUE(resp_b.is_dict());
  EXPECT_EQ(resp_a.GetDict().FindInt("id").value_or(-1), 101);
  EXPECT_EQ(resp_b.GetDict().FindInt("id").value_or(-1), 202);

  CloseClient(client_a);
  CloseClient(client_b);
}

// A deferred callback that fires after its client disconnected is dropped
// without writing to another connection and without crashing.
TEST_F(MahoMcpPipeServerWinTest, LateCallbackAfterDisconnectIsDropped) {
  ASSERT_TRUE(server_->Start());

  HANDLE client = ConnectAndInitialize(1);
  ASSERT_NE(client, INVALID_HANDLE_VALUE);

  SendLine(client, StartCaptureRequest(77));
  ASSERT_EQ(delegate_->pending_start_count(), 1u);

  // Disconnect and wait for the server to observe the broken pipe and evict
  // the connection.
  CloseClient(client);
  ASSERT_EQ(server_->active_session_count(), 0u);

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
  const std::string* output_json = result->FindString("outputJson");
  ASSERT_TRUE(output_json);
  const auto output = base::JSONReader::ReadDict(*output_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(output);
  const std::string* data = output->FindString("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(data->size(), large_payload.size());
  EXPECT_EQ(*data, large_payload);

  CloseClient(client);
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
  ASSERT_EQ(delegate_->pending_start_count(), 1u);

  // Evict A; B shifts to index 0 but keeps its stable connection id.
  CloseClient(client_a);
  ASSERT_EQ(server_->active_session_count(), 1u);

  delegate_->FireStart(0, /*started=*/true);

  base::Value resp_b = ParseLine(ReadLine(client_b));
  ASSERT_TRUE(resp_b.is_dict());
  EXPECT_EQ(resp_b.GetDict().FindInt("id").value_or(-1), 555);

  CloseClient(client_b);
}

// Server teardown releases the pipe and does not crash with a live connection.
TEST_F(MahoMcpPipeServerWinTest, PipeReleasedOnDestroy) {
  ASSERT_TRUE(server_->Start());
  std::wstring pipe_name = server_->pipe_name();

  HANDLE client = ConnectToPipe();
  ASSERT_NE(client, INVALID_HANDLE_VALUE);
  ASSERT_GE(server_->active_session_count(), 1u);
  CloseClient(client);

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
  ASSERT_GE(server_->active_session_count(), 1u);

  SendLine(client, R"({"jsonrpc":"2.0","method":"tools/list","id":1})" "\n");
  std::string response = ReadLine(client);
  EXPECT_NE(response.find("-32600"), std::string::npos);

  CloseClient(client);
}

TEST_F(MahoMcpPipeServerWinTest, MemoryThreadSlowReaderAdmission) {
  ASSERT_NO_FATAL_FAILURE(SetUpMemoryThreadPipe());
  auto& pipe_delegate = *memory_thread_delegate_;
  // Given: a real initialized SID-protected byte-mode pipe with a nonreading peer.
  const std::string requests = FetchPipeBatch(17);
  // When: one kernel read carries 17 ordinary deferred requests.
  DeliverPipe(requests);
  // Then: production dispatch must reserve credit before starting each producer.
  ASSERT_GT(pipe_delegate.pending.size(), 0u);
  EXPECT_LE(pipe_delegate.pending.size(), 16u);
}

TEST_F(MahoMcpPipeServerWinTest, MemoryThreadDeferredReservationAndPartialWrites) {
  ASSERT_NO_FATAL_FAILURE(SetUpMemoryThreadPipe());
  auto& pipe_delegate = *memory_thread_delegate_;
  // Given: pipelined deferred requests whose first reply exceeds the OS pipe buffer.
  DeliverPipe(FetchPipeBatch(17));
  ASSERT_GT(pipe_delegate.pending.size(), 0u);
  const std::string text(900 * 1024, 'x');
  // When: one producer completes but the peer reads only seven bytes.
  pipe_delegate.CompleteFirst(text);
  task_environment_.RunUntilIdle();
  const size_t before = RetainedPipeOutputBytes();
  ASSERT_GT(before, text.size());
  std::string wire = ReadPipeChunk(7);
  // Then: an incomplete overlapped write remains accounted, with request credit held.
  ASSERT_EQ(wire.size(), 7u);
  EXPECT_GE(RetainedPipeOutputBytes(), before - 7);
  EXPECT_LE(pipe_delegate.pending.size(), 15u);
  wire += ReadPipeLine();
  auto response = base::JSONReader::Read(wire, base::JSON_PARSE_RFC);
  ASSERT_TRUE(response && response->is_dict());
  EXPECT_EQ(response->GetDict().FindInt("id"), 100);
  const auto* output_json =
      response->GetDict().FindStringByDottedPath("result.outputJson");
  ASSERT_TRUE(output_json);
  const auto output = base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(output && output->is_dict());
  const auto* delivered = output->GetDict().FindString("text");
  ASSERT_TRUE(delivered);
  EXPECT_EQ(*delivered, text);
}

TEST_F(MahoMcpPipeServerWinTest, MemoryThreadOrdinaryResponseTooLarge) {
  ASSERT_NO_FATAL_FAILURE(SetUpMemoryThreadPipe());
  auto& pipe_delegate = *memory_thread_delegate_;
  // Given: one ordinary request on the actual initialized named pipe.
  DeliverPipe(FetchPipeBatch(1));
  ASSERT_EQ(pipe_delegate.pending.size(), 1u) << ReadPipeLine();
  // When: its deferred completion attempts a 2 MiB ordinary reply.
  pipe_delegate.CompleteFirst(std::string(2 * 1024 * 1024, 'x'));
  task_environment_.RunUntilIdle();
  // Then: pending plus overlapped output stays bounded and the caller receives an error.
  EXPECT_LE(RetainedPipeOutputBytes(), 1024u * 1024u);
  const auto response =
      base::JSONReader::Read(ReadPipeLine(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(response && response->is_dict());
  EXPECT_EQ(response->GetDict().FindInt("id"), 100);
  EXPECT_TRUE(response->GetDict().FindDict("error"));
}

TEST_F(MahoMcpPipeServerWinTest, MemoryThreadDrainResumesAllAcceptedRequests) {
  ASSERT_NO_FATAL_FAILURE(SetUpMemoryThreadPipe());
  DeliverPipe(FetchPipeBatch(17));
  for (int id = 100; id < 117; ++id) {
    ASSERT_FALSE(memory_thread_delegate_->pending.empty());
    memory_thread_delegate_->CompleteFirst("drained");
    const auto response =
        base::JSONReader::ReadDict(ReadPipeLine(), base::JSON_PARSE_RFC);
    ASSERT_TRUE(response);
    EXPECT_EQ(response->FindInt("id"), id);
    ASSERT_TRUE(response->FindDict("result"));
    task_environment_.RunUntilIdle();
  }
  EXPECT_TRUE(memory_thread_delegate_->pending.empty());
  EXPECT_EQ(RetainedPipeOutputBytes(), 0u);
}

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)
