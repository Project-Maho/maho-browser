// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_oauth_session.h"

#include "base/json/json_reader.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "url/url_constants.h"

namespace maho {

MahoMailOAuthSession::StartResult::StartResult() = default;
MahoMailOAuthSession::StartResult::StartResult(const StartResult&) = default;
MahoMailOAuthSession::StartResult&
MahoMailOAuthSession::StartResult::operator=(const StartResult&) = default;
MahoMailOAuthSession::StartResult::StartResult(StartResult&&) noexcept =
    default;
MahoMailOAuthSession::StartResult&
MahoMailOAuthSession::StartResult::operator=(StartResult&&) noexcept =
    default;
MahoMailOAuthSession::StartResult::~StartResult() = default;

MahoMailOAuthSession::MahoMailOAuthSession(content::WebContents* web_contents,
                                           std::string state,
                                           base::OnceClosure cancel_closure)
    : content::WebContentsUserData<MahoMailOAuthSession>(*web_contents),
      state_(std::move(state)),
      cancel_closure_(std::move(cancel_closure)) {}

MahoMailOAuthSession::~MahoMailOAuthSession() {
  if (!complete_ && !cancel_closure_.is_null()) {
    std::move(cancel_closure_).Run();
  }
}

void MahoMailOAuthSession::CreateForWebContents(content::WebContents* web_contents,
                                                std::string state,
                                                base::OnceClosure cancel_closure) {
  DCHECK(web_contents);
  web_contents->SetUserData(
      UserDataKey(),
      base::WrapUnique(new MahoMailOAuthSession(web_contents, std::move(state),
                                                std::move(cancel_closure))));
}

void MahoMailOAuthSession::MarkComplete() {
  complete_ = true;
}

bool MahoMailOAuthSession::MarkCompleteForWebContents(
    content::WebContents* web_contents,
    const std::string& state) {
  if (!web_contents) {
    return false;
  }
  MahoMailOAuthSession* session = FromWebContents(web_contents);
  if (!session || session->state() != state) {
    return false;
  }
  session->MarkComplete();
  return true;
}

bool MahoMailOAuthSession::IsLoopbackSuccess(int response_code) {
  return response_code == 200;
}

bool MahoMailOAuthSession::IsValidAuthorizationUrl(const GURL& auth_url) {
  return auth_url.is_valid() && auth_url.SchemeIs(url::kHttpsScheme);
}

MahoMailOAuthSession::StartResult MahoMailOAuthSession::ParseStartResult(
    const std::string& result_json) {
  auto parsed = base::JSONReader::ReadDict(result_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    StartResult result;
    result.error_json = "Invalid OAuth response";
    return result;
  }

  const std::string* auth_url_string = parsed->FindString("auth_url");
  if (!auth_url_string || auth_url_string->empty()) {
    StartResult result;
    result.error_json = "No authorization URL";
    return result;
  }
  GURL auth_url(*auth_url_string);
  if (!IsValidAuthorizationUrl(auth_url)) {
    StartResult result;
    result.error_json = "Invalid authorization URL";
    return result;
  }

  const std::string* state = parsed->FindString("state");
  if (!state || state->empty()) {
    StartResult result;
    result.error_json = "No OAuth state";
    return result;
  }

  StartResult result;
  result.ok = true;
  result.state = *state;
  result.auth_url = auth_url;
  return result;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoMailOAuthSession);

}  // namespace maho
