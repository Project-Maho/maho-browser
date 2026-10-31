// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_network_observer.h"

#include <string>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace maho {

namespace {

constexpr size_t kMaxCaptureEntries = 10000;
constexpr size_t kMaxCaptureBytes = 8 * 1024 * 1024;
constexpr size_t kMaxEntryBytes = 64 * 1024;

void SetCaptureLimits(base::DictValue& har,
                      size_t dropped_entries,
                      size_t retained_bytes) {
  if (dropped_entries == 0) {
    return;
  }
  har.Set("_truncated", true);
  har.Set("_droppedEntries", static_cast<int>(dropped_entries));
  har.Set("_retainedBytes", static_cast<int>(retained_bytes));
  har.Set("_limitBytes", static_cast<int>(kMaxCaptureBytes));
}

std::string RedactDataUrlPayload(const GURL& url) {
  std::string spec = url.spec();
  if (!url.SchemeIs(url::kDataScheme)) {
    return spec;
  }

  const size_t payload_separator = spec.find(',');
  if (payload_separator == std::string::npos) {
    return "data:[REDACTED_DATA_URL_PAYLOAD]";
  }
  spec.erase(payload_separator + 1);
  spec.append("[REDACTED_DATA_URL_PAYLOAD]");
  return spec;
}

base::DictValue RequestHeadersToDict(
    const net::HttpRequestHeaders& headers) {
  base::DictValue dict;
  net::HttpRequestHeaders::Iterator it(headers);
  while (it.GetNext()) {
    dict.Set(it.name(), it.value());
  }
  return dict;
}

base::DictValue ResponseHeadersToDict(
    const net::HttpResponseHeaders& headers) {
  base::DictValue dict;
  size_t iter = 0;
  std::string name;
  std::string value;
  while (headers.EnumerateHeaderLines(&iter, &name, &value)) {
    dict.Set(name, value);
  }
  return dict;
}

std::string StatusTextForCode(int status) {
  if (status <= 0) {
    return std::string();
  }
  return std::string(net::GetHttpReasonPhrase(status));
}

}  // namespace

MahoMcpNetworkObserver::Entry::Entry() = default;
MahoMcpNetworkObserver::Entry::~Entry() = default;
MahoMcpNetworkObserver::Entry::Entry(Entry&&) = default;
MahoMcpNetworkObserver::Entry& MahoMcpNetworkObserver::Entry::operator=(
    Entry&&) = default;

MahoMcpNetworkObserver::Snapshot::Snapshot() = default;
MahoMcpNetworkObserver::Snapshot::~Snapshot() = default;
MahoMcpNetworkObserver::Snapshot::Snapshot(Snapshot&&) = default;
MahoMcpNetworkObserver::Snapshot&
MahoMcpNetworkObserver::Snapshot::operator=(Snapshot&&) = default;

MahoMcpNetworkObserver::MahoMcpNetworkObserver() = default;
MahoMcpNetworkObserver::~MahoMcpNetworkObserver() = default;

void MahoMcpNetworkObserver::StartCapture() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  capturing_ = true;
  entries_.clear();
  dropped_entries_ = 0;
  retained_bytes_ = 0;
}

void MahoMcpNetworkObserver::StartCapture(content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Observe(web_contents);
  StartCapture();
}

void MahoMcpNetworkObserver::StopCapture() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  capturing_ = false;
  Observe(nullptr);
}

MahoMcpNetworkObserver::Snapshot MahoMcpNetworkObserver::TakeSnapshot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopCapture();
  Snapshot snapshot;
  snapshot.entries.swap(entries_);
  snapshot.dropped_entries = std::exchange(dropped_entries_, 0);
  snapshot.retained_bytes = std::exchange(retained_bytes_, 0);
  snapshot.truncated = snapshot.dropped_entries != 0;
  return snapshot;
}

void MahoMcpNetworkObserver::RecordEntry(Entry entry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!capturing_) {
    return;
  }
  size_t bytes = sizeof(Entry) + entry.url.size() + entry.method.size() +
                 entry.status_text.size() + entry.mime_type.size();
  for (const auto* headers : {&entry.request_headers, &entry.response_headers}) {
    for (const auto pair : *headers) {
      bytes += sizeof(base::Value) + pair.first.size() +
               pair.second.GetString().size();
    }
  }
  if (entries_.size() >= kMaxCaptureEntries || bytes > kMaxEntryBytes ||
      bytes > kMaxCaptureBytes - retained_bytes_) {
    ++dropped_entries_;
    return;
  }
  retained_bytes_ += bytes;
  entries_.push_back(std::move(entry));
}

base::DictValue MahoMcpNetworkObserver::GetHarRaw() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::DictValue har = BuildHarFromEntries(entries_);
  SetCaptureLimits(har, dropped_entries_, retained_bytes_);
  return har;
}

// static
base::DictValue MahoMcpNetworkObserver::BuildHarFromEntries(
    const std::vector<Entry>& entries) {
  base::DictValue har;
  har.Set("version", "1.2");

  base::DictValue creator;
  creator.Set("name", "Maho");
  creator.Set("version", "1.0");
  har.Set("creator", std::move(creator));

  base::ListValue entry_list;
  for (const auto& e : entries) {
    base::DictValue entry;

    if (e.start_time.is_null()) {
      entry.Set("startedDateTime", "1970-01-01T00:00:00.000Z");
    } else {
      entry.Set("startedDateTime", "1970-01-01T00:00:00.000Z");
    }

    entry.Set("time", static_cast<double>(e.duration.InMilliseconds()));

    base::DictValue request;
    request.Set("method", e.method);
    request.Set("url", e.url);
    request.Set("httpVersion", "HTTP/1.1");
    request.Set("_headersAvailable", e.request_headers_available);

    // Headers as dict for MahoMcpFirewall::WrapHar compatibility.
    base::DictValue req_headers;
    for (const auto pair : e.request_headers) {
      if (pair.second.is_string()) {
        req_headers.Set(pair.first, pair.second.GetString());
      } else {
        req_headers.Set(pair.first, "");
      }
    }
    request.Set("headers", std::move(req_headers));
    request.Set("headersSize", -1);
    request.Set("bodySize", static_cast<int>(e.request_size));
    entry.Set("request", std::move(request));

    base::DictValue response;
    response.Set("status", e.status);
    response.Set("statusText", e.status_text);
    response.Set("httpVersion", "HTTP/1.1");
    response.Set("_headersAvailable", e.response_headers_available);

    base::DictValue resp_headers;
    for (const auto pair : e.response_headers) {
      if (pair.second.is_string()) {
        resp_headers.Set(pair.first, pair.second.GetString());
      } else {
        resp_headers.Set(pair.first, "");
      }
    }
    response.Set("headers", std::move(resp_headers));
    response.Set("headersSize", -1);
    response.Set("bodySize", static_cast<int>(e.response_size));
    response.Set("_transferSize", static_cast<int>(e.response_transfer_size));

    base::DictValue content;
    content.Set("size", static_cast<int>(e.response_size));
    content.Set("mimeType", e.mime_type);
    response.Set("content", std::move(content));
    entry.Set("response", std::move(response));

    base::DictValue timings;
    timings.Set("send", 0);
    timings.Set("wait", static_cast<double>(e.duration.InMilliseconds()));
    timings.Set("receive", 0);
    entry.Set("timings", std::move(timings));

    entry_list.Append(base::Value(std::move(entry)));
  }

  har.Set("entries", std::move(entry_list));
  return har;
}

// static
base::DictValue MahoMcpNetworkObserver::BuildHarFromSnapshot(
    Snapshot snapshot) {
  base::DictValue har = BuildHarFromEntries(snapshot.entries);
  if (snapshot.truncated) {
    SetCaptureLimits(har, snapshot.dropped_entries, snapshot.retained_bytes);
  }
  return har;
}

// static
Redacted<base::DictValue>
MahoMcpNetworkObserver::BuildRedactedHarFromSnapshot(Snapshot snapshot) {
  return MahoMcpFirewall::WrapHar(BuildHarFromSnapshot(std::move(snapshot)));
}

void MahoMcpNetworkObserver::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!capturing_ || !navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument() ||
      navigation_handle->IsErrorPage()) {
    return;
  }

  Entry entry;
  entry.url = RedactDataUrlPayload(navigation_handle->GetURL());
  entry.method = navigation_handle->GetRequestMethod();
  if (entry.method.empty()) {
    entry.method = "GET";
  }
  entry.request_headers = RequestHeadersToDict(
      navigation_handle->GetRequestHeaders());

  const net::HttpResponseHeaders* headers =
      navigation_handle->GetResponseHeaders();
  entry.status = headers ? headers->response_code() : 200;
  if (headers) {
    entry.status_text = headers->GetStatusText();
    if (entry.status_text.empty()) {
      entry.status_text = StatusTextForCode(entry.status);
    }
    entry.response_headers = ResponseHeadersToDict(*headers);
  } else {
    entry.status_text = StatusTextForCode(entry.status);
    entry.response_headers_available = false;
  }
  entry.mime_type = web_contents() ? web_contents()->GetContentsMimeType()
                                   : std::string();
  entry.start_time = navigation_handle->NavigationStart();
  if (entry.start_time.is_null()) {
    entry.start_time = base::TimeTicks::Now();
  }
  const base::TimeTicks finish_time = base::TimeTicks::Now();
  if (finish_time >= entry.start_time) {
    entry.duration = finish_time - entry.start_time;
  }
  RecordEntry(std::move(entry));
}

void MahoMcpNetworkObserver::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const GURL& original_url,
    const blink::mojom::ResourceLoadInfo& resource_load_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!capturing_ || !resource_load_info.final_url.is_valid()) {
    return;
  }
  if (render_frame_host && render_frame_host->IsInPrimaryMainFrame() &&
      resource_load_info.request_destination ==
          network::mojom::RequestDestination::kDocument) {
    return;
  }

  Entry entry;
  entry.url = RedactDataUrlPayload(resource_load_info.final_url);
  entry.method =
      resource_load_info.method.empty() ? "GET" : resource_load_info.method;
  entry.status = resource_load_info.http_status_code;
  entry.status_text = StatusTextForCode(entry.status);
  entry.mime_type = resource_load_info.mime_type;
  entry.request_headers_available = false;
  entry.response_headers_available = false;
  entry.request_size = -1;
  entry.start_time = resource_load_info.load_timing_info.request_start;
  if (entry.start_time.is_null()) {
    entry.start_time = base::TimeTicks::Now();
  }

  const base::TimeTicks end =
      resource_load_info.load_timing_info.receive_headers_end;
  if (!end.is_null() && end >= entry.start_time) {
    entry.duration = end - entry.start_time;
  }
  entry.response_size = resource_load_info.raw_body_bytes.InBytes();
  entry.response_transfer_size =
      resource_load_info.total_received_bytes.InBytes();
  RecordEntry(std::move(entry));
}

}  // namespace maho
