// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "base/functional/callback.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"

extern "C" {
struct MahoPasswordImportJob;
}

namespace maho_settings_password_helpers {

maho_settings::mojom::PasswordProviderKind ToPasswordProviderKind(
    const std::string& mode);

std::string NormalizePasswordProviderMode(const std::string& mode);

maho_settings::mojom::PasswordProviderCapabilitiesPtr
MakePasswordProviderCapabilities(
    maho_settings::mojom::PasswordProviderKind kind);

std::vector<maho_settings::mojom::PasswordProviderOptionPtr>
BuildPasswordProviderOptions();

maho_settings::mojom::PasswordProviderStatusPtr
BuildPasswordProviderStatus();

bool SetPasswordProviderInCore(
    maho_settings::mojom::PasswordProviderKind provider);

// Returns nullopt when the JSON payload is not a list, contains any non-dict
// element, or any entry is missing/empty for id, domain, username, or
// createdAt. Plaintext passwords are intentionally omitted from the output.
std::optional<std::vector<maho_settings::mojom::SavedPasswordPtr>>
ParseSavedPasswordsJson(const std::string& json);

// New saved-password library adapter. These methods expose Vault metadata and
// operation status only; they never return plaintext over Mojo.
maho_settings::mojom::VaultItemListResultPtr ListSavedPasswordLibraryItemsFromCore(
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::optional<std::string>& cursor,
    uint32_t limit);

maho_settings::mojom::VaultItemListResultPtr SearchSavedPasswordLibraryItemsFromCore(
    const std::string& query,
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider);

maho_settings::mojom::VaultOperationResultPtr AddSavedPasswordLibraryLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password);

maho_settings::mojom::VaultOperationResultPtr UpdateSavedPasswordLibraryLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password);

maho_settings::mojom::VaultOperationResultPtr DeleteSavedPasswordLibraryItemInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision);

maho_settings::mojom::VaultOperationResultPtr UseSavedPasswordLibrarySecretInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action);

// Legacy SavedPassword shims for the existing password settings surface. New UI
// code should use the VaultItem/VaultOperationResult adapter above instead.
std::vector<maho_settings::mojom::SavedPasswordPtr> GetSavedPasswordsFromCore();

std::vector<maho_settings::mojom::SavedPasswordPtr> SearchSavedPasswordsFromCore(
    const std::string& query);

bool AddSavedPasswordCompatibilityShimInCore(
    const std::string& authorization_profile_key,
    const std::string& domain,
                                             const std::string& username,
                                             const std::string& password);

bool UpdateSavedPasswordUsernameCompatibilityShimInCore(
    const std::string& authorization_profile_key,
    const std::string& password_id,
    const std::string& username);

bool DeleteSavedPasswordCompatibilityShimInCore(
    const std::string& authorization_profile_key,
    const std::string& password_id);

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatus(
    const std::string& selected_provider,
    bool bitwarden_available,
    bool onepassword_available);

bool UpdateAutofillSettingsInCore(bool passwords_enabled,
                                  const std::string& mode);

// Supported Vault auto-lock timeouts in minutes; 0 means never.
bool IsSupportedVaultAutoLockMinutes(unsigned int minutes);

// Reads the Vault auto-lock timeout from core settings. Returns std::nullopt
// when core or the setting is unavailable.
std::optional<unsigned int> ReadVaultAutoLockMinutesFromCore();

// Writes the Vault auto-lock timeout. Unsupported values and a missing core
// are rejected rather than coerced.
bool UpdateVaultAutoLockMinutesInCore(unsigned int minutes);

// Whether password operations must be device-reauthenticated (Touch ID).
// Missing or unreadable core settings fail closed to `true`.
bool ReadVaultDeviceAuthRequiredFromCore();

bool UpdateVaultDeviceAuthRequiredInCore(bool required);

std::string ReadPasswordProviderModeFromCore();

bool ReadPasswordsEnabledFromCore();

std::string ReadSelectedPasswordProviderModeFromCore();

// Password setup is complete only when the persisted raw provider token is an
// approved provider and the Vault reports an unlocked status. Provider
// extension availability deliberately has no bearing on this gate.
bool IsPasswordSetupComplete();

bool IsPasswordSetupCompleteForTesting(
    const std::string& raw_selected_provider,
    const maho_settings::mojom::VaultOperationResult* vault_status);

maho_settings::mojom::VaultOperationResultPtr ParseVaultOperationResultJson(
    const std::string& json);

maho_settings::mojom::VaultItemListResultPtr ParseVaultItemListResultJson(
    const std::string& json);

maho_settings::mojom::VaultItemListResultPtr
BuildUnavailableVaultItemListResult();

maho_settings::mojom::VaultOperationResultPtr
BuildUnavailableVaultOperationResult();

maho_settings::mojom::VaultProviderStatusPtr
BuildUnavailableVaultProviderStatus();

maho_settings::mojom::PasswordImportOperationResultPtr
BuildUnavailablePasswordImportOperationResult();

maho_settings::mojom::VaultOperationResultPtr GetVaultStatusFromCore();

maho_settings::mojom::VaultOperationResultPtr InitializeVaultInCore(
    const std::string& master_passphrase,
    const std::string& recovery_secret);

maho_settings::mojom::VaultOperationResultPtr UnlockVaultInCore(
    const std::string& master_passphrase);

maho_settings::mojom::VaultOperationResultPtr UnlockVaultWithRecoveryInCore(
    const std::string& recovery_secret);

maho_settings::mojom::VaultOperationResultPtr LockVaultInCore();

maho_settings::mojom::VaultItemListResultPtr ListVaultItemsFromCore(
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
    const std::optional<std::string>& cursor,
    uint32_t limit,
    bool trash_only = false,
    bool favorites_only = false);

maho_settings::mojom::VaultItemListResultPtr SearchVaultItemsFromCore(
    const std::string& origin,
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds);

maho_settings::mojom::VaultOperationResultPtr AddVaultLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password,
    const std::optional<std::string>& notes = std::nullopt);

maho_settings::mojom::VaultOperationResultPtr UpdateVaultLoginInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password,
    const std::optional<std::string>& notes = std::nullopt);

maho_settings::mojom::VaultOperationResultPtr DeleteVaultItemInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision);

maho_settings::mojom::VaultOperationResultPtr UseVaultSecretInCore(
    const std::string& authorization_profile_key,
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action,
    base::OnceCallback<bool(const char*)> reveal = {});

struct VaultNotesResult {
  VaultNotesResult();
  VaultNotesResult(const VaultNotesResult&);
  VaultNotesResult& operator=(const VaultNotesResult&);
  VaultNotesResult(VaultNotesResult&&);
  VaultNotesResult& operator=(VaultNotesResult&&);
  ~VaultNotesResult();

  bool success = false;
  std::optional<std::string> notes;
  std::optional<std::string> error_code;
  std::optional<std::string> username;
};

maho_settings::mojom::VaultOperationResultPtr TrashVaultItemInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision);
maho_settings::mojom::VaultOperationResultPtr RestoreVaultItemInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision);
maho_settings::mojom::VaultOperationResultPtr EmptyVaultTrashInCore(
    const std::string& profile_key);
maho_settings::mojom::VaultOperationResultPtr SetVaultItemFavoriteInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision,
    bool favorite);
maho_settings::mojom::VaultOperationResultPtr AddVaultSecureNoteInCore(
    const std::string& profile_key, const std::string& title,
    const std::string& notes);
maho_settings::mojom::VaultOperationResultPtr UpdateVaultSecureNoteInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision,
    const std::string& title, const std::string& notes);
maho_settings::mojom::VaultOperationResultPtr SetVaultLoginTotpInCore(
    const std::string& profile_key, const std::string& id, uint64_t revision,
    const std::string& secret);
VaultNotesResult GetVaultItemNotesFromCore(const std::string& id);
maho_settings::mojom::VaultTotpCodeResultPtr GetVaultTotpCodeFromCore(
    const std::string& id);
maho_settings::mojom::GeneratedPasswordResultPtr GeneratePasswordInCore(
    const maho_settings::mojom::PasswordGeneratorOptions& options);
std::pair<uint32_t, double> EstimatePasswordStrengthInCore(
    const std::string& password);
maho_settings::mojom::VaultHealthReportPtr GetVaultHealthReportFromCore();

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatus();

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatusForTesting(
    const std::string& selected_provider,
    bool bitwarden_available,
    bool one_password_available);

maho_settings::mojom::VaultPolicyStatusPtr BuildUnavailableVaultPolicyStatus();

maho_settings::mojom::VaultPolicyStatusPtr GetVaultPolicyStatusFromCore();

maho_settings::mojom::VaultPolicyStatusPtr SetVaultPolicyInCore(
    const std::string& authorization_profile_key,
    maho_settings::mojom::VaultAgentPolicy policy,
    const std::optional<std::string>& item_id,
    const std::optional<std::string>& origin,
    const std::optional<std::string>& expires_at);

maho_settings::mojom::VaultAuditPagePtr BuildUnavailableVaultAuditPage(
    const std::optional<std::string>& cursor);

maho_settings::mojom::VaultAuditPagePtr GetVaultAuditPageFromCore(
    const std::optional<std::string>& cursor,
    uint32_t limit);

MahoPasswordImportJob* CreatePasswordImportJob();

void FreePasswordImportJob(MahoPasswordImportJob* job);

maho_settings::mojom::PasswordImportOperationResultPtr
PreviewPasswordImportFromPath(
    MahoPasswordImportJob* job,
    maho_settings::mojom::PasswordImportSourceFormat source_format,
    const std::string& file_path);

maho_settings::mojom::PasswordImportOperationResultPtr CancelPasswordImportJob(
    MahoPasswordImportJob* job);

maho_settings::mojom::PasswordImportOperationResultPtr CommitPasswordImportInCore(
    const std::string& authorization_profile_key,
    MahoPasswordImportJob* job,
    const std::string& preview_token);

}  // namespace maho_settings_password_helpers

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_
