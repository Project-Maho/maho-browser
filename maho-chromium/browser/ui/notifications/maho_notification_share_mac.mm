// Copyright 2026 Maho Browser. All rights reserved.

#import "maho/browser/ui/notifications/maho_notification_share_mac.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "base/check.h"
#include "base/functional/callback.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "net/base/apple/url_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

@interface MahoSharePickerDelegate
    : NSObject <NSSharingServicePickerDelegate, NSSharingServiceDelegate>
- (instancetype)initWithPicker:(NSSharingServicePicker*)picker
                  onDismissed:(base::OnceClosure)on_dismissed;
@end

namespace {

// AppKit declares NSSharingServicePicker.delegate as |weak|, so without a
// process-wide strong holder the delegate would be deallocated as soon as
// ShowNativeSharePicker returns and the picker would lose all terminal
// callbacks. Membership is removed inside -[MahoSharePickerDelegate
// terminate] when any of {item shared, share failed, dismissed} fires.
NSMutableSet<MahoSharePickerDelegate*>* GetActiveDelegates() {
  static NSMutableSet<MahoSharePickerDelegate*>* set =
      [[NSMutableSet alloc] init];
  return set;
}

}  // namespace

@implementation MahoSharePickerDelegate {
  NSSharingServicePicker* __strong _picker;
  base::OnceClosure _on_dismissed;
  BOOL _terminated;
}

- (instancetype)initWithPicker:(NSSharingServicePicker*)picker
                  onDismissed:(base::OnceClosure)on_dismissed {
  if ((self = [super init])) {
    _picker = picker;
    _on_dismissed = std::move(on_dismissed);
    _terminated = NO;
  }
  return self;
}

- (void)terminate {
  if (_terminated) {
    return;
  }
  _terminated = YES;
  _picker = nil;
  if (_on_dismissed) {
    std::move(_on_dismissed).Run();
  }
  [GetActiveDelegates() removeObject:self];
}

- (void)sharingServicePicker:(NSSharingServicePicker*)picker
     didChooseSharingService:(NSSharingService*)service {
  if (!service) {
    [self terminate];
  }
}

- (void)sharingService:(NSSharingService*)service
         didShareItems:(NSArray*)items {
  [self terminate];
}

- (void)sharingService:(NSSharingService*)service
    didFailToShareItems:(NSArray*)items
                  error:(NSError*)error {
  [self terminate];
}

- (id<NSSharingServiceDelegate>)sharingServicePicker:
                                    (NSSharingServicePicker*)picker
                          delegateForSharingService:
                                    (NSSharingService*)service {
  return self;
}

@end

namespace maho {

void ShowNativeSharePicker(views::View* anchor_view,
                           const GURL& share_url,
                           const std::u16string& title,
                           base::OnceClosure on_dismissed) {
  DCHECK(anchor_view);
  if (!share_url.is_valid()) {
    return;
  }
  views::Widget* widget = anchor_view->GetWidget();
  if (!widget) {
    return;
  }
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window) {
    return;
  }
  NSView* content_view = ns_window.contentView;
  if (!content_view) {
    return;
  }

  NSURL* ns_url = net::NSURLWithGURL(share_url);
  if (!ns_url) {
    return;
  }
  NSMutableArray* items = [NSMutableArray array];
  if (@available(macOS 13.0, *)) {
    NSString* ns_title = base::SysUTF16ToNSString(
        title.empty() ? base::UTF8ToUTF16(share_url.spec()) : title);
    [items addObject:[[NSPreviewRepresentingActivityItem alloc]
                         initWithItem:ns_url
                                title:ns_title
                                image:nil
                                 icon:nil]];
  } else {
    [items addObject:ns_url];
    if (!title.empty()) {
      [items addObject:base::SysUTF16ToNSString(title)];
    }
  }

  gfx::Rect screen_bounds = anchor_view->GetBoundsInScreen();
  NSRect ns_screen_rect = gfx::ScreenRectToNSRect(screen_bounds);
  NSRect window_rect = [ns_window convertRectFromScreen:ns_screen_rect];
  NSRect view_rect = [content_view convertRect:window_rect fromView:nil];

  NSSharingServicePicker* picker =
      [[NSSharingServicePicker alloc] initWithItems:items];
  MahoSharePickerDelegate* delegate =
      [[MahoSharePickerDelegate alloc] initWithPicker:picker
                                          onDismissed:std::move(on_dismissed)];
  [GetActiveDelegates() addObject:delegate];
  picker.delegate = delegate;

  [picker showRelativeToRect:view_rect
                      ofView:content_view
               preferredEdge:NSMinYEdge];
}

}  // namespace maho
