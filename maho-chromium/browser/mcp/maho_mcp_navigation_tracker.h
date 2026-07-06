// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_TRACKER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_TRACKER_H_

#include <string>
#include <vector>

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace maho {

class MahoMcpNavigationTracker
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoMcpNavigationTracker> {
 public:
  struct NavigationEvent {
    std::string url;
    std::string title;
    int status_code = 200;
    int64_t timestamp_ms = 0;
  };

  static constexpr size_t kMaxEventBufferSize = 100;

  ~MahoMcpNavigationTracker() override;

  const std::vector<NavigationEvent>& events() const { return events_; }
  void Clear() { events_.clear(); }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<MahoMcpNavigationTracker>;
  explicit MahoMcpNavigationTracker(content::WebContents* web_contents);

  std::vector<NavigationEvent> events_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_TRACKER_H_
