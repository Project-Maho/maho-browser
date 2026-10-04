// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/rollout_bucket.h"

#include <stdint.h>
#include <string_view>
#include "base/containers/span.h"
#include "crypto/hmac.h"

namespace maho {
namespace updates {

int RolloutBucket::Compute(const std::string& install_id) {
  if (install_id.empty()) {
    return 0;
  }
  constexpr std::string_view kData = "rollout";
  auto key_span = base::as_byte_span(install_id);
  auto data_span = base::as_byte_span(kData);
  auto hash = crypto::hmac::SignSha256(key_span, data_span);
  
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value = (value << 8) | hash[i];
  }
  return static_cast<int>(value % 100);
}

}  // namespace updates
}  // namespace maho
