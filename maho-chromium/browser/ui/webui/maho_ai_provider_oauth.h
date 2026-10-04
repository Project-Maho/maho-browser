// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_PROVIDER_OAUTH_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_PROVIDER_OAUTH_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "components/os_crypt/async/common/encryptor.h"

class Browser;
class PrefService;
class Profile;

namespace maho::ai_oauth {

struct ProviderTokens {
  std::string access_token;
  std::string refresh_token;
  // Unix seconds; 0 when the provider reported no lifetime.
  int64_t expires_at = 0;
};

bool IsOAuthSupported(std::string_view provider);

bool HasOAuthSession(const PrefService* prefs, std::string_view provider);

std::string GetOAuthClientId(const PrefService* prefs,
                             std::string_view provider);

void SetOAuthClientId(PrefService* prefs,
                      std::string_view provider,
                      const std::string& client_id);

using ProviderOAuthTokensCallback =
    base::OnceCallback<void(bool ok,
                            ProviderTokens tokens,
                            const std::string& error_message)>;

// PKCE loopback sign-in. The callback always runs on the UI thread; a second
// start supersedes the first, and tokens are handed back unencrypted so the
// caller owns storage.
void StartProviderOAuth(Profile* profile,
                        Browser* peek_host_browser,
                        const std::string& provider,
                        const std::string& client_id,
                        ProviderOAuthTokensCallback callback);

// Writes the access token into the provider's encrypted BYOK slot, so the agent
// credential path serves an OAuth login exactly like a pasted key.
bool StoreProviderOAuthTokens(PrefService* prefs,
                              const os_crypt_async::Encryptor& encryptor,
                              std::string_view provider,
                              const ProviderTokens& tokens);

std::string LoadProviderRefreshToken(const PrefService* prefs,
                                     const os_crypt_async::Encryptor& encryptor,
                                     std::string_view provider);

bool IsAccessTokenExpiring(const PrefService* prefs,
                           std::string_view provider);

// Runs the token exchange on a blocking pool thread; replies on this sequence.
void RefreshProviderOAuthTokens(const std::string& provider,
                                const std::string& client_id,
                                const std::string& refresh_token,
                                ProviderOAuthTokensCallback callback);

void ClearProviderOAuth(PrefService* prefs, std::string_view provider);

}  // namespace maho::ai_oauth

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_PROVIDER_OAUTH_H_
