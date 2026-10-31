// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_SIGNATURE_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_SIGNATURE_H_

#include <stdint.h>
#include <string_view>

#include "base/containers/span.h"
#include "base/files/file_path.h"

namespace maho {
namespace updates {

bool IsZeroKey(base::span<const uint8_t> public_key);

bool VerifyManifestSignature(std::string_view payload_bytes,
                             base::span<const uint8_t> ed25519_sig,
                             base::span<const uint8_t> public_key);

bool VerifySha256(const base::FilePath& file_path, std::string_view expected_hex);

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_SIGNATURE_H_
