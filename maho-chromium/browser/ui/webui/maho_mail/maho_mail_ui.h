// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_UI_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

class Profile;

class MahoMailUI;

class MahoMailUIConfig : public content::DefaultWebUIConfig<MahoMailUI> {
 public:
  MahoMailUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoMailHTMLSource : public content::URLDataSource {
 public:
  explicit MahoMailHTMLSource(Profile* profile);

  MahoMailHTMLSource(const MahoMailHTMLSource&) = delete;
  MahoMailHTMLSource& operator=(const MahoMailHTMLSource&) = delete;

  ~MahoMailHTMLSource() override;

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

class MahoMailUI : public content::WebUIController {
 public:
  explicit MahoMailUI(content::WebUI* web_ui);

  MahoMailUI(const MahoMailUI&) = delete;
  MahoMailUI& operator=(const MahoMailUI&) = delete;

  ~MahoMailUI() override = default;
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_UI_H_
