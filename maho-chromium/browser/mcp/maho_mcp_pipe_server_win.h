// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_PIPE_SERVER_WIN_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_PIPE_SERVER_WIN_H_

#if BUILDFLAG(IS_WIN)

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/win/object_watcher.h"
#include "base/win/scoped_handle.h"
#include "build/build_config.h"

namespace maho {

class MahoMcpSession;

// Windows Named Pipe server for the Maho MCP (Model Context Protocol).
//
// Creates a named pipe at \\.\pipe\maho-browser-{username}, secured with a
// DACL that grants access only to the current user's SID.
//
// Uses overlapped I/O with base::win::ObjectWatcher for async accept and read.
//
// Lifecycle:
//   1. Construct (no args — pipe name derived from current username).
//   2. Call Start() to create pipe + begin accepting.
//   3. Destructor closes all pipe handles.
class MahoMcpPipeServer : public base::win::ObjectWatcher::Delegate {
 public:
  MahoMcpPipeServer();
  ~MahoMcpPipeServer() override;

  MahoMcpPipeServer(const MahoMcpPipeServer&) = delete;
  MahoMcpPipeServer& operator=(const MahoMcpPipeServer&) = delete;

  bool Start();
  void Stop();

  // Returns the full pipe name (for testing).
  const std::wstring& pipe_name() const { return pipe_name_; }

  // Returns number of active sessions (for testing).
  size_t active_session_count() const { return connections_.size(); }

  // base::win::ObjectWatcher::Delegate:
  void OnObjectSignaled(HANDLE object) override;

 private:
  struct PipeConnection {
    base::win::ScopedHandle pipe_handle;
    std::unique_ptr<MahoMcpSession> session;
    OVERLAPPED read_overlapped = {};
    base::win::ScopedHandle read_event;
    base::win::ObjectWatcher read_watcher;
    std::vector<char> read_buffer;
  };

  bool CreateListenPipe();
  bool BuildSecurityAttributes(SECURITY_ATTRIBUTES* sa,
                               PSECURITY_DESCRIPTOR* sd_out);
  void BeginAccept();
  void OnClientConnected();
  void BeginRead(PipeConnection* conn);
  void OnReadComplete(PipeConnection* conn);
  void RemoveConnection(PipeConnection* conn);
  void CloseAllConnections();

  static std::wstring ComputePipeName();
  static std::wstring GetCurrentUserSidString();
  static std::wstring SanitizeUsername(const std::wstring& username);

  std::wstring pipe_name_;

  // Listening pipe state.
  base::win::ScopedHandle listen_handle_;
  OVERLAPPED connect_overlapped_ = {};
  base::win::ScopedHandle connect_event_;
  base::win::ObjectWatcher connect_watcher_;

  // Active connections.
  std::vector<std::unique_ptr<PipeConnection>> connections_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMcpPipeServer> weak_factory_{this};
};

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_PIPE_SERVER_WIN_H_
