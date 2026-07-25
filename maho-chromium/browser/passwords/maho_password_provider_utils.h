// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_PROVIDER_UTILS_H_
#define MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_PROVIDER_UTILS_H_

#include <string>
#include <string_view>

namespace maho::passwords {

std::string NormalizePasswordProviderMode(std::string_view mode);
std::string GetActivePasswordProviderMode();
bool IsNativePasswordProviderActive();
bool ShouldEnableChromiumPasswordManager();
bool IsBitwardenExtensionAvailable();
bool IsOnePasswordExtensionAvailable();
bool IsPasswordProviderAvailable(std::string_view mode);

bool IsChromiumAccountStoreDisabled();
bool IsGooglePasswordsSyncDisabled();

}  // namespace maho::passwords

#endif  // MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_PROVIDER_UTILS_H_
