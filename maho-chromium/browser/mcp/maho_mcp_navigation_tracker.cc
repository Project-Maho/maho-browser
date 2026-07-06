// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_navigation_tracker.h"

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"

namespace maho {

MahoMcpNavigationTracker::MahoMcpNavigationTracker(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoMcpNavigationTracker>(*web_contents) {}

MahoMcpNavigationTracker::~MahoMcpNavigationTracker() = default;

void MahoMcpNavigationTracker::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsErrorPage() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  NavigationEvent event;
  event.url = navigation_handle->GetURL().spec();
  if (web_contents()) {
    event.title = base::UTF16ToUTF8(web_contents()->GetTitle());
  }
  event.status_code = navigation_handle->GetResponseHeaders()
                          ? navigation_handle->GetResponseHeaders()->response_code()
                          : 200;
  event.timestamp_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();

  if (events_.size() >= kMaxEventBufferSize) {
    events_.erase(events_.begin());
  }
  events_.push_back(std::move(event));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoMcpNavigationTracker);

}  // namespace maho
