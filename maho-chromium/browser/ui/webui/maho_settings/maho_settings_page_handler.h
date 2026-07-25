// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/shell_dialogs/select_file_dialog.h"

class PrefService;
class Profile;

extern "C" {
struct MahoPasswordImportJob;
}

namespace maho::ai {
class MahoModelListFetcher;
class MahoManagedConnectionTest;
}  // namespace maho::ai

namespace device_reauth {
class DeviceAuthenticator;
}

class MahoSettingsPageHandler : public maho_settings::mojom::PageHandler,
                                 public maho::MahoMailService::Observer,
                                 public maho::MahoUpdateObserver,
                                 public ui::SelectFileDialog::Listener {
 public:
  MahoSettingsPageHandler(
      mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_settings::mojom::Page> page,
      Profile* profile);
  MahoSettingsPageHandler(const MahoSettingsPageHandler&) = delete;
  MahoSettingsPageHandler& operator=(const MahoSettingsPageHandler&) = delete;
  ~MahoSettingsPageHandler() override;

  void OnProfileDeletedForTesting(DeleteProfileCallback callback,
                                  std::string json_res) {
    OnProfileDeleted(std::move(callback), json_res);
  }

  // maho::MahoMailService::Observer:
  void OnAccountsChanged() override;

  // maho::MahoUpdateObserver:
  void OnUpdateStateChanged(maho::UpdateState state) override;
  void OnUpdateProgress(double percent) override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

  static MahoSettingsPageHandler* GetActiveInstance();
  const os_crypt_async::Encryptor* encryptor() const {
    return encryptor_.has_value() ? &encryptor_.value() : nullptr;
  }
  void NotifyShortcutRecorded(maho_settings::mojom::MojoKeyComboPtr key_combo);

  void GetSettings(GetSettingsCallback callback) override;
  void SetSetting(const std::string& key,
                  const std::string& value,
                  SetSettingCallback callback) override;

  void GetBrowserVersionInfo(GetBrowserVersionInfoCallback callback) override;
  void CheckForBrowserUpdates() override;
  void ApplyBrowserUpdateAndRestart() override;

  void GetProfiles(GetProfilesCallback callback) override;
  void CreateProfile(const std::string& name,
                     CreateProfileCallback callback) override;
  void DeleteProfile(const std::string& profile_id,
                     DeleteProfileCallback callback) override;
  void SwitchProfile(const std::string& profile_id,
                     SwitchProfileCallback callback) override;

  void GetATCRules(GetATCRulesCallback callback) override;
  void AddATCRule(const std::string& url_pattern,
                  const std::string& target_space_id,
                  AddATCRuleCallback callback) override;
  void RemoveATCRule(const std::string& rule_id) override;
  void ToggleATCRule(const std::string& rule_id, bool enabled) override;

  void GetAISettings(GetAISettingsCallback callback) override;
  void SetAIProvider(const std::string& provider) override;
  void SetAIBaseUrl(const std::string& url) override;
  void SetAIApiKey(const std::string& key) override;
  void SetAIModel(const std::string& model) override;
  void SetAIApprovalPolicy(const std::string& policy) override;
  void SetAISessionPersistence(bool enabled) override;
  void FetchProviderModels(const std::string& provider_id,
                           FetchProviderModelsCallback callback) override;
  void RefreshProviderModels(const std::string& provider_id,
                             RefreshProviderModelsCallback callback) override;
  void TestManagedConnection(TestManagedConnectionCallback callback) override;

  void GetShortcuts(GetShortcutsCallback callback) override;
  void SetShortcut(const std::string& action,
                   maho_settings::mojom::MojoKeyComboPtr key_combo,
                   SetShortcutCallback callback) override;
  void CheckShortcutConflict(maho_settings::mojom::MojoKeyComboPtr key_combo,
                             CheckShortcutConflictCallback callback) override;
  void ResetShortcut(const std::string& action) override;
  void ResetAllShortcuts() override;
  void ToggleShortcut(const std::string& action, bool enabled) override;
  void SetRecordingMode(bool enabled) override;
  void ExportShortcuts(ExportShortcutsCallback callback) override;
  void ImportShortcuts(const std::string& json_data,
                       ImportShortcutsCallback callback) override;

  void GetSyncStatus(GetSyncStatusCallback callback) override;
  void GetSyncDevices(GetSyncDevicesCallback callback) override;
  void GenerateSyncKey(GenerateSyncKeyCallback callback) override;
  void ConfigureSyncEncryption(
      const std::string& recovery_phrase,
      ConfigureSyncEncryptionCallback callback) override;
  void JoinSync(const std::string& recovery_phrase,
                JoinSyncCallback callback) override;
  void StopSync() override;
  void DisconnectSyncDevice(const std::string& device_id,
                            DisconnectSyncDeviceCallback callback) override;
  void RenameSyncDevice(const std::string& device_id,
                        const std::string& new_name,
                        RenameSyncDeviceCallback callback) override;

  void GetSpaces(GetSpacesCallback callback) override;

  void GetVaultStatus(GetVaultStatusCallback callback) override;
  void GetVaultProviderStatus(GetVaultProviderStatusCallback callback) override;
  void InitializeVault(const std::string& master_passphrase,
                       const std::string& recovery_secret,
                       InitializeVaultCallback callback) override;
  void UnlockVault(const std::string& master_passphrase,
                   UnlockVaultCallback callback) override;
  void UnlockVaultWithRecovery(
      const std::string& recovery_secret,
      UnlockVaultWithRecoveryCallback callback) override;
  void LockVault(LockVaultCallback callback) override;
  void ListVaultItems(
      std::optional<maho_settings::mojom::PasswordProviderKind> provider,
      const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
      const std::optional<std::string>& cursor,
      uint32_t limit,
      ListVaultItemsCallback callback) override;
  void SearchVaultItems(
      const std::string& origin,
      std::optional<maho_settings::mojom::PasswordProviderKind> provider,
      const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
      SearchVaultItemsCallback callback) override;
  void AddVaultLogin(const std::string& title,
                     const std::vector<std::string>& origins,
                     const std::string& username,
                     const std::string& password,
                     AddVaultLoginCallback callback) override;
  void UpdateVaultLogin(const std::string& item_id,
                        uint64_t expected_revision,
                        const std::string& title,
                        const std::vector<std::string>& origins,
                        const std::string& username,
                        const std::optional<std::string>& password,
                        UpdateVaultLoginCallback callback) override;
  void DeleteVaultItem(const std::string& item_id,
                       uint64_t expected_revision,
                       DeleteVaultItemCallback callback) override;
  void UseVaultSecret(const std::string& item_id,
                      uint64_t expected_revision,
                      maho_settings::mojom::SecretAction action,
                      UseVaultSecretCallback callback) override;
  void GetVaultPolicyStatus(GetVaultPolicyStatusCallback callback) override;
  void SetVaultPolicy(maho_settings::mojom::VaultAgentPolicy policy,
                      const std::optional<std::string>& item_id,
                      const std::optional<std::string>& origin,
                      const std::optional<std::string>& expires_at,
                      SetVaultPolicyCallback callback) override;
  void GetVaultAuditPage(const std::optional<std::string>& cursor,
                         uint32_t limit,
                         GetVaultAuditPageCallback callback) override;
  void SelectPasswordImportFile(
      maho_settings::mojom::PasswordImportSourceFormat source_format,
      SelectPasswordImportFileCallback callback) override;
  void PreviewPasswordImport(
      maho_settings::mojom::PasswordImportSourceFormat source_format,
      const std::string& file_path,
      PreviewPasswordImportCallback callback) override;
  void CancelPasswordImport(CancelPasswordImportCallback callback) override;
  void CommitPasswordImport(const std::string& preview_token,
                            CommitPasswordImportCallback callback) override;

  void GetPasswordProviderStatus(
      GetPasswordProviderStatusCallback callback) override;
  void GetPasswordProviderOptions(
      GetPasswordProviderOptionsCallback callback) override;
  void GetSavedPasswords(GetSavedPasswordsCallback callback) override;
  void SearchPasswords(const std::string& query,
                       SearchPasswordsCallback callback) override;
  void AddPassword(const std::string& domain,
                   const std::string& username,
                   const std::string& password,
                   AddPasswordCallback callback) override;
  void UpdatePasswordUsername(const std::string& password_id,
                              const std::string& username,
                              UpdatePasswordUsernameCallback callback) override;
  void DeletePassword(const std::string& password_id,
                      DeletePasswordCallback callback) override;

  void GetAutofillAddresses(GetAutofillAddressesCallback callback) override;
  void AddAutofillAddress(const std::string& id,
                          const std::string& name,
                          const std::string& address_line1,
                          const std::string& address_line2,
                          const std::string& city,
                          const std::string& state,
                          const std::string& postal_code,
                          const std::string& country,
                          const std::string& phone,
                          const std::string& email,
                          AddAutofillAddressCallback callback) override;
  void DeleteAutofillAddress(const std::string& id,
                             DeleteAutofillAddressCallback callback) override;
  void GetAutofillPayments(GetAutofillPaymentsCallback callback) override;
  void AddAutofillPayment(const std::string& id,
                          const std::string& card_network,
                          const std::string& last_four,
                          const std::string& expiration,
                          const std::string& cardholder_name,
                          AddAutofillPaymentCallback callback) override;
  void DeleteAutofillPayment(const std::string& id,
                             DeleteAutofillPaymentCallback callback) override;

  void GetFilterLists(GetFilterListsCallback callback) override;
  void AddFilterList(const std::string& id,
                     const std::string& name,
                     const std::string& url,
                     AddFilterListCallback callback) override;
  void ToggleFilterList(const std::string& id,
                        bool enabled,
                        ToggleFilterListCallback callback) override;
  void RemoveFilterList(const std::string& id,
                        RemoveFilterListCallback callback) override;
  void RebuildContentRules(RebuildContentRulesCallback callback) override;
  void GetContentBlockerStats(GetContentBlockerStatsCallback callback) override;
  void SetContentBlockingMode(maho_settings::mojom::ContentBlockingMode mode,
                              SetContentBlockingModeCallback callback) override;
  void TriggerFilterUpdate(const std::optional<std::string>& list_id,
                           TriggerFilterUpdateCallback callback) override;

  void OpenExtensionsPage() override;

  void SetBYOKKey(const std::string& provider,
                  const std::string& key,
                  SetBYOKKeyCallback callback) override;
  void ClearBYOKKey(const std::string& provider,
                    ClearBYOKKeyCallback callback) override;

  void Login(const std::string& email,
             const std::string& password,
             LoginCallback callback) override;
  void Logout(LogoutCallback callback) override;
  void GetAccountStatus(GetAccountStatusCallback callback) override;
  void GetBillingInfo(GetBillingInfoCallback callback) override;
  void GetInvoices(GetInvoicesCallback callback) override;
  void GetBillingPortalUrl(GetBillingPortalUrlCallback callback) override;
  void GetBuyCreditsUrl(int32_t pack_size_usd,
                        GetBuyCreditsUrlCallback callback) override;

  // Mail accounts — thin proxies to the profile-keyed MahoMailService.
  void MailListAccounts(MailListAccountsCallback callback) override;
  void MailAddAccount(const std::string& request_json,
                      MailAddAccountCallback callback) override;
  void MailDeleteAccount(const std::string& account_id,
                         MailDeleteAccountCallback callback) override;
  void MailOAuthStartUrl(const std::string& provider,
                         const std::string& client_id,
                         const std::string& redirect_uri,
                         MailOAuthStartUrlCallback callback) override;
  void MailOAuthComplete(const std::string& state,
                         const std::string& code,
                         MailOAuthCompleteCallback callback) override;
  void MailReconnectAccount(const std::string& account_id,
                            MailReconnectAccountCallback callback) override;
  void MailOAuthCancel(const std::string& state,
                       MailOAuthCancelCallback callback) override;

  // Signatures
  void MailListSignatures(const std::optional<std::string>& account_id,
                          MailListSignaturesCallback callback) override;
  void MailUpsertSignature(const std::string& request_json,
                           MailUpsertSignatureCallback callback) override;
  void MailDeleteSignature(const std::string& id,
                           MailDeleteSignatureCallback callback) override;

  // Templates
  void MailListTemplates(MailListTemplatesCallback callback) override;
  void MailUpsertTemplate(const std::string& request_json,
                          MailUpsertTemplateCallback callback) override;
  void MailDeleteTemplate(const std::string& id,
                          MailDeleteTemplateCallback callback) override;

  // Labels
  void MailListLabels(const std::string& account_id,
                      MailListLabelsCallback callback) override;
  void MailCreateLabel(const std::string& account_id,
                       const std::string& name,
                       const std::string& color,
                       MailCreateLabelCallback callback) override;
  void MailDeleteLabel(const std::string& id,
                       MailDeleteLabelCallback callback) override;

  // Rules
  void MailListRules(const std::string& account_id,
                     MailListRulesCallback callback) override;
  void MailUpsertRule(const std::string& request_json,
                      MailUpsertRuleCallback callback) override;
  void MailDeleteRule(const std::string& id,
                      MailDeleteRuleCallback callback) override;
  void MailReorderRules(const std::string& account_id,
                        const std::string& rule_ids_json,
                        MailReorderRulesCallback callback) override;

  // Calendar
  void MailGetCalendarPrefs(MailGetCalendarPrefsCallback callback) override;
  void MailSetCalendarPrefs(const std::string& prefs_json,
                            MailSetCalendarPrefsCallback callback) override;
  void MailListCalendarCategories(
      const std::string& account_id,
      MailListCalendarCategoriesCallback callback) override;
  void MailCreateCalendarCategory(
      const std::string& account_id,
      const std::string& name,
      const std::string& color,
      MailCreateCalendarCategoryCallback callback) override;
  void MailUpdateCalendarCategory(
      const std::string& id,
      const std::string& name,
      const std::string& color,
      MailUpdateCalendarCategoryCallback callback) override;
  void MailDeleteCalendarCategory(
      const std::string& id,
      MailDeleteCalendarCategoryCallback callback) override;
  void MailListAccountCalendars(
      const std::string& account_id,
      MailListAccountCalendarsCallback callback) override;
  void MailSetCalendarVisibility(
      const std::string& calendar_row_id,
      bool visible,
      MailSetCalendarVisibilityCallback callback) override;

  // Behavior
  void MailGetBehaviorPrefs(MailGetBehaviorPrefsCallback callback) override;
  void MailSetBehaviorPref(const std::string& key,
                           const std::string& value,
                           MailSetBehaviorPrefCallback callback) override;

  // AI
  void MailGetAIConfig(const std::string& feature,
                       MailGetAIConfigCallback callback) override;
  void MailSetAIConfig(const std::string& feature,
                       const std::string& config_json,
                       MailSetAIConfigCallback callback) override;
  void MailSetAIKey(const std::string& provider,
                    const std::string& api_key,
                    MailSetAIKeyCallback callback) override;

  // Security (PGP/S-MIME)
  void MailGeneratePgpKey(const std::string& account_id,
                          const std::string& email,
                          const std::string& passphrase,
                          MailGeneratePgpKeyCallback callback) override;
  void MailImportPgpKey(const std::string& account_id,
                        const std::string& key_block,
                        const std::optional<std::string>& passphrase,
                        MailImportPgpKeyCallback callback) override;
  void MailListPgpKeys(const std::string& account_id,
                       MailListPgpKeysCallback callback) override;
  void MailDeletePgpKey(const std::string& key_id,
                        MailDeletePgpKeyCallback callback) override;
  void MailSetDefaultPgpKey(const std::string& account_id,
                            const std::string& key_id,
                            MailSetDefaultPgpKeyCallback callback) override;
  void MailImportSmimeIdentity(
      const std::string& account_id,
      const std::string& p12_data_base64,
      const std::string& passphrase,
      MailImportSmimeIdentityCallback callback) override;
  void MailListSmimeIdentities(
      const std::string& account_id,
      MailListSmimeIdentitiesCallback callback) override;
  void MailDeleteSmimeIdentity(
      const std::string& id,
      MailDeleteSmimeIdentityCallback callback) override;
  void MailSetDefaultSmimeIdentity(
      const std::string& account_id,
      const std::string& identity_id,
      MailSetDefaultSmimeIdentityCallback callback) override;

  // MahoMailService::Observer overrides:
  void OnMutation(const std::string& account_id,
                  const std::string& payload) override;
  void OnCalendar(const std::string& account_id,
                  const std::string& payload) override;

 private:
  static const char* GetPrefPath(const std::string& maho_key);
  std::string ReadSetting(const std::string& maho_key);
  void OnPrefChanged(const std::string& pref_name);

  // Builds the full settings snapshot. Used by both GetSettings() and the
  // coalesced notify path so there is a single construction site.
  std::vector<maho_settings::mojom::SettingValuePtr> BuildSettingsSnapshot();

  // Schedules a coalesced notify: if one is already pending the timer is
  // restarted, collapsing burst triggers into a single flush.
  void ScheduleNotify();

  // Fired by notify_timer_. Builds the snapshot, compares against the last
  // emitted value, and pushes to the page only if something changed.
  void FlushNotify();

  enum class FfiResult { kNotHandled, kOk, kInvalid };
  FfiResult HandleFfiSetting(const std::string& key, const std::string& value);
  void AppendFfiSettings(
      std::vector<maho_settings::mojom::SettingValuePtr>& settings);

  void OnRelayLoginCompleted(LoginCallback callback,
                             bool ok,
                             const std::string& error_message);
  void OnProfileDeleted(DeleteProfileCallback callback, std::string json_res);
  void OnSubscriptionResponse(GetBillingInfoCallback callback,
                              bool refreshed_once,
                              std::unique_ptr<network::SimpleURLLoader> loader,
                              std::optional<std::string> response_body);
  void BroadcastAccountStatus();

  mojo::Receiver<maho_settings::mojom::PageHandler> receiver_;
  mojo::Remote<maho_settings::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar pref_registrar_;
  raw_ptr<PrefService> local_state_;
  PrefChangeRegistrar local_state_pref_registrar_;

  base::OneShotTimer notify_timer_;
  std::string last_emitted_snapshot_;

  std::optional<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);
  void OnProviderModelsRefreshed(const std::string& provider_id,
                                 const std::vector<std::string>& model_ids);
  void OnReauthAddComplete(
      const std::string& domain,
      const std::string& username,
      const std::string& password,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      AddPasswordCallback callback,
      bool success);
  void OnReauthUpdateComplete(
      const std::string& password_id,
      const std::string& username,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      UpdatePasswordUsernameCallback callback,
      bool success);
  void OnReauthDeleteComplete(
      const std::string& password_id,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      DeletePasswordCallback callback,
      bool success);
  void OnReauthUseVaultSecretComplete(
      const std::string& item_id,
      uint64_t expected_revision,
      maho_settings::mojom::SecretAction action,
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      UseVaultSecretCallback callback,
      bool success);

  struct PendingByokSave {
    PendingByokSave(std::string provider,
                    std::string key,
                    SetBYOKKeyCallback callback);
    ~PendingByokSave();
    PendingByokSave(PendingByokSave&&);
    PendingByokSave& operator=(PendingByokSave&&);
    std::string provider;
    std::string key;
    SetBYOKKeyCallback callback;
  };
  std::vector<PendingByokSave> pending_byok_saves_;

  std::unique_ptr<maho::ai::MahoModelListFetcher> model_list_fetcher_;
  std::unique_ptr<maho::ai::MahoManagedConnectionTest> managed_connection_test_;
  raw_ptr<MahoPasswordImportJob> password_import_job_ = nullptr;
  scoped_refptr<ui::SelectFileDialog> password_import_select_file_dialog_;
  SelectPasswordImportFileCallback password_import_file_callback_;
  double last_known_credit_balance_ = -1.0;
  double last_known_tier_ceiling_ = -1.0;

  static MahoSettingsPageHandler* g_active_instance;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoSettingsPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
