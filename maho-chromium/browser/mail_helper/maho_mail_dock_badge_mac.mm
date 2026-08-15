// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_dock_badge_mac.h"
#include "maho/browser/mail_helper/maho_mail_badge.h"

#import <AppKit/AppKit.h>

#include "base/strings/sys_string_conversions.h"

namespace maho {

void SetMailDockBadge(int unread_count, bool enabled) {
  NSApplication* app = NSApp;
  if (!app) {
    return;
  }
  const MailBadgePresentation presentation =
      ResolveMailBadgePresentation(/*mail_enabled=*/true, enabled,
                                   unread_count);
  NSString* label = presentation.visible
                        ? base::SysUTF16ToNSString(presentation.text)
                        : nil;
  [[app dockTile] setBadgeLabel:label];
}

}  // namespace maho
