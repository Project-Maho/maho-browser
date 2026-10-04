// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"

#include "maho/browser/ui/webui/maho_subscription_checkout.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/escape.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"  // nogncheck
#include "chrome/browser/shell_integration.h"  // nogncheck
#include "chrome/browser/sessions/session_restore.h"  // nogncheck
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"   // nogncheck
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator_params.h"  // nogncheck
#include "chrome/browser/ui/browser_tabstrip.h"  // nogncheck
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/browser/ui/startup/startup_tab.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "chrome/browser/ui/views/frame/browser_view.h"  // nogncheck
#include "chrome/common/chrome_paths.h"  // nogncheck
#include "chrome/grit/generated_resources.h"  // nogncheck
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/browser/importer/maho_import_session_bridge.h"
#include "maho/browser/importer/importers/essential_importer.h"
#include "maho/browser/importer/maho_space_ffi_client.h"
#include "maho/browser/mail_helper/maho_mail_helper_version.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/mail_helper/maho_mail_service_factory.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail_oauth_session.h"
#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/browser/ui/views/welcome/maho_welcome_window.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_google_sign_in.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/platform_util.h"  // nogncheck
#include "chrome/browser/app_controller_mac.h"  // nogncheck
#endif

namespace {

maho_settings::mojom::VaultOperationResultPtr
FinalizeWelcomeVaultLifecycleOperation(
    maho_settings::mojom::VaultOperationResultPtr operation) {
  if (!operation || !operation->success) {
    return operation;
  }

  auto canonical_status =
      maho_settings_password_helpers::GetVaultStatusFromCore();
  if (!canonical_status || !canonical_status->success ||
      !canonical_status->status) {
    maho::NotifyVaultLockStateChanged(/*locked=*/true);
    operation->success = false;
    operation->status.reset();
    operation->error_code = "status_unavailable";
    operation->error_message = "Vault operation failed.";
    return operation;
  }

  const bool locked =
      canonical_status->status->lock_state !=
      maho_settings::mojom::VaultLockState::kUnlocked;
  operation->status = canonical_status->status.Clone();
  maho::NotifyVaultLockStateChanged(locked);
  return operation;
}

// Parented onboarding dialog hosting the existing Settings AI pane
// (chrome://maho-settings?pane=maho-ai) so BYOK credentials are configured
// with the production Settings fields during the welcome wizard.
constexpr int kByokDialogWidth = 560;
constexpr int kByokDialogHeight = 640;

views::Widget* g_byok_settings_dialog_widget = nullptr;
content::WebContents* g_byok_settings_dialog_web_contents = nullptr;

// Mirror of IsProviderConfiguredForRuntime (maho_unified_agent_adapter.cc,
// namespace-local there) for the BYOK providers; keep in sync: openai and
// anthropic accept an encrypted key, an OAuth refresh token, or a legacy
// provider-scoped API key; openai-compatible additionally requires a base
// URL. "maho-managed" and unknown providers report false.
bool IsByokProviderConfiguredForRuntime(const std::string& provider_id,
                                        PrefService* prefs) {
  if (!prefs) {
    return false;
  }
  if (provider_id == "openai") {
    return !prefs->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64).empty() ||
           !prefs->GetString(maho::ai_prefs::kOAuthOpenAIRefreshEncryptedB64)
                .empty() ||
           (prefs->GetString(maho::ai_prefs::kProvider) == "openai" &&
            !prefs->GetString(maho::ai_prefs::kApiKey).empty());
  }
  if (provider_id == "anthropic") {
    return !prefs->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64)
                .empty() ||
           !prefs->GetString(maho::ai_prefs::kOAuthAnthropicRefreshEncryptedB64)
                .empty() ||
           (prefs->GetString(maho::ai_prefs::kProvider) == "anthropic" &&
            !prefs->GetString(maho::ai_prefs::kApiKey).empty());
  }
  if (provider_id == "openai-compatible") {
    return prefs->GetString(maho::ai_prefs::kProvider) ==
               "openai-compatible" &&
           !prefs->GetString(maho::ai_prefs::kBaseUrl).empty();
  }
  return false;
}

class MahoByokSettingsDialogDelegate : public views::DialogDelegate {
 public:
  MahoByokSettingsDialogDelegate(
      Profile* profile, const GURL& url,
      MahoWelcomePageHandler::OpenByokSettingsDialogCallback callback)
      : profile_(profile), callback_(std::move(callback)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    // kWindow gives a native close button; the Settings WebUI has none.
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(true);
    SetTitle(u"Maho Settings");
    SetCanResize(false);
    set_fixed_width(kByokDialogWidth);
    set_margins(gfx::Insets());
    set_corner_radius(12);

    auto web_view = std::make_unique<views::WebView>(profile);
    web_view_ = web_view.get();
    web_view_->SetPreferredSize(gfx::Size(kByokDialogWidth, kByokDialogHeight));
    web_view_->LoadInitialURL(url);
    SetContentsView(std::move(web_view));
    g_byok_settings_dialog_web_contents = web_view_->web_contents();
  }

  MahoByokSettingsDialogDelegate(const MahoByokSettingsDialogDelegate&) = delete;
  MahoByokSettingsDialogDelegate& operator=(
      const MahoByokSettingsDialogDelegate&) = delete;

  void WindowClosing() override {
    if (web_view_) {
      web_view_->SetWebContents(nullptr);
    }
    g_byok_settings_dialog_widget = nullptr;
    g_byok_settings_dialog_web_contents = nullptr;

    PrefService* prefs = profile_ ? profile_->GetPrefs() : nullptr;
    const std::string provider =
        prefs ? prefs->GetString(maho::ai_prefs::kProvider) : std::string();
    const bool configured = IsByokProviderConfiguredForRuntime(provider, prefs);
    std::move(callback_).Run(configured);
  }

 private:
  raw_ptr<Profile> profile_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  MahoWelcomePageHandler::OpenByokSettingsDialogCallback callback_;
};

}  // namespace

content::WebContents*
maho::MahoWelcomeWindow::GetByokSettingsDialogWebContents() {
  return g_byok_settings_dialog_web_contents;
}

MahoWelcomePageHandler::MahoWelcomePageHandler(
    mojo::PendingReceiver<maho_welcome::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_welcome::mojom::Page> page,
    Profile* profile,
    Browser* browser,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile),
      browser_(browser),
      web_contents_(web_contents) {
  // R-4 fail-closed: forged Mojo receiver bypasses config-level denial. Do not remove.
  if (!MahoIsWebUIEnabled(profile)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoWelcomePageHandler::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()));
  }
  maho::MahoMailService* service = maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (service) {
    mail_service_observation_.Observe(service);
  }
}

MahoWelcomePageHandler::~MahoWelcomePageHandler() {
  if (sync_save_dialog_) {
    sync_save_dialog_->ListenerDestroyed();
  }
  if (MahoSpaceThemeState::HasPreviewOverride(browser_)) {
    MahoSpaceThemeState::ClearPreviewOverride(browser_);
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  }
}

void MahoWelcomePageHandler::SaveSyncKeyBackup(
    const std::string& content, SaveSyncKeyBackupCallback callback) {
  if (sync_save_callback_) {
    std::move(callback).Run(false, "A backup save is already in progress.");
    return;
  }
  if (content.empty() || content.size() > 16384 || !web_contents_ ||
      !web_contents_->GetTopLevelNativeWindow()) {
    std::move(callback).Run(false, "The backup or its window is unavailable.");
    return;
  }
  sync_save_callback_ = std::move(callback);
  sync_save_content_ = content;
  if (!sync_save_dialog_) {
    sync_save_dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(web_contents_));
  }
  // SelectFile can synchronously dispatch cancellation or a nested close.
  // Keep the dialog and arguments local and do not access this after the call.
  scoped_refptr<ui::SelectFileDialog> dialog = sync_save_dialog_;
  const base::FilePath filename(FILE_PATH_LITERAL("maho-sync-key-backup.txt"));
  dialog->SelectFile(ui::SelectFileDialog::SELECT_SAVEAS_FILE,
                     u"Save sync recovery key", filename, nullptr, 0,
                     base::FilePath::StringType(),
                     web_contents_->GetTopLevelNativeWindow());
}

void MahoWelcomePageHandler::FileSelected(
    const ui::SelectedFileInfo& file, int index) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath path, std::string content) {
            return base::WriteFile(path, content);
          },
          file.path(), std::move(sync_save_content_)),
      base::BindOnce(&MahoWelcomePageHandler::OnSyncKeyBackupWritten,
                     weak_factory_.GetWeakPtr()));
}

void MahoWelcomePageHandler::FileSelectionCanceled() {
  sync_save_content_.clear();
  std::move(sync_save_callback_).Run(false, "");
}

void MahoWelcomePageHandler::OnSyncKeyBackupWritten(bool saved) {
  std::move(sync_save_callback_).Run(
      saved, saved ? "" : "Could not write the backup file. Please try again.");
}

void MahoWelcomePageHandler::GetAvailableBrowsers(
    GetAvailableBrowsersCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&maho::DetectInstalledBrowsers),
      base::BindOnce(&MahoWelcomePageHandler::OnBrowsersDetected,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoWelcomePageHandler::OnBrowsersDetected(
    GetAvailableBrowsersCallback callback,
    std::vector<maho::DetectedBrowser> browsers) {
  detected_browsers_ = std::move(browsers);
  std::vector<maho_welcome::mojom::BrowserProfilePtr> result;
  result.reserve(detected_browsers_.size());
  for (size_t i = 0; i < detected_browsers_.size(); ++i) {
    auto info = maho_welcome::mojom::BrowserProfile::New();
    info->index = static_cast<int32_t>(i);
    info->name = detected_browsers_[i].display_name;
    info->services_supported = detected_browsers_[i].services_supported;
    result.push_back(std::move(info));
  }
  std::move(callback).Run(std::move(result));
}

void MahoWelcomePageHandler::StartImport(int32_t browser_index,
                                         uint32_t items) {
  if (browser_index < 0 ||
      static_cast<size_t>(browser_index) >= detected_browsers_.size()) {
    LOG(WARNING) << "StartImport: browser_index out of range: " << browser_index;
    return;
  }

  if (import_session_bridge_ && import_session_bridge_->is_active()) {
    LOG(WARNING) << "StartImport: import already in progress";
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    LOG(ERROR) << "StartImport: MahoCore not initialized";
    return;
  }

  const auto& browser = detected_browsers_[browser_index];

  // Serialize DetectedBrowser to JSON matching Rust's serde shape.
  // Rust workers call profile_path.parent() to get the directory, so we pass
  // the file path as stored in detected_browsers_ (with appended filename).

  const char* type_str = nullptr;
  switch (browser.type) {
    case maho::BrowserType::kChrome:  type_str = "Chrome"; break;
    case maho::BrowserType::kArc:     type_str = "Arc"; break;
    case maho::BrowserType::kBrave:   type_str = "Brave"; break;
    case maho::BrowserType::kEdge:    type_str = "Edge"; break;
    case maho::BrowserType::kVivaldi: type_str = "Vivaldi"; break;
    case maho::BrowserType::kOpera:   type_str = "Opera"; break;
    case maho::BrowserType::kFirefox: type_str = "Firefox"; break;
    case maho::BrowserType::kZen:     type_str = "Zen"; break;
    case maho::BrowserType::kSafari:  type_str = "Safari"; break;
  }

  base::DictValue browser_dict;
  browser_dict.Set("browser_type", type_str);
  browser_dict.Set("display_name", browser.display_name);
  browser_dict.Set("profile_path", browser.profile_path.AsUTF8Unsafe());
  browser_dict.Set("services_supported",
                   static_cast<int>(browser.services_supported));
  browser_dict.Set("requires_full_disk_access",
                   browser.requires_full_disk_access);

  std::string browser_json;
  base::JSONWriter::Write(browser_dict, &browser_json);

  std::string essentials_json;

  import_session_bridge_ = std::make_unique<maho::MahoImportSessionBridge>(
      weak_factory_.GetWeakPtr(),
      base::SequencedTaskRunner::GetCurrentDefault(),
      profile_);

  if (!import_session_bridge_->Start(core, browser_json, items,
                                     essentials_json)) {
    import_session_bridge_.reset();
    if (page_) {
      page_->OnImportProgress(0, 0, "Failed to start import session", true);
    }
  }
}

void MahoWelcomePageHandler::NotifyImportProgress(uint32_t type,
                                                   int32_t items_imported,
                                                   const std::string& error,
                                                   bool complete) {
  if (page_) {
    page_->OnImportProgress(type, items_imported, error, complete);
  }
}

void MahoWelcomePageHandler::OpenMigrationDialog(OpenMigrationDialogCallback callback) {
  views::Widget* parent_widget = nullptr;
  if (web_contents_) {
    parent_widget = views::Widget::GetWidgetForNativeWindow(web_contents_->GetTopLevelNativeWindow());
  }
  if (!parent_widget && browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    if (bv) {
      parent_widget = bv->GetWidget();
    }
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&maho::DetectInstalledBrowsers),
      base::BindOnce(&MahoWelcomePageHandler::OnOpenMigrationDialogBrowsersDetected,
                     weak_factory_.GetWeakPtr(), parent_widget, std::move(callback)));
}

void MahoWelcomePageHandler::OnOpenMigrationDialogBrowsersDetected(
    views::Widget* parent,
    OpenMigrationDialogCallback callback,
    std::vector<maho::DetectedBrowser> browsers) {
  detected_browsers_ = std::move(browsers);

  maho::MahoMigrationDialogView::Show(
      parent, profile_, detected_browsers_,
      base::BindOnce(
          [](OpenMigrationDialogCallback cb, bool was_cancelled, uint32_t imported_items_bitmask) {
            std::move(cb).Run(maho_welcome::mojom::MigrationDialogResult::New(
                was_cancelled, imported_items_bitmask));
          },
          std::move(callback)));
}

void MahoWelcomePageHandler::GetLocalizedStrings(
    GetLocalizedStringsCallback callback) {
  base::flat_map<std::string, std::string> strings;

  auto add = [&](const std::string& key, const std::string& en) {
    strings[key] = en;
  };

  add("IDS_MAHO_WELCOME_SPLASH_TITLE1", "Built for people who");
  add("IDS_MAHO_WELCOME_SPLASH_TITLE2", "took browsers seriously.");
  add("IDS_MAHO_WELCOME_SPLASH_START", "Get started");
  add("IDS_MAHO_WELCOME_APPEARANCE_TITLE", "Choose your appearance");
  add("IDS_MAHO_WELCOME_APPEARANCE_BODY",
      "Pick a look for Maho. You can change it anytime in Settings.");
  add("IDS_MAHO_WELCOME_APPEARANCE_SYSTEM", "System");
  add("IDS_MAHO_WELCOME_APPEARANCE_SYSTEM_DESC", "Match your operating system.");
  add("IDS_MAHO_WELCOME_APPEARANCE_LIGHT", "Light");
  add("IDS_MAHO_WELCOME_APPEARANCE_DARK", "Dark");
  add("IDS_MAHO_WELCOME_APPEARANCE_HELPER",
      "You can change this later in Settings.");
  add("IDS_MAHO_WELCOME_APPEARANCE_CONTINUE", "Continue");
  add("IDS_MAHO_WELCOME_IMPORT_TITLE", "A Fresh Start, Same Bookmarks");
  add("IDS_MAHO_WELCOME_IMPORT_BODY1", "Your bookmarks, history, and passwords are like a trail of breadcrumbs through the internet—don't leave them behind!");
  add("IDS_MAHO_WELCOME_IMPORT_BODY2", "Easily bring them over from another browser and pick up right where you left off.");
  add("IDS_MAHO_WELCOME_IMPORT_PROGRESS_TITLE", "Importing your data...");
  add("IDS_MAHO_WELCOME_IMPORT_PROGRESS_BODY",
      "Please wait while we bring over your bookmarks, history, and other data.");
  add("IDS_MAHO_WELCOME_IMPORT_PROGRESS_STATUS", "Importing in progress...");
  add("IDS_MAHO_WELCOME_IMPORT_ERROR_TITLE", "Import couldn't be completed");
  add("IDS_MAHO_WELCOME_IMPORT_ERROR_BODY",
      "Your data was not changed. Go back and try the import again.");
  add("IDS_MAHO_WELCOME_IMPORT_STARTING_STATUS", "Starting import…");
  add("IDS_MAHO_WELCOME_IMPORT_BROWSER_PLACEHOLDER", "Browser");
  add("IDS_MAHO_WELCOME_IMPORT_PROFILE_PLACEHOLDER", "Profile");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_TITLE", "Set as default browser");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_BODY",
      "Make Maho your go-to browser so every link opens right where you want it.");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_YES", "Set as default");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_NO", "Maybe later");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_NOTE",
      "You can change this later in browser settings.");
  add("IDS_MAHO_WELCOME_SEARCH_TITLE", "Your Default Search Engine");
  add("IDS_MAHO_WELCOME_SEARCH_BODY", "Choose your default search engine. You can always change it later!");
  add("IDS_MAHO_WELCOME_SEARCH_LOADING", "Loading search engines…");
  add("IDS_MAHO_WELCOME_SEARCH_EMPTY", "No search engines available.");
  add("IDS_MAHO_WELCOME_ESSENTIALS_TITLE", "Pin Your Essential Apps");
  add("IDS_MAHO_WELCOME_ESSENTIALS_BODY1",
      "Choose the apps you use every day. They'll be pinned to your sidebar — always one click away.");
  add("IDS_MAHO_WELCOME_ESSENTIALS_BODY2",
      "Pinned apps stay visible across every workspace.");
  add("IDS_MAHO_WELCOME_THEME_TITLE", "Choose your theme");
  add("IDS_MAHO_WELCOME_THEME_BODY",
      "Personalize your browser by giving each workspace its own unique color identity.");
  add("IDS_MAHO_WELCOME_THEME_PICK", "Pick a Theme");
  add("IDS_MAHO_WELCOME_THEME_CHANGE", "Change Theme");
  add("IDS_MAHO_WELCOME_COMPLETION_TITLE", "You're all set");
  add("IDS_MAHO_WELCOME_COMPLETION_BODY", "The browser tools you liked, on a stack that isn't going anywhere.");
  add("IDS_MAHO_WELCOME_CTA_IMPORT", "Import now");
  add("IDS_MAHO_WELCOME_CTA_SKIP", "Skip");
  add("IDS_MAHO_WELCOME_CTA_NEXT", "Next");
  add("IDS_MAHO_WELCOME_CTA_BACK", "Back");
  add("IDS_MAHO_WELCOME_CTA_DONE", "Dive in!");
  add("IDS_MAHO_WELCOME_CTA_SIGN_IN", "Sign in");
  add("IDS_MAHO_WELCOME_CTA_SIGN_UP", "Sign up");
  add("IDS_MAHO_WELCOME_AUTH_SIGNIN_TAB", "Sign in");
  add("IDS_MAHO_WELCOME_AUTH_SIGNUP_TAB", "Create account");
  add("IDS_MAHO_WELCOME_AUTH_EMAIL_LABEL", "Email");
  add("IDS_MAHO_WELCOME_AUTH_EMAIL_PLACEHOLDER", "Email");
  add("IDS_MAHO_WELCOME_AUTH_PASSWORD_LABEL", "Password");
  add("IDS_MAHO_WELCOME_AUTH_PASSWORD_PLACEHOLDER", "Password");
  add("IDS_MAHO_WELCOME_AUTH_SIGNIN_CTA", "Sign in");
  add("IDS_MAHO_WELCOME_AUTH_SIGNUP_CTA", "Create account");
  add("IDS_MAHO_WELCOME_AUTH_REAUTH_TITLE", "Session expired");
  add("IDS_MAHO_WELCOME_AUTH_REAUTH_BODY",
      "Sign in again to restore your Maho session.");
  add("IDS_MAHO_WELCOME_AUTH_SIGNUP_TITLE", "Create your account");
  add("IDS_MAHO_WELCOME_AUTH_SIGNUP_BODY",
      "Set up a Maho account to continue.");
  add("IDS_MAHO_WELCOME_AUTH_SIGNIN_TITLE", "Sign in to Maho");
  add("IDS_MAHO_WELCOME_AUTH_SIGNIN_BODY",
      "Sign in to continue setting up Maho.");
  add("IDS_MAHO_WELCOME_AUTH_REQUIRED_ERROR",
      "Email and password are required.");
  add("IDS_MAHO_WELCOME_AUTH_SIGNIN_ERROR", "Sign in failed.");
  add("IDS_MAHO_WELCOME_AUTH_SIGNUP_ERROR", "Sign up failed.");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_TITLE",
      "Choose your password provider");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_BODY",
      "Select where Maho should manage passwords. Your local Vault must be unlocked whichever provider you choose.");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_REQUIRED_INITIALIZE",
      "Master passphrase and recovery secret are required.");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_REQUIRED_UNLOCK",
      "Master passphrase is required.");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_LOADING",
      "Loading password setup…");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_FAILED", "Password setup failed");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_RETRY", "Retry");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_WORKING", "Working…");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_INITIALIZE",
      "Initialize Vault and continue");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_UNLOCK",
      "Unlock Vault and continue");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_CONTINUE", "Continue");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_LOAD_VAULT_ERROR",
      "Failed to load the local Vault status.");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_LOAD_ERROR",
      "Failed to load password setup.");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_TITLE", "Password provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_BODY",
      "External providers require their enabled browser extension.");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_LABEL", "Provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_ARIA", "Password provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_PLACEHOLDER",
      "Choose a provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_UNAVAILABLE",
      "Extension not installed or disabled.");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_UNAVAILABLE_HELPER",
      "The extension is not installed or is disabled. This selection remains saved, and Maho Native will be used until the extension is enabled.");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_SAVE_ERROR",
      "Failed to save the password provider.");
  add("IDS_MAHO_WELCOME_VAULT_CARD_TITLE", "Local Vault");
  add("IDS_MAHO_WELCOME_VAULT_INITIALIZE_BODY",
      "Create the local Vault used to protect password metadata and provider access.");
  add("IDS_MAHO_WELCOME_VAULT_UNLOCK_BODY",
      "Unlock the local Vault before continuing.");
  add("IDS_MAHO_WELCOME_VAULT_READY_BODY",
      "Your local Vault is unlocked and ready.");
  add("IDS_MAHO_WELCOME_VAULT_MASTER_LABEL", "Master passphrase");
  add("IDS_MAHO_WELCOME_VAULT_RECOVERY_LABEL", "Recovery secret");
  add("IDS_MAHO_WELCOME_VAULT_INITIALIZE_ERROR",
      "Vault initialization failed.");
  add("IDS_MAHO_WELCOME_VAULT_UNLOCK_ERROR", "Vault unlock failed.");
  add("IDS_MAHO_WELCOME_VAULT_REQUIRED_ERROR",
      "Unlock your local Vault to continue.");
  add("IDS_MAHO_WELCOME_COMPLETION_SIDEBAR_TITLE1", "All set?");
  add("IDS_MAHO_WELCOME_COMPLETION_SIDEBAR_TITLE2", "Let's get rolling!");
  add("IDS_MAHO_WELCOME_COMPLETION_SIDEBAR_BODY",
      "You're all set up and ready to go. Click the button below to start browsing with Maho.");
  add("IDS_MAHO_WELCOME_COMPLETION_MAIL_SKIPPED_BODY",
      "You can connect your email accounts anytime in settings.");
  add("IDS_MAHO_WELCOME_DEV_SKIP_TITLE",
      "Local dev only — skip onboarding and open the browser");
  add("IDS_MAHO_WELCOME_DEV_SKIP", "Dev: Skip onboarding");
  add("IDS_MAHO_WELCOME_PROGRESS_ARIA",
      "Setup progress: step {{current}} of {{total}}");
  add("IDS_MAHO_WELCOME_PROGRESS_CURRENT", "Current step");
  add("IDS_MAHO_WELCOME_MAIL_COMMON_CANCEL", "Cancel");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_EMAILADDRESS", "Email address");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_DISPLAYNAME", "Display name");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_PASSWORD", "Password");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_PORTRANGE", "Port numbers must be between 1 and 65535.");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_REQUIREDFIELDS", "Please fill in all required fields.");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_CONNECTIONSUCCESS", "Connection successful!");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_TESTCONNECTION", "Test Connection");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_ADDACCOUNT", "Add Account");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_IMAPINCOMING", "IMAP (Incoming)");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_HOST", "Host");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_PORT", "Port");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_ENCRYPTION", "Encryption");
  add("IDS_MAHO_WELCOME_MAIL_ACCOUNT_SMTPOUTGOING", "SMTP (Outgoing)");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_STEPPROVIDER", "Provider");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_STEPCONNECT", "Connect");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_STEPTRANSLATION", "Translation");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_STEPDONE", "Done");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_DELETECONFIRM", "Delete {{email}} from Maho Mail?");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_DELETEFAILED",
      "Could not delete this mail account.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_CHOOSEPROVIDER", "Choose your email provider");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_MAILPREPARING", "Mail is still getting ready. We will retry automatically.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_MOREPROVIDERS", "More providers");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_SHOWFEWER", "Show fewer");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_OAUTHUNAVAILABLE", "OAuth is unavailable. Try IMAP instead.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TESTREQUIREDFIELDS", "Email, password, and IMAP host are required.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TESTFAILED", "Could not connect to the IMAP server.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_INVALIDEMAIL", "Please enter a valid email address.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ADDFAILED", "Could not add this mail account.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONUNAVAILABLE", "Translation setup is unavailable right now.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONCONFIGFAILED", "Failed to configure translation setting.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_SIDEBARTITLE", "Bring your inbox to Maho");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_SIDEBARBODY", "Connect an account now so Mail is ready when you finish setup.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_SIDEBARCONNECTEDCOUNT", "{{count}} account{{plural}} connected.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_SIDEBARSKIPBODY", "You can skip this step and add accounts later.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATURESMARTINBOX", "Smart Inbox");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATURESMARTINBOXDESC", "AI categorizes your emails automatically");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATUREAIASSISTANT", "AI Assistant");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATUREAIASSISTANTDESC", "Summarize, reply, and translate with AI");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATUREQUICKSEARCH", "Quick Search");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATUREQUICKSEARCHDESC", "Find emails with keyword or AI search");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATUREPRIVACY", "Privacy First");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_FEATUREPRIVACYDESC", "Your data stays on your device");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ACCOUNTADDED", "Account added!");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ACCOUNTADDEDDESCRIPTION", "Your email account has been set up successfully.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_GETSTARTED", "Get Started");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_OAUTHWAITINGTITLE", "Waiting for authorization…");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_OAUTHWAITINGBODY", "Complete sign-in to {{provider}} in the new tab.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_REMOVEACCOUNTLABEL", "Remove {{email}}");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONSTEPTITLE", "Translation Setup");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONSTEPSUBTITLE", "Choose how you'd like to translate emails in other languages.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONBYOKTITLE", "Cloud AI (Your API Key)");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONBYOKRECOMMENDED", "Recommended");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONBYOKSUBTITLE", "Use your existing OpenAI, Anthropic, or Ollama key for fast, high-quality translation.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONCHECKINGCONFIG", "Checking configuration...");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONBYOKNEEDSCONFIG", "You'll need to add an API key in Settings → AI after setup.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONSKIPTITLE", "Skip for now");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TRANSLATIONSKIPSUBTITLE", "You can enable translation later in Settings.");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_BACK", "Back");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_CREDENTIALSTITLE", "Enter your credentials");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_EMAILPLACEHOLDER", "you@example.com");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_DISPLAYNAMEPLACEHOLDER", "Your Name");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PASSWORDPLACEHOLDER", "App password or account password");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ADVANCEDSETTINGS", "Advanced server settings");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_TESTINGCONNECTION", "Testing...");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ADDINGACCOUNT", "Adding...");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_IMAPHOSTPLACEHOLDER", "imap.example.com");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_SMTPHOSTPLACEHOLDER", "smtp.example.com");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERGMAIL", "Gmail");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERGMAILDESC", "Sign in with Google");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDEROUTLOOK", "Outlook");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDEROUTLOOKDESC", "Sign in with Microsoft");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERYAHOO", "Yahoo Mail");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERAPPPASSWORDDESC", "App password required");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERICLOUD", "iCloud Mail");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERFASTMAIL", "Fastmail");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERZOHO", "Zoho Mail");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERAOL", "AOL");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERGMX", "GMX");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERGMXDESC", "Free email service");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERYANDEX", "Yandex");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERNAVER", "Naver");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDERNAVERDESC", "Enable IMAP and use an app password");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDEROTHER", "Other");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_PROVIDEROTHERDESC", "IMAP/SMTP manual setup");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_NAVERGUIDETITLE", "Naver Mail Setup");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_NAVERGUIDESTEP1", "Log in to Naver Mail (mail.naver.com)");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_NAVERGUIDESTEP2", "Go to Settings → POP3/IMAP");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_NAVERGUIDESTEP3", "Enable IMAP/SMTP access");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_NAVERGUIDESTEP4", "Set up 2-step verification in Naver account settings");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_NAVERGUIDESTEP5", "Generate an app password and use it below");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_YAHOOGUIDETITLE", "Yahoo Mail Setup");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_YAHOOGUIDESTEP1", "Enable 2-step verification in Yahoo account security");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_YAHOOGUIDESTEP2", "Generate an app password (Account Info → Security → App passwords)");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_YAHOOGUIDESTEP3", "Use the app password below instead of your regular password");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ICLOUDGUIDETITLE", "iCloud Mail Setup");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ICLOUDGUIDESTEP1", "Enable 2-factor authentication for your Apple ID");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ICLOUDGUIDESTEP2", "Go to appleid.apple.com → Sign-In and Security → App-Specific Passwords");
  add("IDS_MAHO_WELCOME_MAIL_ONBOARDING_ICLOUDGUIDESTEP3", "Generate an app password and use it below");

  std::move(callback).Run(std::move(strings));
}


void MahoWelcomePageHandler::IsDefaultBrowser(
    IsDefaultBrowserCallback callback) {
  shell_integration::DefaultWebClientState state =
      shell_integration::GetDefaultBrowser();
  std::move(callback).Run(state == shell_integration::IS_DEFAULT);
}

void MahoWelcomePageHandler::SetAsDefaultBrowser() {
  shell_integration::SetAsDefaultBrowser();
}

void MahoWelcomePageHandler::GetSearchEngines(
    GetSearchEnginesCallback callback) {
  std::vector<maho_welcome::mojom::SearchEngineInfoPtr> engines;
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(std::move(engines));
    return;
  }

  const TemplateURL* default_engine = service->GetDefaultSearchProvider();
  const std::string default_keyword =
      default_engine ? base::UTF16ToUTF8(default_engine->keyword())
                     : std::string();

  bool has_ddg = false;
  for (const TemplateURL* turl : service->GetTemplateURLs()) {
    if (!turl->prepopulate_id()) continue;
    std::string name_utf8 = base::UTF16ToUTF8(turl->short_name());
    std::string name_lower = base::ToLowerASCII(name_utf8);
    if (name_lower.find("wikipedia") != std::string::npos ||
        name_lower.find("ebay") != std::string::npos) {
      continue;
    }
    if (name_lower.find("duckduckgo") != std::string::npos ||
        turl->prepopulate_id() == 92) {
      has_ddg = true;
    }
    auto info = maho_welcome::mojom::SearchEngineInfo::New();
    info->keyword = base::UTF16ToUTF8(turl->keyword());
    info->name = name_utf8;
    // Prepopulated search engines have no cached favicon (user has never visited
    // the page), so chrome://favicon2 returns the generic globe. We bundle Brave
    // Leo SVGs (MPL 2.0, brave/leo @ ed35a435) and select by prepopulate_id.
    static constexpr struct {
      int id;
      const char* path;
    } kBundledSearchEngineFavicons[] = {
        {1,  "chrome://maho-welcome/icons/search-engines/google.svg"},
        {3,  "chrome://maho-welcome/icons/search-engines/bing.svg"},
        {67, "chrome://maho-welcome/icons/search-engines/naver.svg"},
        {68, "chrome://maho-welcome/icons/search-engines/daum.svg"},
        {91, "chrome://maho-welcome/icons/search-engines/coccoc.svg"},
        {92, "chrome://maho-welcome/icons/search-engines/duckduckgo.png"},
    };
    bool found = false;
    for (const auto& entry : kBundledSearchEngineFavicons) {
      if (turl->prepopulate_id() == entry.id) {
        info->icon_url = entry.path;
        found = true;
        break;
      }
    }
    if (!found && name_lower.find("duckduckgo") != std::string::npos) {
      info->icon_url = "chrome://maho-welcome/icons/search-engines/duckduckgo.png";
      found = true;
    }
    if (!found) {
      // Other prepopulated engines: try favicon2 (cache-only fallback)
      std::string raw_favicon = turl->favicon_url().spec();
      if (!raw_favicon.empty()) {
        info->icon_url =
            "chrome://favicon2/?size=32&scaleFactor=1x&iconUrl=" +
            base::EscapeQueryParamValue(raw_favicon, /*use_plus=*/false);
      }
    }
    info->is_default = (info->keyword == default_keyword);
    engines.push_back(std::move(info));
  }

  if (!has_ddg) {
    const TemplateURL* ddg_turl =
        service->GetTemplateURLForKeyword(u"duckduckgo.com");
    if (!ddg_turl) {
      ddg_turl = service->GetTemplateURLForKeyword(u"duckduckgo");
    }
    if (!ddg_turl) {
      TemplateURLData data;
      data.SetShortName(u"DuckDuckGo");
      data.SetKeyword(u"duckduckgo.com");
      data.SetURL("https://duckduckgo.com/?q={searchTerms}");
      data.favicon_url = GURL("https://duckduckgo.com/favicon.ico");
      ddg_turl = service->Add(std::make_unique<TemplateURL>(data));
    }
    if (ddg_turl) {
      auto info = maho_welcome::mojom::SearchEngineInfo::New();
      info->keyword = base::UTF16ToUTF8(ddg_turl->keyword());
      info->name = base::UTF16ToUTF8(ddg_turl->short_name());
      info->icon_url = "chrome://maho-welcome/icons/search-engines/duckduckgo.png";
      info->is_default = (info->keyword == default_keyword);
      engines.push_back(std::move(info));
    }
  }

  std::move(callback).Run(std::move(engines));
}

void MahoWelcomePageHandler::SetDefaultSearchEngine(
    const std::string& keyword) {
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service) return;

  for (const TemplateURL* turl : service->GetTemplateURLs()) {
    if (base::UTF16ToUTF8(turl->keyword()) == keyword) {
      service->SetUserSelectedDefaultSearchProvider(
          const_cast<TemplateURL*>(turl));
      break;
    }
  }
}

void MahoWelcomePageHandler::GetEssentialSites(
    GetEssentialSitesCallback callback) {
  std::vector<maho_welcome::mojom::EssentialSitePtr> sites;
  auto add = [&](const char* url, const char* name) {
    auto site = maho_welcome::mojom::EssentialSite::New();
    site->url = url;
    site->name = name;
    site->icon_path = "";
    sites.push_back(std::move(site));
  };
  add("https://obsidian.md", "Obsidian");
  add("https://discord.com", "Discord");
  add("https://trello.com", "Trello");
  add("https://slack.com", "Slack");
  add("https://github.com", "GitHub");
  add("https://app.tuta.com/", "Tuta");
  add("https://notion.com", "Notion");
  add("https://calendar.google.com", "Calendar");
  add("https://figma.com", "Figma");
  add("https://youtube.com", "YouTube");
  add("https://twitter.com", "Twitter");
  add("https://reddit.com", "Reddit");
  std::move(callback).Run(std::move(sites));
}

void MahoWelcomePageHandler::FavoriteEssentialSites(
    const std::vector<std::string>& urls) {
  if (!essential_importer_) {
    essential_importer_ = std::make_unique<maho::EssentialImporter>();
  }
  essential_importer_->FavoriteEssentialSites(urls, base::DoNothing());
}

void MahoWelcomePageHandler::PreviewTheme(const std::string& theme_json) {
  if (MahoSpaceThemeState::SetPreviewOverride(browser_, theme_json)) {
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  }
}

void MahoWelcomePageHandler::ApplyTheme(const std::string& theme_json) {
  if (browser_) {
    std::string active_space_id =
        maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser_);
    if (!active_space_id.empty()) {
      MahoCore* core = maho::GetCore();
      if (core) {
        maho_theme::ApplyThemeJsonToSpace(core, active_space_id, theme_json);
      }
    }
  }
  MahoSpaceThemeState::ClearPreviewOverride(browser_);
  MahoSpaceThemeState::UpdateFromCore();
  MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
}

void MahoWelcomePageHandler::ClearThemePreview() {
  if (MahoSpaceThemeState::HasPreviewOverride(browser_)) {
    MahoSpaceThemeState::ClearPreviewOverride(browser_);
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  }
}

void MahoWelcomePageHandler::OpenThemePickerDialog(
    const std::string& current_theme_json,
    OpenThemePickerDialogCallback callback) {
  if (!browser_) {
    std::move(callback).Run("");  // empty = treated as cancelled by client
    return;
  }
  MahoSpaceThemePickerDialog::Open(
      browser_,
      current_theme_json,
      base::BindOnce(
          [](OpenThemePickerDialogCallback cb,
             const std::string& committed_theme_json,
             bool cancelled) {
            std::move(cb).Run(cancelled ? std::string() : committed_theme_json);
          },
          std::move(callback)));
}

void MahoWelcomePageHandler::SignupFromWelcome(
    const std::string& email,
    const std::string& password,
    const std::string& display_name,
    SignupFromWelcomeCallback callback) {
  if (email.empty() || password.empty()) {
    std::move(callback).Run(false, "Email and password are required.");
    return;
  }

  if (!encryptor_) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  maho::auth::MahoRelaySignup(
      profile_->GetPrefs(), url_loader_factory, *encryptor_, email, password,
      display_name,
      base::BindOnce(
          [](SignupFromWelcomeCallback cb,
             base::WeakPtr<MahoWelcomePageHandler> handler, PrefService* prefs,
             bool ok, const std::string& error_message) {
            if (ok) {
              prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
              if (handler) {
                handler->LaunchMailHelperIfReady();
              }
            }
            std::move(cb).Run(ok, error_message);
          },
          std::move(callback), weak_factory_.GetWeakPtr(),
          profile_->GetPrefs()));
}

void MahoWelcomePageHandler::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
}

void MahoWelcomePageHandler::LaunchMailHelperIfReady() {
  base::FilePath user_data_dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    OnMailHelperCrashpadReady(base::FilePath());
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](base::FilePath user_data_dir) -> base::FilePath {
        base::FilePath crashpad = user_data_dir.AppendASCII("Crashpad");
        base::CreateDirectory(crashpad);
        return crashpad;
      }, user_data_dir),
      base::BindOnce(&MahoWelcomePageHandler::OnMailHelperCrashpadReady,
                     weak_factory_.GetWeakPtr()));
}

void MahoWelcomePageHandler::OnMailHelperCrashpadReady(
    const base::FilePath& crashpad_database) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    return;
  }
  service->EnsureHelperLaunched(maho::mail_helper::kMahoMailHelperVersion,
                                profile_->GetPath().AsUTF8Unsafe(),
                                crashpad_database);
}

void MahoWelcomePageHandler::FinishOnboarding() {
  DVLOG(1) << "[MahoFinish] ENTER is_finishing=" << is_finishing_;
  if (is_finishing_) {
    return;
  }

  if (!maho::auth::HasValidRelaySession(profile_->GetPrefs())) {
    LOG(ERROR) << "FinishOnboarding: Refusing to finish onboarding without valid relay session.";
    return;
  }

  DoFinishOnboarding();
}

void MahoWelcomePageHandler::DevSkipOnboarding() {
  // MAHO_RELAY_URL is only set for local relay development and is never set in
  // shipped builds, so this relay-session bypass is unreachable in production.
  if (!std::getenv("MAHO_RELAY_URL")) {
    LOG(ERROR) << "DevSkipOnboarding ignored: not a local-dev build.";
    return;
  }
  if (is_finishing_) {
    return;
  }
  DoFinishOnboarding();
}

void MahoWelcomePageHandler::DoFinishOnboarding() {
  is_finishing_ = true;

  PrefService* prefs = profile_->GetPrefs();
  DVLOG(1) << "[MahoFinish] step1 SetPref kWelcomeCompleted";
  prefs->SetBoolean(maho::welcome::kWelcomeCompleted, true);
  prefs->SetBoolean(maho::welcome::kLoginGateActive, false);

  DVLOG(1) << "[MahoFinish] step2 theme cleanup; HasPreview="
           << MahoSpaceThemeState::HasPreviewOverride(browser_);
  if (MahoSpaceThemeState::HasPreviewOverride(browser_)) {
    MahoSpaceThemeState::ClearPreviewOverride(browser_);
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  }

  DVLOG(1) << "[MahoFinish] step3 defer reveal; profile_=" << profile_;
  // The login gate suppressed startup session-restore, so the returning user's
  // tabs still live only on disk. Reveal (restore-or-create) the browser after
  // this Mojo call unwinds: SessionRestore(SYNCHRONOUS) spins a nested RunLoop
  // that must not run inside a Mojo dispatch. The welcome window's keep-alive
  // pins the process until RevealBrowserAndCloseWelcome swaps in the browser.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoWelcomePageHandler::RevealBrowserAndCloseWelcome,
                     weak_factory_.GetWeakPtr()));
}

void MahoWelcomePageHandler::RevealBrowserAndCloseWelcome() {
  if (!profile_) {
    return;
  }

  // The gate suppressed startup browser creation, so no browser exists yet:
  // this becomes the sole browser and its sidebar claims the per-profile media
  // dialog delegate exactly once, avoiding the SetDialogDelegate DCHECK.
  // FindTabbedBrowser is a defensive idempotency guard for the non-gated path.
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  BrowserWindowInterface* main_bwi =
      collection ? collection->FindTabbedBrowser(/*match_original_profiles=*/false)
                 : nullptr;
  if (!main_bwi) {
    main_bwi = SessionRestore::RestoreSession(
        profile_, /*browser=*/nullptr,
        SessionRestore::SYNCHRONOUS | SessionRestore::RESTORE_BROWSER,
        StartupTabs());
  }
  if (!main_bwi) {
    BrowserWindowCreateParams params(profile_, true);
    params.creation_source = BrowserWindowCreateParams::CreationSource::kStartupCreator;
    main_bwi = CreateBrowserWindow(std::move(params));
  }
  main_bwi->GetWindow()->Show();
  main_bwi->GetWindow()->Activate();

#if BUILDFLAG(IS_MAC)
  app_controller_mac::DrainQueuedNativeUrlsAfterMahoWelcome(profile_);
#endif

  auto* overlay = MahoNotificationOverlay::GetOrCreateForBrowser(
      static_cast<Browser*>(main_bwi));
  overlay->Show(u"Welcome to Maho", u"Import complete. Start exploring.");

  // Destroys the welcome window and this handler; must be the last statement.
  CloseWelcomeTab();
}

void MahoWelcomePageHandler::OpenFDASettings() {
#if BUILDFLAG(IS_MAC)
  platform_util::OpenExternal(
      GURL("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"));
#else
  // No-op on non-macOS platforms. Full Disk Access is a macOS concept.
#endif
}

void MahoWelcomePageHandler::CloseWelcomeTab() {
  // Welcome lives in a frameless MahoWelcomeWindow (no Browser).  Close the
  // owning window if it is still alive.  Fall back to the legacy app-mode
  // Browser tab close path for safety, in case the window has already been
  // torn down.
  if (web_contents_) {
    if (auto* window =
            maho::MahoWelcomeWindow::FromWebContents(web_contents_)) {
      window->Close();
      return;
    }
  }

  if (!browser_ || !web_contents_) return;
  TabStripModel* model = browser_->GetTabStripModel();
  int index = model->GetIndexOfWebContents(web_contents_);
  if (index == TabStripModel::kNoTab) return;
  model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
}

void MahoWelcomePageHandler::LoginFromWelcome(
    const std::string& email,
    const std::string& password,
    LoginFromWelcomeCallback callback) {
  if (email.empty() || password.empty()) {
    std::move(callback).Run(false, "Email and password are required.");
    return;
  }

  if (!encryptor_) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  maho::auth::MahoRelayLogin(
      profile_->GetPrefs(), url_loader_factory, *encryptor_, email, password,
      base::BindOnce(
          [](LoginFromWelcomeCallback cb,
             base::WeakPtr<MahoWelcomePageHandler> handler, PrefService* prefs,
             bool ok, const std::string& error_message) {
            if (ok) {
              if (prefs->GetString(maho::ai_prefs::kProvider).empty()) {
                prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
              }
              if (handler) {
                handler->LaunchMailHelperIfReady();
              }
            }
            std::move(cb).Run(ok, error_message);
          },
          std::move(callback), weak_factory_.GetWeakPtr(),
          profile_->GetPrefs()));
}

void MahoWelcomePageHandler::SignInWithGoogleFromWelcome(
    SignInWithGoogleFromWelcomeCallback callback) {
  if (!encryptor_) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  BrowserWindowInterface* last_active =
      collection ? collection->GetLastActiveBrowser() : nullptr;
  Browser* peek_host_browser = static_cast<Browser*>(last_active);
  if (!maho::IsPeekEligible(peek_host_browser) ||
      peek_host_browser->GetProfile() != profile_ ||
      peek_host_browser->GetProfile() != web_contents_->GetBrowserContext()) {
    peek_host_browser = nullptr;
  }

  maho::auth::StartGoogleSignIn(
      profile_, peek_host_browser, *encryptor_,
      base::BindOnce(
          [](SignInWithGoogleFromWelcomeCallback cb,
             base::WeakPtr<MahoWelcomePageHandler> handler, PrefService* prefs,
             bool ok, const std::string& error_message) {
            if (ok) {
              if (prefs->GetString(maho::ai_prefs::kProvider).empty()) {
                prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
              }
              if (handler) {
                handler->LaunchMailHelperIfReady();
              }
            }
            std::move(cb).Run(ok, error_message);
          },
          std::move(callback), weak_factory_.GetWeakPtr(),
          profile_->GetPrefs()));
}

void MahoWelcomePageHandler::GetRelayAccountStatusFromWelcome(
    GetRelayAccountStatusFromWelcomeCallback callback) {
  PrefService* prefs = profile_->GetPrefs();
  bool signed_in = maho::auth::HasValidRelaySession(prefs);
  std::string tier = prefs->GetString(maho::account_prefs::kRelayUserTier);
  std::string sub_status =
      prefs->GetString(maho::account_prefs::kRelaySubscriptionStatus);
  bool welcome_completed = prefs->GetBoolean(maho::welcome::kWelcomeCompleted);
  bool has_stored_session =
      !prefs->GetString(maho::account_prefs::kRelayAccessTokenEncryptedB64)
           .empty();
  bool is_local_dev = std::getenv("MAHO_RELAY_URL") != nullptr;
  std::move(callback).Run(signed_in, tier, sub_status, welcome_completed,
                          has_stored_session, is_local_dev);
}

void MahoWelcomePageHandler::GetPasswordProviderOptions(
    GetPasswordProviderOptionsCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::BuildPasswordProviderOptions());
}

void MahoWelcomePageHandler::GetPasswordProviderStatus(
    GetPasswordProviderStatusCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::BuildPasswordProviderStatus());
}

void MahoWelcomePageHandler::SetPasswordProvider(
    maho_settings::mojom::PasswordProviderKind provider,
    SetPasswordProviderCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::SetPasswordProviderInCore(provider));
}

void MahoWelcomePageHandler::GetVaultStatus(
    GetVaultStatusCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultOperationResult());
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultStatusFromCore());
}

void MahoWelcomePageHandler::InitializeVault(
    const std::string& master_passphrase,
    const std::string& recovery_secret,
    InitializeVaultCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultOperationResult());
    return;
  }
  std::move(callback).Run(FinalizeWelcomeVaultLifecycleOperation(
      maho_settings_password_helpers::InitializeVaultInCore(
          master_passphrase, recovery_secret)));
}

void MahoWelcomePageHandler::UnlockVault(
    const std::string& master_passphrase,
    UnlockVaultCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultOperationResult());
    return;
  }
  std::move(callback).Run(FinalizeWelcomeVaultLifecycleOperation(
      maho_settings_password_helpers::UnlockVaultInCore(master_passphrase)));
}

void MahoWelcomePageHandler::GenerateSyncKey(
    GenerateSyncKeyCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run("", "", "");
    return;
  }
  char* json_str = maho_core_generate_sync_key(core);
  if (!json_str) {
    std::move(callback).Run("", "", "");
    return;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  maho::welcome::GeneratedSyncKeyResult parsed =
      maho::welcome::ParseGenerateSyncKeyJsonForTesting(json);
  std::move(callback).Run(parsed.sync_key, parsed.room_id,
                           parsed.recovery_phrase);
}

void MahoWelcomePageHandler::StartSync(
    const std::string& room_id,
    const std::string& recovery_phrase,
    StartSyncCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false, "Core not available");
    return;
  }
  char* result = maho_core_join_sync(
      core, maho::auth::GetSyncRelayUrl().c_str(), recovery_phrase.c_str());
  if (!result) {
    std::move(callback).Run(true, "");
    return;
  }
  std::string json(result);
  maho_string_free(result);

  maho::welcome::StartSyncResult parsed =
      maho::welcome::ParseStartSyncJsonForTesting(json);
  std::move(callback).Run(parsed.ok, parsed.error_message);
}

void MahoWelcomePageHandler::MailAddAccount(const std::string& request_json,
                                            MailAddAccountCallback callback) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->AddAccount(request_json, base::BindOnce(
      [](MailAddAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoWelcomePageHandler::MailTestConnection(
    const std::string& params_json,
    MailTestConnectionCallback callback) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->TestConnection(params_json, base::BindOnce(
      [](MailTestConnectionCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoWelcomePageHandler::MailBeginOAuth(const std::string& provider,
                                            MailBeginOAuthCallback callback) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable", std::string());
    return;
  }
  const std::string options_json =
      provider == "gmail"
          ? maho::auth::BuildMailOAuthStartOptionsJson(profile_->GetPrefs())
          : "{}";
  service->OAuthLoopbackSignIn(
      provider, options_json,
      base::BindOnce(
          [](base::WeakPtr<MahoWelcomePageHandler> self,
             MailBeginOAuthCallback cb, bool ok, std::string result) {
            if (!ok) {
              std::move(cb).Run(false, std::move(result), std::string());
              return;
            }
            maho::MahoMailOAuthSession::StartResult start =
                maho::MahoMailOAuthSession::ParseStartResult(result);
            if (!start.ok) {
              std::move(cb).Run(false, std::move(start.error_json),
                                std::string());
              return;
            }
            if (!self) {
              std::move(cb).Run(false, "Welcome handler unavailable",
                                std::string());
              return;
            }
            auto cancel_closure = base::BindOnce(
                [](Profile* profile, std::string state) {
                  maho::MahoMailService* service =
                      maho::MahoMailServiceFactory::GetForProfileIfExists(
                          profile);
                  if (service) {
                    service->OAuthCancel(state, base::DoNothing());
                  }
                },
                self->profile_, start.state);
            NavigateParams params(self->profile_, start.auth_url,
                                  ui::PAGE_TRANSITION_LINK);
            params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
            Navigate(&params);
            content::WebContents* oauth_contents =
                params.navigated_or_inserted_contents;
            if (!oauth_contents) {
              std::move(cancel_closure).Run();
              std::move(cb).Run(false, "Failed to open authorization tab",
                                std::string());
              return;
            }
            maho::MahoMailOAuthSession::CreateForWebContents(
                oauth_contents, start.state, std::move(cancel_closure));
            self->pending_mail_oauth_web_contents_ =
                oauth_contents->GetWeakPtr();
            std::move(cb).Run(true, std::string(), std::move(start.state));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoWelcomePageHandler::MailListAccounts(
    MailListAccountsCallback callback) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListAccounts(base::BindOnce(
      [](MailListAccountsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoWelcomePageHandler::MailDeleteAccount(
    const std::string& account_id,
    MailDeleteAccountCallback callback) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteAccount(account_id, base::BindOnce(
      [](MailDeleteAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoWelcomePageHandler::GetAiProviderConfigured(
    GetAiProviderConfiguredCallback callback) {
  PrefService* prefs = profile_->GetPrefs();
  bool configured =
      !prefs->GetString(maho::ai_prefs::kProvider).empty() ||
      !prefs->GetString(maho::ai_prefs::kApiKey).empty() ||
      !prefs->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64).empty() ||
      !prefs->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64).empty();
  std::move(callback).Run(configured);
}

void MahoWelcomePageHandler::SetTranslationProvider(
    const std::string& provider, SetTranslationProviderCallback callback) {
  if (provider != "byok" && provider != "skip") {
    std::move(callback).Run(false);
    return;
  }
  profile_->GetPrefs()->SetString(maho::ai_prefs::kTranslationProvider, provider);
  std::move(callback).Run(true);
}

void MahoWelcomePageHandler::GetByokCredentialConfigured(
    GetByokCredentialConfiguredCallback callback) {
  PrefService* prefs = profile_ ? profile_->GetPrefs() : nullptr;
  const std::string provider =
      prefs ? prefs->GetString(maho::ai_prefs::kProvider) : std::string();
  // Only the user's actually-selected BYOK provider can certify readiness;
  // the login-written "maho-managed" default and unrelated stored keys do not.
  std::move(callback).Run(IsByokProviderConfiguredForRuntime(provider, prefs));
}

void MahoWelcomePageHandler::OpenByokSettingsDialog(
    OpenByokSettingsDialogCallback callback) {
  views::Widget* parent_widget = nullptr;
  if (web_contents_) {
    parent_widget = views::Widget::GetWidgetForNativeWindow(
        web_contents_->GetTopLevelNativeWindow());
  }
  if (!parent_widget && browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    if (bv) {
      parent_widget = bv->GetWidget();
    }
  }
  gfx::NativeWindow parent_window =
      parent_widget ? parent_widget->GetNativeWindow() : gfx::NativeWindow();
  if (!parent_window) {
    std::move(callback).Run(false);
    return;
  }

  if (g_byok_settings_dialog_widget) {
    std::move(callback).Run(false);
    return;
  }

  GURL url(maho::kMahoSettingsURL);
  url = net::AppendQueryParameter(url, "pane", "maho-ai");

  auto dialog = std::make_unique<MahoByokSettingsDialogDelegate>(
      profile_, url, std::move(callback));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), parent_window, parent_widget->GetNativeView());
  g_byok_settings_dialog_widget = widget;
  widget->Show();
  widget->Activate();
}

void MahoWelcomePageHandler::GetSubscriptionCheckoutUrl(
    const std::string& tier,
    GetSubscriptionCheckoutUrlCallback callback) {
  const std::string_view checkout_url_template =
      maho::webui::SubscriptionCheckoutUrlForTier(tier);
  const std::string user_id =
      profile_ && profile_->GetPrefs()
          ? profile_->GetPrefs()->GetString(maho::account_prefs::kRelayUserId)
          : std::string();
  std::move(callback).Run(
      maho::webui::BuildSubscriptionCheckoutUrl(checkout_url_template, user_id));
}

void MahoWelcomePageHandler::MailOAuthCancel(
    const std::string& state,
    MailOAuthCancelCallback callback) {
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->OAuthCancel(state, std::move(callback));
}

void MahoWelcomePageHandler::CompletePendingMailOAuth() {
  content::WebContents* oauth_contents = pending_mail_oauth_web_contents_.get();
  pending_mail_oauth_web_contents_.reset();
  if (oauth_contents) {
    if (maho::MahoMailOAuthSession* session =
            maho::MahoMailOAuthSession::FromWebContents(oauth_contents)) {
      session->MarkComplete();
    }
    CloseMailOAuthTab(oauth_contents);
  }
  ReactivateWelcomeTab();
}

void MahoWelcomePageHandler::CloseMailOAuthTab(
    content::WebContents* oauth_contents) {
  if (!oauth_contents) {
    return;
  }
  Browser* oauth_browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(oauth_contents));
  if (oauth_browser) {
    TabStripModel* model = oauth_browser->GetTabStripModel();
    int index = model->GetIndexOfWebContents(oauth_contents);
    if (index != TabStripModel::kNoTab) {
      model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
      return;
    }
  }
  if (content::WebContentsDelegate* delegate = oauth_contents->GetDelegate()) {
    delegate->CloseContents(oauth_contents);
  }
}

void MahoWelcomePageHandler::ReactivateWelcomeTab() {
  if (!web_contents_) {
    return;
  }
  if (maho::MahoWelcomeWindow* window =
          maho::MahoWelcomeWindow::FromWebContents(web_contents_)) {
    if (views::Widget* widget = window->GetWidget()) {
      widget->Show();
      widget->Activate();
    }
    web_contents_->Focus();
    return;
  }
  Browser* welcome_browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(web_contents_));
  if (!welcome_browser) {
    return;
  }
  TabStripModel* model = welcome_browser->GetTabStripModel();
  int index = model->GetIndexOfWebContents(web_contents_);
  if (index != TabStripModel::kNoTab) {
    model->ActivateTabAt(index);
  }
  if (welcome_browser->GetWindow()) {
    welcome_browser->GetWindow()->Activate();
  }
  web_contents_->Focus();
}

void MahoWelcomePageHandler::OnAccountsChanged() {
  if (pending_mail_oauth_web_contents_) {
    CompletePendingMailOAuth();
  }
  if (page_) {
    page_->OnMailAccountsChanged();
  }
}
