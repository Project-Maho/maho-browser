// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_google_sign_in.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace maho::auth {

namespace {

class GoogleSignInFlow;

struct CoreCallbackContext {
  explicit CoreCallbackContext(scoped_refptr<GoogleSignInFlow> flow_in)
      : flow(std::move(flow_in)) {}

  scoped_refptr<GoogleSignInFlow> flow;
};

scoped_refptr<GoogleSignInFlow>& ActiveFlow() {
  static base::NoDestructor<scoped_refptr<GoogleSignInFlow>> active_flow;
  return *active_flow;
}

content::WebContents* NavigateToGoogleSignIn(Profile* profile,
                                             Browser* peek_host_browser,
                                             const GURL& url) {
  // The caller supplies the explicit host surface. Native Navigate(NEW_POPUP)
  // bypasses Browser::AddNewContents, so route directly through the host's
  // controller and observe the WebContents that ShowUrl creates and owns.
  if (peek_host_browser &&
      peek_host_browser->GetProfile() == profile &&
      profile->GetPrefs()->GetBoolean(
          maho::sidebar_prefs::kPeekEnabled) &&
      maho::IsPeekEligible(peek_host_browser)) {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(peek_host_browser);
    maho::MahoPeekController* peek_controller =
        browser_view ? browser_view->GetOrCreateMahoPeekController() : nullptr;
    if (peek_controller) {
      if (content::WebContents* hosted_contents =
              peek_controller->ShowUrl(/*source=*/nullptr, url)) {
        return hosted_contents;
      }
    }
  }

  NavigateParams params(profile, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_POPUP;
  params.window_features.bounds = gfx::Rect(0, 0, 720, 840);
  Navigate(&params);
  return params.navigated_or_inserted_contents;
}

class GoogleSignInFlow
    : public base::RefCountedDeleteOnSequence<GoogleSignInFlow>,
      public content::WebContentsObserver {
 public:
  GoogleSignInFlow(Profile* profile, GoogleSignInCallback callback)
      : base::RefCountedDeleteOnSequence<GoogleSignInFlow>(
            base::SequencedTaskRunner::GetCurrentDefault()),
        profile_(profile->GetWeakPtr()),
        callback_(std::move(callback)),
        ui_task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}

  bool Start(Browser* peek_host_browser) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    auto* context = new CoreCallbackContext(this);
    char* started_json = maho_google_sign_in_start(
        /*client_id=*/"", &GoogleSignInFlow::OnCoreComplete, context);
    if (!started_json) {
      delete context;
      return false;
    }

    std::string json(started_json);
    maho_string_free(started_json);
    auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      Retire("Could not start Google sign-in.");
      return true;
    }
    // Keep the CSRF state so an abandoned flow can tear the Rust listener down
    // instead of leaving it (and its bound loopback port) alive until the
    // backstop timeout. See RetireForSupersede()/Retire().
    if (const std::string* state = parsed->GetDict().FindString("state")) {
      state_ = *state;
    }
    const std::string* auth_url = parsed->GetDict().FindString("authUrl");
    if (!auth_url) {
      Retire("Could not start Google sign-in.");
      return true;
    }
    GURL url(*auth_url);
    if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {
      Retire("Could not start Google sign-in.");
      return true;
    }

    Profile* profile = profile_.get();
    if (!profile) {
      Retire("Could not start Google sign-in.");
      return true;
    }
    content::WebContents* oauth_contents =
        NavigateToGoogleSignIn(profile, peek_host_browser, url);
    if (!oauth_contents) {
      Retire("Could not start Google sign-in.");
      return true;
    }
    Observe(oauth_contents);
    return true;
  }

  void FailUnarmedStart() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    CompleteUi(false, "Could not start Google sign-in.");
    ClearActive();
  }

  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!navigation_handle || !navigation_handle->IsInPrimaryMainFrame()) {
      return;
    }
    // Google has redirected the popup to our loopback receiver
    // (http://127.0.0.1:<port>/...). From this point the Rust listener owns the
    // outcome and reports it through OnCoreComplete. Stop observing so the
    // popup's teardown (it navigates to a local page and is then closed) is not
    // misreported by WebContentsDestroyed as a user cancellation, which would
    // set retired_ and drop an already-successful sign-in.
    //
    // REGRESSION CONTRACT: docs/operations/google-signin-loopback-reliability.md
    // (F3). Do NOT remove the loopback-host check or the Observe(nullptr) call.
    const std::string_view host = navigation_handle->GetURL().host();
    if (host == "127.0.0.1" || host == "localhost" || host == "[::1]") {
      Observe(nullptr);
    }
  }

  void WebContentsDestroyed() override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    scoped_refptr<GoogleSignInFlow> keep_alive(this);
    Observe(nullptr);
    Retire("Google sign-in was cancelled.");
  }

  // Abandons this flow because a newer sign-in is superseding it. Unlike
  // Retire(), it does not report a cancellation error to any UI (the new flow
  // now owns the surface); it just stops observing and resolves this flow's own
  // callback benignly so a stale JS promise cannot hang.
  void RetireForSupersede() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (retired_) {
      return;
    }
    retired_ = true;
    Observe(nullptr);
    CancelCoreFlow();
    if (callback_) {
      std::move(callback_).Run(false, "Superseded by a newer sign-in.");
    }
  }

 private:
  friend class base::RefCountedDeleteOnSequence<GoogleSignInFlow>;
  friend class base::DeleteHelper<GoogleSignInFlow>;

  ~GoogleSignInFlow() override = default;

  static void OnCoreComplete(void* user_data,
                             const char* id_token,
                             const char* nonce,
                             const char* error) {
    std::unique_ptr<CoreCallbackContext> context(
        static_cast<CoreCallbackContext*>(user_data));
    scoped_refptr<GoogleSignInFlow> flow = std::move(context->flow);
    flow->ui_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&GoogleSignInFlow::OnCoreCompleteOnUi, std::move(flow),
                       id_token ? id_token : std::string(),
                       nonce ? nonce : std::string(),
                       error ? error : std::string()));
  }

  void OnCoreCompleteOnUi(std::string id_token,
                          std::string nonce,
                          std::string error) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    // The Rust flow has reported its result, so there is nothing left to
    // cancel; a later Retire() must not cancel an unrelated newer flow.
    state_.clear();
    if (retired_) {
      return;
    }
    if (!error.empty() || id_token.empty()) {
      CloseOAuthPopup();
      ClearActive();
      CompleteUi(false, error.empty() ? "Google sign-in failed." : error);
      return;
    }

    // Google authorization is complete. From here on the active flow belongs
    // to the encryptor/relay result, so closing the OAuth tab must not report a
    // conflicting cancellation while relay login can still persist tokens.
    // The popup has served its purpose, so close it now; relay login continues
    // in the background and reports its result on the Welcome surface.
    CloseOAuthPopup();

    if (!profile_ || !g_browser_process ||
        !g_browser_process->os_crypt_async()) {
      Retire("Google sign-in was cancelled.");
      return;
    }

    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&GoogleSignInFlow::OnEncryptorReady,
                       scoped_refptr<GoogleSignInFlow>(this),
                       std::move(id_token), std::move(nonce)));
  }

  void OnEncryptorReady(std::string id_token,
                        std::string nonce,
                        scoped_refptr<os_crypt_async::Encryptor> encryptor) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (retired_) {
      return;
    }
    Profile* profile = profile_.get();
    if (!profile) {
      Retire("Google sign-in was cancelled.");
      return;
    }

    encryptor_ = std::move(encryptor);
    auto url_loader_factory = profile->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    MahoRelayGoogleLogin(
        base::BindRepeating(
            [](base::WeakPtr<Profile> profile) -> PrefService* {
              return profile ? profile->GetPrefs() : nullptr;
            },
            profile_),
        url_loader_factory, *encryptor_, id_token, nonce,
        base::BindOnce(&GoogleSignInFlow::OnRelayComplete,
                       scoped_refptr<GoogleSignInFlow>(this)));
  }

  void OnRelayComplete(bool ok, const std::string& error_message) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    CloseOAuthPopup();
    ClearActive();
    CompleteUi(ok, error_message);
  }

  // Closes the dedicated OAuth popup once it is no longer needed. Stops
  // observing BEFORE closing so the WebContents teardown is not misreported as
  // a user cancellation, then requests the popup to close.
  void CloseOAuthPopup() {
    content::WebContents* popup = web_contents();
    Observe(nullptr);
    if (popup) {
      popup->Close();
    }
  }

  void Retire(const std::string& error_message) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (retired_) {
      return;
    }
    retired_ = true;
    Observe(nullptr);
    CancelCoreFlow();
    CompleteUi(false, error_message);
    ClearActive();
  }

  // Ends the Rust listener for this flow. Without it an abandoned flow keeps a
  // thread and a bound loopback port for the whole CALLBACK_TIMEOUT backstop,
  // and its pending callback overlaps the next sign-in attempt.
  //
  // REGRESSION CONTRACT: docs/operations/google-signin-loopback-reliability.md
  // (invariant 5). Keep this wired from every abandonment path.
  void CancelCoreFlow() {
    if (state_.empty()) {
      return;
    }
    maho_google_sign_in_cancel(state_.c_str());
    state_.clear();
  }

  void CompleteUi(bool ok, const std::string& error_message) {
    if (callback_) {
      std::move(callback_).Run(ok, error_message);
    }
  }

  void ClearActive() {
    if (ActiveFlow().get() == this) {
      ActiveFlow().reset();
    }
  }

  base::WeakPtr<Profile> profile_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  GoogleSignInCallback callback_;
  const scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  // CSRF state of the in-flight Rust flow, cleared once that flow has either
  // reported its result or been cancelled.
  std::string state_;
  bool retired_ = false;
};

}  // namespace

namespace testing {

content::WebContents* NavigateToGoogleSignInForTesting(
    Profile* profile,
    Browser* peek_host_browser,
    const GURL& url) {
  return NavigateToGoogleSignIn(profile, peek_host_browser, url);
}

}  // namespace testing

void StartGoogleSignIn(Profile* profile,
                       Browser* peek_host_browser,
                       const os_crypt_async::Encryptor&,
                       GoogleSignInCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || profile->IsOffTheRecord()) {
    std::move(callback).Run(false, profile ? "Google sign-in is unavailable."
                                           : "No profile.");
    return;
  }
  if (ActiveFlow()) {
    // Supersede a stale/abandoned prior flow instead of blocking the user with
    // "already in progress". The old flow's listener thread self-clears on its
    // backstop timeout; the new flow binds a fresh loopback port.
    //
    // REGRESSION CONTRACT: docs/operations/google-signin-loopback-reliability.md
    // (F4). A new start must supersede, never reject with "already in progress".
    ActiveFlow()->RetireForSupersede();
    ActiveFlow().reset();
  }

  auto flow =
      base::MakeRefCounted<GoogleSignInFlow>(profile, std::move(callback));
  ActiveFlow() = flow;
  if (!flow->Start(peek_host_browser)) {
    flow->FailUnarmedStart();
  }
}

}  // namespace maho::auth
