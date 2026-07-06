// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_network_observer.h"

#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"

namespace maho {

MahoMcpNetworkObserver::Entry::Entry() = default;
MahoMcpNetworkObserver::Entry::~Entry() = default;
MahoMcpNetworkObserver::Entry::Entry(Entry&&) = default;
MahoMcpNetworkObserver::Entry& MahoMcpNetworkObserver::Entry::operator=(
    Entry&&) = default;

MahoMcpNetworkObserver::MahoMcpNetworkObserver() = default;
MahoMcpNetworkObserver::~MahoMcpNetworkObserver() = default;

void MahoMcpNetworkObserver::StartCapture() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  capturing_ = true;
  entries_.clear();
}

void MahoMcpNetworkObserver::StopCapture() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  capturing_ = false;
}

void MahoMcpNetworkObserver::TestOnly_AddEntry(Entry entry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  entries_.push_back(std::move(entry));
}

base::DictValue MahoMcpNetworkObserver::GetHarRaw() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return BuildHarFromEntries(entries_);
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

}  // namespace maho
