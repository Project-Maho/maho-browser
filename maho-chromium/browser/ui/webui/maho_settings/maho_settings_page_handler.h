// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/simple_url_loader.h"

class PrefService;
class Profile;

class MahoSettingsPageHandler : public maho_settings::mojom::PageHandler {
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
  void SetAIApiKey(const std::string& key) override;
  void SetAIEndpoint(const std::string& endpoint) override;
  void SetAIModel(const std::string& model) override;
  void SetAIApprovalPolicy(const std::string& policy) override;
  void SetAISessionPersistence(bool enabled) override;
  void SetAIRuntimeEndpoint(const std::string& endpoint) override;
  void SetAIRuntimeModel(const std::string& model) override;

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

 private:
  static const char* GetPrefPath(const std::string& maho_key);
  std::string ReadSetting(const std::string& maho_key);
  void OnPrefChanged(const std::string& pref_name);
  void NotifySettingsChanged();
  bool HandleFfiSetting(const std::string& key, const std::string& value);
  void AppendFfiSettings(
      std::vector<maho_settings::mojom::SettingValuePtr>& settings);

  void OnThemeStoreFetched(FetchThemeStoreCallback callback,
                           std::optional<std::string> body);
  void LoadCachedThemeStoreFromDisk();
  void PersistCachedThemeStoreToDisk() const;
  base::FilePath GetThemeStoreCachePath() const;

  mojo::Receiver<maho_settings::mojom::PageHandler> receiver_;
  mojo::Remote<maho_settings::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar pref_registrar_;
  raw_ptr<PrefService> local_state_;
  PrefChangeRegistrar local_state_pref_registrar_;

  std::unique_ptr<network::SimpleURLLoader> theme_store_loader_;
  std::string cached_theme_store_json_;
  base::TimeTicks last_theme_store_fetch_;

  base::WeakPtrFactory<MahoSettingsPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_PAGE_HANDLER_H_
