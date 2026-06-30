// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"

#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader.h"

class PrefService;
class Profile;

class MahoSettingsPageHandler
    : public maho_settings::mojom::PageHandler,
      public maho::MahoExtensionStateBridge::Observer,
      public extensions::ExtensionUninstallDialog::Delegate {
 public:
  MahoSettingsPageHandler(
      mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_settings::mojom::Page> page,
      Profile* profile);
  MahoSettingsPageHandler(const MahoSettingsPageHandler&) = delete;
  MahoSettingsPageHandler& operator=(const MahoSettingsPageHandler&) = delete;
  ~MahoSettingsPageHandler() override;

  void GetSettings(GetSettingsCallback callback) override;
  void SetSetting(const std::string& key,
                  const std::string& value,
                  SetSettingCallback callback) override;

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

  void GetShortcuts(GetShortcutsCallback callback) override;
  void SetShortcut(const std::string& action,
                   const std::string& key_combo,
                   SetShortcutCallback callback) override;
  void ResetShortcut(const std::string& action) override;
  void ResetAllShortcuts() override;
  void ToggleShortcut(const std::string& action, bool enabled) override;

  void GetSyncStatus(GetSyncStatusCallback callback) override;
  void GetSyncDevices(GetSyncDevicesCallback callback) override;
  void GenerateSyncKey(GenerateSyncKeyCallback callback) override;
  void StartSync(const std::string& sync_key,
                 StartSyncCallback callback) override;
  void JoinSync(const std::string& recovery_phrase,
                JoinSyncCallback callback) override;
  void StopSync() override;

  void FetchThemeStore(FetchThemeStoreCallback callback) override;
  void ApplyThemeToSpace(const std::string& space_id,
                         const std::string& theme_json,
                         ApplyThemeToSpaceCallback callback) override;
  void GetSpaces(GetSpacesCallback callback) override;

  void GetPasswordProviderStatus(GetPasswordProviderStatusCallback callback) override;
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
  void ToggleFilterList(const std::string& id, bool enabled) override;
  void RemoveFilterList(const std::string& id) override;
  void RebuildContentRules(RebuildContentRulesCallback callback) override;
  void GetContentBlockerStats(GetContentBlockerStatsCallback callback) override;

  void GetInstalledExtensions(GetInstalledExtensionsCallback callback) override;
  void ToggleExtension(const std::string& id,
                       ToggleExtensionCallback callback) override;
  void RemoveExtension(const std::string& id,
                       RemoveExtensionCallback callback) override;

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

  // maho::MahoExtensionStateBridge::Observer:
  void OnInstalledExtensionsChanged() override;

  // extensions::ExtensionUninstallDialog::Delegate:
  void OnExtensionUninstallDialogClosed(
      bool did_start_uninstall,
      const std::u16string& error) override;

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

  bool HandleFfiSetting(const std::string& key, const std::string& value);
  void AppendFfiSettings(
      std::vector<maho_settings::mojom::SettingValuePtr>& settings);

  void ScheduleExtensionNotify();
  void FlushExtensionNotify();

  void OnThemeStoreFetched(FetchThemeStoreCallback callback,
                           std::optional<std::string> body);
  void LoadCachedThemeStoreFromDisk();
  void PersistCachedThemeStoreToDisk() const;
  base::FilePath GetThemeStoreCachePath() const;

  void OnLoginResponse(LoginCallback callback,
                       std::unique_ptr<network::SimpleURLLoader> loader,
                       std::optional<std::string> response_body);
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

  base::OneShotTimer ext_notify_timer_;

  base::ScopedObservation<maho::MahoExtensionStateBridge,
                          maho::MahoExtensionStateBridge::Observer>
      ext_bridge_observation_{this};

  std::unique_ptr<extensions::ExtensionUninstallDialog> uninstall_dialog_;
  RemoveExtensionCallback pending_remove_callback_;

  std::unique_ptr<network::SimpleURLLoader> theme_store_loader_;
  std::string cached_theme_store_json_;
  base::TimeTicks last_theme_store_fetch_;

  std::optional<os_crypt_async::Encryptor> encryptor_;
  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoSettingsPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
