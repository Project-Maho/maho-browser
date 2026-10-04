// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_notification_share_mac.h"

#include "base/functional/callback.h"

namespace maho {

void ShowNativeSharePicker(views::View* anchor_view,
                           const GURL& share_url,
                           const std::u16string& title,
                           base::OnceClosure on_dismissed) {
  // No-op stub for Linux desktop.
  if (on_dismissed) {
    std::move(on_dismissed).Run();
  }
}

}  // namespace maho
