// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_

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

// Translates into the target language, or reloads to restore original if already
// translated.
void TranslatePageViaOnDevice(content::WebContents* web_contents);

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_
