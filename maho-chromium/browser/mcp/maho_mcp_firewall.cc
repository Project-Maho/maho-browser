// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_firewall.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "maho/browser/ai/maho_credential_redaction.h"
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

const re2::RE2& CredentialFieldNamePattern() {
  static const base::NoDestructor<re2::RE2> kPattern(
      "(?i)(password|passcode|passphrase|one[- _]?time|\\botp\\b|\\btotp\\b|"
      "\\b2fa\\b|\\bmfa\\b|two[- _]?factor|verification code|security code|"
      "authenticator|backup code|\\bcvv\\b|\\bcvc\\b|card number|\\bssn\\b|"
      "social security|recovery|\\bsecret\\b|\\bpin\\b)");
  return *kPattern;
}

bool IsSensitiveAutocompleteToken(std::string_view token) {
  return credential_redaction::IsSensitiveAutocomplete(token);
}

bool IsCredentialFieldContext(const base::DictValue& node) {
  if (node.FindBool("protected").value_or(false)) {
    return true;
  }
  if (const std::string* input_type = node.FindString("inputType")) {
    if (base::EqualsCaseInsensitiveASCII(*input_type, "password")) {
      return true;
    }
  }
  if (const std::string* autocomplete = node.FindString("autocomplete")) {
    if (IsSensitiveAutocompleteToken(*autocomplete)) {
      return true;
    }
  }
  if (const std::string* name = node.FindString("name")) {
    if (re2::RE2::PartialMatch(*name, CredentialFieldNamePattern())) {
      return true;
    }
  }
  static constexpr std::array<std::string_view, 5> kSemanticKeys = {
      "ariaLabel", "fieldName", "elementId", "placeholder", "dataField"};
  for (std::string_view key : kSemanticKeys) {
    if (const std::string* signal = node.FindString(key)) {
      if (re2::RE2::PartialMatch(*signal, CredentialFieldNamePattern()) ||
          credential_redaction::IsCredentialName(*signal)) {
        return true;
      }
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
  return credential_redaction::IsCredentialName(key);
}

std::optional<std::pair<size_t, size_t>> FindHtmlAttributeValue(
    std::string_view tag,
    std::string_view attribute_name) {
  const std::string lower = credential_redaction::LowerAscii(tag);
  size_t search = 0;
  while ((search = lower.find(attribute_name, search)) != std::string::npos) {
    const size_t after_name = search + attribute_name.size();
    const bool left_boundary =
        search == 0 || !std::isalnum(static_cast<unsigned char>(lower[search - 1]));
    const bool right_boundary =
        after_name == lower.size() ||
        !std::isalnum(static_cast<unsigned char>(lower[after_name]));
    if (!left_boundary || !right_boundary) {
      search = after_name;
      continue;
    }
    size_t equals = after_name;
    while (equals < tag.size() && std::isspace(static_cast<unsigned char>(tag[equals]))) {
      ++equals;
    }
    if (equals >= tag.size() || tag[equals] != '=') {
      search = after_name;
      continue;
    }
    size_t value_start = equals + 1;
    while (value_start < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[value_start]))) {
      ++value_start;
    }
    if (value_start >= tag.size()) {
      return std::nullopt;
    }
    if (tag[value_start] == '"' || tag[value_start] == '\'') {
      const char quote = tag[value_start++];
      const size_t value_end = tag.find(quote, value_start);
      if (value_end == std::string_view::npos) {
        return std::nullopt;
      }
      return std::pair(value_start, value_end);
    }
    size_t value_end = value_start;
    while (value_end < tag.size() &&
           !std::isspace(static_cast<unsigned char>(tag[value_end])) &&
           tag[value_end] != '>') {
      ++value_end;
    }
    return std::pair(value_start, value_end);
  }
  return std::nullopt;
}

bool IsSensitiveHtmlField(std::string_view tag) {
  static constexpr std::array<std::string_view, 7> kAttributes = {
      "type", "autocomplete", "aria-label", "name", "id", "placeholder",
      "data-maho-field"};
  for (std::string_view attribute : kAttributes) {
    const auto span = FindHtmlAttributeValue(tag, attribute);
    if (!span.has_value()) {
      continue;
    }
    const std::string_view value =
        tag.substr(span->first, span->second - span->first);
    if ((attribute == "type" &&
         base::EqualsCaseInsensitiveASCII(value, "password")) ||
        (attribute == "autocomplete" &&
         credential_redaction::IsSensitiveAutocomplete(value)) ||
        credential_redaction::IsCredentialName(value) ||
        re2::RE2::PartialMatch(std::string(value),
                               CredentialFieldNamePattern())) {
      return true;
    }
  }
  return false;
}

std::string RedactHtmlValueAttribute(std::string_view tag) {
  const auto span = FindHtmlAttributeValue(tag, "value");
  if (!span.has_value()) {
    return std::string(tag);
  }
  std::string redacted(tag);
  redacted.replace(span->first, span->second - span->first,
                   MahoMcpFirewall::kRedacted);
  return redacted;
}

}  // namespace

// static
std::string MahoMcpFirewall::RedactPasswordFields(
    std::string_view html_or_text) {
  const std::string lower = credential_redaction::LowerAscii(html_or_text);
  std::string output;
  output.reserve(html_or_text.size());
  size_t cursor = 0;
  while (cursor < html_or_text.size()) {
    const size_t tag_start = html_or_text.find('<', cursor);
    if (tag_start == std::string_view::npos) {
      output.append(html_or_text.substr(cursor));
      break;
    }
    const size_t tag_end = html_or_text.find('>', tag_start + 1);
    if (tag_end == std::string_view::npos) {
      output.append(html_or_text.substr(cursor));
      break;
    }
    const std::string_view tag =
        html_or_text.substr(tag_start, tag_end - tag_start + 1);
    const std::string_view lower_tag =
        std::string_view(lower).substr(tag_start, tag.size());
    const bool is_input = lower_tag.rfind("<input", 0) == 0;
    const bool is_textarea = lower_tag.rfind("<textarea", 0) == 0;
    if ((!is_input && !is_textarea) || !IsSensitiveHtmlField(tag)) {
      output.append(html_or_text.substr(cursor, tag_end - cursor + 1));
      cursor = tag_end + 1;
      continue;
    }
    output.append(html_or_text.substr(cursor, tag_start - cursor));
    if (is_input) {
      output.append(RedactHtmlValueAttribute(tag));
      cursor = tag_end + 1;
      continue;
    }
    output.append(tag);
    const size_t close_start = lower.find("</textarea", tag_end + 1);
    if (close_start == std::string::npos) {
      output.append(kRedacted);
      break;
    }
    output.append(kRedacted);
    cursor = close_start;
  }
  return output;
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
  if (IsCredentialFieldContext(node) && node.contains("value")) {
    node.Set("value", kRedacted);
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
  std::string out = credential_redaction::RedactCredentialText(raw);
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
    RedactAxTreeAutofill(*dict);
    if (const std::string* name = dict->FindString("name")) {
      if (credential_redaction::IsCredentialName(*name) &&
          dict->contains("value")) {
        dict->Set("value", kRedacted);
      }
    }
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
  base::Value value(std::move(har_json));
  RedactAll(value);
  return Redacted<base::DictValue>(std::move(value).TakeDict());
}

}  // namespace maho
