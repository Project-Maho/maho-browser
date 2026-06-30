// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_auth_utils.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace maho::auth {

StoreTokensParams::StoreTokensParams() = default;
StoreTokensParams::~StoreTokensParams() = default;
StoreTokensParams::StoreTokensParams(const StoreTokensParams&) = default;
StoreTokensParams& StoreTokensParams::operator=(const StoreTokensParams&) =
    default;
StoreTokensParams::StoreTokensParams(StoreTokensParams&&) noexcept = default;
StoreTokensParams& StoreTokensParams::operator=(StoreTokensParams&&) noexcept =
    default;

namespace {

constexpr char kDefaultRelayUrl[] = "https://relay.maho.co";

const net::NetworkTrafficAnnotationTag kRefreshTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_auth_refresh", R"(
      semantics {
        sender: "Maho Auth Refresh"
        description:
          "Refreshes the relay access token using the stored refresh token."
        trigger:
          "A user-initiated request received HTTP 401 from the relay; the "
          "browser attempts a single token refresh before failing."
        data: "Refresh token in the request body."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting:
          "Active while the user is signed in to Maho."
        policy_exception_justification: "Not implemented."
      }
    )");

std::string DecryptB64(const std::string& b64,
                       const os_crypt_async::Encryptor& encryptor) {
  if (b64.empty()) {
    return {};
  }
  std::string ciphertext;
  if (!base::Base64Decode(b64, &ciphertext)) {
    return {};
  }
  std::string plaintext;
  if (!encryptor.DecryptString(ciphertext, &plaintext)) {
    return {};
  }
  return plaintext;
}

std::string EncryptB64(const std::string& plaintext,
                       const os_crypt_async::Encryptor& encryptor) {
  if (plaintext.empty()) {
    return {};
  }
  std::string ciphertext;
  if (!encryptor.EncryptString(plaintext, &ciphertext)) {
    return {};
  }
  return base::Base64Encode(ciphertext);
}

}  // namespace

std::string GetRelayAccessToken(PrefService* prefs,
                                const os_crypt_async::Encryptor& encryptor) {
  return DecryptB64(
      prefs->GetString(maho::account_prefs::kRelayAccessTokenEncryptedB64),
      encryptor);
}

std::string GetRelayRefreshToken(PrefService* prefs,
                                 const os_crypt_async::Encryptor& encryptor) {
  return DecryptB64(
      prefs->GetString(maho::account_prefs::kRelayRefreshTokenEncryptedB64),
      encryptor);
}

bool StoreRelayTokens(PrefService* prefs,
                      const os_crypt_async::Encryptor& encryptor,
                      const StoreTokensParams& params) {
  const std::string access_b64 = EncryptB64(params.access_token, encryptor);
  const std::string refresh_b64 = EncryptB64(params.refresh_token, encryptor);
  if (access_b64.empty() || refresh_b64.empty()) {
    return false;
  }

  prefs->SetString(maho::account_prefs::kRelayAccessTokenEncryptedB64,
                   access_b64);
  prefs->SetString(maho::account_prefs::kRelayRefreshTokenEncryptedB64,
                   refresh_b64);
  prefs->SetInt64(maho::account_prefs::kRelayAccessTokenExpiresAt,
                  params.access_expires_at);
  prefs->SetInt64(maho::account_prefs::kRelayRefreshTokenExpiresAt,
                  params.refresh_expires_at);
  prefs->SetString(maho::account_prefs::kRelayUserEmail, params.user_email);
  prefs->SetString(maho::account_prefs::kRelayUserId, params.user_id);
  prefs->SetString(maho::account_prefs::kRelayUserDisplayName,
                   params.user_display_name);
  prefs->SetString(maho::account_prefs::kRelayUserTier, params.user_tier);
  return true;
}

void ClearRelayTokens(PrefService* prefs) {
  prefs->ClearPref(maho::account_prefs::kRelayAccessTokenEncryptedB64);
  prefs->ClearPref(maho::account_prefs::kRelayRefreshTokenEncryptedB64);
  prefs->ClearPref(maho::account_prefs::kRelayAccessTokenExpiresAt);
  prefs->ClearPref(maho::account_prefs::kRelayRefreshTokenExpiresAt);
  prefs->ClearPref(maho::account_prefs::kRelayUserEmail);
  prefs->ClearPref(maho::account_prefs::kRelayUserId);
  prefs->ClearPref(maho::account_prefs::kRelayUserDisplayName);
  prefs->ClearPref(maho::account_prefs::kRelayUserTier);
}

std::string GetRelayBaseUrl() {
  const char* env = std::getenv("MAHO_RELAY_URL");
  if (env && *env) {
    return env;
  }
  return kDefaultRelayUrl;
}

void RefreshAccessToken(
    const std::string& current_refresh_token,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    RefreshCallback callback) {
  if (current_refresh_token.empty()) {
    std::move(callback).Run(false, std::nullopt);
    return;
  }

  // Body: {"refresh_token": "<token>"}
  base::DictValue body_dict;
  body_dict.Set("refresh_token", current_refresh_token);
  std::string body_json;
  base::JSONWriter::Write(body_dict, &body_json);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = GURL(GetRelayBaseUrl() + "/auth/refresh");
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kRefreshTrafficAnnotation);
  url_loader->AttachStringForUpload(body_json, "application/json");

  auto* raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> loader,
             RefreshCallback cb,
             std::optional<std::string> response_body) {
            const int net_error = loader->NetError();
            const int http_status =
                (loader->ResponseInfo() && loader->ResponseInfo()->headers)
                    ? loader->ResponseInfo()->headers->response_code()
                    : 0;

            if (net_error != net::OK || http_status != 200 || !response_body) {
              std::move(cb).Run(false, std::nullopt);
              return;
            }

            auto parsed = base::JSONReader::Read(
                *response_body, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict()) {
              std::move(cb).Run(false, std::nullopt);
              return;
            }

            const auto& dict = parsed->GetDict();
            const std::string* new_access = dict.FindString("access_token");
            const std::string* new_refresh = dict.FindString("refresh_token");
            if (!new_access || !new_refresh) {
              std::move(cb).Run(false, std::nullopt);
              return;
            }

            RefreshedTokens tokens;
            tokens.access_token = *new_access;
            tokens.refresh_token = *new_refresh;
            // FindDouble + cast keeps full int64 range for unix timestamps.
            tokens.access_expires_at = static_cast<int64_t>(
                dict.FindDouble("expires_at").value_or(0));
            tokens.refresh_expires_at = static_cast<int64_t>(
                dict.FindDouble("refresh_expires_at").value_or(0));

            std::move(cb).Run(true, std::move(tokens));
          },
          std::move(url_loader), std::move(callback)),
      /*max_body_size=*/16384);
}

}  // namespace maho::auth
