// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PAGE_HANDLER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/shell_dialogs/select_file_dialog.h"

class Browser;
class Profile;

namespace content {
class WebContents;
}

namespace views {
class Widget;
}

namespace maho {
class EssentialImporter;
class MahoImportSessionBridge;
}

class MahoWelcomePageHandler : public maho_welcome::mojom::PageHandler,
                               public maho::MahoMailService::Observer,
                               public ui::SelectFileDialog::Listener {
 public:
  MahoWelcomePageHandler(
      mojo::PendingReceiver<maho_welcome::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_welcome::mojom::Page> page,
      Profile* profile,
      Browser* browser,
      content::WebContents* web_contents);
  MahoWelcomePageHandler(const MahoWelcomePageHandler&) = delete;
  MahoWelcomePageHandler& operator=(const MahoWelcomePageHandler&) = delete;
  ~MahoWelcomePageHandler() override;

  // maho::MahoMailService::Observer:
  void OnAccountsChanged() override;

  // maho_welcome::mojom::PageHandler:
  void GetAvailableBrowsers(GetAvailableBrowsersCallback callback) override;
  void StartImport(int32_t browser_index, uint32_t items) override;
  void OpenMigrationDialog(OpenMigrationDialogCallback callback) override;
  void GetLocalizedStrings(GetLocalizedStringsCallback callback) override;

  void IsDefaultBrowser(IsDefaultBrowserCallback callback) override;
  void SetAsDefaultBrowser() override;

  void GetSearchEngines(GetSearchEnginesCallback callback) override;
  void SetDefaultSearchEngine(const std::string& keyword) override;

  void GetEssentialSites(GetEssentialSitesCallback callback) override;
  void FavoriteEssentialSites(const std::vector<std::string>& urls) override;

  void PreviewTheme(const std::string& theme_json) override;
  void ApplyTheme(const std::string& theme_json) override;
  void ClearThemePreview() override;
  void OpenThemePickerDialog(const std::string& current_theme_json,
                             OpenThemePickerDialogCallback callback) override;

  void LoginFromWelcome(const std::string& email,
                        const std::string& password,
                        LoginFromWelcomeCallback callback) override;
  void SignInWithGoogleFromWelcome(
      SignInWithGoogleFromWelcomeCallback callback) override;
  void SignupFromWelcome(const std::string& email,
                         const std::string& password,
                         const std::string& display_name,
                         SignupFromWelcomeCallback callback) override;
  void GetRelayAccountStatusFromWelcome(
      GetRelayAccountStatusFromWelcomeCallback callback) override;

  void GetPasswordProviderOptions(
      GetPasswordProviderOptionsCallback callback) override;
  void GetPasswordProviderStatus(
      GetPasswordProviderStatusCallback callback) override;
  void SetPasswordProvider(
      maho_settings::mojom::PasswordProviderKind provider,
      SetPasswordProviderCallback callback) override;
  void GetVaultStatus(GetVaultStatusCallback callback) override;
  void InitializeVault(const std::string& master_passphrase,
                       const std::string& recovery_secret,
                       InitializeVaultCallback callback) override;
  void UnlockVault(const std::string& master_passphrase,
                   UnlockVaultCallback callback) override;

  // Sync key setup
  void GenerateSyncKey(GenerateSyncKeyCallback callback) override;
  void SaveSyncKeyBackup(const std::string& content,
                        SaveSyncKeyBackupCallback callback) override;
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
  void StartSync(const std::string& room_id,
                 const std::string& recovery_phrase,
                 StartSyncCallback callback) override;

  // Mail account onboarding. Thin proxies to the profile-keyed
  // MahoMailService (via MahoMailServiceFactory::GetForProfile(profile_)).
  void MailAddAccount(const std::string& request_json,
                      MailAddAccountCallback callback) override;
  void MailTestConnection(const std::string& params_json,
                          MailTestConnectionCallback callback) override;
  void MailBeginOAuth(const std::string& provider,
                      MailBeginOAuthCallback callback) override;
  void MailListAccounts(MailListAccountsCallback callback) override;
  void MailDeleteAccount(const std::string& account_id,
                         MailDeleteAccountCallback callback) override;
  void MailOAuthCancel(const std::string& state,
                       MailOAuthCancelCallback callback) override;

  // Translation setup (cloud-backed; no local model).
  void GetAiProviderConfigured(GetAiProviderConfiguredCallback callback) override;
  void SetTranslationProvider(const std::string& provider,
                              SetTranslationProviderCallback callback) override;
  void GetByokCredentialConfigured(
      GetByokCredentialConfiguredCallback callback) override;
  void OpenByokSettingsDialog(OpenByokSettingsDialogCallback callback) override;

  void GetSubscriptionCheckoutUrl(
      const std::string& tier,
      GetSubscriptionCheckoutUrlCallback callback) override;

  void FinishOnboarding() override;
  void DevSkipOnboarding() override;
  void OpenFDASettings() override;

  // Called by MahoImportSessionBridge from the UI thread to dispatch Mojo
  // progress events.
  void NotifyImportProgress(uint32_t type,
                            int32_t items_imported,
                            const std::string& error,
                            bool complete);

 private:
  void CloseWelcomeTab();
  void OnSyncKeyBackupWritten(bool saved);
  void CompletePendingMailOAuth();
  void CloseMailOAuthTab(content::WebContents* oauth_contents);
  void ReactivateWelcomeTab();
  void DoFinishOnboarding();
  void RevealBrowserAndCloseWelcome();
  void OnBrowsersDetected(GetAvailableBrowsersCallback callback,
                          std::vector<maho::DetectedBrowser> browsers);
  void OnOpenMigrationDialogBrowsersDetected(
      views::Widget* parent,
      OpenMigrationDialogCallback callback,
      std::vector<maho::DetectedBrowser> browsers);
  void OnOsCryptReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);

  // Idempotent mail-helper launch, called only after relay login/signup
  // succeeds (prefs already hold a valid session, so no gate is needed).
  // Runs on the UI thread where blocking is forbidden, so the crashpad-dir
  // mkdir hops to the thread pool and resumes in OnMailHelperCrashpadReady().
  void LaunchMailHelperIfReady();
  void OnMailHelperCrashpadReady(const base::FilePath& crashpad_database);


  mojo::Receiver<maho_welcome::mojom::PageHandler> receiver_;
  mojo::Remote<maho_welcome::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> web_contents_;
  base::WeakPtr<content::WebContents> pending_mail_oauth_web_contents_;
  base::ScopedObservation<maho::MahoMailService,
                          maho::MahoMailService::Observer>
      mail_service_observation_{this};

  std::vector<maho::DetectedBrowser> detected_browsers_;
  std::unique_ptr<maho::MahoImportSessionBridge> import_session_bridge_;
  std::unique_ptr<maho::EssentialImporter> essential_importer_;
  bool is_finishing_ = false;
  scoped_refptr<ui::SelectFileDialog> sync_save_dialog_;
  SaveSyncKeyBackupCallback sync_save_callback_;
  std::string sync_save_content_;

  scoped_refptr<os_crypt_async::Encryptor> encryptor_;

  base::WeakPtrFactory<MahoWelcomePageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PAGE_HANDLER_H_
