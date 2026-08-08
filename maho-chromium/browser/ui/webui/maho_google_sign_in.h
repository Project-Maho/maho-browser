// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_GOOGLE_SIGN_IN_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_GOOGLE_SIGN_IN_H_

#include <string>

#include "base/functional/callback.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"

class Browser;
class GURL;
class Profile;

namespace content {
class WebContents;
}

namespace maho::auth {

// Runs the whole desktop Google Sign-In flow and reports the final result:
// maho-core mints the PKCE authorization URL and binds a loopback listener, the
// URL is opened in the supplied Peek-capable browser when possible, with the
// existing dedicated popup retained as the fallback, and the resulting
// id_token is exchanged for a relay session whose tokens are stored like a
// password login.
//
// The callback always runs on the UI thread. Only one user-visible flow may be
// in flight per browser process; a second call while one is pending fails fast.
using GoogleSignInCallback =
    base::OnceCallback<void(bool ok, const std::string& error_message)>;
void StartGoogleSignIn(Profile* profile,
                       Browser* peek_host_browser,
                       const os_crypt_async::Encryptor& encryptor,
                       GoogleSignInCallback callback);

namespace testing {

// Routes a supplied local fake-OAuth URL through the same Peek-or-popup host
// contract as the production flow. Browser tests use this without invoking the
// core PKCE or real Google endpoints.
content::WebContents* NavigateToGoogleSignInForTesting(
    Profile* profile,
    Browser* peek_host_browser,
    const GURL& url);

}  // namespace testing

}  // namespace maho::auth

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_GOOGLE_SIGN_IN_H_
