// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_vibrancy.h"

#import <AppKit/AppKit.h>

#include "base/logging.h"
#include "ui/views/widget/widget.h"

namespace maho {

void ApplyVibrancyToCommandOverlay(views::Widget* widget) {
  if ([[NSWorkspace sharedWorkspace]
          accessibilityDisplayShouldReduceTransparency]) {
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
  [vev setMaterial:NSVisualEffectMaterialPopover];
  [vev setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
  [vev setState:NSVisualEffectStateActive];
  [vev setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [vev setWantsLayer:YES];
  vev.layer.cornerRadius = 26;
  vev.layer.masksToBounds = YES;
  [content addSubview:vev positioned:NSWindowBelow relativeTo:nil];
  [content setNeedsLayout:YES];
}

}  // namespace maho
