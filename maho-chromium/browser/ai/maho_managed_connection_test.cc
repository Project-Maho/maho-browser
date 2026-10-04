// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_managed_connection_test.h"

#include <utility>

#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"

namespace maho::ai {

MahoManagedConnectionTest::MahoManagedConnectionTest(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)) {}

MahoManagedConnectionTest::~MahoManagedConnectionTest() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoManagedConnectionTest::RunTest(
    const os_crypt_async::Encryptor* encryptor,
    TestCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!encryptor) {
    std::move(callback).Run(false, "Decryption helper not ready.");
    return;
  }

  encryptor_ = encryptor;

  std::string access_token = maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  if (access_token.empty()) {
    std::move(callback).Run(false, "No active subscription session found. Please sign in.");
    return;
  }

  StartRequest(access_token, std::move(callback), /*is_retry=*/false);
}

void MahoManagedConnectionTest::StartRequest(
    const std::string& access_token,
    TestCallback callback,
    bool is_retry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  char* p = maho_core_managed_proxy_url();
  std::string proxy_url(p ? p : "https://proxy.maho.co/v1");
  if (p) maho_core_free_string(p);
  if (proxy_url.back() == '/') {
    proxy_url.pop_back();
  }
  std::string url = proxy_url + "/models";

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = GURL(url);
  resource_request->method = "GET";
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource_request->headers.SetHeader("Authorization", "Bearer " + access_token);

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_managed_connection_test", R"(
        semantics {
          sender: "Maho Managed Connection Test"
          description: "Tests connectivity to the Maho Managed AI service proxy."
          trigger: "User clicks the Test Connection button."
          data: "Bearer token credentials."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "This feature cannot be disabled."
        }
      )");

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request), traffic_annotation);
  loader->SetTimeoutDuration(base::Seconds(10));

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoManagedConnectionTest::OnRequestComplete,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     is_retry, std::move(loader)));
}

void MahoManagedConnectionTest::OnRequestComplete(
    TestCallback callback,
    bool is_retry,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  int response_code = -1;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    response_code = loader->ResponseInfo()->headers->response_code();
  }

  if (loader->NetError() != net::OK) {
    std::move(callback).Run(false, "Cannot reach Maho AI service. Check your connection.");
    return;
  }

  if (response_code == 200 && response_body) {
    std::optional<base::Value> val = base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
    int model_count = 0;
    if (val && val->is_dict()) {
      const base::ListValue* data = val->GetDict().FindList("data");
      if (data) {
        model_count = data->size();
      }
    }
    std::move(callback).Run(true, "Connected. " + std::to_string(model_count) + " models available.");
    return;
  }

  if (response_code == 401 && !is_retry) {
    if (!encryptor_) {
      std::move(callback).Run(false, "Decryption helper not ready.");
      return;
    }
    std::string refresh_token = maho::auth::GetRelayRefreshToken(prefs_, *encryptor_);
    if (refresh_token.empty()) {
      std::move(callback).Run(false, "Session expired. Please sign in again.");
      return;
    }
    maho::auth::RefreshAccessToken(
        refresh_token, url_loader_factory_,
        base::BindOnce(&MahoManagedConnectionTest::OnRefreshComplete,
                       weak_factory_.GetWeakPtr(), std::move(callback)));
    return;
  }

  if (response_code == 401) {
    std::move(callback).Run(false, "Session expired. Please sign in again.");
    return;
  }

  std::move(callback).Run(false, "Cannot reach Maho AI service (HTTP " + std::to_string(response_code) + ").");
}

void MahoManagedConnectionTest::OnRefreshComplete(
    TestCallback callback,
    bool success,
    int http_status,
    std::optional<maho::auth::RefreshedTokens> tokens) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!success || !tokens.has_value()) {
    std::move(callback).Run(false, "Session expired. Please sign in again.");
    return;
  }

  if (!encryptor_) {
    std::move(callback).Run(false, "Decryption helper not ready after token refresh.");
    return;
  }

  maho::auth::StoreTokensParams store_params;
  store_params.access_token = tokens->access_token;
  store_params.refresh_token = tokens->refresh_token;
  store_params.access_expires_at = tokens->access_expires_at;
  store_params.refresh_expires_at = tokens->refresh_expires_at;
  store_params.user_email = prefs_->GetString(maho::account_prefs::kRelayUserEmail);
  store_params.user_id = prefs_->GetString(maho::account_prefs::kRelayUserId);
  store_params.user_display_name = prefs_->GetString(maho::account_prefs::kRelayUserDisplayName);
  store_params.user_tier = prefs_->GetString(maho::account_prefs::kRelayUserTier);
  maho::auth::PreserveRelayIdentityMetadata(prefs_, &store_params);

  maho::auth::StoreRelayTokens(prefs_, *encryptor_, store_params);

  std::string new_access_token = maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  StartRequest(new_access_token, std::move(callback), /*is_retry=*/true);
}

}  // namespace maho::ai
