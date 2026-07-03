// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_toast_vibrancy.h"

#import <AppKit/AppKit.h>

#include "base/logging.h"
#include "ui/views/widget/widget.h"

namespace maho {

void ApplyVibrancyToToast(views::Widget* widget) {
  if ([[NSWorkspace sharedWorkspace]
          accessibilityDisplayShouldReduceTransparency]) {
    LOG(INFO) << "[maho-toast-vibrancy] skipped: reduce transparency";
    return;
  }

  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  LOG(INFO) << "[maho-toast-vibrancy] window=" << window;
  if (!window) {
    return;
  }
  NSView* content = [window contentView];
  LOG(INFO) << "[maho-toast-vibrancy] content=" << content;
  if (!content) {
    return;
  }

  for (NSView* sub in [content subviews]) {
    if ([sub isKindOfClass:[NSVisualEffectView class]]) {
      LOG(INFO) << "[maho-toast-vibrancy] already applied";
      return;
    }
  }

  [window setOpaque:NO];
  [window setBackgroundColor:[NSColor clearColor]];

  NSVisualEffectView* vev =
      [[NSVisualEffectView alloc] initWithFrame:[content bounds]];
  [vev setMaterial:NSVisualEffectMaterialHUDWindow];
  [vev setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
  [vev setState:NSVisualEffectStateActive];
  [vev setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [vev setWantsLayer:YES];
  [vev layer].cornerRadius = 12;
  [vev layer].masksToBounds = YES;
  [content addSubview:vev positioned:NSWindowBelow relativeTo:nil];
  [content setNeedsLayout:YES];
  LOG(INFO) << "[maho-toast-vibrancy] applied";
}

}  // namespace maho
