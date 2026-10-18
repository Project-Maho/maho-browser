// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/webauthn/maho_vault_passkey_authenticator.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64url.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "components/cbor/reader.h"
#include "components/cbor/values.h"
#include "crypto/hash.h"
#include "device/fido/attested_credential_data.h"
#include "device/fido/attestation_object.h"
#include "device/fido/authenticator_data.h"
#include "device/fido/authenticator_get_assertion_response.h"
#include "device/fido/authenticator_make_credential_response.h"
#include "device/fido/ctap_get_assertion_request.h"
#include "device/fido/ctap_make_credential_request.h"
#include "device/fido/opaque_attestation_statement.h"
#include "device/fido/public/fido_constants.h"
#include "device/fido/public/fido_transport_protocol.h"
#include "device/fido/public/public_key_credential_descriptor.h"
#include "device/fido/public/public_key_credential_params.h"
#include "device/fido/public/public_key_credential_user_entity.h"
#include "device/fido/public_key.h"
#include "maho/browser/maho_core_holder.h"

namespace maho {
namespace {

using JsonReply = std::optional<base::Value>;

const std::string* FindStringAtPath(const base::Value& value,
                                    std::string_view path) {
  const base::DictValue* dict = value.GetIfDict();
  return dict ? dict->FindStringByDottedPath(path) : nullptr;
}

bool IsOkResponse(const base::Value& response) {
  const base::DictValue* dict = response.GetIfDict();
  return dict && dict->FindBool("ok").value_or(false);
}

std::string EncodeBase64Url(base::span<const uint8_t> input) {
  std::string output;
  base::Base64UrlEncode(input, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &output);
  return output;
}
device::MakeCredentialStatus MapCreateError(const base::Value& response) {
  const std::string* error = FindStringAtPath(response, "error.code");
  if (error && *error == "uv_required") {
    return device::MakeCredentialStatus::kAuthenticatorMissingUserVerification;
  }
  if (error && *error == "locked") {
    return device::MakeCredentialStatus::kUserConsentDenied;
  }
  return device::MakeCredentialStatus::kAuthenticatorResponseInvalid;
}

device::GetAssertionStatus MapGetError(const base::Value& response) {
  const std::string* error = FindStringAtPath(response, "error.code");
  if (error && *error == "uv_required") {
    return device::GetAssertionStatus::kAuthenticatorMissingUserVerification;
  }
  if (error && (*error == "not_found" || *error == "rp_mismatch")) {
    return device::GetAssertionStatus::kUserConsentButCredentialNotRecognized;
  }
  if (error && *error == "locked") {
    return device::GetAssertionStatus::kUserConsentDenied;
  }
  return device::GetAssertionStatus::kAuthenticatorResponseInvalid;
}

std::optional<std::vector<uint8_t>> DecodeBase64Url(const base::Value& value,
                                                    const char* path) {
  const std::string* encoded = FindStringAtPath(value, path);
  if (!encoded) {
    return std::nullopt;
  }
  std::string decoded;
  if (!base::Base64UrlDecode(*encoded,
                             base::Base64UrlDecodePolicy::DISALLOW_PADDING,
                             &decoded)) {
    return std::nullopt;
  }
  return std::vector<uint8_t>(decoded.begin(), decoded.end());
}

}  // namespace

device::MakeCredentialStatus MapCreateErrorForTesting(
    const base::Value& response) {
  return MapCreateError(response);
}

device::GetAssertionStatus MapGetErrorForTesting(const base::Value& response) {
  return MapGetError(response);
}

std::optional<std::vector<uint8_t>> DecodeBase64UrlForTesting(
    const base::Value& response,
    const char* path) {
  return DecodeBase64Url(response, path);
}

VaultPasskeyAuthenticator::VaultPasskeyAuthenticator() {
  options_.is_platform_device =
      device::AuthenticatorSupportedOptions::PlatformDevice::kYes;
  options_.supports_user_presence = true;
  options_.supports_resident_key = true;
  options_.user_verification_availability =
      device::AuthenticatorSupportedOptions::UserVerificationAvailability::
          kNotSupported;
}

VaultPasskeyAuthenticator::~VaultPasskeyAuthenticator() = default;

void VaultPasskeyAuthenticator::InitializeAuthenticator(
    base::OnceClosure callback) {
  std::move(callback).Run();
}

void VaultPasskeyAuthenticator::MakeCredential(
    device::CtapMakeCredentialRequest request,
    device::MakeCredentialOptions options,
    MakeCredentialCallback callback) {
  base::DictValue payload;
  payload.Set("rpId", request.rp.id);
  payload.Set("userIdB64url", EncodeBase64Url(request.user.id));
  payload.Set("userName", request.user.name.value_or(""));
  payload.Set("displayName", request.user.display_name.value_or(""));
  const bool require_uv = request.user_verification ==
                              device::UserVerificationRequirement::kRequired ||
                          options.user_verification ==
                              device::UserVerificationRequirement::kRequired;
  payload.Set("requireUv", require_uv);
  payload.Set("uvPerformed", false);
  const std::string request_json = base::WriteJson(payload).value_or("{}");
  const auto weak = weak_factory_.GetWeakPtr();
  PostCoreTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](std::string request) -> JsonReply {
            MahoCore* core = maho::GetCore();
            if (!core) {
              return std::nullopt;
            }
            std::string response =
                maho::core::VaultPasskeyCreateJson(core, request.c_str());
            return base::JSONReader::Read(response, base::JSON_PARSE_RFC);
          },
          request_json),
      base::BindOnce(
          [](base::WeakPtr<VaultPasskeyAuthenticator> self,
             std::string rp_id, bool require_uv,
             MakeCredentialCallback callback, JsonReply response) {
            if (!self || !response || !IsOkResponse(*response)) {
              std::move(callback).Run(
                  response ? MapCreateError(*response)
                           : device::MakeCredentialStatus::
                                 kAuthenticatorResponseInvalid,
                  std::nullopt);
              return;
            }
            auto credential_id = DecodeBase64Url(*response, "data.credentialId");
            auto public_key = DecodeBase64Url(*response, "data.coseKey");
            if (!public_key) {
              public_key = DecodeBase64Url(*response, "data.publicKeyCose");
            }
            if (!credential_id || !public_key) {
              std::move(callback).Run(
                  device::MakeCredentialStatus::kAuthenticatorResponseInvalid,
                  std::nullopt);
              return;
            }
            auto rp_hash = crypto::hash::Sha256(rp_id);
            auto cose_key = cbor::Reader::Read(*public_key);
            if (!cose_key || !cose_key->is_map()) {
              std::move(callback).Run(
                  device::MakeCredentialStatus::kAuthenticatorResponseInvalid,
                  std::nullopt);
              return;
            }
            const auto algorithm_it = cose_key->GetMap().find(cbor::Value(3));
            if (algorithm_it == cose_key->GetMap().end() ||
                !algorithm_it->second.is_integer()) {
              std::move(callback).Run(
                  device::MakeCredentialStatus::kAuthenticatorResponseInvalid,
                  std::nullopt);
              return;
            }
            const int32_t algorithm = static_cast<int32_t>(
                algorithm_it->second.GetInteger());
            auto public_key_object = device::PublicKey::FromCOSEKey(
                algorithm, *public_key, cose_key->GetMap());
            if (!public_key_object) {
              std::move(callback).Run(
                  device::MakeCredentialStatus::kAuthenticatorResponseInvalid,
                  std::nullopt);
              return;
            }
            std::array<uint8_t, device::kAaguidLength> aaguid = {};
            device::AttestedCredentialData attested(
                aaguid, *credential_id, std::move(public_key_object));
            device::AuthenticatorData authenticator_data(
                rp_hash, true, require_uv, false, false, 0, std::move(attested),
                std::nullopt);
            auto statement = std::make_unique<device::OpaqueAttestationStatement>(
                "none", cbor::Value(cbor::Value::MapValue()));
            device::AuthenticatorMakeCredentialResponse result(
                device::FidoTransportProtocol::kInternal,
                device::AttestationObject(std::move(authenticator_data),
                                         std::move(statement)));
            result.is_resident_key = true;
            std::move(callback).Run(device::MakeCredentialStatus::kSuccess,
                                    std::move(result));
          },
          weak, request.rp.id, require_uv, std::move(callback)));
}

void VaultPasskeyAuthenticator::GetAssertion(
    device::CtapGetAssertionRequest request,
    device::CtapGetAssertionOptions options,
    GetAssertionCallback callback) {
  base::DictValue payload;
  payload.Set("rpId", request.rp_id);
  payload.Set("clientDataHash", EncodeBase64Url(request.client_data_hash));
  base::ListValue allow_ids;
  for (const auto& credential : request.allow_list) {
    allow_ids.Append(EncodeBase64Url(credential.id));
  }
  if (!allow_ids.empty()) {
    payload.Set("allowCredentialIds", std::move(allow_ids));
  }
  // CtapGetAssertionOptions carries no UV requirement; the request owns it.
  payload.Set("requireUv", request.user_verification ==
                               device::UserVerificationRequirement::kRequired);
  payload.Set("uvPerformed", false);
  const std::string request_json = base::WriteJson(payload).value_or("{}");
  const auto weak = weak_factory_.GetWeakPtr();
  PostCoreTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](std::string request) -> JsonReply {
            MahoCore* core = maho::GetCore();
            if (!core) {
              return std::nullopt;
            }
            std::string response = maho::core::VaultPasskeyGetAssertionJson(
                core, request.c_str());
            return base::JSONReader::Read(response, base::JSON_PARSE_RFC);
          },
          request_json),
      base::BindOnce(
          [](base::WeakPtr<VaultPasskeyAuthenticator> self,
             GetAssertionCallback callback, JsonReply response) {
            if (!self || !response || !IsOkResponse(*response)) {
              std::move(callback).Run(
                  response ? MapGetError(*response)
                           : device::GetAssertionStatus::
                                 kAuthenticatorResponseInvalid,
                  {});
              return;
            }
            auto credential_id = DecodeBase64Url(*response, "data.credentialId");
            auto authenticator_data = DecodeBase64Url(*response,
                                                       "data.authenticatorData");
            auto signature = DecodeBase64Url(*response, "data.signature");
            auto user_handle = DecodeBase64Url(*response, "data.userHandle");
            if (!credential_id || !authenticator_data || !signature ||
                !user_handle) {
              std::move(callback).Run(
                  device::GetAssertionStatus::kAuthenticatorResponseInvalid, {});
              return;
            }
            auto parsed = device::AuthenticatorData::DecodeAuthenticatorData(
                *authenticator_data);
            if (!parsed) {
              std::move(callback).Run(
                  device::GetAssertionStatus::kAuthenticatorResponseInvalid, {});
              return;
            }
            device::AuthenticatorGetAssertionResponse result(
                std::move(*parsed), std::move(*signature),
                device::FidoTransportProtocol::kInternal);
            result.credential.emplace(device::CredentialType::kPublicKey,
                                      std::move(*credential_id));
            result.user_entity = device::PublicKeyCredentialUserEntity(
                std::move(*user_handle), std::nullopt, std::nullopt);
            std::vector<device::AuthenticatorGetAssertionResponse> results;
            results.push_back(std::move(result));
            std::move(callback).Run(device::GetAssertionStatus::kSuccess,
                                    std::move(results));
          },
          weak, std::move(callback)));
}

void VaultPasskeyAuthenticator::GetPlatformCredentialInfoForRequest(
    const device::CtapGetAssertionRequest& request,
    const device::CtapGetAssertionOptions& options,
    GetPlatformCredentialInfoForRequestCallback callback) {
  std::move(callback).Run(
      /*credentials=*/{},
      device::FidoRequestHandlerBase::RecognizedCredential::
          kHasRecognizedCredential);
}

void VaultPasskeyAuthenticator::Cancel() { weak_factory_.InvalidateWeakPtrs(); }

device::AuthenticatorType VaultPasskeyAuthenticator::GetType() const {
  return device::AuthenticatorType::kEnclave;
}

std::string VaultPasskeyAuthenticator::GetId() const { return id_; }

const device::AuthenticatorSupportedOptions&
VaultPasskeyAuthenticator::Options() const {
  return options_;
}

std::optional<device::FidoTransportProtocol>
VaultPasskeyAuthenticator::AuthenticatorTransport() const {
  return device::FidoTransportProtocol::kInternal;
}

base::WeakPtr<device::FidoAuthenticator>
VaultPasskeyAuthenticator::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace maho
