// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_boost_injection_handler.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "ui/base/resource/resource_bundle.h"
#include "chrome/grit/maho_boost_content_script_resources.h"
#include "url/gurl.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoBoostInjectionHandler);

namespace {

constexpr int kIsolatedWorldIdForBoost =
    content::ISOLATED_WORLD_ID_CONTENT_END + 4;

constexpr base::TimeDelta kPollInterval = base::Milliseconds(100);

std::string BuildBoostCssScript(const std::string& css) {
  if (css.empty()) {
    return "(function() {"
           "  var existing = document.getElementById('maho-boost-style');"
           "  if (existing) { existing.remove(); }"
           "})();";
  }

  std::string escaped;
  base::ReplaceChars(css, "\\", "\\\\", &escaped);
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "${", "\\${");
  base::ReplaceChars(escaped, "`", "\\`", &escaped);

  return base::StrCat({
      "(function() {"
      "  var existing = document.getElementById('maho-boost-style');"
      "  if (existing) {"
      "    existing.textContent = `",
      escaped,
      "`;"
      "    return;"
      "  }"
      "  var s = document.createElement('style');"
      "  s.id = 'maho-boost-style';"
      "  s.textContent = `",
      escaped,
      "`;"
      "  (document.head || document.documentElement).appendChild(s);"
      "})();"});
}

std::string ExtractBoostDomainFromUpdate(const base::DictValue& update,
                                         const std::string& kind) {
  if (kind == "boost_updated") {
    const base::DictValue* boost = update.FindDict("boost");
    if (!boost) {
      return std::string();
    }
    const std::string* domain = boost->FindString("domain");
    return domain ? *domain : std::string();
  }

  if (kind == "boost_deleted" || kind == "boost_active_changed") {
    const std::string* domain = update.FindString("domain");
    return domain ? *domain : std::string();
  }

  return std::string();
}

void ReinjectBoostIntoTabsForDomain(const std::string& domain) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (domain.empty()) {
    LOG(WARNING) << "[MahoBoost] ReinjectBoostIntoTabsForDomain: empty domain";
    return;
  }
  int matched_tabs = 0;
  const std::string dot_domain = "." + domain;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&domain, &dot_domain, &matched_tabs](BrowserWindowInterface* bwi) {
        TabStripModel* model = bwi->GetTabStripModel();
        if (!model) {
          return true;
        }
        for (int i = 0; i < model->count(); ++i) {
          content::WebContents* wc = model->GetWebContentsAt(i);
          if (!wc) {
            continue;
          }
          const GURL& url = wc->GetLastCommittedURL();
          if (!url.SchemeIsHTTPOrHTTPS()) {
            continue;
          }
          const std::string host(url.host());
          if (host != domain &&
              !base::EndsWith(host, dot_domain,
                              base::CompareCase::INSENSITIVE_ASCII)) {
            continue;
          }
          ++matched_tabs;
          MahoBoostInjectionHandler::CreateForWebContents(wc);
          auto* handler = MahoBoostInjectionHandler::FromWebContents(wc);
          if (handler) {
            handler->InjectBoostCssIntoTab(wc->GetPrimaryMainFrame(),
                                           url.spec());
          }
        }
        return true;
      });
  VLOG(1) << "[MahoBoost] ReinjectBoostIntoTabsForDomain: domain=" << domain
          << " matched_tabs=" << matched_tabs;
}

void HandleOneCoreUpdate(const base::DictValue& update) {
  const std::string* kind = update.FindString("kind");
  if (!kind) {
    return;
  }
  if (*kind != "boost_updated" && *kind != "boost_active_changed" &&
      *kind != "boost_deleted") {
    return;
  }
  const std::string domain = ExtractBoostDomainFromUpdate(update, *kind);
  ReinjectBoostIntoTabsForDomain(domain);
}

std::string BuildZapSelectorsJson(
    const std::vector<std::string>& selectors) {
  std::string json = "[";
  for (size_t i = 0; i < selectors.size(); ++i) {
    if (i > 0) json += ",";
    std::string escaped = selectors[i];
    base::ReplaceChars(escaped, "\\", "\\\\", &escaped);
    base::ReplaceChars(escaped, "\"", "\\\"", &escaped);
    json += "\"" + escaped + "\"";
  }
  json += "]";
  return json;
}

}  // namespace

// static
void MahoBoostInjectionHandler::OnCoreUpdatesJson(
    const std::string& updates_json) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (updates_json.empty() || updates_json == "{}") {
    return;
  }
  if (updates_json.find("boost_") == std::string::npos) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(updates_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return;
  }
  if (parsed->is_list()) {
    for (const auto& item : parsed->GetList()) {
      const base::DictValue* dict = item.GetIfDict();
      if (dict) {
        HandleOneCoreUpdate(*dict);
      }
    }
  } else {
    const base::DictValue* dict = parsed->GetIfDict();
    if (dict) {
      HandleOneCoreUpdate(*dict);
    }
  }
}

// static
void MahoBoostInjectionHandler::ReinjectForDomain(
    const std::string& domain) {
  ReinjectBoostIntoTabsForDomain(domain);
}

MahoBoostInjectionHandler::MahoBoostInjectionHandler(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoBoostInjectionHandler>(*web_contents) {}

MahoBoostInjectionHandler::~MahoBoostInjectionHandler() {
  StopPolling();
}

void MahoBoostInjectionHandler::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted() ||
      navigation_handle->IsErrorPage() ||
      !navigation_handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }

  if (!navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }

  content_script_injected_ = false;
  StopPolling();

  InjectBoostCss(navigation_handle->GetRenderFrameHost(),
                 navigation_handle->GetURL().spec());
}

void MahoBoostInjectionHandler::InjectBoostCssIntoTab(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InjectBoostCss(render_frame_host, url);
}

void MahoBoostInjectionHandler::InjectBoostCss(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !render_frame_host || !render_frame_host->IsRenderFrameLive() ||
      !render_frame_host->IsActive()) {
    LOG(WARNING) << "[MahoBoost] InjectBoostCss: early return — "
                 << "core=" << (core ? "ok" : "null")
                 << " rfh=" << (render_frame_host ? "ok" : "null")
                 << " url=" << url;
    return;
  }

  std::string css = maho::core::GetBoostInjectionCss(core, url.c_str());
  VLOG(1) << "[MahoBoost] InjectBoostCss: url=" << url
          << " css_length=" << css.size();
  std::string script = BuildBoostCssScript(css);
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::DoNothing(),
      kIsolatedWorldIdForBoost);
}

void MahoBoostInjectionHandler::InjectContentScript() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (content_script_injected_) {
    return;
  }

  content::WebContents* wc = web_contents();
  if (!wc) return;
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || !rfh->IsActive()) return;

  // Load CSS from GRD and inject as global for Shadow DOM consumption.
  std::string css = ui::ResourceBundle::GetSharedInstance()
      .LoadDataResourceString(
          IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_CSS);
  DCHECK(!css.empty());
  std::string escaped_css;
  base::ReplaceChars(css, "\\", "\\\\", &escaped_css);
  base::ReplaceChars(escaped_css, "`", "\\`", &escaped_css);
  base::ReplaceSubstringsAfterOffset(&escaped_css, 0, "${", "\\${");
  std::string css_inject = base::StrCat({
      "window.__mahoBoost = window.__mahoBoost || {};",
      "window.__mahoBoostCSS = `", escaped_css, "`;"});
  ExecuteInIsolatedWorld(css_inject);

  // Load JS content scripts in dependency order.
  // selector_component.js must precede zap_overlay.js (reads ns.SelectorComponent).
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_SELECTOR_COMPONENT_JS));
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_ZAP_OVERLAY_JS));
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_JS));

  content_script_injected_ = true;
}

void MahoBoostInjectionHandler::ExecuteInIsolatedWorld(
    const std::string& script) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContents* wc = web_contents();
  if (!wc) return;
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || !rfh->IsActive()) return;

  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::DoNothing(),
      kIsolatedWorldIdForBoost);
}

void MahoBoostInjectionHandler::EnterZapMode(
    const std::vector<std::string>& zap_selectors,
    ContentScriptEventCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content_script_mode_ = ContentScriptMode::kZap;
  event_callback_ = std::move(callback);

  InjectContentScript();

  std::string selectors_json = BuildZapSelectorsJson(zap_selectors);
  ExecuteInIsolatedWorld(
      "window.__mahoBoost && window.__mahoBoost.enterZapMode(" +
      selectors_json + ");");

  if (!poll_timer_.IsRunning()) {
    poll_timer_.Start(FROM_HERE, kPollInterval,
                      base::BindRepeating(
                          &MahoBoostInjectionHandler::PollContentScript,
                          weak_factory_.GetWeakPtr()));
  }
}

void MahoBoostInjectionHandler::ExitZapMode() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (content_script_mode_ != ContentScriptMode::kZap) return;

  ExecuteInIsolatedWorld(
      "window.__mahoBoost && window.__mahoBoost.exitZapMode();");

  content_script_mode_ = ContentScriptMode::kNone;
  StopPolling();
}

void MahoBoostInjectionHandler::EnterPickerMode(
    ContentScriptEventCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content_script_mode_ = ContentScriptMode::kPicker;
  event_callback_ = std::move(callback);

  InjectContentScript();

  ExecuteInIsolatedWorld(
      "window.__mahoBoost && window.__mahoBoost.enterPickerMode();");

  if (!poll_timer_.IsRunning()) {
    poll_timer_.Start(FROM_HERE, kPollInterval,
                      base::BindRepeating(
                          &MahoBoostInjectionHandler::PollContentScript,
                          weak_factory_.GetWeakPtr()));
  }
}

void MahoBoostInjectionHandler::ExitPickerMode() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (content_script_mode_ != ContentScriptMode::kPicker) return;

  ExecuteInIsolatedWorld(
      "window.__mahoBoost && window.__mahoBoost.exitPickerMode();");

  content_script_mode_ = ContentScriptMode::kNone;
  StopPolling();
}

void MahoBoostInjectionHandler::PollContentScript() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContents* wc = web_contents();
  if (!wc) {
    StopPolling();
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || !rfh->IsActive()) {
    StopPolling();
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"(function() {"
      u"  if (!window.__mahoBoost || !window.__mahoBoost.drain) return [];"
      u"  return window.__mahoBoost.drain();"
      u"})()",
      base::BindOnce(&MahoBoostInjectionHandler::OnPollResult,
                      weak_factory_.GetWeakPtr()),
      kIsolatedWorldIdForBoost);
}

void MahoBoostInjectionHandler::OnPollResult(base::Value result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result.is_list() || !event_callback_) return;

  for (const auto& item : result.GetList()) {
    const base::DictValue* msg = item.GetIfDict();
    if (!msg) continue;

    const std::string* type = msg->FindString("type");
    if (!type) continue;

    const std::string* selector = msg->FindString("selector");
    const std::string* notify_msg = msg->FindString("msg");

    event_callback_.Run(
        *type,
        selector ? *selector : std::string(),
        notify_msg ? *notify_msg : std::string());
  }
}

void MahoBoostInjectionHandler::StopPolling() {
  poll_timer_.Stop();
  event_callback_.Reset();
}
