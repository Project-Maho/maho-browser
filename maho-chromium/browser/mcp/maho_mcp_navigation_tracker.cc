// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_navigation_tracker.h"

#include <cstdint>
#include <string_view>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/mojom/fetch_api.mojom-forward.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "url/gurl.h"

namespace maho {

MahoMcpNavigationTracker::MahoMcpNavigationTracker(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoMcpNavigationTracker>(*web_contents) {}

MahoMcpNavigationTracker::~MahoMcpNavigationTracker() = default;

MahoMcpNavigationTracker::NavigationEvent::NavigationEvent() = default;
MahoMcpNavigationTracker::NavigationEvent::~NavigationEvent() = default;
MahoMcpNavigationTracker::NavigationEvent::NavigationEvent(
    NavigationEvent&&) = default;
MahoMcpNavigationTracker::NavigationEvent&
MahoMcpNavigationTracker::NavigationEvent::operator=(NavigationEvent&&) =
    default;

MahoMcpNavigationTracker::BotChallengeStatus::BotChallengeStatus() = default;
MahoMcpNavigationTracker::BotChallengeStatus::~BotChallengeStatus() = default;
MahoMcpNavigationTracker::BotChallengeStatus::BotChallengeStatus(
    const BotChallengeStatus&) = default;
MahoMcpNavigationTracker::BotChallengeStatus&
MahoMcpNavigationTracker::BotChallengeStatus::operator=(
    const BotChallengeStatus&) = default;
MahoMcpNavigationTracker::BotChallengeStatus::BotChallengeStatus(
    BotChallengeStatus&&) = default;
MahoMcpNavigationTracker::BotChallengeStatus&
MahoMcpNavigationTracker::BotChallengeStatus::operator=(
    BotChallengeStatus&&) = default;

MahoMcpNavigationTracker::SubresourceEvidence::SubresourceEvidence() =
    default;
MahoMcpNavigationTracker::SubresourceEvidence::~SubresourceEvidence() =
    default;
MahoMcpNavigationTracker::SubresourceEvidence::SubresourceEvidence(
    SubresourceEvidence&&) = default;
MahoMcpNavigationTracker::SubresourceEvidence&
MahoMcpNavigationTracker::SubresourceEvidence::operator=(
    SubresourceEvidence&&) = default;

namespace {

using BotChallengeStatus = MahoMcpNavigationTracker::BotChallengeStatus;

constexpr int64_t NowMs() {
  return base::Time::Now().InMillisecondsSinceUnixEpoch();
}

std::string HostWithPort(const GURL& url) {
  const std::string port =
      url.has_port() ? ":" + std::string(url.port()) : std::string();
  return std::string(url.host()) + port;
}

bool UrlMatches(const GURL& url, std::string_view needle) {
  const std::string host = HostWithPort(url);
  if (host.find(needle) != std::string::npos) {
    return true;
  }
  return url.spec().find(needle) != std::string::npos;
}

std::string ChallengeUrlFor(const GURL& url,
                            std::string_view matched_host_or_path) {
  GURL challenge(matched_host_or_path);
  return challenge.is_valid() && !challenge.is_empty()
             ? challenge.spec()
             : url.possibly_invalid_spec();
}

std::string_view ProviderFromMatch(std::string_view needle) {
  if (needle == "challenges.cloudflare.com" ||
      needle == "cdn-cgi/challenge-platform" || needle == "turnstile") {
    return "cloudflare_turnstile";
  }
  if (needle == "ct.captcha-delivery.com" ||
      needle == "geo.captcha-delivery.com") {
    return "datadome";
  }
  if (needle == "token.awswaf.com") {
    return "aws_waf";
  }
  if (needle == "captcha.px-cdn.net") {
    return "perimeterx";
  }
  if (needle == "google.com/recaptcha" ||
      needle == "recaptcha/api.js" || needle == "www.google.com/recaptcha") {
    return "recaptcha";
  }
  return "generic";
}

// True when the navigation looks like an interstitial / block page rather
// than a page that merely embeds a captcha widget.
bool HasChallengeStatusCode(int status_code) {
  return status_code == 403 || status_code == 429;
}

// Challenge provider fingerprints matched against the final URL of a
// completed subresource load. `explicit_endpoint` marks endpoints that are
// only fetched while a challenge is actively being served/solved; all other
// markers (embedded widget scripts, always-on sensor scripts) are passive
// presence and never report a block on their own.
struct SubresourceChallengeMarker {
  std::string_view needle;
  std::string_view provider;
  bool explicit_endpoint;
};

constexpr SubresourceChallengeMarker kSubresourceChallengeMarkers[] = {
    // Cloudflare: challenge solving host + challenge platform scripts are
    // explicit; the Turnstile widget embed is passive.
    {"challenges.cloudflare.com", "cloudflare_turnstile", true},
    {"cdn-cgi/challenge-platform", "cloudflare_turnstile", true},
    {"turnstile", "cloudflare_turnstile", false},
    // DataDome: ct./geo. captcha-delivery.com endpoints are explicit; the
    // always-on tag script is passive.
    {"captcha-delivery.com", "datadome", true},
    {"datadome", "datadome", false},
    // Kasada: kpsdk client / io endpoints (passive until challenge).
    {"kpsdk", "kasada", false},
    {"kasada", "kasada", false},
    // AWS WAF: token endpoint is explicit, integration scripts passive.
    {"token.awswaf.com", "aws_waf", true},
    {"awswaf", "aws_waf", false},
    // Akamai: block-page challenge path is explicit, sensor path passive.
    {"_sec/cp_challenge", "akamai", true},
    {"akam/", "akamai", false},
    // PerimeterX: captcha host / payload are explicit, sensor host passive.
    {"captcha.px-cdn.net", "perimeterx", true},
    {"px-captcha", "perimeterx", true},
    {"px-cdn.net", "perimeterx", false},
    // reCAPTCHA / hCaptcha widget embeds are passive.
    {"recaptcha", "recaptcha", false},
    {"hcaptcha", "hcaptcha", false},
};

bool HasChallengePageText(std::string_view title, std::string_view evidence) {
  static constexpr std::string_view kChallengeText[] = {
      "Just a moment...",       "_sec/cp_challenge",
      "Attention Required!",    "Access Denied",
      "Security Check",         "Bot Verification",
      "Please verify you are a human",
  };
  for (std::string_view needle : kChallengeText) {
    if (title.find(needle) != std::string::npos ||
        evidence.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

// static
BotChallengeStatus MahoMcpNavigationTracker::DetectBotChallenge(
    const GURL& url,
    int status_code,
    const std::string& title,
    const std::string& body_or_evidence) {
  BotChallengeStatus status;
  status.status_code = status_code;
  status.challenge_url = url.possibly_invalid_spec();
  status.detected_at_ms = NowMs();

  const std::string_view evidence = body_or_evidence;
  const std::string_view title_view = title;

  // (needle, interactive_captcha) pairs per provider. Note that provider
  // fingerprints must be matched against the URL host/path, title, and
  // evidence together so a block page and an embedded widget both match.
  static constexpr std::string_view kInteractiveProviderNeedles[] = {
      "challenges.cloudflare.com",   "cdn-cgi/challenge-platform",
      "datadome.js",                 "ct.captcha-delivery.com",
      "geo.captcha-delivery.com",    "kpsdk",
      "149e9513-01fa-4fb0-8445-30e439724115",
      "token.awswaf.com",            "awswaf",
      "_sec/cp_challenge",           "captcha.px-cdn.net",
      "px.js",                       "google.com/recaptcha",
      "www.google.com/recaptcha",    "recaptcha/api.js",
      "hcaptcha.com/1/api.js",
  };

  std::string_view matched_needle;
  bool matched = false;
  for (std::string_view needle : kInteractiveProviderNeedles) {
    if (UrlMatches(url, needle) ||
        title_view.find(needle) != std::string::npos ||
        evidence.find(needle) != std::string::npos) {
      matched_needle = needle;
      matched = true;
      break;
    }
  }

  if (matched) {
    status.provider = std::string(ProviderFromMatch(matched_needle));
    status.reason = "interactive_captcha";
    if (HasChallengeStatusCode(status_code) ||
        HasChallengePageText(title_view, evidence)) {
      status.is_blocked = true;
      if (title_view.find("Just a moment...") != std::string::npos ||
          evidence.find("Just a moment...") != std::string::npos) {
        status.reason = "interstitial";
      } else if (title_view.find("_sec/cp_challenge") != std::string::npos ||
                 evidence.find("_sec/cp_challenge") != std::string::npos) {
        status.reason = "waf_block_page";
      }
    } else {
      // Passive presence of a script alone is not a block.
      status.is_blocked = false;
      status.challenge_url = ChallengeUrlFor(url, matched_needle);
    }
  } else if (HasChallengeStatusCode(status_code) ||
             HasChallengePageText(title_view, evidence)) {
    status.provider = "generic";
    status.reason = "waf_block_page";
    status.is_blocked = true;
  } else {
    status.provider = "generic";
    status.reason = "js_challenge";
    status.is_blocked = false;
  }

  return status;
}

base::DictValue MahoMcpNavigationTracker::BotChallengeStatus::ToDict() const {
  base::DictValue dict;
  dict.Set("is_blocked", is_blocked);
  dict.Set("provider", provider);
  dict.Set("reason", reason);
  dict.Set("challenge_url", challenge_url);
  dict.Set("status_code", status_code);
  dict.Set("detected_at_ms", static_cast<double>(detected_at_ms));
  return dict;
}

base::DictValue MahoMcpNavigationTracker::SubresourceEvidence::ToDict()
    const {
  base::DictValue dict;
  dict.Set("final_url", final_url);
  dict.Set("provider", provider);
  dict.Set("reason", reason);
  dict.Set("is_blocked", is_blocked);
  dict.Set("status_code", status_code);
  dict.Set("detected_at_ms", static_cast<double>(detected_at_ms));
  return dict;
}

void MahoMcpNavigationTracker::RecordSubresourceEvidence(
    const blink::mojom::ResourceLoadInfo& resource_load_info) {
  const GURL& final_url = resource_load_info.final_url;
  if (!final_url.is_valid()) {
    return;
  }

  const std::string spec = final_url.possibly_invalid_spec();
  std::string_view matched_provider;
  bool explicit_endpoint = false;
  for (const SubresourceChallengeMarker& marker : kSubresourceChallengeMarkers) {
    if (spec.find(marker.needle) != std::string::npos) {
      matched_provider = marker.provider;
      explicit_endpoint = marker.explicit_endpoint;
      break;
    }
  }
  if (matched_provider.empty()) {
    return;
  }

  SubresourceEvidence evidence;
  evidence.final_url = spec;
  evidence.provider = std::string(matched_provider);
  evidence.status_code = resource_load_info.http_status_code;
  evidence.detected_at_ms = NowMs();

  const int status_code = resource_load_info.http_status_code;
  const bool blocked = explicit_endpoint || HasChallengeStatusCode(status_code);
  evidence.is_blocked = blocked;
  if (explicit_endpoint) {
    evidence.reason = "challenge_endpoint";
  } else if (blocked) {
    evidence.reason = "challenge_status_code";
  } else {
    evidence.reason = "passive_presence";
  }

  if (subresource_evidence_.size() >= kMaxSubresourceEvidenceSize) {
    subresource_evidence_.erase(subresource_evidence_.begin());
  }
  subresource_evidence_.push_back(std::move(evidence));

  if (blocked) {
    BotChallengeStatus status;
    status.is_blocked = true;
    status.provider = std::string(matched_provider);
    status.reason = evidence.reason;
    status.challenge_url = spec;
    status.status_code = status_code;
    status.detected_at_ms = evidence.detected_at_ms;
    UpdateChallengeStatus(status);
  }
}

void MahoMcpNavigationTracker::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const GURL& original_url,
    const blink::mojom::ResourceLoadInfo& resource_load_info) {
  RecordSubresourceEvidence(resource_load_info);
}

void MahoMcpNavigationTracker::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }

  // A committed primary main frame navigation starts a new document:
  // subresource evidence is scoped per document, so drop whatever the
  // previous document accumulated. Same-document navigations and error
  // pages keep the current document's evidence.
  if (!navigation_handle->IsSameDocument() &&
      !navigation_handle->IsErrorPage()) {
    subresource_evidence_.clear();
  }

  const GURL url = navigation_handle->GetURL();
  const net::HttpResponseHeaders* response_headers =
      navigation_handle->GetResponseHeaders();
  int status_code = response_headers ? response_headers->response_code() : 200;
  std::string title;
  if (web_contents()) {
    title = base::UTF16ToUTF8(web_contents()->GetTitle());
  }

  // Error-page navigations never go into event history, but challenge evidence
  // on them still updates status.
  BotChallengeStatus challenge;
  if (navigation_handle->IsSameDocument()) {
    challenge = DetectBotChallenge(url, status_code, title,
                                   /*body_or_evidence=*/std::string());
  } else if (navigation_handle->IsErrorPage()) {
    challenge =
        DetectBotChallenge(url, status_code, title, "error page navigation");
  } else {
    challenge = DetectBotChallenge(url, status_code, title,
                                   /*body_or_evidence=*/std::string());
  }
  if (challenge.is_blocked) {
    // Preserve the challenge_url an explicit subresource endpoint pinned to
    // the current document; the navigation URL would otherwise overwrite it
    // with the (still blocked) page URL.
    const std::string previous_challenge_url = current_challenge_.challenge_url;
    UpdateChallengeStatus(challenge);
    if (current_challenge_.challenge_url.empty() &&
        !previous_challenge_url.empty()) {
      current_challenge_.challenge_url = previous_challenge_url;
    }
  }

  if (navigation_handle->IsErrorPage()) {
    return;
  }

  NavigationEvent event;
  event.url = url.spec();
  event.title = title;
  event.status_code = status_code;
  event.timestamp_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();

  if (events_.size() >= kMaxEventBufferSize) {
    events_.erase(events_.begin());
  }
  events_.push_back(std::move(event));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoMcpNavigationTracker);

}  // namespace maho
