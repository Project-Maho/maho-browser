// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_WINDOW_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_WINDOW_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "url/gurl.h"

class Browser;
class Profile;

namespace content {
class BrowserContext;
}

namespace maho {

enum class LinkDestinationType {
  kNormal,
  kMahoMini,
  kSpace,
};

struct LinkDestinationResult {
  LinkDestinationType type = LinkDestinationType::kNormal;
  std::string space_id;
};

struct MahoMiniRequest {
  GURL url;
};

void LaunchMahoMini(content::BrowserContext* context, MahoMiniRequest request);

void DecideLinkDestination(
    const GURL& url,
    bool is_external,
    base::OnceCallback<void(LinkDestinationResult)> callback);

void RegisterGlobalShortcut(Profile* profile);

// Returns the space id that was active in the originating (last active tabbed)
// browser when the most recent Maho Mini window launched. Empty if none was
// captured. Used by the Maho Mini top bar to seed the "Open in <Space>"
// promote target, since the popup browser itself has no space context.
std::string GetMahoMiniOriginatingSpaceId(Browser* popup_browser);

}  // namespace maho

// MahoMiniWindow creates a resizable popup browser window (600x760 by
// default), similar to Arc's "Maho Mini" feature. It remembers the user's
// resized dimensions, opens in front for the initiating gesture, then
// participates in normal window z-order while remaining open on blur.
class MahoMiniWindow {
 public:
  // Opens a new MahoMini popup window. If |url| is empty,
  // opens about:blank.
  static void Open(Browser* parent_browser, const GURL& url);
  static void Open(Profile* profile, const GURL& url);

  // Promotes the current page in |popup_browser| to a new tab in its
  // opener (parent) browser, then closes the popup.
  static void PromoteToTab(Browser* popup_browser);

 private:
  MahoMiniWindow() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_WINDOW_H_
