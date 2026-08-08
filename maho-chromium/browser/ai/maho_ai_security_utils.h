// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_SECURITY_UTILS_H_
#define MAHO_BROWSER_AI_MAHO_AI_SECURITY_UTILS_H_

// Header-only AI security primitives shared across AI attachment / context
// flows. No .cc counterpart needed — all helpers are inline/constexpr so this
// file can be included without touching BUILD.gn.
//
// Three concerns:
//   1. Profile eligibility  — OTR profiles must never see AI context flows.
//   2. URL eligibility      — internal and sensitive-scheme URLs must be
//                             withheld from AI prompt assembly.
//   3. Prompt safety        — untrusted content (page text, tab titles, history
//                             snippets) must be delimited so the AI cannot
//                             confuse it with trusted system instructions.

#include <string>
#include <string_view>

#include "chrome/browser/profiles/profile.h"
#include "maho/browser/ai/maho_credential_redaction.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "url/gurl.h"

namespace maho::ai_security {

// ---------------------------------------------------------------------------
// 1. Profile eligibility
// ---------------------------------------------------------------------------

// Returns true when |profile| is safe for AI context attachment.
// Null profiles and OTR profiles (Incognito, Guest) are always rejected.
inline bool IsProfileEligible(Profile* profile) {
  if (!profile) {
    return false;
  }
  return !profile->IsOffTheRecord();
}

// ---------------------------------------------------------------------------
// 2. URL eligibility
// ---------------------------------------------------------------------------

// Returns true when |url| must not be fed into AI prompt assembly.
//
// Blocked categories:
//   • Internal browser pages (chrome://, devtools://, about:, …) — reuses the
//     canonical MahoDisplayPolicy::IsInternalBrowserPage check.
//   • Dangerous / local-file schemes that leak private data or allow
//     injection: file:, view-source:, data:, javascript:, blob:, filesystem:.
inline bool IsUrlBlocked(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return true;
  }
  // Reuse the shared internal-browser-page predicate (covers chrome://,
  // chrome-untrusted://, devtools://, about:, chrome-extension://, etc.).
  if (MahoDisplayPolicy::IsInternalBrowserPage(url)) {
    return true;
  }
  // Additional sensitive schemes not covered by IsInternalBrowserPage.
  if (url.SchemeIsFile()) {
    return true;
  }
  if (url.SchemeIs("view-source")) {
    return true;
  }
  if (url.SchemeIs("data")) {
    return true;
  }
  if (url.SchemeIs("javascript")) {
    return true;
  }
  if (url.SchemeIs("blob")) {
    return true;
  }
  if (url.SchemeIs("filesystem")) {
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// 3. URL redaction for display / prompt transport
// ---------------------------------------------------------------------------

// Returns a redacted URL string safe for inclusion in AI prompts and event
// payloads. Strips query, fragment, username, and password; preserves
// scheme, host, port, and path. Returns an empty string for invalid/blocked
// URLs so callers can use `.empty()` as a sentinel.
//
// Example:
//   https://user:pw@example.com/path?q=secret#frag  →
//   https://example.com/path
inline std::string RedactUrlForAi(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return std::string();
  }
  GURL::Replacements replacements;
  replacements.ClearQuery();
  replacements.ClearRef();
  replacements.ClearUsername();
  replacements.ClearPassword();
  return url.ReplaceComponents(replacements).spec();
}

// Convenience overload: parses |url_spec| then redacts.
inline std::string RedactUrlForAi(std::string_view url_spec) {
  return RedactUrlForAi(GURL(url_spec));
}

// ---------------------------------------------------------------------------
// 4. Untrusted-content delimiting for prompt assembly
// ---------------------------------------------------------------------------

// Fixed delimiter tokens. Must not appear verbatim inside any user-controlled
// string; EscapeForDelimiter() below enforces this invariant.
// Using const char[] rather than constexpr string_view avoids an Apple
// libc++ constexpr restriction on string_view construction from literals.
static const char kUntrustedContentBegin[] =
    "<<<MAHO_UNTRUSTED_CONTENT_BEGIN>>>";
static const char kUntrustedContentEnd[] =
    "<<<MAHO_UNTRUSTED_CONTENT_END>>>";

// Escapes any literal occurrence of the delimiter tokens inside |input| so
// that injected content can never close or reopen the untrusted block.
// Replacement uses a visually distinct inert tag so the model sees the
// collision but cannot exploit it.
inline std::string EscapeForDelimiter(std::string_view input) {
  std::string out;
  out.reserve(input.size());

  const std::string_view kBegin(kUntrustedContentBegin);
  const std::string_view kEnd(kUntrustedContentEnd);
  const std::string_view kBeginEsc("<<<[ESCAPED_BEGIN]>>>");
  const std::string_view kEndEsc("<<<[ESCAPED_END]>>>");

  size_t pos = 0;
  while (pos < input.size()) {
    if (input.substr(pos, kBegin.size()) == kBegin) {
      out.append(kBeginEsc);
      pos += kBegin.size();
    } else if (input.substr(pos, kEnd.size()) == kEnd) {
      out.append(kEndEsc);
      pos += kEnd.size();
    } else {
      out += input[pos++];
    }
  }
  return out;
}

// Wraps |content| in untrusted-data delimiters after escaping any collision.
// Use this for every piece of page text / tab content / history snippet that
// is concatenated into a final prompt string.
inline std::string WrapUntrusted(std::string_view content) {
  return std::string(kUntrustedContentBegin) + "\n" +
         EscapeForDelimiter(content) + "\n" +
         std::string(kUntrustedContentEnd);
}

// ---------------------------------------------------------------------------
// 5. Credential-token redaction for AI-facing page text
// ---------------------------------------------------------------------------

// True for base64url alphabet plus padding/url-safe variants. Kept local so
// this header stays dependency-free (no re2) and safe to include anywhere.
inline std::string RedactCredentialTokens(std::string_view input) {
  return credential_redaction::RedactCredentialText(input);
}

}  // namespace maho::ai_security

#endif  // MAHO_BROWSER_AI_MAHO_AI_SECURITY_UTILS_H_
