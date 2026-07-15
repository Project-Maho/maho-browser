// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_ui.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/memory/ref_counted_memory.h"
#include "base/logging.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"

// MahoMailUIConfig

MahoMailUIConfig::MahoMailUIConfig()
    : DefaultWebUIConfig("chrome", "maho-mail") {}

bool MahoMailUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return true;
}

// MahoMailHTMLSource

MahoMailHTMLSource::MahoMailHTMLSource(Profile* profile)
    : profile_(profile) {}

MahoMailHTMLSource::~MahoMailHTMLSource() = default;

std::string MahoMailHTMLSource::GetSource() {
  return "maho-mail";
}

void MahoMailHTMLSource::StartDataRequest(
    const GURL& url,
    const content::WebContents::Getter& wc_getter,
    content::URLDataSource::GotDataCallback callback) {
  std::string path(url.path());
  if (path.empty() || path == "/") {
    path = "/index.html";
  }

  // Serve static assets from the user's local workspace build directory
  base::FilePath workspace_dist(FILE_PATH_LITERAL("/Users/indo/code/project/maho-workspace/mail/dist"));
  base::FilePath file_path = workspace_dist.AppendASCII(path.substr(1));

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](const base::FilePath& file) -> std::string {
        std::string contents;
        if (!base::ReadFileToString(file, &contents)) {
          LOG(WARNING) << "[MahoMailHTMLSource] Failed to read file: " << file.value();
        }
        return contents;
      }, file_path),
      base::BindOnce([](content::URLDataSource::GotDataCallback cb, std::string contents) {
        std::move(cb).Run(base::MakeRefCounted<base::RefCountedString>(std::move(contents)));
      }, std::move(callback)));
}

std::string MahoMailHTMLSource::GetMimeType(const GURL& url) {
  std::string path(url.path());
  if (path.ends_with(".js") || path.ends_with(".mjs")) {
    return "application/javascript";
  }
  if (path.ends_with(".css")) {
    return "text/css";
  }
  if (path.ends_with(".html")) {
    return "text/html";
  }
  if (path.ends_with(".svg")) {
    return "image/svg+xml";
  }
  return "text/html";
}

// MahoMailUI

MahoMailUI::MahoMailUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::URLDataSource::Add(
      profile, std::make_unique<MahoMailHTMLSource>(profile));
}
