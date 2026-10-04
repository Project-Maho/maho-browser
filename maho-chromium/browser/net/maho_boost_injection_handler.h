// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_BOOST_INJECTION_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_BOOST_INJECTION_HANDLER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class MahoBoostInjectionHandler
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoBoostInjectionHandler> {
 public:
  enum class ContentScriptMode { kNone, kZap, kPicker };

  using ContentScriptEventCallback =
      base::RepeatingCallback<void(const std::string& type,
                                   const std::string& selector,
                                   const std::string& msg)>;

  ~MahoBoostInjectionHandler() override;

  MahoBoostInjectionHandler(const MahoBoostInjectionHandler&) = delete;
  MahoBoostInjectionHandler& operator=(const MahoBoostInjectionHandler&) =
      delete;

  static void OnCoreUpdatesJson(const std::string& updates_json);
  static void ReinjectForDomain(const std::string& domain);

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  void InjectBoostCssIntoTab(content::RenderFrameHost* render_frame_host,
                             const std::string& url);

  void EnterZapMode(const std::vector<std::string>& zap_selectors,
                    ContentScriptEventCallback callback);
  void ExitZapMode();
  void EnterPickerMode(ContentScriptEventCallback callback);
  void ExitPickerMode();
  void ExitCurrentEditMode();

  // Runs only in the Boost isolated world so interactive tests can drive the
  // real content-script overlay without publishing a page-world test API.
  void ExecuteInIsolatedWorldForTesting(
      const std::string& script,
      base::OnceCallback<void(base::Value)> callback);

  ContentScriptMode content_script_mode() const {
    return content_script_mode_;
  }
  bool poll_timer_is_running_for_testing() const {
    return poll_timer_.IsRunning();
  }
  bool poll_in_flight_for_testing() const { return poll_in_flight_; }

 private:
  friend class content::WebContentsUserData<MahoBoostInjectionHandler>;
  explicit MahoBoostInjectionHandler(content::WebContents* web_contents);

  void InjectBoostCss(content::RenderFrameHost* render_frame_host,
                      const std::string& url);

  void InjectContentScript();
  void ExecuteInIsolatedWorld(const std::string& script);
  void ExecuteInIsolatedWorldWithResult(
      const std::string& script,
      base::OnceCallback<void(base::Value)> callback);
  void PollContentScript();
  void OnPollResult(uint64_t generation, base::Value result);
  void StopPolling();

  ContentScriptMode content_script_mode_ = ContentScriptMode::kNone;
  bool content_script_injected_ = false;
  bool poll_in_flight_ = false;
  uint64_t poll_generation_ = 0;
  ContentScriptEventCallback event_callback_;
  base::RepeatingTimer poll_timer_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoBoostInjectionHandler> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_BOOST_INJECTION_HANDLER_H_
