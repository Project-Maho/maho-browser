// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"

namespace content {
class NavigationHandle;
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace maho {

// Lifecycle of an on-device page translation. kTranslated is only reached once
// the content script confirmed the swap; kError means the on-device model could
// not be created or the translation failed.
enum class MahoTranslateState {
  kIdle,
  kTranslating,
  kTranslated,
  kError,
};

// Per-tab translate state, reset on cross-document navigation.
//
// The state is reported by the injected content script as verbose console
// messages ("__mahoTranslate:state:<state>[:<source>]"). Console messages are
// the only available channel: the injector runs the script through
// ExecuteJavaScriptInIsolatedWorld, whose result callback does not resolve
// promises.
class MahoTranslateTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoTranslateTabHelper> {
 public:
  ~MahoTranslateTabHelper() override;

  MahoTranslateTabHelper(const MahoTranslateTabHelper&) = delete;
  MahoTranslateTabHelper& operator=(const MahoTranslateTabHelper&) = delete;

  MahoTranslateState state() const { return state_; }
  const std::string& source_language() const { return source_language_; }
  bool is_translated() const { return state_ == MahoTranslateState::kTranslated; }

  // The returned subscription becomes inert when this helper is destroyed.
  base::CallbackListSubscription AddStateChangedCallback(
      base::RepeatingClosure callback);

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override;

 private:
  friend class content::WebContentsUserData<MahoTranslateTabHelper>;
  friend void TranslatePageViaOnDevice(content::WebContents* web_contents,
                                       std::string_view target_language);
  friend void RestoreOriginalPage(content::WebContents* web_contents);
  explicit MahoTranslateTabHelper(content::WebContents* web_contents);

  void SetState(MahoTranslateState state, std::string source_language);

  MahoTranslateState state_ = MahoTranslateState::kIdle;
  std::string source_language_;
  base::RepeatingClosureList state_changed_callbacks_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

bool CanOnDeviceTranslate(content::WebContents* web_contents);

bool IsMahoPageTranslated(content::WebContents* web_contents);

MahoTranslateState GetMahoTranslateState(content::WebContents* web_contents);

// Translates into |target_language|; an empty target falls back to the recent
// translate target, then "en". Restores the original in place when the page is
// already translated.
void TranslatePageViaOnDevice(content::WebContents* web_contents,
                              std::string_view target_language = {});

// Restores the original text in place, keeping scroll position and form state.
void RestoreOriginalPage(content::WebContents* web_contents);

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_TRANSLATE_INJECTION_HANDLER_H_
