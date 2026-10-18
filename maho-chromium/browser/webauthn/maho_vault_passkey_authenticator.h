// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_WEBAUTHN_MAHO_VAULT_PASSKEY_AUTHENTICATOR_H_
#define MAHO_BROWSER_WEBAUTHN_MAHO_VAULT_PASSKEY_AUTHENTICATOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "device/fido/fido_authenticator.h"
#include "maho/third_party/maho/maho_bridge.h"

namespace maho {

device::MakeCredentialStatus MapCreateErrorForTesting(
    const base::Value& response);
device::GetAssertionStatus MapGetErrorForTesting(
    const base::Value& response);
std::optional<std::vector<uint8_t>> DecodeBase64UrlForTesting(
    const base::Value& response,
    const char* path);

class VaultPasskeyAuthenticator final : public device::FidoAuthenticator {
 public:
  VaultPasskeyAuthenticator();
  ~VaultPasskeyAuthenticator() override;

  void InitializeAuthenticator(base::OnceClosure callback) override;
  void MakeCredential(device::CtapMakeCredentialRequest request,
                      device::MakeCredentialOptions options,
                      MakeCredentialCallback callback) override;
  void GetAssertion(device::CtapGetAssertionRequest request,
                    device::CtapGetAssertionOptions options,
                    GetAssertionCallback callback) override;
  void GetPlatformCredentialInfoForRequest(
      const device::CtapGetAssertionRequest& request,
      const device::CtapGetAssertionOptions& options,
      GetPlatformCredentialInfoForRequestCallback callback) override;
  void Cancel() override;
  device::AuthenticatorType GetType() const override;
  std::string GetId() const override;
  const device::AuthenticatorSupportedOptions& Options() const override;
  std::optional<device::FidoTransportProtocol> AuthenticatorTransport()
      const override;
  base::WeakPtr<device::FidoAuthenticator> GetWeakPtr() override;

 private:
  const std::string id_ = "maho-vault-passkey";
  device::AuthenticatorSupportedOptions options_;
  base::WeakPtrFactory<VaultPasskeyAuthenticator> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_WEBAUTHN_MAHO_VAULT_PASSKEY_AUTHENTICATOR_H_
