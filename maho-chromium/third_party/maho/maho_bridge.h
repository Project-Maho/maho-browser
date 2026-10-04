// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_THIRD_PARTY_MAHO_MAHO_BRIDGE_H_
#define MAHO_CHROMIUM_THIRD_PARTY_MAHO_MAHO_BRIDGE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"

// Opaque Rust core handle (defined in the cbindgen-generated maho_ffi.h).
// Forward-declared rather than #included so this facade does not re-export that
// frequently-regenerated header to its includers, avoiding mass recompiles.
struct MahoCore;
struct MahoTraceRecorder;
struct MahoVaultBackendBuffer;
struct MahoVaultBackendResult;
struct MahoVaultBackendSession;
struct OpaqueCompiledEngine;

namespace maho::core {

// Lifecycle — returns opaque MahoCore pointer from Rust FFI.
// Create() is the in-memory/test variant (no storage).
MahoCore* Create();
// CreateWithStorage() initialises both LMDB and SQLite storage under |path|.
// The directory must exist or be creatable.  Returns nullptr on error.
MahoCore* CreateWithStorage(const std::string& path);
void Destroy(MahoCore* core);

bool SetStorageKey(const std::string& hex_key);
bool DeriveAndSetStorageKey(const std::string& key_file_path);

enum class VaultPreflightState : int32_t {
  kUnavailable = -1,
  kHealthy = 0,
  kLocked = 1,
  kUnrecoverableKey = 2,
  kStructuralCorruption = 3,
  kPlaintextResidue = 4,
};

struct StorageKeySetupResult {
  bool storage_key_ready = false;
  VaultPreflightState vault_preflight_state =
      VaultPreflightState::kUnavailable;
  std::string vault_preflight_json;
};

// Derives and installs the storage key, then performs a read-only Vault
// preflight against `database_path`. The Rust preflight receives the explicit
// key-store availability verdict, so an unavailable key is not conflated with
// a valid locked Vault or SQLCipher corruption.
StorageKeySetupResult DeriveAndSetStorageKey(
    const std::string& key_file_path,
    const std::string& database_path);
VaultPreflightState GetVaultPreflightState();
const char* VaultPreflightStateName(VaultPreflightState state);

// Durable state persistence (tab/space state via LMDB + SQLite).
// Returns true on success.  Silently succeeds on in-memory cores.
bool SaveState(MahoCore* core);
bool LoadState(MahoCore* core);

bool LockVault(MahoCore* core);

using VaultLockCallbackForTesting = base::RepeatingCallback<bool(MahoCore*)>;
void SetVaultLockCallbackForTesting(VaultLockCallbackForTesting callback);

// Vault password manager parity operations (JSON in/out).
// Owned char* envelopes returned by FFI are converted to std::string and freed
// with maho_string_free. For sensitive payloads (notes, TOTP codes, generated
// passwords), response buffers are securely wiped before deallocation.
void SecureClearString(std::string* value);
void SecureClearBuffer(char* buffer, size_t length);

std::string VaultTrashItemJson(MahoCore* core, const char* request_json);
std::string VaultRestoreItemJson(MahoCore* core, const char* request_json);
std::string VaultSetFavoriteJson(MahoCore* core, const char* request_json);
std::string VaultEmptyTrashJson(MahoCore* core);
std::string VaultGetNotesJson(MahoCore* core, const char* request_json);
std::string VaultAddSecureNoteJson(MahoCore* core, const char* request_json);
std::string VaultUpdateSecureNoteJson(MahoCore* core, const char* request_json);
std::string VaultSetLoginTotpJson(MahoCore* core, const char* request_json);
std::string VaultTotpCodeJson(MahoCore* core, const char* request_json);
std::string VaultHealthReportJson(MahoCore* core);
std::string VaultGeneratePasswordJson(MahoCore* core, const char* request_json);
std::string VaultPasswordStrengthJson(MahoCore* core, const char* request_json);


enum class VaultBackendResultStatus : uint8_t {
  kSuccess = 0,
  kInvalidRequest = 1,
  kRuntimeUnavailable = 2,
  kLocked = 3,
  kFailed = 4,
};

class VaultBackendBuffer {
 public:
  VaultBackendBuffer();
  ~VaultBackendBuffer();
  VaultBackendBuffer(VaultBackendBuffer&& other) noexcept;
  VaultBackendBuffer& operator=(VaultBackendBuffer&& other) noexcept;
  VaultBackendBuffer(const VaultBackendBuffer&) = delete;
  VaultBackendBuffer& operator=(const VaultBackendBuffer&) = delete;

  bool is_valid() const;
  std::string CopyBytes() const;

 private:
  explicit VaultBackendBuffer(::MahoVaultBackendBuffer* buffer);

  ::MahoVaultBackendBuffer* buffer_ = nullptr;

  friend class VaultBackendResult;
};

class VaultBackendResult {
 public:
  VaultBackendResult();
  ~VaultBackendResult();
  VaultBackendResult(VaultBackendResult&& other) noexcept;
  VaultBackendResult& operator=(VaultBackendResult&& other) noexcept;
  VaultBackendResult(const VaultBackendResult&) = delete;
  VaultBackendResult& operator=(const VaultBackendResult&) = delete;

  bool is_valid() const;
  VaultBackendResultStatus status() const;
  std::string ConsumeJson();

 private:
  explicit VaultBackendResult(::MahoVaultBackendResult* result);

  ::MahoVaultBackendResult* result_ = nullptr;

  friend class VaultBackendSession;
};

class VaultBackendSession {
 public:
  struct State;

  VaultBackendSession();
  ~VaultBackendSession();
  VaultBackendSession(VaultBackendSession&& other) noexcept;
  VaultBackendSession& operator=(VaultBackendSession&& other) noexcept;
  VaultBackendSession(const VaultBackendSession&) = delete;
  VaultBackendSession& operator=(const VaultBackendSession&) = delete;

  bool is_valid() const;
  void Close();
  VaultBackendResult ExecuteBatch(const std::string& request_json);
  VaultBackendResult ExecuteCredentials(const std::string& request_json);

 private:
  explicit VaultBackendSession(std::shared_ptr<State> state);

  std::shared_ptr<State> state_;

  friend VaultBackendSession CreateVaultBackendSession(MahoCore* core);
  friend void QuiesceVaultBackendSessions();
};

VaultBackendSession CreateVaultBackendSession(MahoCore* core);
void QuiesceVaultBackendSessions();

struct VaultBackendFfiForTesting {
  ::MahoVaultBackendSession* (*session_new)(::MahoCore* core);
  void (*session_close)(::MahoVaultBackendSession* session);
  void (*session_free)(::MahoVaultBackendSession* session);
  ::MahoVaultBackendResult* (*session_execute)(
      ::MahoVaultBackendSession* session,
      const char* request_json);
  ::MahoVaultBackendResult* (*session_execute_credentials)(
      ::MahoVaultBackendSession* session,
      const char* request_json);
  int (*result_status)(const ::MahoVaultBackendResult* result);
  ::MahoVaultBackendBuffer* (*result_consume)(
      ::MahoVaultBackendResult* result);
  void (*result_free)(::MahoVaultBackendResult* result);
  const uint8_t* (*buffer_data)(const ::MahoVaultBackendBuffer* buffer);
  size_t (*buffer_len)(const ::MahoVaultBackendBuffer* buffer);
  void (*buffer_free)(::MahoVaultBackendBuffer* buffer);
};

void SetVaultBackendFfiForTesting(const VaultBackendFfiForTesting* ffi);

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
int GetContentBlockingMode(MahoCore* core);
bool SetContentBlockingMode(MahoCore* core, int mode);
std::string GetContentBlockerStateJson(MahoCore* core);
std::string AddFilterListResultJson(MahoCore* core,
                                    const char* id,
                                    const char* name,
                                    const char* url);
std::string ToggleFilterListResultJson(MahoCore* core,
                                       const char* id,
                                       bool enabled);
std::string RemoveFilterListResultJson(MahoCore* core, const char* id);
std::string ApplyFilterListUpdateResultJson(MahoCore* core,
                                            const char* json);
std::string InstallContentEngineResultJson(MahoCore* core,
                                           OpaqueCompiledEngine* handle);
bool ApplyFilterListUpdateJson(MahoCore* core, const char* json);

void AddSiteException(MahoCore* core, const char* origin);
void RemoveSiteException(MahoCore* core, const char* origin);
std::string GetSiteExceptions(MahoCore* core);
std::string NormalizeSiteExceptionKey(const std::string& input);
bool SaveContentEngineCache(MahoCore* core, const char* path);
bool LoadContentEngineCache(MahoCore* core, const char* path);

std::string GetCosmeticResources(MahoCore* core, const char* url);

std::string GetBoostInjectionCss(MahoCore* core, const char* url);

std::string CreateTempBoost(MahoCore* core, const char* domain);
std::string CommitBoost(MahoCore* core, const char* boost_id);
std::string DiscardBoost(MahoCore* core, const char* boost_id);
std::string ShuffleBoost(MahoCore* core, const char* boost_id);
std::string ResetBoost(MahoCore* core, const char* boost_id);
std::string ExportBoost(MahoCore* core, const char* boost_id);
std::string ImportBoost(MahoCore* core,
                        const char* domain,
                        const char* json);
std::string AppendZapSelector(MahoCore* core,
                              const char* boost_id,
                              const char* selector);
std::string RemoveZapSelector(MahoCore* core,
                              const char* boost_id,
                              const char* selector);

// History search (returns JSON)
std::string SearchHistory(MahoCore* core,
                          const char* query,
                          size_t limit);

// Bookmarks search (returns JSON)
std::string SearchBookmarks(MahoCore* core,
                            const char* query);

// Stores a bookmark and returns the stored entry as JSON
// (`{"id","url","title","folder"}`), or an empty string when nothing was
// stored (private tab, import in progress, invalid input).
std::string AddBookmark(MahoCore* core,
                        const char* url,
                        const char* title,
                        const char* folder_id);

// Get password provider registry (returns JSON)
std::string GetPasswordProviderRegistry();

// Generic event handler (returns JSON)
std::string HandleEvent(MahoCore* core, const char* event_json);

// String memory management (for Rust-allocated strings)
void FreeString(char* str);

// Logging bridge — routes Rust log output to Chromium LOG().
void InstallLogCallback();

}  // namespace maho::core

namespace maho {

// C++ RAII wrapper over Rust MahoTraceRecorder.
class TraceRecorder {
 public:
  TraceRecorder();
  ~TraceRecorder();

  TraceRecorder(const TraceRecorder&) = delete;
  TraceRecorder& operator=(const TraceRecorder&) = delete;

  void RecordClick(const std::optional<std::string>& target_ref,
                   double x,
                   double y);
  void RecordFill(const std::string& text,
                  const std::optional<std::string>& selector);
  void RecordNavigate(const std::string& url);
  void RecordHover(const std::optional<std::string>& target_ref);
  std::string FinishAndExportJson();

 private:
  struct MahoTraceRecorder* handle_{nullptr};
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_THIRD_PARTY_MAHO_MAHO_BRIDGE_H_
