// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_FIREWALL_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_FIREWALL_H_

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "base/values.h"

namespace maho {

// Forward declaration for friend access.
class MahoMcpFirewall;

// Compile-time wrapper that enforces all MCP tool outputs pass through the
// firewall before egress. Only MahoMcpFirewall can construct instances.
//
// Usage:
//   Redacted<base::Value> safe = MahoMcpFirewall::Wrap(std::move(payload));
//   framer_.BuildSuccessResponse(msg.id, safe.get().Clone());
//
// Move-only. Attempting to construct outside MahoMcpFirewall is a compile
// error (private constructor).
template <typename T>
class Redacted {
 public:
  Redacted(Redacted&& other) noexcept = default;
  Redacted& operator=(Redacted&& other) noexcept = default;

  Redacted(const Redacted&) = delete;
  Redacted& operator=(const Redacted&) = delete;

  // Access the underlying redacted value (read-only).
  const T& get() const { return value_; }

  // Move the underlying value out (consumes the wrapper).
  T take() && { return std::move(value_); }

 private:
  friend class MahoMcpFirewall;

  explicit Redacted(T value) : value_(std::move(value)) {}

  T value_;
};

// 4-vector credential firewall (OQ-4) applied to every MCP tool response
// before egress to an external agent.
//
// Vector 1 — DOM password fields
// Vector 2 — HTTP headers (Cookie / Authorization / token-like names)
// Vector 3 — Accessibility-tree autofill values in credential contexts
// Vector 4 — URL fragments / query params matching credential patterns
//
// All redactions replace the sensitive value with the literal string
// "[REDACTED]". Compile-time discipline: MCP tool handlers MUST route
// their output through one of the Redact* functions below before wrapping
// the payload in a JSON-RPC response.
class MahoMcpFirewall {
 public:
  static constexpr char kRedacted[] = "[REDACTED]";

  // --- Redacted<T> factory methods (compile-time gate) ---

  // Apply all 4 vectors to a JSON payload and wrap in Redacted<>.
  static Redacted<base::Value> Wrap(base::Value payload);

  // Apply password field + URL redaction to a raw string and wrap.
  static Redacted<std::string> WrapString(std::string_view raw);

  // Apply HAR-specific redaction (header scrub + URL scrub across entries)
  // and wrap. Placeholder for Task 4.4.
  static Redacted<base::DictValue> WrapHar(base::DictValue har_json);

  // --- Raw redaction primitives (used internally by Wrap*) ---

  // Vector 1: In-place replace `value` attribute of every
  // <input type="password"> element in |html_or_text| with [REDACTED].
  // Returns the redacted string. Idempotent.
  static std::string RedactPasswordFields(std::string_view html_or_text);

  // Vector 2: Redact every header whose name matches the credential
  // pattern. Modifies |headers| dict in place.
  static void RedactHeaders(base::DictValue& headers);

  // Vector 3: Walk an accessibility tree snapshot and redact values of
  // textbox/searchbox/combobox nodes whose form contains a password
  // field, plus any `AXAutocompleteValue` attributes regardless of role.
  static void RedactAxTreeAutofill(base::DictValue& ax_tree_node);

  // Vector 4: Strip credential-like query params and fragment params
  // from |url|. Returns the sanitized URL.
  static std::string RedactUrl(std::string_view url);

  // Redact JWT patterns and Bearer tokens in raw string.
  static std::string RedactString(std::string_view raw);

  // Convenience: apply all four vectors to a JSON payload representing a
  // typed tool result. Recursively traverses dicts/lists.
  static void RedactAll(base::Value& value);

  // Test predicate: returns true if the header name is on the scrub list
  // (exact match or regex match against credential-name pattern).
  static bool IsCredentialHeaderName(std::string_view name);

  // Test predicate: returns true if the query/fragment param name is on
  // the scrub list (case-insensitive credential-pattern match).
  static bool IsCredentialParamName(std::string_view name);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_FIREWALL_H_
