// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_WEBAUTHN_MAHO_VAULT_PASSKEY_DISCOVERY_H_
#define MAHO_BROWSER_WEBAUTHN_MAHO_VAULT_PASSKEY_DISCOVERY_H_

#include <memory>
#include <vector>

#include "device/fido/fido_discovery_base.h"

namespace content {
class RenderFrameHost;
}

namespace maho {

std::vector<std::unique_ptr<device::FidoDiscoveryBase>>
CreateVaultPasskeyDiscoveries(content::RenderFrameHost* render_frame_host);

}  // namespace maho

#endif  // MAHO_BROWSER_WEBAUTHN_MAHO_VAULT_PASSKEY_DISCOVERY_H_
