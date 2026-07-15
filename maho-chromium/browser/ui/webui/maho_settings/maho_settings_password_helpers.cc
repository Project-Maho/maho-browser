// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho_settings_password_helpers {

maho_settings::mojom::PasswordProviderKind ToPasswordProviderKind(
    const std::string& mode) {
  if (mode == "bitwarden") {
    return maho_settings::mojom::PasswordProviderKind::kBitwarden;
  }
  if (mode == "onepassword") {
    return maho_settings::mojom::PasswordProviderKind::kOnePassword;
  }
  return maho_settings::mojom::PasswordProviderKind::kMahoNative;
}

std::string NormalizePasswordProviderMode(const std::string& mode) {
  return maho::passwords::NormalizePasswordProviderMode(mode);
}

maho_settings::mojom::PasswordProviderCapabilitiesPtr
MakePasswordProviderCapabilities(
    maho_settings::mojom::PasswordProviderKind kind) {
  auto capabilities = maho_settings::mojom::PasswordProviderCapabilities::New();
  const bool native =
      kind == maho_settings::mojom::PasswordProviderKind::kMahoNative;
  capabilities->can_list_saved_passwords = native;
  capabilities->can_search_saved_passwords = native;
  capabilities->can_delete_saved_passwords = native;
  capabilities->can_add_saved_passwords = native;
  capabilities->can_edit_saved_passwords = native;
  capabilities->can_reveal_passwords = native;
  return capabilities;
}

std::optional<std::vector<maho_settings::mojom::SavedPasswordPtr>>
ParseSavedPasswordsJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  std::vector<maho_settings::mojom::SavedPasswordPtr> passwords;
  for (const base::Value& value : parsed->GetList()) {
    if (!value.is_dict()) {
      // Any non-dict element is a malformed payload — fail the whole parse.
      return std::nullopt;
    }
    const auto& dict = value.GetDict();

    // All four required fields must be present and non-empty.
    const std::string* id = dict.FindString("id");
    const std::string* domain = dict.FindString("domain");
    const std::string* username = dict.FindString("username");
    const std::string* created_at = dict.FindString("createdAt");

    if (!id || id->empty() || !domain || domain->empty() ||
        !username || !created_at || created_at->empty()) {
      // Partial/missing required fields — reject entire payload.
      return std::nullopt;
    }

    auto password = maho_settings::mojom::SavedPassword::New();
    password->id = *id;
    password->domain = *domain;
    password->username = *username;
    password->created_at = *created_at;

    if (const std::string* last_used = dict.FindString("lastUsed")) {
      password->last_used = *last_used;
    }
    // Plaintext passwords are intentionally omitted from the output.
    passwords.push_back(std::move(password));
  }

  return passwords;
}

std::vector<maho_settings::mojom::SavedPasswordPtr> GetSavedPasswordsFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return {};
  }

  char* json_str = maho_core_search_passwords(core, "");
  if (!json_str) {
    return {};
  }

  std::string json(json_str);
  maho_string_free(json_str);
  auto result = ParseSavedPasswordsJson(json);
  if (!result) {
    return {};
  }
  return std::move(*result);
}

bool UpdateAutofillSettingsInCore(bool passwords_enabled,
                                  const std::string& mode) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }

  base::DictValue autofill;
  autofill.Set("passwordsEnabled", passwords_enabled);
  autofill.Set("passwordProvider", mode);

  base::DictValue settings;
  settings.Set("autofill", std::move(autofill));

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(settings)), &json);
  maho_core_update_settings(core, json.c_str());
  return true;
}

std::string ReadPasswordProviderModeFromCore() {
  return maho::passwords::GetActivePasswordProviderMode();
}

bool ReadPasswordsEnabledFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }

  char* json_str = maho_core_get_settings(core);
  if (!json_str) {
    return true;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return true;
  }

  const base::DictValue& root = parsed->GetDict();
  const base::DictValue* autofill = root.FindDict("autofill");
  if (!autofill) {
    return true;
  }

  const std::string* mode = autofill->FindString("passwordProvider");
  if (mode && *mode == "disabled") {
    return false;
  }

  std::optional<bool> passwords_enabled = autofill->FindBool("passwordsEnabled");
  if (passwords_enabled) {
    return *passwords_enabled;
  }

  return true;
}

}  // namespace maho_settings_password_helpers
