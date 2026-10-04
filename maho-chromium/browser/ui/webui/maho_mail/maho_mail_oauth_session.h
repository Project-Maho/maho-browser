// Copyright 2026 Maho Browser. All rights reserved.

#ifndef CHROME_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_OAUTH_SESSION_H_
#define CHROME_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_OAUTH_SESSION_H_

#include <string>

#include "base/functional/callback.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace maho {

class MahoMailOAuthSession : public content::WebContentsUserData<MahoMailOAuthSession> {
 public:
  struct StartResult {
    StartResult();
    StartResult(const StartResult&);
    StartResult& operator=(const StartResult&);
    StartResult(StartResult&&) noexcept;
    StartResult& operator=(StartResult&&) noexcept;
    ~StartResult();

    bool ok = false;
    std::string error_json;
    std::string state;
    GURL auth_url;
  };

  ~MahoMailOAuthSession() override;

  static void CreateForWebContents(content::WebContents* web_contents,
                                   std::string state,
                                   base::OnceClosure cancel_closure);

  const std::string& state() const { return state_; }

  void MarkComplete();
  static bool MarkCompleteForWebContents(content::WebContents* web_contents,
                                         const std::string& state);

  static bool IsLoopbackSuccess(int response_code);
  static bool IsValidAuthorizationUrl(const GURL& auth_url);
  static StartResult ParseStartResult(const std::string& result_json);

 private:
  friend class content::WebContentsUserData<MahoMailOAuthSession>;
  explicit MahoMailOAuthSession(content::WebContents* web_contents,
                                std::string state,
                                base::OnceClosure cancel_closure);

  std::string state_;
  base::OnceClosure cancel_closure_;
  bool complete_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace maho

#endif  // CHROME_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_OAUTH_SESSION_H_
