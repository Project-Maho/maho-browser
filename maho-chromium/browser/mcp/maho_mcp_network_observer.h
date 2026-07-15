// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_NETWORK_OBSERVER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_NETWORK_OBSERVER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"

namespace blink::mojom {
class ResourceLoadInfo;
}

namespace content {
class NavigationHandle;
class RenderFrameHost;
class WebContents;
struct GlobalRequestID;
}  // namespace content

namespace maho {

// Captures network requests for a tab and produces HAR 1.2 JSON.
class MahoMcpNetworkObserver : public content::WebContentsObserver {
 public:
  struct Entry {
    Entry();
    ~Entry();
    Entry(Entry&&);
    Entry& operator=(Entry&&);

    std::string url;
    std::string method;
    int status = 0;
    std::string status_text;
    std::string mime_type;
    base::TimeTicks start_time;
    base::TimeDelta duration;
    int64_t request_size = -1;
    int64_t response_size = -1;
    int64_t response_transfer_size = -1;
    base::DictValue request_headers;
    base::DictValue response_headers;
    bool request_headers_available = true;
    bool response_headers_available = true;
  };

  MahoMcpNetworkObserver();
  ~MahoMcpNetworkObserver() override;

  MahoMcpNetworkObserver(const MahoMcpNetworkObserver&) = delete;
  MahoMcpNetworkObserver& operator=(const MahoMcpNetworkObserver&) = delete;

  void StartCapture();
  void StartCapture(content::WebContents* web_contents);
  void StopCapture();
  bool is_capturing() const { return capturing_; }

  // Returns HAR 1.2 JSON dict with "version", "creator", "entries" at root.
  // MUST be wrapped by MahoMcpFirewall::WrapHar before egress.
  // Test-only — do not call from production code.
  base::DictValue GetHarRaw() const;

  // Production egress path — always wrapped through the firewall.
  Redacted<base::DictValue> GetHar() const {
    return MahoMcpFirewall::WrapHar(GetHarRaw());
  }

  // Static helper: builds HAR 1.2 dict from a vector of entries.
  // Unit-testable without WebContents.
  static base::DictValue BuildHarFromEntries(
      const std::vector<Entry>& entries);

 private:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void ResourceLoadComplete(
      content::RenderFrameHost* render_frame_host,
      const content::GlobalRequestID& request_id,
      const blink::mojom::ResourceLoadInfo& resource_load_info) override;
  void RecordEntry(Entry entry);

  bool capturing_ = false;
  std::vector<Entry> entries_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_NETWORK_OBSERVER_H_
