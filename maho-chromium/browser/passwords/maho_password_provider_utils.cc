// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_provider_utils.h"

#include <optional>
#include <string>
#include <string_view>

#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {
namespace passwords {

namespace {

constexpr char kNativeProviderMode[] = "maho_native";

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
  if (maho_core_is_native_password_provider_active(core)) {
    return kNativeProviderMode;
  }
  if (maho_core_is_bitwarden_extension_available(core)) {
    return "bitwarden";
  }
  if (maho_core_is_onepassword_extension_available(core)) {
    return "onepassword";
  }
  return kNativeProviderMode;
}

bool IsNativePasswordProviderActive() {
  MahoCore* core = maho::GetCore();
  return core ? maho_core_is_native_password_provider_active(core) : true;
}

bool ShouldEnableChromiumPasswordManager() {
  return IsNativePasswordProviderActive();
}

bool IsBitwardenExtensionAvailable() {
  MahoCore* core = maho::GetCore();
  return core ? maho_core_is_bitwarden_extension_available(core) : false;
}

bool IsOnePasswordExtensionAvailable() {
  MahoCore* core = maho::GetCore();
  return core ? maho_core_is_onepassword_extension_available(core) : false;
}

bool IsPasswordProviderAvailable(std::string_view mode) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }
  std::string mode_str(mode);
  return maho_core_is_password_provider_available(core, mode_str.c_str());
}

}  // namespace passwords
}  // namespace maho
