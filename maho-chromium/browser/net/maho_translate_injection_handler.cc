// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_translate_injection_handler.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_translate_content_script_resources.h"
#include "components/prefs/pref_service.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace maho {
namespace {

constexpr int kIsolatedWorldId = content::ISOLATED_WORLD_ID_CONTENT_END + 9;

constexpr std::u16string_view kStateMessagePrefix =
    u"__mahoTranslate:state:";

std::string LoadClassicContentScript(int resource_id) {
  std::string script =
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          resource_id);
  // Release builds minify the trailing tsc module marker to `export{};`;
  // both forms are a SyntaxError in a classic script.
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(script, base::TRIM_TRAILING);
  for (std::string_view marker : {"export {};", "export{};"}) {
    if (base::EndsWith(trimmed, marker)) {
      script.resize(trimmed.size() - marker.size());
      break;
    }
  }
  return script;
}

std::string EscapeJsString(std::string value) {
  std::string escaped;
  base::ReplaceChars(value, "\\", "\\\\", &escaped);
  base::ReplaceChars(escaped, "'", "\\'", &escaped);
  return escaped;
}

std::string ResolveFallbackTargetLanguage(content::WebContents* web_contents) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  std::string target = profile->GetPrefs()->GetString(
      translate::prefs::kPrefTranslateRecentTarget);
  return target.empty() ? "en" : target;
}

std::optional<MahoTranslateState> ParseStateName(std::string_view name) {
  if (name == "idle") {
    return MahoTranslateState::kIdle;
  }
  if (name == "translating") {
    return MahoTranslateState::kTranslating;
  }
  if (name == "translated") {
    return MahoTranslateState::kTranslated;
  }
  if (name == "error") {
    return MahoTranslateState::kError;
  }
  return std::nullopt;
}

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoTranslateTabHelper);

MahoTranslateTabHelper::MahoTranslateTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoTranslateTabHelper>(*web_contents) {}

MahoTranslateTabHelper::~MahoTranslateTabHelper() = default;

base::CallbackListSubscription MahoTranslateTabHelper::AddStateChangedCallback(
    base::RepeatingClosure callback) {
  return state_changed_callbacks_.Add(std::move(callback));
}

void MahoTranslateTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted() &&
      !navigation_handle->IsSameDocument()) {
    SetState(MahoTranslateState::kIdle, std::string());
  }
}

void MahoTranslateTabHelper::OnDidAddMessageToConsole(
    content::RenderFrameHost* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id,
    const std::optional<std::u16string>& untrusted_stack_trace) {
  if (!base::StartsWith(message, kStateMessagePrefix)) {
    return;
  }
  const std::string payload =
      base::UTF16ToUTF8(message.substr(kStateMessagePrefix.size()));
  const std::vector<std::string> parts = base::SplitString(
      payload, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.empty()) {
    return;
  }
  const std::optional<MahoTranslateState> state = ParseStateName(parts[0]);
  if (!state) {
    return;
  }
  SetState(*state, parts.size() > 1 ? parts[1] : std::string());
}

void MahoTranslateTabHelper::SetState(MahoTranslateState state,
                                      std::string source_language) {
  if (state_ == state && source_language_ == source_language) {
    return;
  }
  state_ = state;
  source_language_ = std::move(source_language);
  state_changed_callbacks_.Notify();
}

bool CanOnDeviceTranslate(content::WebContents* web_contents) {
  return web_contents &&
         web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
}

bool IsMahoPageTranslated(content::WebContents* web_contents) {
  return GetMahoTranslateState(web_contents) ==
         MahoTranslateState::kTranslated;
}

MahoTranslateState GetMahoTranslateState(content::WebContents* web_contents) {
  auto* helper = web_contents
                     ? MahoTranslateTabHelper::FromWebContents(web_contents)
                     : nullptr;
  return helper ? helper->state() : MahoTranslateState::kIdle;
}

void TranslatePageViaOnDevice(content::WebContents* web_contents,
                              std::string_view target_language) {
  if (!CanOnDeviceTranslate(web_contents)) {
    return;
  }
  MahoTranslateTabHelper::CreateForWebContents(web_contents);
  auto* helper = MahoTranslateTabHelper::FromWebContents(web_contents);

  if (helper && helper->is_translated()) {
    RestoreOriginalPage(web_contents);
    return;
  }

  content::RenderFrameHost* render_frame_host =
      web_contents->GetPrimaryMainFrame();
  if (!render_frame_host || !render_frame_host->IsRenderFrameLive()) {
    return;
  }

  // The built-in Translator/LanguageDetector require transient user activation
  // to download an on-device model. The sidebar/context-menu invocation is a
  // real user gesture; propagate it to the frame so create() is permitted.
  render_frame_host->NotifyUserActivation(
      blink::mojom::UserActivationNotificationType::kInteraction);

  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(LoadClassicContentScript(
          IDR_MAHO_TRANSLATE_CONTENT_SCRIPT_CONTENT_SCRIPT_JS)),
      base::DoNothing(), kIsolatedWorldId);

  const std::string call = base::StrCat(
      {"window.__mahoTranslate && window.__mahoTranslate.translatePage('",
       EscapeJsString(target_language.empty()
                          ? ResolveFallbackTargetLanguage(web_contents)
                          : std::string(target_language)),
       "');"});
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(call), base::DoNothing(), kIsolatedWorldId);

  if (helper) {
    helper->SetState(MahoTranslateState::kTranslating, std::string());
  }
}

void RestoreOriginalPage(content::WebContents* web_contents) {
  if (!CanOnDeviceTranslate(web_contents)) {
    return;
  }
  content::RenderFrameHost* render_frame_host =
      web_contents->GetPrimaryMainFrame();
  if (!render_frame_host || !render_frame_host->IsRenderFrameLive()) {
    return;
  }
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      u"window.__mahoTranslate && window.__mahoTranslate.restorePage();",
      base::DoNothing(), kIsolatedWorldId);

  if (auto* helper = MahoTranslateTabHelper::FromWebContents(web_contents)) {
    helper->SetState(MahoTranslateState::kIdle, std::string());
  }
}

}  // namespace maho
