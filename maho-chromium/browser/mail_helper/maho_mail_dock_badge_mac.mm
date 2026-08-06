// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_dock_badge_mac.h"

#import <AppKit/AppKit.h>

#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"

namespace maho {

void SetMailDockBadge(int unread_count, bool enabled) {
  NSApplication* app = NSApp;
  if (!app) {
    return;
  }
  NSString* label = nil;
  if (enabled && unread_count > 0) {
    label = unread_count > 99
                ? @"99+"
                : base::SysUTF8ToNSString(base::NumberToString(unread_count));
  }
  [[app dockTile] setBadgeLabel:label];
}

}  // namespace maho
