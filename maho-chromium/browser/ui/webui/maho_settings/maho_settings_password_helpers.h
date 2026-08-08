// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password);

maho_settings::mojom::VaultOperationResultPtr UpdateSavedPasswordLibraryLoginInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password);

maho_settings::mojom::VaultOperationResultPtr DeleteSavedPasswordLibraryItemInCore(
    const std::string& item_id,
    uint64_t expected_revision);

maho_settings::mojom::VaultOperationResultPtr UseSavedPasswordLibrarySecretInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action);

// Legacy SavedPassword shims for the existing password settings surface. New UI
// code should use the VaultItem/VaultOperationResult adapter above instead.
std::vector<maho_settings::mojom::SavedPasswordPtr> GetSavedPasswordsFromCore();

std::vector<maho_settings::mojom::SavedPasswordPtr> SearchSavedPasswordsFromCore(
    const std::string& query);

bool AddSavedPasswordCompatibilityShimInCore(const std::string& domain,
                                             const std::string& username,
                                             const std::string& password);

bool UpdateSavedPasswordUsernameCompatibilityShimInCore(
    const std::string& password_id,
    const std::string& username);

bool DeleteSavedPasswordCompatibilityShimInCore(const std::string& password_id);

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatus(
    const std::string& selected_provider,
    bool bitwarden_available,
    bool onepassword_available);

bool UpdateAutofillSettingsInCore(bool passwords_enabled,
                                  const std::string& mode);

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
    uint32_t limit);

maho_settings::mojom::VaultItemListResultPtr SearchVaultItemsFromCore(
    const std::string& origin,
    const std::optional<maho_settings::mojom::PasswordProviderKind>& provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds);

maho_settings::mojom::VaultOperationResultPtr AddVaultLoginInCore(
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password);

maho_settings::mojom::VaultOperationResultPtr UpdateVaultLoginInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password);

maho_settings::mojom::VaultOperationResultPtr DeleteVaultItemInCore(
    const std::string& item_id,
    uint64_t expected_revision);

maho_settings::mojom::VaultOperationResultPtr UseVaultSecretInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action);

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatus();

maho_settings::mojom::VaultProviderStatusPtr BuildVaultProviderStatusForTesting(
    const std::string& selected_provider,
    bool bitwarden_available,
    bool one_password_available);

maho_settings::mojom::VaultPolicyStatusPtr BuildUnavailableVaultPolicyStatus();

maho_settings::mojom::VaultPolicyStatusPtr GetVaultPolicyStatusFromCore();

maho_settings::mojom::VaultPolicyStatusPtr SetVaultPolicyInCore(
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
    MahoPasswordImportJob* job,
    const std::string& preview_token);

}  // namespace maho_settings_password_helpers

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PASSWORD_HELPERS_H_
