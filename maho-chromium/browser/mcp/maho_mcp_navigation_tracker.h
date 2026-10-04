// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_TRACKER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_TRACKER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/values.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-forward.h"
#include "url/gurl.h"

namespace maho {

class MahoMcpNavigationTracker
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoMcpNavigationTracker> {
 public:
  struct NavigationEvent {
    NavigationEvent();
    ~NavigationEvent();
    NavigationEvent(NavigationEvent&&);
    NavigationEvent& operator=(NavigationEvent&&);

    std::string url;
    std::string title;
    int status_code = 200;
    int64_t timestamp_ms = 0;
  };

  // Result of a WAF / CAPTCHA signature scan for a single navigation. Kept
  // separate from NavigationEvent history.
  struct BotChallengeStatus {
    BotChallengeStatus();
    ~BotChallengeStatus();
    BotChallengeStatus(const BotChallengeStatus&);
    BotChallengeStatus& operator=(const BotChallengeStatus&);
    BotChallengeStatus(BotChallengeStatus&&);
    BotChallengeStatus& operator=(BotChallengeStatus&&);

    bool is_blocked = false;
    std::string provider;  // e.g. "cloudflare_turnstile", "datadome",
                           // "kasada", "aws_waf", "akamai", "perimeterx",
                           // "recaptcha", "hcaptcha", "generic"
    std::string reason;    // e.g. "waf_block_page", "interactive_captcha",
                           // "interstitial", "js_challenge"
    std::string challenge_url;
    int status_code = 0;
    int64_t detected_at_ms = 0;

    base::DictValue ToDict() const;
  };

  // HAR-independent challenge evidence observed on a single completed
  // subresource load. Unlike NavigationEvent history this is retained
  // regardless of MahoMcpNetworkObserver capture state, and it is cleared
  // when a new primary main frame document commits.
  struct SubresourceEvidence {
    SubresourceEvidence();
    ~SubresourceEvidence();
    SubresourceEvidence(SubresourceEvidence&&);
    SubresourceEvidence& operator=(SubresourceEvidence&&);

    std::string final_url;
    std::string provider;
    std::string reason;
    bool is_blocked = false;
    int status_code = 0;
    int64_t detected_at_ms = 0;

    base::DictValue ToDict() const;
  };

  static constexpr size_t kMaxEventBufferSize = 100;
  // Upper bound on retained subresource evidence per document.
  static constexpr size_t kMaxSubresourceEvidenceSize = 50;

  ~MahoMcpNavigationTracker() override;

  const std::vector<NavigationEvent>& events() const { return events_; }
  void Clear() { events_.clear(); }

  const BotChallengeStatus& current_challenge() const {
    return current_challenge_;
  }
  void UpdateChallengeStatus(const BotChallengeStatus& status) {
    current_challenge_ = status;
  }

  // Subresource challenge evidence for the current document. Retained
  // independently of HAR capture state.
  const std::vector<SubresourceEvidence>& subresource_evidence() const {
    return subresource_evidence_;
  }

  // Scans the navigation's URL, page title, and body/evidence text for known
  // WAF / CAPTCHA provider signatures. Passive presence of a provider script
  // alone does not report a block; a challenge is only reported when it is
  // accompanied by a challenge status code (403, 429) or challenge page
  // title / interstitial text.
  static BotChallengeStatus DetectBotChallenge(
      const GURL& url,
      int status_code,
      const std::string& title,
      const std::string& body_or_evidence);

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void ResourceLoadComplete(
      content::RenderFrameHost* render_frame_host,
      const content::GlobalRequestID& request_id,
      const GURL& original_url,
      const blink::mojom::ResourceLoadInfo& resource_load_info) override;

  // Collects challenge evidence from a completed subresource load without
  // consulting MahoMcpNetworkObserver or its HAR capture state. A bare
  // provider fingerprint in the final URL is passive evidence; only an
  // explicit challenge endpoint or a challenge status code (403 / 429)
  // upgrades it to a block and updates current_challenge().
  void RecordSubresourceEvidence(
      const blink::mojom::ResourceLoadInfo& resource_load_info);

 private:
  friend class content::WebContentsUserData<MahoMcpNavigationTracker>;
  explicit MahoMcpNavigationTracker(content::WebContents* web_contents);

  std::vector<NavigationEvent> events_;
  BotChallengeStatus current_challenge_;
  std::vector<SubresourceEvidence> subresource_evidence_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_TRACKER_H_
