// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_provider_utils.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {
namespace passwords {

namespace {

constexpr char kNativeProviderMode[] = "maho_native";

std::optional<base::Value> ReadInstalledExtensionsJsonFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }

  char* json_str = maho_core_get_installed_extensions(core);
  if (!json_str) {
    return std::nullopt;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  return parsed;
}

bool ExtensionNameOrIdContains(const base::DictValue& extension,
                               std::string_view primary_match,
                               std::string_view secondary_match = {}) {
  const std::string* name = extension.FindString("name");
  const std::string* id = extension.FindString("id");
  std::string lowered_name = base::ToLowerASCII(name ? *name : std::string());
  std::string lowered_id = base::ToLowerASCII(id ? *id : std::string());
  return lowered_name.find(primary_match) != std::string::npos ||
         lowered_id.find(primary_match) != std::string::npos ||
         (!secondary_match.empty() &&
          (lowered_name.find(secondary_match) != std::string::npos ||
           lowered_id.find(secondary_match) != std::string::npos));
}

bool IsEnabledInstalledExtensionMatching(const base::DictValue& extension,
                                        std::string_view primary_match,
                                        std::string_view secondary_match = {}) {
  const bool enabled = extension.FindBool("enabled").value_or(true);
  if (!enabled) {
    return false;
  }

  return ExtensionNameOrIdContains(extension, primary_match, secondary_match);
}

bool HasMatchingInstalledExtension(std::string_view primary_match,
                                   std::string_view secondary_match = {}) {
  std::optional<base::Value> parsed = ReadInstalledExtensionsJsonFromCore();
  if (!parsed) {
    return false;
  }

  for (const base::Value& extension_value : parsed->GetList()) {
    const base::DictValue* extension = extension_value.GetIfDict();
    if (!extension) {
      continue;
    }
    if (IsEnabledInstalledExtensionMatching(*extension, primary_match,
                                            secondary_match)) {
      return true;
    }
  }

  return false;
}

bool IsNativePasswordProviderAvailableInternal() {
  return true;
}

bool IsBitwardenPasswordProviderAvailableInternal() {
  return HasMatchingInstalledExtension("bitwarden");
}

bool IsOnePasswordPasswordProviderAvailableInternal() {
  return HasMatchingInstalledExtension("1password", "onepassword");
}

}  // namespace

std::string NormalizePasswordProviderMode(std::string_view mode) {
  if (mode == "chromium" || mode == "builtin" || mode == "disabled" ||
      mode == "system") {
    return kNativeProviderMode;
  }
  if (mode == "bitwarden") {
    return "bitwarden";
  }
  if (mode == "onepassword") {
    return "onepassword";
  }
  return kNativeProviderMode;
}

std::string GetActivePasswordProviderMode() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return kNativeProviderMode;
  }

  char* json_str = maho_core_get_settings(core);
  if (!json_str) {
    return kNativeProviderMode;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return kNativeProviderMode;
  }

  const base::DictValue& root = parsed->GetDict();
  const base::DictValue* autofill = root.FindDict("autofill");
  if (!autofill) {
    return kNativeProviderMode;
  }

  const std::string* mode = autofill->FindString("passwordProvider");
  return NormalizePasswordProviderMode(mode ? *mode : kNativeProviderMode);
}

bool IsNativePasswordProviderActive() {
  return GetActivePasswordProviderMode() == kNativeProviderMode;
}

bool ShouldEnableChromiumPasswordManager() {
  return IsNativePasswordProviderActive();
}

bool IsBitwardenExtensionAvailable() {
  return IsBitwardenPasswordProviderAvailableInternal();
}

bool IsOnePasswordExtensionAvailable() {
  return IsOnePasswordPasswordProviderAvailableInternal();
}

bool IsPasswordProviderAvailable(std::string_view mode) {
  const std::string normalized_mode = NormalizePasswordProviderMode(mode);
  if (normalized_mode == kNativeProviderMode) {
    return IsNativePasswordProviderAvailableInternal();
  }
  if (normalized_mode == "bitwarden") {
    return IsBitwardenPasswordProviderAvailableInternal();
  }
  if (normalized_mode == "onepassword") {
    return IsOnePasswordPasswordProviderAvailableInternal();
  }
  return true;
}

}  // namespace passwords
}  // namespace maho
