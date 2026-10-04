// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_bridge.h"

#include "build/build_config.h"
#include "build/buildflag.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/synchronization/lock.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "maho_ffi.h"
#include "url/gurl.h"

extern "C" char* maho_normalize_site_exception_key(const char* input);
extern "C" int32_t maho_core_vault_preflight_state(
    MahoCore* core,
    const char* database_path,
    const char* database_key,
    bool key_store_key_available,
    char** out_state_json);

namespace maho::core {

namespace {

struct VaultBackendFfiState {
  base::Lock lock;
  const VaultBackendFfiForTesting* ffi = nullptr;
};

VaultBackendFfiState& GetVaultBackendFfiState() {
  static base::NoDestructor<VaultBackendFfiState> state;
  return *state;
}

std::atomic<VaultPreflightState> g_vault_preflight_state{
    VaultPreflightState::kUnavailable};

VaultPreflightState ToVaultPreflightState(int value) {
  switch (value) {
    case 0:
      return VaultPreflightState::kHealthy;
    case 1:
      return VaultPreflightState::kLocked;
    case 2:
      return VaultPreflightState::kUnrecoverableKey;
    case 3:
      return VaultPreflightState::kStructuralCorruption;
    case 4:
      return VaultPreflightState::kPlaintextResidue;
    default:
      return VaultPreflightState::kUnavailable;
  }
}

const VaultBackendFfiForTesting* GetVaultBackendFfiForTesting() {
  VaultBackendFfiState& state = GetVaultBackendFfiState();
  base::AutoLock lock(state.lock);
  return state.ffi;
}

MahoVaultBackendSession* VaultBackendSessionNew(MahoCore* core) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) return ffi->session_new(core);
  return maho_vault_backend_session_new(core);
}
void VaultBackendSessionClose(MahoVaultBackendSession* session) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) { ffi->session_close(session); return; }
  maho_vault_backend_session_close(session);
}
void VaultBackendSessionFree(MahoVaultBackendSession* session) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) { ffi->session_free(session); return; }
  maho_vault_backend_session_free(session);
}
MahoVaultBackendResult* VaultBackendSessionExecute(MahoVaultBackendSession* session, const char* request_json) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) return ffi->session_execute(session, request_json);
  return maho_vault_backend_session_execute(session, request_json);
}
MahoVaultBackendResult* VaultBackendSessionExecuteCredentials(
    MahoVaultBackendSession* session,
    const char* request_json) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) {
    return ffi->session_execute_credentials(session, request_json);
  }
  return maho_vault_backend_session_execute_credentials(session, request_json);
}
int VaultBackendFfiResultStatus(const MahoVaultBackendResult* result) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) return ffi->result_status(result);
  return static_cast<int>(maho_vault_backend_result_status(result));
}
MahoVaultBackendBuffer* VaultBackendResultConsume(MahoVaultBackendResult* result) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) return ffi->result_consume(result);
  return maho_vault_backend_result_consume(result);
}
void VaultBackendResultFree(MahoVaultBackendResult* result) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) { ffi->result_free(result); return; }
  maho_vault_backend_result_free(result);
}
const uint8_t* VaultBackendBufferData(const MahoVaultBackendBuffer* buffer) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) return ffi->buffer_data(buffer);
  return maho_vault_backend_buffer_data(buffer);
}
size_t VaultBackendBufferLen(const MahoVaultBackendBuffer* buffer) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) return ffi->buffer_len(buffer);
  return maho_vault_backend_buffer_len(buffer);
}
void VaultBackendBufferFree(MahoVaultBackendBuffer* buffer) {
  if (const auto* ffi = GetVaultBackendFfiForTesting()) { ffi->buffer_free(buffer); return; }
  maho_vault_backend_buffer_free(buffer);
}

VaultLockCallbackForTesting& GetVaultLockCallbackForTesting() {
  static base::NoDestructor<VaultLockCallbackForTesting> callback;
  return *callback;
}

}  // namespace

struct VaultBackendSession::State {
  explicit State(::MahoVaultBackendSession* session) : session_(session) {}
  ~State() { Reset(); }
  void Close() {
    base::AutoLock lock(lock_);
    if (session_ && !closed_) { VaultBackendSessionClose(session_); closed_ = true; }
  }
  VaultBackendResult ExecuteBatch(const std::string& request_json) {
    base::AutoLock lock(lock_);
    if (!session_ || closed_) return VaultBackendResult();
    return VaultBackendResult(VaultBackendSessionExecute(session_, request_json.c_str()));
  }
  VaultBackendResult ExecuteCredentials(const std::string& request_json) {
    base::AutoLock lock(lock_);
    if (!session_ || closed_) return VaultBackendResult();
    return VaultBackendResult(
        VaultBackendSessionExecuteCredentials(session_, request_json.c_str()));
  }
  bool is_valid() const {
    base::AutoLock lock(lock_);
    return session_ && !closed_;
  }
 private:
  void Reset() {
    base::AutoLock lock(lock_);
    if (session_) { VaultBackendSessionFree(session_); session_ = nullptr; closed_ = true; }
  }
  mutable base::Lock lock_;
  ::MahoVaultBackendSession* session_ GUARDED_BY(lock_) = nullptr;
  bool closed_ GUARDED_BY(lock_) = false;
};

namespace {

struct VaultBackendSessionRegistry {
  base::Lock lock;
  std::vector<std::weak_ptr<VaultBackendSession::State>> sessions
      GUARDED_BY(lock);
};

VaultBackendSessionRegistry& GetVaultBackendSessionRegistry() {
  static base::NoDestructor<VaultBackendSessionRegistry> registry;
  return *registry;
}

}  // namespace

VaultBackendBuffer::VaultBackendBuffer() = default;
VaultBackendBuffer::VaultBackendBuffer(::MahoVaultBackendBuffer* buffer)
    : buffer_(buffer) {}
VaultBackendBuffer::~VaultBackendBuffer() {
  VaultBackendBufferFree(buffer_);
}
VaultBackendBuffer::VaultBackendBuffer(VaultBackendBuffer&& other) noexcept
    : buffer_(std::exchange(other.buffer_, nullptr)) {}
VaultBackendBuffer& VaultBackendBuffer::operator=(
    VaultBackendBuffer&& other) noexcept {
  if (this != &other) {
    VaultBackendBufferFree(buffer_);
    buffer_ = std::exchange(other.buffer_, nullptr);
  }
  return *this;
}
bool VaultBackendBuffer::is_valid() const { return buffer_ != nullptr; }
std::string VaultBackendBuffer::CopyBytes() const {
  if (!buffer_) return std::string();
  const uint8_t* data = VaultBackendBufferData(buffer_);
  const size_t length = VaultBackendBufferLen(buffer_);
  if (!data && length != 0) return std::string();
  return std::string(reinterpret_cast<const char*>(data), length);
}

VaultBackendResult::VaultBackendResult() = default;
VaultBackendResult::VaultBackendResult(::MahoVaultBackendResult* result)
    : result_(result) {}
VaultBackendResult::~VaultBackendResult() {
  VaultBackendResultFree(result_);
}
VaultBackendResult::VaultBackendResult(VaultBackendResult&& other) noexcept
    : result_(std::exchange(other.result_, nullptr)) {}
VaultBackendResult& VaultBackendResult::operator=(
    VaultBackendResult&& other) noexcept {
  if (this != &other) {
    VaultBackendResultFree(result_);
    result_ = std::exchange(other.result_, nullptr);
  }
  return *this;
}
bool VaultBackendResult::is_valid() const { return result_ != nullptr; }
VaultBackendResultStatus VaultBackendResult::status() const {
  return result_ ? static_cast<VaultBackendResultStatus>(
                       VaultBackendFfiResultStatus(result_))
                 : VaultBackendResultStatus::kFailed;
}
std::string VaultBackendResult::ConsumeJson() {
  VaultBackendBuffer buffer(result_ ? VaultBackendResultConsume(result_) : nullptr);
  return buffer.CopyBytes();
}

VaultBackendSession::VaultBackendSession() = default;
VaultBackendSession::VaultBackendSession(std::shared_ptr<State> state)
    : state_(std::move(state)) {}
VaultBackendSession::~VaultBackendSession() = default;
VaultBackendSession::VaultBackendSession(VaultBackendSession&& other) noexcept =
    default;
VaultBackendSession& VaultBackendSession::operator=(
    VaultBackendSession&& other) noexcept = default;
bool VaultBackendSession::is_valid() const {
  return state_ && state_->is_valid();
}
void VaultBackendSession::Close() {
  if (state_) {
    state_->Close();
  }
}
VaultBackendResult VaultBackendSession::ExecuteBatch(const std::string& request_json) {
  return state_ ? state_->ExecuteBatch(request_json) : VaultBackendResult();
}
VaultBackendResult VaultBackendSession::ExecuteCredentials(
    const std::string& request_json) {
  return state_ ? state_->ExecuteCredentials(request_json) : VaultBackendResult();
}
VaultBackendSession CreateVaultBackendSession(MahoCore* core) {
  auto state = std::make_shared<VaultBackendSession::State>(
      VaultBackendSessionNew(core));
  if (!state->is_valid()) {
    return VaultBackendSession();
  }
  VaultBackendSessionRegistry& registry = GetVaultBackendSessionRegistry();
  {
    base::AutoLock lock(registry.lock);
    registry.sessions.emplace_back(state);
  }
  return VaultBackendSession(std::move(state));
}
void QuiesceVaultBackendSessions() {
  std::vector<std::shared_ptr<VaultBackendSession::State>> sessions;
  VaultBackendSessionRegistry& registry = GetVaultBackendSessionRegistry();
  {
    base::AutoLock lock(registry.lock);
    auto output = registry.sessions.begin();
    for (auto input = registry.sessions.begin(); input != registry.sessions.end();
         ++input) {
      if (auto session = input->lock()) {
        sessions.push_back(session);
        *output++ = *input;
      }
    }
    registry.sessions.erase(output, registry.sessions.end());
  }
  for (const auto& session : sessions) {
    session->Close();
  }
}
void SetVaultBackendFfiForTesting(const VaultBackendFfiForTesting* ffi) {
  VaultBackendFfiState& state = GetVaultBackendFfiState();
  base::AutoLock lock(state.lock);
  state.ffi = ffi;
}

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

bool SetStorageKey(const std::string& hex_key) {
  return maho_storage_set_sqlcipher_key(hex_key.c_str());
}

bool DeriveAndSetStorageKey(const std::string& key_file_path) {
  uint32_t status = 0;
  if (!maho_core_oscrypt_key_path_is_secure(key_file_path.c_str(), &status)) {
    if (status != 1) {  // 1 = IoError (meaning it does not exist)
      LOG(ERROR) << "Maho storage key path exists but permission verification "
                    "failed: status="
                 << status;
      return false;
    }
  }
  uint8_t key16[16] = {0};
  if (!maho_core_derive_oscrypt_key(key_file_path.c_str(), key16, &status)) {
    LOG(ERROR) << "Maho storage key derivation failed: status=" << status;
    return false;
  }
  if (!maho_core_oscrypt_key_path_is_secure(key_file_path.c_str(), &status)) {
    LOG(ERROR) << "Maho storage key path permission verification failed after "
                  "derivation: status="
               << status;
    return false;
  }
  std::string hex_key = base::HexEncode(base::span<const uint8_t>(key16));
  return SetStorageKey(hex_key);
}

StorageKeySetupResult DeriveAndSetStorageKey(
    const std::string& key_file_path,
    const std::string& database_path) {
  StorageKeySetupResult result;
  uint32_t status = 0;
  const base::FilePath key_path =
      base::FilePath::FromUTF8Unsafe(key_file_path);
  const base::FilePath vault_database_path =
      base::FilePath::FromUTF8Unsafe(database_path);
  const bool database_exists = base::PathExists(vault_database_path);
  bool key_store_key_available = true;
  if (!maho_core_oscrypt_key_path_is_secure(key_file_path.c_str(), &status)) {
    // A first-run profile may create key material only while no database
    // exists. If persistent data already exists, a missing/unreadable/insecure
    // key is unrecoverable input to preflight and must never be replaced.
    const bool key_is_missing = status == 1 && !base::PathExists(key_path);
    if (!key_is_missing || database_exists) {
      LOG(ERROR) << "Maho storage key unavailable for existing state: status="
                 << status;
      key_store_key_available = false;
    }
  }

  uint8_t key16[16] = {0};
  std::string hex_key;
  if (key_store_key_available &&
      maho_core_derive_oscrypt_key(key_file_path.c_str(), key16, &status)) {
    if (maho_core_oscrypt_key_path_is_secure(key_file_path.c_str(), &status)) {
      hex_key = base::HexEncode(base::span<const uint8_t>(key16));
      result.storage_key_ready = SetStorageKey(hex_key);
    } else {
      LOG(ERROR) << "Maho storage key path permission verification failed "
                    "after derivation: status="
                 << status;
      key_store_key_available = false;
    }
  } else if (key_store_key_available) {
    LOG(ERROR) << "Maho storage key derivation failed: status=" << status;
    key_store_key_available = false;
  }

  // The preflight accessor is deliberately callable on an in-memory core: it
  // only inspects the supplied database path and never opens persistent core
  // state. This lets startup classify key loss before deciding whether the
  // encrypted core can be created.
  MahoCore* preflight_core = Create();
  if (preflight_core) {
    char* state_json = nullptr;
    const int state = maho_core_vault_preflight_state(
        preflight_core, database_path.c_str(),
        key_store_key_available ? hex_key.c_str() : nullptr,
        key_store_key_available, &state_json);
    result.vault_preflight_state = ToVaultPreflightState(state);
    if (state_json) {
      result.vault_preflight_json.assign(state_json);
      maho_string_free(state_json);
    }
    Destroy(preflight_core);
  }
  g_vault_preflight_state.store(result.vault_preflight_state,
                                std::memory_order_release);
  return result;
}

VaultPreflightState GetVaultPreflightState() {
  return g_vault_preflight_state.load(std::memory_order_acquire);
}

const char* VaultPreflightStateName(VaultPreflightState state) {
  switch (state) {
    case VaultPreflightState::kHealthy:
      return "healthy";
    case VaultPreflightState::kLocked:
      return "locked";
    case VaultPreflightState::kUnrecoverableKey:
      return "unrecoverableKey";
    case VaultPreflightState::kStructuralCorruption:
      return "structuralCorruption";
    case VaultPreflightState::kPlaintextResidue:
      return "plaintextResidue";
    case VaultPreflightState::kUnavailable:
      return "unavailable";
  }
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

bool LockVault(MahoCore* core) {
  if (!core) {
    return false;
  }
  VaultLockCallbackForTesting& callback = GetVaultLockCallbackForTesting();
  if (!callback.is_null()) {
    return callback.Run(core);
  }
  char* result = maho_vault_lock_json(core);
  if (!result) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result, base::JSON_PARSE_RFC);
  maho_string_free(result);
  return parsed && parsed->is_dict() &&
         parsed->GetDict().FindBool("ok").value_or(false);
}

void SetVaultLockCallbackForTesting(VaultLockCallbackForTesting callback) {
  GetVaultLockCallbackForTesting() = std::move(callback);
}

void SecureClearBuffer(char* buffer, size_t length) {
  if (!buffer || length == 0) {
    return;
  }
  UNSAFE_BUFFERS({
    volatile char* p = buffer;
    for (size_t i = 0; i < length; ++i) {
      p[i] = 0;
    }
  });
}

void SecureClearString(std::string* value) {
  if (!value || value->empty()) {
    return;
  }
  SecureClearBuffer(value->data(), value->size());
  value->clear();
}

namespace {

std::string TakeAndFreeMahoJson(char* raw_json, bool sensitive = false) {
  if (!raw_json) {
    return std::string();
  }
  std::string result(raw_json);
  if (sensitive) {
    SecureClearBuffer(raw_json, result.size());
  }
  maho_string_free(raw_json);
  return result;
}

}  // namespace

std::string VaultTrashItemJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_trash_item_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultRestoreItemJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_restore_item_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultSetFavoriteJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_set_favorite_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultEmptyTrashJson(MahoCore* core) {
  if (!core) {
    return std::string();
  }
  char* raw_json = maho_vault_empty_trash_json(core);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultGetNotesJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_get_notes_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/true);
}

std::string VaultAddSecureNoteJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_add_secure_note_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultUpdateSecureNoteJson(MahoCore* core,
                                      const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_update_secure_note_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultSetLoginTotpJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_set_login_totp_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultTotpCodeJson(MahoCore* core, const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_totp_code_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/true);
}

std::string VaultHealthReportJson(MahoCore* core) {
  if (!core) {
    return std::string();
  }
  char* raw_json = maho_vault_health_report_json(core);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
}

std::string VaultGeneratePasswordJson(MahoCore* core,
                                      const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_generate_password_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/true);
}

std::string VaultPasswordStrengthJson(MahoCore* core,
                                      const char* request_json) {
  if (!core || !request_json) {
    return std::string();
  }
  char* raw_json = maho_vault_password_strength_json(core, request_json);
  return TakeAndFreeMahoJson(raw_json, /*sensitive=*/false);
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
  // POD across the boundary: this runs on the UI thread for every subresource
  // request, so the previous JSON round trip (serialize in Rust, parse in C++,
  // two heap allocations) was pure overhead. Allowed requests — the common
  // case — now allocate nothing; only a redirect/rewrite returns owned strings.
  MahoBlockResult raw =
      maho_core_check_request(core, url, source_url, request_type);
  result.blocked = raw.blocked;
  if (raw.redirect) {
    result.redirect.assign(raw.redirect);
    maho_string_free(raw.redirect);
  }
  if (raw.rewritten_url) {
    result.rewritten_url.assign(raw.rewritten_url);
    maho_string_free(raw.rewritten_url);
  }
  return result;
}

int GetContentBlockingMode(MahoCore* core) {
  return maho_core_get_content_blocking_mode(core);
}

bool SetContentBlockingMode(MahoCore* core, int mode) {
  return maho_core_set_content_blocking_mode(core, mode);
}

std::string GetContentBlockerStateJson(MahoCore* core) {
  char* result = maho_core_get_content_blocker_state_json(core);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string AddFilterListResultJson(MahoCore* core,
                                    const char* id,
                                    const char* name,
                                    const char* url) {
  char* result = maho_core_add_filter_list_result_json(core, id, name, url);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string ToggleFilterListResultJson(MahoCore* core,
                                       const char* id,
                                       bool enabled) {
  char* result = maho_core_toggle_filter_list_result_json(core, id, enabled);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string RemoveFilterListResultJson(MahoCore* core, const char* id) {
  char* result = maho_core_remove_filter_list_result_json(core, id);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string ApplyFilterListUpdateResultJson(MahoCore* core,
                                            const char* json) {
  char* result = maho_core_apply_filter_list_update_result_json(core, json);
  if (!result) {
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string InstallContentEngineResultJson(MahoCore* core,
                                           OpaqueCompiledEngine* handle) {
  char* result = maho_content_engine_install_result_json(core, handle);
  if (!result) {
    return R"({"success":false,"error":{"code":"bridge_result_unavailable","message":"content engine install result was unavailable"},"compileRequired":false})";
  }
  std::string ret(result);
  maho_string_free(result);
  auto parsed = base::JSONReader::Read(ret, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() ||
      !parsed->GetDict().FindBool("success").has_value()) {
    return R"({"success":false,"error":{"code":"bridge_result_invalid","message":"content engine install returned an invalid result"},"compileRequired":false})";
  }
  return ret;
}

bool ApplyFilterListUpdateJson(MahoCore* core, const char* json) {
  char* result = maho_core_apply_filter_list_update_result_json(core, json);
  if (!result) {
    return false;
  }
  auto parsed = base::JSONReader::Read(result, base::JSON_PARSE_RFC);
  maho_string_free(result);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  return parsed->GetDict().FindBool("success").value_or(false) &&
         parsed->GetDict().FindBool("compileRequired").value_or(false);
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

std::string NormalizeSiteExceptionKey(const std::string& input) {
  char* result = maho_normalize_site_exception_key(input.c_str());
  if (!result) {
    return std::string();
  }
  std::string ret(result);
  FreeString(result);
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

  char* boosts_json = maho_core_get_boosts_for_url(core, url);
  if (!boosts_json) {
    return std::string();
  }
  auto parsed = base::JSONReader::Read(boosts_json, base::JSON_PARSE_RFC);
  maho_string_free(boosts_json);
  if (!parsed || !parsed->is_list() || parsed->GetList().empty()) {
    VLOG(1) << "[MahoBoost] GetBoostInjectionCss: no active boost for url="
            << url;
    return std::string();
  }
  const base::DictValue* boost = parsed->GetList().front().GetIfDict();
  const std::string* css = boost ? boost->FindString("customCss") : nullptr;
  if (!css) {
    return std::string();
  }
  VLOG(1) << "[MahoBoost] GetBoostInjectionCss: url=" << url
          << " css_length=" << css->size();
  return *css;
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

std::string AddBookmark(MahoCore* core,
                        const char* url,
                        const char* title,
                        const char* folder_id) {
  char* result = maho_core_add_bookmark_json(core, url, title, folder_id);
  if (!result) {
    return std::string();
  }
  std::string ret(result);
  maho_string_free(result);
  return ret;
}

std::string HandleEvent(MahoCore* core, const char* event_json) {
  char* result = maho_core_handle_event(core, event_json);
  if (!result) {
    LOG(WARNING) << "maho_core_handle_event failed or returned null for event: " << (event_json ? event_json : "null");
    return "{}";
  }
  std::string ret(result);
  maho_string_free(result);

  // A serde parse failure in maho_core_handle_event returns a structured payload
  // containing {"error":{...,"kind":"parse",...}}. serde_json sorts object keys
  // alphabetically (no preserve_order feature), so match order-independently:
  // the "kind":"parse" pair is contiguous regardless of sibling-key order. This
  // lightweight bridge TU does not expose base::Value::Dict; downstream callers
  // still receive "{}" so the update contract is unchanged.
  if (ret.find("\"error\"") != std::string::npos &&
      ret.find("\"kind\":\"parse\"") != std::string::npos) {
    LOG(ERROR) << "maho_core_handle_event parse error for event: "
               << (event_json ? event_json : "null") << " detail: " << ret;
    return "{}";
  }

  return ret;
}

std::string GetPasswordProviderRegistry() {
  char* raw_json = maho_core_get_password_provider_registry();
  if (!raw_json) {
    return "[]";
  }
  std::string json(raw_json);
  maho_string_free(raw_json);
  return json;
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

namespace maho {

TraceRecorder::TraceRecorder() : handle_(maho_trace_recorder_create()) {}

TraceRecorder::~TraceRecorder() {
  if (handle_) {
    maho_trace_recorder_destroy(handle_);
    handle_ = nullptr;
  }
}

void TraceRecorder::RecordClick(const std::optional<std::string>& target_ref,
                                double x,
                                double y) {
  if (!handle_) {
    return;
  }
  maho_trace_recorder_record_click(
      handle_, target_ref ? target_ref->c_str() : nullptr, x, y);
}

void TraceRecorder::RecordFill(const std::string& text,
                               const std::optional<std::string>& selector) {
  if (!handle_) {
    return;
  }
  maho_trace_recorder_record_fill(
      handle_, text.c_str(), selector ? selector->c_str() : nullptr);
}

void TraceRecorder::RecordNavigate(const std::string& url) {
  if (!handle_) {
    return;
  }
  maho_trace_recorder_record_navigate(handle_, url.c_str());
}

void TraceRecorder::RecordHover(const std::optional<std::string>& target_ref) {
  if (!handle_) {
    return;
  }
  maho_trace_recorder_record_hover(
      handle_, target_ref ? target_ref->c_str() : nullptr);
}

std::string TraceRecorder::FinishAndExportJson() {
  if (!handle_) {
    return "{}";
  }
  char* json = maho_trace_recorder_finish_json(handle_);
  handle_ = nullptr;  // finish_json deallocates the recorder
  if (!json) {
    return "{}";
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

}  // namespace maho
