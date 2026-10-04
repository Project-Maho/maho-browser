// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_SRC_CHROME_BROWSER_UI_VIEWS_PASSWORDS_PASSWORD_BUBBLE_VIEW_BASE_H_
#define MAHO_CHROMIUM_SRC_CHROME_BROWSER_UI_VIEWS_PASSWORDS_PASSWORD_BUBBLE_VIEW_BASE_H_

#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"

#if defined(UNIT_TEST)
#define MAHO_PASSWORD_BUBBLE_TESTING_API \
  static void DestroyManagePasswordsBubbleForTesting();
#else
#define MAHO_PASSWORD_BUBBLE_TESTING_API
#endif

#define CreateBubble                                                       \
  CreateBubble(content::WebContents* web_contents,                         \
               views::BubbleAnchor anchor_view, DisplayReason reason);     \
  MAHO_PASSWORD_BUBBLE_TESTING_API                                         \
  static PasswordBubbleViewBase* CreateBubble_ChromiumImpl

#include "../src/chrome/browser/ui/views/passwords/password_bubble_view_base.h"  // IWYU pragma: export

#undef CreateBubble
#undef MAHO_PASSWORD_BUBBLE_TESTING_API

#endif  // MAHO_CHROMIUM_SRC_CHROME_BROWSER_UI_VIEWS_PASSWORDS_PASSWORD_BUBBLE_VIEW_BASE_H_
