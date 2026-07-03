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
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) {
    return kNativeProviderMode;
  }
  std::string provider = ext_id;
  maho_string_free(ext_id);

  if (provider == "nngceckbapebfimnlniiiahkandclblb") {
    return "bitwarden";
  }
  if (provider == "aeblfdkhhhdcdjpifhhbdiojplfjncoa") {
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
  if (!core) return false;
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) return false;
  bool res = (std::string(ext_id) == "nngceckbapebfimnlniiiahkandclblb");
  maho_string_free(ext_id);
  return res;
}

bool IsOnePasswordExtensionAvailable() {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) return false;
  bool res = (std::string(ext_id) == "aeblfdkhhhdcdjpifhhbdiojplfjncoa");
  maho_string_free(ext_id);
  return res;
}

bool IsPasswordProviderAvailable(std::string_view mode) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }
  std::string mode_str(mode);
  if (mode_str == "chromium" || mode_str == "builtin" || mode_str == "disabled" ||
      mode_str == "system" || mode_str == "maho_native") {
    return true;
  }
  char* ext_id = maho_core_get_password_provider_extension(core);
  if (!ext_id) return false;
  std::string provider = ext_id;
  maho_string_free(ext_id);

  if (mode_str == "bitwarden") {
    return provider == "nngceckbapebfimnlniiiahkandclblb";
  }
  if (mode_str == "onepassword") {
    return provider == "aeblfdkhhhdcdjpifhhbdiojplfjncoa";
  }
  return false;
}

}  // namespace passwords
}  // namespace maho
