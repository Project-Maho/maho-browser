// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_signature.h"

#include <array>
#include <optional>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/secure_hash.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace maho {
namespace updates {

bool IsZeroKey(base::span<const uint8_t> public_key) {
  for (uint8_t b : public_key) {
    if (b != 0) {
      return false;
    }
  }
  return true;
}

bool VerifyManifestSignature(std::string_view payload_bytes,
                             base::span<const uint8_t> ed25519_sig,
                             base::span<const uint8_t> public_key) {
  if (ed25519_sig.size() != 64 || public_key.size() != 32) {
    return false;
  }
  if (IsZeroKey(public_key)) {
    return false;
  }
  return ED25519_verify(
             reinterpret_cast<const uint8_t*>(payload_bytes.data()),
             payload_bytes.size(), ed25519_sig.data(), public_key.data()) == 1;
}

bool VerifySha256(const base::FilePath& file_path, std::string_view expected_hex) {
  base::File file(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return false;
  }

  std::unique_ptr<crypto::SecureHash> hasher =
      crypto::SecureHash::Create(crypto::SecureHash::SHA256);

  std::array<uint8_t, 4096> buffer{};
  while (true) {
    std::optional<size_t> bytes_read =
        file.ReadAtCurrentPos(base::span<uint8_t>(buffer));
    if (!bytes_read.has_value()) {
      return false;
    }
    if (*bytes_read == 0) {
      break;
    }
    hasher->Update(base::span<const uint8_t>(buffer).first(*bytes_read));
  }

  std::array<uint8_t, 32> hash{};
  hasher->Finish(base::span<uint8_t>(hash));

  std::string hex = base::HexEncode(base::span<const uint8_t>(hash));
  return base::EqualsCaseInsensitiveASCII(hex, expected_hex);
}

}  // namespace updates
}  // namespace maho
