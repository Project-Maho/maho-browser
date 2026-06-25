// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"

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
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator_params.h"  // nogncheck
#include "chrome/browser/ui/browser_tabstrip.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "chrome/browser/ui/views/frame/browser_view.h"  // nogncheck
#include "chrome/grit/generated_resources.h"  // nogncheck
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/browser/importer/maho_import_session_bridge.h"
#include "maho/browser/importer/importers/essential_importer.h"
#include "maho/browser/importer/maho_space_ffi_client.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/browser/ui/views/welcome/maho_welcome_window.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/platform_util.h"  // nogncheck
#endif

namespace {

void RepaintBrowserAfterThemeChange(Browser* browser) {
  ui::ColorProviderManager::Get().ResetColorProviderCache();
  if (!browser) return;
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) return;
  auto* widget = browser_view->GetWidget();
  if (!widget || widget->IsClosed()) return;
  auto* root_view = static_cast<views::internal::RootView*>(
      widget->GetRootView());
  if (root_view) {
    root_view->ThemeChanged();
  }
  widget->NotifyColorProviderChanged();
}

}  // namespace

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
      web_contents_(web_contents) {}

MahoWelcomePageHandler::~MahoWelcomePageHandler() {
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    RepaintBrowserAfterThemeChange(browser_);
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
  std::string locale = g_browser_process->GetApplicationLocale();
  bool is_ko = base::StartsWith(locale, "ko", base::CompareCase::INSENSITIVE_ASCII);

  base::flat_map<std::string, std::string> strings;

  auto add = [&](const std::string& key, const std::string& en, const std::string& ko) {
    strings[key] = is_ko ? ko : en;
  };

  add("IDS_MAHO_WELCOME_SPLASH_TITLE1", "Welcome to", "환영합니다");
  add("IDS_MAHO_WELCOME_SPLASH_TITLE2", "a calmer internet", "더 조용한 인터넷");
  add("IDS_MAHO_WELCOME_IMPORT_TITLE", "A Fresh Start, Same Bookmarks", "새로운 시작, 같은 북마크");
  add("IDS_MAHO_WELCOME_IMPORT_BODY1", "Your bookmarks, history, and passwords are like a trail of breadcrumbs through the internet—don't leave them behind!", "북마크, 방문 기록, 비밀번호는 인터넷에서의 흔적입니다. 두고 가지 마세요!");
  add("IDS_MAHO_WELCOME_IMPORT_BODY2", "Easily bring them over from another browser and pick up right where you left off.", "다른 브라우저에서 간편하게 데이터를 가져와 중단했던 지점부터 다시 시작해 보세요.");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_TITLE", "Set as default browser", "기본 브라우저 설정");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_BODY",
      "Make Maho your go-to browser so every link opens right where you want it.",
      "모든 링크가 원하는 곳에서 바로 열리도록 Maho를 기본 브라우저로 설정하세요.");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_YES", "Set as default", "기본 브라우저로 설정");
  add("IDS_MAHO_WELCOME_DEFAULT_BROWSER_NO", "Maybe later", "나중에");
  add("IDS_MAHO_WELCOME_SEARCH_TITLE", "Your Default Search Engine", "기본 검색 엔진");
  add("IDS_MAHO_WELCOME_SEARCH_BODY", "Choose your default search engine. You can always change it later!", "기본 검색 엔진을 선택하세요. 나중에 언제든지 변경할 수 있습니다!");
  add("IDS_MAHO_WELCOME_ESSENTIALS_TITLE", "Your Key Tabs, Always Within Reach", "항상 손이 닿는 곳에 있는 주요 탭");
  add("IDS_MAHO_WELCOME_ESSENTIALS_BODY1", "Keep your most important tabs easily accessible and always at hand, no matter how many you open.", "얼마나 많은 탭을 열든 상관없이 가장 중요한 탭을 언제나 간편하게 이용할 수 있도록 유지하세요.");
  add("IDS_MAHO_WELCOME_ESSENTIALS_BODY2", "Essential tabs are always visible, no matter what workspace you are in.", "어떤 워크스페이스에 있든 에센셜 탭은 항상 표시됩니다.");
  add("IDS_MAHO_WELCOME_THEME_TITLE", "Choose your theme", "테마 선택");
  add("IDS_MAHO_WELCOME_THEME_BODY",
      "Personalize your browser by giving each workspace its own unique color identity.",
      "각 워크스페이스에 고유한 색상 아이덴티티를 부여해 브라우저를 나만의 스타일로 꾸며보세요.");
  add("IDS_MAHO_WELCOME_THEME_PICK", "Pick a Theme", "테마 선택");
  add("IDS_MAHO_WELCOME_THEME_CHANGE", "Change Theme", "테마 변경");
  add("IDS_MAHO_WELCOME_COMPLETION_TITLE", "You're all set", "모든 준비가 완료되었습니다");
  add("IDS_MAHO_WELCOME_COMPLETION_BODY", "Your internet is calmer now.", "이제 더 편안한 인터넷을 즐겨보세요.");
  add("IDS_MAHO_WELCOME_CTA_IMPORT", "Import now", "지금 가져오기");
  add("IDS_MAHO_WELCOME_CTA_SKIP", "Skip", "건너뛰기");
  add("IDS_MAHO_WELCOME_CTA_NEXT", "Next", "다음");
  add("IDS_MAHO_WELCOME_CTA_BACK", "Back", "이전");
  add("IDS_MAHO_WELCOME_CTA_DONE", "Dive in!", "시작하기!");

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
    RepaintBrowserAfterThemeChange(browser_);
  }
}

void MahoWelcomePageHandler::ApplyTheme(const std::string& theme_json) {
  if (browser_) {
    std::string active_space_id =
        maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser_);
    if (!active_space_id.empty()) {
      std::optional<base::Value> theme_val =
          base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
      if (theme_val) {
        base::DictValue changes;
        changes.Set("theme", std::move(*theme_val));
        std::string changes_json;
        base::JSONWriter::Write(changes, &changes_json);
        maho::MahoSpaceFFIClient::GetInstance()->UpdateSpaceConfig(active_space_id, changes_json);
      }
    }
  }
  MahoSpaceThemeState::ClearPreviewOverride();
  MahoSpaceThemeState::UpdateFromCore();
  RepaintBrowserAfterThemeChange(browser_);
}

void MahoWelcomePageHandler::ClearThemePreview() {
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    RepaintBrowserAfterThemeChange(browser_);
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

void MahoWelcomePageHandler::FinishOnboarding() {
  LOG(WARNING) << "[MahoFinish] ENTER is_finishing=" << is_finishing_;
  if (is_finishing_) {
    return;
  }
  is_finishing_ = true;

  LOG(WARNING) << "[MahoFinish] step1 SetPref kWelcomeCompleted";
  profile_->GetPrefs()->SetBoolean(maho::welcome::kWelcomeCompleted, true);

  LOG(WARNING) << "[MahoFinish] step2 theme cleanup; HasPreview=" << MahoSpaceThemeState::HasPreviewOverride();
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    RepaintBrowserAfterThemeChange(browser_);
  }

  LOG(WARNING) << "[MahoFinish] step3 Browser::Create; browser_=" << browser_ << " profile_=" << profile_;
  if (profile_) {
    Browser::CreateParams params(profile_, true);
    params.creation_source = Browser::CreationSource::kStartupCreator;
    Browser* main_browser = Browser::Create(params);
    LOG(WARNING) << "[MahoFinish] step3b main_browser=" << main_browser;
    chrome::AddTabAt(main_browser, GURL(), -1, /*foreground=*/true);
    LOG(WARNING) << "[MahoFinish] step3c NTP added";
    main_browser->window()->Show();
    LOG(WARNING) << "[MahoFinish] step4 window shown";

    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(main_browser);
    LOG(WARNING) << "[MahoFinish] step4b bv=" << bv;
    // TODO(maho): Restore overlay toast once
    // MahoNotificationOverlay::GetForBrowserView is implemented.
    (void)bv;
  }

  LOG(WARNING) << "[MahoFinish] step5 CloseWelcomeTab";
  CloseWelcomeTab();
  LOG(WARNING) << "[MahoFinish] EXIT";
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
