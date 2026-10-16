// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/webauthn/maho_vault_passkey_discovery.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "device/fido/public/fido_transport_protocol.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/webauthn/maho_vault_passkey_authenticator.h"

namespace maho {
namespace {

class VaultPasskeyDiscovery final : public device::FidoDiscoveryBase {
 public:
  explicit VaultPasskeyDiscovery(MahoCore* core)
      : FidoDiscoveryBase(device::FidoTransportProtocol::kInternal),
        core_(core) {}

  void Start() override {
    auto weak = weak_factory_.GetWeakPtr();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&VaultPasskeyDiscovery::StartOnSequence,
                                  weak));
  }

  void Stop() override { authenticator_.reset(); }

 private:
  void StartOnSequence() {
    if (!observer()) {
      return;
    }
    std::vector<device::FidoAuthenticator*> authenticators;
    if (core_) {
      authenticator_ = std::make_unique<VaultPasskeyAuthenticator>();
      authenticators.push_back(authenticator_.get());
    }
    observer()->DiscoveryStarted(this, true, std::move(authenticators));
  }

  const raw_ptr<MahoCore> core_;
  std::unique_ptr<VaultPasskeyAuthenticator> authenticator_;
  base::WeakPtrFactory<VaultPasskeyDiscovery> weak_factory_{this};
};

}  // namespace

std::vector<std::unique_ptr<device::FidoDiscoveryBase>>
CreateVaultPasskeyDiscoveries(content::RenderFrameHost* render_frame_host) {
  std::vector<std::unique_ptr<device::FidoDiscoveryBase>> discoveries;
  if (!render_frame_host ||
      render_frame_host->GetBrowserContext()->IsOffTheRecord() ||
      !IsPasswordManagerAllowedForProfile(
          Profile::FromBrowserContext(render_frame_host->GetBrowserContext()))) {
    return discoveries;
  }
  discoveries.push_back(std::make_unique<VaultPasskeyDiscovery>(GetCore()));
  return discoveries;
}

}  // namespace maho
