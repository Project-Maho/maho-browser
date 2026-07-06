// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_firewall.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr std::array<std::string_view, 8> kHardScrubHeaderNames = {
    "cookie",
    "set-cookie",
    "authorization",
    "proxy-authorization",
    "x-csrf-token",
    "x-auth-token",
    "x-api-key",
    "x-amz-security-token",
};

constexpr std::array<std::string_view, 8> kHardScrubParamNames = {
    "token",
    "access_token",
    "id_token",
    "refresh_token",
    "session_id",
    "auth",
    "code",
    "state",
};

const re2::RE2& CredentialNamePattern() {
  static const base::NoDestructor<re2::RE2> kPattern(
      "(?i)(auth|token|secret|key|session)");
  return *kPattern;
}

bool MatchesAnyLowercase(std::string_view needle,
                         const std::array<std::string_view, 8>& list) {
  std::string lower(needle);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (const auto& entry : list) {
    if (lower == entry) {
      return true;
    }
  }
  return false;
}

bool IsCredentialDictKey(std::string_view key) {
  std::string lower(key);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lower == "key") {
    return false;
  }
  if (lower.find("key") != std::string::npos && 
      (lower.find("api") != std::string::npos ||
       lower.find("access") != std::string::npos ||
       lower.find("secret") != std::string::npos ||
       lower.find("private") != std::string::npos ||
       lower.find("auth") != std::string::npos ||
       lower.find("token") != std::string::npos)) {
    return true;
  }
  static const base::NoDestructor<re2::RE2> kDictKeyPattern(
      "(?i)(auth|token|secret|session|password)");
  return re2::RE2::PartialMatch(std::string(key), *kDictKeyPattern);
}

}  // namespace

// static
std::string MahoMcpFirewall::RedactPasswordFields(
    std::string_view html_or_text) {
  static const base::NoDestructor<re2::RE2> kPasswordInputPattern(
      R"((?is)(<input\b[^>]*\btype\s*=\s*["']password["'][^>]*)"
      R"(\bvalue\s*=\s*["'])[^"']*(["']))");
  std::string out(html_or_text);
  re2::RE2::GlobalReplace(&out, *kPasswordInputPattern,
                          base::StrCat({"\\1", kRedacted, "\\2"}));
  return out;
}

// static
bool MahoMcpFirewall::IsCredentialHeaderName(std::string_view name) {
  if (MatchesAnyLowercase(name, kHardScrubHeaderNames)) {
    return true;
  }
  return re2::RE2::PartialMatch(std::string(name), CredentialNamePattern());
}

// static
bool MahoMcpFirewall::IsCredentialParamName(std::string_view name) {
  if (MatchesAnyLowercase(name, kHardScrubParamNames)) {
    return true;
  }
  return re2::RE2::PartialMatch(std::string(name), CredentialNamePattern());
}

// static
void MahoMcpFirewall::RedactHeaders(base::DictValue& headers) {
  std::vector<std::string> keys_to_redact;
  keys_to_redact.reserve(headers.size());
  for (const auto pair : headers) {
    if (IsCredentialHeaderName(pair.first)) {
      keys_to_redact.push_back(pair.first);
    }
  }
  for (const auto& key : keys_to_redact) {
    headers.Set(key, kRedacted);
  }
}

// static
void MahoMcpFirewall::RedactAxTreeAutofill(base::DictValue& node) {
  const std::string* role = node.FindString("role");
  const bool is_credential_context = role && (*role == "textbox" ||
                                              *role == "searchbox" ||
                                              *role == "combobox" ||
                                              *role == "password");

  if (is_credential_context) {
    if (node.contains("value")) {
      node.Set("value", kRedacted);
    }
  }

  // Always redact autocomplete suggestion lists regardless of role.
  if (node.contains("AXAutocompleteValue")) {
    node.Set("AXAutocompleteValue", kRedacted);
  }
  if (base::ListValue* suggestions = node.FindList("autocomplete")) {
    for (auto& item : *suggestions) {
      item = base::Value(kRedacted);
    }
  }

  // Recurse into children.
  if (base::ListValue* children = node.FindList("children")) {
    for (auto& child : *children) {
      if (base::DictValue* child_dict = child.GetIfDict()) {
        RedactAxTreeAutofill(*child_dict);
      }
    }
  }
}

// static
std::string MahoMcpFirewall::RedactUrl(std::string_view url) {
  GURL gurl((std::string(url)));
  if (!gurl.is_valid()) {
    return std::string(url);
  }

  auto scrub_query_or_ref = [](std::string_view input) -> std::string {
    if (input.empty()) {
      return std::string();
    }
    std::string result;
    result.reserve(input.size());
    size_t i = 0;
    bool first = true;
    while (i < input.size()) {
      size_t amp = input.find('&', i);
      std::string_view pair = input.substr(
          i, amp == std::string_view::npos ? input.size() - i : amp - i);
      size_t eq = pair.find('=');
      std::string_view name =
          eq == std::string_view::npos ? pair : pair.substr(0, eq);
      std::string_view value =
          eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1);
      if (!first) {
        result.push_back('&');
      }
      first = false;
      result.append(name);
      if (eq != std::string_view::npos) {
        result.push_back('=');
        if (IsCredentialParamName(name)) {
          result.append(kRedacted);
        } else {
          result.append(value);
        }
      }
      if (amp == std::string_view::npos) {
        break;
      }
      i = amp + 1;
    }
    return result;
  };

  const std::string clean_query = scrub_query_or_ref(gurl.query());
  const std::string clean_ref = scrub_query_or_ref(gurl.ref());

  GURL::Replacements replacements;
  if (clean_query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(clean_query);
  }
  if (clean_ref.empty()) {
    replacements.ClearRef();
  } else {
    replacements.SetRefStr(clean_ref);
  }
  return gurl.ReplaceComponents(replacements).spec();
}

// static
std::string MahoMcpFirewall::RedactString(std::string_view raw) {
  std::string out(raw);

  // Redact JWT: eyJ[a-zA-Z0-9_\-]+\.[a-zA-Z0-9_\-]+\.[a-zA-Z0-9_\-]+ -> [REDACTED_JWT]
  static const base::NoDestructor<re2::RE2> kJwtPattern(
      R"(eyJ[a-zA-Z0-9_\-]+\.[a-zA-Z0-9_\-]+\.[a-zA-Z0-9_\-]+)");
  re2::RE2::GlobalReplace(&out, *kJwtPattern, "[REDACTED_JWT]");

  // Redact Bearer: (?i)Bearer\s+[a-zA-Z0-9_\-\.]+ -> Bearer [REDACTED]
  static const base::NoDestructor<re2::RE2> kBearerPattern(
      R"((?i)Bearer\s+[a-zA-Z0-9_\-\.]+)");
  re2::RE2::GlobalReplace(&out, *kBearerPattern, "Bearer [REDACTED]");

  // Existing URL scrub also applied
  out = RedactUrl(out);

  return out;
}

// static
void MahoMcpFirewall::RedactAll(base::Value& value) {
  if (value.is_string()) {
    value = base::Value(RedactString(value.GetString()));
    return;
  }
  if (base::DictValue* dict = value.GetIfDict()) {
    for (auto pair : *dict) {
      const std::string& key = pair.first;
      base::Value& child = pair.second;
      if (base::EqualsCaseInsensitiveASCII(key, "headers")) {
        if (base::DictValue* headers = child.GetIfDict()) {
          RedactHeaders(*headers);
          continue;
        }
      }
      if (base::EqualsCaseInsensitiveASCII(key, "url") && child.is_string()) {
        child = base::Value(RedactUrl(child.GetString()));
        continue;
      }
      if (base::EqualsCaseInsensitiveASCII(key, "html") && child.is_string()) {
        child = base::Value(RedactPasswordFields(child.GetString()));
        continue;
      }
      if (child.is_string()) {
        if (IsCredentialDictKey(key)) {
          child = base::Value(kRedacted);
          continue;
        }
      }
      RedactAll(child);
    }
    return;
  }
  if (base::ListValue* list = value.GetIfList()) {
    for (auto& item : *list) {
      RedactAll(item);
    }
  }
}

// static
Redacted<base::Value> MahoMcpFirewall::Wrap(base::Value payload) {
  RedactAll(payload);
  return Redacted<base::Value>(std::move(payload));
}

// static
Redacted<std::string> MahoMcpFirewall::WrapString(std::string_view raw) {
  std::string result = RedactPasswordFields(raw);
  result = RedactUrl(result);
  return Redacted<std::string>(std::move(result));
}

// static
Redacted<base::DictValue> MahoMcpFirewall::WrapHar(
    base::DictValue har_json) {
  if (base::ListValue* entries = har_json.FindList("entries")) {
    for (auto& entry_val : *entries) {
      if (base::DictValue* entry = entry_val.GetIfDict()) {
        if (base::DictValue* request = entry->FindDict("request")) {
          if (base::DictValue* req_headers = request->FindDict("headers")) {
            RedactHeaders(*req_headers);
          }
          if (const std::string* url = request->FindString("url")) {
            request->Set("url", RedactUrl(*url));
          }
        }
        if (base::DictValue* response = entry->FindDict("response")) {
          if (base::DictValue* resp_headers =
                  response->FindDict("headers")) {
            RedactHeaders(*resp_headers);
          }
        }
      }
    }
  }
  return Redacted<base::DictValue>(std::move(har_json));
}

}  // namespace maho
