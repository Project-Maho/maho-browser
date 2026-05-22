// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_

#include <optional>
#include <string>
#include <vector>

#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"

namespace maho_settings_password_helpers {

maho_settings::mojom::PasswordProviderKind ToPasswordProviderKind(
    const std::string& mode);

std::string NormalizePasswordProviderMode(const std::string& mode);

maho_settings::mojom::PasswordProviderCapabilitiesPtr
MakePasswordProviderCapabilities(
    maho_settings::mojom::PasswordProviderKind kind);

// Returns nullopt when the JSON payload is not a list, contains any non-dict
// element, or any entry is missing/empty for id, domain, username, or
// createdAt. Plaintext passwords are intentionally omitted from the output.
std::optional<std::vector<maho_settings::mojom::SavedPasswordPtr>>
ParseSavedPasswordsJson(const std::string& json);

std::vector<maho_settings::mojom::SavedPasswordPtr> GetSavedPasswordsFromCore();

bool UpdateAutofillSettingsInCore(bool passwords_enabled,
                                  const std::string& mode);

std::string ReadPasswordProviderModeFromCore();

bool ReadPasswordsEnabledFromCore();

}  // namespace maho_settings_password_helpers

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_
