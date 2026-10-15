// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_controller_pairing.h"

#include <array>
#include <string>
#include <string_view>

#include "base/strings/string_number_conversions.h"
#include "base/containers/span.h"
#include "crypto/random.h"
#include "crypto/secure_util.h"
#include "crypto/sha2.h"

namespace maho {
namespace {

constexpr size_t kTokenSize = 32;

std::string HashToken(std::string_view token) {
  return base::HexEncode(crypto::SHA256HashString(token));
}

bool DigestEquals(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         crypto::SecureMemEqual(base::as_byte_span(a), base::as_byte_span(b));
}

}  // namespace

std::string MahoMcpControllerPairing::Issue(base::DictValue& prefs) {
  std::array<uint8_t, kTokenSize> token_bytes = {};
  crypto::RandBytes(token_bytes);
  std::string token = base::HexEncode(token_bytes);
  base::ListValue* hashes = prefs.FindList(kPrefName);
  if (!hashes) {
    hashes = &prefs.Set(kPrefName, base::ListValue())->GetList();
  }
  hashes->Append(HashToken(token));
  return token;
}

bool MahoMcpControllerPairing::Verify(const base::DictValue& prefs,
                                      const std::string& token) {
  const base::ListValue* hashes = prefs.FindList(kPrefName);
  if (!hashes || token.empty()) {
    return false;
  }
  const std::string digest = HashToken(token);
  for (const base::Value& value : *hashes) {
    const std::string* stored_digest = value.GetIfString();
    if (stored_digest && DigestEquals(*stored_digest, digest)) {
      return true;
    }
  }
  return false;
}

bool MahoMcpControllerPairing::Revoke(base::DictValue& prefs,
                                      const std::string& token) {
  base::ListValue* hashes = prefs.FindList(kPrefName);
  if (!hashes || token.empty()) {
    return false;
  }
  const std::string digest = HashToken(token);
  bool removed = false;
  hashes->EraseIf([&](const base::Value& value) {
    const std::string* stored_digest = value.GetIfString();
    const bool matches = stored_digest && DigestEquals(*stored_digest, digest);
    removed |= matches;
    return matches;
  });
  return removed;
}

}  // namespace maho
