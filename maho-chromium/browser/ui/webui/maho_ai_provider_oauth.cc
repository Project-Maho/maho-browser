// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai_provider_oauth.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace maho::ai_oauth {

namespace {

// A token within this window of expiry is renewed before the next turn, so a
// long agent run cannot start with a credential that dies mid-request.
constexpr base::TimeDelta kRefreshWindow = base::Minutes(5);

struct ProviderEndpoints {
  const char* authorize_url;
  const char* token_url;
  const char* scopes;
  const char* redirect_path;
  uint16_t redirect_port;
};

const ProviderEndpoints* EndpointsFor(std::string_view provider) {
  static constexpr ProviderEndpoints kOpenAI = {
      "https://auth.openai.com/oauth/authorize",
      "https://auth.openai.com/oauth/token",
      "openid profile email offline_access", "/auth/callback", 1455};
  static constexpr ProviderEndpoints kAnthropic = {
      "https://claude.ai/oauth/authorize",
      "https://console.anthropic.com/v1/oauth/token",
      "org:create_api_key user:profile user:inference", "/callback", 0};
  if (provider == "openai") {
    return &kOpenAI;
  }
  if (provider == "anthropic") {
    return &kAnthropic;
  }
  return nullptr;
}

const char* AccessTokenPrefFor(std::string_view provider) {
  if (provider == "openai") {
    return maho::ai_prefs::kByokOpenAIEncryptedB64;
  }
  if (provider == "anthropic") {
    return maho::ai_prefs::kByokAnthropicEncryptedB64;
  }
  return nullptr;
}

const char* RefreshTokenPrefFor(std::string_view provider) {
  if (provider == "openai") {
    return maho::ai_prefs::kOAuthOpenAIRefreshEncryptedB64;
  }
  if (provider == "anthropic") {
    return maho::ai_prefs::kOAuthAnthropicRefreshEncryptedB64;
  }
  return nullptr;
}

const char* ExpiresAtPrefFor(std::string_view provider) {
  if (provider == "openai") {
    return maho::ai_prefs::kOAuthOpenAIExpiresAt;
  }
  if (provider == "anthropic") {
    return maho::ai_prefs::kOAuthAnthropicExpiresAt;
  }
  return nullptr;
}

const char* ClientIdPrefFor(std::string_view provider) {
  if (provider == "openai") {
    return maho::ai_prefs::kOAuthOpenAIClientId;
  }
  if (provider == "anthropic") {
    return maho::ai_prefs::kOAuthAnthropicClientId;
  }
  return nullptr;
}

std::string BuildConfigJson(std::string_view provider,
                            const std::string& client_id) {
  const ProviderEndpoints* endpoints = EndpointsFor(provider);
  if (!endpoints) {
    return std::string();
  }
  base::DictValue config;
  config.Set("provider", std::string(provider));
  config.Set("client_id", client_id);
  config.Set("authorize_url", endpoints->authorize_url);
  config.Set("token_url", endpoints->token_url);
  config.Set("scopes", endpoints->scopes);
  config.Set("redirect_path", endpoints->redirect_path);
  config.Set("redirect_port", static_cast<int>(endpoints->redirect_port));
  std::string json;
  if (!base::JSONWriter::Write(config, &json)) {
    return std::string();
  }
  return json;
}

std::optional<ProviderTokens> ParseTokensJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& dict = parsed->GetDict();
  const std::string* access_token = dict.FindString("accessToken");
  if (!access_token || access_token->empty()) {
    return std::nullopt;
  }
  ProviderTokens tokens;
  tokens.access_token = *access_token;
  if (const std::string* refresh_token = dict.FindString("refreshToken")) {
    tokens.refresh_token = *refresh_token;
  }
  tokens.expires_at = static_cast<int64_t>(dict.FindDouble("expiresAt").value_or(0));
  return tokens;
}

std::string ExtractErrorJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  const std::string* error = parsed->GetDict().FindString("error");
  return error ? *error : std::string();
}

std::string RefreshOnBlockingPool(std::string config_json,
                                  std::string refresh_token) {
  char* raw = maho_provider_oauth_refresh(config_json.c_str(),
                                          refresh_token.c_str());
  if (!raw) {
    return std::string();
  }
  std::string result(raw);
  maho_string_free(raw);
  return result;
}

content::WebContents* NavigateToProviderSignIn(Profile* profile,
                                               Browser* peek_host_browser,
                                               const GURL& url) {
  if (peek_host_browser && peek_host_browser->GetProfile() == profile &&
      profile->GetPrefs()->GetBoolean(maho::sidebar_prefs::kPeekEnabled) &&
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

class ProviderOAuthFlow;

struct CoreCallbackContext {
  explicit CoreCallbackContext(scoped_refptr<ProviderOAuthFlow> flow_in)
      : flow(std::move(flow_in)) {}

  scoped_refptr<ProviderOAuthFlow> flow;
};

scoped_refptr<ProviderOAuthFlow>& ActiveFlow() {
  static base::NoDestructor<scoped_refptr<ProviderOAuthFlow>> active_flow;
  return *active_flow;
}

class ProviderOAuthFlow
    : public base::RefCountedDeleteOnSequence<ProviderOAuthFlow>,
      public content::WebContentsObserver {
 public:
  ProviderOAuthFlow(Profile* profile,
                    std::string provider,
                    std::string client_id,
                    ProviderOAuthTokensCallback callback)
      : base::RefCountedDeleteOnSequence<ProviderOAuthFlow>(
            base::SequencedTaskRunner::GetCurrentDefault()),
        profile_(profile->GetWeakPtr()),
        provider_(std::move(provider)),
        client_id_(std::move(client_id)),
        callback_(std::move(callback)),
        ui_task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}

  bool Start(Browser* peek_host_browser) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    const std::string config_json = BuildConfigJson(provider_, client_id_);
    if (config_json.empty()) {
      return false;
    }

    auto* context = new CoreCallbackContext(this);
    char* started_json = maho_provider_oauth_start(
        config_json.c_str(), &ProviderOAuthFlow::OnCoreComplete, context);
    if (!started_json) {
      delete context;
      return false;
    }

    std::string json(started_json);
    maho_string_free(started_json);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      Retire("Could not start the provider sign-in.");
      return true;
    }
    if (const std::string* state = parsed->GetDict().FindString("state")) {
      state_ = *state;
    }
    const std::string* auth_url = parsed->GetDict().FindString("authUrl");
    if (!auth_url) {
      Retire("Could not start the provider sign-in.");
      return true;
    }
    GURL url(*auth_url);
    if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {
      Retire("Could not start the provider sign-in.");
      return true;
    }

    Profile* profile = profile_.get();
    if (!profile) {
      Retire("Could not start the provider sign-in.");
      return true;
    }
    content::WebContents* oauth_contents =
        NavigateToProviderSignIn(profile, peek_host_browser, url);
    if (!oauth_contents) {
      Retire("Could not start the provider sign-in.");
      return true;
    }
    Observe(oauth_contents);
    return true;
  }

  void FailUnarmedStart() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    Complete(false, ProviderTokens(),
             "Could not start the provider sign-in. Check the OAuth client ID.");
    ClearActive();
  }

  void RetireForSupersede() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (retired_) {
      return;
    }
    retired_ = true;
    CancelCoreFlow();
    Observe(nullptr);
    if (callback_) {
      std::move(callback_).Run(false, ProviderTokens(),
                               "Superseded by a newer sign-in.");
    }
  }

  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!navigation_handle || !navigation_handle->IsInPrimaryMainFrame()) {
      return;
    }
    // Once the provider redirects to the loopback receiver, the Rust listener
    // owns the outcome. Stop observing so the popup's own teardown is not
    // misreported as a user cancellation, which would drop a successful login.
    const std::string_view host = navigation_handle->GetURL().host();
    if (host == "127.0.0.1" || host == "localhost" || host == "[::1]") {
      Observe(nullptr);
    }
  }

  void WebContentsDestroyed() override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    scoped_refptr<ProviderOAuthFlow> keep_alive(this);
    Observe(nullptr);
    Retire("The provider sign-in was cancelled.");
  }

 private:
  friend class base::RefCountedDeleteOnSequence<ProviderOAuthFlow>;
  friend class base::DeleteHelper<ProviderOAuthFlow>;

  ~ProviderOAuthFlow() override = default;

  static void OnCoreComplete(void* user_data,
                             const char* tokens_json,
                             const char* error) {
    std::unique_ptr<CoreCallbackContext> context(
        static_cast<CoreCallbackContext*>(user_data));
    scoped_refptr<ProviderOAuthFlow> flow = std::move(context->flow);
    flow->ui_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&ProviderOAuthFlow::OnCoreCompleteOnUi, std::move(flow),
                       tokens_json ? tokens_json : std::string(),
                       error ? error : std::string()));
  }

  void OnCoreCompleteOnUi(std::string tokens_json, std::string error) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (retired_) {
      return;
    }
    state_.clear();
    CloseOAuthPopup();
    ClearActive();

    if (!error.empty()) {
      Complete(false, ProviderTokens(), error);
      return;
    }
    std::optional<ProviderTokens> tokens = ParseTokensJson(tokens_json);
    if (!tokens) {
      Complete(false, ProviderTokens(),
               "The provider returned an unreadable token response.");
      return;
    }
    Complete(true, std::move(*tokens), std::string());
  }

  void Retire(const std::string& message) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (retired_) {
      return;
    }
    retired_ = true;
    CancelCoreFlow();
    ClearActive();
    Complete(false, ProviderTokens(), message);
  }

  void Complete(bool ok, ProviderTokens tokens, const std::string& message) {
    retired_ = true;
    Observe(nullptr);
    if (callback_) {
      std::move(callback_).Run(ok, std::move(tokens), message);
    }
  }

  void CancelCoreFlow() {
    if (state_.empty()) {
      return;
    }
    maho_provider_oauth_cancel(state_.c_str());
    state_.clear();
  }

  void CloseOAuthPopup() {
    if (content::WebContents* contents = web_contents()) {
      Observe(nullptr);
      contents->Close();
    }
  }

  void ClearActive() {
    if (ActiveFlow().get() == this) {
      ActiveFlow().reset();
    }
  }

  base::WeakPtr<Profile> profile_;
  std::string provider_;
  std::string client_id_;
  std::string state_;
  ProviderOAuthTokensCallback callback_;
  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  bool retired_ = false;
};

void OnRefreshComplete(std::string provider,
                       ProviderOAuthTokensCallback callback,
                       std::string response) {
  if (response.empty()) {
    std::move(callback).Run(false, ProviderTokens(),
                            "Could not reach the provider's token endpoint.");
    return;
  }
  const std::string error = ExtractErrorJson(response);
  if (!error.empty()) {
    std::move(callback).Run(false, ProviderTokens(), error);
    return;
  }
  std::optional<ProviderTokens> tokens = ParseTokensJson(response);
  if (!tokens) {
    std::move(callback).Run(false, ProviderTokens(),
                            "The provider returned an unreadable token "
                            "response.");
    return;
  }
  std::move(callback).Run(true, std::move(*tokens), std::string());
}

}  // namespace

bool IsOAuthSupported(std::string_view provider) {
  return EndpointsFor(provider) != nullptr;
}

bool HasOAuthSession(const PrefService* prefs, std::string_view provider) {
  const char* refresh_pref = RefreshTokenPrefFor(provider);
  if (!prefs || !refresh_pref) {
    return false;
  }
  return !prefs->GetString(refresh_pref).empty();
}

std::string GetOAuthClientId(const PrefService* prefs,
                             std::string_view provider) {
  const char* client_id_pref = ClientIdPrefFor(provider);
  if (!prefs || !client_id_pref) {
    return std::string();
  }
  return prefs->GetString(client_id_pref);
}

void SetOAuthClientId(PrefService* prefs,
                      std::string_view provider,
                      const std::string& client_id) {
  const char* client_id_pref = ClientIdPrefFor(provider);
  if (!prefs || !client_id_pref) {
    return;
  }
  prefs->SetString(client_id_pref, client_id);
  prefs->CommitPendingWrite();
}

void StartProviderOAuth(Profile* profile,
                        Browser* peek_host_browser,
                        const std::string& provider,
                        const std::string& client_id,
                        ProviderOAuthTokensCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || profile->IsOffTheRecord()) {
    std::move(callback).Run(false, ProviderTokens(),
                            "Provider sign-in is unavailable in this profile.");
    return;
  }
  if (!IsOAuthSupported(provider)) {
    std::move(callback).Run(false, ProviderTokens(),
                            "This provider does not support OAuth sign-in.");
    return;
  }
  if (client_id.empty()) {
    std::move(callback).Run(
        false, ProviderTokens(),
        "Add the provider's OAuth client ID before signing in.");
    return;
  }
  if (ActiveFlow()) {
    ActiveFlow()->RetireForSupersede();
    ActiveFlow().reset();
  }

  auto flow = base::MakeRefCounted<ProviderOAuthFlow>(
      profile, provider, client_id, std::move(callback));
  ActiveFlow() = flow;
  if (!flow->Start(peek_host_browser)) {
    flow->FailUnarmedStart();
  }
}

bool StoreProviderOAuthTokens(PrefService* prefs,
                              const os_crypt_async::Encryptor& encryptor,
                              std::string_view provider,
                              const ProviderTokens& tokens) {
  const char* access_pref = AccessTokenPrefFor(provider);
  const char* refresh_pref = RefreshTokenPrefFor(provider);
  const char* expires_pref = ExpiresAtPrefFor(provider);
  if (!prefs || !access_pref || !refresh_pref || !expires_pref ||
      tokens.access_token.empty()) {
    return false;
  }

  std::string encrypted_access;
  if (!encryptor.EncryptString(tokens.access_token, &encrypted_access)) {
    return false;
  }
  prefs->SetString(access_pref, base::Base64Encode(encrypted_access));

  if (!tokens.refresh_token.empty()) {
    std::string encrypted_refresh;
    if (!encryptor.EncryptString(tokens.refresh_token, &encrypted_refresh)) {
      return false;
    }
    prefs->SetString(refresh_pref, base::Base64Encode(encrypted_refresh));
  }
  prefs->SetInt64(expires_pref, tokens.expires_at);
  prefs->CommitPendingWrite();
  return true;
}

std::string LoadProviderRefreshToken(
    const PrefService* prefs,
    const os_crypt_async::Encryptor& encryptor,
    std::string_view provider) {
  const char* refresh_pref = RefreshTokenPrefFor(provider);
  if (!prefs || !refresh_pref) {
    return std::string();
  }
  const std::string encrypted_b64 = prefs->GetString(refresh_pref);
  if (encrypted_b64.empty()) {
    return std::string();
  }
  std::string encrypted_bytes;
  if (!base::Base64Decode(encrypted_b64, &encrypted_bytes)) {
    return std::string();
  }
  std::string plaintext;
  if (!encryptor.DecryptString(encrypted_bytes, &plaintext)) {
    return std::string();
  }
  return plaintext;
}

bool IsAccessTokenExpiring(const PrefService* prefs,
                           std::string_view provider) {
  const char* expires_pref = ExpiresAtPrefFor(provider);
  if (!prefs || !expires_pref) {
    return false;
  }
  const int64_t expires_at = prefs->GetInt64(expires_pref);
  if (expires_at <= 0) {
    return false;
  }
  const base::Time deadline =
      base::Time::UnixEpoch() + base::Seconds(expires_at);
  return base::Time::Now() + kRefreshWindow >= deadline;
}

void RefreshProviderOAuthTokens(const std::string& provider,
                                const std::string& client_id,
                                const std::string& refresh_token,
                                ProviderOAuthTokensCallback callback) {
  const std::string config_json = BuildConfigJson(provider, client_id);
  if (config_json.empty() || refresh_token.empty()) {
    std::move(callback).Run(false, ProviderTokens(),
                            "This provider has no stored OAuth session.");
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&RefreshOnBlockingPool, config_json, refresh_token),
      base::BindOnce(&OnRefreshComplete, provider, std::move(callback)));
}

void ClearProviderOAuth(PrefService* prefs, std::string_view provider) {
  const char* access_pref = AccessTokenPrefFor(provider);
  const char* refresh_pref = RefreshTokenPrefFor(provider);
  const char* expires_pref = ExpiresAtPrefFor(provider);
  if (!prefs || !access_pref || !refresh_pref || !expires_pref) {
    return;
  }
  prefs->SetString(access_pref, "");
  prefs->SetString(refresh_pref, "");
  prefs->SetInt64(expires_pref, 0);
  prefs->CommitPendingWrite();
}

}  // namespace maho::ai_oauth
