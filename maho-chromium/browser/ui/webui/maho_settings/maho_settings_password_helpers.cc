#include "base/strings/string_number_conversions.h"
#include "base/logging.h"
// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho_settings_password_helpers {

bool ConsumeSettingsAuthorization(
    const std::string& authorization_profile_key,
    maho::passwords::PasswordAuthorizationAction action);

namespace {

constexpr char kVaultOperationFailed[] = "Vault operation failed.";
constexpr char kPasswordImportOperationFailed[] =
    "Password import operation failed.";

std::string TakeVaultJson(char* value) {
  if (!value) {
    return {};
  }
  std::string json(value);
  maho_string_free(value);
  return json;
}

std::string TakeImportJson(char* value) {
  if (!value) {
    return {};
  }
  std::string json(value);
  maho_import_string_free(value);
  return json;
}

void SecureClearString(std::string* value) {
  maho::core::SecureClearString(value);
}

std::optional<maho_settings::mojom::PasswordProviderKind>
VaultProviderFromToken(const std::string& token) {
  if (token == "maho_native") {
    return maho_settings::mojom::PasswordProviderKind::kMahoNative;
  }
  if (token == "bitwarden") {
    return maho_settings::mojom::PasswordProviderKind::kBitwarden;
  }
  if (token == "onepassword") {
    return maho_settings::mojom::PasswordProviderKind::kOnePassword;
  }
  if (token == "disabled") {
    return maho_settings::mojom::PasswordProviderKind::kDisabled;
  }
  return std::nullopt;
}

maho_settings::mojom::VaultItemKind VaultItemKindFromToken(
    const std::string& token) {
  if (token == "login") {
    return maho_settings::mojom::VaultItemKind::kLogin;
  }
  if (token == "totp") {
    return maho_settings::mojom::VaultItemKind::kTotp;
  }
  if (token == "passkey") {
    return maho_settings::mojom::VaultItemKind::kPasskey;
  }
  if (token == "secure_item") {
    return maho_settings::mojom::VaultItemKind::kSecureItem;
  }
  return maho_settings::mojom::VaultItemKind::kUnknown;
}

std::string VaultProviderToken(
    maho_settings::mojom::PasswordProviderKind provider) {
  switch (provider) {
    case maho_settings::mojom::PasswordProviderKind::kBitwarden:
      return "bitwarden";
    case maho_settings::mojom::PasswordProviderKind::kOnePassword:
      return "onepassword";
    case maho_settings::mojom::PasswordProviderKind::kMahoNative:
      return "maho_native";
    case maho_settings::mojom::PasswordProviderKind::kDisabled:
      return "disabled";
  }
}

bool IsApprovedRawPasswordProvider(const std::string& provider) {
  return provider == "maho_native" || provider == "bitwarden" ||
         provider == "onepassword";
}

std::string VaultItemKindToken(maho_settings::mojom::VaultItemKind kind) {
  switch (kind) {
    case maho_settings::mojom::VaultItemKind::kLogin:
      return "login";
    case maho_settings::mojom::VaultItemKind::kTotp:
      return "totp";
    case maho_settings::mojom::VaultItemKind::kPasskey:
      return "passkey";
    case maho_settings::mojom::VaultItemKind::kSecureItem:
      return "secure_item";
    case maho_settings::mojom::VaultItemKind::kUnknown:
      return "unknown";
  }
}

std::optional<std::string> SecretActionToken(
    maho_settings::mojom::SecretAction action) {
  switch (action) {
    case maho_settings::mojom::SecretAction::kCopy:
      return "copy";
    case maho_settings::mojom::SecretAction::kReveal:
      return "copy";
  }
  return std::nullopt;
}

std::optional<maho_settings::mojom::VaultLockState> VaultLockStateFromToken(
    const std::string& token) {
  if (token == "uninitialized") {
    return maho_settings::mojom::VaultLockState::kUninitialized;
  }
  if (token == "locked") {
    return maho_settings::mojom::VaultLockState::kLocked;
  }
  if (token == "unlocked") {
    return maho_settings::mojom::VaultLockState::kUnlocked;
  }
  if (token == "auto_locked") {
    return maho_settings::mojom::VaultLockState::kAutoLocked;
  }
  return std::nullopt;
}

std::optional<maho_settings::mojom::VaultAgentPolicy> VaultPolicyFromToken(
    const std::string& token) {
  if (token == "deny") {
    return maho_settings::mojom::VaultAgentPolicy::kDeny;
  }
  if (token == "ask_every_use") {
    return maho_settings::mojom::VaultAgentPolicy::kAskEveryUse;
  }
  if (token == "allow_for_task") {
    return maho_settings::mojom::VaultAgentPolicy::kAllowForTask;
  }
  if (token == "while_unlocked") {
    return maho_settings::mojom::VaultAgentPolicy::kWhileUnlocked;
  }
  if (token == "always_allow") {
    return maho_settings::mojom::VaultAgentPolicy::kAlwaysAllow;
  }
  return std::nullopt;
}

std::string VaultPolicyToken(maho_settings::mojom::VaultAgentPolicy policy) {
  switch (policy) {
    case maho_settings::mojom::VaultAgentPolicy::kDeny:
      return "deny";
    case maho_settings::mojom::VaultAgentPolicy::kAskEveryUse:
      return "ask_every_use";
    case maho_settings::mojom::VaultAgentPolicy::kAllowForTask:
      return "allow_for_task";
    case maho_settings::mojom::VaultAgentPolicy::kWhileUnlocked:
      return "while_unlocked";
    case maho_settings::mojom::VaultAgentPolicy::kAlwaysAllow:
      return "always_allow";
  }
}

bool IsValidUint64(double value) {
  return std::isfinite(value) && value >= 0 && std::floor(value) == value &&
         value < static_cast<double>(std::numeric_limits<uint64_t>::max());
}

maho_settings::mojom::VaultOperationResultPtr VaultOperationFailure(
    const std::string& code) {
  auto result = maho_settings::mojom::VaultOperationResult::New();
  result->success = false;
  result->error_code = code;
  result->error_message = kVaultOperationFailed;
  return result;
}

maho_settings::mojom::VaultOperationResultPtr VaultOperationSuccess() {
  auto result = maho_settings::mojom::VaultOperationResult::New();
  result->success = true;
  return result;
}

maho_settings::mojom::PasswordImportOperationResultPtr
PasswordImportOperationFailure(const std::string& code) {
  auto result = maho_settings::mojom::PasswordImportOperationResult::New();
  result->success = false;
  result->error_code = code;
  result->error_message = kPasswordImportOperationFailed;
  result->committed = 0;
  result->failed = 0;
  result->terminal_result_count = 1;
  return result;
}

std::optional<maho_settings::mojom::PasswordImportSourceFormat>
PasswordImportSourceFormatFromToken(const std::string& token) {
  if (token == "one_password_csv") {
    return maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordCsv;
  }
  if (token == "one_password_1pux") {
    return maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordPux;
  }
  if (token == "bitwarden_individual_csv") {
    return maho_settings::mojom::PasswordImportSourceFormat::
        kBitwardenIndividualCsv;
  }
  if (token == "bitwarden_organization_csv") {
    return maho_settings::mojom::PasswordImportSourceFormat::
        kBitwardenOrganizationCsv;
  }
  if (token == "bitwarden_json") {
    return maho_settings::mojom::PasswordImportSourceFormat::kBitwardenJson;
  }
  if (token == "apple_passwords_csv") {
    return maho_settings::mojom::PasswordImportSourceFormat::kApplePasswordsCsv;
  }
  if (token == "keepassxc_csv") {
    return maho_settings::mojom::PasswordImportSourceFormat::kKeePassXcCsv;
  }
  if (token == "keepass_classic_csv") {
    return maho_settings::mojom::PasswordImportSourceFormat::kKeePassClassicCsv;
  }
  return std::nullopt;
}

std::string PasswordImportSourceFormatToken(
    maho_settings::mojom::PasswordImportSourceFormat source_format) {
  switch (source_format) {
    case maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordCsv:
      return "one_password_csv";
    case maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordPux:
      return "one_password_1pux";
    case maho_settings::mojom::PasswordImportSourceFormat::
        kBitwardenIndividualCsv:
      return "bitwarden_individual_csv";
    case maho_settings::mojom::PasswordImportSourceFormat::
        kBitwardenOrganizationCsv:
      return "bitwarden_organization_csv";
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenJson:
      return "bitwarden_json";
    case maho_settings::mojom::PasswordImportSourceFormat::kApplePasswordsCsv:
      return "apple_passwords_csv";
    case maho_settings::mojom::PasswordImportSourceFormat::kKeePassXcCsv:
      return "keepassxc_csv";
    case maho_settings::mojom::PasswordImportSourceFormat::kKeePassClassicCsv:
      return "keepass_classic_csv";
  }
  return "one_password_csv";
}

std::optional<uint32_t> ReadUint32(const base::DictValue& dict,
                                   std::string_view key) {
  std::optional<int> value = dict.FindInt(key);
  if (!value || *value < 0) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(*value);
}

std::optional<std::vector<std::string>> ReadStringList(
    const base::ListValue* list) {
  if (!list) {
    return std::nullopt;
  }
  std::vector<std::string> values;
  for (const base::Value& value : *list) {
    const std::string* text = value.GetIfString();
    if (!text) {
      return std::nullopt;
    }
    values.push_back(*text);
  }
  return values;
}

std::string PasswordImportErrorCode(const base::DictValue& root) {
  const base::DictValue* error = root.FindDict("error");
  const std::string* code = error ? error->FindString("code") : nullptr;
  return code && !code->empty() ? *code : "unknown_error";
}

maho_settings::mojom::PasswordImportOperationResultPtr
ParsePasswordImportOperationResultJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return PasswordImportOperationFailure("invalid_response");
  }
  const base::DictValue& root = parsed->GetDict();
  if (!root.FindBool("ok").value_or(false)) {
    return PasswordImportOperationFailure(PasswordImportErrorCode(root));
  }
  const base::DictValue* data = root.FindDict("data");
  if (!data) {
    return PasswordImportOperationFailure("invalid_response");
  }

  auto result = maho_settings::mojom::PasswordImportOperationResult::New();
  result->success = true;
  result->committed = ReadUint32(*data, "committed").value_or(0);
  result->failed = ReadUint32(*data, "failed").value_or(0);
  result->terminal_result_count =
      ReadUint32(*data, "terminalResultCount").value_or(1);

  const std::string* preview_token = data->FindString("previewToken");
  if (!preview_token) {
    return result;
  }
  const std::string* source_format = data->FindString("sourceFormat");
  const std::string* source_label = data->FindString("sourceLabel");
  auto source_format_enum =
      source_format ? PasswordImportSourceFormatFromToken(*source_format)
                    : std::nullopt;
  auto safe_messages = ReadStringList(data->FindList("safeMessages"));
  auto safe_errors = ReadStringList(data->FindList("safeErrors"));
  auto imported = ReadUint32(*data, "imported");
  auto skipped = ReadUint32(*data, "skipped");
  auto duplicates = ReadUint32(*data, "duplicates");
  auto blank_passwords = ReadUint32(*data, "blankPasswords");
  auto unsupported_fields = ReadUint32(*data, "unsupportedFields");
  if (!source_format_enum || !source_label || !safe_messages || !safe_errors ||
      !imported || !skipped || !duplicates || !blank_passwords ||
      !unsupported_fields) {
    return PasswordImportOperationFailure("invalid_response");
  }
  auto preview = maho_settings::mojom::PasswordImportPreview::New();
  preview->preview_token = *preview_token;
  preview->source_format = *source_format_enum;
  preview->source_label = *source_label;
  preview->imported = *imported;
  preview->skipped = *skipped;
  preview->duplicates = *duplicates;
  preview->blank_passwords = *blank_passwords;
  preview->unsupported_fields = *unsupported_fields;
  preview->safe_messages = std::move(*safe_messages);
  preview->safe_errors = std::move(*safe_errors);
  result->preview = std::move(preview);
  return result;
}

bool PerformSecretAction(maho_settings::mojom::SecretAction action,
                         char* raw_secret) {
  switch (action) {
    case maho_settings::mojom::SecretAction::kCopy: {
      std::u16string secret_text = base::UTF8ToUTF16(raw_secret);
      maho::passwords::MahoPasswordAuthorizationService::Get()
          ->CopySecretToClipboard(std::move(secret_text));
      return true;
    }
    case maho_settings::mojom::SecretAction::kReveal:
      return false;  // Only the browser-owned dialog callback may reveal.
  }
  return false;
}

maho_settings::mojom::VaultItemListResultPtr VaultItemListFailure(
    const std::string& code) {
  auto result = maho_settings::mojom::VaultItemListResult::New();
  result->success = false;
  result->error_code = code;
  result->error_message = kVaultOperationFailed;
  return result;
}

std::optional<maho_settings::mojom::VaultStatusPtr> ParseVaultStatus(
    const base::DictValue& dict) {
  const std::string* lock_state = dict.FindString("lockState");
  const std::string* selected_provider = dict.FindString("selectedProvider");
  const std::string* effective_provider = dict.FindString("effectiveProvider");
  const std::optional<double> item_count = dict.FindDouble("itemCount");
  const std::string* policy = dict.FindString("agentPolicyDefault");
  const std::optional<int> auto_lock_minutes = dict.FindInt("autoLockMinutes");
  const std::optional<int> failed_unlock_count =
      dict.FindInt("failedUnlockCount");
  const auto parsed_lock_state =
      lock_state ? VaultLockStateFromToken(*lock_state) : std::nullopt;
  const auto parsed_selected_provider =
      selected_provider ? VaultProviderFromToken(*selected_provider)
                        : std::nullopt;
  const auto parsed_effective_provider =
      effective_provider ? VaultProviderFromToken(*effective_provider)
                         : std::nullopt;
  const auto parsed_policy =
      policy ? VaultPolicyFromToken(*policy) : std::nullopt;
  if (!lock_state || !selected_provider || !effective_provider || !item_count ||
      !IsValidUint64(*item_count) || !policy || !auto_lock_minutes ||
      *auto_lock_minutes < 0 || !failed_unlock_count ||
      *failed_unlock_count < 0 || !parsed_lock_state ||
      !parsed_selected_provider || !parsed_effective_provider ||
      !parsed_policy) {
    return std::nullopt;
  }

  auto status = maho_settings::mojom::VaultStatus::New();
  status->lock_state = *parsed_lock_state;
  status->selected_provider = *parsed_selected_provider;
  status->effective_provider = *parsed_effective_provider;
  status->item_count = static_cast<uint64_t>(*item_count);
  status->agent_policy_default = *parsed_policy;
  status->auto_lock_minutes = static_cast<uint32_t>(*auto_lock_minutes);
  status->failed_unlock_count = static_cast<uint32_t>(*failed_unlock_count);
  const base::Value* retry_at_value = dict.Find("retryAt");
  if (retry_at_value && !retry_at_value->is_none()) {
    const std::string* retry_at = retry_at_value->GetIfString();
    if (!retry_at) {
      return std::nullopt;
    }
    base::Time retry_time;
    if (!base::Time::FromString(retry_at->c_str(), &retry_time)) {
      return std::nullopt;
    }
    status->retry_at_timestamp = retry_time.InMillisecondsSinceUnixEpoch();
  }
  return status;
}

bool ContainsSecretMaterial(const base::DictValue& dict) {
  constexpr std::string_view kForbiddenFields[] = {
      "password",   "username", "secret", "encryptedPayload", "envelope",
      "ciphertext", "nonce",    "tag",    "privateKey",       "seed",
      "notes", "totpSeed"};
  for (std::string_view field : kForbiddenFields) {
    if (dict.Find(field)) {
      return true;
    }
  }
  const base::DictValue* totp = dict.FindDict("totp");
  return totp &&
         (totp->Find("secret") || totp->Find("seed") || totp->Find("code"));
}

std::optional<std::string> CredentialOriginForDomain(const std::string& domain) {
  if (domain.empty()) {
    return std::nullopt;
  }
  if (base::StartsWith(domain, "http://") ||
      base::StartsWith(domain, "https://")) {
    return domain;
  }
  return "https://" + domain;
}

bool StringContainsQuery(const std::string& value,
                         const std::string& query_lower) {
  return base::ToLowerASCII(value).find(query_lower) != std::string::npos;
}

bool VaultItemMatchesQuery(const maho_settings::mojom::VaultItem& item,
                           const std::string& query_lower) {
  if (query_lower.empty()) {
    return true;
  }
  if (StringContainsQuery(item.title, query_lower) ||
      StringContainsQuery(item.username_hint, query_lower)) {
    return true;
  }
  return std::any_of(item.origins.begin(), item.origins.end(),
                     [&](const std::string& origin) {
                       return StringContainsQuery(origin, query_lower);
                     });
}

maho_settings::mojom::SavedPasswordPtr SavedPasswordFromLibraryItem(
    const maho_settings::mojom::VaultItem& item) {
  if (item.id.empty() || item.item_kind !=
                             maho_settings::mojom::VaultItemKind::kLogin) {
    return nullptr;
  }
  const std::string domain =
      item.origins.empty() ? item.title : item.origins.front();
  if (domain.empty()) {
    return nullptr;
  }
  auto password = maho_settings::mojom::SavedPassword::New();
  password->id = item.id;
  password->domain = domain;
  password->username = item.username_hint;
  password->created_at = item.created_at;
  password->last_used = item.last_used_at;
  return password;
}

std::vector<maho_settings::mojom::SavedPasswordPtr> SavedPasswordsFromLibraryItems(
    maho_settings::mojom::VaultItemListResultPtr result) {
  if (!result || !result->success) {
    return {};
  }
  std::vector<maho_settings::mojom::SavedPasswordPtr> passwords;
  for (const auto& item : result->items) {
    if (!item) {
      continue;
    }
    auto password = SavedPasswordFromLibraryItem(*item);
    if (password) {
      passwords.push_back(std::move(password));
    }
  }
  return passwords;
}

maho_settings::mojom::VaultItemPtr FindSavedPasswordLibraryItemById(
    const std::string& item_id) {
  auto listed = ListSavedPasswordLibraryItemsFromCore(std::nullopt,
                                                      std::nullopt, 0);
  if (!listed->success) {
    return nullptr;
  }
  for (auto& item : listed->items) {
    if (item && item->id == item_id) {
      return std::move(item);
    }
  }
  return nullptr;
}

std::optional<maho_settings::mojom::VaultItemPtr> ParseVaultItem(
    const base::DictValue& dict) {
  if (ContainsSecretMaterial(dict)) {
    return std::nullopt;
  }
  const std::string* id = dict.FindString("id");
  uint64_t revision = 0;
  const std::string* revision_text = dict.FindString("revision");
  const auto revision_number = dict.FindDouble("revision");
  const bool valid_revision = revision_text
      ? base::StringToUint64(*revision_text, &revision)
      : revision_number && IsValidUint64(*revision_number);
  if (!revision_text && valid_revision) {
    revision = static_cast<uint64_t>(*revision_number);
  }
  const std::string* provider = dict.FindString("provider");
  const std::string* item_kind = dict.FindString("itemKind");
  const std::string* title = dict.FindString("title");
  const base::ListValue* origins = dict.FindList("origins");
  const std::string* username_hint = dict.FindString("usernameHint");
  const std::string* created_at = dict.FindString("createdAt");
  const std::string* updated_at = dict.FindString("updatedAt");
  const auto parsed_provider =
      provider ? VaultProviderFromToken(*provider) : std::nullopt;
  if (!id || id->empty() || !valid_revision ||
      !parsed_provider || !item_kind || !title || !origins || !username_hint ||
      !created_at || !updated_at) {
    return std::nullopt;
  }

  auto item = maho_settings::mojom::VaultItem::New();
  item->id = *id;
  item->revision = revision;
  item->provider = *parsed_provider;
  item->item_kind = VaultItemKindFromToken(*item_kind);
  item->title = *title;
  item->username_hint = *username_hint;
  item->created_at = *created_at;
  item->updated_at = *updated_at;
  for (const base::Value& origin : *origins) {
    const std::string* value = origin.GetIfString();
    if (!value) {
      return std::nullopt;
    }
    item->origins.push_back(*value);
  }
  if (const std::string* last_used_at = dict.FindString("lastUsedAt")) {
    item->last_used_at = *last_used_at;
  }
  item->has_totp = dict.Find("totp") && !dict.Find("totp")->is_none();
  item->has_passkey = dict.Find("passkey") && !dict.Find("passkey")->is_none();
  item->favorite = dict.FindBool("favorite").value_or(false);
  item->has_notes = dict.FindBool("hasNotes").value_or(false);
  if (const auto* trashed_at = dict.FindString("trashedAt")) {
    item->trashed_at = *trashed_at;
  }
  return item;
}

std::string VaultErrorCode(const base::DictValue& root) {
  const base::DictValue* error = root.FindDict("error");
  const std::string* code = error ? error->FindString("code") : nullptr;
  return code ? *code : "invalid_response";
}

std::string VaultErrorMessage(const base::DictValue& root) {
  const base::DictValue* error = root.FindDict("error");
  const std::string* message = error ? error->FindString("message") : nullptr;
  return message ? *message : kVaultOperationFailed;
}

maho_settings::mojom::VaultPolicyStatusPtr VaultPolicyUnavailable(
    const std::string& reason) {
  auto status = maho_settings::mojom::VaultPolicyStatus::New();
  status->is_available = false;
  status->policy = maho_settings::mojom::VaultAgentPolicy::kDeny;
  status->unavailable_reason = reason;
  return status;
}

maho_settings::mojom::VaultAuditPagePtr VaultAuditPageUnavailable(
    const std::string& code,
    const std::string& reason) {
  auto page = maho_settings::mojom::VaultAuditPage::New();
  page->is_available = false;
  page->error_code = code;
  page->unavailable_reason = reason;
  return page;
}

maho_settings::mojom::VaultPolicyStatusPtr ParseVaultPolicyStatusJson(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return VaultPolicyUnavailable("invalid_response");
  }
  const base::DictValue& root = parsed->GetDict();
  if (!root.FindBool("ok").value_or(false)) {
    return VaultPolicyUnavailable(VaultErrorMessage(root));
  }
  const base::DictValue* data = root.FindDict("data");
  if (!data) {
    return VaultPolicyUnavailable("invalid_response");
  }
  const std::string* policy = data->FindString("policy");
  const auto parsed_policy = policy ? VaultPolicyFromToken(*policy) : std::nullopt;
  if (!policy || !parsed_policy) {
    return VaultPolicyUnavailable("invalid_response");
  }

  auto status = maho_settings::mojom::VaultPolicyStatus::New();
  status->is_available = true;
  status->policy = *parsed_policy;
  if (const std::string* item_id = data->FindString("itemId")) {
    status->item_id = *item_id;
  }
  if (const std::string* origin = data->FindString("origin")) {
    status->origin = *origin;
  }
  if (const std::string* expires_at = data->FindString("expiresAt")) {
    status->expires_at = *expires_at;
  }
  return status;
}

std::optional<maho_settings::mojom::VaultAuditEntryPtr> ParseVaultAuditEntry(
    const base::DictValue& dict) {
  const base::Value* item_alias = dict.Find("itemAlias");
  if (item_alias && !item_alias->is_none()) {
    return std::nullopt;
  }
  const std::string* timestamp = dict.FindString("timestamp");
  const std::string* operation = dict.FindString("operation");
  const std::string* decision = dict.FindString("decision");
  const std::string* device_name = dict.FindString("deviceName");
  if (!timestamp || !operation || !decision || !device_name) {
    return std::nullopt;
  }
  auto entry = maho_settings::mojom::VaultAuditEntry::New();
  entry->timestamp = *timestamp;
  entry->operation = *operation;
  entry->decision = *decision;
  entry->device_name = *device_name;
  if (const std::string* task_id = dict.FindString("taskId")) {
    entry->task_id = *task_id;
  }
  if (const std::string* origin = dict.FindString("origin")) {
    entry->origin = *origin;
  }
  if (const std::string* policy = dict.FindString("policy")) {
    auto parsed_policy = VaultPolicyFromToken(*policy);
    if (!parsed_policy) {
      return std::nullopt;
    }
    entry->policy = *parsed_policy;
  }
  if (const std::string* reason = dict.FindString("reason")) {
    entry->reason = *reason;
  }
  return entry;
}

maho_settings::mojom::VaultAuditPagePtr ParseVaultAuditPageJson(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return VaultAuditPageUnavailable("invalid_response", "invalid_response");
  }
  const base::DictValue& root = parsed->GetDict();
  if (!root.FindBool("ok").value_or(false)) {
    const std::string message = VaultErrorMessage(root);
    const std::string code = message.find("invalid audit cursor") != std::string::npos
                                 ? "invalid_cursor"
                                 : VaultErrorCode(root);
    return VaultAuditPageUnavailable(code, message);
  }
  const base::DictValue* data = root.FindDict("data");
  const base::ListValue* entries = data ? data->FindList("entries") : nullptr;
  if (!data || !entries) {
    return VaultAuditPageUnavailable("invalid_response", "invalid_response");
  }
  auto page = maho_settings::mojom::VaultAuditPage::New();
  page->is_available = true;
  for (const base::Value& value : *entries) {
    const base::DictValue* entry_dict = value.GetIfDict();
    if (!entry_dict) {
      return VaultAuditPageUnavailable("invalid_response", "invalid_response");
    }
    auto entry = ParseVaultAuditEntry(*entry_dict);
    if (!entry) {
      return VaultAuditPageUnavailable("invalid_response", "invalid_response");
    }
    page->entries.push_back(std::move(*entry));
  }
  if (const std::string* next_cursor = data->FindString("nextCursor")) {
    page->next_cursor = *next_cursor;
  }
  return page;
}

base::ListValue VaultItemKindsJson(
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds) {
  base::ListValue values;
  for (maho_settings::mojom::VaultItemKind kind : kinds) {
    values.Append(VaultItemKindToken(kind));
  }
  return values;
}

base::DictValue VaultMetadataJson(const std::string& title,
                                  const std::vector<std::string>& origins) {
  base::DictValue metadata;
  metadata.Set("title", title);
  base::ListValue origin_values;
  for (const std::string& origin : origins) {
    origin_values.Append(origin);
  }
  metadata.Set("origins", std::move(origin_values));
  metadata.Set("usernameHint", "");
  metadata.Set("itemKind", "login");
  metadata.Set("totp", base::Value());
  metadata.Set("passkey", base::Value());
  return metadata;
}

std::string WriteVaultRequest(base::DictValue request) {
  std::string json;
  base::JSONWriter::Write(request, &json);
  for (const char* key : {"password", "notes", "secret", "masterPassphrase",
                          "recoverySecret"}) {
    if (auto* value = request.FindString(key)) {
      SecureClearString(value);
    }
  }
  return json;
}

class VaultJsonResponse {
 public:
  explicit VaultJsonResponse(std::string json) {
    value_ = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    SecureClearString(&json);
  }
  ~VaultJsonResponse() {
    if (value_) {
      Clear(*value_);
    }
  }
  const base::DictValue* data() const {
    const auto* root = value_ ? value_->GetIfDict() : nullptr;
    return root && root->FindBool("ok").value_or(false)
        ? root->FindDict("data") : nullptr;
  }
  std::string error() const {
    const auto* root = value_ ? value_->GetIfDict() : nullptr;
    return root ? VaultErrorCode(*root) : "invalid_response";
  }
 private:
  static void Clear(base::Value& value) {
    if (value.is_string()) {
      SecureClearString(&value.GetString());
    } else if (value.is_dict()) {
      for (auto entry : value.GetDict()) {
        Clear(entry.second);
      }
    } else if (value.is_list()) {
      for (auto& entry : value.GetList()) {
        Clear(entry);
      }
    }
  }
  std::optional<base::Value> value_;
};

base::DictValue ItemRevisionRequest(const std::string& id, uint64_t revision) {
  return base::DictValue().Set("id", id)
      .Set("expectedRevision", base::NumberToString(revision));
}

maho_settings::mojom::VaultOperationResultPtr RunVaultMutation(
    const std::string& profile_key,
    maho::passwords::PasswordAuthorizationAction action,
    base::DictValue request,
    std::string (*operation)(MahoCore*, const char*)) {
  std::string json = WriteVaultRequest(std::move(request));
  MahoCore* core = maho::GetCore();
  if (!core || !ConsumeSettingsAuthorization(profile_key, action)) {
    SecureClearString(&json);
    return VaultOperationFailure(core ? "reauth_required" : "core_unavailable");
  }
  auto response = operation(core, json.c_str());
  SecureClearString(&json);
  return ParseVaultOperationResultJson(response);
}

void SetOptionalProvider(
    base::DictValue& request,
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider) {
  request.Set("provider", provider ? base::Value(VaultProviderToken(*provider))
                                   : base::Value());
}

}  // namespace

maho_settings::mojom::PasswordProviderKind ToPasswordProviderKind(
    const std::string& mode) {
  if (mode == "bitwarden") {
    return maho_settings::mojom::PasswordProviderKind::kBitwarden;
  }
  if (mode == "onepassword") {
    return maho_settings::mojom::PasswordProviderKind::kOnePassword;
  }
  if (mode == "disabled") {
    return maho_settings::mojom::PasswordProviderKind::kDisabled;
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
  return capabilities;
}

std::vector<maho_settings::mojom::PasswordProviderOptionPtr>
BuildPasswordProviderOptions() {
  std::vector<maho_settings::mojom::PasswordProviderOptionPtr> options;
  const std::string registry_json = maho::core::GetPasswordProviderRegistry();
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(registry_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return options;
  }

  for (const base::Value& item : parsed->GetList()) {
    const base::DictValue* provider = item.GetIfDict();
    if (!provider) {
      continue;
    }
    const std::string* provider_id = provider->FindString("providerId");
    if (!provider_id) {
      continue;
    }
    const std::string* display_name = provider->FindString("displayName");
    const std::string* description = provider->FindString("description");

    auto option = maho_settings::mojom::PasswordProviderOption::New();
    option->provider = ToPasswordProviderKind(*provider_id);
    if (display_name) {
      option->display_name = *display_name;
    }
    if (description) {
      option->description = *description;
    }
    if (*provider_id == "maho_native") {
      option->is_available = true;
    } else if (*provider_id == "bitwarden") {
      option->is_available = maho::passwords::IsBitwardenExtensionAvailable();
    } else if (*provider_id == "onepassword") {
      option->is_available = maho::passwords::IsOnePasswordExtensionAvailable();
    }
    options.push_back(std::move(option));
  }
  return options;
}

maho_settings::mojom::PasswordProviderStatusPtr
BuildPasswordProviderStatus() {
  const bool passwords_enabled = ReadPasswordsEnabledFromCore();
  const std::string mode = ReadSelectedPasswordProviderModeFromCore();
  auto status = maho_settings::mojom::PasswordProviderStatus::New();
  status->provider = ToPasswordProviderKind(mode);
  status->is_enabled = passwords_enabled;
  status->is_available = maho::passwords::IsPasswordProviderAvailable(mode);
  status->capabilities = MakePasswordProviderCapabilities(status->provider);

  for (auto& option : BuildPasswordProviderOptions()) {
    if (option->provider != status->provider) {
      continue;
    }
    if (!option->display_name.empty()) {
      status->display_name = option->display_name;
    }
    if (!option->description.empty()) {
      status->description =
          status->is_available
              ? option->description
              : option->display_name + " is selected, but the " +
                    option->display_name +
                    " extension is not installed or enabled.";
    }
    break;
  }
  return status;
}

bool SetPasswordProviderInCore(
    maho_settings::mojom::PasswordProviderKind provider) {
  return UpdateAutofillSettingsInCore(ReadPasswordsEnabledFromCore(),
                                      VaultProviderToken(provider));
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

    if (!id || id->empty() || !domain || domain->empty() || !username ||
        !created_at || created_at->empty()) {
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

std::vector<maho_settings::mojom::SavedPasswordPtr>
GetSavedPasswordsFromCore() {
  return SavedPasswordsFromLibraryItems(
      ListSavedPasswordLibraryItemsFromCore(std::nullopt, std::nullopt, 0));
}

std::vector<maho_settings::mojom::SavedPasswordPtr> SearchSavedPasswordsFromCore(
    const std::string& query) {
  return SavedPasswordsFromLibraryItems(
      SearchSavedPasswordLibraryItemsFromCore(query, std::nullopt));
}

maho_settings::mojom::VaultOperationResultPtr ParseVaultOperationResultJson(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return VaultOperationFailure("invalid_response");
  }
  const base::DictValue& root = parsed->GetDict();
  if (!root.FindBool("ok").value_or(false)) {
    return VaultOperationFailure(VaultErrorCode(root));
  }
  const base::DictValue* data = root.FindDict("data");
  if (!data) {
    return VaultOperationFailure("invalid_response");
  }
  if (ContainsSecretMaterial(*data)) {
    return VaultOperationFailure("invalid_response");
  }

  auto result = maho_settings::mojom::VaultOperationResult::New();
  result->success = true;
  if (data->Find("lockState")) {
    auto status = ParseVaultStatus(*data);
    if (!status) {
      return VaultOperationFailure("invalid_response");
    }
    result->status = std::move(*status);
  } else if (data->Find("id")) {
    if (data->Find("title") || data->Find("itemKind")) {
      auto item = ParseVaultItem(*data);
      if (!item) {
        return VaultOperationFailure("invalid_response");
      }
      result->item = std::move(*item);
    } else {
      const std::string* id = data->FindString("id");
      if (!id || id->empty()) {
        return VaultOperationFailure("invalid_response");
      }
      uint64_t rev = 0;
      if (const std::string* rev_str = data->FindString("revision")) {
        if (!base::StringToUint64(*rev_str, &rev)) {
          return VaultOperationFailure("invalid_response");
        }
      } else if (std::optional<double> rev_num = data->FindDouble("revision")) {
        if (!IsValidUint64(*rev_num)) {
          return VaultOperationFailure("invalid_response");
        }
        rev = static_cast<uint64_t>(*rev_num);
      } else {
        return VaultOperationFailure("invalid_response");
      }
      auto item = maho_settings::mojom::VaultItem::New();
      item->id = *id;
      item->revision = rev;
      result->item = std::move(item);
    }
  } else if (data->empty()) {
    return result;
  } else {
    return VaultOperationFailure("invalid_response");
  }
  return result;
}

maho_settings::mojom::VaultItemListResultPtr ParseVaultItemListResultJson(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return VaultItemListFailure("invalid_response");
  }
  const base::DictValue& root = parsed->GetDict();
  if (!root.FindBool("ok").value_or(false)) {
    return VaultItemListFailure(VaultErrorCode(root));
  }
  const base::ListValue* data = root.FindList("data");
  if (!data) {
    return VaultItemListFailure("invalid_response");
  }

  auto result = maho_settings::mojom::VaultItemListResult::New();
  result->success = true;
  for (const base::Value& value : *data) {
    const base::DictValue* item_dict = value.GetIfDict();
    if (!item_dict) {
      return VaultItemListFailure("invalid_response");
    }
    auto item = ParseVaultItem(*item_dict);
    if (!item) {
      return VaultItemListFailure("invalid_response");
    }
    result->items.push_back(std::move(*item));
  }
  return result;
}

maho_settings::mojom::VaultItemListResultPtr
BuildUnavailableVaultItemListResult() {
  return VaultItemListFailure("profile_not_allowed");
}

maho_settings::mojom::VaultOperationResultPtr
BuildUnavailableVaultOperationResult() {
  return VaultOperationFailure("profile_not_allowed");
}

maho_settings::mojom::VaultProviderStatusPtr
BuildUnavailableVaultProviderStatus() {
  auto status = maho_settings::mojom::VaultProviderStatus::New();
  status->selected_provider =
      maho_settings::mojom::PasswordProviderKind::kDisabled;
  status->effective_provider =
      maho_settings::mojom::PasswordProviderKind::kDisabled;
  status->selected_provider_is_available = false;
  return status;
}

maho_settings::mojom::PasswordImportOperationResultPtr
BuildUnavailablePasswordImportOperationResult() {
  return PasswordImportOperationFailure("profile_not_allowed");
}

maho_settings::mojom::VaultOperationResultPtr GetVaultStatusFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  auto result = ParseVaultOperationResultJson(
      TakeVaultJson(maho_vault_status_json(core)));
  if (result->status) {
    auto provider_status = BuildVaultProviderStatus();
    result->status->selected_provider = provider_status->selected_provider;
    result->status->effective_provider = provider_status->effective_provider;
  }
  return result;
}

maho_settings::mojom::VaultOperationResultPtr InitializeVaultInCore(
    const std::string& master_passphrase,
    const std::string& recovery_secret) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("masterPassphrase", master_passphrase);
  request.Set("recoverySecret", recovery_secret);
  std::string json = WriteVaultRequest(std::move(request));
  char* response = maho_vault_initialize_json(core, json.c_str());
  SecureClearString(&json);
  auto result = ParseVaultOperationResultJson(TakeVaultJson(response));
  if (result->status) {
    auto provider_status = BuildVaultProviderStatus();
    result->status->selected_provider = provider_status->selected_provider;
    result->status->effective_provider = provider_status->effective_provider;
  }
  return result;
}

maho_settings::mojom::VaultOperationResultPtr UnlockVaultInCore(
    const std::string& master_passphrase) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("masterPassphrase", master_passphrase);
  std::string json = WriteVaultRequest(std::move(request));
  char* response = maho_vault_unlock_json(core, json.c_str());
  SecureClearString(&json);
  auto result = ParseVaultOperationResultJson(TakeVaultJson(response));
  if (result->status) {
    auto provider_status = BuildVaultProviderStatus();
    result->status->selected_provider = provider_status->selected_provider;
    result->status->effective_provider = provider_status->effective_provider;
  }
  return result;
}

maho_settings::mojom::VaultOperationResultPtr UnlockVaultWithRecoveryInCore(
    const std::string& recovery_secret) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("recoverySecret", recovery_secret);
  std::string json = WriteVaultRequest(std::move(request));
  char* response = maho_vault_unlock_with_recovery_json(core, json.c_str());
  SecureClearString(&json);
  auto result = ParseVaultOperationResultJson(TakeVaultJson(response));
  if (result->status) {
    auto provider_status = BuildVaultProviderStatus();
    result->status->selected_provider = provider_status->selected_provider;
    result->status->effective_provider = provider_status->effective_provider;
  }
  return result;
}

maho_settings::mojom::VaultOperationResultPtr LockVaultInCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  auto result =
      ParseVaultOperationResultJson(TakeVaultJson(maho_vault_lock_json(core)));
  if (result->status) {
    auto provider_status = BuildVaultProviderStatus();
    result->status->selected_provider = provider_status->selected_provider;
    result->status->effective_provider = provider_status->effective_provider;
  }
  return result;
}

maho_settings::mojom::VaultItemListResultPtr ListVaultItemsFromCore(
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
    const std::optional<std::string>& cursor,
    uint32_t limit,
    bool trash_only,
    bool favorites_only) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultItemListFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("schemaVersion", 1);
  request.Set("trash", trash_only ? "only" : "exclude");
  request.Set("favoritesOnly", favorites_only);
  SetOptionalProvider(request, provider);
  request.Set("kinds", VaultItemKindsJson(kinds));
  request.Set("cursor", cursor && !cursor->empty() ? base::Value(*cursor)
                                                   : base::Value());
  if (limit >= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return VaultItemListFailure("invalid_limit");
  }
  const int ffi_limit = limit == 0 ? 0 : static_cast<int>(limit) + 1;
  request.Set("limit", ffi_limit);
  const std::string json = WriteVaultRequest(std::move(request));
  auto result = ParseVaultItemListResultJson(
      TakeVaultJson(maho_vault_list_items_json(core, json.c_str())));
  if (result->success && limit > 0 && result->items.size() > limit) {
    result->items.resize(limit);
    const auto& last_item = result->items.back();
    result->next_cursor = last_item->created_at + "|" + last_item->id;
  }
  return result;
}

maho_settings::mojom::VaultItemListResultPtr SearchVaultItemsFromCore(
    const std::string& origin,
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultItemListFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("schemaVersion", 1);
  request.Set("origin", origin);
  SetOptionalProvider(request, provider);
  request.Set("kinds", VaultItemKindsJson(kinds));
  const std::string json = WriteVaultRequest(std::move(request));
  return ParseVaultItemListResultJson(
      TakeVaultJson(maho_vault_search_items_json(core, json.c_str())));
}

maho_settings::mojom::VaultItemListResultPtr ListSavedPasswordLibraryItemsFromCore(
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::optional<std::string>& cursor,
    uint32_t limit) {
  return ListVaultItemsFromCore(
      provider, {maho_settings::mojom::VaultItemKind::kLogin}, cursor, limit);
}

maho_settings::mojom::VaultItemListResultPtr SearchSavedPasswordLibraryItemsFromCore(
    const std::string& query,
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider) {
  auto listed = ListSavedPasswordLibraryItemsFromCore(provider, std::nullopt, 0);
  if (!listed->success) {
    return listed;
  }
  auto result = maho_settings::mojom::VaultItemListResult::New();
  result->success = true;
  const std::string query_lower = base::ToLowerASCII(query);
  for (auto& item : listed->items) {
    if (item && VaultItemMatchesQuery(*item, query_lower)) {
      result->items.push_back(std::move(item));
    }
  }
  return result;
}

maho_settings::mojom::VaultOperationResultPtr AddSavedPasswordLibraryLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password) {
  return AddVaultLoginInCore(authorization_profile_key, title, origins,
                             username, password);
}

maho_settings::mojom::VaultOperationResultPtr UpdateSavedPasswordLibraryLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password) {
  return UpdateVaultLoginInCore(authorization_profile_key, item_id,
                                expected_revision, title, origins, username,
                                password);
}

maho_settings::mojom::VaultOperationResultPtr DeleteSavedPasswordLibraryItemInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision) {
  return DeleteVaultItemInCore(authorization_profile_key, item_id,
                               expected_revision);
}

maho_settings::mojom::VaultOperationResultPtr UseSavedPasswordLibrarySecretInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action) {
  return UseVaultSecretInCore(authorization_profile_key, item_id,
                              expected_revision, action);
}

bool AddSavedPasswordCompatibilityShimInCore(
    const std::string& authorization_profile_key,
    const std::string& domain,
    const std::string& username,
    const std::string& password) {
  const std::optional<std::string> origin = CredentialOriginForDomain(domain);
  if (!origin) {
    return false;
  }
  auto result = AddSavedPasswordLibraryLoginInCore(
      authorization_profile_key, domain, {*origin}, username, password);
  return result->success;
}

bool UpdateSavedPasswordUsernameCompatibilityShimInCore(
    const std::string& authorization_profile_key,
    const std::string& password_id,
    const std::string& username) {
  auto item = FindSavedPasswordLibraryItemById(password_id);
  if (!item) {
    return false;
  }
  auto result = UpdateSavedPasswordLibraryLoginInCore(
      authorization_profile_key, item->id, item->revision, item->title,
      item->origins, username, std::nullopt);
  return result->success;
}

bool DeleteSavedPasswordCompatibilityShimInCore(
    const std::string& authorization_profile_key,
    const std::string& password_id) {
  auto item = FindSavedPasswordLibraryItemById(password_id);
  if (!item) {
    return false;
  }
  auto result = DeleteSavedPasswordLibraryItemInCore(
      authorization_profile_key, item->id, item->revision);
  return result->success;
}

bool ConsumeSettingsAuthorization(
    const std::string& authorization_profile_key,
    maho::passwords::PasswordAuthorizationAction action) {
  return maho::passwords::MahoPasswordAuthorizationService::Get()
      ->ConsumeAuthorization(
          authorization_profile_key,
          maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
          action);
}

maho_settings::mojom::VaultOperationResultPtr AddVaultLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password,
    const std::optional<std::string>& notes) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("metadata", VaultMetadataJson(title, origins));
  request.Set("username", username);
  request.Set("password", password);
  if (notes) {
    request.Set("notes", *notes);
  }
  std::string json = WriteVaultRequest(std::move(request));
  if (!ConsumeSettingsAuthorization(
          authorization_profile_key,
          maho::passwords::PasswordAuthorizationAction::kAdd)) {
    SecureClearString(&json);
    return VaultOperationFailure("reauth_required");
  }
  char* response = maho_vault_add_login_json(core, json.c_str());
  SecureClearString(&json);
  return ParseVaultOperationResultJson(TakeVaultJson(response));
}

maho_settings::mojom::VaultOperationResultPtr UpdateVaultLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password,
    const std::optional<std::string>& notes) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("id", item_id);
  request.Set("expectedRevision", base::NumberToString(expected_revision));
  request.Set("metadata", VaultMetadataJson(title, origins));
  request.Set("username", username);
  request.Set("password", password ? base::Value(*password) : base::Value());
  if (notes) {
    request.Set("notes", *notes);
  }
  std::string json = WriteVaultRequest(std::move(request));
  if (!ConsumeSettingsAuthorization(
          authorization_profile_key,
          maho::passwords::PasswordAuthorizationAction::kUpdate)) {
    SecureClearString(&json);
    return VaultOperationFailure("reauth_required");
  }
  char* response = maho_vault_update_login_json(core, json.c_str());
  SecureClearString(&json);
  return ParseVaultOperationResultJson(TakeVaultJson(response));
}

maho_settings::mojom::VaultOperationResultPtr DeleteVaultItemInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("id", item_id);
  request.Set("expectedRevision", base::NumberToString(expected_revision));
  std::string json = WriteVaultRequest(std::move(request));
  if (!ConsumeSettingsAuthorization(
          authorization_profile_key,
          maho::passwords::PasswordAuthorizationAction::kDelete)) {
    SecureClearString(&json);
    return VaultOperationFailure("reauth_required");
  }
  char* response = maho_vault_delete_item_json(core, json.c_str());
  SecureClearString(&json);
  return ParseVaultOperationResultJson(TakeVaultJson(response));
}

maho_settings::mojom::VaultOperationResultPtr UseVaultSecretInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action,
    base::OnceCallback<bool(const char*)> reveal) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  auto status = GetVaultStatusFromCore();
  if (!status->success) {
    return status;
  }
  if (!status->status || status->status->lock_state !=
                             maho_settings::mojom::VaultLockState::kUnlocked) {
    return VaultOperationFailure("locked");
  }
  const std::optional<std::string> action_token = SecretActionToken(action);
  if (!action_token) {
    return VaultOperationFailure("invalid_action");
  }
  base::DictValue request;
  request.Set("itemId", item_id);
  request.Set("expectedRevision", base::NumberToString(expected_revision));
  request.Set("action", *action_token);
  std::string json = WriteVaultRequest(std::move(request));
  if (!ConsumeSettingsAuthorization(
          authorization_profile_key,
          maho::passwords::PasswordAuthorizationAction::kCopy)) {
    SecureClearString(&json);
    return VaultOperationFailure("reauth_required");
  }
  char* raw_secret = maho_vault_use_secret_buffer_json(core, json.c_str());
  SecureClearString(&json);
  if (!raw_secret) {
    return VaultOperationFailure("secret_unavailable");
  }
  const bool action_completed = action == maho_settings::mojom::SecretAction::kReveal
      ? reveal && std::move(reveal).Run(raw_secret)
      : PerformSecretAction(action, raw_secret);
  maho_vault_free_secret_buffer(raw_secret);
  if (!action_completed) {
    return VaultOperationFailure("action_failed");
  }
  return VaultOperationSuccess();
}

maho_settings::mojom::VaultOperationResultPtr TrashVaultItemInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision) {
  return RunVaultMutation(profile_key,
      maho::passwords::PasswordAuthorizationAction::kDelete,
      ItemRevisionRequest(id, revision), maho::core::VaultTrashItemJson);
}

maho_settings::mojom::VaultOperationResultPtr RestoreVaultItemInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision) {
  return RunVaultMutation(profile_key,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      ItemRevisionRequest(id, revision), maho::core::VaultRestoreItemJson);
}

maho_settings::mojom::VaultOperationResultPtr EmptyVaultTrashInCore(
    const std::string& profile_key) {
  auto* core = maho::GetCore();
  if (!core) {
    return VaultOperationFailure("core_unavailable");
  }
  if (!ConsumeSettingsAuthorization(profile_key,
          maho::passwords::PasswordAuthorizationAction::kDelete)) {
    return VaultOperationFailure("reauth_required");
  }
  VaultJsonResponse response(maho::core::VaultEmptyTrashJson(core));
  const auto* data = response.data();
  if (!data) {
    return VaultOperationFailure(response.error());
  }
  auto ids = ReadStringList(data->FindList("tombstonedIds"));
  auto count = ReadUint32(*data, "count");
  if (ContainsSecretMaterial(*data) || !ids || !count || ids->size() != *count) {
    return VaultOperationFailure("invalid_response");
  }
  return VaultOperationSuccess();
}

maho_settings::mojom::VaultOperationResultPtr SetVaultItemFavoriteInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision,
    bool favorite) {
  auto request = ItemRevisionRequest(id, revision);
  request.Set("favorite", favorite);
  return RunVaultMutation(profile_key,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      std::move(request), maho::core::VaultSetFavoriteJson);
}

maho_settings::mojom::VaultOperationResultPtr AddVaultSecureNoteInCore(
    const std::string& profile_key, const std::string& title,
    const std::string& notes) {
  base::DictValue request;
  request.Set("title", title);
  request.Set("notes", notes);
  return RunVaultMutation(profile_key,
      maho::passwords::PasswordAuthorizationAction::kAdd,
      std::move(request), maho::core::VaultAddSecureNoteJson);
}

maho_settings::mojom::VaultOperationResultPtr UpdateVaultSecureNoteInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision,
    const std::string& title, const std::string& notes) {
  auto request = ItemRevisionRequest(id, revision);
  request.Set("title", title);
  request.Set("notes", notes);
  return RunVaultMutation(profile_key,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      std::move(request), maho::core::VaultUpdateSecureNoteJson);
}

maho_settings::mojom::VaultOperationResultPtr SetVaultLoginTotpInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision,
    const std::string& secret) {
  auto request = ItemRevisionRequest(id, revision);
  request.Set("secret", secret);
  return RunVaultMutation(profile_key,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      std::move(request), maho::core::VaultSetLoginTotpJson);
}

VaultNotesResult::VaultNotesResult() = default;
VaultNotesResult::VaultNotesResult(const VaultNotesResult&) = default;
VaultNotesResult& VaultNotesResult::operator=(const VaultNotesResult&) =
    default;
VaultNotesResult::VaultNotesResult(VaultNotesResult&&) = default;
VaultNotesResult& VaultNotesResult::operator=(VaultNotesResult&&) = default;
VaultNotesResult::~VaultNotesResult() = default;

VaultNotesResult GetVaultItemNotesFromCore(const std::string& id) {
  VaultNotesResult result;
  if (!maho::GetCore()) {
    result.error_code = "core_unavailable";
    return result;
  }
  const auto json = WriteVaultRequest(base::DictValue().Set("id", id));
  VaultJsonResponse response(maho::core::VaultGetNotesJson(maho::GetCore(), json.c_str()));
  const auto* data = response.data();
  const auto* notes = data ? data->FindString("notes") : nullptr;
  if (!notes) {
    result.error_code = response.error();
    return result;
  }
  result.success = true;
  result.notes = *notes;
  if (const auto* username = data->FindString("username")) {
    result.username = *username;
  }
  return result;
}

maho_settings::mojom::VaultTotpCodeResultPtr GetVaultTotpCodeFromCore(
    const std::string& id) {
  auto result = maho_settings::mojom::VaultTotpCodeResult::New();
  if (!maho::GetCore()) {
    result->error_code = "core_unavailable";
    return result;
  }
  const auto json = WriteVaultRequest(base::DictValue().Set("id", id));
  VaultJsonResponse response(maho::core::VaultTotpCodeJson(maho::GetCore(), json.c_str()));
  const auto* data = response.data();
  if (!data) {
    result->error_code = response.error();
    return result;
  }
  const auto* code = data->FindString("code");
  auto seconds = ReadUint32(*data, "secondsRemaining");
  auto period = ReadUint32(*data, "period");
  if (!code || (code->size() != 6 && code->size() != 8) ||
      !std::all_of(code->begin(), code->end(), [](char c) { return c >= '0' && c <= '9'; }) ||
      !seconds || !period || !*period || *seconds > *period) {
    result->error_code = "invalid_response";
    return result;
  }
  result->success = true;
  result->code = *code;
  result->seconds_remaining = *seconds;
  result->period = *period;
  return result;
}

maho_settings::mojom::GeneratedPasswordResultPtr GeneratePasswordInCore(
    const maho_settings::mojom::PasswordGeneratorOptions& options) {
  auto result = maho_settings::mojom::GeneratedPasswordResult::New();
  if (!maho::GetCore() || options.length > 128 || options.word_count > 12) {
    return result;
  }
  base::DictValue request;
  request.Set("mode", options.mode);
  request.Set("length", static_cast<int>(options.length));
  request.Set("includeLowercase", options.include_lowercase);
  request.Set("includeUppercase", options.include_uppercase);
  request.Set("includeDigits", options.include_digits);
  request.Set("includeSymbols", options.include_symbols);
  request.Set("avoidAmbiguous", options.avoid_ambiguous);
  request.Set("wordCount", static_cast<int>(options.word_count));
  request.Set("separator", options.separator);
  request.Set("capitalize", options.capitalize);
  request.Set("includeNumber", options.include_number);
  const auto json = WriteVaultRequest(std::move(request));
  VaultJsonResponse response(maho::core::VaultGeneratePasswordJson(maho::GetCore(), json.c_str()));
  const auto* data = response.data();
  if (!data || !data->FindBool("success").value_or(false)) {
    return result;
  }
  const auto* password = data->FindString("password");
  auto score = ReadUint32(*data, "strengthScore");
  auto entropy = data->FindDouble("entropyBits");
  if (!password || !score || *score > 4 || !entropy ||
      !std::isfinite(*entropy) || *entropy < 0) {
    return result;
  }
  result->success = true;
  result->password = *password;
  result->strength_score = *score;
  result->entropy_bits = *entropy;
  return result;
}

std::pair<uint32_t, double> EstimatePasswordStrengthInCore(
    const std::string& password) {
  if (!maho::GetCore()) {
    return {0, 0};
  }
  auto json = WriteVaultRequest(base::DictValue().Set("password", password));
  VaultJsonResponse response(maho::core::VaultPasswordStrengthJson(maho::GetCore(), json.c_str()));
  SecureClearString(&json);
  const auto* data = response.data();
  if (!data || ContainsSecretMaterial(*data)) {
    return {0, 0};
  }
  auto score = ReadUint32(*data, "score");
  auto entropy = data->FindDouble("entropyBits");
  if (!score || *score > 4 || !entropy || !std::isfinite(*entropy) || *entropy < 0) {
    return {0, 0};
  }
  return {*score, *entropy};
}

maho_settings::mojom::VaultHealthReportPtr GetVaultHealthReportFromCore() {
  auto result = maho_settings::mojom::VaultHealthReport::New();
  if (!maho::GetCore()) {
    result->error_code = "core_unavailable";
    return result;
  }
  VaultJsonResponse response(maho::core::VaultHealthReportJson(maho::GetCore()));
  const auto* data = response.data();
  if (!data) {
    result->error_code = response.error();
    return result;
  }
  auto weak = ReadStringList(data->FindList("weak"));
  const auto* groups = data->FindList("reused");
  auto count = ReadUint32(*data, "totalLogins");
  if (ContainsSecretMaterial(*data) || !weak || !groups || !count) {
    result->error_code = "invalid_response";
    return result;
  }
  std::vector<std::vector<std::string>> reused;
  for (const auto& group : *groups) {
    auto ids = ReadStringList(group.GetIfList());
    if (!ids) {
      result->error_code = "invalid_response";
      return result;
    }
    reused.push_back(std::move(*ids));
  }
  result->success = true;
  result->weak_item_ids = std::move(*weak);
  result->reused_groups = std::move(reused);
  result->total_logins = *count;
  return result;
}

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatus(
    const std::string& selected_provider,
    bool bitwarden_available,
    bool onepassword_available) {
  auto status = maho_settings::mojom::VaultProviderStatus::New();
  const std::string normalized =
      NormalizePasswordProviderMode(selected_provider);
  status->selected_provider = ToPasswordProviderKind(normalized);
  status->selected_provider_is_available =
      status->selected_provider ==
          maho_settings::mojom::PasswordProviderKind::kMahoNative ||
      (status->selected_provider ==
           maho_settings::mojom::PasswordProviderKind::kBitwarden &&
       bitwarden_available) ||
      (status->selected_provider ==
           maho_settings::mojom::PasswordProviderKind::kOnePassword &&
       onepassword_available);
  status->effective_provider =
      status->selected_provider_is_available
          ? status->selected_provider
          : maho_settings::mojom::PasswordProviderKind::kDisabled;

  for (const auto& [provider, display_name, available] : {
           std::tuple{maho_settings::mojom::PasswordProviderKind::kMahoNative,
                      "Maho Vault", true},
           std::tuple{maho_settings::mojom::PasswordProviderKind::kBitwarden,
                      "Bitwarden", bitwarden_available},
           std::tuple{maho_settings::mojom::PasswordProviderKind::kOnePassword,
                      "1Password", onepassword_available},
       }) {
    auto option = maho_settings::mojom::VaultProviderOption::New();
    option->provider = provider;
    option->display_name = display_name;
    option->is_available = available;
    option->capabilities = MakePasswordProviderCapabilities(provider);
    status->providers.push_back(std::move(option));
  }
  return status;
}

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatus() {
  return BuildVaultProviderStatus(
      ReadSelectedPasswordProviderModeFromCore(),
      maho::passwords::IsBitwardenExtensionAvailable(),
      maho::passwords::IsOnePasswordExtensionAvailable());
}

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatusForTesting(
    const std::string& selected_provider,
    bool bitwarden_available,
    bool one_password_available) {
  return BuildVaultProviderStatus(selected_provider, bitwarden_available,
                                  one_password_available);
}

maho_settings::mojom::VaultPolicyStatusPtr BuildUnavailableVaultPolicyStatus() {
  return VaultPolicyUnavailable("Vault policy persistence is unavailable.");
}

maho_settings::mojom::VaultPolicyStatusPtr GetVaultPolicyStatusFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultPolicyUnavailable("core_unavailable");
  }
  return ParseVaultPolicyStatusJson(
      TakeVaultJson(maho_vault_get_policy_json(core)));
}

maho_settings::mojom::VaultPolicyStatusPtr SetVaultPolicyInCore(
    const std::string& authorization_profile_key,
    maho_settings::mojom::VaultAgentPolicy policy,
    const std::optional<std::string>& item_id,
    const std::optional<std::string>& origin,
    const std::optional<std::string>& expires_at) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultPolicyUnavailable("core_unavailable");
  }
  base::DictValue request;
  request.Set("schemaVersion", 1);
  request.Set("policy", VaultPolicyToken(policy));
  request.Set("itemId", item_id && !item_id->empty() ? base::Value(*item_id)
                                                      : base::Value());
  request.Set("origin", origin && !origin->empty() ? base::Value(*origin)
                                                    : base::Value());
  request.Set("expiresAt", expires_at && !expires_at->empty()
                               ? base::Value(*expires_at)
                               : base::Value());
  std::string json = WriteVaultRequest(std::move(request));
  if (!ConsumeSettingsAuthorization(
          authorization_profile_key,
          maho::passwords::PasswordAuthorizationAction::kPolicyUpdate)) {
    SecureClearString(&json);
    return VaultPolicyUnavailable("reauth_required");
  }
  char* response = maho_vault_set_policy_json(core, json.c_str());
  SecureClearString(&json);
  return ParseVaultPolicyStatusJson(TakeVaultJson(response));
}

maho_settings::mojom::VaultAuditPagePtr BuildUnavailableVaultAuditPage(
    const std::optional<std::string>& cursor) {
  return VaultAuditPageUnavailable(
      cursor && !cursor->empty() ? "invalid_cursor" : "audit_unavailable",
      "Vault audit persistence is unavailable.");
}

maho_settings::mojom::VaultAuditPagePtr GetVaultAuditPageFromCore(
    const std::optional<std::string>& cursor,
    uint32_t limit) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return VaultAuditPageUnavailable("core_unavailable", "core_unavailable");
  }
  if (limit >= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return VaultAuditPageUnavailable("invalid_limit", "invalid_limit");
  }
  base::DictValue request;
  request.Set("schemaVersion", 1);
  request.Set("cursor", cursor && !cursor->empty() ? base::Value(*cursor)
                                                    : base::Value());
  request.Set("limit", static_cast<int>(limit));
  const std::string json = WriteVaultRequest(std::move(request));
  return ParseVaultAuditPageJson(
      TakeVaultJson(maho_vault_audit_page_json(core, json.c_str())));
}

MahoPasswordImportJob* CreatePasswordImportJob() {
  return maho_password_import_job_new();
}

void FreePasswordImportJob(MahoPasswordImportJob* job) {
  maho_password_import_job_free(job);
}

maho_settings::mojom::PasswordImportOperationResultPtr
PreviewPasswordImportFromPath(
    MahoPasswordImportJob* job,
    maho_settings::mojom::PasswordImportSourceFormat source_format,
    const std::string& file_path) {
  if (!job) {
    return PasswordImportOperationFailure("invalid_handle");
  }
  base::DictValue request;
  request.Set("sourceFormat", PasswordImportSourceFormatToken(source_format));
  request.Set("path", file_path);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(request)), &json);
  return ParsePasswordImportOperationResultJson(TakeImportJson(
      maho_password_import_job_preview_path_json(job, json.c_str())));
}

maho_settings::mojom::PasswordImportOperationResultPtr CancelPasswordImportJob(
    MahoPasswordImportJob* job) {
  if (!job) {
    return PasswordImportOperationFailure("invalid_handle");
  }
  return ParsePasswordImportOperationResultJson(
      TakeImportJson(maho_password_import_job_cancel(job)));
}

maho_settings::mojom::PasswordImportOperationResultPtr CommitPasswordImportInCore(
    const std::string& authorization_profile_key,
    MahoPasswordImportJob* job,
    const std::string& preview_token) {
  if (!job) {
    return PasswordImportOperationFailure("invalid_handle");
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return PasswordImportOperationFailure("core_unavailable");
  }
  base::DictValue request;
  request.Set("previewToken", preview_token);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(request)), &json);
  if (!ConsumeSettingsAuthorization(
          authorization_profile_key,
          maho::passwords::PasswordAuthorizationAction::kImportCommit)) {
    SecureClearString(&json);
    return PasswordImportOperationFailure("reauth_required");
  }
  char* response =
      maho_password_import_job_commit_json(job, core, json.c_str());
  SecureClearString(&json);
  return ParsePasswordImportOperationResultJson(TakeImportJson(response));
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

bool IsSupportedVaultAutoLockMinutes(unsigned int minutes) {
  switch (minutes) {
    case 0:
    case 1:
    case 5:
    case 15:
    case 30:
    case 60:
    case 240:
      return true;
    default:
      return false;
  }
}

std::optional<unsigned int> ReadVaultAutoLockMinutesFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  const std::string json = TakeVaultJson(maho_core_get_settings(core));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::DictValue* autofill =
      parsed && parsed->is_dict() ? parsed->GetDict().FindDict("autofill")
                                  : nullptr;
  const std::optional<int> minutes =
      autofill ? autofill->FindInt("vaultAutoLockMinutes") : std::nullopt;
  if (!minutes || *minutes < 0) {
    return std::nullopt;
  }
  return static_cast<unsigned int>(*minutes);
}

bool UpdateVaultAutoLockMinutesInCore(unsigned int minutes) {
  if (!IsSupportedVaultAutoLockMinutes(minutes)) {
    return false;
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  base::DictValue autofill;
  autofill.Set("vaultAutoLockMinutes", static_cast<int>(minutes));
  base::DictValue settings;
  settings.Set("autofill", std::move(autofill));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(settings)), &json);
  maho_core_update_settings(core, json.c_str());
  return true;
}

bool ReadVaultDeviceAuthRequiredFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }
  const std::string json = TakeVaultJson(maho_core_get_settings(core));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::DictValue* autofill =
      parsed && parsed->is_dict() ? parsed->GetDict().FindDict("autofill")
                                  : nullptr;
  const std::optional<bool> required =
      autofill ? autofill->FindBool("vaultRequireDeviceAuth") : std::nullopt;
  return required.value_or(true);
}

bool UpdateVaultDeviceAuthRequiredInCore(bool required) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  base::DictValue autofill;
  autofill.Set("vaultRequireDeviceAuth", required);
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

std::string ReadSelectedPasswordProviderModeFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return "maho_native";
  }
  const std::string json = TakeVaultJson(maho_core_get_settings(core));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::DictValue* autofill = parsed && parsed->is_dict()
                                        ? parsed->GetDict().FindDict("autofill")
                                        : nullptr;
  const std::string* provider =
      autofill ? autofill->FindString("passwordProvider") : nullptr;
  return provider ? *provider : "maho_native";
}

bool IsPasswordSetupCompleteForTesting(
    const std::string& raw_selected_provider,
    const maho_settings::mojom::VaultOperationResult* vault_status) {
  if (!IsApprovedRawPasswordProvider(raw_selected_provider) || !vault_status ||
      !vault_status->success || !vault_status->status) {
    return false;
  }
  switch (vault_status->status->lock_state) {
    case maho_settings::mojom::VaultLockState::kLocked:
    case maho_settings::mojom::VaultLockState::kUnlocked:
    case maho_settings::mojom::VaultLockState::kAutoLocked:
      return true;
    case maho_settings::mojom::VaultLockState::kUninitialized:
      return false;
  }
}

bool IsPasswordSetupComplete() {
  const std::string raw_selected_provider =
      ReadSelectedPasswordProviderModeFromCore();
  auto vault_status = GetVaultStatusFromCore();
  return IsPasswordSetupCompleteForTesting(raw_selected_provider,
                                           vault_status.get());
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

  std::optional<bool> passwords_enabled =
      autofill->FindBool("passwordsEnabled");
  if (passwords_enabled) {
    return *passwords_enabled;
  }

  return true;
}

}  // namespace maho_settings_password_helpers
