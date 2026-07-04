// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"

#if BUILDFLAG(IS_WIN)

#include <sddl.h>
#include <userenv.h>
#include <wchar.h>

#include <algorithm>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/mcp/maho_mcp_session.h"

namespace maho {

namespace {

constexpr size_t kPipeBufferSize = 64 * 1024;  // 64 KiB
constexpr DWORD kPipeTimeout = 5000;           // 5 seconds
constexpr size_t kReadBufferSize = 4096;
constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\maho-browser-";

}  // namespace

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

MahoMcpPipeServer::MahoMcpPipeServer() : pipe_name_(ComputePipeName()) {}

MahoMcpPipeServer::~MahoMcpPipeServer() {
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
  } else {
    // Read completion for a connection.
    for (auto& conn : connections_) {
      if (object == conn->read_event.get()) {
        OnReadComplete(conn.get());
        return;
      }
    }
  }
}

void MahoMcpPipeServer::OnClientConnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Transfer the connected pipe handle to a new connection.
  auto conn = std::make_unique<PipeConnection>();
  conn->pipe_handle = std::move(listen_handle_);
  conn->session = std::make_unique<MahoMcpSession>();
  conn->read_buffer.resize(kReadBufferSize);

  // Create read event for this connection.
  conn->read_event.Set(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  memset(&conn->read_overlapped, 0, sizeof(conn->read_overlapped));
  conn->read_overlapped.hEvent = conn->read_event.get();

  PipeConnection* conn_ptr = conn.get();
  connections_.push_back(std::move(conn));

  BeginRead(conn_ptr);

  // Create a new listen pipe for the next connection.
  if (CreateListenPipe()) {
    BeginAccept();
  }
}

void MahoMcpPipeServer::BeginRead(PipeConnection* conn) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!conn->pipe_handle.is_valid()) {
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
    OnReadComplete(conn);
    return;
  }

  DWORD error = ::GetLastError();
  if (error == ERROR_IO_PENDING) {
    conn->read_watcher.StartWatchingOnce(conn->read_event.get(), this);
  } else if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
    RemoveConnection(conn);
  } else {
    LOG(ERROR) << "MCP: ReadFile on pipe failed: " << error;
    RemoveConnection(conn);
  }
}

void MahoMcpPipeServer::OnReadComplete(PipeConnection* conn) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DWORD bytes_read = 0;
  if (!::GetOverlappedResult(conn->pipe_handle.get(),
                             &conn->read_overlapped,
                             &bytes_read, FALSE)) {
    DWORD error = ::GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
      RemoveConnection(conn);
      return;
    }
    LOG(ERROR) << "MCP: GetOverlappedResult failed: " << error;
    RemoveConnection(conn);
    return;
  }

  if (bytes_read == 0) {
    RemoveConnection(conn);
    return;
  }

  std::string data(conn->read_buffer.data(), bytes_read);
  auto responses = conn->session->ProcessData(data);

  for (const auto& response : responses) {
    DWORD bytes_written = 0;
    // Synchronous write for response simplicity (responses are small).
    ::WriteFile(conn->pipe_handle.get(), response.data(),
                static_cast<DWORD>(response.size()),
                &bytes_written, nullptr);
  }

  if (conn->session->state() == MahoMcpSession::State::kClosed) {
    RemoveConnection(conn);
    return;
  }

  BeginRead(conn);
}

void MahoMcpPipeServer::RemoveConnection(PipeConnection* conn) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto it = connections_.begin(); it != connections_.end(); ++it) {
    if (it->get() == conn) {
      (*it)->read_watcher.StopWatching();
      ::DisconnectNamedPipe((*it)->pipe_handle.get());
      connections_.erase(it);
      return;
    }
  }
}

void MahoMcpPipeServer::CloseAllConnections() {
  for (auto& conn : connections_) {
    conn->read_watcher.StopWatching();
    if (conn->pipe_handle.is_valid()) {
      ::DisconnectNamedPipe(conn->pipe_handle.get());
    }
  }
  connections_.clear();
}

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)
