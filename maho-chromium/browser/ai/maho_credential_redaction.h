// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_CREDENTIAL_REDACTION_H_
#define MAHO_BROWSER_AI_MAHO_CREDENTIAL_REDACTION_H_

#include <array>
#include <cctype>
#include <string>
#include <string_view>

#if !defined(MAHO_STANDALONE_TEST)
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#endif

#include <optional>

namespace maho::credential_redaction {

inline constexpr char kRedacted[] = "[REDACTED]";

inline std::string LowerAscii(std::string_view value) {
  std::string lower(value);
  for (char& character : lower) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return lower;
}

inline bool IsCredentialName(std::string_view value) {
  const std::string lower = LowerAscii(value);
  static constexpr std::array<std::string_view, 28> kSignals = {
      "authorization", "cookie",       "password",      "passcode",
      "passphrase",    "access_token", "refresh_token", "id_token",
      "api_key",       "apikey",       "private_key",   "client_secret",
      "auth_token",    "session_id",   "session_token", "one-time-code",
      "one_time_code", "totp",         "otp",           "2fa",
      "mfa",           "recovery",     "backup_code",   "security_code",
      "csrf_token",    "token",        "secret",        "session",
  };
  for (std::string_view signal : kSignals) {
    if (lower.find(signal) != std::string::npos) {
      return true;
    }
  }
  return false;
}

inline bool IsSensitiveAutocomplete(std::string_view value) {
  const std::string lower = LowerAscii(value);
  return lower.find("password") != std::string::npos ||
         lower == "one-time-code" || lower == "otp" || lower == "totp" ||
         lower.find("recovery") != std::string::npos ||
         lower.rfind("cc-", 0) == 0;
}

inline bool IsBase64UrlChar(char character) {
  const unsigned char value = static_cast<unsigned char>(character);
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || character == '-' ||
         character == '_' || character == '=' || character == '+' ||
         character == '/';
}

inline bool EqualsCaseInsensitiveAt(std::string_view input,
                                    size_t offset,
                                    std::string_view expected) {
  if (offset + expected.size() > input.size()) {
    return false;
  }
  for (size_t index = 0; index < expected.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(input[offset + index])) !=
        std::tolower(static_cast<unsigned char>(expected[index]))) {
      return false;
    }
  }
  return true;
}

inline std::string RedactCredentialText(std::string_view input) {
  std::string tokens_redacted;
  tokens_redacted.reserve(input.size());
  size_t index = 0;
  auto scan_base64 = [&](size_t start) {
    size_t end = start;
    while (end < input.size() && IsBase64UrlChar(input[end])) {
      ++end;
    }
    return end;
  };
  while (index < input.size()) {
    if (EqualsCaseInsensitiveAt(input, index, "Bearer")) {
      size_t token_start = index + 6;
      while (token_start < input.size() &&
             (input[token_start] == ' ' || input[token_start] == '\t')) {
        ++token_start;
      }
      size_t token_end = token_start;
      while (token_end < input.size() &&
             (IsBase64UrlChar(input[token_end]) || input[token_end] == '.')) {
        ++token_end;
      }
      if (token_start > index + 6 && token_end > token_start) {
        tokens_redacted.append("Bearer [REDACTED]");
        index = token_end;
        continue;
      }
    }
    if (index + 3 <= input.size() && input.substr(index, 3) == "eyJ") {
      const size_t first = scan_base64(index);
      if (first < input.size() && input[first] == '.') {
        const size_t second = scan_base64(first + 1);
        if (second > first + 1 && second < input.size() &&
            input[second] == '.') {
          const size_t third = scan_base64(second + 1);
          if (third > second + 1) {
            tokens_redacted.append("[REDACTED_JWT]");
            index = third;
            continue;
          }
        }
      }
    }
    tokens_redacted.push_back(input[index++]);
  }

  static constexpr std::array<std::string_view, 21> kLabels = {
      "Proxy-Authorization", "Authorization", "Set-Cookie", "Cookie",
      "current-password",    "new-password",  "password",   "passcode",
      "passphrase",          "access_token",  "refresh_token",
      "id_token",            "api_key",       "client_secret",
      "one-time-code",       "totp",          "recovery_code",
      "backup_code",          "token",          "secret",
      "session",
  };
  std::string output;
  output.reserve(tokens_redacted.size());
  index = 0;
  while (index < tokens_redacted.size()) {
    bool replaced = false;
    for (std::string_view label : kLabels) {
      if (!EqualsCaseInsensitiveAt(tokens_redacted, index, label)) {
        continue;
      }
      const bool left_boundary =
          index == 0 || !std::isalnum(static_cast<unsigned char>(
                            tokens_redacted[index - 1]));
      size_t separator = index + label.size();
      while (separator < tokens_redacted.size() &&
             (tokens_redacted[separator] == ' ' ||
              tokens_redacted[separator] == '\t')) {
        ++separator;
      }
      if (!left_boundary || separator >= tokens_redacted.size() ||
          (tokens_redacted[separator] != ':' &&
           tokens_redacted[separator] != '=')) {
        continue;
      }
      output.append(tokens_redacted, index, separator - index + 1);
      size_t value_start = separator + 1;
      while (value_start < tokens_redacted.size() &&
             (tokens_redacted[value_start] == ' ' ||
              tokens_redacted[value_start] == '\t')) {
        output.push_back(tokens_redacted[value_start++]);
      }
      if (tokens_redacted.compare(value_start, sizeof(kRedacted) - 1,
                                  kRedacted) == 0) {
        output.append(kRedacted);
        index = value_start + sizeof(kRedacted) - 1;
        replaced = true;
        break;
      }
      static constexpr std::string_view kRedactedJwt = "[REDACTED_JWT]";
      if (tokens_redacted.compare(value_start, kRedactedJwt.size(),
                                  kRedactedJwt) == 0) {
        output.append(kRedactedJwt);
        index = value_start + kRedactedJwt.size();
        replaced = true;
        break;
      }
      output.append(kRedacted);
      const std::string lower_label = LowerAscii(label);
      const bool header_value = lower_label.find("authorization") !=
                                    std::string::npos ||
                                lower_label.find("cookie") != std::string::npos;
      size_t value_end = value_start;
      while (value_end < tokens_redacted.size()) {
        const char character = tokens_redacted[value_end];
        if (character == '\n' || character == '\r' ||
            (!header_value && (character == '&' || character == ';' ||
                               character == ',' || character == '}' ||
                               character == ']' ||
                               std::isspace(static_cast<unsigned char>(
                                   character))))) {
          break;
        }
        ++value_end;
      }
      index = value_end;
      replaced = true;
      break;
    }
    if (!replaced) {
      output.push_back(tokens_redacted[index++]);
    }
  }
  return output;
}

#if !defined(MAHO_STANDALONE_TEST)
inline void RedactStructuredValue(base::Value& value) {
  if (value.is_string()) {
    value = base::Value(RedactCredentialText(value.GetString()));
    return;
  }
  if (base::DictValue* dict = value.GetIfDict()) {
    const std::string* name = dict->FindString("name");
    if (name && IsCredentialName(*name) && dict->contains("value")) {
      dict->Set("value", kRedacted);
    }
    for (auto pair : *dict) {
      if (IsCredentialName(pair.first) && pair.second.is_string()) {
        pair.second = base::Value(kRedacted);
      } else {
        RedactStructuredValue(pair.second);
      }
    }
    return;
  }
  if (base::ListValue* list = value.GetIfList()) {
    for (base::Value& item : *list) {
      RedactStructuredValue(item);
    }
  }
}
#endif

inline std::string RedactJsonOrText(std::string_view input) {
#if !defined(MAHO_STANDALONE_TEST)
  std::optional<base::Value> parsed =
      base::JSONReader::Read(input, base::JSON_PARSE_RFC);
  if (!parsed.has_value()) {
    return RedactCredentialText(input);
  }
  RedactStructuredValue(*parsed);
  std::string output;
  base::JSONWriter::Write(*parsed, &output);
  return output;
#else
  return RedactCredentialText(input);
#endif
}

}  // namespace maho::credential_redaction

#endif  // MAHO_BROWSER_AI_MAHO_CREDENTIAL_REDACTION_H_
