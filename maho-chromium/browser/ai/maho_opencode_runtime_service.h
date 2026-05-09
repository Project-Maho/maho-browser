// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_OPENCODE_RUNTIME_SERVICE_H_
#define MAHO_BROWSER_AI_MAHO_OPENCODE_RUNTIME_SERVICE_H_

#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

// Browser-scoped service that ensures a local OpenCode server is available
// for Maho AI on browser startup. Lifecycle:
//
//   1. On startup, check if user has a custom endpoint configured.
//      If so, do nothing (kUsingCustomEndpoint).
//   2. Otherwise, probe the default local endpoint (127.0.0.1:4096)
//      for a healthy OpenCode server via /session + /event.
//   3. If healthy, reuse it (kReady, we_started_process_ = false).
//   4. If not, launch `opencode serve --hostname 127.0.0.1 --port 4096`
//      on a MayBlock() thread pool task.
//   5. Poll health until ready or give up.
//   6. On browser shutdown, terminate the process only if we started it.
class MahoOpenCodeRuntimeService {
 public:
  enum class State {
    kIdle,                  // Not yet started.
    kUsingCustomEndpoint,   // User has explicit endpoint; no managed server.
    kProbing,               // Checking if local server is already running.
    kStarting,              // Launching opencode process.
    kWaitingForHealth,      // Process launched, polling for readiness.
    kReady,                 // Server is healthy and ready.
    kBinaryMissing,         // `opencode` not found on PATH.
    kLaunchFailed,          // Process launch returned an error.
    kHealthCheckFailed,     // Health probes exhausted without success.
  };

  MahoOpenCodeRuntimeService();
  ~MahoOpenCodeRuntimeService();

  MahoOpenCodeRuntimeService(const MahoOpenCodeRuntimeService&) = delete;
  MahoOpenCodeRuntimeService& operator=(const MahoOpenCodeRuntimeService&) =
      delete;

  static MahoOpenCodeRuntimeService* GetInstance();
  static void SetInstance(
      std::unique_ptr<MahoOpenCodeRuntimeService> instance);
  static void ResetInstance();

  void Start(
      const std::string& custom_endpoint,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  void Shutdown();

  State state() const { return state_; }
  GURL GetManagedEndpoint() const;
  std::string GetStatusMessage() const;

 private:
  static constexpr char kDefaultHost[] = "127.0.0.1";
  static constexpr int kDefaultPort = 4096;
  static constexpr int kMaxHealthRetries = 30;
  static constexpr base::TimeDelta kHealthPollInterval = base::Seconds(2);

  void SetState(State new_state);

  void ProbeSessionEndpoint();
  void OnProbeSessionResponse(std::optional<std::string> body);
  void ProbeEventEndpoint();
  void OnProbeEventComplete();
  void OnProbesFailed();

  void StartLaunchOnThreadPool();
  void OnLaunchComplete(base::Process process, bool binary_missing);

  void PollHealth();
  void OnSessionHealthResponse(std::optional<std::string> body);
  void ProbeEventHealth();
  void OnEventHealthComplete();

  bool IsSessionResponseValid(const std::optional<std::string>& body) const;

  SEQUENCE_CHECKER(sequence_checker_);

  State state_ = State::kIdle;
  bool we_started_process_ = false;
  base::Process managed_process_;
  int health_retry_count_ = 0;
  bool health_poll_in_flight_ = false;
  base::RepeatingTimer health_poll_timer_;

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> session_loader_;
  std::unique_ptr<network::SimpleURLLoader> event_loader_;

  base::WeakPtrFactory<MahoOpenCodeRuntimeService> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_OPENCODE_RUNTIME_SERVICE_H_
