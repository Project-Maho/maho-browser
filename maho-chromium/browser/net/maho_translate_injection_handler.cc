// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_translate_injection_handler.h"

#include <string>
#include <string_view>

#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
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

std::string LoadClassicContentScript(int resource_id) {
  std::string script =
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          resource_id);
  constexpr std::string_view kModuleMarker = "export {};";
  const size_t marker = script.rfind(kModuleMarker);
  if (marker != std::string::npos) {
    script.erase(marker, kModuleMarker.size());
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

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoTranslateTabHelper);

MahoTranslateTabHelper::MahoTranslateTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoTranslateTabHelper>(*web_contents) {}

MahoTranslateTabHelper::~MahoTranslateTabHelper() = default;

void MahoTranslateTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted() &&
      !navigation_handle->IsSameDocument()) {
    is_translated_ = false;
  }
}

bool CanOnDeviceTranslate(content::WebContents* web_contents) {
  return web_contents &&
         web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
}

bool IsMahoPageTranslated(content::WebContents* web_contents) {
  auto* helper = web_contents
                     ? MahoTranslateTabHelper::FromWebContents(web_contents)
                     : nullptr;
  return helper && helper->is_translated();
}

void TranslatePageViaOnDevice(content::WebContents* web_contents,
                              std::string_view target_language) {
  if (!CanOnDeviceTranslate(web_contents)) {
    return;
  }
  MahoTranslateTabHelper::CreateForWebContents(web_contents);
  auto* helper = MahoTranslateTabHelper::FromWebContents(web_contents);

  if (helper && helper->is_translated()) {
    helper->set_is_translated(false);
    web_contents->GetController().Reload(content::ReloadType::NORMAL,
                                         /*check_for_repost=*/false);
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
    helper->set_is_translated(true);
  }
}

}  // namespace maho
