// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PAGE_HANDLER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/importer/maho_browser_detector.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

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

class MahoWelcomePageHandler : public maho_welcome::mojom::PageHandler {
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

  void FinishOnboarding() override;
  void OpenFDASettings() override;

  // Called by MahoImportSessionBridge from the UI thread to dispatch Mojo
  // progress events.
  void NotifyImportProgress(uint32_t type,
                            int32_t items_imported,
                            const std::string& error,
                            bool complete);

 private:
  void CloseWelcomeTab();
  void OnBrowsersDetected(GetAvailableBrowsersCallback callback,
                          std::vector<maho::DetectedBrowser> browsers);
  void OnOpenMigrationDialogBrowsersDetected(
      views::Widget* parent,
      OpenMigrationDialogCallback callback,
      std::vector<maho::DetectedBrowser> browsers);


  mojo::Receiver<maho_welcome::mojom::PageHandler> receiver_;
  mojo::Remote<maho_welcome::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> web_contents_;

  std::vector<maho::DetectedBrowser> detected_browsers_;
  std::unique_ptr<maho::MahoImportSessionBridge> import_session_bridge_;
  std::unique_ptr<maho::EssentialImporter> essential_importer_;
  bool is_finishing_ = false;

  base::WeakPtrFactory<MahoWelcomePageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PAGE_HANDLER_H_
