// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_opencode_runtime_service.h"

#include <optional>
#include <signal.h>
#include <string>
#include <unistd.h>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kHealthCheckAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_opencode_health_check",
        R"maho(semantics {
          sender: "Maho OpenCode Runtime Service"
          description:
            "Probes local OpenCode server health on browser startup."
          trigger: "Browser startup when no custom endpoint is configured."
          data: "None. Simple GET/HEAD request to local server."
          destination: LOCAL
        }
        policy {
          cookies_allowed: NO
          setting: "Configure maho.ai.runtime_endpoint in settings."
        })maho");

MahoOpenCodeRuntimeService* g_instance = nullptr;

struct LaunchResult {
  base::Process process;
  bool binary_missing = false;
};

constexpr char kLaunchdJobLabel[] = "com.maho.opencode-runtime";

LaunchResult FindBinaryAndLaunch(const char* host, int port) {
  LaunchResult result;

  auto env = base::Environment::Create();
  auto path_value = env->GetVar("PATH");
  if (!path_value.has_value()) {
    result.binary_missing = true;
    return result;
  }

  base::FilePath opencode_path;
  for (const auto& dir :
       base::SplitString(*path_value, ":", base::KEEP_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY)) {
    base::FilePath candidate =
        base::FilePath::FromUTF8Unsafe(dir).Append("opencode");
    if (base::PathExists(candidate)) {
      base::FilePath native_candidate =
          candidate.DirName().Append(".opencode");
      if (!base::PathExists(native_candidate)) {
        base::FilePath resolved;
        if (base::ReadSymbolicLink(candidate, &resolved)) {
          if (!resolved.IsAbsolute()) {
            resolved = candidate.DirName().Append(resolved);
          }
          native_candidate = resolved.DirName().Append(".opencode");
        }
      }
      if (base::PathExists(native_candidate)) {
        opencode_path = native_candidate;
      } else {
        opencode_path = candidate;
      }
      break;
    }
  }

  if (opencode_path.empty()) {
    result.binary_missing = true;
    return result;
  }

  // On macOS, Chromium's Hardened Runtime causes dyld4 to deadlock when
  // loading Bun's ad-hoc signed JIT dylib in any directly-spawned child.
  // Launching via launchctl creates the process through launchd, which has
  // no security-context inheritance from Chromium.
  std::string port_str = base::NumberToString(port);
  std::string cmd = base::StringPrintf(
      "/bin/launchctl submit -l %s -- %s serve --hostname %s --port %s",
      kLaunchdJobLabel, opencode_path.value().c_str(), host, port_str.c_str());

  int ret = system(cmd.c_str());
  if (ret != 0) {
    LOG(WARNING) << "OpenCode runtime: launchctl submit failed (ret=" << ret
                 << ")";
    return result;
  }

  // launchctl submit does not return the child PID. Poll briefly.
  std::string pgrep_pattern = base::StringPrintf(
      "opencode.*serve.*--port %s", port_str.c_str());
  for (int i = 0; i < 10; ++i) {
    usleep(300000);
    std::string pgrep_output;
    if (base::GetAppOutput({"pgrep", "-f", pgrep_pattern}, &pgrep_output)) {
      pid_t pid = atoi(pgrep_output.c_str());
      if (pid > 0) {
        result.process = base::Process(pid);
        break;
      }
    }
  }
  return result;
}

}  // namespace

MahoOpenCodeRuntimeService::MahoOpenCodeRuntimeService() = default;

MahoOpenCodeRuntimeService::~MahoOpenCodeRuntimeService() {
  Shutdown();
}

MahoOpenCodeRuntimeService* MahoOpenCodeRuntimeService::GetInstance() {
  return g_instance;
}

void MahoOpenCodeRuntimeService::SetInstance(
    std::unique_ptr<MahoOpenCodeRuntimeService> instance) {
  DCHECK(!g_instance);
  g_instance = instance.release();
}

void MahoOpenCodeRuntimeService::ResetInstance() {
  delete g_instance;
  g_instance = nullptr;
}

void MahoOpenCodeRuntimeService::Start(
    const std::string& custom_endpoint,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  url_loader_factory_ = std::move(url_loader_factory);

  if (!custom_endpoint.empty()) {
    SetState(State::kUsingCustomEndpoint);
    LOG(INFO) << "OpenCode runtime: using custom endpoint: "
              << custom_endpoint;
    return;
  }

  SetState(State::kProbing);
  ProbeSessionEndpoint();
}

void MahoOpenCodeRuntimeService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  health_poll_timer_.Stop();
  session_loader_.reset();
  event_loader_.reset();

  if (we_started_process_ && managed_process_.IsValid()) {
    LOG(INFO) << "OpenCode runtime: terminating managed process (pid="
              << managed_process_.Pid() << ")";
    system(base::StringPrintf("/bin/launchctl remove %s 2>/dev/null",
                              kLaunchdJobLabel).c_str());
    kill(managed_process_.Pid(), SIGKILL);
    managed_process_.Close();
  }

  we_started_process_ = false;
  state_ = State::kIdle;
}

GURL MahoOpenCodeRuntimeService::GetManagedEndpoint() const {
  return GURL(base::StringPrintf("http://%s:%d/", kDefaultHost, kDefaultPort));
}

std::string MahoOpenCodeRuntimeService::GetStatusMessage() const {
  switch (state_) {
    case State::kIdle:
      return "OpenCode runtime has not been initialized.";
    case State::kUsingCustomEndpoint:
      return "";
    case State::kProbing:
    case State::kStarting:
    case State::kWaitingForHealth:
      return "OpenCode runtime is starting up. Please wait a moment.";
    case State::kReady:
      return "";
    case State::kBinaryMissing:
      return "The opencode command was not found. "
             "Please install OpenCode and ensure it is on your PATH.";
    case State::kLaunchFailed:
      return "Failed to start the local OpenCode server.";
    case State::kHealthCheckFailed:
      return "The local OpenCode server did not become ready in time.";
  }
}

void MahoOpenCodeRuntimeService::SetState(State new_state) {
  if (state_ == new_state) {
    return;
  }
  state_ = new_state;
}

bool MahoOpenCodeRuntimeService::IsSessionResponseValid(
    const std::optional<std::string>& body) const {
  if (!body.has_value()) {
    return false;
  }
  auto parsed = base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  return parsed && parsed->is_list();
}

// --- Initial probe: /session then /event ---

void MahoOpenCodeRuntimeService::ProbeSessionEndpoint() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GetManagedEndpoint().Resolve("session");
  request->method = "GET";

  session_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kHealthCheckAnnotation);
  session_loader_->SetTimeoutDuration(base::Seconds(3));
  session_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoOpenCodeRuntimeService::OnProbeSessionResponse,
                     weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeRuntimeService::OnProbeSessionResponse(
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  session_loader_.reset();

  if (!IsSessionResponseValid(body)) {
    OnProbesFailed();
    return;
  }

  ProbeEventEndpoint();
}

void MahoOpenCodeRuntimeService::ProbeEventEndpoint() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GetManagedEndpoint().Resolve("event");
  request->method = "HEAD";

  event_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kHealthCheckAnnotation);
  event_loader_->SetTimeoutDuration(base::Seconds(3));
  event_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(
          [](base::WeakPtr<MahoOpenCodeRuntimeService> self,
             std::optional<std::string>) {
            if (self) {
              self->OnProbeEventComplete();
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeRuntimeService::OnProbeEventComplete() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int response_code = -1;
  if (event_loader_ && event_loader_->ResponseInfo() &&
      event_loader_->ResponseInfo()->headers) {
    response_code =
        event_loader_->ResponseInfo()->headers->response_code();
  }
  event_loader_.reset();

  if (response_code == 200) {
    LOG(INFO) << "OpenCode runtime: existing server detected at "
              << GetManagedEndpoint().spec()
              << " (/session + /event both healthy)";
    we_started_process_ = false;
    SetState(State::kReady);
    return;
  }

  OnProbesFailed();
}

void MahoOpenCodeRuntimeService::OnProbesFailed() {
  StartLaunchOnThreadPool();
}

// --- Process launch on thread pool ---

void MahoOpenCodeRuntimeService::StartLaunchOnThreadPool() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetState(State::kStarting);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&FindBinaryAndLaunch, kDefaultHost, kDefaultPort),
      base::BindOnce(
          [](base::WeakPtr<MahoOpenCodeRuntimeService> self,
             LaunchResult result) {
            if (self) {
              self->OnLaunchComplete(std::move(result.process),
                                     result.binary_missing);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeRuntimeService::OnLaunchComplete(base::Process process,
                                                    bool binary_missing) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (binary_missing) {
    LOG(ERROR) << "OpenCode runtime: 'opencode' binary not found on PATH";
    SetState(State::kBinaryMissing);
    return;
  }

  if (!process.IsValid()) {
    LOG(ERROR) << "OpenCode runtime: failed to launch process";
    SetState(State::kLaunchFailed);
    return;
  }

  managed_process_ = std::move(process);
  we_started_process_ = true;
  health_retry_count_ = 0;
  health_poll_in_flight_ = false;

  LOG(INFO) << "OpenCode runtime: launched managed process (pid="
            << managed_process_.Pid() << ")";

  SetState(State::kWaitingForHealth);
  health_poll_timer_.Start(
      FROM_HERE, kHealthPollInterval,
      base::BindRepeating(&MahoOpenCodeRuntimeService::PollHealth,
                          weak_factory_.GetWeakPtr()));
}

// --- Health polling: /session then /event ---

void MahoOpenCodeRuntimeService::PollHealth() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (health_poll_in_flight_) {
    return;
  }
  health_poll_in_flight_ = true;

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GetManagedEndpoint().Resolve("session");
  request->method = "GET";

  session_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kHealthCheckAnnotation);
  session_loader_->SetTimeoutDuration(base::Seconds(3));
  session_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoOpenCodeRuntimeService::OnSessionHealthResponse,
                     weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeRuntimeService::OnSessionHealthResponse(
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  session_loader_.reset();

  if (!IsSessionResponseValid(body)) {
    health_poll_in_flight_ = false;
    health_retry_count_++;
    if (health_retry_count_ >= kMaxHealthRetries) {
      LOG(ERROR) << "OpenCode runtime: health check failed after "
                 << kMaxHealthRetries << " retries";
      health_poll_timer_.Stop();
      SetState(State::kHealthCheckFailed);
    }
    return;
  }

  ProbeEventHealth();
}

void MahoOpenCodeRuntimeService::ProbeEventHealth() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GetManagedEndpoint().Resolve("event");
  request->method = "HEAD";

  event_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kHealthCheckAnnotation);
  event_loader_->SetTimeoutDuration(base::Seconds(3));
  event_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(
          [](base::WeakPtr<MahoOpenCodeRuntimeService> self,
             std::optional<std::string>) {
            if (self) {
              self->OnEventHealthComplete();
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoOpenCodeRuntimeService::OnEventHealthComplete() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int response_code = -1;
  if (event_loader_ && event_loader_->ResponseInfo() &&
      event_loader_->ResponseInfo()->headers) {
    response_code =
        event_loader_->ResponseInfo()->headers->response_code();
  }
  event_loader_.reset();
  health_poll_in_flight_ = false;

  if (response_code == 200) {
    LOG(INFO) << "OpenCode runtime: managed server is ready "
              << "(/session + /event both healthy)";
    health_poll_timer_.Stop();
    SetState(State::kReady);
    return;
  }

  health_retry_count_++;
  if (health_retry_count_ >= kMaxHealthRetries) {
    LOG(ERROR) << "OpenCode runtime: health check failed after "
               << kMaxHealthRetries << " retries (/event not ready)";
    health_poll_timer_.Stop();
    SetState(State::kHealthCheckFailed);
  }
}
