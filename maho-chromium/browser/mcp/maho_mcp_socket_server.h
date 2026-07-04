// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SOCKET_SERVER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SOCKET_SERVER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "net/socket/stream_socket.h"

namespace net {
class UnixDomainServerSocket;
}  // namespace net

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
  struct ActiveConnection {
    std::unique_ptr<net::StreamSocket> socket;
    std::unique_ptr<MahoMcpSession> session;
  };

  static bool AuthenticateClient(
      const net::UnixDomainServerSocket::Credentials& credentials);

  void DoAccept();
  void OnAcceptComplete(int result);
  void OnReadComplete(size_t connection_index, int result);
  void ReadFromConnection(size_t connection_index);
  void RemoveConnection(size_t connection_index);
  void CleanupStaleSocket();

  base::FilePath user_data_dir_;
  base::FilePath socket_path_;
  std::unique_ptr<net::UnixDomainServerSocket> server_socket_;
  std::unique_ptr<net::StreamSocket> accepted_socket_;
  std::vector<ActiveConnection> sessions_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMcpSocketServer> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SOCKET_SERVER_H_
