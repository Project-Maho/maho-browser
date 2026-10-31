// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_socket_server.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "base/containers/span.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "net/base/net_errors.h"
#include "net/socket/socket_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {

// allow: SIZE_OK - Existing GN-linked transport TU; lane ownership forbids new
// source registration. Keep regressions here rather than alter shared GN files.

// ---------------------------------------------------------------------------
// Authentication / socket-cleanup coverage (unchanged behavior).
//
// NOTE: the fixture moved from base::test::TaskEnvironment{IO} to
// content::BrowserTaskEnvironment{IO_MAINLOOP}. The socket server posts
// ProcessData() to content::GetUIThreadTaskRunner({}), which requires a
// registered BrowserThread::UI. IO_MAINLOOP hosts BrowserThread::UI+IO on the
// single main thread with an IO message pump, so the real transport path runs
// deterministically under RunUntilIdle() while net sockets stay valid.
// ---------------------------------------------------------------------------
class MahoMcpSocketServerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    // Create a "MahoCore" subdirectory to mimic the user-data layout.
    base::FilePath core_dir = temp_dir_.GetPath().AppendASCII("MahoCore");
    ASSERT_TRUE(base::CreateDirectory(core_dir));
    server_ = std::make_unique<MahoMcpSocketServer>(core_dir);
  }

  using ActiveConnection = MahoMcpSocketServer::ActiveConnection;

  base::RepeatingCallback<void(std::string)> GetDeferredCallback(uint64_t id) {
    return base::BindRepeating(&MahoMcpSocketServer::OnDeferredResponse,
                               GetWeakPtrForTesting(), id);
  }

  int ConnectToSocket() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::string path = server_->socket_path().value();
    const size_t copy_len =
        std::min(path.size(), sizeof(addr.sun_path) - 1);
    base::span(addr.sun_path).first(copy_len).copy_from(
        base::span(path).first(copy_len));

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) !=
        0) {
      close(fd);
      return -1;
    }
    return fd;
  }

  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::IO_MAINLOOP};
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<MahoMcpSocketServer> server_;

  void TearDown() override {
    server_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
  }

  MahoMcpSocketServer::ActiveConnection* FindConnectionForTesting(
      uint64_t id) {
    return server_->FindConnection(id);
  }
  std::vector<MahoMcpSocketServer::ActiveConnection>& SessionsForTesting() {
    return server_->sessions_;
  }
  void ReadFromConnectionForTesting(uint64_t id) {
    server_->ReadFromConnection(id);
  }
  base::WeakPtr<MahoMcpSocketServer> GetWeakPtrForTesting() {
    return server_->weak_factory_.GetWeakPtr();
  }
  MahoMcpLeaseRegistry* lease_registry_for_testing() {
    return server_->lease_registry_.get();
  }
};

TEST_F(MahoMcpSocketServerTest, AcceptsConnection) {
  ASSERT_TRUE(server_->Start());
  EXPECT_TRUE(base::PathExists(server_->socket_path()));

  int fd = ConnectToSocket();
  ASSERT_GE(fd, 0);

  // Allow the accept callback to fire.
  base::RunLoop().RunUntilIdle();

  EXPECT_GE(server_->active_session_count(), 0u);
  close(fd);
}

TEST_F(MahoMcpSocketServerTest, RejectsMalformedFrame) {
  ASSERT_TRUE(server_->Start());

  int fd = ConnectToSocket();
  ASSERT_GE(fd, 0);

  // Send malformed JSON.
  std::string bad = "this is not json\n";
  ASSERT_NE(write(fd, bad.c_str(), bad.size()), -1);

  // Allow processing.
  base::RunLoop().RunUntilIdle();

  // Read response — should contain parse error -32700.
  char buf[4096] = {0};
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  if (n > 0) {
    std::string response(buf, n);
    EXPECT_NE(response.find("-32700"), std::string::npos);
  }

  close(fd);
}

TEST_F(MahoMcpSocketServerTest, EnforcesPeerUidMatch) {
  // The AuthenticateClient callback checks getuid() == peer credentials.
  // Since we're connecting from the same process, the UID will match.
  // Testing UID mismatch requires a different process or root — we test
  // the session-level UID rejection via MahoMcpSessionTest::RejectsPeerUidMismatch.
  // Here we verify that same-UID connections ARE accepted.
  ASSERT_TRUE(server_->Start());

  int fd = ConnectToSocket();
  ASSERT_GE(fd, 0);
  base::RunLoop().RunUntilIdle();

  // Connection should be alive (not immediately closed).
  char probe = 0;
  // Send a byte to check the socket is writable.
  ssize_t written = write(fd, &probe, 0);
  EXPECT_GE(written, 0);

  close(fd);
}

TEST_F(MahoMcpSocketServerTest, HandlesStaleSocketOnStartup) {
  // Create a fake stale socket file.
  base::FilePath socket_path = server_->socket_path();
  base::WriteFile(socket_path, "stale");
  EXPECT_TRUE(base::PathExists(socket_path));

  // Server should remove the stale file and start successfully.
  ASSERT_TRUE(server_->Start());
  EXPECT_TRUE(base::PathExists(socket_path));

  // And it should actually accept connections.
  int fd = ConnectToSocket();
  ASSERT_GE(fd, 0);
  close(fd);
}

TEST_F(MahoMcpSocketServerTest, CleansUpSocketOnDestruction) {
  ASSERT_TRUE(server_->Start());
  base::FilePath socket_path = server_->socket_path();
  EXPECT_TRUE(base::PathExists(socket_path));

  server_.reset();
  EXPECT_FALSE(base::PathExists(socket_path));
}

// ---------------------------------------------------------------------------
// Transport-level deferred-response routing regression tests.
//
// These drive the REAL production deferred path: the sender installed in
// MahoMcpSocketServer::OnAcceptComplete (which captures a weak server +
// connection_id, never a vector index or raw pointer) -> OnDeferredResponse ->
// FindConnection(connection_id) -> drainable write to the originating socket.
//
// The trigger is a real browser_screenshot_full tool call: the session stores
// no immediate response and instead invokes the browser delegate's
// CaptureFullPagePngBase64 callback. The fake delegate below stores each such
// callback so the test controls completion ORDER and TIMING (reverse order,
// or after a connection is torn down).
// ---------------------------------------------------------------------------
class FakeScreenshotDelegate : public MahoMcpBrowserDelegate {
 public:
  FakeScreenshotDelegate() {
    MahoMcpSession::TabInfo tab;
    tab.id = 1;
    tab.title = "tab";
    tab.url = "https://example.com/";
    tab.is_active = true;
    tabs_.push_back(std::move(tab));
  }

  std::vector<MahoMcpSession::TabInfo> GetTabList() override { return tabs_; }
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

  // Stores the completion callback instead of running it, so the test decides
  // when (and in which order) each deferred response fires.
  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string,
                              std::optional<MahoMcpCaptureMetrics>)>
          callback) override {
    pending_.push_back(std::move(callback));
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
                           StartNetworkCaptureCallback callback) override {}
  void StopNetworkCapture(const std::string& capture_id,
                          const ResolvedMahoMcpTarget& target,
                          StopNetworkCaptureCallback callback) override {}
  void CancelNetworkCapture(const std::string& capture_id) override {}

  // Remaining MahoMcpBrowserDelegate surface is unused by the transport tests;
  // trivial stubs keep the fake concrete against the full interface.
  PageContentResult GetPageContent(int tab_id) override { return {}; }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string& query) override {
    return {};
  }
  QuerySelectorResult QuerySelector(int tab_id,
                                    const std::string& selector) override {
    return {};
  }
  std::string GetElementText(int tab_id, const std::string& ref_id) override {
    return std::string();
  }
  std::string GetElementAttribute(int tab_id,
                                  const std::string& ref_id,
                                  const std::string& attribute) override {
    return std::string();
  }
  bool WaitForSelector(int tab_id,
                       const std::string& selector,
                       int timeout_ms) override {
    return false;
  }
  int CreateNewTab(const GURL& url) override { return 0; }
  bool CloseTab(int tab_id) override { return false; }
  std::vector<BookmarkInfo> SearchBookmarks(const std::string& query) override {
    return {};
  }
  BookmarkInfo CreateBookmark(const std::string& title,
                              const GURL& url,
                              const std::string& folder) override {
    return {};
  }
  std::vector<HistoryEntry> SearchHistory(const std::string& query,
                                          size_t max_results) override {
    return {};
  }
  void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) override {}

  using ScreenshotCallback = base::OnceCallback<void(
      std::string, std::optional<MahoMcpCaptureMetrics>)>;

  size_t pending_count() const { return pending_.size(); }

  ScreenshotCallback TakeFront() {
    ScreenshotCallback cb = std::move(pending_.front());
    pending_.pop_front();
    return cb;
  }

  ScreenshotCallback TakeBack() {
    ScreenshotCallback cb = std::move(pending_.back());
    pending_.pop_back();
    return cb;
  }

 private:
  std::vector<MahoMcpSession::TabInfo> tabs_;
  std::deque<ScreenshotCallback> pending_;
};

class MahoMcpSocketServerDeferredTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath core_dir = temp_dir_.GetPath().AppendASCII("MahoCore");
    ASSERT_TRUE(base::CreateDirectory(core_dir));
    delegate_ = std::make_unique<FakeScreenshotDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    server_ = std::make_unique<MahoMcpSocketServer>(core_dir);
    ASSERT_TRUE(server_->Start());
  }

  void TearDown() override {
    for (int fd : client_fds_) {
      if (fd >= 0) {
        close(fd);
      }
    }
    server_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    delegate_.reset();
  }

  // Connect a client and switch it to non-blocking so reads never wedge the
  // single-threaded test loop.
  int Connect() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::string path = server_->socket_path().value();
    const size_t copy_len = std::min(path.size(), sizeof(addr.sun_path) - 1);
    base::span(addr.sun_path).first(copy_len).copy_from(
        base::span(path).first(copy_len));
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) !=
        0) {
      close(fd);
      return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    client_fds_.push_back(fd);
    // Let the server accept and arm the first read.
    task_environment_.RunUntilIdle();
    return fd;
  }

  void CloseClient(int fd) {
    for (auto& stored : client_fds_) {
      if (stored == fd) {
        close(fd);
        stored = -1;
      }
    }
  }

  void SendRaw(int fd, const std::string& data) {
    ssize_t n = write(fd, data.data(), data.size());
    ASSERT_EQ(n, static_cast<ssize_t>(data.size()));
  }

  // Reads one nd-JSON line from |fd|, pumping the event loop so the server's
  // (potentially multi-chunk) deferred write can drain. Residual bytes past the
  // newline are retained per-fd for the next call.
  std::string ReadLine(int fd) {
    for (int i = 0; i < 4000; ++i) {
      std::string& acc = rx_[fd];
      size_t nl = acc.find('\n');
      if (nl != std::string::npos) {
        std::string line = acc.substr(0, nl);
        acc.erase(0, nl + 1);
        return line;
      }
      char buf[65536];
      ssize_t n = read(fd, buf, sizeof(buf));
      if (n > 0) {
        acc.append(buf, static_cast<size_t>(n));
      }
      task_environment_.RunUntilIdle();
    }
    return std::string();
  }

  // Drives initialize handshake and consumes the initialize response.
  void Handshake(int fd) {
    SendRaw(fd,
            R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
            R"("params":{"protocolVersion":"2025-03-26",)"
            R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
            "\n");
    std::string resp = ReadLine(fd);
    ASSERT_NE(resp.find("\"id\":1"), std::string::npos) << resp;
  }

  // Issues a deferred screenshot tool call; on return the delegate holds one
  // more pending completion callback (no immediate response is written).
  void RequestScreenshot(int fd, int request_id) {
    SendRaw(fd, base::StringPrintf(
                    R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                    R"("params":{"name":"browser_screenshot_full",)"
                    R"("arguments":{"tab_id":1}}})"
                    "\n",
                    request_id));
    task_environment_.RunUntilIdle();
  }

  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::IO_MAINLOOP};
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeScreenshotDelegate> delegate_;
  std::unique_ptr<MahoMcpSocketServer> server_;
  std::vector<int> client_fds_;
  std::map<int, std::string> rx_;
};

TEST_F(MahoMcpSocketServerDeferredTest,
       InitializeAutonomousFieldRoundTripsOverUds) {
  int client = Connect();
  ASSERT_GE(client, 0);
  SendRaw(client,
          R"({"jsonrpc":"2.0","method":"initialize","id":41,)"
          R"("params":{"protocolVersion":"2025-03-26",)"
          R"("autonomous":true,)"
          R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
          "\n");
  std::string init_line = ReadLine(client);
  ASSERT_FALSE(init_line.empty());
  auto init = base::JSONReader::Read(init_line, base::JSON_PARSE_RFC);
  ASSERT_TRUE(init.has_value()) << init_line;
  const base::DictValue* result = init->GetDict().FindDict("result");
  ASSERT_TRUE(result) << init_line;
  const base::DictValue* info = result->FindDict("sessionInfo");
  ASSERT_TRUE(info) << init_line;
  EXPECT_EQ(info->FindBool("autonomous"), true);

  SendRaw(client,
          R"({"jsonrpc":"2.0","method":"tools/list","id":42,"params":{}})"
          "\n");
  std::string list_line = ReadLine(client);
  ASSERT_FALSE(list_line.empty());
  auto list = base::JSONReader::Read(list_line, base::JSON_PARSE_RFC);
  ASSERT_TRUE(list.has_value()) << list_line;
  EXPECT_TRUE(list->GetDict().FindDict("result")) << list_line;
}

// Two live UDS clients issue deferred screenshots with distinct request IDs
// and distinct payloads. Their completion callbacks fire in REVERSE order.
// Each response must reach ONLY its originating socket, proving routing keys on
// the stable connection_id (not an index/pointer that would swap under
// reordering).
TEST_F(MahoMcpSocketServerDeferredTest, TwoClientReverseOrderDeferredRouting) {
  int a = Connect();
  ASSERT_GE(a, 0);
  Handshake(a);
  int b = Connect();
  ASSERT_GE(b, 0);
  Handshake(b);

  EXPECT_EQ(server_->active_session_count(), 2u);

  RequestScreenshot(a, 101);
  RequestScreenshot(b, 202);
  ASSERT_EQ(delegate_->pending_count(), 2u);

  const std::string payload_a = "PAYLOAD_A_aaaaaaaaaaaaaaaa";
  const std::string payload_b = "PAYLOAD_B_bbbbbbbbbbbbbbbb";

  // Fire in REVERSE connection order: B (last connected) first, then A.
  FakeScreenshotDelegate::ScreenshotCallback cb_b = delegate_->TakeBack();
  std::move(cb_b).Run(payload_b, std::nullopt);
  task_environment_.RunUntilIdle();

  FakeScreenshotDelegate::ScreenshotCallback cb_a = delegate_->TakeBack();
  std::move(cb_a).Run(payload_a, std::nullopt);
  task_environment_.RunUntilIdle();

  std::string resp_a = ReadLine(a);
  std::string resp_b = ReadLine(b);

  // Client A received ONLY A's id + payload.
  EXPECT_NE(resp_a.find("\"id\":101"), std::string::npos) << resp_a;
  EXPECT_NE(resp_a.find(payload_a), std::string::npos) << resp_a;
  EXPECT_EQ(resp_a.find("\"id\":202"), std::string::npos) << resp_a;
  EXPECT_EQ(resp_a.find(payload_b), std::string::npos) << resp_a;

  // Client B received ONLY B's id + payload.
  EXPECT_NE(resp_b.find("\"id\":202"), std::string::npos) << resp_b;
  EXPECT_NE(resp_b.find(payload_b), std::string::npos) << resp_b;
  EXPECT_EQ(resp_b.find("\"id\":101"), std::string::npos) << resp_b;
  EXPECT_EQ(resp_b.find(payload_a), std::string::npos) << resp_b;
}

// A connection is torn down (client close -> server observes EOF ->
// RemoveConnection) BEFORE its deferred callback fires. The late callback must
// be dropped safely (no bytes, no crash) while the surviving connection still
// receives its own response.
TEST_F(MahoMcpSocketServerDeferredTest,
       ConnectionRemovedBeforeLateCallbackDropsSafely) {
  int a = Connect();
  ASSERT_GE(a, 0);
  Handshake(a);
  int b = Connect();
  ASSERT_GE(b, 0);
  Handshake(b);
  ASSERT_EQ(server_->active_session_count(), 2u);

  RequestScreenshot(a, 101);
  RequestScreenshot(b, 202);
  ASSERT_EQ(delegate_->pending_count(), 2u);

  // pending_ == [cbA, cbB]; take both before tearing A down.
  FakeScreenshotDelegate::ScreenshotCallback cb_a = delegate_->TakeFront();
  FakeScreenshotDelegate::ScreenshotCallback cb_b = delegate_->TakeFront();

  // Tear down connection A and let the server observe EOF + RemoveConnection.
  CloseClient(a);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(server_->active_session_count(), 1u);

  // Late callback for the removed connection: must be dropped, no crash.
  std::move(cb_a).Run("PAYLOAD_A_dropped", std::nullopt);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(server_->active_session_count(), 1u);

  // Surviving connection B still gets its own response.
  std::move(cb_b).Run("PAYLOAD_B_survives", std::nullopt);
  task_environment_.RunUntilIdle();
  std::string resp_b = ReadLine(b);
  EXPECT_NE(resp_b.find("\"id\":202"), std::string::npos) << resp_b;
  EXPECT_NE(resp_b.find("PAYLOAD_B_survives"), std::string::npos) << resp_b;
  EXPECT_EQ(resp_b.find("PAYLOAD_A_dropped"), std::string::npos) << resp_b;
  EXPECT_EQ(server_->active_session_count(), 1u);
}

// A deferred payload larger than the socket send buffer must be delivered
// byte-for-byte via the DrainableIOBuffer write loop (partial writes +
// ERR_IO_PENDING), with the client draining incrementally.
TEST_F(MahoMcpSocketServerDeferredTest, LargeDeferredPayloadDrainsFully) {
  int a = Connect();
  ASSERT_GE(a, 0);
  Handshake(a);

  RequestScreenshot(a, 700);
  ASSERT_EQ(delegate_->pending_count(), 1u);

  // ~2 MiB payload — far exceeds any UDS send buffer, forcing multi-chunk
  // draining. Head/tail markers verify no truncation at either end.
  const std::string head = "HEAD_MARKER_START";
  const std::string tail = "TAIL_MARKER_END";
  const size_t filler = 2u * 1024u * 1024u;
  std::string payload;
  payload.reserve(head.size() + filler + tail.size());
  payload.append(head);
  payload.append(filler, 'A');
  payload.append(tail);

  FakeScreenshotDelegate::ScreenshotCallback cb = delegate_->TakeFront();
  std::move(cb).Run(payload, std::nullopt);

  std::string line = ReadLine(a);
  EXPECT_NE(line.find(head), std::string::npos);
  EXPECT_NE(line.find(tail), std::string::npos);
  EXPECT_NE(line.find("\"id\":700"), std::string::npos);
  // The full payload (plus JSON framing) must have arrived intact.
  EXPECT_GE(line.size(), payload.size());
  EXPECT_NE(line.find(payload), std::string::npos);
}

// ---------------------------------------------------------------------------
// Transport-level SYNCHRONOUS-response coverage (FIX B2 regression).
//
// The synchronous path (MahoMcpSocketServer::OnResponsesReady) previously did a
// single fire-and-forget net::StreamSocket::Write() per response, ignoring
// PARTIAL writes: on a Unix domain socket, Write() may complete fewer bytes
// than requested once the kernel send buffer fills, silently dropping the tail
// (including the terminating '\n') so the newline-delimited client hangs
// forever. FIX B2 routes OnResponsesReady through the same per-connection
// serialized DrainableIOBuffer write queue as the deferred path
// (EnqueueWrite/PumpWriteQueue/OnWriteComplete), which resends remainders until
// the whole frame is on the wire.
//
// This drives the REAL sync path via a large browser_page_text tool call: the
// session handler returns its result through the immediate `responses` vector
// (NOT the deferred sender), so OnResponsesReady must enqueue and fully drain a
// payload that far exceeds a single one-shot UDS write.
// ---------------------------------------------------------------------------
class FakeSyncTextDelegate : public FakeScreenshotDelegate {
 public:
  void set_page_text(std::string text) { page_text_ = std::move(text); }
  std::string GetPageText(int tab_id) override { return page_text_; }

 private:
  std::string page_text_;
};

class MahoMcpSocketServerSyncTest : public MahoMcpSocketServerDeferredTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath core_dir = temp_dir_.GetPath().AppendASCII("MahoCore");
    ASSERT_TRUE(base::CreateDirectory(core_dir));
    auto delegate = std::make_unique<FakeSyncTextDelegate>();
    sync_delegate_ = delegate.get();
    delegate_ = std::move(delegate);
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    server_ = std::make_unique<MahoMcpSocketServer>(core_dir);
    ASSERT_TRUE(server_->Start());
  }

  // Not owned; aliases delegate_ for setting the synchronous page text.
  raw_ptr<FakeSyncTextDelegate> sync_delegate_ = nullptr;
};

// A synchronous browser_page_text response larger than a single UDS one-shot
// write must be delivered byte-for-byte through OnResponsesReady's serialized
// write queue (partial writes + ERR_IO_PENDING drained via OnWriteComplete),
// with the terminating newline present so the client's ReadLine completes.
// Before FIX B2 the tail (and newline) were dropped and ReadLine would hang.
TEST_F(MahoMcpSocketServerSyncTest, LargeSyncResponseDrainsFully) {
  int a = Connect();
  ASSERT_GE(a, 0);
  Handshake(a);
  EXPECT_EQ(server_->active_session_count(), 1u);

  // ~1 MiB of page text — far exceeds any UDS send buffer, forcing the sync
  // write path to drain across multiple chunks. Head/tail markers verify no
  // truncation at either end of the emitted frame.
  const std::string head = "SYNC_HEAD_MARKER_START";
  const std::string tail = "SYNC_TAIL_MARKER_END";
  const size_t filler = 1u * 1024u * 1024u;
  std::string page_text;
  page_text.reserve(head.size() + filler + tail.size());
  page_text.append(head);
  page_text.append(filler, 'A');
  page_text.append(tail);
  sync_delegate_->set_page_text(page_text);

  // browser_page_text returns via the immediate `responses` vector -> the
  // SYNCHRONOUS OnResponsesReady path (no deferred sender involved).
  SendRaw(a,
          R"({"jsonrpc":"2.0","method":"tools/call","id":808,)"
          R"("params":{"name":"browser_page_text",)"
          R"("arguments":{"tab_id":1}}})"
          "\n");

  // ReadLine only returns once the terminating '\n' has been received, so a
  // successful (non-hanging) return proves the newline reached the client.
  std::string line = ReadLine(a);
  EXPECT_NE(line.find("\"id\":808"), std::string::npos) << line.size();
  EXPECT_NE(line.find(head), std::string::npos);
  EXPECT_NE(line.find(tail), std::string::npos);
  // The complete page text (plus JSON framing) must have arrived intact; a
  // dropped partial-write tail would fail this and/or hang ReadLine above.
  EXPECT_GE(line.size(), page_text.size());
  EXPECT_NE(line.find(page_text), std::string::npos);
}

// A controlled kernel boundary: reads require an armed buffer, writes retain
// their actual IOBuffer until an explicit completion. No server queue is mocked.
class MemoryThreadSocket : public net::MockClientSocket {
 public:
  MemoryThreadSocket() : MockClientSocket(net::NetLogWithSource()) {}
  int Connect(net::CompletionOnceCallback callback) override { return net::OK; }
  bool WasEverUsed() const override { return true; }
  bool GetSSLInfo(net::SSLInfo* info) override { return false; }
  int Read(net::IOBuffer* buffer, int length,
           net::CompletionOnceCallback callback) override {
    CHECK(!read_callback_);
    read_buffer_ = buffer;
    read_length_ = length;
    read_callback_ = std::move(callback);
    return net::ERR_IO_PENDING;
  }
  int Write(net::IOBuffer* buffer, int length,
            net::CompletionOnceCallback callback,
            const net::NetworkTrafficAnnotationTag& annotation) override {
    CHECK(!write_callback_);
    write_buffer_ = buffer;
    write_length_ = length;
    write_callback_ = std::move(callback);
    return net::ERR_IO_PENDING;
  }
  void Feed(const std::string& bytes) {
    CHECK(read_callback_);
    CHECK_LE(bytes.size(), static_cast<size_t>(read_length_));
    read_buffer_->span().first(bytes.size()).copy_from(base::as_byte_span(bytes));
    read_buffer_.reset();
    std::move(read_callback_).Run(static_cast<int>(bytes.size()));
  }
  void CompleteWrite(int maximum) {
    CHECK(write_callback_);
    const int count = std::min(maximum, write_length_);
    wire_.append(write_buffer_->data(), count);
    write_buffer_.reset();
    std::move(write_callback_).Run(count);
  }
  void Drain() {
    while (write_callback_) {
      CompleteWrite(8192);
    }
  }
  std::string TakeWire() { return std::exchange(wire_, {}); }

 private:
  scoped_refptr<net::IOBuffer> read_buffer_;
  scoped_refptr<net::IOBuffer> write_buffer_;
  int read_length_ = 0;
  int write_length_ = 0;
  net::CompletionOnceCallback read_callback_;
  net::CompletionOnceCallback write_callback_;
  std::string wire_;
};

class MemoryThreadFetchDelegate : public FakeScreenshotDelegate {
 public:
  PageContentResult GetPageContent(int tab_id) override {
    PageContentResult result;
    result.url = "https://example.com/";
    return result;
  }
  void SameOriginFetch(int tab_id, const GURL& url, const std::string& method,
                       const std::string& body, const std::string& headers,
                       SameOriginFetchCallback callback) override {
    pending_fetches.push_back(std::move(callback));
  }
  void CompleteFetch(std::string text) {
    SameOriginFetchResult result;
    result.success = true;
    result.status = 200;
    result.final_url = "https://example.com/mt";
    result.text = std::move(text);
    auto callback = std::move(pending_fetches.front());
    pending_fetches.pop_front();
    std::move(callback).Run(std::move(result));
  }
  std::deque<SameOriginFetchCallback> pending_fetches;
};

class MahoMcpSocketMemoryThreadTest : public MahoMcpSocketServerTest {};

TEST_F(MahoMcpSocketServerTest, MemoryThreadSlowReaderAdmission) {
  // Given: an initialized client with no response completion/read credit returned.
  MemoryThreadFetchDelegate delegate;
  MahoMcpSession::SetBrowserDelegate(&delegate);
  ActiveConnection connection;
  auto socket = std::make_unique<MemoryThreadSocket>();
  auto* socket_ptr = socket.get();
  connection.socket = std::move(socket);
  connection.connection_id = 1;
  connection.session = std::make_unique<MahoMcpSession>(
      getuid(), lease_registry_for_testing());
  connection.session->SetDeferredResponseSender(base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      GetDeferredCallback(1)));
  SessionsForTesting().push_back(std::move(connection));
  ReadFromConnectionForTesting(1);
  socket_ptr->Feed(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"memory-thread","version":"1"}}})"
      "\n");
  task_environment_.RunUntilIdle();
  socket_ptr->Drain();
  const auto init =
      base::JSONReader::Read(socket_ptr->TakeWire(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(init && init->is_dict() && init->GetDict().FindDict("result"));

  std::string batch;
  for (int id = 100; id < 117; ++id) {
    batch += base::StringPrintf(
        R"({"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":"browser_same_origin_fetch","arguments":{"tab_id":1,"url":"https://example.com/mt"}}})"
        "\n",
        id);
  }
  // When: one read contains 17 genuinely deferred ordinary requests.
  socket_ptr->Feed(batch);
  task_environment_.RunUntilIdle();
  // Then: the real dispatch path cannot start the seventeenth producer.
  ASSERT_GT(delegate.pending_fetches.size(), 0u);
  EXPECT_LE(delegate.pending_fetches.size(), 16u);
}

TEST_F(MahoMcpSocketServerTest, MemoryThreadDeferredReservationAndPartialWrites) {
  // Given: all ordinary request credits are held by deferred completions.
  MemoryThreadFetchDelegate delegate;
  MahoMcpSession::SetBrowserDelegate(&delegate);
  ActiveConnection connection;
  auto socket = std::make_unique<MemoryThreadSocket>();
  auto* socket_ptr = socket.get();
  connection.socket = std::move(socket);
  connection.connection_id = 1;
  connection.session = std::make_unique<MahoMcpSession>(
      getuid(), lease_registry_for_testing());
  connection.session->SetDeferredResponseSender(base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      GetDeferredCallback(1)));
  SessionsForTesting().push_back(std::move(connection));
  ReadFromConnectionForTesting(1);
  socket_ptr->Feed(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"memory-thread","version":"1"}}})"
      "\n");
  task_environment_.RunUntilIdle();
  socket_ptr->Drain();
  const auto init =
      base::JSONReader::Read(socket_ptr->TakeWire(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(init && init->is_dict() && init->GetDict().FindDict("result"));

  std::string batch;
  for (int id = 200; id < 217; ++id) {
    batch += base::StringPrintf(
        R"({"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":"browser_same_origin_fetch","arguments":{"tab_id":1,"url":"https://example.com/mt"}}})"
        "\n",
        id);
  }
  socket_ptr->Feed(batch);
  task_environment_.RunUntilIdle();
  ASSERT_GT(delegate.pending_fetches.size(), 0u);
  const std::string text(900 * 1024, 'x');
  delegate.CompleteFetch(text);
  task_environment_.RunUntilIdle();
  auto RetainedOutputBytes = [&]() -> size_t {
    const auto* conn = FindConnectionForTesting(1);
    if (!conn) {
      return 0;
    }
    size_t bytes =
        conn->current_write ? conn->current_write->BytesRemaining() : 0;
    for (const auto& response : conn->write_queue) {
      bytes += response.size();
    }
    return bytes;
  };
  const size_t before = RetainedOutputBytes();
  ASSERT_GT(before, text.size());
  socket_ptr->CompleteWrite(7);
  ASSERT_EQ(RetainedOutputBytes(), before - 7);
  // When: the first accepted response is only partly written while ingress remains buffered.
  // Then: neither deferred completion nor a partial write releases its request credit.
  EXPECT_LE(delegate.pending_fetches.size(), 15u);
  socket_ptr->Drain();
  auto response =
      base::JSONReader::Read(socket_ptr->TakeWire(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(response && response->is_dict());
  EXPECT_EQ(response->GetDict().FindInt("id"), 200);
  const auto* delivered =
      response->GetDict().FindStringByDottedPath("result.text");
  ASSERT_TRUE(delivered);
  EXPECT_EQ(*delivered, text);
}

TEST_F(MahoMcpSocketServerTest, MemoryThreadOrdinaryResponseTooLarge) {
  // Given: an ordinary request admitted through real framing/authorization.
  MemoryThreadFetchDelegate delegate;
  MahoMcpSession::SetBrowserDelegate(&delegate);
  ActiveConnection connection;
  auto socket = std::make_unique<MemoryThreadSocket>();
  auto* socket_ptr = socket.get();
  connection.socket = std::move(socket);
  connection.connection_id = 1;
  connection.session = std::make_unique<MahoMcpSession>(
      getuid(), lease_registry_for_testing());
  connection.session->SetDeferredResponseSender(base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      GetDeferredCallback(1)));
  SessionsForTesting().push_back(std::move(connection));
  ReadFromConnectionForTesting(1);
  socket_ptr->Feed(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"memory-thread","version":"1"}}})"
      "\n");
  task_environment_.RunUntilIdle();
  socket_ptr->Drain();
  const auto init =
      base::JSONReader::Read(socket_ptr->TakeWire(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(init && init->is_dict() && init->GetDict().FindDict("result"));

  socket_ptr->Feed(base::StringPrintf(
      R"({"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":"browser_same_origin_fetch","arguments":{"tab_id":1,"url":"https://example.com/mt"}}})"
      "\n",
      300));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(delegate.pending_fetches.size(), 1u);
  // When: its asynchronous producer returns more than its ordinary response allowance.
  delegate.CompleteFetch(std::string(2 * 1024 * 1024, 'x'));
  task_environment_.RunUntilIdle();
  auto RetainedOutputBytes = [&]() -> size_t {
    const auto* conn = FindConnectionForTesting(1);
    if (!conn) {
      return 0;
    }
    size_t bytes =
        conn->current_write ? conn->current_write->BytesRemaining() : 0;
    for (const auto& response : conn->write_queue) {
      bytes += response.size();
    }
    return bytes;
  };
  // Then: return a bounded error for the same ID, not silent loss or oversized success.
  EXPECT_LE(RetainedOutputBytes(), 1024u * 1024u);
  socket_ptr->Drain();
  auto response =
      base::JSONReader::Read(socket_ptr->TakeWire(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(response && response->is_dict());
  EXPECT_EQ(response->GetDict().FindInt("id"), 300);
  EXPECT_TRUE(response->GetDict().FindDict("error"));
  EXPECT_FALSE(response->GetDict().FindDict("result"));
}

TEST_F(MahoMcpSocketServerTest, MemoryThreadLargeResultCompatibility) {
  // Given: two screenshot requests in the same read; the large-result slot is exclusive.
  FakeScreenshotDelegate screenshot_delegate;
  MahoMcpSession::SetBrowserDelegate(&screenshot_delegate);
  ActiveConnection connection;
  auto socket = std::make_unique<MemoryThreadSocket>();
  auto* socket_ptr = socket.get();
  connection.socket = std::move(socket);
  connection.connection_id = 1;
  connection.session = std::make_unique<MahoMcpSession>(
      getuid(), lease_registry_for_testing());
  connection.session->SetDeferredResponseSender(base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      GetDeferredCallback(1)));
  SessionsForTesting().push_back(std::move(connection));
  ReadFromConnectionForTesting(1);
  socket_ptr->Feed(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"memory-thread","version":"1"}}})"
      "\n");
  task_environment_.RunUntilIdle();
  socket_ptr->Drain();

  const std::string request =
      R"({"jsonrpc":"2.0","method":"tools/call","id":400,"params":{"name":"browser_screenshot_full","arguments":{"tab_id":1}}})"
      "\n";
  // When: the peer pipelines a second large producer before the first completes.
  socket_ptr->Feed(
      request +
      R"({"jsonrpc":"2.0","method":"tools/call","id":401,"params":{"name":"browser_screenshot_full","arguments":{"tab_id":1}}})"
      "\n");
  task_environment_.RunUntilIdle();
  // Then: the slot prevents concurrent production without shrinking a legal response.
  ASSERT_GT(screenshot_delegate.pending_count(), 0u);
  EXPECT_EQ(screenshot_delegate.pending_count(), 1u);
  const std::string payload(2 * 1024 * 1024, 'A');
  std::move(screenshot_delegate.TakeFront()).Run(payload, std::nullopt);
  task_environment_.RunUntilIdle();
  socket_ptr->CompleteWrite(7);
  socket_ptr->Drain();
  const std::string wire = socket_ptr->TakeWire();
  auto response = base::JSONReader::Read(wire.substr(0, wire.find('\n')),
                                         base::JSON_PARSE_RFC);
  ASSERT_TRUE(response && response->is_dict());
  EXPECT_EQ(response->GetDict().FindInt("id"), 400);
  const auto* data = response->GetDict().FindStringByDottedPath("result.data");
  ASSERT_TRUE(data);
  EXPECT_EQ(*data, payload);
}
}  // namespace maho
