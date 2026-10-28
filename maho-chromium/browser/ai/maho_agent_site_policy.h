// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AGENT_SITE_POLICY_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_SITE_POLICY_H_

// Header-only C++ mirror of the maho-agent permission.rs site policy (GAP-6):
//   - per-site agent deny list (`agent.denied_origins`) -> agent_site_denied
//   - credential origin binding                        -> credential_origin_mismatch
// Both are deterministic refusals chosen by user data, never approval gates.
// Kept header-only so the MCP session target can use it without depending on
// maho_browser_ai_tools (which already depends on maho_mcp). The reason-code
// strings are machine-consumed and must stay byte-identical to the Rust
// constants AGENT_SITE_DENIED / CREDENTIAL_ORIGIN_MISMATCH.

#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_util.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace maho::ai {

inline constexpr std::string_view kAgentSiteDeniedReason = "agent_site_denied";
inline constexpr std::string_view kCredentialOriginMismatchReason =
    "credential_origin_mismatch";

// Parses the `agent.denied_origins` pref value: one entry per line (commas
// also accepted), trimmed, lowercased, blank entries dropped. Mirrors
// permission::parse_agent_denied_origins.
inline std::vector<std::string> ParseAgentDeniedOrigins(std::string_view raw) {
  std::vector<std::string> entries;
  size_t start = 0;
  while (start <= raw.size()) {
    size_t end = raw.find_first_of("\n,", start);
    if (end == std::string_view::npos) {
      end = raw.size();
    }
    std::string_view entry = base::TrimWhitespaceASCII(
        raw.substr(start, end - start), base::TRIM_ALL);
    if (!entry.empty()) {
      entries.push_back(base::ToLowerASCII(entry));
    }
    start = end + 1;
  }
  return entries;
}

namespace internal {
inline bool HostCoveredBy(std::string_view target_host,
                          std::string_view entry_host) {
  while (!entry_host.empty() && entry_host.back() == '.') {
    entry_host.remove_suffix(1);
  }
  if (entry_host.empty()) {
    return false;
  }
  if (target_host == entry_host) {
    return true;
  }
  return target_host.size() > entry_host.size() &&
         base::EndsWith(target_host, entry_host) &&
         target_host[target_host.size() - entry_host.size() - 1] == '.';
}
}  // namespace internal

// True when |target_origin| is covered by a deny-list entry. An entry is a
// full origin (scheme and port must match) or a bare host (any scheme/port);
// both cover label-boundary subdomains. Non-http(s) or host-less targets
// match nothing. Mirrors permission::agent_origin_denied.
inline bool AgentOriginDenied(const std::vector<std::string>& denied_origins,
                              std::string_view target_origin) {
  if (denied_origins.empty()) {
    return false;
  }
  const GURL target(base::TrimWhitespaceASCII(target_origin, base::TRIM_ALL));
  if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS() ||
      !target.has_host()) {
    return false;
  }
  const std::string target_host = base::ToLowerASCII(target.host());
  for (const std::string& raw_entry : denied_origins) {
    const std::string entry = base::ToLowerASCII(
        base::TrimWhitespaceASCII(raw_entry, base::TRIM_ALL));
    if (entry.empty()) {
      continue;
    }
    if (entry.find("://") != std::string::npos) {
      const GURL entry_url(entry);
      if (entry_url.is_valid() && entry_url.has_host() &&
          entry_url.scheme() == target.scheme() &&
          entry_url.EffectiveIntPort() == target.EffectiveIntPort() &&
          internal::HostCoveredBy(target_host, entry_url.host())) {
        return true;
      }
    } else {
      std::string_view host(entry);
      while (!host.empty() && host.back() == '/') {
        host.remove_suffix(1);
      }
      if (internal::HostCoveredBy(target_host, host)) {
        return true;
      }
    }
  }
  return false;
}

// Credential origin binding: the vault item origin and the target frame
// origin must be the same (scheme, host, port) origin — the vault's Exact
// match policy. Opaque, invalid, or non-http(s) origins never match (fail
// closed). Mirrors permission::authorize_credential_typing_for_origin with
// OriginMatchPolicy::Exact.
inline bool CredentialOriginMatches(std::string_view vault_origin,
                                    std::string_view target_frame_origin) {
  const GURL stored(vault_origin);
  const GURL target(target_frame_origin);
  if (!stored.is_valid() || !target.is_valid() ||
      !stored.SchemeIsHTTPOrHTTPS() || !target.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const url::Origin stored_origin = url::Origin::Create(stored);
  const url::Origin target_origin = url::Origin::Create(target);
  return !stored_origin.opaque() && stored_origin.IsSameOriginWith(target_origin);
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AGENT_SITE_POLICY_H_
