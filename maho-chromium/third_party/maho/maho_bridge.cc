// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_bridge.h"

#include "build/build_config.h"
#include "build/buildflag.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "maho_ffi.h"
#include "url/gurl.h"

namespace maho::core {

// Platform-specific initialization (if needed in future)
#if BUILDFLAG(IS_MAC)
#elif BUILDFLAG(IS_WIN)
#elif BUILDFLAG(IS_LINUX)
#endif

MahoCore* Create() {
  return maho_core_new();
}

MahoCore* CreateWithStorage(const std::string& path) {
  MahoCore* core = maho_core_new_with_storage(path.c_str());
  if (!core) {
    LOG(ERROR) << "Maho core: CreateWithStorage failed for path: " << path;
  }
  return core;
}

void Destroy(MahoCore* core) {
  maho_core_free(core);
}

bool SaveState(MahoCore* core) {
  if (!core) {
    return false;
  }
  bool ok = maho_core_save_state(core) != 0;
  if (!ok) {
    LOG(WARNING) << "Maho core: SaveState failed";
  }
  return ok;
}

bool LoadState(MahoCore* core) {
  if (!core) {
    return false;
  }
  bool ok = maho_core_load_state(core) != 0;
  if (!ok) {
    LOG(WARNING) << "Maho core: LoadState failed";
  }
  return ok;
}

std::string Version() {
  // TODO(maho): Add maho_core_version() to maho-ffi
  return "0.1.0";
}

bool ShouldBlockRequest(MahoCore* core,
                        const char* url,
                        const char* source_url,
                        const char* request_type) {
  return maho_core_should_block_request(core, url, source_url, request_type);
}

BlockResult CheckRequest(MahoCore* core,
                         const char* url,
                         const char* source_url,
                         const char* request_type) {
  BlockResult result;
  char* json = maho_core_check_request(core, url, source_url, request_type);
  if (!json) {
    return result;
  }
  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  maho_string_free(json);
  if (!parsed || !parsed->is_dict()) {
    return result;
  }
  const auto& dict = parsed->GetDict();
  result.blocked = dict.FindBool("blocked").value_or(false);
  if (const std::string* redirect = dict.FindString("redirect")) {
    result.redirect = *redirect;
  }
  if (const std::string* rewritten = dict.FindString("rewrittenUrl")) {
    result.rewritten_url = *rewritten;
  }
  return result;
}

void AddSiteException(MahoCore* core, const char* origin) {
  maho_core_add_site_exception(core, origin);
}

void RemoveSiteException(MahoCore* core, const char* origin) {
  maho_core_remove_site_exception(core, origin);
}

std::string GetSiteExceptions(MahoCore* core) {
  char* result = maho_core_get_site_exceptions(core);
  if (!result) {
    return "[]";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

bool SaveContentEngineCache(MahoCore* core, const char* path) {
  return maho_core_save_content_engine_cache(core, path);
}

bool LoadContentEngineCache(MahoCore* core, const char* path) {
  return maho_core_load_content_engine_cache(core, path);
}

std::string GetCosmeticResources(MahoCore* core, const char* url) {
  char* result = maho_core_get_cosmetic_resources(core, url);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string GetBoostInjectionCss(MahoCore* core, const char* url) {
  if (!core || !url) {
    return std::string();
  }

  GURL gurl(url);
  if (!gurl.is_valid() || !gurl.has_host()) {
    return std::string();
  }
  const std::string domain(gurl.host());
  if (domain.empty()) {
    return std::string();
  }

  char* active_json = maho_core_boost_get_active(core, domain.c_str());
  if (!active_json) {
    VLOG(1) << "[MahoBoost] GetBoostInjectionCss: no active boost for domain="
            << domain;
    return std::string();
  }

  auto parsed = base::JSONReader::Read(active_json, base::JSON_PARSE_RFC);
  maho_string_free(active_json);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  const std::string* boost_id = parsed->GetDict().FindString("id");
  if (!boost_id || boost_id->empty()) {
    return std::string();
  }

  char* css = maho_core_boost_compose_css(core, boost_id->c_str());
  if (!css) {
    VLOG(1) << "[MahoBoost] GetBoostInjectionCss: compose_css returned null "
            << "for boost_id=" << *boost_id << " domain=" << domain;
    return std::string();
  }
  std::string result(css);
  maho_string_free(css);
  VLOG(1) << "[MahoBoost] GetBoostInjectionCss: domain=" << domain
          << " css_length=" << result.size();
  return result;
}

std::string CreateTempBoost(MahoCore* core, const char* domain) {
  if (!core || !domain) {
    return std::string();
  }
  char* json = maho_core_boost_create_temp(core, domain);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string CommitBoost(MahoCore* core, const char* boost_id) {
  if (!core || !boost_id) {
    return std::string();
  }
  char* json = maho_core_boost_commit(core, boost_id);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string DiscardBoost(MahoCore* core, const char* boost_id) {
  if (!core || !boost_id) {
    return std::string();
  }
  char* prev_id = maho_core_boost_discard(core, boost_id);
  if (!prev_id) {
    return std::string();
  }
  std::string result(prev_id);
  maho_string_free(prev_id);
  return result;
}

std::string ShuffleBoost(MahoCore* core, const char* boost_id) {
  if (!core || !boost_id) {
    return std::string();
  }
  char* json = maho_core_boost_shuffle(core, boost_id);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string ResetBoost(MahoCore* core, const char* boost_id) {
  if (!core || !boost_id) {
    return std::string();
  }
  char* json = maho_core_boost_reset(core, boost_id);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string ExportBoost(MahoCore* core, const char* boost_id) {
  if (!core || !boost_id) {
    return std::string();
  }
  char* json = maho_core_boost_export(core, boost_id);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string ImportBoost(MahoCore* core,
                        const char* domain,
                        const char* json) {
  if (!core || !domain || !json) {
    return std::string();
  }
  char* result_json = maho_core_boost_import(core, domain, json);
  if (!result_json) {
    return std::string();
  }
  std::string result(result_json);
  maho_string_free(result_json);
  return result;
}

std::string AppendZapSelector(MahoCore* core,
                              const char* boost_id,
                              const char* selector) {
  if (!core || !boost_id || !selector) {
    return std::string();
  }
  char* json = maho_core_boost_append_zap(core, boost_id, selector);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string RemoveZapSelector(MahoCore* core,
                              const char* boost_id,
                              const char* selector) {
  if (!core || !boost_id || !selector) {
    return std::string();
  }
  char* json = maho_core_boost_remove_zap(core, boost_id, selector);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string SearchHistory(MahoCore* core,
                          const char* query,
                          size_t limit) {
  char* result = maho_core_search_history(core, query, limit);
  if (!result) {
    return "[]";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string SearchBookmarks(MahoCore* core,
                            const char* query) {
  char* result = maho_core_search_bookmarks(core, query);
  if (!result) {
    return "[]";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string HandleEvent(MahoCore* core, const char* event_json) {
  char* result = maho_core_handle_event(core, event_json);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

void FreeString(char* str) {
  maho_string_free(str);
}

void InstallLogCallback() {
  // TODO(maho): Wire maho_core_set_log_callback() once implemented in Rust.
  // For now, Rust logging goes to stderr by default.
  LOG(INFO) << "Maho log callback: pending Rust implementation";
}

}  // namespace maho::core
