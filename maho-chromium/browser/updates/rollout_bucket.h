// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_ROLLOUT_BUCKET_H_
#define MAHO_BROWSER_UPDATES_ROLLOUT_BUCKET_H_

#include <string>

namespace maho {
namespace updates {

class RolloutBucket {
 public:
  // Computes the stable rollout bucket (0..99) for the given install_id.
  static int Compute(const std::string& install_id);
};

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_ROLLOUT_BUCKET_H_
