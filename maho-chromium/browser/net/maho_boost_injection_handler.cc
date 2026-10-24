// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_boost_injection_handler.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/lru_cache.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
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

constexpr base::TimeDelta kPollInterval = base::Milliseconds(25);

constexpr size_t kMaxCachedBoostHosts = 64;

// Last known Boost style script per host. Fetching the CSS is a round trip to
// the core sequence, during which the new page paints with zapped elements
// still visible; replaying the cached script at commit closes that window and
// the fetched result then replaces it. Entries belong to one core generation.
struct BoostCssCache {
  uint64_t generation = 0;
  // Bumped on every invalidation so a fetch that started before a Boost
  // changed cannot re-cache the outdated style.
  uint64_t epoch = 0;
  base::LRUCache<std::string, std::string> scripts{kMaxCachedBoostHosts};
};

BoostCssCache& GetBoostCssCache() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  static base::NoDestructor<BoostCssCache> cache;
  if (cache->generation != maho::GetCoreGeneration()) {
    cache->generation = maho::GetCoreGeneration();
    cache->scripts.Clear();
  }
  return *cache;
}

// Records the fetched style for |host|; an empty CSS result drops the entry so
// a deleted or disabled Boost is never replayed.
void UpdateCachedBoostCss(const std::string& host,
                          uint64_t epoch,
                          const std::string& css,
                          const std::string& script) {
  BoostCssCache& cache = GetBoostCssCache();
  if (host.empty() || epoch != cache.epoch) {
    return;
  }
  auto& scripts = cache.scripts;
  if (css.empty()) {
    auto it = scripts.Peek(host);
    if (it != scripts.end()) {
      scripts.Erase(it);
    }
    return;
  }
  scripts.Put(host, script);
}

// Drops cached styles for |domain| and its subdomains after a Boost changed,
// including hosts with no open tab to re-inject.
void InvalidateCachedBoostCss(const std::string& domain) {
  BoostCssCache& cache = GetBoostCssCache();
  ++cache.epoch;
  auto& scripts = cache.scripts;
  const std::string dot_domain = "." + domain;
  for (auto it = scripts.begin(); it != scripts.end();) {
    if (it->first == domain ||
        base::EndsWith(it->first, dot_domain,
                       base::CompareCase::INSENSITIVE_ASCII)) {
      it = scripts.Erase(it);
    } else {
      ++it;
    }
  }
}

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
      // The cached style is replayed right at commit, possibly before the
      // parser created the root element; attach as soon as it exists.
      "  var root = document.head || document.documentElement;"
      "  if (root) { root.appendChild(s); return; }"
      "  var mo = new MutationObserver(function() {"
      "    var r = document.documentElement;"
      "    if (!r) { return; }"
      "    mo.disconnect();"
      "    if (!document.getElementById('maho-boost-style')) {"
      "      (document.head || r).appendChild(s);"
      "    }"
      "  });"
      "  mo.observe(document, {childList: true});"
      "})();"});
}

std::string LoadClassicContentScript(int resource_id) {
  std::string script = ui::ResourceBundle::GetSharedInstance()
                           .LoadDataResourceString(resource_id);
  // tsc emits a trailing ES-module marker that is a SyntaxError when the file
  // runs as a classic script. Debug builds keep tsc's `export {};`; release
  // builds pass through terser, which emits `export{};` (no space). Missing
  // the minified form made every content script fail to parse, so Zap and
  // the element picker never appeared.
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
  InvalidateCachedBoostCss(domain);
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

void ExecuteBoostCssScript(content::RenderFrameHost* frame,
                           const std::string& script) {
  if (frame->GetLastCommittedURL().SchemeIs("chrome-error")) {
    frame->ExecuteJavaScript(base::UTF8ToUTF16(script), base::DoNothing());
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), base::DoNothing(), kIsolatedWorldIdForBoost);
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
  ExitCurrentEditMode();
}

void MahoBoostInjectionHandler::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  TRACE_EVENT0("browser", "BoostInjection.DidFinishNavigation");
  if (!navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }

  content_script_injected_ = false;
  StopPolling();
  content_script_mode_ = ContentScriptMode::kNone;

  if (!navigation_handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }

  DVLOG(1) << "[MAHO_PERF] BoostInjection.DidFinishNavigation url="
           << navigation_handle->GetURL().spec();

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

  const std::string host(GURL(url).host());
  const uint64_t cache_epoch = GetBoostCssCache().epoch;
  if (!host.empty()) {
    auto& scripts = GetBoostCssCache().scripts;
    auto cached = scripts.Get(host);
    if (cached != scripts.end()) {
      ExecuteBoostCssScript(render_frame_host, cached->second);
    }
  }

  const uint64_t generation = maho::GetCoreGeneration();
  maho::PostCoreTask<std::pair<std::string, std::string>>(
      FROM_HERE,
      base::BindOnce([](uint64_t generation, std::string url) {
        if (generation != maho::GetCoreGeneration() || !maho::GetCore()) {
          return std::pair<std::string, std::string>();
        }
        std::string css =
            maho::core::GetBoostInjectionCss(maho::GetCore(), url.c_str());
        std::string script = BuildBoostCssScript(css);
        return std::make_pair(std::move(css), std::move(script));
      }, generation, url),
      base::BindOnce([](base::WeakPtr<MahoBoostInjectionHandler> owner,
                        content::WeakDocumentPtr document, uint64_t generation,
                        std::string host, uint64_t cache_epoch,
                        std::pair<std::string, std::string> result) {
        const auto& [css, script] = result;
        if (generation != maho::GetCoreGeneration() || script.empty()) {
          return;
        }
        UpdateCachedBoostCss(host, cache_epoch, css, script);
        auto* frame = document.AsRenderFrameHostIfValid();
        if (!owner || !frame || !frame->IsRenderFrameLive() ||
            !frame->IsActive()) {
          return;
        }
        ExecuteBoostCssScript(frame, script);
      }, weak_factory_.GetWeakPtr(), render_frame_host->GetWeakDocumentPtr(),
         generation, host, cache_epoch));
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
  ExecuteInIsolatedWorld(LoadClassicContentScript(
      IDR_MAHO_BOOST_CONTENT_SCRIPT_SELECTOR_COMPONENT_JS));
  ExecuteInIsolatedWorld(
      LoadClassicContentScript(IDR_MAHO_BOOST_CONTENT_SCRIPT_ZAP_OVERLAY_JS));
  ExecuteInIsolatedWorld(
      LoadClassicContentScript(IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_JS));

  content_script_injected_ = true;
}

void MahoBoostInjectionHandler::ExecuteInIsolatedWorld(
    const std::string& script) {
  ExecuteInIsolatedWorldWithResult(script, base::DoNothing());
}

void MahoBoostInjectionHandler::ExecuteInIsolatedWorldForTesting(
    const std::string& script,
    base::OnceCallback<void(base::Value)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ExecuteInIsolatedWorldWithResult(script, std::move(callback));
}

void MahoBoostInjectionHandler::ExecuteInIsolatedWorldWithResult(
    const std::string& script,
    base::OnceCallback<void(base::Value)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContents* wc = web_contents();
  if (!wc) {
    std::move(callback).Run(base::Value());
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || !rfh->IsActive()) {
    std::move(callback).Run(base::Value());
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      std::move(callback),
      kIsolatedWorldIdForBoost);
}

void MahoBoostInjectionHandler::EnterZapMode(
    const std::vector<std::string>& zap_selectors,
    ContentScriptEventCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content_script_mode_ = ContentScriptMode::kZap;
  event_callback_ = std::move(callback);
  ++poll_generation_;
  poll_in_flight_ = false;

  InjectContentScript();

  std::string selectors_json = BuildZapSelectorsJson(zap_selectors);
  ExecuteInIsolatedWorld(
      "window.__mahoBoost && window.__mahoBoost.enterZapMode(" +
      selectors_json + ");");

  PollContentScript();
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
  ++poll_generation_;
  poll_in_flight_ = false;

  InjectContentScript();

  ExecuteInIsolatedWorld(
      "window.__mahoBoost && window.__mahoBoost.enterPickerMode();");

  PollContentScript();
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

void MahoBoostInjectionHandler::ExitCurrentEditMode() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (content_script_mode_ == ContentScriptMode::kZap) {
    ExecuteInIsolatedWorld(
        "window.__mahoBoost && window.__mahoBoost.exitZapMode();");
  } else if (content_script_mode_ == ContentScriptMode::kPicker) {
    ExecuteInIsolatedWorld(
        "window.__mahoBoost && window.__mahoBoost.exitPickerMode();");
  }
  content_script_mode_ = ContentScriptMode::kNone;
  event_callback_.Reset();
  StopPolling();
}

void MahoBoostInjectionHandler::PollContentScript() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (poll_in_flight_) {
    return;
  }
  content::WebContents* wc = web_contents();
  if (!wc) {
    StopPolling();
    content_script_mode_ = ContentScriptMode::kNone;
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || !rfh->IsActive()) {
    StopPolling();
    content_script_mode_ = ContentScriptMode::kNone;
    return;
  }

  poll_in_flight_ = true;
  const uint64_t generation = poll_generation_;
  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"(function() {"
      u"  if (!window.__mahoBoost || !window.__mahoBoost.drain) return [];"
      u"  return window.__mahoBoost.drain();"
      u"})()",
      base::BindOnce(&MahoBoostInjectionHandler::OnPollResult,
                      weak_factory_.GetWeakPtr(), generation),
      kIsolatedWorldIdForBoost);
}

void MahoBoostInjectionHandler::OnPollResult(uint64_t generation,
                                            base::Value result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != poll_generation_) {
    return;
  }
  poll_in_flight_ = false;
  if (!result.is_list() || !event_callback_) return;

  bool exited_current_mode = false;
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

    if (*type == "notify" && selector && notify_msg &&
        ((*selector == "zap-state-update" ||
          *selector == "selector-picker-state-update") &&
         *notify_msg == "ondisable")) {
      exited_current_mode = true;
    }
  }

  if (exited_current_mode) {
    content_script_mode_ = ContentScriptMode::kNone;
    StopPolling();
  }
}

void MahoBoostInjectionHandler::StopPolling() {
  ++poll_generation_;
  poll_timer_.Stop();
  poll_in_flight_ = false;
  event_callback_.Reset();
}
