// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_WEBSTORE_REBRAND_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_WEBSTORE_REBRAND_HANDLER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class RenderFrameHost;
}  // namespace content

// Rebrands the Chrome Web Store install experience for Maho. The new store
// (chromewebstore.google.com) determines the "Add to Chrome" label and the
// "Switch to Chrome" nags from a server-side signal (the GOOGLE_CHROME_BRANDING
// API key) that a Chromium fork cannot supply, so neither the User-Agent nor
// the UA Client Hints brand can change them. The only reliable client-side fix
// — used by Brave and Helium — is DOM surgery via an injected script. This
// observer injects that script (in an isolated world) on every webstore
// navigation; the script installs a MutationObserver that renames the install
// button to "Add to Maho" and removes the switch-to-Chrome nag banner/popup.
class MahoWebStoreRebrandHandler
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoWebStoreRebrandHandler> {
 public:
  ~MahoWebStoreRebrandHandler() override;

  MahoWebStoreRebrandHandler(const MahoWebStoreRebrandHandler&) = delete;
  MahoWebStoreRebrandHandler& operator=(const MahoWebStoreRebrandHandler&) =
      delete;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<MahoWebStoreRebrandHandler>;
  explicit MahoWebStoreRebrandHandler(content::WebContents* web_contents);

  void InjectRebrandScript(content::RenderFrameHost* render_frame_host);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_WEBSTORE_REBRAND_HANDLER_H_
