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
  char* raw = maho_core_get_boosts_for_url(core, url);
  if (!raw) {
    return std::string();
  }
  auto parsed = base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  maho_string_free(raw);
  if (!parsed || !parsed->is_list()) {
    return std::string();
  }
  std::string combined;
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    const auto& dict = item.GetDict();
    if (!dict.FindBool("enabled").value_or(false)) {
      continue;
    }
    const std::string* css = dict.FindString("customCss");
    if (css && !css->empty()) {
      if (!combined.empty()) {
        combined += "\n";
      }
      combined += *css;
    }
  }
  return combined;
}

std::string GetBoostInjectionJs(MahoCore* core, const char* url) {
  char* raw = maho_core_get_boosts_for_url(core, url);
  if (!raw) {
    return std::string();
  }
  auto parsed = base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  maho_string_free(raw);
  if (!parsed || !parsed->is_list()) {
    return std::string();
  }
  std::string combined;
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    const auto& dict = item.GetDict();
    if (!dict.FindBool("enabled").value_or(false)) {
      continue;
    }
    const std::string* js = dict.FindString("customJs");
    if (js && !js->empty()) {
      if (!combined.empty()) {
        combined += "\n";
      }
      combined += *js;
    }
  }
  return combined;
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
