// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_SHARE_MAC_H_
#define MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_SHARE_MAC_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "url/gurl.h"

namespace views {
class View;
}

namespace maho {

// Pops the macOS NSSharingServicePicker anchored to |anchor_view|, offering
// |share_url| as the shared item plus an optional |title|. |on_dismissed|
// runs on the UI thread when the picker terminates (item shared, share
// failed, or user dismissed without choosing). The delegate is retained in
// a process-wide set so it survives until a terminal event fires.
//
// Must be called on the UI thread. No-op if |share_url| is invalid or
// |anchor_view| is not attached to a views::Widget with a native window.
// When the call is a no-op, |on_dismissed| is dropped without running.
__attribute__((visibility("default")))
void ShowNativeSharePicker(views::View* anchor_view,
                           const GURL& share_url,
                           const std::u16string& title,
                           base::OnceClosure on_dismissed);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_SHARE_MAC_H_
