// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SOCKET_SERVER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SOCKET_SERVER_H_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "net/base/io_buffer.h"
#include "net/socket/stream_socket.h"
#include "net/socket/unix_domain_server_socket_posix.h"

namespace maho {

class MahoMcpSession;

// Unix Domain Socket server for the Maho MCP (Model Context Protocol).
//
// Listens on a well-known socket path, accepts connections, authenticates
// peers via SO_PEERCRED / LOCAL_PEERCRED, and manages MahoMcpSession
// instances per connection.
//
// Lifecycle:
//   1. Construct with storage_path (parent directory for the socket file).
//   2. Call Start() to bind + listen.
//   3. Destructor (or explicit Stop()) unlinks the socket.
//
// Thread-safety: all methods must be called on the IO thread.
class MahoMcpSocketServer {
 public:
  // |storage_path| is the Maho user-data directory (e.g., ~/Library/Application
  // Support/Maho/MahoCore). The socket will be placed at
  // |storage_path|/../maho.sock (i.e., the user-data dir parent).
  explicit MahoMcpSocketServer(const base::FilePath& user_data_dir);
  ~MahoMcpSocketServer();

  MahoMcpSocketServer(const MahoMcpSocketServer&) = delete;
  MahoMcpSocketServer& operator=(const MahoMcpSocketServer&) = delete;

  // Bind the socket, apply permissions, and start accepting connections.
  // Returns true on success.
  bool Start();

  // Stop accepting and unlink the socket.
  void Stop();

  // Returns the socket path (for testing).
  base::FilePath socket_path() const { return socket_path_; }

  // Returns number of active sessions (for testing).
  size_t active_session_count() const { return sessions_.size(); }

 private:
  friend class MahoMcpSocketServerTest;
  friend class MahoMcpSocketMemoryThreadTest;

  struct ActiveConnection {
    ActiveConnection();
    ~ActiveConnection();
    ActiveConnection(ActiveConnection&&);
    ActiveConnection& operator=(ActiveConnection&&);

    std::unique_ptr<net::StreamSocket> socket;
    std::unique_ptr<MahoMcpSession> session;
    scoped_refptr<net::IOBufferWithSize> read_buf;
    uint64_t connection_id = 0;

    std::deque<std::string> write_queue;
    scoped_refptr<net::DrainableIOBuffer> current_write;
    bool write_in_flight = false;
  };

  static bool AuthenticateClient(
      const net::UnixDomainServerSocket::Credentials& credentials);

  ActiveConnection* FindConnection(uint64_t connection_id);
  void DoAccept();
  void OnAcceptComplete(int result);
  void OnReadComplete(uint64_t connection_id, int result);
  void OnResponsesReady(uint64_t connection_id,
                        std::vector<std::string> responses);
  void OnDeferredResponse(uint64_t connection_id, std::string response);
  void EnqueueWrite(uint64_t connection_id, std::string response);
  void PumpWriteQueue(uint64_t connection_id);
  void OnWriteComplete(uint64_t connection_id, int result);
  void ReadFromConnection(uint64_t connection_id);
  void RemoveConnection(uint64_t connection_id);
  void CleanupStaleSocket();

  base::FilePath user_data_dir_;
  base::FilePath socket_path_;
  std::unique_ptr<MahoMcpLeaseRegistry> lease_registry_;
  std::unique_ptr<net::UnixDomainServerSocket> server_socket_;
  std::unique_ptr<net::StreamSocket> accepted_socket_;
  std::vector<ActiveConnection> sessions_;
  uint64_t next_connection_id_ = 1;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMcpSocketServer> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SOCKET_SERVER_H_
