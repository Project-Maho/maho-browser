// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PENDING_SURFACE_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PENDING_SURFACE_H_

#include <cstdint>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

class PrefService;

namespace maho::ai {

// Profile-scoped one-shot state used to carry a requested compact-panel
// surface from a tab-hosted chrome://maho-ai navigation to the fixed-URL side
// panel without navigating or reloading that panel's WebContents.
class MahoAiPendingSurface {
 public:
  using RequestCallback =
      base::RepeatingCallback<void(maho_ai::mojom::SurfaceRequestPtr)>;

  explicit MahoAiPendingSurface(PrefService* prefs);
  MahoAiPendingSurface(const MahoAiPendingSurface&) = delete;
  MahoAiPendingSurface& operator=(const MahoAiPendingSurface&) = delete;
  ~MahoAiPendingSurface();

  // Records a new request with a profile-monotonic generation. If a consumer
  // is registered for this profile, the request is atomically consumed before
  // that consumer is notified.
  static uint64_t Request(PrefService* prefs,
                          maho_ai::mojom::CompactSurface surface);

  // Returns and clears the pending request. A stale request at or below
  // |last_seen_generation| is rejected and normal chat is returned instead.
  maho_ai::mojom::SurfaceRequestPtr Consume(uint64_t last_seen_generation);

  // One active Maho AI page handler owns warm notifications for a profile.
  // Registering a replacement prevents an old panel from receiving requests.
  void SetConsumer(RequestCallback callback);

 private:
  raw_ptr<PrefService> prefs_;
  RequestCallback callback_;
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_PENDING_SURFACE_H_
