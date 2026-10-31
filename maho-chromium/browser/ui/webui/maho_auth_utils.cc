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
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include <functional>

namespace maho::auth {

StoreTokensParams::StoreTokensParams() = default;
StoreTokensParams::~StoreTokensParams() = default;
StoreTokensParams::StoreTokensParams(const StoreTokensParams &) = default;
StoreTokensParams &
StoreTokensParams::operator=(const StoreTokensParams &) = default;
StoreTokensParams::StoreTokensParams(StoreTokensParams &&) noexcept = default;
StoreTokensParams &
StoreTokensParams::operator=(StoreTokensParams &&) noexcept = default;

namespace {

constexpr char kDefaultRelayUrl[] = "https://relay.mahobrowser.com";

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
        policy_exception_justification:
          "Not controlled by enterprise policy. Triggered when the browser "
          "receives a 401 response and attempts to refresh the access token "
          "using the stored refresh token. No ambient or background network activity."
      }
    )");

std::string DecryptB64(const std::string &b64,
                       const os_crypt_async::Encryptor &encryptor) {
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

std::string EncryptB64(const std::string &plaintext,
                       const os_crypt_async::Encryptor &encryptor) {
  if (plaintext.empty()) {
    return {};
  }
  std::string ciphertext;
  if (!encryptor.EncryptString(plaintext, &ciphertext)) {
    return {};
  }
  return base::Base64Encode(ciphertext);
}

const net::NetworkTrafficAnnotationTag kAuthLoginTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_auth_login", R"(
      semantics {
        sender: "Maho Auth Login"
        description:
          "Authenticates the user with the Maho relay using email and password."
        trigger:
          "User submits the sign-in form in the Account settings pane or welcome wizard."
        data: "User email and password in the request body."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by an explicit user action; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. The user explicitly submits "
          "credentials in the Account settings pane or welcome wizard to authenticate with the "
          "Maho relay service. No ambient or background network activity."
      }
    )");

const net::NetworkTrafficAnnotationTag kSyncBootstrapTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_sync_account_bootstrap", R"(
      semantics {
        sender: "Maho Sync Account Bootstrap"
        description:
          "Retrieves or creates the signed-in account's encrypted Sync "
          "bootstrap material so all devices derive the same Sync room key."
        trigger: "A relay account login succeeds."
        data: "Bearer access token and versioned Sync room bootstrap material."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Active while the user is signed in to Maho."
        policy_exception_justification:
          "Required to configure account-based end-to-end encrypted Sync "
          "immediately after the user signs in."
      }
    )");

const net::NetworkTrafficAnnotationTag kAuthGoogleLoginTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_auth_google_login", R"(
      semantics {
        sender: "Maho Auth Google Sign-In"
        description:
          "Exchanges a Google OpenID Connect id_token for a Maho relay "
          "session. The id_token is obtained by an OAuth2 PKCE flow the user "
          "completes on Google's own sign-in page."
        trigger:
          "User activates Sign in with Google in the Account settings pane or "
          "welcome wizard and completes Google's consent screen."
        data:
          "Google id_token (contains the user's Google account id, email and "
          "display name), the matching one-time nonce, and this device's "
          "name/type/id."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by an explicit user action; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. The user explicitly chooses "
          "Sign in with Google and completes Google's consent screen before "
          "this request is made. No ambient or background network activity."
      }
    )");

// Fills the shared device identity fields every relay auth request carries.
// The device id is generated once and persisted so a later sign-in reuses this
// device's relay row and sync room instead of orphaning its snapshot.
void SetDeviceFields(PrefService *prefs, base::DictValue &body) {
  std::string device_id = prefs->GetString(maho::account_prefs::kRelayDeviceId);
  if (device_id.empty()) {
    device_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(maho::account_prefs::kRelayDeviceId, device_id);
  }
  body.Set("device_id", device_id);
#if BUILDFLAG(IS_MAC)
  body.Set("device_name", "Maho macOS");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_WIN)
  body.Set("device_name", "Maho Windows");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_LINUX)
  body.Set("device_name", "Maho Linux");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_IOS)
  body.Set("device_name", "Maho iOS");
  body.Set("device_type", "mobile");
#elif BUILDFLAG(IS_ANDROID)
  body.Set("device_name", "Maho Android");
  body.Set("device_type", "mobile");
#else
  body.Set("device_name", "Maho");
  body.Set("device_type", "unknown");
#endif
}

struct SyncBootstrapMaterial {
  int version = 0;
  std::string room_id;
  std::string seed;
  // Optional account-escrow vault key slots. Empty strings mean the bootstrap
  // record carries no escrow (a legacy passphrase vault, or a record that this
  // device should publish its own escrow to after sign-in).
  std::string vault_kdf_params;
  std::string vault_wrapped_account_key;
};

std::optional<SyncBootstrapMaterial>
ParseSyncBootstrap(const std::string &json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue &dict = parsed->GetDict();
  const std::optional<int> version = dict.FindInt("version");
  const std::string *room_id = dict.FindString("room_id");
  if (!room_id) {
    room_id = dict.FindString("roomId");
  }
  const std::string *seed = dict.FindString("seed");
  if (!version || *version != 1 || !room_id || room_id->empty() || !seed ||
      seed->empty()) {
    return std::nullopt;
  }
  SyncBootstrapMaterial material{*version, *room_id, *seed};
  if (const base::DictValue *escrow = dict.FindDict("vault_escrow")) {
    const std::string *kdf_params = escrow->FindString("kdf_params");
    const std::string *wrapped = escrow->FindString("wrapped_account_key");
    if (kdf_params && wrapped && !kdf_params->empty() && !wrapped->empty()) {
      material.vault_kdf_params = *kdf_params;
      material.vault_wrapped_account_key = *wrapped;
    }
  }
  return material;
}

std::string GetSyncRoomIdOnCoreSequence() {
  MahoCore *core = maho::GetCore();
  if (!core) {
    return {};
  }
  char *value = maho_core_get_sync_room_id(core);
  if (!value) {
    return {};
  }
  std::string room_id(value);
  maho_string_free(value);
  return room_id;
}

std::optional<SyncBootstrapMaterial> GenerateSyncBootstrapOnCoreSequence() {
  MahoCore *core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  char *value = maho_core_generate_sync_bootstrap(core);
  if (!value) {
    return std::nullopt;
  }
  std::string json(value);
  maho_string_free(value);
  return ParseSyncBootstrap(json);
}

bool ConfigureSyncBootstrapOnCoreSequence(SyncBootstrapMaterial bootstrap) {
  MahoCore *core = maho::GetCore();
  if (!core) {
    return false;
  }
  const std::string relay_url = GetSyncRelayUrl();
  const char *escrow_kdf_params = bootstrap.vault_kdf_params.empty()
                                      ? nullptr
                                      : bootstrap.vault_kdf_params.c_str();
  const char *escrow_wrapped_key =
      bootstrap.vault_wrapped_account_key.empty()
          ? nullptr
          : bootstrap.vault_wrapped_account_key.c_str();
  char *value = maho_core_configure_sync_bootstrap_with_escrow(
      core, relay_url.c_str(), bootstrap.seed.c_str(), escrow_kdf_params,
      escrow_wrapped_key);
  if (!value) {
    return false;
  }
  std::string json(value);
  maho_string_free(value);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  return parsed && parsed->is_dict() &&
         parsed->GetDict().FindBool("success").value_or(false);
}

std::string ExportVaultEscrowOnCoreSequence() {
  MahoCore *core = maho::GetCore();
  if (!core) {
    return {};
  }
  char *value = maho_core_export_vault_account_escrow(core);
  if (!value) {
    return {};
  }
  std::string json(value);
  maho_string_free(value);
  return json;
}

void PutVaultEscrow(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, std::string room_id, std::string seed,
    std::string kdf_params, std::string wrapped_account_key,
    LoginResultCallback callback) {
  base::DictValue body;
  body.Set("version", 1);
  body.Set("room_id", room_id);
  body.Set("seed", seed);
  base::DictValue escrow;
  escrow.Set("kdf_params", kdf_params);
  escrow.Set("wrapped_account_key", wrapped_account_key);
  body.Set("vault_escrow", std::move(escrow));
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(GetSyncRelayUrl() + "/sync/bootstrap");
  request->method = "PUT";
  request->headers.SetHeader("Authorization", "Bearer " + access_token);
  request->headers.SetHeader("Content-Type", "application/json");
  request->headers.SetHeader("Accept", "application/json");
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader = network::SimpleURLLoader::Create(
      std::move(request), kSyncBootstrapTrafficAnnotation);
  loader->AttachStringForUpload(body_json, "application/json");
  loader->SetTimeoutDuration(base::Seconds(30));
  loader->SetAllowHttpErrorResults(true);
  network::SharedURLLoaderFactory *factory_ptr = url_loader_factory.get();
  loader->DownloadToString(
      factory_ptr,
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> completed_loader,
             LoginResultCallback cb,
             std::optional<std::string> response_body) {
            const auto *info = completed_loader->ResponseInfo();
            const int status =
                info && info->headers ? info->headers->response_code() : 0;
            if (completed_loader->NetError() != net::OK || !response_body ||
                (status != 200 && status != 201)) {
              // Escrow publication is best-effort: sign-in must not fail
              // because the vault key could not be escrowed yet.
              LOG(WARNING) << "MahoSyncBootstrap: vault escrow PUT failed with "
                           << "http_status=" << status;
              std::move(cb).Run(true, std::string());
              return;
            }
            std::move(cb).Run(true, std::string());
          },
          std::move(loader), std::move(callback)),
      64 * 1024);
}

void PublishLocalVaultEscrow(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, std::string room_id, std::string seed,
    LoginResultCallback callback) {
  maho::PostCoreTask<std::string>(
      FROM_HERE, base::BindOnce(&ExportVaultEscrowOnCoreSequence),
      base::BindOnce(
          [](scoped_refptr<network::SharedURLLoaderFactory> factory,
             std::string token, std::string room_id, std::string seed,
             LoginResultCallback cb, std::string json) {
            std::optional<base::Value> parsed =
                base::JSONReader::Read(json, base::JSON_PARSE_RFC);
            const base::DictValue *dict =
                parsed && parsed->is_dict() ? &parsed->GetDict() : nullptr;
            const std::string *kdf_params =
                dict ? dict->FindString("kdfParams") : nullptr;
            const std::string *wrapped =
                dict ? dict->FindString("wrappedAccountKey") : nullptr;
            if (!kdf_params || !wrapped || kdf_params->empty() ||
                wrapped->empty()) {
              // No local escrow (legacy passphrase vault): nothing to publish.
              std::move(cb).Run(true, std::string());
              return;
            }
            PutVaultEscrow(std::move(factory), std::move(token),
                           std::move(room_id), std::move(seed), *kdf_params,
                           *wrapped, std::move(cb));
          },
          std::move(url_loader_factory), std::move(access_token),
          std::move(room_id), std::move(seed), std::move(callback)));
}

void ConfigureFetchedSyncBootstrap(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, SyncBootstrapMaterial bootstrap,
    LoginResultCallback callback) {
  const bool record_has_escrow = !bootstrap.vault_kdf_params.empty() &&
                                 !bootstrap.vault_wrapped_account_key.empty();
  const std::string room_id = bootstrap.room_id;
  const std::string seed = bootstrap.seed;
  maho::PostCoreTask<bool>(
      FROM_HERE,
      base::BindOnce(&ConfigureSyncBootstrapOnCoreSequence,
                     std::move(bootstrap)),
      base::BindOnce(
          [](scoped_refptr<network::SharedURLLoaderFactory> factory,
             std::string token, std::string room_id, std::string seed,
             bool record_has_escrow, LoginResultCallback cb, bool configured) {
            if (!configured) {
              std::move(cb).Run(false, "Unable to configure account Sync.");
              return;
            }
            if (record_has_escrow) {
              std::move(cb).Run(true, std::string());
              return;
            }
            PublishLocalVaultEscrow(std::move(factory), std::move(token),
                                    std::move(room_id), std::move(seed),
                                    std::move(cb));
          },
          std::move(url_loader_factory), std::move(access_token), room_id, seed,
          record_has_escrow, std::move(callback)));
}

void FetchAccountSyncBootstrap(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, bool allow_create, LoginResultCallback callback);

void PutGeneratedSyncBootstrap(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, SyncBootstrapMaterial generated,
    LoginResultCallback callback) {
  base::DictValue body;
  body.Set("version", generated.version);
  body.Set("room_id", generated.room_id);
  body.Set("seed", generated.seed);
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(GetSyncRelayUrl() + "/sync/bootstrap");
  request->method = "PUT";
  request->headers.SetHeader("Authorization", "Bearer " + access_token);
  request->headers.SetHeader("Content-Type", "application/json");
  request->headers.SetHeader("Accept", "application/json");
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader = network::SimpleURLLoader::Create(
      std::move(request), kSyncBootstrapTrafficAnnotation);
  loader->AttachStringForUpload(body_json, "application/json");
  loader->SetTimeoutDuration(base::Seconds(30));
  loader->SetAllowHttpErrorResults(true);
  auto *raw_loader = loader.get();
  // See FetchAccountSyncBootstrap: the factory pointer must be read before the
  // BindOnce below moves the refptr, or DownloadToString receives nullptr.
  network::SharedURLLoaderFactory *factory_ptr = url_loader_factory.get();
  raw_loader->DownloadToString(
      factory_ptr,
      base::BindOnce(
          [](scoped_refptr<network::SharedURLLoaderFactory> factory,
             std::string token, SyncBootstrapMaterial local_generated,
             LoginResultCallback cb,
             std::unique_ptr<network::SimpleURLLoader> completed_loader,
             std::optional<std::string> response_body) {
            const auto *info = completed_loader->ResponseInfo();
            const int status =
                info && info->headers ? info->headers->response_code() : 0;
            if (completed_loader->NetError() != net::OK) {
              std::move(cb).Run(false,
                                "Unable to create account Sync bootstrap.");
              return;
            }
            if (status == 409) {
              FetchAccountSyncBootstrap(std::move(factory), std::move(token),
                                        false, std::move(cb));
              return;
            }
            if ((status != 200 && status != 201) || !response_body) {
              std::move(cb).Run(false,
                                "Unable to create account Sync bootstrap.");
              return;
            }
            std::optional<SyncBootstrapMaterial> stored =
                ParseSyncBootstrap(*response_body);
            if (!stored || stored->room_id != local_generated.room_id ||
                stored->seed != local_generated.seed) {
              std::move(cb).Run(false,
                                "Invalid account Sync bootstrap response.");
              return;
            }
            ConfigureFetchedSyncBootstrap(std::move(factory), std::move(token),
                                          std::move(*stored), std::move(cb));
          },
          std::move(url_loader_factory), access_token, std::move(generated),
          std::move(callback), std::move(loader)),
      64 * 1024);
}

void FetchAccountSyncBootstrap(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, bool allow_create, LoginResultCallback callback) {
  GURL request_url(GetSyncRelayUrl() + "/sync/bootstrap");
  if (!request_url.is_valid()) {
    std::move(callback).Run(false, "Invalid Sync relay URL.");
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = request_url;
  request->method = "GET";
  request->headers.SetHeader("Authorization", "Bearer " + access_token);
  request->headers.SetHeader("Accept", "application/json");
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader = network::SimpleURLLoader::Create(
      std::move(request), kSyncBootstrapTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(30));
  loader->SetAllowHttpErrorResults(true);
  auto *raw_loader = loader.get();
  // Read the factory pointer BEFORE the BindOnce below moves the refptr away.
  // Argument evaluation order is unspecified, so passing `url_loader_factory
  // .get()` inline races the `std::move(url_loader_factory)` in the bound
  // arguments: right-to-left evaluation (clang-cl/x64) empties the refptr
  // first and hands DownloadToString a null factory, tripping
  // CHECK(url_loader_factory) in SimpleURLLoaderImpl::StartRequest.
  network::SharedURLLoaderFactory *factory_ptr = url_loader_factory.get();
  raw_loader->DownloadToString(
      factory_ptr,
      base::BindOnce(
          [](scoped_refptr<network::SharedURLLoaderFactory> factory,
             std::string token, bool can_create, LoginResultCallback cb,
             std::unique_ptr<network::SimpleURLLoader> completed_loader,
             std::optional<std::string> response_body) {
            const auto *info = completed_loader->ResponseInfo();
            const int status =
                info && info->headers ? info->headers->response_code() : 0;
            LOG(ERROR) << "MahoSyncBootstrap: GET result net_error="
                       << completed_loader->NetError() << " http_status="
                       << status << " can_create=" << can_create
                       << " body_bytes="
                       << (response_body ? response_body->size() : 0u);
            if (completed_loader->NetError() != net::OK) {
              std::move(cb).Run(false,
                                "Unable to retrieve account Sync bootstrap.");
              return;
            }
            if (status == 200 && response_body) {
              std::optional<SyncBootstrapMaterial> bootstrap =
                  ParseSyncBootstrap(*response_body);
              if (!bootstrap) {
                std::move(cb).Run(false,
                                  "Invalid account Sync bootstrap response.");
                return;
              }
              ConfigureFetchedSyncBootstrap(std::move(factory), std::move(token),
                                            std::move(*bootstrap),
                                            std::move(cb));
              return;
            }
            if (status != 404 || !can_create) {
              std::move(cb).Run(
                  false, status == 404
                             ? "Existing Recovery Phrase Sync requires "
                               "explicit migration."
                             : "Unable to retrieve account Sync bootstrap.");
              return;
            }
            maho::PostCoreTask<std::optional<SyncBootstrapMaterial>>(
                FROM_HERE, base::BindOnce(&GenerateSyncBootstrapOnCoreSequence),
                base::BindOnce(
                    [](scoped_refptr<network::SharedURLLoaderFactory>
                           inner_factory,
                       std::string inner_token, LoginResultCallback inner_cb,
                       std::optional<SyncBootstrapMaterial> generated) {
                      if (!generated) {
                        std::move(inner_cb).Run(
                            false,
                            "Unable to generate account Sync bootstrap.");
                        return;
                      }
                      PutGeneratedSyncBootstrap(
                          std::move(inner_factory), std::move(inner_token),
                          std::move(*generated), std::move(inner_cb));
                    },
                    std::move(factory), std::move(token), std::move(cb)));
          },
          std::move(url_loader_factory), access_token, allow_create,
          std::move(callback), std::move(loader)),
      64 * 1024);
}

void ConfigureAccountSyncAfterLogin(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string access_token, LoginResultCallback callback) {
  maho::PostCoreTask<std::string>(
      FROM_HERE, base::BindOnce(&GetSyncRoomIdOnCoreSequence),
      base::BindOnce(
          [](scoped_refptr<network::SharedURLLoaderFactory> factory,
             std::string token, LoginResultCallback cb,
             std::string legacy_room_id) {
            FetchAccountSyncBootstrap(std::move(factory), std::move(token),
                                      legacy_room_id.empty(), std::move(cb));
          },
          std::move(url_loader_factory), std::move(access_token),
          std::move(callback)));
}

void CompleteLogin(PrefService *prefs, StoreTokensParams params,
                   LoginResultCallback callback, bool bootstrap_ok,
                   const std::string &bootstrap_error) {
  if (!bootstrap_ok) {
    ClearRelayTokens(prefs);
    std::move(callback).Run(false, bootstrap_error);
    return;
  }

  base::DictValue sign_in_event;
  base::DictValue sign_in_data;
  sign_in_data.Set("email", params.user_email);
  sign_in_data.Set("password", "");
  sign_in_data.Set("display_name", params.user_display_name);
  sign_in_data.Set("access_token", params.access_token);
  sign_in_data.Set("user_id", params.user_id);
  sign_in_event.Set("SignIn", std::move(sign_in_data));
  std::string event_json;
  base::JSONWriter::Write(sign_in_event, &event_json);
  maho::PostCoreClosure(FROM_HERE, base::BindOnce(
                                       [](std::string json) {
                                         MahoCore *core = maho::GetCore();
                                         if (!core) {
                                           return;
                                         }
                                         char *result = maho_core_sign_in(
                                             core, json.c_str());
                                         if (result) {
                                           maho_string_free(result);
                                         }
                                       },
                                       std::move(event_json)));
  std::move(callback).Run(true, "");
}

void OnLoginResponse(
    PrefService *prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor &encryptor, LoginResultCallback callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  int net_error = loader->NetError();
  const auto *response_info = loader->ResponseInfo();
  int http_status = response_info ? response_info->headers->response_code() : 0;

  if (net_error != net::OK || !response_body.has_value()) {
    std::move(callback).Run(false, "Network error");
    return;
  }

  auto parsed = base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(false, "Invalid server response.");
    return;
  }

  const base::DictValue &dict = parsed->GetDict();

  if (http_status != 200) {
    const std::string *error_msg = dict.FindString("error");
    std::move(callback).Run(
        false, error_msg ? *error_msg
                         : "Authentication failed (HTTP " +
                               base::NumberToString(http_status) + ")");
    return;
  }

  const std::string *access_token = dict.FindString("access_token");
  const std::string *refresh_token = dict.FindString("refresh_token");
  if (!access_token || !refresh_token) {
    std::move(callback).Run(false, "Missing tokens in response.");
    return;
  }

  int64_t expires_at =
      static_cast<int64_t>(dict.FindDouble("expires_at").value_or(0));
  int64_t refresh_expires_at =
      static_cast<int64_t>(dict.FindDouble("refresh_expires_at").value_or(0));

  const base::DictValue *account = dict.FindDict("account");
  std::string user_email;
  std::string user_id;
  std::string user_display_name;
  std::string user_tier;
  std::string oauth_provider;
  std::string oauth_provider_sub;
  if (account) {
    if (const std::string *v = account->FindString("email"))
      user_email = *v;
    if (std::optional<int> v_int = account->FindInt("id"))
      user_id = base::NumberToString(*v_int);
    else if (const std::string *v_str = account->FindString("id"))
      user_id = *v_str;
    if (const std::string *v = account->FindString("display_name"))
      user_display_name = *v;
    if (const std::string *v = account->FindString("tier"))
      user_tier = *v;
    if (const std::string *v = account->FindString("oauth_provider"))
      oauth_provider = *v;
    if (const std::string *v = account->FindString("oauth_provider_sub"))
      oauth_provider_sub = *v;
  }

  StoreTokensParams params;
  params.access_token = *access_token;
  params.refresh_token = *refresh_token;
  params.access_expires_at = expires_at;
  params.refresh_expires_at = refresh_expires_at;
  params.user_email = user_email;
  params.user_id = user_id;
  params.user_display_name = user_display_name;
  params.user_tier = user_tier;
  params.oauth_provider = oauth_provider;
  params.oauth_provider_sub = oauth_provider_sub;

  if (!StoreRelayTokens(prefs, encryptor, params)) {
    std::move(callback).Run(false, "Failed to store credentials.");
    return;
  }

  // Copy the token out BEFORE the BindOnce below moves `params` away.
  // Argument evaluation order is unspecified, so reading `params.access_token`
  // inline races the `std::move(params)` in the bound arguments: right-to-left
  // evaluation (clang-cl/x64) empties `params` first and forwards an empty
  // token, which the relay rejects with 401 "invalid bearer token".
  std::string access_token_for_sync = params.access_token;
  ConfigureAccountSyncAfterLogin(
      std::move(url_loader_factory), std::move(access_token_for_sync),
      base::BindOnce(&CompleteLogin, prefs, std::move(params),
                     std::move(callback)));
}

void OnGoogleLoginResponse(
    base::RepeatingCallback<PrefService *()> prefs_provider,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor &encryptor, LoginResultCallback callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  PrefService *prefs = prefs_provider.Run();
  if (!prefs) {
    std::move(callback).Run(false, "Profile closed before sign-in completed.");
    return;
  }
  OnLoginResponse(prefs, std::move(url_loader_factory), encryptor,
                  std::move(callback), std::move(loader),
                  std::move(response_body));
}

} // namespace

namespace testing {

void FetchAccountSyncBootstrapForTesting(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const std::string& access_token,
    LoginResultCallback callback) {
  FetchAccountSyncBootstrap(std::move(url_loader_factory), access_token, true,
                            std::move(callback));
}

}  // namespace testing

std::string GetRelayAccessToken(PrefService *prefs,
                                const os_crypt_async::Encryptor &encryptor) {
  return DecryptB64(
      prefs->GetString(maho::account_prefs::kRelayAccessTokenEncryptedB64),
      encryptor);
}

std::string GetRelayRefreshToken(PrefService *prefs,
                                 const os_crypt_async::Encryptor &encryptor) {
  return DecryptB64(
      prefs->GetString(maho::account_prefs::kRelayRefreshTokenEncryptedB64),
      encryptor);
}

bool StoreRelayTokens(PrefService *prefs,
                      const os_crypt_async::Encryptor &encryptor,
                      const StoreTokensParams &params) {
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

  std::string oauth_provider;
  std::string oauth_provider_sub;
  base::TrimWhitespaceASCII(params.oauth_provider, base::TRIM_ALL,
                            &oauth_provider);
  base::TrimWhitespaceASCII(params.oauth_provider_sub, base::TRIM_ALL,
                            &oauth_provider_sub);
  if (!oauth_provider.empty() && !oauth_provider_sub.empty()) {
    prefs->SetString(maho::account_prefs::kRelayOAuthProvider, oauth_provider);
    prefs->SetString(maho::account_prefs::kRelayOAuthProviderSub,
                     oauth_provider_sub);
  } else {
    prefs->ClearPref(maho::account_prefs::kRelayOAuthProvider);
    prefs->ClearPref(maho::account_prefs::kRelayOAuthProviderSub);
  }
  return true;
}

void PreserveRelayIdentityMetadata(PrefService *prefs,
                                   StoreTokensParams *params) {
  params->oauth_provider =
      prefs->GetString(maho::account_prefs::kRelayOAuthProvider);
  params->oauth_provider_sub =
      prefs->GetString(maho::account_prefs::kRelayOAuthProviderSub);
}

StoreTokensParams BuildRefreshedRelayTokenParams(PrefService *prefs,
                                                const RefreshedTokens &tokens) {
  StoreTokensParams params;
  params.access_token = tokens.access_token;
  params.refresh_token = tokens.refresh_token;
  params.access_expires_at = tokens.access_expires_at;
  params.refresh_expires_at = tokens.refresh_expires_at;
  params.user_email = prefs->GetString(maho::account_prefs::kRelayUserEmail);
  params.user_id = prefs->GetString(maho::account_prefs::kRelayUserId);
  params.user_display_name =
      prefs->GetString(maho::account_prefs::kRelayUserDisplayName);
  params.user_tier = prefs->GetString(maho::account_prefs::kRelayUserTier);
  PreserveRelayIdentityMetadata(prefs, &params);
  return params;
}

bool StoreRefreshedRelayTokens(PrefService *prefs,
                               const os_crypt_async::Encryptor &encryptor,
                               const RefreshedTokens &tokens) {
  return StoreRelayTokens(prefs, encryptor,
                          BuildRefreshedRelayTokenParams(prefs, tokens));
}

void ClearRelayTokens(PrefService *prefs) {
  prefs->ClearPref(maho::account_prefs::kRelayAccessTokenEncryptedB64);
  prefs->ClearPref(maho::account_prefs::kRelayRefreshTokenEncryptedB64);
  prefs->ClearPref(maho::account_prefs::kRelayAccessTokenExpiresAt);
  prefs->ClearPref(maho::account_prefs::kRelayRefreshTokenExpiresAt);
  prefs->ClearPref(maho::account_prefs::kRelayUserEmail);
  prefs->ClearPref(maho::account_prefs::kRelayUserId);
  prefs->ClearPref(maho::account_prefs::kRelayUserDisplayName);
  prefs->ClearPref(maho::account_prefs::kRelayUserTier);
  prefs->ClearPref(maho::account_prefs::kRelayOAuthProvider);
  prefs->ClearPref(maho::account_prefs::kRelayOAuthProviderSub);
  prefs->ClearPref(maho::account_prefs::kRelaySubscriptionStatus);
  prefs->ClearPref(maho::account_prefs::kRelaySubscriptionExpiresAt);
}

std::string BuildMailOAuthStartOptionsJson(PrefService *prefs) {
  std::string provider;
  std::string provider_sub;
  std::string email;
  base::TrimWhitespaceASCII(
      prefs->GetString(maho::account_prefs::kRelayOAuthProvider),
      base::TRIM_ALL, &provider);
  base::TrimWhitespaceASCII(
      prefs->GetString(maho::account_prefs::kRelayOAuthProviderSub),
      base::TRIM_ALL, &provider_sub);
  base::TrimWhitespaceASCII(
      prefs->GetString(maho::account_prefs::kRelayUserEmail), base::TRIM_ALL,
      &email);
  if (provider != "google" || provider_sub.empty() || email.empty()) {
    return "{}";
  }

  base::DictValue options;
  options.Set("login_hint", email);
  options.Set("expected_google_sub", provider_sub);
  std::string options_json;
  base::JSONWriter::Write(options, &options_json);
  return options_json;
}

bool HasValidRelaySession(PrefService *prefs) {
  // Relay auth prefs are registered by the AI panel's prefs registrar, which
  // may not have run yet when PostProfileInit calls this at startup. Guard
  // with FindPreference so an unregistered pref reads as "no session"
  // instead of CHECK-crashing the browser.
  const PrefService::Preference *access_pref =
      prefs->FindPreference(maho::account_prefs::kRelayAccessTokenEncryptedB64);
  if (access_pref == nullptr || !access_pref->GetValue()->is_string()) {
    return false;
  }
  const std::string access_token_enc =
      access_pref->GetValue()->GetString();
  if (access_token_enc.empty()) {
    return false;
  }

  const PrefService::Preference *expires_pref =
      prefs->FindPreference(maho::account_prefs::kRelayAccessTokenExpiresAt);
  // base::Value stores ints as `int`; widen to int64_t for unix timestamps.
  const std::optional<int> access_expires_at_int =
      expires_pref ? expires_pref->GetValue()->GetIfInt()
                   : std::optional<int>();
  const std::optional<int64_t> access_expires_at =
      access_expires_at_int.has_value()
          ? std::optional<int64_t>(*access_expires_at_int)
          : std::optional<int64_t>();
  const int64_t now = base::Time::Now().ToTimeT();
  if (access_expires_at.has_value() && access_expires_at.value() > now) {
    return true;
  }

  const PrefService::Preference *refresh_pref =
      prefs->FindPreference(maho::account_prefs::kRelayRefreshTokenEncryptedB64);
  if (refresh_pref == nullptr || !refresh_pref->GetValue()->is_string()) {
    return false;
  }
  const std::string refresh_token_enc = refresh_pref->GetValue()->GetString();
  if (refresh_token_enc.empty()) {
    return false;
  }

  const int64_t refresh_expires_at =
      prefs->GetInt64(maho::account_prefs::kRelayRefreshTokenExpiresAt);
  return refresh_expires_at > now;
}

std::string GetRelayBaseUrl() {
  const char *env = std::getenv("MAHO_RELAY_URL");
  if (env && *env) {
    return env;
  }
  return kDefaultRelayUrl;
}

std::string GetSyncRelayUrl() {
  const char *env = std::getenv("MAHO_SYNC_RELAY_URL");
  if (env && *env) {
    return env;
  }
  return GetRelayBaseUrl();
}

void RefreshAccessToken(
    const std::string &current_refresh_token,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    RefreshCallback callback) {
  if (current_refresh_token.empty()) {
    std::move(callback).Run(false, 0, std::nullopt);
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

  auto *raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> loader,
             RefreshCallback cb, std::optional<std::string> response_body) {
            const int net_error = loader->NetError();
            const int http_status =
                (loader->ResponseInfo() && loader->ResponseInfo()->headers)
                    ? loader->ResponseInfo()->headers->response_code()
                    : 0;

            if (net_error != net::OK || http_status != 200 || !response_body) {
              std::move(cb).Run(false, http_status, std::nullopt);
              return;
            }

            auto parsed =
                base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict()) {
              std::move(cb).Run(false, http_status, std::nullopt);
              return;
            }

            const auto &dict = parsed->GetDict();
            const std::string *new_access = dict.FindString("access_token");
            const std::string *new_refresh = dict.FindString("refresh_token");
            if (!new_access || !new_refresh) {
              std::move(cb).Run(false, http_status, std::nullopt);
              return;
            }

            RefreshedTokens tokens;
            tokens.access_token = *new_access;
            tokens.refresh_token = *new_refresh;
            // FindDouble + cast keeps full int64 range for unix timestamps.
            tokens.access_expires_at =
                static_cast<int64_t>(dict.FindDouble("expires_at").value_or(0));
            tokens.refresh_expires_at = static_cast<int64_t>(
                dict.FindDouble("refresh_expires_at").value_or(0));

            std::move(cb).Run(true, http_status, std::move(tokens));
          },
          std::move(url_loader), std::move(callback)),
      /*max_body_size=*/16384);
}

void MahoRelayLogin(
    PrefService *prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor &encryptor, const std::string &email,
    const std::string &password, LoginResultCallback callback) {
  if (email.empty() || password.empty()) {
    std::move(callback).Run(false, "Email and password are required.");
    return;
  }

  base::DictValue body;
  body.Set("email", email);
  body.Set("password", password);

  // Stable per-profile device id: generate once and persist, so re-login reuses
  // this device's relay row and sync room instead of orphaning the snapshot.
  std::string device_id = prefs->GetString(maho::account_prefs::kRelayDeviceId);
  if (device_id.empty()) {
    device_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(maho::account_prefs::kRelayDeviceId, device_id);
  }
  body.Set("device_id", device_id);
#if BUILDFLAG(IS_MAC)
  body.Set("device_name", "Maho macOS");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_WIN)
  body.Set("device_name", "Maho Windows");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_LINUX)
  body.Set("device_name", "Maho Linux");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_IOS)
  body.Set("device_name", "Maho iOS");
  body.Set("device_type", "mobile");
#elif BUILDFLAG(IS_ANDROID)
  body.Set("device_name", "Maho Android");
  body.Set("device_type", "mobile");
#else
  body.Set("device_name", "Maho");
  body.Set("device_type", "unknown");
#endif
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  std::string relay_url = GetRelayBaseUrl();
  GURL request_url(relay_url + "/auth/login");
  if (!request_url.is_valid()) {
    std::move(callback).Run(false, "Invalid relay URL.");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kAuthLoginTrafficAnnotation);
  url_loader->AttachStringForUpload(body_json, "application/json");
  url_loader->SetTimeoutDuration(base::Seconds(30));
  // Deliver the response body for non-2xx (401/409/etc.) so OnLoginResponse
  // can surface the relay's real error instead of a bare "Network error".
  url_loader->SetAllowHttpErrorResults(true);

  auto *raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&OnLoginResponse, prefs, url_loader_factory,
                     std::cref(encryptor), std::move(callback),
                     std::move(url_loader)),
      /*max_body_size=*/64 * 1024);
}

void MahoRelaySignup(
    PrefService *prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor &encryptor, const std::string &email,
    const std::string &password, const std::string &display_name,
    SignupResultCallback callback) {
  if (email.empty() || password.empty()) {
    std::move(callback).Run(false, "Email and password are required.");
    return;
  }

  base::DictValue body;
  body.Set("email", email);
  body.Set("password", password);
  body.Set("display_name", display_name);

  // Stable per-profile device id (see MahoRelayLogin): generate once, persist,
  // so the account's first device reuses this row on later logins.
  std::string device_id = prefs->GetString(maho::account_prefs::kRelayDeviceId);
  if (device_id.empty()) {
    device_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(maho::account_prefs::kRelayDeviceId, device_id);
  }
  body.Set("device_id", device_id);
#if BUILDFLAG(IS_MAC)
  body.Set("device_name", "Maho macOS");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_WIN)
  body.Set("device_name", "Maho Windows");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_LINUX)
  body.Set("device_name", "Maho Linux");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_IOS)
  body.Set("device_name", "Maho iOS");
  body.Set("device_type", "mobile");
#elif BUILDFLAG(IS_ANDROID)
  body.Set("device_name", "Maho Android");
  body.Set("device_type", "mobile");
#else
  body.Set("device_name", "Maho");
  body.Set("device_type", "unknown");
#endif
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  std::string relay_url = GetRelayBaseUrl();
  GURL request_url(relay_url + "/auth/signup");
  if (!request_url.is_valid()) {
    std::move(callback).Run(false, "Invalid relay URL.");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kAuthLoginTrafficAnnotation);
  url_loader->AttachStringForUpload(body_json, "application/json");
  url_loader->SetTimeoutDuration(base::Seconds(30));
  // Deliver the response body for non-2xx (401/409/etc.) so OnLoginResponse
  // can surface the relay's real error instead of a bare "Network error".
  url_loader->SetAllowHttpErrorResults(true);

  auto *raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&OnLoginResponse, prefs, url_loader_factory,
                     std::cref(encryptor), std::move(callback),
                     std::move(url_loader)),
      /*max_body_size=*/64 * 1024);
}

void MahoRelayGoogleLogin(
    base::RepeatingCallback<PrefService *()> prefs_provider,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor &encryptor, const std::string &id_token,
    const std::string &nonce, LoginResultCallback callback) {
  if (id_token.empty() || nonce.empty()) {
    std::move(callback).Run(false, "Google sign-in did not complete.");
    return;
  }

  PrefService *prefs = prefs_provider.Run();
  if (!prefs) {
    std::move(callback).Run(false, "Profile closed before sign-in completed.");
    return;
  }

  base::DictValue body;
  body.Set("id_token", id_token);
  body.Set("nonce", nonce);
  SetDeviceFields(prefs, body);

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  GURL request_url(GetRelayBaseUrl() + "/auth/oauth/google");
  if (!request_url.is_valid()) {
    std::move(callback).Run(false, "Invalid relay URL.");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kAuthGoogleLoginTrafficAnnotation);
  url_loader->AttachStringForUpload(body_json, "application/json");
  url_loader->SetTimeoutDuration(base::Seconds(30));
  // Deliver the response body for non-2xx (401/409/etc.) so OnLoginResponse
  // can surface the relay's real error instead of a bare "Network error".
  url_loader->SetAllowHttpErrorResults(true);

  auto *raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&OnGoogleLoginResponse, std::move(prefs_provider),
                     url_loader_factory, std::cref(encryptor),
                     std::move(callback), std::move(url_loader)),
      /*max_body_size=*/64 * 1024);
}

} // namespace maho::auth
