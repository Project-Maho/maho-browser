// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_DOCK_BADGE_MAC_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_DOCK_BADGE_MAC_H_

namespace maho {

// UI-thread only (touches NSApplication); no-op when there is no NSApplication.
// Clears the badge when `enabled` is false or `unread_count` <= 0.
void SetMailDockBadge(int unread_count, bool enabled);

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_DOCK_BADGE_MAC_H_
