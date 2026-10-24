// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_translate_injection_handler.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/unguessable_token.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/grit/maho_translate_content_script_resources.h"
#include "components/prefs/pref_service.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
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
  if (!profile) {
    return "en";
  }
  // Resolve exactly like the header, the popover and auto-translate
  // (TranslateManager::GetTargetLanguage): forced target, auto-pair, recent
  // target, UI locale, then accept languages. The context menu and the header
  // must never disagree about the target language.
  std::unique_ptr<translate::TranslatePrefs> prefs =
      ChromeTranslateClient::CreateTranslatePrefs(profile->GetPrefs());
  return translate::TranslateManager::GetTargetLanguage(
      prefs.get(), /*language_model=*/nullptr);
}

// The content script compares base language codes ("en" for "en-US").
std::string BaseLanguage(std::string_view code) {
  return base::ToLowerASCII(code.substr(0, code.find('-')));
}

bool IsOnDeviceTranslatableUrl(content::WebContents* web_contents) {
  return web_contents &&
         web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
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
    request_token_.clear();
    target_language_.clear();
    // The new document starts untranslated; the old document's translated
    // text is gone with it.
    has_translated_content_ = false;
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
  // Page scripts share the console with the isolated world, so a message is
  // only trusted when it comes from the primary main frame (the only frame the
  // script is injected into) and carries the token of the current request,
  // which never leaves the browser and the isolated world.
  if (request_token_.empty() || !source_frame ||
      source_frame != web_contents()->GetPrimaryMainFrame()) {
    return;
  }
  const std::string payload =
      base::UTF16ToUTF8(message.substr(kStateMessagePrefix.size()));
  const std::vector<std::string> parts = base::SplitString(
      payload, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() < 2 || parts[0] != request_token_) {
    return;
  }
  const std::optional<MahoTranslateState> state = ParseStateName(parts[1]);
  if (!state) {
    return;
  }
  SetState(*state, parts.size() > 2 ? parts[2] : std::string());
}

void MahoTranslateTabHelper::SetState(MahoTranslateState state,
                                      std::string source_language) {
  // The flag means "the page is showing translated text right now", not "it
  // was translated once". The content script restores the original text before
  // every request (content_script.ts restoreUnits, ahead of report('translating')),
  // so a new request must clear the flag at the same moment: a failed retry or
  // re-target then reports kError over ORIGINAL text, and leaving the flag set
  // would show a "Translated to X" popover with a "Show original" button that
  // restores nothing. kTranslated re-latches it as soon as the swap happens, so
  // a page that really is translated keeps the restore route.
  if (state == MahoTranslateState::kTranslating) {
    has_translated_content_ = false;
  }
  if (state == MahoTranslateState::kTranslated) {
    has_translated_content_ = true;
  }
  if (state_ == state && source_language_ == source_language) {
    return;
  }
  state_ = state;
  source_language_ = std::move(source_language);
  state_changed_callbacks_.Notify();
}

bool CanOnDeviceTranslate(content::WebContents* web_contents) {
  if (!IsOnDeviceTranslatableUrl(web_contents)) {
    return false;
  }
  // A page already showing translated text can always be translated back,
  // including after a later failure (kError) left the translated text on
  // screen.
  if (HasMahoTranslatedContent(web_contents)) {
    return true;
  }
  // Translating a page that is already in the target language is a no-op, so
  // it is not offered. While detection has not finished (or is
  // inconclusive) the page is still offered; the content script reports a
  // no-op instead of claiming success.
  ChromeTranslateClient* client =
      ChromeTranslateClient::FromWebContents(web_contents);
  const std::string page_language =
      client ? client->GetLanguageState().source_language() : std::string();
  if (page_language.empty() || page_language == "und") {
    return true;
  }
  return BaseLanguage(page_language) !=
         BaseLanguage(ResolveFallbackTargetLanguage(web_contents));
}

bool IsMahoPageTranslated(content::WebContents* web_contents) {
  return GetMahoTranslateState(web_contents) ==
         MahoTranslateState::kTranslated;
}

bool HasMahoTranslatedContent(content::WebContents* web_contents) {
  auto* helper = web_contents
                     ? MahoTranslateTabHelper::FromWebContents(web_contents)
                     : nullptr;
  return helper && helper->has_translated_content();
}

MahoTranslateState GetMahoTranslateState(content::WebContents* web_contents) {
  auto* helper = web_contents
                     ? MahoTranslateTabHelper::FromWebContents(web_contents)
                     : nullptr;
  return helper ? helper->state() : MahoTranslateState::kIdle;
}

void TranslatePageViaOnDevice(content::WebContents* web_contents,
                              std::string_view target_language) {
  if (!IsOnDeviceTranslatableUrl(web_contents)) {
    return;
  }
  MahoTranslateTabHelper::CreateForWebContents(web_contents);
  auto* helper = MahoTranslateTabHelper::FromWebContents(web_contents);

  const std::string target = BaseLanguage(
      target_language.empty() ? ResolveFallbackTargetLanguage(web_contents)
                              : std::string(target_language));
  if (helper->has_translated_content()) {
    // Without an explicit target (the page context menu) the command toggles
    // back to the original, which must also work when the state is kError but
    // the translated text is still on screen. An explicit target that is
    // already shown is a no-op; a different one re-translates, which the
    // content script does from the original text. After a failure an explicit
    // request is a retry rather than a no-op.
    if (target_language.empty()) {
      RestoreOriginalPage(web_contents);
      return;
    }
    if (target == helper->target_language_ && helper->is_translated()) {
      return;
    }
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

  helper->request_token_ = base::UnguessableToken::Create().ToString();
  helper->target_language_ = target;
  const std::string call = base::StrCat(
      {"window.__mahoTranslate && window.__mahoTranslate.translatePage('",
       EscapeJsString(target), "', '", helper->request_token_, "');"});
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(call), base::DoNothing(), kIsolatedWorldId);

  helper->SetState(MahoTranslateState::kTranslating, std::string());
}

void RestoreOriginalPage(content::WebContents* web_contents) {
  if (!IsOnDeviceTranslatableUrl(web_contents)) {
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
    helper->target_language_.clear();
    helper->has_translated_content_ = false;
    helper->SetState(MahoTranslateState::kIdle, std::string());
  }
}

}  // namespace maho
