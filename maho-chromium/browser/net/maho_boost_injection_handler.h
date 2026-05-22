// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_BOOST_INJECTION_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_BOOST_INJECTION_HANDLER_H_

#include <string>

#include "base/sequence_checker.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class MahoBoostInjectionHandler
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoBoostInjectionHandler> {
 public:
  ~MahoBoostInjectionHandler() override;

  MahoBoostInjectionHandler(const MahoBoostInjectionHandler&) = delete;
  MahoBoostInjectionHandler& operator=(const MahoBoostInjectionHandler&) =
      delete;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  void InjectBoostCssIntoTab(content::RenderFrameHost* render_frame_host,
                              const std::string& url);
  void InjectBoostJsIntoTab(content::RenderFrameHost* render_frame_host,
                             const std::string& url);

 private:
  friend class content::WebContentsUserData<MahoBoostInjectionHandler>;
  explicit MahoBoostInjectionHandler(content::WebContents* web_contents);

  void InjectBoostCss(content::RenderFrameHost* render_frame_host,
                      const std::string& url);
  void InjectBoostJs(content::RenderFrameHost* render_frame_host,
                     const std::string& url);

  SEQUENCE_CHECKER(sequence_checker_);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_BOOST_INJECTION_HANDLER_H_
