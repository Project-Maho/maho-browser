// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_utility_panel_vibrancy.h"

#import <AppKit/AppKit.h>



#include "base/logging.h"
#include "ui/views/widget/widget.h"

namespace maho {


void ApplyVibrancyToUtilityPanel(views::Widget* widget) {
  if (!widget) {
    return;
  }
  if ([[NSWorkspace sharedWorkspace]
          accessibilityDisplayShouldReduceTransparency]) {
    LOG(INFO) << "[maho-utility-panel-vibrancy] skipped: reduce transparency";
    return;
  }

  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!window) {
    return;
  }
  NSView* content = [window contentView];
  if (!content) {
    return;
  }

  for (NSView* sub in [content subviews]) {
    if ([sub isKindOfClass:[NSVisualEffectView class]]) {
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
  [vev layer].cornerRadius = 18;
  [vev layer].masksToBounds = YES;
  [content addSubview:vev positioned:NSWindowBelow relativeTo:nil];
  [content setNeedsLayout:YES];
}

}  // namespace maho
