// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_

#include <string_view>

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace maho {

// Tracks whether Maho translated the active page; cleared on cross-document nav.
class MahoTranslateTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoTranslateTabHelper> {
 public:
  ~MahoTranslateTabHelper() override;

  MahoTranslateTabHelper(const MahoTranslateTabHelper&) = delete;
  MahoTranslateTabHelper& operator=(const MahoTranslateTabHelper&) = delete;

  bool is_translated() const { return is_translated_; }
  void set_is_translated(bool value) { is_translated_ = value; }

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<MahoTranslateTabHelper>;
  explicit MahoTranslateTabHelper(content::WebContents* web_contents);

  bool is_translated_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// http/https injectable page. Foreign-language gating is applied by the caller,
// which owns the language-detection state (this layer cannot reach it).
bool CanOnDeviceTranslate(content::WebContents* web_contents);

bool IsMahoPageTranslated(content::WebContents* web_contents);

// Translates into |target_language|, or reloads to restore original if already
// translated. Callers with translate-manager access (the sidebar translate pill)
// pass the resolved target; an empty target (the page context menu) falls back
// to the recent translate target, then "en", because this layer has no
// translate-manager dependency.
void TranslatePageViaOnDevice(content::WebContents* web_contents,
                              std::string_view target_language = {});

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_
