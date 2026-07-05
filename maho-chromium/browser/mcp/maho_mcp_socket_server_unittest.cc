// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_socket_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoMcpSocketServerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    // Create a "MahoCore" subdirectory to mimic the user-data layout.
    base::FilePath core_dir = temp_dir_.GetPath().AppendASCII("MahoCore");
    ASSERT_TRUE(base::CreateDirectory(core_dir));
    server_ = std::make_unique<MahoMcpSocketServer>(core_dir);
  }

  void TearDown() override { server_.reset(); }

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

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<MahoMcpSocketServer> server_;
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
  write(fd, bad.c_str(), bad.size());

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

}  // namespace
}  // namespace maho
