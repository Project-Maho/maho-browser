// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_THIRD_PARTY_MAHO_MAHO_BRIDGE_H_
#define MAHO_CHROMIUM_THIRD_PARTY_MAHO_MAHO_BRIDGE_H_

#include <cstddef>
#include <string>

#include "maho_ffi.h"

namespace maho::core {

// Lifecycle — returns opaque MahoCore pointer from Rust FFI.
// Create() is the in-memory/test variant (no storage).
MahoCore* Create();
// CreateWithStorage() initialises both LMDB and SQLite storage under |path|.
// The directory must exist or be creatable.  Returns nullptr on error.
MahoCore* CreateWithStorage(const std::string& path);
void Destroy(MahoCore* core);

// Durable state persistence (tab/space state via LMDB + SQLite).
// Returns true on success.  Silently succeeds on in-memory cores.
bool SaveState(MahoCore* core);
bool LoadState(MahoCore* core);

// Version info
std::string Version();

// Content blocking
struct BlockResult {
  bool blocked = false;
  std::string redirect;
  std::string rewritten_url;
};

bool ShouldBlockRequest(MahoCore* core,
                        const char* url,
                        const char* source_url,
                        const char* request_type);

BlockResult CheckRequest(MahoCore* core,
                         const char* url,
                         const char* source_url,
                         const char* request_type);
void AddSiteException(MahoCore* core, const char* origin);
void RemoveSiteException(MahoCore* core, const char* origin);
std::string GetSiteExceptions(MahoCore* core);
bool SaveContentEngineCache(MahoCore* core, const char* path);
bool LoadContentEngineCache(MahoCore* core, const char* path);

std::string GetCosmeticResources(MahoCore* core, const char* url);

// Boost injection — returns the concatenated CSS/JS from all enabled boosts
// matching |url|. Returns an empty string if none apply.
std::string GetBoostInjectionCss(MahoCore* core, const char* url);
std::string GetBoostInjectionJs(MahoCore* core, const char* url);

// History search (returns JSON)
std::string SearchHistory(MahoCore* core,
                          const char* query,
                          size_t limit);

// Generic event handler (returns JSON)
std::string HandleEvent(MahoCore* core, const char* event_json);

// String memory management (for Rust-allocated strings)
void FreeString(char* str);

// Logging bridge — routes Rust log output to Chromium LOG().
void InstallLogCallback();

}  // namespace maho::core

#endif  // MAHO_CHROMIUM_THIRD_PARTY_MAHO_MAHO_BRIDGE_H_
