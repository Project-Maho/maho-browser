// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_TEST_MAHO_TEST_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_TEST_MAHO_TEST_UI_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

class Profile;

namespace content {
class WebUI;
}  // namespace content

class MahoTestUI;

namespace content {
class BrowserContext;
}

class MahoTestUIConfig : public content::DefaultWebUIConfig<MahoTestUI> {
 public:
  MahoTestUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoTestHTMLSource : public content::URLDataSource {
 public:
  explicit MahoTestHTMLSource(Profile* profile);

  MahoTestHTMLSource(const MahoTestHTMLSource&) = delete;
  MahoTestHTMLSource& operator=(const MahoTestHTMLSource&) = delete;

  ~MahoTestHTMLSource() override;

  // content::URLDataSource:
  std::string GetSource() override;
  void StartDataRequest(
      const GURL& url,
      const content::WebContents::Getter& wc_getter,
      content::URLDataSource::GotDataCallback callback) override;
  std::string GetMimeType(const GURL& url) override;

 private:
  raw_ptr<Profile> profile_;
};

class MahoTestUI : public content::WebUIController {
 public:
  explicit MahoTestUI(content::WebUI* web_ui);

  MahoTestUI(const MahoTestUI&) = delete;
  MahoTestUI& operator=(const MahoTestUI&) = delete;

  ~MahoTestUI() override = default;
};

void SeedRuntimeVerificationState(Profile* profile, int level);

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_TEST_MAHO_TEST_UI_H_
