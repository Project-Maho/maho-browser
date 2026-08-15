// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// Win32 API interop uses raw buffers / pointer arithmetic (memset, .data(),
// pointer offsets) that cannot be expressed with bounds-checked spans.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"

#if BUILDFLAG(IS_WIN)

#include <sddl.h>
#include <userenv.h>
#include <wchar.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/win/scoped_handle.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "maho/browser/mcp/maho_mcp_session_token_win.h"

namespace maho {

namespace {

constexpr size_t kPipeBufferSize = 64 * 1024;  // 64 KiB
constexpr DWORD kPipeTimeout = 5000;           // 5 seconds
constexpr size_t kReadBufferSize = 4096;
constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\maho-browser-";

std::string GetPipeClientExecutable(HANDLE pipe) {
  ULONG client_pid = 0;
  if (!::GetNamedPipeClientProcessId(pipe, &client_pid)) {
    return std::string();
  }
  base::win::ScopedHandle process(
      ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, client_pid));
  if (!process.is_valid()) {
    return std::string();
  }
  std::wstring path(32768, L'\0');
  DWORD path_length = static_cast<DWORD>(path.size());
  if (!::QueryFullProcessImageNameW(process.get(), 0, path.data(),
                                    &path_length)) {
    return std::string();
  }
  path.resize(path_length);
  return base::WideToUTF8(path);
}

}  // namespace

MahoMcpPipeServer::PipeConnection::PipeConnection() = default;
MahoMcpPipeServer::PipeConnection::~PipeConnection() = default;

// static
std::wstring MahoMcpPipeServer::SanitizeUsername(const std::wstring& username) {
  std::wstring sanitized;
  sanitized.reserve(username.size());
  for (wchar_t c : username) {
    if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
        (c >= L'0' && c <= L'9') || c == L'_' || c == L'-') {
      sanitized.push_back(c);
    }
  }
  return sanitized;
}

// static
std::wstring MahoMcpPipeServer::ComputePipeName() {
  wchar_t username_buf[256] = {};
  DWORD username_len = static_cast<DWORD>(std::size(username_buf));
  if (!::GetUserNameW(username_buf, &username_len)) {
    LOG(ERROR) << "MCP: GetUserNameW failed: " << ::GetLastError();
    return std::wstring();
  }
  std::wstring username(username_buf);
  std::wstring sanitized = SanitizeUsername(username);
  if (sanitized.empty()) {
    sanitized = L"default";
  }
  return std::wstring(kPipePrefix) + sanitized;
}

// static
std::wstring MahoMcpPipeServer::GetCurrentUserSidString() {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
    LOG(ERROR) << "MCP: OpenProcessToken failed: " << ::GetLastError();
    return std::wstring();
  }

  DWORD token_info_len = 0;
  ::GetTokenInformation(token, TokenUser, nullptr, 0, &token_info_len);

  std::vector<BYTE> token_info_buffer(token_info_len);
  if (!::GetTokenInformation(token, TokenUser, token_info_buffer.data(),
                             token_info_len, &token_info_len)) {
    LOG(ERROR) << "MCP: GetTokenInformation failed: " << ::GetLastError();
    ::CloseHandle(token);
    return std::wstring();
  }
  ::CloseHandle(token);

  TOKEN_USER* token_user =
      reinterpret_cast<TOKEN_USER*>(token_info_buffer.data());
  LPWSTR sid_string = nullptr;
  if (!::ConvertSidToStringSidW(token_user->User.Sid, &sid_string)) {
    LOG(ERROR) << "MCP: ConvertSidToStringSidW failed: " << ::GetLastError();
    return std::wstring();
  }

  std::wstring result(sid_string);
  ::LocalFree(sid_string);
  return result;
}

MahoMcpPipeServer::MahoMcpPipeServer(MahoMcpSessionToken* session_token)
    : pipe_name_(ComputePipeName()),
      session_token_(session_token),
      lease_registry_(std::make_unique<MahoMcpLeaseRegistry>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  MahoMcpSession::SetLeaseRegistryForBrowserActions(lease_registry_.get());
}

MahoMcpPipeServer::~MahoMcpPipeServer() {
  if (MahoMcpSession::GetLeaseRegistryForBrowserActions() ==
      lease_registry_.get()) {
    MahoMcpSession::SetLeaseRegistryForBrowserActions(nullptr);
  }
  Stop();
}

bool MahoMcpPipeServer::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (pipe_name_.empty()) {
    LOG(ERROR) << "MCP: Cannot start pipe server — pipe name is empty";
    return false;
  }

  if (!CreateListenPipe()) {
    return false;
  }

  LOG(INFO) << "MCP: Listening on pipe "
            << base::WideToUTF8(pipe_name_);
  BeginAccept();
  return true;
}

void MahoMcpPipeServer::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  connect_watcher_.StopWatching();
  CloseAllConnections();

  if (listen_handle_.is_valid()) {
    ::CancelIoEx(listen_handle_.get(), &connect_overlapped_);
    listen_handle_.Close();
  }
  if (connect_event_.is_valid()) {
    connect_event_.Close();
  }
}

bool MahoMcpPipeServer::BuildSecurityAttributes(SECURITY_ATTRIBUTES* sa,
                                                  PSECURITY_DESCRIPTOR* sd_out) {
  std::wstring sid_string = GetCurrentUserSidString();
  if (sid_string.empty()) {
    return false;
  }

  // DACL: Protected, Allow Generic Read + Generic Write to current user only.
  std::wstring sddl =
      L"D:P(A;;GRGW;;;" + sid_string + L")";

  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, sd_out, nullptr)) {
    LOG(ERROR) << "MCP: ConvertStringSecurityDescriptorToSecurityDescriptorW "
               << "failed: " << ::GetLastError();
    return false;
  }

  sa->nLength = sizeof(SECURITY_ATTRIBUTES);
  sa->lpSecurityDescriptor = *sd_out;
  sa->bInheritHandle = FALSE;
  return true;
}

bool MahoMcpPipeServer::CreateListenPipe() {
  SECURITY_ATTRIBUTES sa = {};
  PSECURITY_DESCRIPTOR sd = nullptr;

  if (!BuildSecurityAttributes(&sa, &sd)) {
    return false;
  }

  // RAII cleanup for security descriptor allocated by LocalAlloc.
  std::unique_ptr<void, decltype(&::LocalFree)> sd_guard(sd, ::LocalFree);

  // Use FILE_FLAG_FIRST_PIPE_INSTANCE only on the very first pipe creation
  // to guard against pipe-squatting attacks. Subsequent instances (created
  // after a client connects) omit it since the first instance is still open.
  DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
  if (connections_.empty() && !listen_handle_.is_valid()) {
    open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
  }

  HANDLE pipe = ::CreateNamedPipeW(
      pipe_name_.c_str(),
      open_mode,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
      PIPE_UNLIMITED_INSTANCES,
      kPipeBufferSize,
      kPipeBufferSize,
      kPipeTimeout,
      &sa);

  if (pipe == INVALID_HANDLE_VALUE) {
    LOG(ERROR) << "MCP: CreateNamedPipeW failed: " << ::GetLastError();
    return false;
  }

  listen_handle_.Set(pipe);

  // Create event for ConnectNamedPipe overlapped operation.
  connect_event_.Set(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!connect_event_.is_valid()) {
    LOG(ERROR) << "MCP: CreateEvent for connect failed: " << ::GetLastError();
    listen_handle_.Close();
    return false;
  }

  memset(&connect_overlapped_, 0, sizeof(connect_overlapped_));
  connect_overlapped_.hEvent = connect_event_.get();
  return true;
}

void MahoMcpPipeServer::BeginAccept() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!listen_handle_.is_valid()) {
    return;
  }

  ::ResetEvent(connect_event_.get());

  BOOL connected = ::ConnectNamedPipe(listen_handle_.get(),
                                      &connect_overlapped_);
  if (connected) {
    // Immediate connection (rare for overlapped pipes).
    OnClientConnected();
    return;
  }

  DWORD error = ::GetLastError();
  if (error == ERROR_IO_PENDING) {
    // Normal case: wait for client.
    connect_watcher_.StartWatchingOnce(connect_event_.get(), this);
  } else if (error == ERROR_PIPE_CONNECTED) {
    // Client connected between CreateNamedPipeW and ConnectNamedPipe.
    OnClientConnected();
  } else {
    LOG(ERROR) << "MCP: ConnectNamedPipe failed: " << error;
  }
}

void MahoMcpPipeServer::OnObjectSignaled(HANDLE object) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (object == connect_event_.get()) {
    OnClientConnected();
    return;
  }
  for (auto& conn : connections_) {
    if (object == conn->read_event.get()) {
      OnReadComplete(conn->connection_id);
      return;
    }
    if (object == conn->write_event.get()) {
      OnWriteComplete(conn->connection_id);
      return;
    }
  }
}

void MahoMcpPipeServer::OnClientConnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Transfer the connected pipe handle to a new connection.
  auto conn = std::make_unique<PipeConnection>();
  conn->connection_id = next_connection_id_++;
  conn->pipe_handle = std::move(listen_handle_);
  const std::string peer_executable =
      GetPipeClientExecutable(conn->pipe_handle.get());
  conn->session = std::make_unique<MahoMcpSession>(
      session_token_, lease_registry_.get(), peer_executable, true);
  conn->read_buffer.resize(kReadBufferSize);

  conn->read_event.Set(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  memset(&conn->read_overlapped, 0, sizeof(conn->read_overlapped));
  conn->read_overlapped.hEvent = conn->read_event.get();

  conn->write_event.Set(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  memset(&conn->write_overlapped, 0, sizeof(conn->write_overlapped));
  conn->write_overlapped.hEvent = conn->write_event.get();

  const uint64_t connection_id = conn->connection_id;

  // Install the deferred-response sink. The callback captures a WeakPtr to the
  // server, the pipe sequence's task runner, and the STABLE connection id —
  // never a raw PipeConnection* or a vector index. It hops back to the pipe
  // sequence; OnDeferredResponse resolves the id and drops the response if the
  // connection is gone.
  scoped_refptr<base::SequencedTaskRunner> pipe_runner =
      base::SequencedTaskRunner::GetCurrentDefault();
  conn->session->SetDeferredResponseSender(base::BindRepeating(
      [](base::WeakPtr<MahoMcpPipeServer> self,
         scoped_refptr<base::SequencedTaskRunner> runner, uint64_t cid,
         std::string response) {
        runner->PostTask(
            FROM_HERE,
            base::BindOnce(&MahoMcpPipeServer::OnDeferredResponse, self, cid,
                           std::move(response)));
      },
      weak_factory_.GetWeakPtr(), pipe_runner, connection_id));

  connections_.push_back(std::move(conn));

  BeginRead(connection_id);

  // Create a new listen pipe for the next connection.
  if (CreateListenPipe()) {
    BeginAccept();
  }
}

MahoMcpPipeServer::PipeConnection* MahoMcpPipeServer::FindConnection(
    uint64_t connection_id) {
  for (auto& conn : connections_) {
    if (conn->connection_id == connection_id) {
      return conn.get();
    }
  }
  return nullptr;
}

void MahoMcpPipeServer::BeginRead(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PipeConnection* conn = FindConnection(connection_id);
  if (!conn || !conn->pipe_handle.is_valid()) {
    return;
  }

  ::ResetEvent(conn->read_event.get());

  BOOL success = ::ReadFile(
      conn->pipe_handle.get(),
      conn->read_buffer.data(),
      static_cast<DWORD>(conn->read_buffer.size()),
      nullptr,
      &conn->read_overlapped);

  if (success) {
    OnReadComplete(connection_id);
    return;
  }

  DWORD error = ::GetLastError();
  if (error == ERROR_IO_PENDING) {
    conn->read_watcher.StartWatchingOnce(conn->read_event.get(), this);
  } else if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
    RemoveConnection(connection_id);
  } else {
    LOG(ERROR) << "MCP: ReadFile on pipe failed: " << error;
    RemoveConnection(connection_id);
  }
}

void MahoMcpPipeServer::OnReadComplete(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PipeConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }

  DWORD bytes_read = 0;
  if (!::GetOverlappedResult(conn->pipe_handle.get(),
                             &conn->read_overlapped,
                             &bytes_read, FALSE)) {
    DWORD error = ::GetLastError();
    if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED) {
      LOG(ERROR) << "MCP: GetOverlappedResult (read) failed: " << error;
    }
    RemoveConnection(connection_id);
    return;
  }

  if (bytes_read == 0) {
    RemoveConnection(connection_id);
    return;
  }

  std::string data(conn->read_buffer.data(), bytes_read);

  // Dispatch ProcessData() on the Chromium UI sequence: the session and its
  // browser delegate touch UI objects, which must never be accessed on the
  // pipe sequence. Responses come back to the pipe sequence in
  // OnResponsesReady(), keyed by the stable connection id.
  MahoMcpSession* session_ptr = conn->session.get();
  content::GetUIThreadTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&MahoMcpSession::ProcessData,
                     base::Unretained(session_ptr), data),
      base::BindOnce(&MahoMcpPipeServer::OnResponsesReady,
                     weak_factory_.GetWeakPtr(), connection_id));
}

void MahoMcpPipeServer::OnResponsesReady(uint64_t connection_id,
                                         std::vector<std::string> responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PipeConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }

  for (auto& response : responses) {
    conn->write_queue.push_back(std::move(response));
  }

  const bool closed =
      conn->session->state() == MahoMcpSession::State::kClosed;
  conn->close_after_drain = conn->close_after_drain || closed;

  MaybeWriteNext(connection_id);

  // MaybeWriteNext() may have removed the connection (write error or a fully
  // drained close). Re-resolve before touching it again.
  conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }
  if (!conn->close_after_drain) {
    BeginRead(connection_id);
  }
}

void MahoMcpPipeServer::OnDeferredResponse(uint64_t connection_id,
                                           std::string response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PipeConnection* conn = FindConnection(connection_id);
  if (!conn || !conn->pipe_handle.is_valid()) {
    LOG(WARNING) << "[MahoMcp] OnDeferredResponse: DROPPED connection_id="
                 << connection_id
                 << " reason=" << (conn ? "no_pipe" : "connection_not_found");
    return;
  }

  conn->write_queue.push_back(std::move(response));
  MaybeWriteNext(connection_id);
}

void MahoMcpPipeServer::MaybeWriteNext(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PipeConnection* conn = FindConnection(connection_id);
  if (!conn || conn->write_in_progress) {
    return;
  }

  while (!conn->write_queue.empty()) {
    const std::string& front = conn->write_queue.front();
    if (conn->write_offset >= front.size()) {
      conn->write_queue.pop_front();
      conn->write_offset = 0;
      continue;
    }

    ::ResetEvent(conn->write_event.get());

    const size_t remaining = front.size() - conn->write_offset;
    const DWORD to_write = static_cast<DWORD>(std::min<size_t>(
        remaining, std::numeric_limits<DWORD>::max()));
    BOOL success = ::WriteFile(
        conn->pipe_handle.get(),
        front.data() + conn->write_offset,
        to_write,
        nullptr,
        &conn->write_overlapped);

    if (success) {
      // Completed synchronously; resolve the byte count via the overlapped
      // result (consistent with the read path) and keep draining.
      DWORD bytes_written = 0;
      ::GetOverlappedResult(conn->pipe_handle.get(), &conn->write_overlapped,
                            &bytes_written, FALSE);
      conn->write_offset += bytes_written;
      if (conn->write_offset >= conn->write_queue.front().size()) {
        conn->write_queue.pop_front();
        conn->write_offset = 0;
      }
      continue;
    }

    DWORD error = ::GetLastError();
    if (error == ERROR_IO_PENDING) {
      conn->write_in_progress = true;
      conn->write_watcher.StartWatchingOnce(conn->write_event.get(), this);
      return;
    }

    if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED) {
      LOG(WARNING) << "MCP: WriteFile on pipe failed: " << error;
    }
    RemoveConnection(connection_id);
    return;
  }

  // Queue fully drained.
  if (conn->close_after_drain) {
    RemoveConnection(connection_id);
  }
}
void MahoMcpPipeServer::OnWriteComplete(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PipeConnection* conn = FindConnection(connection_id);
  if (!conn) {
    return;
  }

  DWORD bytes_written = 0;
  if (!::GetOverlappedResult(conn->pipe_handle.get(),
                             &conn->write_overlapped,
                             &bytes_written, FALSE)) {
    DWORD error = ::GetLastError();
    if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED) {
      LOG(WARNING) << "MCP: GetOverlappedResult (write) failed: " << error;
    }
    RemoveConnection(connection_id);
    return;
  }

  conn->write_in_progress = false;
  if (!conn->write_queue.empty()) {
    conn->write_offset += bytes_written;
    if (conn->write_offset >= conn->write_queue.front().size()) {
      conn->write_queue.pop_front();
      conn->write_offset = 0;
    }
  }

  MaybeWriteNext(connection_id);
}

void MahoMcpPipeServer::RemoveConnection(uint64_t connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto it = connections_.begin(); it != connections_.end(); ++it) {
    if ((*it)->connection_id != connection_id) {
      continue;
    }
    // Stop watchers and cancel any outstanding overlapped I/O BEFORE erasing,
    // so no read/write completion can fire against freed connection state.
    (*it)->read_watcher.StopWatching();
    (*it)->write_watcher.StopWatching();
    if ((*it)->pipe_handle.is_valid()) {
      ::CancelIoEx((*it)->pipe_handle.get(), nullptr);
      ::DisconnectNamedPipe((*it)->pipe_handle.get());
    }
    if (lease_registry_ && (*it)->session) {
      lease_registry_->OnSessionDropped((*it)->session->session_id());
    }
    connections_.erase(it);
    return;
  }
}

void MahoMcpPipeServer::CloseAllConnections() {
  for (auto& conn : connections_) {
    if (lease_registry_ && conn->session) {
      lease_registry_->OnSessionDropped(conn->session->session_id());
    }
    conn->read_watcher.StopWatching();
    conn->write_watcher.StopWatching();
    if (conn->pipe_handle.is_valid()) {
      ::CancelIoEx(conn->pipe_handle.get(), nullptr);
      ::DisconnectNamedPipe(conn->pipe_handle.get());
    }
  }
  connections_.clear();
}

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)
