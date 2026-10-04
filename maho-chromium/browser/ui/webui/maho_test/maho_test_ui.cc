// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_test/maho_test_ui.h"

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/time/time.h"

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"

#include "base/memory/ref_counted_memory.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/url_constants.h"
#include "net/base/url_util.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace {

std::string HtmlEscape(std::string_view text) {
  return base::EscapeForHTML(std::string(text));
}

std::string HtmlEscape(std::u16string_view text) {
  return HtmlEscape(base::UTF16ToUTF8(text));
}

std::string BoolText(bool value) {
  return value ? "true" : "false";
}

std::string JoinHtmlLabels(const std::vector<std::string>& labels) {
  if (labels.empty()) {
    return "EMPTY";
  }

  std::string joined;
  for (size_t i = 0; i < labels.size(); ++i) {
    if (i > 0) {
      joined += " | ";
    }
    joined += HtmlEscape(labels[i]);
  }
  return joined;
}

std::string SidebarNodeDisplayLabel(const maho::SidebarTreeNode& node) {
  if (node.kind == maho::SidebarNodeKind::kFolder) {
    return base::UTF16ToUTF8(node.folder_name);
  }
  if (!node.title.empty()) {
    return base::UTF16ToUTF8(node.title);
  }
  return base::UTF16ToUTF8(node.host);
}

std::vector<std::string> FavoriteOrderLabels(
    const maho::MahoSidebarViewStateModel& state) {
  std::vector<std::string> labels;
  labels.reserve(state.favorites.items.size());
  for (const auto& item : state.favorites.items) {
    labels.push_back(base::UTF16ToUTF8(item.title));
  }
  return labels;
}

std::vector<std::string> RootTabOrderLabels(
    const std::vector<maho::SidebarTreeNode>& tree) {
  std::vector<std::string> labels;
  labels.reserve(tree.size());
  for (const auto& node : tree) {
    if (node.kind != maho::SidebarNodeKind::kTab) {
      continue;
    }
    labels.push_back(SidebarNodeDisplayLabel(node));
  }
  return labels;
}

base::DictValue SidebarNodeToJsonDict(const maho::SidebarTreeNode& node) {
  base::DictValue dict;
  dict.Set("kind",
           node.kind == maho::SidebarNodeKind::kFolder ? "folder" : "tab");
  dict.Set("depth", node.depth);

  if (node.kind == maho::SidebarNodeKind::kFolder) {
    dict.Set("folder_id", node.folder_id);
    dict.Set("folder_name", base::UTF16ToUTF8(node.folder_name));
    dict.Set("is_expanded", node.is_expanded);
    dict.Set("is_pinned", node.folder_is_pinned);
    dict.Set("parent_folder_id", node.parent_of_folder_id);

    base::ListValue children;
    for (const auto& child : node.children) {
      children.Append(SidebarNodeToJsonDict(child));
    }
    dict.Set("children", std::move(children));
    return dict;
  }

  dict.Set("tab_id", node.tab_id);
  dict.Set("title", base::UTF16ToUTF8(node.title));
  dict.Set("host", base::UTF16ToUTF8(node.host));
  dict.Set("is_active", node.is_active);
  dict.Set("is_pinned", node.is_pinned);
  dict.Set("is_favorite", node.is_favorite);
  dict.Set("parent_folder_id", node.parent_folder_id);
  dict.Set("folder_child_index", node.folder_child_index);
  return dict;
}

std::string BuildSidebarSnapshotJson(const maho::MahoSidebarViewStateModel& state,
                                     bool sidebar_visible,
                                     bool sidebar_layout_enabled,
                                     bool sidebar_panel_expanded,
                                     size_t registered_space_count) {
  base::DictValue root;
  root.Set("sidebar_visible", sidebar_visible);
  root.Set("sidebar_layout_enabled", sidebar_layout_enabled);
  root.Set("sidebar_panel_expanded", sidebar_panel_expanded);
  root.Set("registered_space_count",
           static_cast<int>(registered_space_count));

  base::DictValue snapshot;
  snapshot.Set("favorites_count", static_cast<int>(state.favorites.items.size()));
  snapshot.Set("favorites_slot_count",
               static_cast<int>(state.favorites.slot_count));
  snapshot.Set("pinned_tabs_count", static_cast<int>(state.tab_list.pinned_tree.size()));
  snapshot.Set("normal_tabs_count", static_cast<int>(state.tab_list.normal_tree.size()));
  snapshot.Set("space_count", state.footer.space_count);
  snapshot.Set("active_space_index", state.footer.active_space_index);
  snapshot.Set("active_tab_index", state.tab_list.active_tab.index);
  snapshot.Set("active_tab_host", base::UTF16ToUTF8(state.tab_list.active_tab.host));
  root.Set("snapshot", std::move(snapshot));

  base::ListValue pinned_tree;
  for (const auto& node : state.tab_list.pinned_tree) {
    pinned_tree.Append(SidebarNodeToJsonDict(node));
  }
  root.Set("pinned_tree", std::move(pinned_tree));

  base::ListValue normal_tree;
  for (const auto& node : state.tab_list.normal_tree) {
    normal_tree.Append(SidebarNodeToJsonDict(node));
  }
  root.Set("normal_tree", std::move(normal_tree));

  base::ListValue favorites;
  for (const auto& item : state.favorites.items) {
    base::DictValue favorite;
    favorite.Set("tab_id", item.tab_id);
    favorite.Set("title", base::UTF16ToUTF8(item.title));
    favorite.Set("subtitle", base::UTF16ToUTF8(item.subtitle));
    favorites.Append(std::move(favorite));
  }
  root.Set("favorites", std::move(favorites));

  std::string json;
  base::JSONWriter::WriteWithOptions(
      root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

bool ShouldSeedRuntimeVerificationState(const GURL& url) {
  std::string seed_value;
  return url.has_query() &&
         net::GetValueForKeyInQuery(url, "seed", &seed_value) &&
         !seed_value.empty();
}

std::vector<std::string>& FavoriteDebugLines() {
  static auto* debug_lines = new std::vector<std::string>();
  return *debug_lines;
}

int GetSeedLevel(const GURL& url) {
  std::string seed_value;
  if (!url.has_query() ||
      !net::GetValueForKeyInQuery(url, "seed", &seed_value)) {
    return 0;
  }
  int level = 0;
  base::StringToInt(seed_value, &level);
  return level;
}

void SeedLevel1(Profile* profile) {
  if (!profile) {
    return;
  }

  bookmarks::BookmarkModel* bookmark_model =
      BookmarkModelFactory::GetForBrowserContext(profile);
  if (bookmark_model && bookmark_model->loaded() &&
      bookmark_model->bookmark_bar_node() &&
      bookmark_model->bookmark_bar_node()->children().empty()) {
    bookmark_model->AddURL(bookmark_model->bookmark_bar_node(), 0,
                           u"Maho Test Seed",
                           GURL("data:text/html,runtime-seed"));
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  Browser* browser = static_cast<Browser*>(
      collection ? collection->GetLastActiveBrowser() : nullptr);
  if (!browser || !browser->GetTabStripModel()) {
    return;
  }

  auto* tab_strip_model = browser->GetTabStripModel();
  for (int i = 0; i < tab_strip_model->count(); ++i) {
    if (tab_strip_model->IsTabPinned(i)) {
      return;
    }
  }

  if (tab_strip_model->count() > 0) {
    tab_strip_model->SetTabPinned(0, true);
  }
}

void SeedLevel2(Profile* profile) {
  SeedLevel1(profile);
  FavoriteDebugLines().clear();

  auto write_favorite_debug_file = [&]() {
    std::string favorite_debug_output;
    const std::vector<std::string>& favorite_debug_lines = FavoriteDebugLines();
    for (const std::string& line : favorite_debug_lines) {
      if (!favorite_debug_output.empty()) {
        favorite_debug_output.push_back('\n');
      }
      favorite_debug_output.append(line);
    }

    const base::FilePath favorite_debug_path(
        FILE_PATH_LITERAL("/tmp/maho_seed_level2_favorite_debug.txt"));
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(
            [](base::FilePath favorite_debug_path,
               std::string favorite_debug_output) {
              const bool wrote_favorite_debug =
                  base::WriteFile(favorite_debug_path, favorite_debug_output);
              LOG(INFO) << "SeedLevel2 favorite debug file "
                        << (wrote_favorite_debug ? "written" : "write failed")
                        << ": " << favorite_debug_path.value();
            },
            favorite_debug_path, std::move(favorite_debug_output)));
  };

  FavoriteDebugLines().push_back("SeedLevel2 entering");

  ProfileBrowserCollection* collection2 =
      ProfileBrowserCollection::GetForProfile(profile);
  Browser* browser = static_cast<Browser*>(
      collection2 ? collection2->GetLastActiveBrowser() : nullptr);
  if (!browser || !browser->GetTabStripModel()) {
    FavoriteDebugLines().push_back(
        "SeedLevel2 missing browser or tab_strip_model");
    write_favorite_debug_file();
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    FavoriteDebugLines().push_back("SeedLevel2 missing core");
    write_favorite_debug_file();
    return;
  }

  auto* tab_strip_model = browser->GetTabStripModel();

  struct SeedTab {
    const char* url;
    bool pin;
    bool favorite;
    const char* folder_name;
  };
  static constexpr SeedTab kFavoriteSeedTabs[] = {
      {"data:text/html,github", true, true, nullptr},
      {"data:text/html,docs", true, false, "Research"},
      {"data:text/html,news", false, true, "Planning"},
      {"data:text/html,shop", false, true, "Planning"},
      {"data:text/html,blog", false, false, "Work"},
      {"data:text/html,about", false, false, "Planning"},
  };
  static constexpr SeedTab kSupportSeedTabs[] = {
      {"data:text/html,github", true, false, nullptr},
      {"data:text/html,ycombinator", false, false, nullptr},
      {"data:text/html,runtime-seed", false, false, nullptr},
  };

  // Open all tabs first WITHOUT pinning. Pin state is applied later, after
  // core tab IDs have been created and bound to the WebContents, so that
  // OnTabPinnedStateChanged() can always resolve a valid core tab ID.
  auto open_seed_tab_if_missing = [&](const SeedTab& seed) {
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* wc = tab_strip_model->GetWebContentsAt(i);
      if (wc && wc->GetVisibleURL().spec() == std::string(seed.url)) {
        return;
      }
    }
    chrome::AddAndReturnTabAt(browser, GURL(seed.url), -1, false);
  };

  for (const auto& seed : kFavoriteSeedTabs) {
    open_seed_tab_if_missing(seed);
  }

  for (const auto& seed : kSupportSeedTabs) {
    open_seed_tab_if_missing(seed);
  }

  auto dispatch_seed_tab_event = [&](const char* kind,
                                      const std::string& tab_id,
                                      std::string* response_json = nullptr) -> bool {
    base::DictValue event;
    event.Set("kind", kind);
    event.Set("tab_id", tab_id);

    std::string event_json;
    base::JSONWriter::Write(event, &event_json);
    char* result_c = maho_core_handle_event(core, event_json.c_str());
    if (!result_c) {
      if (response_json) {
        response_json->clear();
      }
      return false;
    }

    if (response_json) {
      *response_json = result_c;
    }
    maho_string_free(result_c);
    return true;
  };

  auto resolve_active_space_id = [&]() -> std::string {
    char* c = maho_core_get_active_space_id(core);
    if (c) {
      std::optional<base::Value> parsed_active_id =
          base::JSONReader::Read(c, base::JSON_PARSE_RFC);
      std::string s = parsed_active_id && parsed_active_id->is_string()
                          ? parsed_active_id->GetString()
                          : std::string(c);
      maho_string_free(c);
      if (!s.empty()) {
        return s;
      }
    }

    if (auto* space_bridge = maho::MahoSpaceProfileBridge::GetInstance();
        space_bridge) {
      return space_bridge->GetActiveSpaceId();
    }

    return {};
  };

  std::string active_space_id = resolve_active_space_id();

  if (active_space_id.empty()) {
    FavoriteDebugLines().push_back("SeedLevel2 missing active_space_id");
    write_favorite_debug_file();
    return;
  }

  // Level 2 is the multi-space fixture: guarantee a second (inactive) space so
  // space switching and per-window space isolation have a target. Mirrors the
  // sidebar create-space flow: core record, bridge registration, footer
  // invalidation. Core create_space leaves the active space unchanged.
  if (auto* space_bridge = maho::MahoSpaceProfileBridge::GetInstance();
      space_bridge && space_bridge->GetRegisteredSpaceCount() < 2u) {
    char* created = maho_core_create_space(
        core, "Seed Space 2",
        R"({"hue":140.0,"saturation":0.6,"brightness":0.9,"grain":0.0})",
        "default");
    if (created) {
      std::optional<base::Value> parsed_space =
          base::JSONReader::Read(created, base::JSON_PARSE_RFC);
      maho_string_free(created);
      const std::string* second_space_id =
          parsed_space && parsed_space->is_dict()
              ? parsed_space->GetDict().FindString("id")
              : nullptr;
      if (second_space_id && !second_space_id->empty()) {
        space_bridge->RegisterSpace(*second_space_id,
                                    base::FilePath::FromASCII("Default"));
        maho::SidebarCacheInvalidation invalidation;
        invalidation.fragments = maho::SidebarCoreFragment::kFooter;
        maho::InvalidateSidebarCoreCache(invalidation);
      }
    }
  }

  auto log_favorite_tab_readback = [&](const SeedTab& seed,
                                       const std::string& tab_id,
                                       const std::string& response_json) {
    bool target_tab_present = false;
    std::string readback_json;
    if (!active_space_id.empty()) {
      const std::string space_id_json = base::GetQuotedJSONString(active_space_id);
      char* json_c = maho_core_get_favorite_tabs(core, space_id_json.c_str());
      if (json_c) {
        readback_json = json_c;
        std::optional<base::Value> parsed =
            base::JSONReader::Read(readback_json, base::JSON_PARSE_RFC);
        if (parsed && parsed->is_list()) {
          for (const auto& item : parsed->GetList()) {
            const auto* dict = item.GetIfDict();
            if (!dict) {
              continue;
            }
            const std::string* tid = dict->FindString("id");
            if (tid && *tid == tab_id) {
              target_tab_present = true;
              break;
            }
          }
        }
        maho_string_free(json_c);
      }
    }

    FavoriteDebugLines().push_back(
        "url=" + std::string(seed.url) +
        " tab_id=" + tab_id +
        " dispatch_status=" +
        (response_json.empty() ? "no-op" : "dispatched") +
        " readback_status=" +
        (target_tab_present ? "present" : "absent") +
        " response_payload=" +
        (response_json.empty() ? "<empty>" : response_json) +
        " readback_payload=" +
        (readback_json.empty() ? "<empty>" : readback_json));

    if (response_json.empty()) {
      LOG(WARNING) << "SeedLevel2 favorite_tab no-op response: tab_id=" << tab_id
                   << " url=" << seed.url << " payload=<empty>";
      return;
    }

    if (target_tab_present) {
      LOG(INFO) << "SeedLevel2 favorite_tab present in readback: tab_id="
                << tab_id << " url=" << seed.url << " response_payload="
                << response_json << " readback_payload="
                << (readback_json.empty() ? "<empty>" : readback_json);
    } else {
      LOG(WARNING) << "SeedLevel2 favorite_tab absent from readback: tab_id="
                   << tab_id << " url=" << seed.url << " response_payload="
                   << response_json << " readback_payload="
                   << (readback_json.empty() ? "<empty>" : readback_json);
    }
  };

  using maho::DispatchShellEvent;
  using maho::DispatchShellEventEx;
  using maho::ShellEventField;

  auto resolve_space_id_json = [&]() -> std::string {
    return active_space_id.empty() ? std::string()
                                   : base::GetQuotedJSONString(active_space_id);
  };

  auto normalize_url_for_seed_match = [](const std::string& url) {
    std::string normalized = url;
    if (normalized.size() > 1 && normalized.back() == '/') {
      normalized.pop_back();
    }
    return normalized;
  };

  std::set<std::string> live_urls_bound_to_core_ids;

  auto bind_live_tab_id_for_url = [&](const std::string& target_url,
                                      const std::string& tab_id) {
    if (target_url.empty() || tab_id.empty() ||
        live_urls_bound_to_core_ids.contains(target_url)) {
      return;
    }

    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }

      const std::string live_url = contents->GetVisibleURL().is_empty()
                                       ? contents->GetLastCommittedURL().spec()
                                       : contents->GetVisibleURL().spec();
      if (normalize_url_for_seed_match(live_url) !=
          normalize_url_for_seed_match(target_url)) {
        continue;
      }

      if (auto* helper = MahoTabIdHelper::FromWebContents(contents)) {
        helper->SetRestoredTabId(tab_id);
        live_urls_bound_to_core_ids.insert(target_url);
        FavoriteDebugLines().push_back(
            "bind_live_tab_id url=" + target_url + " tab_id=" + tab_id +
            " strip_index=" + base::NumberToString(i));
      }
      return;
    }
  };

  auto find_tab_id_by_url = [&](const std::string& target_url) -> std::string {
    const std::string normalized_target_url =
        normalize_url_for_seed_match(target_url);

    auto search_global_list = [&](char* (*ffi_fn)(MahoCore*)) -> std::string {
      char* json_c = ffi_fn(core);
      if (!json_c) return {};
      std::string json(json_c);
      maho_string_free(json_c);
      auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (!parsed || !parsed->is_list()) return {};
      for (const auto& item : parsed->GetList()) {
        const auto* dict = item.GetIfDict();
        if (!dict) continue;
        const std::string* url = dict->FindString("url");
        const std::string* tid = dict->FindString("id");
        if (url && tid && normalize_url_for_seed_match(*url) == normalized_target_url) {
          return *tid;
        }
      }
      return {};
    };

    auto search_space_list = [&](auto get_json_fn) -> std::string {
      std::string space_id_json = resolve_space_id_json();
      if (space_id_json.empty()) return {};
      char* json_c = get_json_fn(space_id_json.c_str());
      if (!json_c) return {};
      std::string json(json_c);
      maho_string_free(json_c);
      auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (!parsed || !parsed->is_list()) return {};
      for (const auto& item : parsed->GetList()) {
        const auto* dict = item.GetIfDict();
        if (!dict) continue;
        const std::string* url = dict->FindString("url");
        const std::string* tid = dict->FindString("id");
        if (url && tid && normalize_url_for_seed_match(*url) == normalized_target_url) {
          return *tid;
        }
      }
      return {};
    };

    std::string id = search_space_list([&](const char* s) { return maho_core_get_space_tabs(core, s, nullptr); });
    if (id.empty()) id = search_space_list([&](const char* s) { return maho_core_get_pinned_tabs(core, s); });
    if (id.empty()) id = search_global_list(maho_core_get_tab_view_models);
    return id;
  };

  auto find_tab_id_by_url_with_retry = [&](const std::string& target_url) {
    constexpr int kMaxAttempts = 12;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
      std::string tab_id = find_tab_id_by_url(target_url);
      if (!tab_id.empty()) {
        return tab_id;
      }
      if (attempt + 1 < kMaxAttempts) {
        base::RunLoop run_loop;
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(10));
        run_loop.Run();
      }
    }
    return std::string();
  };

  auto create_core_tab_for_seed = [&](const SeedTab& seed,
                                      std::string* response_json_out = nullptr,
                                      bool* parsed_tab_created_out = nullptr) -> std::string {
    if (parsed_tab_created_out) {
      *parsed_tab_created_out = false;
    }

    base::DictValue event;
    event.Set("kind", "create_tab");
    event.Set("space_id", active_space_id);
    event.Set("url", std::string(seed.url));
    event.Set("parent_id", base::Value());

    std::string event_json;
    base::JSONWriter::Write(event, &event_json);
    if (seed.favorite) {
      FavoriteDebugLines().push_back("favorite_seed url=" + std::string(seed.url) +
                                     " create_tab_event_json=" + event_json);
    }
    char* result_c = maho_core_handle_event(core, event_json.c_str());
    if (!result_c) {
      if (response_json_out) {
        response_json_out->clear();
      }
      return {};
    }

    std::string result_json(result_c);
    maho_string_free(result_c);
    if (response_json_out) {
      *response_json_out = result_json;
    }
    std::optional<base::Value> parsed =
        base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_list()) {
      return {};
    }

    for (const auto& item : parsed->GetList()) {
      const auto* dict = item.GetIfDict();
      if (!dict) {
        continue;
      }
      const std::string* kind = dict->FindString("kind");
      if (!kind || *kind != "tab_created") {
        continue;
      }
      const auto* tab = dict->FindDict("tab");
      if (!tab) {
        continue;
      }
      const std::string* tab_id = tab->FindString("id");
      if (tab_id && !tab_id->empty()) {
        if (parsed_tab_created_out) {
          *parsed_tab_created_out = true;
        }
        return *tab_id;
      }
    }

    return {};
  };

  auto custom_title_for_favorite_seed = [](const std::string& url) -> std::string {
    if (url == "data:text/html,github") {
      return "GitHub";
    }
    if (url == "data:text/html,news") {
      return "Example News";
    }
    if (url == "data:text/html,shop") {
      return "Example Shop";
    }
    return {};
  };

  auto set_seed_favorite_custom_title = [&](const SeedTab& seed,
                                            const std::string& tab_id) {
    if (!seed.favorite || tab_id.empty()) {
      return;
    }

    const std::string custom_title = custom_title_for_favorite_seed(seed.url);
    if (custom_title.empty()) {
      return;
    }

    maho_core_set_tab_custom_title(core, tab_id.c_str(), custom_title.c_str());
    FavoriteDebugLines().push_back("favorite_seed url=" + std::string(seed.url) +
                                   " custom_title=" + custom_title +
                                   " tab_id=" + tab_id);
  };

  std::vector<std::string> pending_live_pins;

  auto apply_live_strip_pin_for_url = [&](const std::string& target_url) {
    const std::string normalized = normalize_url_for_seed_match(target_url);
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* wc = tab_strip_model->GetWebContentsAt(i);
      if (!wc) continue;
      const std::string live_url =
          wc->GetVisibleURL().is_empty()
              ? wc->GetLastCommittedURL().spec()
              : wc->GetVisibleURL().spec();
      if (normalize_url_for_seed_match(live_url) != normalized) continue;
      if (!tab_strip_model->IsTabPinned(i)) {
        tab_strip_model->SetTabPinned(i, true);
      }
      return;
    }
  };

  auto ensure_core_tab_id_for_seed = [&](const SeedTab& seed,
                                          bool force_create = false) {
    const std::string target_url(seed.url);
    const bool should_log_favorite_debug = seed.favorite;
    std::string tab_id;

    const bool already_bound =
        live_urls_bound_to_core_ids.contains(target_url) ||
        live_urls_bound_to_core_ids.contains(
            normalize_url_for_seed_match(target_url));
    if (!force_create || already_bound) {
      tab_id = find_tab_id_by_url_with_retry(target_url);
      if (should_log_favorite_debug) {
        FavoriteDebugLines().push_back("favorite_seed url=" + target_url +
                                       " initial_lookup_result=" +
                                       (tab_id.empty() ? "<empty>" : tab_id));
      }
      if (!tab_id.empty()) {
        if (should_log_favorite_debug) {
          FavoriteDebugLines().push_back("favorite_seed url=" + target_url +
                                         " create_core_tab_for_seed_attempted=false");
        }
        if (seed.pin) {
          dispatch_seed_tab_event("pin_tab", tab_id);
        }
        bind_live_tab_id_for_url(target_url, tab_id);
        if (seed.pin) {
          pending_live_pins.push_back(target_url);
        }
        set_seed_favorite_custom_title(seed, tab_id);
        return tab_id;
      }
      if (!force_create) {
        return tab_id;
      }
    }

    if (should_log_favorite_debug) {
      FavoriteDebugLines().push_back("favorite_seed url=" + target_url +
                                     " create_core_tab_for_seed_attempted=true");
    }

    std::string create_tab_response_json;
    bool parsed_tab_created = false;
    tab_id = create_core_tab_for_seed(seed, &create_tab_response_json,
                                      &parsed_tab_created);
    if (should_log_favorite_debug) {
      FavoriteDebugLines().push_back("favorite_seed url=" + target_url +
                                     " create_tab_response_payload=" +
                                     (create_tab_response_json.empty() ? "<empty>"
                                                                       : create_tab_response_json));
      FavoriteDebugLines().push_back("favorite_seed url=" + target_url +
                                     " tab_created.tab.id_parsed=" +
                                     BoolText(parsed_tab_created));
    }
    if (tab_id.empty()) {
      tab_id = find_tab_id_by_url_with_retry(target_url);
      if (should_log_favorite_debug) {
        FavoriteDebugLines().push_back("favorite_seed url=" + target_url +
                                       " retry_lookup_result=" +
                                       (tab_id.empty() ? "<empty>" : tab_id));
      }
    }
    if (!tab_id.empty() && seed.pin) {
      dispatch_seed_tab_event("pin_tab", tab_id);
    }
    bind_live_tab_id_for_url(target_url, tab_id);
    if (!tab_id.empty() && seed.pin) {
      pending_live_pins.push_back(target_url);
    }
    set_seed_favorite_custom_title(seed, tab_id);
    return tab_id;
  };

  FavoriteDebugLines().push_back("SeedLevel2 favorite loop started");
  for (const auto& seed : kFavoriteSeedTabs) {
    if (!seed.favorite) continue;
    std::string tab_id = ensure_core_tab_id_for_seed(seed);
    if (tab_id.empty()) {
      FavoriteDebugLines().push_back(std::string("SeedLevel2 favorite skip: empty tab_id url=") +
                                     seed.url);
      LOG(WARNING) << "SeedLevel2 favorite_tab skipped: no tab_id for url="
                   << seed.url;
      continue;
    }

    std::string favorite_response_json;
    const bool favorite_dispatched =
        dispatch_seed_tab_event("favorite_tab", tab_id, &favorite_response_json);
    if (!favorite_dispatched) {
      LOG(WARNING) << "SeedLevel2 favorite_tab no-op response: tab_id="
                   << tab_id << " url=" << seed.url << " payload=<empty>";
    } else {
      LOG(INFO) << "SeedLevel2 favorite_tab response payload: tab_id=" << tab_id
                << " url=" << seed.url << " payload="
                << (favorite_response_json.empty() ? "<empty>" : favorite_response_json);
    }
    log_favorite_tab_readback(seed, tab_id, favorite_response_json);
  }

  auto custom_title_for_support_seed = [](const std::string& url) -> std::string {
    if (url == "data:text/html,github") {
      return "Page not found · GitHub · GitHub";
    }
    if (url == "data:text/html,runtime-seed") {
      return "Maho Test Page";
    }
    return {};
  };

  FavoriteDebugLines().push_back("SeedLevel2 support loop started");
  for (const auto& seed : kSupportSeedTabs) {
    std::string tab_id = ensure_core_tab_id_for_seed(seed, true);
    if (tab_id.empty()) {
      FavoriteDebugLines().push_back(std::string("SeedLevel2 support skip: empty tab_id url=") +
                                     seed.url);
      LOG(WARNING) << "SeedLevel2 support seed skipped: no tab_id for url="
                   << seed.url;
      continue;
    }

    const std::string support_title = custom_title_for_support_seed(seed.url);
    if (!support_title.empty()) {
      maho_core_set_tab_custom_title(core, tab_id.c_str(), support_title.c_str());
      FavoriteDebugLines().push_back("support_seed url=" + std::string(seed.url) +
                                     " custom_title=" + support_title +
                                     " tab_id=" + tab_id);
    }
  }

  DispatchShellEventEx("create_folder", {
      ShellEventField("space_id", active_space_id),
      ShellEventField("name", std::string("Work")),
      ShellEventField("is_pinned", false)});
  DispatchShellEventEx("create_folder", {
      ShellEventField("space_id", active_space_id),
      ShellEventField("name", std::string("Planning")),
      ShellEventField("is_pinned", false)});
  DispatchShellEventEx("create_folder", {
      ShellEventField("space_id", active_space_id),
      ShellEventField("name", std::string("Research")),
      ShellEventField("is_pinned", true)});

  auto find_folder_id_by_name = [&](const std::string& name) -> std::string {
    std::string space_id_json = resolve_space_id_json();
    if (space_id_json.empty()) return {};
    char* json_c = maho_core_get_folder_view_models(core, space_id_json.c_str());
    if (!json_c) return {};
    std::string json(json_c);
    maho_string_free(json_c);
    auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_list()) return {};
    for (const auto& item : parsed->GetList()) {
      const auto* dict = item.GetIfDict();
      if (!dict) continue;
      const std::string* n = dict->FindString("name");
      const std::string* id = dict->FindString("id");
      if (n && id && *n == name) return *id;
    }
    return {};
  };

  for (const auto& seed : kFavoriteSeedTabs) {
    if (!seed.folder_name) continue;
    std::string tab_id = ensure_core_tab_id_for_seed(seed);
    std::string folder_id = find_folder_id_by_name(seed.folder_name);
    if (!tab_id.empty() && !folder_id.empty()) {
      DispatchShellEvent("move_tab_to_folder", {
          {"space_id", active_space_id},
          {"folder_id", folder_id},
          {"tab_id", tab_id}});
    }
  }

  for (const std::string& pin_url : pending_live_pins) {
    apply_live_strip_pin_for_url(pin_url);
  }

  {
    std::string space_id_json = resolve_space_id_json();
    bool has_favorites = false;
    bool target_tab_present = false;
    std::string favorite_readback_json;
    std::string favorite_target_tab_id;
    if (!space_id_json.empty()) {
      char* json_c = maho_core_get_favorite_tabs(core, space_id_json.c_str());
      if (json_c) {
        favorite_readback_json = json_c;
        std::optional<base::Value> parsed =
            base::JSONReader::Read(favorite_readback_json, base::JSON_PARSE_RFC);
        has_favorites = parsed && parsed->is_list() &&
                        !parsed->GetList().empty();
        if (parsed && parsed->is_list()) {
          for (const auto& item : parsed->GetList()) {
            const auto* dict = item.GetIfDict();
            if (!dict) {
              continue;
            }
            const std::string* tid = dict->FindString("id");
            if (!tid) {
              continue;
            }
            for (const auto& seed : kFavoriteSeedTabs) {
              if (!seed.favorite) {
                continue;
              }
              std::string expected_tab_id = find_tab_id_by_url(seed.url);
              if (!expected_tab_id.empty() && *tid == expected_tab_id) {
                target_tab_present = true;
                favorite_target_tab_id = *tid;
                break;
              }
            }
            if (target_tab_present) {
              break;
            }
          }
        }
        maho_string_free(json_c);
      }
    }

    if (has_favorites) {
      LOG(INFO) << "SeedLevel2 favorite readback found favorites for "
                << active_space_id;
    } else {
      LOG(WARNING) << "SeedLevel2 favorite readback was empty for "
                   << active_space_id;
    }

    if (target_tab_present) {
      LOG(INFO) << "SeedLevel2 favorite readback contains target tab: space_id="
                << active_space_id << " tab_id=" << favorite_target_tab_id
                << " payload="
                << (favorite_readback_json.empty() ? "<empty>" : favorite_readback_json);
    } else {
      LOG(WARNING) << "SeedLevel2 favorite readback missing target tab: space_id="
                   << active_space_id << " payload="
                   << (favorite_readback_json.empty() ? "<empty>" : favorite_readback_json);
    }
  }

  write_favorite_debug_file();

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce([]() {
        if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
          bridge->NotifyChanged();
        }
      }));
}



std::string BuildSidebarSnapshotHtml(Profile* profile) {
  std::string html = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Maho Test Page</title>
  <style>
    body {
      font-family: system-ui, -apple-system, sans-serif;
      margin: 40px;
      background: #1a1a2e;
      color: #e0e0e0;
    }
    h1 { color: #e94560; }
    h2 { margin-top: 24px; color: #4ecca3; }
    .status {
      background: #16213e;
      padding: 16px;
      border-radius: 8px;
      margin-top: 20px;
    }
    .ok {
      color: #0f3460;
      background: #4ecca3;
      padding: 4px 12px;
      border-radius: 4px;
      font-weight: bold;
    }
    dl { display: grid; grid-template-columns: max-content 1fr; gap: 8px 16px; }
    dt { font-weight: 600; }
    dd { margin: 0; }
    ul { margin: 8px 0 0 20px; }
    code, pre { background: #0f172a; padding: 2px 6px; border-radius: 4px; }
  </style>
</head>
<body>
  <h1>Maho Test Page</h1>
  <div class="status">
    <p>This page reflects the live sidebar runtime state for deterministic verification.</p>
    <p>Set up bookmarks, pin tabs, open normal tabs, and register spaces before reloading this page.</p>
    <p>Sidebar tree JSON fields: normal_tree, parent_folder_id, folder_child_index.</p>
  </div>
)html";

  if (!profile) {
    html += R"html(<p><span class="ok">NO PROFILE</span> Unable to read sidebar state.</p>
</body>
</html>)html";
    return html;
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  Browser* browser = static_cast<Browser*>(
      collection ? collection->GetLastActiveBrowser() : nullptr);
  maho::MahoSidebarStateAdapter adapter;
  if (const auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    adapter.SetFavoritesModel(
        maho::MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
            base::GetQuotedJSONString(bridge->GetActiveSpaceId())));
  }
  const auto state = adapter.BuildViewStateModel(browser);
  const bool sidebar_visible =
      maho::sidebar_prefs::ShouldShowSidebarInShell(profile->GetPrefs());
  const bool sidebar_layout_enabled =
      maho::sidebar_prefs::IsSidebarLayoutEnabled(profile->GetPrefs());
  const bool sidebar_panel_expanded =
      maho::sidebar_prefs::IsSidebarPanelExpanded(profile->GetPrefs());
  const auto* space_bridge = maho::MahoSpaceProfileBridge::GetInstance();
  const size_t registered_space_count =
      space_bridge ? space_bridge->GetRegisteredSpaceCount() : 0u;
  const std::string snapshot_json = BuildSidebarSnapshotJson(
      state, sidebar_visible, sidebar_layout_enabled, sidebar_panel_expanded,
      registered_space_count);

  html += "  <section>";
  html += "    <h2>Snapshot</h2>";
  html += "    <dl>";
  html += "      <dt>Sidebar visible</dt><dd>" + BoolText(sidebar_visible) + "</dd>";
  html += "      <dt>Sidebar layout enabled</dt><dd>" +
          BoolText(sidebar_layout_enabled) + "</dd>";
  html += "      <dt>Sidebar panel expanded</dt><dd>" +
          BoolText(sidebar_panel_expanded) + "</dd>";
  html += "      <dt>Favorites present</dt><dd>" +
          BoolText(!state.favorites.items.empty()) + " (" +
          std::to_string(state.favorites.items.size()) + " items / " +
          std::to_string(state.favorites.slot_count) + " slots)</dd>";
  html += "      <dt>Pinned tabs present</dt><dd>" +
          BoolText(!state.tab_list.pinned_tree.empty()) + " (" +
          std::to_string(state.tab_list.pinned_tree.size()) + ")</dd>";
  html += "      <dt>Normal tabs present</dt><dd>" +
          BoolText(!state.tab_list.normal_tree.empty()) + " (" +
          std::to_string(state.tab_list.normal_tree.size()) + ")</dd>";
  html += "      <dt>Spaces visible</dt><dd>" +
          BoolText(state.footer.space_count > 0) + " (" +
          std::to_string(state.footer.space_count) + ", registered " +
          std::to_string(registered_space_count) + ")</dd>";
  html += "      <dt>Active space index</dt><dd>" +
          std::to_string(state.footer.active_space_index) + "</dd>";
  html += "      <dt>Active tab index</dt><dd>" +
          std::to_string(state.tab_list.active_tab.index) + "</dd>";
  html += "      <dt>Active tab host</dt><dd>" +
          HtmlEscape(state.tab_list.active_tab.host) + "</dd>";
  html += "    </dl>";
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Favorites</h2>";
  html += "    <p><strong>Favorites order:</strong> " +
          JoinHtmlLabels(FavoriteOrderLabels(state)) + "</p>";
  if (state.favorites.items.empty()) {
    html += "    <p><span class=\"ok\">EMPTY</span></p>";
  } else {
    html += "    <ul>";
    for (const auto& item : state.favorites.items) {
      html += "      <li>" + HtmlEscape(item.title) + " — " +
              HtmlEscape(item.subtitle) + "</li>";
    }
    html += "    </ul>";
  }
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Favorite debug</h2>";
  html += "    <p>Deterministic favorite seeding and drag diagnostics for the Chromium sidebar runtime.</p>";
  const auto& favorite_debug_lines = FavoriteDebugLines();
  if (favorite_debug_lines.empty()) {
    html += "    <p><span class=\"ok\">READY</span> No additional favorite debug lines were needed for this snapshot.</p>";
  } else {
    html += "    <ul>";
    for (const auto& line : favorite_debug_lines) {
      html += "      <li><pre>" + HtmlEscape(line) + "</pre></li>";
    }
    html += "    </ul>";
  }
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Pinned tabs</h2>";
  html += "    <p><strong>Pinned order:</strong> " +
          JoinHtmlLabels(RootTabOrderLabels(state.tab_list.pinned_tree)) +
          "</p>";
  if (state.tab_list.pinned_tree.empty()) {
    html += "    <p><span class=\"ok\">EMPTY</span></p>";
  } else {
    html += "    <ul>";
    for (const auto& node : state.tab_list.pinned_tree) {
      if (node.kind != maho::SidebarNodeKind::kTab) {
        continue;
      }
      html += "      <li>#" + std::to_string(node.tab_strip_index) + " " +
              HtmlEscape(node.title) + " — " + HtmlEscape(node.host) +
              (node.is_active ? " <span class=\"ok\">ACTIVE</span>" : "") +
              "</li>";
    }
    html += "    </ul>";
  }
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Normal tabs</h2>";
  html += "    <p><strong>Normal order:</strong> " +
          JoinHtmlLabels(RootTabOrderLabels(state.tab_list.normal_tree)) +
          "</p>";
  if (state.tab_list.normal_tree.empty()) {
    html += "    <p><span class=\"ok\">EMPTY</span></p>";
  } else {
    html += "    <ul>";
    for (const auto& node : state.tab_list.normal_tree) {
      if (node.kind != maho::SidebarNodeKind::kTab) {
        continue;
      }
      html += "      <li>#" + std::to_string(node.tab_strip_index) + " " +
              HtmlEscape(node.title) + " — " + HtmlEscape(node.host) +
              (node.is_active ? " <span class=\"ok\">ACTIVE</span>" : "") +
              "</li>";
    }
    html += "    </ul>";
  }
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Spaces</h2>";
  html += "    <p>Status: " + HtmlEscape(state.footer.status_text) + "</p>";
  html += "    <p>Open controls: " + BoolText(state.footer.can_open_space_controls) +
          "</p>";
  html += "    <p>Create space: " + BoolText(state.footer.can_create_space) +
          "</p>";
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Folders</h2>";
  bool has_folders = false;
  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == maho::SidebarNodeKind::kFolder) {
      has_folders = true;
      break;
    }
  }
  if (!has_folders) {
    for (const auto& node : state.tab_list.normal_tree) {
      if (node.kind == maho::SidebarNodeKind::kFolder) {
        has_folders = true;
        break;
      }
    }
  }
  if (!has_folders) {
    html += "    <p><span class=\"ok\">NONE</span></p>";
  } else {
    html += "    <ul>";
    auto render_folder = [&](const maho::SidebarTreeNode& node,
                             const std::string& section_label) {
      if (node.kind != maho::SidebarNodeKind::kFolder) return;
      html += "      <li>[" + section_label + "] " +
              HtmlEscape(node.folder_name) +
              " (id=" + node.folder_id +
              ", expanded=" + BoolText(node.is_expanded) +
              ", children=" + std::to_string(node.children.size()) + ")</li>";
    };
    for (const auto& node : state.tab_list.pinned_tree) {
      render_folder(node, "pinned");
    }
    for (const auto& node : state.tab_list.normal_tree) {
      render_folder(node, "normal");
    }
    html += "    </ul>";
  }
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Sidebar tree JSON</h2>";
  html += "    <pre id=\"sidebar-tree-json\">" +
          HtmlEscape(snapshot_json) + "</pre>";
  html += "  </section>";

  html += "  <section>";
  html += "    <h2>Phase 2 Coverage</h2>";
  html += "    <dl>";
  html += "      <dt>Favorites</dt><dd class=\"ok\">SUPPORTED — seed, display, drag-to-favorite, reorder</dd>";
  html += "      <dt>Pinned tabs</dt><dd class=\"ok\">SUPPORTED — seed, display, pin/unpin via drag</dd>";
  html += "      <dt>Normal tabs</dt><dd class=\"ok\">SUPPORTED — seed, display, activate, close</dd>";
  html += "      <dt>Folders</dt><dd class=\"ok\">SUPPORTED — create, move-tab-into-folder, folder-into-folder, and folder-heavy verification via seed/runtime snapshot plus sidebar tree JSON</dd>";
  html += "      <dt>Tab reorder (DnD)</dt><dd class=\"ok\">SUPPORTED — reorder_tab via before_tab_id and reorder_tab_in_folder via post-removal insertion index</dd>";
  html += "      <dt>Favorite reorder (DnD)</dt><dd class=\"ok\">SUPPORTED — reorder_favorite via new_index</dd>";
  html += "      <dt>Folder reorder (DnD)</dt><dd class=\"ok\">SUPPORTED — reorder_folder via before_folder_id and nested parent_folder_id semantics</dd>";
  html += "      <dt>Tab out of folder (DnD)</dt><dd class=\"ok\">SUPPORTED — move_tab_to_root via atomic folder-child root return</dd>";
  html += "      <dt>Spaces</dt><dd class=\"ok\">SUPPORTED — display, seed registers spaces</dd>";
  html += "      <dt>Tab-on-tab split</dt><dd class=\"ok\">SUPPORTED — tab on tab body routes to split view, AX proof is still the weakest part</dd>";
  html += "    </dl>";
  html += "  </section>";

  html += "</body>\n</html>";
  return html;
}

}  // namespace

void SeedRuntimeVerificationState(Profile* profile, int level) {
  if (level <= 0) {
    return;
  }
  if (level == 1) {
    SeedLevel1(profile);
  } else {
    SeedLevel2(profile);
  }
}

// MahoTestUIConfig

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

MahoTestUIConfig::MahoTestUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoTestHost) {}

bool MahoTestUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

// MahoTestHTMLSource

MahoTestHTMLSource::MahoTestHTMLSource(Profile* profile)
    : profile_(profile) {
  LOG(INFO) << "MahoTestHTMLSource constructor";
}

MahoTestHTMLSource::~MahoTestHTMLSource() = default;

std::string MahoTestHTMLSource::GetSource() {
  return maho::kMahoTestHost;
}

#include "base/task/single_thread_task_runner.h"

void MahoTestHTMLSource::StartDataRequest(
    const GURL& url,
    const content::WebContents::Getter& wc_getter,
    content::URLDataSource::GotDataCallback callback) {
  LOG(INFO) << "MahoTestHTMLSource::StartDataRequest URL=" << url.spec();
  if (ShouldSeedRuntimeVerificationState(url)) {
    LOG(INFO) << "MahoTestHTMLSource: posting seeding runtime verification state level=" << GetSeedLevel(url);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SeedRuntimeVerificationState, profile_, GetSeedLevel(url)));
  }

  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(
          BuildSidebarSnapshotHtml(profile_)));
  LOG(INFO) << "MahoTestHTMLSource::StartDataRequest finished";
}

std::string MahoTestHTMLSource::GetMimeType(const GURL& url) {
  return "text/html";
}

// MahoTestUI

MahoTestUI::MahoTestUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  LOG(INFO) << "MahoTestUI constructor";
  Profile* profile = Profile::FromWebUI(web_ui);
  content::URLDataSource::Add(
      profile, std::make_unique<MahoTestHTMLSource>(profile));
}
