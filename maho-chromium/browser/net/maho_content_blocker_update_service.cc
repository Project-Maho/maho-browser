// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_content_blocker_update_service.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/mojom/url_loader_factory.mojom-forward.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace maho {

namespace {

constexpr base::TimeDelta kTimeout = base::Seconds(30);
constexpr base::TimeDelta kRefreshCadence = base::Hours(24);
// Floors that prevent zero-delay reschedule loops. A short floor is used when
// no request is in flight (a due list will be fetched promptly); a longer floor
// is used while requests are active, because their terminal paths reschedule.
constexpr base::TimeDelta kIdleFloor = base::Seconds(1);
constexpr base::TimeDelta kActiveFloor = base::Minutes(5);
// Non-200/304 sentinel forwarded for transport / policy failures with no
// trustworthy HTTP status. The Rust apply boundary treats it as a failure.
constexpr int kFailureStatus = 0;

MahoContentBlockerUpdateService* g_designated_updater = nullptr;

const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_content_blocker_update", R"(
        semantics {
          sender: "Maho Content Blocker Update Service"
          description:
            "Fetches updated adblock filter lists for Maho native content blocking."
          trigger:
            "Automatic periodic update check (daily) or explicit user action in chrome://maho-settings."
          data: "Filter list HTTP requests with conditional ETag / Last-Modified headers."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Managed via Maho Content Blocking Mode setting."
        })");

int64_t NowSeconds() {
  return static_cast<int64_t>(base::Time::Now().InSecondsFSinceUnixEpoch());
}

std::optional<int64_t> FindTimestamp(const base::DictValue& dict,
                                     std::string_view key) {
  if (std::optional<double> v = dict.FindDouble(key)) {
    return static_cast<int64_t>(*v);
  }
  if (std::optional<int> v = dict.FindInt(key)) {
    return static_cast<int64_t>(*v);
  }
  return std::nullopt;
}

// Earliest wall-clock second at which |list| should next be fetched: an active
// retry timestamp when the list is failing, otherwise a 24h cadence from the
// last success, otherwise "now" for a never-fetched list.
int64_t ListDueSeconds(const base::DictValue& list, int64_t now) {
  if (std::optional<int64_t> retry =
          FindTimestamp(list, "nextRetryTimestamp")) {
    return *retry;
  }
  if (std::optional<int64_t> ok = FindTimestamp(list, "lastSuccessTimestamp")) {
    return *ok + kRefreshCadence.InSeconds();
  }
  return now;
}

bool CompileRequiredByMutationResult(const std::string& result_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    LOG(WARNING)
        << "Content blocker update returned an invalid mutation result";
    return false;
  }

  const base::DictValue& result = parsed->GetDict();
  if (!result.FindBool("success").value_or(false)) {
    const base::DictValue* error = result.FindDict("error");
    LOG(WARNING) << "Content blocker update rejected"
                 << (error && error->FindString("code")
                         ? ": " + *error->FindString("code")
                         : std::string());
    return false;
  }
  return result.FindBool("compileRequired").value_or(false);
}

}  // namespace

// Per-request stream sink. Accumulates the body under the hard cap and routes
// terminal signals back to the owning service on the same sequence.
class MahoContentBlockerUpdateService::StreamConsumer
    : public network::SimpleURLLoaderStreamConsumer {
 public:
  StreamConsumer(MahoContentBlockerUpdateService* service,
                 ActiveRequest* request)
      : service_(service), request_(request) {}
  ~StreamConsumer() override = default;

  void OnDataReceived(std::string_view chunk,
                      base::OnceClosure resume) override {
    service_->OnStreamData(request_, chunk, std::move(resume));
  }
  void OnComplete(bool success) override {
    service_->OnStreamComplete(request_, success);
  }
  void OnRetry(base::OnceClosure start_retry) override {
    // Retries are not enabled for these requests, so this is not expected; if a
    // retry ever occurs, discard partial data and restart cleanly.
    request_->body.clear();
    std::move(start_retry).Run();
  }

 private:
  raw_ptr<MahoContentBlockerUpdateService> service_;
  raw_ptr<ActiveRequest> request_;
};

MahoContentBlockerUpdateService::ActiveRequest::ActiveRequest() = default;
MahoContentBlockerUpdateService::ActiveRequest::~ActiveRequest() = default;

MahoContentBlockerUpdateService::PendingFetch::PendingFetch() = default;
MahoContentBlockerUpdateService::PendingFetch::PendingFetch(
    const PendingFetch&) = default;
MahoContentBlockerUpdateService::PendingFetch&
MahoContentBlockerUpdateService::PendingFetch::operator=(
    const PendingFetch&) = default;
MahoContentBlockerUpdateService::PendingFetch::PendingFetch(
    PendingFetch&&) noexcept = default;
MahoContentBlockerUpdateService::PendingFetch&
MahoContentBlockerUpdateService::PendingFetch::operator=(
    PendingFetch&&) noexcept = default;
MahoContentBlockerUpdateService::PendingFetch::~PendingFetch() = default;

// static
bool MahoContentBlockerUpdateService::IsAllowedUpdateUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {
    return false;
  }
  if (net::IsLocalhost(url)) {
    return false;
  }
  if (url.HostIsIPAddress()) {
    net::IPAddress ip;
    if (!ip.AssignFromIPLiteral(url.host())) {
      return false;
    }
    if (ip.IsZero() || ip.IsLoopback() || ip.IsLinkLocal() ||
        ip.IsUniqueLocalIPv6() || !ip.IsPubliclyRoutable()) {
      return false;
    }
  }
  return true;
}

// static
bool MahoContentBlockerUpdateService::IsPubliclyRoutablePeer(
    const net::IPEndPoint& endpoint) {
  // Post-connect no-DNS TOCTOU guard: the actual connected peer must be a
  // present, publicly routable address. NOTE: when an HTTP proxy is in use the
  // reported endpoint is the proxy, not the origin, so this guard cannot see
  // the true origin address; that is an accepted limitation and no speculative
  // resolver is added here.
  const net::IPAddress& ip = endpoint.address();
  if (!ip.IsValid() || ip.IsZero() || ip.IsLoopback() || ip.IsLinkLocal() ||
      ip.IsUniqueLocalIPv6() || !ip.IsPubliclyRoutable()) {
    return false;
  }
  return true;
}

// static
void MahoContentBlockerUpdateService::SetDesignatedUpdater(
    MahoContentBlockerUpdateService* service) {
  MahoContentBlockerUpdateService* previous = g_designated_updater;
  g_designated_updater = service;
  // Stop the outgoing designated service from driving the shared core before it
  // loses designation, so no stray timer/request survives the handover.
  if (previous && previous != service) {
    previous->OnModeChanged(/*is_native=*/false);
  }
}

bool MahoContentBlockerUpdateService::IsDesignatedUpdater() const {
  return g_designated_updater == this;
}

// static
void MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(
    bool is_native) {
  if (g_designated_updater) {
    g_designated_updater->OnModeChanged(is_native);
  }
}

// static
bool MahoContentBlockerUpdateService::TriggerDesignatedUpdate(
    const std::string& list_id) {
  if (!g_designated_updater) {
    return false;
  }
  return g_designated_updater->TriggerUpdate(list_id);
}

bool MahoContentBlockerUpdateService::TriggerUpdate(
    const std::string& list_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Manual updates do no network work outside native mode.
  if (is_shutdown_ || !is_native_mode_ || !IsDesignatedUpdater()) {
    return false;
  }
  if (list_id.empty()) {
    // Manual "update all" forces every enabled list now, ignoring cadence.
    ForceUpdateAllEnabled();
  } else {
    UpdateList(list_id);
  }
  return true;
}

MahoContentBlockerUpdateService::MahoContentBlockerUpdateService(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  state_provider_ = base::BindRepeating([]() -> std::string {
    MahoCore* core = maho::GetCore();
    return core ? maho::core::GetContentBlockerStateJson(core) : std::string();
  });
  apply_callback_ = base::BindRepeating([](const std::string& json) {
    MahoCore* core = maho::GetCore();
    if (!core || maho::IsBlockerWorkQuiesced()) {
      return std::string();
    }
    return maho::core::ApplyFilterListUpdateResultJson(core, json.c_str());
  });
  compile_callback_ = base::BindRepeating(
      []() { maho::PostBlockerEngineCompileAndInstall(FROM_HERE); });
}

MahoContentBlockerUpdateService::~MahoContentBlockerUpdateService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DoShutdown();
}

void MahoContentBlockerUpdateService::Shutdown() {
  DoShutdown();
}

void MahoContentBlockerUpdateService::DoShutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_) {
    return;
  }
  is_shutdown_ = true;
  // Invalidate before cancellation so no in-flight redirect/apply callback can
  // re-enter after the core is torn down.
  weak_factory_.InvalidateWeakPtrs();
  update_timer_.Stop();
  pending_queue_.clear();
  active_or_queued_ids_.clear();
  active_requests_.clear();
  if (g_designated_updater == this) {
    g_designated_updater = nullptr;
  }
}

void MahoContentBlockerUpdateService::SetStateProviderForTesting(
    StateProvider provider) {
  state_provider_ = std::move(provider);
}

void MahoContentBlockerUpdateService::SetApplyHooksForTesting(
    ApplyCallback apply,
    CompileCallback compile) {
  apply_callback_ = std::move(apply);
  compile_callback_ = std::move(compile);
}

void MahoContentBlockerUpdateService::FetchListForTesting(
    const std::string& list_id,
    const std::string& url) {
  FetchList(list_id, url, /*etag=*/std::string(),
            /*last_modified=*/std::string());
}

std::string MahoContentBlockerUpdateService::GetStateJson() {
  return state_provider_ ? state_provider_.Run() : std::string();
}

void MahoContentBlockerUpdateService::OnModeChanged(bool is_native) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_) {
    return;
  }
  if (!is_native) {
    // Pause: cancel everything so no bytes can apply, then keep a benign daily
    // recheck alive so a later resume path remains deterministic.
    CancelAllWorkForModeOff();
    ScheduleFromState();
    return;
  }
  is_native_mode_ = true;
  CheckForUpdates();
}

void MahoContentBlockerUpdateService::CheckForUpdates() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_ || !IsDesignatedUpdater()) {
    return;
  }

  std::string state_json = GetStateJson();
  auto parsed = base::JSONReader::Read(state_json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_dict()) {
    const auto& dict = parsed->GetDict();
    const std::string* mode = dict.FindString("mode");
    const bool native = mode && *mode == "native";

    if (!native) {
      // Authoritative non-native state must cancel any active/queued work even
      // if an explicit mode notification was missed, so no bytes apply. Marks
      // the service non-native; the following ScheduleFromState then yields a
      // benign daily timer without parsing lists.
      CancelAllWorkForModeOff();
      ScheduleFromState();
      return;
    }

    is_native_mode_ = true;
    const base::ListValue* lists = dict.FindList("lists");
    const int64_t now = NowSeconds();
    if (lists) {
      for (const auto& val : *lists) {
        const base::DictValue* list = val.GetIfDict();
        if (!list || !list->FindBool("enabled").value_or(false)) {
          continue;
        }
        const std::string* id = list->FindString("id");
        const std::string* url = list->FindString("url");
        if (!id || !url || ListDueSeconds(*list, now) > now) {
          continue;
        }
        const std::string* etag = list->FindString("etag");
        const std::string* lm = list->FindString("lastModified");
        FetchList(*id, *url, etag ? *etag : "", lm ? *lm : "");
      }
    }
  }

  ScheduleFromState();
}

void MahoContentBlockerUpdateService::CancelAllWorkForModeOff() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_native_mode_ = false;
  update_timer_.Stop();
  pending_queue_.clear();
  active_or_queued_ids_.clear();
  active_requests_.clear();
}

void MahoContentBlockerUpdateService::ForceUpdateAllEnabled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_) {
    return;
  }
  std::string state_json = GetStateJson();
  auto parsed = base::JSONReader::Read(state_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const auto& dict = parsed->GetDict();
  const std::string* mode = dict.FindString("mode");
  if (!mode || *mode != "native") {
    return;
  }
  is_native_mode_ = true;
  const base::ListValue* lists = dict.FindList("lists");
  if (!lists) {
    return;
  }
  for (const auto& val : *lists) {
    const base::DictValue* list = val.GetIfDict();
    if (!list || !list->FindBool("enabled").value_or(false)) {
      continue;
    }
    const std::string* id = list->FindString("id");
    const std::string* url = list->FindString("url");
    if (!id || !url) {
      continue;
    }
    const std::string* etag = list->FindString("etag");
    const std::string* lm = list->FindString("lastModified");
    // Force: no ListDueSeconds check. Dedup and the max-4 ceiling still apply.
    FetchList(*id, *url, etag ? *etag : "", lm ? *lm : "");
  }
}

void MahoContentBlockerUpdateService::UpdateList(const std::string& list_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_ || !is_native_mode_ || !IsDesignatedUpdater()) {
    return;
  }

  std::string state_json = GetStateJson();
  auto parsed = base::JSONReader::Read(state_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::ListValue* lists = parsed->GetDict().FindList("lists");
  if (!lists) {
    return;
  }
  for (const auto& val : *lists) {
    const base::DictValue* list = val.GetIfDict();
    if (!list) {
      continue;
    }
    const std::string* id = list->FindString("id");
    if (!id || *id != list_id) {
      continue;
    }
    const std::string* url = list->FindString("url");
    if (!url) {
      break;
    }
    const std::string* etag = list->FindString("etag");
    const std::string* lm = list->FindString("lastModified");
    FetchList(*id, *url, etag ? *etag : "", lm ? *lm : "");
    break;
  }
}

void MahoContentBlockerUpdateService::FetchList(
    const std::string& list_id,
    const std::string& url_str,
    const std::string& etag,
    const std::string& last_modified) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_ || !url_loader_factory_) {
    return;
  }
  if (!IsAllowedUpdateUrl(GURL(url_str))) {
    // A configured-but-unsafe URL is a per-list failure so health stays
    // authoritative; no request is issued.
    ForwardOutcome(list_id, kFailureStatus, std::nullopt, std::string(),
                   std::string());
    return;
  }
  // Deduplicate by list id across active and queued work.
  if (active_or_queued_ids_.contains(list_id)) {
    return;
  }
  active_or_queued_ids_.insert(list_id);
  PendingFetch pending_fetch;
  pending_fetch.list_id = list_id;
  pending_fetch.url = url_str;
  pending_fetch.etag = etag;
  pending_fetch.last_modified = last_modified;
  pending_queue_.push_back(std::move(pending_fetch));
  MaybeStartNextFetch();
}

void MahoContentBlockerUpdateService::MaybeStartNextFetch() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (!is_shutdown_ && !pending_queue_.empty() &&
         active_requests_.size() < kMaxInFlightDownloads) {
    PendingFetch fetch = std::move(pending_queue_.front());
    pending_queue_.pop_front();
    StartFetch(fetch);
  }
}

void MahoContentBlockerUpdateService::StartFetch(const PendingFetch& fetch) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  GURL url(fetch.url);
  if (!IsAllowedUpdateUrl(url)) {
    active_or_queued_ids_.erase(fetch.list_id);
    ForwardOutcome(fetch.list_id, kFailureStatus, std::nullopt, std::string(),
                   std::string());
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "GET";
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  // Bypass any configured proxy: a proxied transport reports only the proxy
  // endpoint (remote_endpoint) and maps to an unknown/public address space, so
  // both kURLLoadOptionBlockLocalRequest and the post-connect peer-IP guard
  // would evaluate the proxy instead of the true origin. Direct transport is
  // required for those SSRF safeguards to protect the actual destination;
  // mandatory-proxy environments therefore fail closed rather than weakening
  // the policy.
  resource_request->load_flags =
      net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_PROXY;
  if (!fetch.etag.empty()) {
    resource_request->headers.SetHeader("If-None-Match", fetch.etag);
  }
  if (!fetch.last_modified.empty()) {
    resource_request->headers.SetHeader("If-Modified-Since",
                                        fetch.last_modified);
  }

  auto request = std::make_unique<ActiveRequest>();
  request->list_id = fetch.list_id;
  request->loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kTrafficAnnotation);
  request->loader->SetTimeoutDuration(kTimeout);
  // Pre-send SSRF defense: instruct the Network Service to block this request
  // if DNS/redirect resolution lands in a local/private/link-local/loopback
  // address space, before any HTTP bytes are sent. The post-connect peer check
  // in FinalizeRequest remains as defense-in-depth.
  request->loader->SetURLLoaderFactoryOptions(
      network::mojom::kURLLoadOptionBlockLocalRequest);
  // Surface HTTP error / 304 status to OnComplete so this service classifies
  // success itself instead of trusting SimpleURLLoader's 2xx-only default.
  request->loader->SetAllowHttpErrorResults(true);
  request->consumer = std::make_unique<StreamConsumer>(this, request.get());

  ActiveRequest* request_ptr = request.get();
  network::SimpleURLLoader* loader_ptr = request->loader.get();
  active_requests_.push_back(std::move(request));

  loader_ptr->SetOnRedirectCallback(base::BindRepeating(
      &MahoContentBlockerUpdateService::OnRedirect, weak_factory_.GetWeakPtr(),
      fetch.list_id, request_ptr));
  loader_ptr->DownloadAsStream(url_loader_factory_.get(),
                               request_ptr->consumer.get());
}

void MahoContentBlockerUpdateService::OnRedirect(
    const std::string& list_id,
    ActiveRequest* request,
    const GURL& url_before_redirect,
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* removed_headers) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsAllowedUpdateUrl(redirect_info.new_url)) {
    return;
  }
  // Unsafe redirect: forward a failure for health, then destroy the loader
  // synchronously (safe inside a redirect callback) so the redirect is never
  // followed and no bytes are fetched from or applied out of the destination.
  EmitAbortFailure(request, /*destroy_sync=*/true);
}

void MahoContentBlockerUpdateService::OnStreamData(ActiveRequest* request,
                                                   std::string_view chunk,
                                                   base::OnceClosure resume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (request->finalized) {
    std::move(resume).Run();
    return;
  }
  if (request->body.size() + chunk.size() > kMaxBodySize) {
    // Overflow: never buffer beyond the cap. Emit a failure and do not resume,
    // so the paused request is torn down without exceeding memory or querying
    // the not-yet-finished loader.
    request->overflowed = true;
    EmitAbortFailure(request, /*destroy_sync=*/false);
    return;
  }
  request->body.append(chunk);
  std::move(resume).Run();
}

void MahoContentBlockerUpdateService::OnStreamComplete(ActiveRequest* request,
                                                       bool stream_success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (request->finalized) {
    return;
  }
  FinalizeRequest(request, stream_success);
}

void MahoContentBlockerUpdateService::FinalizeRequest(ActiveRequest* request,
                                                      bool stream_success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  request->finalized = true;

  network::SimpleURLLoader* loader = request->loader.get();
  const std::string list_id = request->list_id;

  const bool final_url_ok = IsAllowedUpdateUrl(loader->GetFinalURL());
  const bool transport_ok =
      loader->NetError() == net::OK && stream_success && !request->overflowed;

  int status_code = 0;
  std::string etag;
  std::string last_modified;
  bool peer_ok = false;
  if (const network::mojom::URLResponseHead* info = loader->ResponseInfo()) {
    if (info->headers) {
      status_code = info->headers->response_code();
      info->headers->EnumerateHeader(nullptr, "ETag", &etag);
      info->headers->EnumerateHeader(nullptr, "Last-Modified", &last_modified);
    }
    peer_ok = IsPubliclyRoutablePeer(info->remote_endpoint);
  }

  const bool safe = transport_ok && final_url_ok && peer_ok;

  int forward_status;
  std::optional<std::string> forward_body;
  if (safe && status_code == 304) {
    forward_status = 304;
  } else if (safe && status_code == 200) {
    forward_status = 200;
    forward_body = std::move(request->body);
  } else {
    // Failure: only trust a genuine HTTP error status from an otherwise-safe
    // exchange; otherwise use the sentinel and drop any unsafe validators.
    forward_status = (final_url_ok && peer_ok &&
                      loader->NetError() == net::OK && status_code >= 400)
                         ? status_code
                         : kFailureStatus;
    etag.clear();
    last_modified.clear();
  }

  active_or_queued_ids_.erase(list_id);
  // Remove before forwarding is unsafe (we run on the consumer's stack), so
  // defer destruction and drop the request from the active set now.
  RemoveRequestDeferred(request);
  ForwardOutcome(list_id, forward_status, std::move(forward_body), etag,
                 last_modified);
  MaybeStartNextFetch();
  ScheduleFromState();
}

void MahoContentBlockerUpdateService::ForwardOutcome(
    const std::string& list_id,
    int status_code,
    std::optional<std::string> body,
    const std::string& etag,
    const std::string& last_modified) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_ || !is_native_mode_) {
    return;
  }
  base::DictValue response;
  response.Set("listId", list_id);
  response.Set("statusCode", status_code);
  if (body) {
    response.Set("body", std::move(*body));
  }
  if (!etag.empty()) {
    response.Set("etag", etag);
  }
  if (!last_modified.empty()) {
    response.Set("lastModified", last_modified);
  }

  std::string json;
  base::JSONWriter::Write(response, &json);
  if (apply_callback_ &&
      CompileRequiredByMutationResult(apply_callback_.Run(json)) &&
      compile_callback_) {
    compile_callback_.Run();
  }
}

void MahoContentBlockerUpdateService::EmitAbortFailure(ActiveRequest* request,
                                                       bool destroy_sync) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  request->finalized = true;
  const std::string list_id = request->list_id;
  active_or_queued_ids_.erase(list_id);
  if (destroy_sync) {
    RemoveRequestSync(request);
  } else {
    RemoveRequestDeferred(request);
  }
  ForwardOutcome(list_id, kFailureStatus, std::nullopt, std::string(),
                 std::string());
  MaybeStartNextFetch();
  ScheduleFromState();
}

void MahoContentBlockerUpdateService::RemoveRequestSync(
    ActiveRequest* request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::erase_if(active_requests_,
                [request](const auto& item) { return item.get() == request; });
}

void MahoContentBlockerUpdateService::RemoveRequestDeferred(
    ActiveRequest* request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = std::find_if(
      active_requests_.begin(), active_requests_.end(),
      [request](const auto& item) { return item.get() == request; });
  if (it == active_requests_.end()) {
    return;
  }
  std::unique_ptr<ActiveRequest> owned = std::move(*it);
  active_requests_.erase(it);
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(owned));
}

void MahoContentBlockerUpdateService::ScheduleFromState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutdown_) {
    return;
  }

  base::TimeDelta delay = kRefreshCadence;
  if (is_native_mode_ && IsDesignatedUpdater()) {
    std::string state_json = GetStateJson();
    auto parsed = base::JSONReader::Read(state_json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const base::ListValue* lists = parsed->GetDict().FindList("lists");
      if (lists) {
        const int64_t now = NowSeconds();
        std::optional<int64_t> earliest;
        for (const auto& val : *lists) {
          const base::DictValue* list = val.GetIfDict();
          if (!list || !list->FindBool("enabled").value_or(false)) {
            continue;
          }
          const int64_t due = ListDueSeconds(*list, now);
          if (!earliest || due < *earliest) {
            earliest = due;
          }
        }
        if (earliest) {
          const base::TimeDelta floor =
              active_requests_.empty() ? kIdleFloor : kActiveFloor;
          delay = std::clamp(base::Seconds(*earliest - now), floor,
                             kRefreshCadence);
        }
      }
    }
  }

  update_timer_.Start(FROM_HERE, delay, this,
                      &MahoContentBlockerUpdateService::CheckForUpdates);
}

}  // namespace maho
