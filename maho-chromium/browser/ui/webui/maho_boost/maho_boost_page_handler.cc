// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_boost/zap_picker_script_string.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"

namespace {

// Helper to extract hostname from pattern
std::string ExtractDomainFromPattern(const std::string& pattern) {
  std::string clean = pattern;
  if (base::StartsWith(clean, "*://", base::CompareCase::SENSITIVE)) {
    clean.replace(0, 4, "https://");
  }
  const size_t wildcard = clean.find("://*.");
  if (wildcard != std::string::npos) {
    clean.erase(wildcard + 3, 2);
  }
  GURL url(clean);
  if (url.is_valid() && url.has_host()) {
    return std::string(url.host());
  }
  return pattern;
}

// Parse zaps from CSS
std::vector<std::string> ParseZapsFromCss(const std::string& css) {
  std::vector<std::string> zaps;
  size_t start = css.find("/* maho-zaps: ");
  if (start == std::string::npos) {
    return zaps;
  }
  size_t end = css.find(" */", start);
  if (end == std::string::npos) {
    return zaps;
  }
  std::string json_str = css.substr(start + 14, end - (start + 14));
  auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_list()) {
    for (const auto& val : parsed->GetList()) {
      if (val.is_string()) {
        zaps.push_back(val.GetString());
      }
    }
  }
  return zaps;
}

// Rebuild CSS with updated zaps
std::string UpdateCssWithZaps(const std::string& original_css,
                              const std::vector<std::string>& new_zaps) {
  std::string clean_css = original_css;
  size_t start = clean_css.find("/* maho-zaps: ");
  if (start != std::string::npos) {
    size_t end = clean_css.find(" */", start);
    if (end != std::string::npos) {
      clean_css.erase(start, end + 3 - start);
    }
  }

  std::vector<std::string> lines = base::SplitString(
      clean_css, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  std::string filtered_css;
  for (const auto& line : lines) {
    if (line.find("display: none !important;") != std::string::npos &&
        line.find("{") != std::string::npos &&
        line.find("}") != std::string::npos) {
      continue;
    }
    filtered_css += line + "\n";
  }

  base::Value list(base::Value::Type::LIST);
  for (const auto& zap : new_zaps) {
    list.GetList().Append(zap);
  }
  std::string json_str;
  base::JSONWriter::Write(list, &json_str);

  std::string result = "/* maho-zaps: " + json_str + " */\n";
  for (const auto& zap : new_zaps) {
    result += zap + " { display: none !important; }\n";
  }
  result += filtered_css;
  return result;
}

}  // namespace

MahoBoostPageHandler::MahoBoostPageHandler(
    mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_boost::mojom::Page> page,
    Profile* profile)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile) {}

MahoBoostPageHandler::~MahoBoostPageHandler() = default;

void MahoBoostPageHandler::GetRulesForSite(const std::string& domain,
                                           GetRulesForSiteCallback callback) {
  auto rule = maho_boost::mojom::SiteRule::New();
  rule->id = "";
  rule->domain = domain;
  rule->css_rules = "";

  // Set JS
  ContentSetting setting =
      HostContentSettingsMapFactory::GetForProfile(profile_)->GetContentSetting(
          GURL("https://" + domain + "/"), GURL(),
          ContentSettingsType::JAVASCRIPT);
  rule->js_enabled = (setting != CONTENT_SETTING_BLOCK);

  // Set Ads Blocked
  MahoCore* core = maho::GetCore();
  if (core) {
    char* raw_exceptions = maho_core_get_site_exceptions(core);
    bool excepted = false;
    if (raw_exceptions) {
      std::string json(raw_exceptions);
      maho_string_free(raw_exceptions);
      auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_list()) {
        for (const auto& val : parsed->GetList()) {
          if (val.is_string() && val.GetString() == domain) {
            excepted = true;
            break;
          }
        }
      }
    }
    rule->ads_blocked = !excepted;

    // Find boost for site
    std::string url = "https://" + domain + "/";
    char* raw_boosts = maho_core_get_boosts_for_url(core, url.c_str());
    if (raw_boosts) {
      std::string json(raw_boosts);
      maho_string_free(raw_boosts);
      auto parsed_boosts = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (parsed_boosts && parsed_boosts->is_list() &&
          !parsed_boosts->GetList().empty()) {
        const auto& first = parsed_boosts->GetList()[0];
        if (first.is_dict()) {
          const auto& d = first.GetDict();
          const std::string* id = d.FindString("id");
          if (id) {
            rule->id = *id;
          }
          const std::string* custom_css = d.FindString("customCss");
          if (custom_css) {
            rule->css_rules = *custom_css;
          }
        }
      }
    }
  } else {
    rule->ads_blocked = true;
  }

  std::move(callback).Run(std::move(rule));
}

void MahoBoostPageHandler::SetCssRules(const std::string& domain,
                                       const std::string& css,
                                       SetCssRulesCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false);
    return;
  }

  // Find if there is an existing boost
  std::string url = "https://" + domain + "/";
  std::string boost_id;
  char* raw_boosts = maho_core_get_boosts_for_url(core, url.c_str());
  if (raw_boosts) {
    std::string json(raw_boosts);
    maho_string_free(raw_boosts);
    auto parsed_boosts = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (parsed_boosts && parsed_boosts->is_list() &&
        !parsed_boosts->GetList().empty()) {
      const auto& first = parsed_boosts->GetList()[0];
      if (first.is_dict()) {
        const auto& d = first.GetDict();
        const std::string* id = d.FindString("id");
        if (id) {
          boost_id = *id;
        }
      }
    }
  }

  if (boost_id.empty()) {
    // Create new boost
    base::Value event(base::Value::Type::DICT);
    event.GetDict().Set("kind", "create_boost");
    base::Value url_pattern(base::Value::Type::DICT);
    url_pattern.GetDict().Set("pattern", "*://" + domain + "/*");
    url_pattern.GetDict().Set("type", "glob");
    event.GetDict().Set("url_pattern", std::move(url_pattern));
    std::string event_json;
    base::JSONWriter::Write(event, &event_json);
    maho::core::HandleEvent(core, event_json.c_str());

    // Retrieve the created boost to get its ID
    char* raw_boosts2 = maho_core_get_boosts_for_url(core, url.c_str());
    if (raw_boosts2) {
      std::string json(raw_boosts2);
      maho_string_free(raw_boosts2);
      auto parsed_boosts2 = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (parsed_boosts2 && parsed_boosts2->is_list() &&
          !parsed_boosts2->GetList().empty()) {
        const auto& first = parsed_boosts2->GetList()[0];
        if (first.is_dict()) {
          const auto& d = first.GetDict();
          const std::string* id = d.FindString("id");
          if (id) {
            boost_id = *id;
          }
        }
      }
    }
  }

  if (boost_id.empty()) {
    std::move(callback).Run(false);
    return;
  }

  // Update CSS
  base::Value event(base::Value::Type::DICT);
  event.GetDict().Set("kind", "update_boost");
  event.GetDict().Set("boost_id", boost_id);
  base::Value changes(base::Value::Type::DICT);
  changes.GetDict().Set("customCss", css);
  event.GetDict().Set("changes", std::move(changes));
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  maho::core::HandleEvent(core, event_json.c_str());

  std::move(callback).Run(true);
}

void MahoBoostPageHandler::ToggleJs(const std::string& domain,
                                    bool enabled,
                                    ToggleJsCallback callback) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->SetContentSettingDefaultScope(
          GURL("https://" + domain + "/"), GURL(),
          ContentSettingsType::JAVASCRIPT,
          enabled ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
  std::move(callback).Run(true);
}

void MahoBoostPageHandler::ToggleAds(const std::string& domain,
                                     bool blocked,
                                     ToggleAdsCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false);
    return;
  }

  if (blocked) {
    maho_core_remove_site_exception(core, domain.c_str());
  } else {
    maho_core_add_site_exception(core, domain.c_str());
  }
  std::move(callback).Run(true);
}

void MahoBoostPageHandler::EnterZapMode(const std::string& boost_id,
                                        EnterZapModeCallback callback) {
  BrowserWindowInterface* bwi = chrome::FindLastActiveWithProfile(profile_);
  Browser* browser = bwi ? bwi->GetBrowserForMigrationOnly() : nullptr;
  if (!browser) {
    std::move(callback).Run(false);
    return;
  }
  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    std::move(callback).Run(false);
    return;
  }
  content::RenderFrameHost* main_frame = web_contents->GetPrimaryMainFrame();
  if (!main_frame) {
    std::move(callback).Run(false);
    return;
  }

  // First execute the script itself to register window.__mahoStartZapPicker
  main_frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kZapPickerScript)), base::DoNothing(),
      content::ISOLATED_WORLD_ID_CONTENT_END + 4);

  // Then start the picker
  std::string run_picker = "window.__mahoStartZapPicker('" + boost_id + "');";
  main_frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(run_picker), base::DoNothing(),
      content::ISOLATED_WORLD_ID_CONTENT_END + 4);

  // Start polling
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MahoBoostPageHandler::PollZapPicker,
                                weak_factory_.GetWeakPtr(),
                                web_contents->GetWeakPtr(), boost_id));

  std::move(callback).Run(true);
}

void MahoBoostPageHandler::RemoveZapSelector(
    const std::string& boost_id,
    const std::string& selector,
    RemoveZapSelectorCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false);
    return;
  }

  char* raw_boosts = maho_core_get_boost_view_models(core);
  if (!raw_boosts) {
    std::move(callback).Run(false);
    return;
  }

  std::string json(raw_boosts);
  maho_string_free(raw_boosts);
  auto parsed_boosts = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed_boosts || !parsed_boosts->is_list()) {
    std::move(callback).Run(false);
    return;
  }

  bool found = false;
  for (const auto& item : parsed_boosts->GetList()) {
    if (item.is_dict()) {
      const auto& d = item.GetDict();
      const std::string* id = d.FindString("id");
      if (id && *id == boost_id) {
        const std::string* url_pattern = d.FindString("urlPattern");
        const std::string* custom_css = d.FindString("customCss");
        std::string css = custom_css ? *custom_css : "";
        std::vector<std::string> zaps = ParseZapsFromCss(css);

        auto it = std::find(zaps.begin(), zaps.end(), selector);
        if (it != zaps.end()) {
          zaps.erase(it);
        }

        std::string new_css = UpdateCssWithZaps(css, zaps);

        // Save CSS via event
        base::Value event(base::Value::Type::DICT);
        event.GetDict().Set("kind", "update_boost");
        event.GetDict().Set("boost_id", boost_id);
        base::Value changes(base::Value::Type::DICT);
        changes.GetDict().Set("customCss", new_css);
        event.GetDict().Set("changes", std::move(changes));
        std::string event_json;
        base::JSONWriter::Write(event, &event_json);
        maho::core::HandleEvent(core, event_json.c_str());

        // Get domain from pattern
        std::string domain;
        if (url_pattern) {
          domain = ExtractDomainFromPattern(*url_pattern);
        }

        // Notify frontend
        auto rule = maho_boost::mojom::SiteRule::New();
        rule->id = boost_id;
        rule->domain = domain;
        rule->css_rules = new_css;
        rule->js_enabled = true;
        rule->ads_blocked = true;
        page_->RulesChanged(std::move(rule));
        found = true;
        break;
      }
    }
  }

  std::move(callback).Run(found);
}

void MahoBoostPageHandler::PollZapPicker(
    base::WeakPtr<content::WebContents> web_contents,
    const std::string& boost_id) {
  if (!web_contents) {
    return;
  }
  Browser* browser = chrome::FindBrowserWithTab(web_contents.get());
  if (!browser) {
    return;
  }

  content::RenderFrameHost* main_frame = web_contents->GetPrimaryMainFrame();
  if (!main_frame || !main_frame->IsRenderFrameLive()) {
    return;
  }

  main_frame->ExecuteJavaScriptInIsolatedWorld(
      u"window.__mahoZapPickerResult || ''",
      base::BindOnce(&MahoBoostPageHandler::OnZapPickerResult,
                     weak_factory_.GetWeakPtr(), web_contents, boost_id),
      content::ISOLATED_WORLD_ID_CONTENT_END + 4);
}

void MahoBoostPageHandler::OnZapPickerResult(
    base::WeakPtr<content::WebContents> web_contents,
    const std::string& boost_id,
    base::Value result) {
  if (!web_contents) {
    return;
  }
  std::string result_str;
  if (result.is_string()) {
    result_str = result.GetString();
  }

  if (result_str.empty()) {
    // Continue polling
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&MahoBoostPageHandler::PollZapPicker,
                       weak_factory_.GetWeakPtr(), web_contents, boost_id),
        base::Milliseconds(200));
    return;
  }

  // Parse result JSON
  auto parsed = base::JSONReader::Read(result_str, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_dict()) {
    const auto& dict = parsed->GetDict();
    const std::string* selector = dict.FindString("selector");
    if (selector && !selector->empty()) {
      MahoCore* core = maho::GetCore();
      if (core) {
        char* raw_boosts = maho_core_get_boost_view_models(core);
        if (raw_boosts) {
          std::string json(raw_boosts);
          maho_string_free(raw_boosts);
          auto parsed_boosts =
              base::JSONReader::Read(json, base::JSON_PARSE_RFC);
          if (parsed_boosts && parsed_boosts->is_list()) {
            for (const auto& item : parsed_boosts->GetList()) {
              if (item.is_dict()) {
                const auto& d = item.GetDict();
                const std::string* id = d.FindString("id");
                if (id && *id == boost_id) {
                  const std::string* url_pattern = d.FindString("urlPattern");
                  const std::string* custom_css = d.FindString("customCss");
                  std::string css = custom_css ? *custom_css : "";
                  std::vector<std::string> zaps = ParseZapsFromCss(css);
                  zaps.push_back(*selector);
                  std::string new_css = UpdateCssWithZaps(css, zaps);

                  // Save CSS via event
                  base::Value event(base::Value::Type::DICT);
                  event.GetDict().Set("kind", "update_boost");
                  event.GetDict().Set("boost_id", boost_id);
                  base::Value changes(base::Value::Type::DICT);
                  changes.GetDict().Set("customCss", new_css);
                  event.GetDict().Set("changes", std::move(changes));
                  std::string event_json;
                  base::JSONWriter::Write(event, &event_json);
                  maho::core::HandleEvent(core, event_json.c_str());

                  // Get domain from pattern
                  std::string domain;
                  if (url_pattern) {
                    domain = ExtractDomainFromPattern(*url_pattern);
                  }

                  // Notify frontend
                  auto rule = maho_boost::mojom::SiteRule::New();
                  rule->id = boost_id;
                  rule->domain = domain;
                  rule->css_rules = new_css;
                  rule->js_enabled = true;
                  rule->ads_blocked = true;
                  page_->RulesChanged(std::move(rule));
                  page_->OnZapSelectorPicked(boost_id, *selector);
                  break;
                }
              }
            }
          }
        }
      }
    }
  }

  // Clear result on page
  content::RenderFrameHost* main_frame = web_contents->GetPrimaryMainFrame();
  if (main_frame && main_frame->IsRenderFrameLive()) {
    main_frame->ExecuteJavaScriptInIsolatedWorld(
        u"window.__mahoZapPickerResult = '';", base::DoNothing(),
        content::ISOLATED_WORLD_ID_CONTENT_END + 4);
  }
}
