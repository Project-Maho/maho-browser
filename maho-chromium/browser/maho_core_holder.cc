#include "maho/browser/maho_core_holder.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

struct MahoCore;

namespace maho {

namespace {
std::atomic<::MahoCore*> g_core{nullptr};
std::atomic<Profile*> g_core_owner_profile{nullptr};
std::atomic<bool> g_blocker_quiesced{false};
std::atomic<bool> g_vault_backend_quiesced{false};
std::atomic<bool> g_core_tasks_quiesced{true};

base::Lock& GetVaultBackendCreationLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

// Vault lock-state broadcast state. Both the list and the cached value are
// owned by the publishing sequence (BrowserThread::UI in production, the test
// sequence in unit tests); they are never touched from the core task runner.
VaultLockStateCallbackList& GetVaultLockStateCallbackList() {
  static base::NoDestructor<VaultLockStateCallbackList> callbacks;
  return *callbacks;
}

// Fail-closed default: nothing has published a state yet, so treat the Vault as
// unusable for secret access.
bool g_vault_locked_for_ui = true;

// UI-sequence-owned compile coalescing state. Both flags are only ever touched
// on the BrowserThread::UI sequence (the PostBlockerEngineCompileAndInstall
// entrypoint and the PostTaskAndReplyWithResult reply, which runs back on UI),
// so they are plain bools rather than atomics. g_blocker_quiesced stays atomic
// because QuiesceBlockerWork may run on another thread during shutdown; the
// UI-sequence reply reads it to suppress any queued rerun.
bool g_compile_in_flight = false;
bool g_compile_coalesced = false;

scoped_refptr<base::SequencedTaskRunner>& CoreTaskRunnerStorage() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_VISIBLE, base::MayBlock()}));
  return *runner;
}

// RAII owners for the opaque content-blocker handles. Ownership crosses
// several callback boundaries (worker task, UI reply) that Chromium may drop
// during shutdown; wrapping the raw handles guarantees each is freed exactly
// once when a task/reply is dropped, while still allowing a successful
// hand-off to release ownership to the consuming FFI call. Both free functions
// are null-safe, so default-constructed and moved-from owners destruct safely.
struct CompileSnapshotDeleter {
  void operator()(::OpaqueCompileSnapshotHandle* snapshot) const {
    maho_compile_snapshot_free(snapshot);
  }
};
using ScopedCompileSnapshot =
    std::unique_ptr<::OpaqueCompileSnapshotHandle, CompileSnapshotDeleter>;

struct CompiledEngineDeleter {
  void operator()(::OpaqueCompiledEngineHandle* handle) const {
    maho_content_engine_free(handle);
  }
};
using ScopedCompiledEngine =
    std::unique_ptr<::OpaqueCompiledEngineHandle, CompiledEngineDeleter>;
}  // namespace

::MahoCore* GetCore() {
  return g_core.load(std::memory_order_acquire);
}

void SetCore(::MahoCore* core) {
  SetCoreForProfile(core, nullptr);
}

void SetCoreForProfile(::MahoCore* core, Profile* owner_profile) {
  base::AutoLock lock(GetVaultBackendCreationLock());
  if (!core) {
    g_vault_backend_quiesced.store(true, std::memory_order_release);
    maho::core::QuiesceVaultBackendSessions();
    g_core_owner_profile.store(nullptr, std::memory_order_release);
  } else {
    g_vault_backend_quiesced.store(false, std::memory_order_release);
    g_core_tasks_quiesced.store(false, std::memory_order_release);
    g_core_owner_profile.store(owner_profile, std::memory_order_release);
  }
  g_core.store(core, std::memory_order_release);
}

Profile* GetCoreOwnerProfile() {
  return g_core_owner_profile.load(std::memory_order_acquire);
}

bool IsPasswordManagerAllowedForProfile(Profile* profile) {
  Profile* owner_profile = GetCoreOwnerProfile();
  return GetCore() && profile && owner_profile && !profile->IsOffTheRecord() &&
         profile->IsRegularProfile() && profile == owner_profile;
}

std::string GetProfileIdentityKey(const base::FilePath& profile_path,
                                  const PrefService* profile_prefs) {
  return profile_path.AsUTF8Unsafe() + "#" +
         base::NumberToString(reinterpret_cast<uintptr_t>(profile_prefs));
}

scoped_refptr<base::SequencedTaskRunner> GetCoreTaskRunner() {
  return CoreTaskRunnerStorage();
}

base::Lock& GetCoreTaskAdmissionLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

bool IsCoreTaskWorkQuiesced() {
  return g_core_tasks_quiesced.load(std::memory_order_acquire);
}

void QuiesceCoreTasksAndWait() {
  base::RunLoop drained;
  {
    base::AutoLock admission(GetCoreTaskAdmissionLock());
    if (g_core_tasks_quiesced.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    if (!GetCoreTaskRunner()->PostTaskAndReply(
            FROM_HERE, base::DoNothing(), drained.QuitClosure())) {
      return;
    }
  }
  drained.Run();
}

base::CallbackListSubscription AddVaultLockStateChangeCallback(
    base::RepeatingCallback<void(bool)> callback) {
  return GetVaultLockStateCallbackList().Add(std::move(callback));
}

bool NotifyVaultLockStateChanged(bool locked) {
  if (g_vault_locked_for_ui == locked) {
    return false;
  }
  g_vault_locked_for_ui = locked;
  GetVaultLockStateCallbackList().Notify(locked);
  return true;
}

bool IsVaultLockedForUi() {
  return g_vault_locked_for_ui;
}

maho::core::VaultBackendSession CreateVaultBackendSession() {
  return CreateVaultBackendSessionForProfile(GetCoreOwnerProfile());
}

maho::core::VaultBackendSession CreateVaultBackendSessionForProfile(
    Profile* profile) {
  if (!profile) {
    return maho::core::VaultBackendSession();
  }
  return CreateVaultBackendSessionForProfileKey(
      GetProfileIdentityKey(profile->GetPath(), profile->GetPrefs()));
}

maho::core::VaultBackendSession CreateVaultBackendSessionForProfileKey(
    std::string_view profile_key) {
  base::AutoLock lock(GetVaultBackendCreationLock());
  Profile* owner_profile = g_core_owner_profile.load(std::memory_order_acquire);
  if (g_vault_backend_quiesced.load(std::memory_order_acquire) ||
      !owner_profile || owner_profile->IsOffTheRecord() ||
      !owner_profile->IsRegularProfile() ||
      profile_key != GetProfileIdentityKey(owner_profile->GetPath(),
                                           owner_profile->GetPrefs())) {
    return maho::core::VaultBackendSession();
  }
  return maho::core::CreateVaultBackendSession(
      g_core.load(std::memory_order_acquire));
}

void QuiesceVaultBackendSessions() {
  base::AutoLock lock(GetVaultBackendCreationLock());
  g_vault_backend_quiesced.store(true, std::memory_order_release);
  maho::core::QuiesceVaultBackendSessions();
}

bool IsVaultBackendSessionWorkQuiesced() {
  return g_vault_backend_quiesced.load(std::memory_order_acquire);
}

void QuiesceBlockerWork() {
  g_blocker_quiesced.store(true, std::memory_order_release);
}

bool IsBlockerWorkQuiesced() {
  return g_blocker_quiesced.load(std::memory_order_acquire);
}

bool IsContentBlockerInstallResultSuccess(const std::string& result_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed.value().is_dict()) {
    return false;
  }
  const base::DictValue& result = parsed.value().GetDict();
  return result.FindBool("success").value_or(false) &&
         !result.FindDict("error");
}

void PostBlockerEngineCompileAndInstall(const base::Location& from_here) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (IsBlockerWorkQuiesced()) {
    return;
  }
  ::MahoCore* core = GetCore();
  if (!core) {
    return;
  }

  // At most one worker compile may be active. A request arriving while one is
  // in flight is collapsed into a single coalesced flag; the reply then kicks
  // off exactly one follow-up so it snapshots the latest pending set (including
  // any candidate added mid-compile) instead of racing the active snapshot.
  if (g_compile_in_flight) {
    g_compile_coalesced = true;
    return;
  }

  ScopedCompileSnapshot snapshot(maho_core_create_compile_snapshot(core));
  if (!snapshot) {
    return;
  }

  g_compile_in_flight = true;

  // maho_compile_engine_from_snapshot consumes (frees) the snapshot and
  // maho_content_engine_install consumes (frees) the compiled handle; ownership
  // is released to those calls exactly at hand-off. Any dropped task/reply
  // (shutdown) or false PostTaskAndReplyWithResult return destroys the bound
  // RAII owners without running, freeing each handle exactly once.
  const bool posted = base::ThreadPool::PostTaskAndReplyWithResult(
      from_here, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(
          [](ScopedCompileSnapshot snapshot) -> ScopedCompiledEngine {
            return ScopedCompiledEngine(
                maho_compile_engine_from_snapshot(snapshot.release()));
          },
          std::move(snapshot)),
      base::BindOnce([](ScopedCompiledEngine engine) {
        DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
        ::MahoCore* core = GetCore();
        if (core && engine && !IsBlockerWorkQuiesced()) {
          const std::string install_result =
              maho::core::InstallContentEngineResultJson(core,
                                                         engine.release());
          if (!IsContentBlockerInstallResultSuccess(install_result)) {
            LOG(WARNING) << "Content blocker engine install failed: "
                         << install_result;
          }
        }
        // This compile is finished regardless of install success/null/stale.
        // Clear in-flight, then fire exactly one follow-up iff a request was
        // coalesced and shutdown/quiesce has not intervened. Clearing the flag
        // before the guarded rerun keeps the state machine deterministic and
        // prevents spinning when nothing is pending.
        g_compile_in_flight = false;
        const bool run_follow_up = g_compile_coalesced;
        g_compile_coalesced = false;
        if (run_follow_up && !IsBlockerWorkQuiesced() && GetCore()) {
          PostBlockerEngineCompileAndInstall(FROM_HERE);
        }
      }));

  // A refused post (shutdown) means the reply will never run to clear the flag,
  // so restore state here. RAII in the dropped callbacks already freed the
  // handles; do not schedule a rerun during shutdown.
  if (!posted) {
    g_compile_in_flight = false;
    g_compile_coalesced = false;
  }
}

}  // namespace maho
