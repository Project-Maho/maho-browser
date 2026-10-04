// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_PIPE_SERVER_WIN_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_PIPE_SERVER_WIN_H_

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)

#include <windows.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/win/object_watcher.h"
#include "base/win/scoped_handle.h"

namespace maho {

class MahoMcpSession;
class MahoMcpLeaseRegistry;
class MahoMcpSessionToken;

// Windows Named Pipe server for the Maho MCP (Model Context Protocol).
//
// Creates a named pipe at \\.\pipe\maho-browser-{username}, secured with a
// DACL that grants access only to the current user's SID.
//
// Uses overlapped I/O with base::win::ObjectWatcher for async accept, read,
// and write.
//
// Threading / deferred-delivery contract (mirrors the POSIX
// MahoMcpSocketServer):
//   - All server methods run on the "pipe sequence" (the IO thread the server
//     was started on). Every access to the SEQUENCE_CHECKER-guarded state
//     stays on that sequence.
//   - MahoMcpSession::ProcessData() is dispatched to the Chromium UI sequence
//     via content::GetUIThreadTaskRunner(); responses come back to the pipe
//     sequence in OnResponsesReady(). The pipe sequence never touches UI
//     objects — the UI hop is owned by the session/browser delegate.
//   - Each connection is assigned a monotonically increasing STABLE
//     connection id. All asynchronous continuations capture that id (and a
//     WeakPtr to the server), never a raw PipeConnection* or a vector index,
//     so a torn-down connection is resolved to nullptr and its late response
//     is dropped safely.
//   - Responses that finish asynchronously (screenshots, HAR) are delivered
//     through SetDeferredResponseSender(), which posts back to the pipe
//     sequence keyed by connection id.
//   - Both immediate and deferred responses flow through a single
//     per-connection ORDERED write queue with explicit partial-write /
//     completion handling, so responses larger than the pipe buffer (HAR,
//     screenshots) are written correctly over the byte-mode named pipe.
//
// Lifecycle:
//   1. Construct with a session token pointer (owned externally; may be
//      nullptr in tests, which bypasses DPAPI token validation).
//   2. Call Start() to create pipe + begin accepting.
//   3. Destructor closes all pipe handles.
class MahoMcpPipeServer : public base::win::ObjectWatcher::Delegate {
 public:
  explicit MahoMcpPipeServer(MahoMcpSessionToken* session_token);
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
  friend class MahoMcpPipeServerWinTest;
  friend class MahoMcpPipeMemoryThreadTest;

  struct PipeConnection {
    PipeConnection();
    ~PipeConnection();

    PipeConnection(const PipeConnection&) = delete;
    PipeConnection& operator=(const PipeConnection&) = delete;

    // Stable, monotonically increasing identifier. Async continuations
    // reference the connection by this id (never by raw pointer or index).
    uint64_t connection_id = 0;

    base::win::ScopedHandle pipe_handle;
    std::shared_ptr<MahoMcpSession> session;

    OVERLAPPED read_overlapped = {};
    base::win::ScopedHandle read_event;
    base::win::ObjectWatcher read_watcher;
    std::vector<char> read_buffer;
    std::string ingress;
    size_t ingress_scan_offset = 0;
    bool read_in_progress = false;
    bool request_in_progress = false;
    bool admission_paused = false;
    uint64_t next_request_id = 1;
    struct Reservation {
      size_t bytes;
      bool large;
      bool replied;
    };
    std::map<uint64_t, Reservation> reservations;
    size_t ordinary_bytes = 0;
    bool large_result_pending = false;

    // Ordered write queue. Only one write is in flight at a time;
    // `write_offset` tracks bytes of the front buffer already written
    // (partial-write handling for byte-mode pipes). Deferred and immediate
    // responses share this queue to preserve ordering.
    OVERLAPPED write_overlapped = {};
    base::win::ScopedHandle write_event;
    base::win::ObjectWatcher write_watcher;
    base::circular_deque<std::string> write_queue;
    base::circular_deque<uint64_t> write_request_ids;
    size_t write_offset = 0;
    bool write_in_progress = false;
    // When the session has transitioned to kClosed, close the connection once
    // the write queue has fully drained (so the final error/response is
    // delivered before disconnect).
    bool close_after_drain = false;
  };

  bool CreateListenPipe();
  bool BuildSecurityAttributes(SECURITY_ATTRIBUTES* sa,
                               PSECURITY_DESCRIPTOR* sd_out);
  void BeginAccept();
  void OnClientConnected();

  PipeConnection* FindConnection(uint64_t connection_id);

  void BeginRead(uint64_t connection_id);
  void OnReadComplete(uint64_t connection_id);
  void PumpRequests(uint64_t connection_id);
  void OnResponsesReady(uint64_t connection_id,
                        uint64_t request_id,
                        bool expects_response,
                        std::vector<std::string> responses);
  void OnDeferredResponse(uint64_t connection_id,
                          uint64_t request_id,
                          std::string response);
  void FinishWrite(PipeConnection& connection);

  void MaybeWriteNext(uint64_t connection_id);
  void OnWriteComplete(uint64_t connection_id);

  void RemoveConnection(uint64_t connection_id);
  void CloseAllConnections();

  static std::wstring ComputePipeName();
  static std::wstring GetCurrentUserSidString();
  static std::wstring SanitizeUsername(const std::wstring& username);

  std::wstring pipe_name_;

  // Session token for DPAPI authentication (not owned).
  raw_ptr<MahoMcpSessionToken> session_token_;
  std::shared_ptr<MahoMcpLeaseRegistry> lease_registry_;

  // Listening pipe state.
  base::win::ScopedHandle listen_handle_;
  OVERLAPPED connect_overlapped_ = {};
  base::win::ScopedHandle connect_event_;
  base::win::ObjectWatcher connect_watcher_;

  // Active connections.
  std::vector<std::unique_ptr<PipeConnection>> connections_;

  uint64_t next_connection_id_ = 1;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMcpPipeServer> weak_factory_{this};
};

}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_PIPE_SERVER_WIN_H_
