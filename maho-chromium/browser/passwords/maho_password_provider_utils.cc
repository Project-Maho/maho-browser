// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_provider_utils.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/third_party/maho/maho_bridge.h"

namespace maho {
namespace passwords {

namespace {

constexpr char kNativeProviderMode[] = "maho_native";

std::string GetSelectedPasswordProviderMode() {
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

  const auto& dict = parsed->GetDict();
  const auto* autofill = dict.FindDict("autofill");
  if (!autofill) {
    return kNativeProviderMode;
  }

  const std::string* provider = autofill->FindString("passwordProvider");
  if (provider) {
    return *provider;
  }
  return kNativeProviderMode;
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

std::string GetActivePasswordProviderMode();
bool IsBitwardenExtensionAvailable();
bool IsOnePasswordExtensionAvailable();

std::string GetActivePasswordProviderMode() {
  std::string selected = GetSelectedPasswordProviderMode();
  std::string normalized = NormalizePasswordProviderMode(selected);

  if (normalized == kNativeProviderMode) {
    return kNativeProviderMode;
  }

  if (normalized == "bitwarden" && IsBitwardenExtensionAvailable()) {
    return "bitwarden";
  }
  if (normalized == "onepassword" && IsOnePasswordExtensionAvailable()) {
    return "onepassword";
  }

  return kNativeProviderMode;
}

bool IsNativePasswordProviderActive() {
  return GetActivePasswordProviderMode() == kNativeProviderMode;
}

bool ShouldEnableChromiumPasswordManager() {
  return IsNativePasswordProviderActive();
}

bool IsExtensionIdForProvider(const std::string& provider_id, const std::string& ext_id) {
  std::string registry_json = maho::core::GetPasswordProviderRegistry();
  std::optional<base::Value> parsed = base::JSONReader::Read(registry_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) continue;
    const auto& dict = item.GetDict();
    const std::string* id = dict.FindString("providerId");
    if (id && *id == provider_id) {
      const auto* ext_ids = dict.FindList("extensionIds");
      if (ext_ids) {
        for (const auto& val : *ext_ids) {
          if (val.is_string() && val.GetString() == ext_id) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool IsBitwardenExtensionAvailable() {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) return false;
  std::string ext_id_str(ext_id);
  maho_string_free(ext_id);
  return IsExtensionIdForProvider("bitwarden", ext_id_str);
}

bool IsOnePasswordExtensionAvailable() {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) return false;
  std::string ext_id_str(ext_id);
  maho_string_free(ext_id);
  return IsExtensionIdForProvider("onepassword", ext_id_str);
}

bool IsPasswordProviderAvailable(std::string_view mode) {
  std::string mode_str(mode);
  if (mode_str == "chromium" || mode_str == "builtin" || mode_str == "disabled" ||
      mode_str == "system" || mode_str == "maho_native") {
    return true;
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) return false;
  std::string ext_id_str(ext_id);
  maho_string_free(ext_id);
  return IsExtensionIdForProvider(mode_str, ext_id_str);
}

}  // namespace passwords
}  // namespace maho
