// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/escape.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"  // nogncheck
#include "chrome/browser/shell_integration.h"  // nogncheck
#include "chrome/browser/sessions/session_restore.h"  // nogncheck
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_finder.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator_params.h"  // nogncheck
#include "chrome/browser/ui/browser_tabstrip.h"  // nogncheck
#include "chrome/browser/ui/startup/startup_tab.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "chrome/browser/ui/views/frame/browser_view.h"  // nogncheck
#include "chrome/grit/generated_resources.h"  // nogncheck
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/browser/importer/maho_import_session_bridge.h"
#include "maho/browser/importer/importers/essential_importer.h"
#include "maho/browser/importer/maho_space_ffi_client.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/browser/ui/views/welcome/maho_welcome_window.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/platform_util.h"  // nogncheck
#endif

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
                       weak_factory_.GetWeakPtr()),
        os_crypt_async::Encryptor::Option::kEncryptSyncCompat);
  }
}

MahoWelcomePageHandler::~MahoWelcomePageHandler() {
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintAllBrowsers();
  }
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
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_TITLE", "Set as default browser");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_BODY",
      "Make Maho your go-to browser so every link opens right where you want it.");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_YES", "Set as default");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_NO", "Maybe later");
  add("IDS_MAHO_WELCOME_SEARCH_TITLE", "Your Default Search Engine");
  add("IDS_MAHO_WELCOME_SEARCH_BODY", "Choose your default search engine. You can always change it later!");
  add("IDS_MAHO_WELCOME_ESSENTIALS_TITLE", "Your Key Tabs, Always Within Reach");
  add("IDS_MAHO_WELCOME_ESSENTIALS_BODY1", "Keep your most important tabs easily accessible and always at hand, no matter how many you open.");
  add("IDS_MAHO_WELCOME_ESSENTIALS_BODY2", "Essential tabs are always visible, no matter what workspace you are in.");
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
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_TITLE",
      "Choose your password provider");
  add("IDS_MAHO_WELCOME_PASSWORD_SETUP_BODY",
      "Select where Maho should manage passwords. Your local Vault must be "
      "unlocked whichever provider you choose.");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_TITLE", "Password provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_BODY",
      "External providers require their enabled browser extension.");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_LABEL", "Provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_ARIA", "Password provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_PLACEHOLDER",
      "Choose a provider");
  add("IDS_MAHO_WELCOME_PASSWORD_PROVIDER_UNAVAILABLE_HELPER",
      "The extension is not installed or is disabled. This selection remains "
      "saved, and Maho Native will be used until the extension is enabled.");
  add("IDS_MAHO_WELCOME_VAULT_CARD_TITLE", "Local Vault");
  add("IDS_MAHO_WELCOME_VAULT_INITIALIZE_BODY",
      "Create the local Vault used to protect password metadata and provider "
      "access.");
  add("IDS_MAHO_WELCOME_VAULT_UNLOCK_BODY",
      "Unlock the local Vault before continuing.");
  add("IDS_MAHO_WELCOME_VAULT_READY_BODY",
      "Your local Vault is unlocked and ready.");
  add("IDS_MAHO_WELCOME_VAULT_MASTER_LABEL", "Master passphrase");
  add("IDS_MAHO_WELCOME_VAULT_RECOVERY_LABEL", "Recovery secret");

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

  for (const TemplateURL* turl : service->GetTemplateURLs()) {
    if (!turl->prepopulate_id()) continue;
    std::string name_utf8 = base::UTF16ToUTF8(turl->short_name());
    std::string name_lower = base::ToLowerASCII(name_utf8);
    if (name_lower.find("wikipedia") != std::string::npos ||
        name_lower.find("ebay") != std::string::npos) {
      continue;
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
    };
    bool found = false;
    for (const auto& entry : kBundledSearchEngineFavicons) {
      if (turl->prepopulate_id() == entry.id) {
        info->icon_url = entry.path;
        found = true;
        break;
      }
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
  if (MahoSpaceThemeState::SetPreviewOverride(theme_json)) {
    MahoSpaceThemePickerDialog::RepaintAllBrowsers();
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
  MahoSpaceThemeState::ClearPreviewOverride();
  MahoSpaceThemeState::UpdateFromCore();
  MahoSpaceThemePickerDialog::RepaintAllBrowsers();
}

void MahoWelcomePageHandler::ClearThemePreview() {
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintAllBrowsers();
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

  if (!encryptor_.has_value()) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  maho::auth::MahoRelaySignup(
      profile_->GetPrefs(), url_loader_factory, *encryptor_, email, password,
      display_name,
      base::BindOnce(
          [](SignupFromWelcomeCallback cb, PrefService* prefs, bool ok,
             const std::string& error_message) {
            if (ok) {
              prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
            }
            std::move(cb).Run(ok, error_message);
          },
          std::move(callback), profile_->GetPrefs()));
}

void MahoWelcomePageHandler::OnOsCryptReady(
    os_crypt_async::Encryptor encryptor) {
  encryptor_ = std::move(encryptor);
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

  if (maho::welcome::RequiresPasswordSetup(
          profile_->GetPrefs()->GetBoolean(maho::welcome::kWelcomeCompleted)) &&
      !maho_settings_password_helpers::IsPasswordSetupComplete()) {
    LOG(ERROR) << "FinishOnboarding: Refusing to finish onboarding before "
                  "password setup completes.";
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

  DVLOG(1) << "[MahoFinish] step2 theme cleanup; HasPreview=" << MahoSpaceThemeState::HasPreviewOverride();
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintAllBrowsers();
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
  Browser* main_browser =
      chrome::FindTabbedBrowser(profile_, /*match_original_profiles=*/false);
  if (!main_browser) {
    main_browser = SessionRestore::RestoreSession(
        profile_, /*browser=*/nullptr,
        SessionRestore::SYNCHRONOUS | SessionRestore::RESTORE_BROWSER,
        StartupTabs());
  }
  if (!main_browser) {
    Browser::CreateParams params(profile_, true);
    params.creation_source = Browser::CreationSource::kStartupCreator;
    main_browser = Browser::Create(params);
  }
  main_browser->window()->Show();
  main_browser->window()->Activate();

  auto* overlay = MahoNotificationOverlay::GetOrCreateForBrowser(main_browser);
  overlay->Show(u"Welcome to Maho", u"Import complete. Start exploring.");

  // Destroys the welcome window and this handler; must be the last statement.
  CloseWelcomeTab();
}

void MahoWelcomePageHandler::OpenFDASettings() {
#if BUILDFLAG(IS_MAC)
  platform_util::OpenExternal(
      GURL("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"));
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
  TabStripModel* model = browser_->tab_strip_model();
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

  if (!encryptor_.has_value()) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  maho::auth::MahoRelayLogin(
      profile_->GetPrefs(), url_loader_factory, *encryptor_, email, password,
      base::BindOnce(
          [](LoginFromWelcomeCallback cb, PrefService* prefs, bool ok,
             const std::string& error_message) {
            if (ok && prefs->GetString(maho::ai_prefs::kProvider).empty()) {
              prefs->SetString(maho::ai_prefs::kProvider, "maho-managed");
            }
            std::move(cb).Run(ok, error_message);
          },
          std::move(callback), profile_->GetPrefs()));
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
  std::move(callback).Run(
      maho_settings_password_helpers::SetPasswordProviderInCore(provider));
}

void MahoWelcomePageHandler::GetVaultStatus(
    GetVaultStatusCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultStatusFromCore());
}

void MahoWelcomePageHandler::InitializeVault(
    const std::string& master_passphrase,
    const std::string& recovery_secret,
    InitializeVaultCallback callback) {
  std::move(callback).Run(maho_settings_password_helpers::InitializeVaultInCore(
      master_passphrase, recovery_secret));
}

void MahoWelcomePageHandler::UnlockVault(
    const std::string& master_passphrase,
    UnlockVaultCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::UnlockVaultInCore(master_passphrase));
}
