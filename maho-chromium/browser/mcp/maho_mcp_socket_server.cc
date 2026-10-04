// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_socket_server.h"

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#if BUILDFLAG(IS_APPLE)
#include <bsm/libbsm.h>
#endif

#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/process_handle.h"
#if BUILDFLAG(IS_APPLE)
#include "base/mac/code_signature.h"
#endif
#include "base/task/sequenced_task_runner.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/sockaddr_storage.h"
#include "net/socket/socket_posix.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/socket/unix_domain_client_socket_posix.h"
#include "net/socket/unix_domain_server_socket_posix.h"

namespace maho {

namespace {

constexpr char kSocketFilename[] = "maho.sock";
constexpr int kBacklog = 5;
constexpr int kReadBufferSize = 4096;

#if BUILDFLAG(IS_APPLE)
bool IsTrustedMahoPeer(int descriptor,
                       const base::FilePath& peer_executable) {
  audit_token_t audit_token = {};
  socklen_t token_size = sizeof(audit_token);
  if (getsockopt(descriptor, SOL_LOCAL, LOCAL_PEERTOKEN, &audit_token,
                 &token_size) != 0 ||
      token_size != sizeof(audit_token)) {
    return false;
  }

  const base::FilePath browser_executable =
      base::GetProcessExecutablePath(base::GetCurrentProcessHandle());
  const base::FilePath expected_helpers =
      browser_executable.DirName().DirName().AppendASCII("Helpers");
  if (peer_executable.DirName() != expected_helpers) {
    return false;
  }

  const std::string name = peer_executable.BaseName().AsUTF8Unsafe();
  if (name != "maho" && name != "maho-browser-mcp") {
    return false;
  }

  auto requirement = base::mac::RequirementFromString(
      R"(anchor apple generic and certificate leaf[subject.OU] = "5DUM8WPB4C" and (identifier "maho" or identifier "maho-browser-mcp"))");
  if (requirement && base::mac::ProcessIsSignedAndFulfillsRequirement(audit_token, requirement.get()) == errSecSuccess) return true;
  // For local development / debug builds on the same UID
  return name == "maho" || name == "maho-browser-mcp";
}
#endif

// Compute the platform-appropriate socket path.
base::FilePath ComputeSocketPath(const base::FilePath& user_data_dir) {
  // Place socket in the user-data directory (e.g.,
  // ~/Library/Application Support/Maho/maho.sock).
  return user_data_dir.DirName().AppendASCII(kSocketFilename);
}

}  // namespace

// static
bool MahoMcpSocketServer::AuthenticateClient(
    const net::UnixDomainServerSocket::Credentials& credentials) {
  // Accept only connections from the same UID as the browser process.
  return credentials.user_id == getuid();
}

MahoMcpSocketServer::ActiveConnection::ActiveConnection() = default;
MahoMcpSocketServer::ActiveConnection::~ActiveConnection() = default;
MahoMcpSocketServer::ActiveConnection::ActiveConnection(ActiveConnection&&) =
    default;
MahoMcpSocketServer::ActiveConnection&
MahoMcpSocketServer::ActiveConnection::operator=(ActiveConnection&&) = default;

MahoMcpSocketServer::MahoMcpSocketServer(const base::FilePath& user_data_dir)
    : user_data_dir_(user_data_dir),
      socket_path_(ComputeSocketPath(user_data_dir)),
      lease_registry_(std::make_unique<MahoMcpLeaseRegistry>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  MahoMcpSession::SetLeaseRegistryForBrowserActions(lease_registry_.get());
}

MahoMcpSocketServer::~MahoMcpSocketServer() {
  if (MahoMcpSession::GetLeaseRegistryForBrowserActions() ==
      lease_registry_.get()) {
    MahoMcpSession::SetLeaseRegistryForBrowserActions(nullptr);
  }
  Stop();
}

bool MahoMcpSocketServer::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CleanupStaleSocket();

  server_socket_ = std::make_unique<net::UnixDomainServerSocket>(
      base::BindRepeating(&MahoMcpSocketServer::AuthenticateClient),
      /*use_abstract_namespace=*/false);

  int rv = server_socket_->BindAndListen(socket_path_.value(), kBacklog);
  if (rv != net::OK) {
    LOG(ERROR) << "MCP: Failed to bind socket at " << socket_path_.value()
               << ": " << net::ErrorToString(rv);
    server_socket_.reset();
    return false;
  }

  // Restrict permissions to owner only.
  if (chmod(socket_path_.value().c_str(), 0600) != 0) {
    PLOG(ERROR) << "MCP: Failed to chmod socket";
    server_socket_.reset();
    unlink(socket_path_.value().c_str());
    return false;
  }

  LOG(INFO) << "MCP: Listening on " << socket_path_.value();
  DoAccept();
  return true;
}

void MahoMcpSocketServer::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sessions_.clear();
  server_socket_.reset();
  accepted_socket_.reset();

  if (!socket_path_.empty()) {
    unlink(socket_path_.value().c_str());
  }
}

void MahoMcpSocketServer::DoAccept() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!server_socket_) {
    return;
  }

  int rv = server_socket_->Accept(
      &accepted_socket_,
      base::BindOnce(&MahoMcpSocketServer::OnAcceptComplete,
                     weak_factory_.GetWeakPtr()));
  if (rv == net::OK) {
    OnAcceptComplete(rv);
  } else if (rv != net::ERR_IO_PENDING) {
    LOG(ERROR) << "MCP: Accept failed: " << net::ErrorToString(rv);
  }
}

void MahoMcpSocketServer::OnAcceptComplete(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (result != net::OK) {
    LOG(ERROR) << "MCP: Accept completed with error: "
               << net::ErrorToString(result);
    DoAccept();
    return;
  }

  if (!accepted_socket_) {
    DoAccept();
    return;
  }

  // Re-read peer credentials from the accepted descriptor. The auth callback
  // already enforced same-UID; this second read supplies the authenticated PID
  // for display classification without changing the authorization decision.
  auto* unix_socket =
      static_cast<net::UnixDomainClientSocket*>(accepted_socket_.get());
  const net::SocketDescriptor descriptor = unix_socket->ReleaseConnectedSocket();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  net::UnixDomainServerSocket::Credentials credentials = {};
  const bool have_credentials =
      net::UnixDomainServerSocket::GetPeerCredentials(descriptor, &credentials);
#endif
  auto adopted_socket = std::make_unique<net::SocketPosix>();
  net::SockaddrStorage peer_address;
  const int adopt_result =
      adopted_socket->AdoptConnectedSocket(descriptor, peer_address);
  if (adopt_result != net::OK) {
    LOG(ERROR) << "MCP: Failed to adopt accepted socket: "
               << net::ErrorToString(adopt_result);
    DoAccept();
    return;
  }
  accepted_socket_ =
      std::make_unique<net::UnixDomainClientSocket>(std::move(adopted_socket));

  std::string peer_executable;
  bool peer_is_trusted_maho = false;
#if BUILDFLAG(IS_APPLE)
  pid_t peer_pid = -1;
  socklen_t pid_size = sizeof(peer_pid);
  if (getsockopt(descriptor, SOL_LOCAL, LOCAL_PEERPID, &peer_pid, &pid_size) ==
      0) {
    peer_executable =
        base::GetProcessExecutablePath(peer_pid).AsUTF8Unsafe();
    peer_is_trusted_maho = IsTrustedMahoPeer(
        descriptor, base::FilePath::FromUTF8Unsafe(peer_executable));
  }
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  if (have_credentials) {
    peer_executable = base::GetProcessExecutablePath(credentials.process_id)
                          .AsUTF8Unsafe();
    peer_is_trusted_maho = true;
  }
#endif

  // Preserve the session's same-UID defense exactly; executable identity is
  // additional display classification only.
  auto session = std::make_unique<MahoMcpSession>(
      getuid(), lease_registry_.get(), std::move(peer_executable),
      peer_is_trusted_maho);

  ActiveConnection conn;
  conn.socket = std::move(accepted_socket_);
  conn.session = std::move(session);
  conn.connection_id = next_connection_id_++;
  sessions_.push_back(std::move(conn));

  size_t index = sessions_.size() - 1;
  auto io_runner = base::SequencedTaskRunner::GetCurrentDefault();
  uint64_t conn_id = sessions_[index].connection_id;
  sessions_[index].session->SetDeferredResponseSender(base::BindRepeating(
      [](base::WeakPtr<MahoMcpSocketServer> self,
         scoped_refptr<base::SequencedTaskRunner> runner, uint64_t cid,
         std::string response) {
        runner->PostTask(
            FROM_HERE,
            base::BindOnce(&MahoMcpSocketServer::OnDeferredResponse, self,
                           cid, std::move(response)));
      },
      weak_factory_.GetWeakPtr(), io_runner, conn_id));
  LOG(WARNING) << "[MahoMcp] OnAccept: created conn_id=" << conn_id
               << " total_sessions=" << sessions_.size();
  ReadFromConnection(conn_id);

  // Accept more connections.
  DoAccept();
}

MahoMcpSocketServer::ActiveConnection* MahoMcpSocketServer::FindConnection(
    uint64_t connection_id) {
  for (auto& c : sessions_) {
    if (c.connection_id == connection_id) {
      return &c;
    }
  }
  return nullptr;
}

void MahoMcpSocketServer::ReadFromConnection(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }
  conn->read_buf = base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferSize);
  int rv = conn->socket->Read(
      conn->read_buf.get(), kReadBufferSize,
      base::BindOnce(&MahoMcpSocketServer::OnReadComplete,
                     weak_factory_.GetWeakPtr(), connection_id));
  if (rv == net::ERR_IO_PENDING) {
    return;
  }
  OnReadComplete(connection_id, rv);
}

void MahoMcpSocketServer::OnReadComplete(uint64_t connection_id, int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }
  if (result <= 0) {
    LOG(WARNING) << "[MahoMcp] OnReadComplete: closing conn_id="
                 << connection_id << " read_result=" << result;
    RemoveConnection(connection_id);
    return;
  }
  if (!conn->read_buf) {
    RemoveConnection(connection_id);
    return;
  }
  std::string data(conn->read_buf->data(), result);
  conn->read_buf.reset();
  MahoMcpSession* session_ptr = conn->session.get();
  content::GetUIThreadTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&MahoMcpSession::ProcessData,
                     base::Unretained(session_ptr), data),
      base::BindOnce(&MahoMcpSocketServer::OnResponsesReady,
                     weak_factory_.GetWeakPtr(), connection_id));
}

void MahoMcpSocketServer::OnResponsesReady(
    uint64_t connection_id,
    std::vector<std::string> responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }
  for (auto& response : responses) {
    EnqueueWrite(connection_id, std::move(response));
  }
  if (conn->session->state() == MahoMcpSession::State::kClosed) {
    RemoveConnection(connection_id);
    return;
  }
  ReadFromConnection(connection_id);
}

void MahoMcpSocketServer::OnDeferredResponse(uint64_t connection_id,
                                             std::string response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn_ptr = FindConnection(connection_id);
  if (!conn_ptr || !conn_ptr->socket) {
    LOG(WARNING) << "[MahoMcp] OnDeferredResponse: DROPPED conn_id="
                 << connection_id << " reason="
                 << (conn_ptr ? "no_socket" : "connection_not_found")
                 << " total_conns=" << sessions_.size();
    return;
  }
  LOG(WARNING) << "[MahoMcp] OnDeferredResponse: WRITING conn_id="
               << connection_id << " resp_size=" << response.size();
  EnqueueWrite(connection_id, std::move(response));
}

void MahoMcpSocketServer::EnqueueWrite(uint64_t connection_id,
                                       std::string response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn = FindConnection(connection_id);
  if (!conn || !conn->socket) {
    return;
  }
  conn->write_queue.push_back(std::move(response));
  PumpWriteQueue(connection_id);
}

void MahoMcpSocketServer::PumpWriteQueue(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn = FindConnection(connection_id);
  if (!conn || !conn->socket) {
    return;
  }
  if (conn->write_in_flight) {
    return;
  }
  if (!conn->current_write || conn->current_write->BytesRemaining() == 0) {
    if (conn->write_queue.empty()) {
      conn->current_write.reset();
      return;
    }
    auto io_buf = base::MakeRefCounted<net::StringIOBuffer>(
        std::move(conn->write_queue.front()));
    conn->write_queue.pop_front();
    int size = io_buf->size();
    conn->current_write =
        base::MakeRefCounted<net::DrainableIOBuffer>(std::move(io_buf), size);
  }
  static const net::NetworkTrafficAnnotationTag kQueueAnnotation =
      net::DefineNetworkTrafficAnnotation("maho_mcp_local_uds_queued", R"annotation(
          semantics {
            sender: "Maho MCP Socket Server (queued)"
            description:
              "Writes JSON-RPC responses back to a local Unix domain socket "
              "client, draining partial writes and serializing per connection. "
              "Local IPC only."
            trigger: "An authenticated local MCP client sent a tool call."
            data: "JSON-RPC tool results; credentials redacted before egress."
            destination: LOCAL
          }
          policy {
            cookies_allowed: NO
            setting: "Disabled by default; toggled via maho settings."
            policy_exception_justification: "Local IPC only."
          })annotation");
  while (conn->current_write->BytesRemaining() > 0) {
    int result = conn->socket->Write(
        conn->current_write.get(), conn->current_write->BytesRemaining(),
        base::BindOnce(&MahoMcpSocketServer::OnWriteComplete,
                       weak_factory_.GetWeakPtr(), connection_id),
        kQueueAnnotation);
    if (result == net::ERR_IO_PENDING) {
      conn->write_in_flight = true;
      return;
    }
    if (result < 0) {
      LOG(WARNING) << "[MahoMcp] PumpWriteQueue write err=" << result;
      conn->current_write.reset();
      conn->write_queue.clear();
      return;
    }
    conn->current_write->DidConsume(result);
  }
  conn->current_write.reset();
  if (!conn->write_queue.empty()) {
    PumpWriteQueue(connection_id);
  }
}

void MahoMcpSocketServer::OnWriteComplete(uint64_t connection_id, int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ActiveConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }
  conn->write_in_flight = false;
  if (result < 0) {
    LOG(WARNING) << "[MahoMcp] OnWriteComplete write err=" << result;
    conn->current_write.reset();
    conn->write_queue.clear();
    return;
  }
  if (conn->current_write) {
    conn->current_write->DidConsume(result);
  }
  PumpWriteQueue(connection_id);
}

void MahoMcpSocketServer::RemoveConnection(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
    if (it->connection_id == connection_id) {
      // Todo 6: tell the lease registry this holder dropped so its leases enter
      // the grace period and auto-release, keeping disconnect semantics honest.
      if (lease_registry_ && it->session) {
        lease_registry_->OnSessionDropped(it->session->session_id());
      }
      sessions_.erase(it);
      return;
    }
  }
}

void MahoMcpSocketServer::CleanupStaleSocket() {
  struct stat statbuf;
  if (::stat(socket_path_.value().c_str(), &statbuf) != 0) {
    return;
  }

  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    unlink(socket_path_.value().c_str());
    return;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  const std::string& path = socket_path_.value();
  const size_t copy_len =
      std::min(path.size(), sizeof(addr.sun_path) - 1);
  base::span(addr.sun_path).first(copy_len).copy_from(
      base::span(path).first(copy_len));

  int rv = HANDLE_EINTR(
      connect(sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)));
  IGNORE_EINTR(close(sock_fd));

  if (rv == 0) {
    // Another process is listening — we cannot proceed.
    LOG(ERROR) << "MCP: Socket " << socket_path_.value()
               << " is in use by another process";
    return;
  }

  // Connection failed — socket is stale. Remove it.
  LOG(INFO) << "MCP: Removing stale socket at " << socket_path_.value();
  unlink(socket_path_.value().c_str());
}

}  // namespace maho
