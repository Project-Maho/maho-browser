#include "maho/browser/maho_core_holder.h"
#include <future>

#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
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
#include "content/public/browser/browser_task_traits.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

struct MahoCore;

namespace maho {

CoreEventResult::CoreEventResult() = default;
CoreEventResult::CoreEventResult(const CoreEventResult&) = default;
CoreEventResult::CoreEventResult(CoreEventResult&&) noexcept = default;
CoreEventResult& CoreEventResult::operator=(const CoreEventResult&) = default;
CoreEventResult& CoreEventResult::operator=(CoreEventResult&&) noexcept = default;
CoreEventResult::~CoreEventResult() = default;

namespace {
std::atomic<::MahoCore*> g_core{nullptr};
std::atomic<Profile*> g_core_owner_profile{nullptr};
std::atomic<bool> g_blocker_quiesced{false};
std::atomic<bool> g_vault_backend_quiesced{false};
std::atomic<bool> g_core_tasks_quiesced{true};
std::atomic<uint64_t> g_core_generation{0};
struct CachedTabFacts {
  uint64_t generation;
  CoreTabFacts facts;
};
base::NoDestructor<std::map<std::string, CachedTabFacts>> g_tab_facts;

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

CoreReadyCallbackList& GetCoreReadyCallbackList() {
  static base::NoDestructor<CoreReadyCallbackList> callbacks;
  return *callbacks;
}

void NotifyCoreReady(uint64_t generation) {
  DCHECK(!content::BrowserThread::IsThreadInitialized(
             content::BrowserThread::UI) ||
         content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (g_core_generation.load(std::memory_order_acquire) != generation ||
      !GetCore()) {
    return;
  }
  GetCoreReadyCallbackList().Notify();
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
          {base::TaskPriority::USER_VISIBLE, base::MayBlock(),
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
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

uint64_t GetCoreGeneration() {
  return g_core_generation.load(std::memory_order_acquire);
}

std::optional<CoreTabFacts> GetCachedCoreTabFacts(std::string_view tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = g_tab_facts->find(std::string(tab_id));
  if (it == g_tab_facts->end() ||
      it->second.generation != GetCoreGeneration()) {
    return std::nullopt;
  }
  return it->second.facts;
}

void PublishCoreTabFacts(uint64_t generation,
                         std::vector<CoreTabFacts> facts) {
  DCHECK(!content::BrowserThread::IsThreadInitialized(content::BrowserThread::UI) ||
         content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (generation != GetCoreGeneration()) {
    return;
  }
  for (auto& fact : facts) {
    if (!fact.exists) {
      g_tab_facts->erase(fact.id);
      continue;
    }
    const std::string id = fact.id;
    g_tab_facts->insert_or_assign(
        id, CachedTabFacts{generation, std::move(fact)});
  }
}

std::vector<CoreTabFacts> ReadCoreTabFactsSnapshot(::MahoCore* core) {
  DCHECK(GetCoreTaskRunner()->RunsTasksInCurrentSequence());
  std::vector<CoreTabFacts> result;
  if (!core) {
    return result;
  }
  std::unique_ptr<char, decltype(&maho_string_free)> json(
      maho_core_get_tab_view_models(core), &maho_string_free);
  if (!json) {
    return result;
  }
  auto tabs = base::JSONReader::Read(json.get(), base::JSON_PARSE_RFC);
  if (!tabs || !tabs->is_list()) {
    return result;
  }
  for (const auto& tab : tabs->GetList()) {
    const std::string* id =
        tab.is_dict() ? tab.GetDict().FindString("id") : nullptr;
    if (id) {
      result.push_back({*id, true,
                        maho_core_is_tab_pinned(core, id->c_str()),
                        maho_core_is_tab_close_protected(core, id->c_str())});
    }
  }
  return result;
}

void DispatchCoreEvent(
    std::string event_json,
    base::OnceCallback<void(CoreEventResult)> reply) {
  const uint64_t generation = GetCoreGeneration();
  PostCoreTask<CoreEventResult>(
      FROM_HERE,
      base::BindOnce(
          [](uint64_t generation, std::string json) {
            CoreEventResult result;
            result.generation = generation;
            if (generation != GetCoreGeneration()) {
              result.status = CoreEventStatus::kStale;
              return result;
            }
            MahoCore* core = GetCore();
            if (!core) {
              return result;
            }
            auto event = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
            if (!event || !event->is_dict()) {
              result.status = CoreEventStatus::kFailed;
              return result;
            }
            auto& payload = event->GetDict();
            const std::string* id = payload.FindString("tab_id");
            if (!id) {
              id = payload.FindString("id");
            }
            const std::string tab_id = id ? *id : std::string();
            const std::string* kind = payload.FindString("kind");
            if (kind && *kind == "close_tab" && !tab_id.empty() &&
                maho_core_is_tab_close_protected(core, tab_id.c_str())) {
              payload.Set("kind", "suspend_tab");
              if (!base::JSONWriter::Write(*event, &json)) {
                result.status = CoreEventStatus::kFailed;
                return result;
              }
            }
            result.updates_json = maho::core::HandleEvent(core, json.c_str());
            auto updates = base::JSONReader::Read(
                result.updates_json, base::JSON_PARSE_RFC);
            if (!updates || !updates->is_list()) {
              result.status = CoreEventStatus::kFailed;
              return result;
            }
            if (!tab_id.empty()) {
              std::unique_ptr<char, decltype(&maho_string_free)> tab(
                  maho_core_get_tab_snapshot_by_id(core, tab_id.c_str()),
                  &maho_string_free);
              const bool exists = tab && std::string_view(tab.get()) != "null";
              result.tab = CoreTabFacts{
                  tab_id, exists,
                  exists && maho_core_is_tab_pinned(core, tab_id.c_str()),
                  exists && maho_core_is_tab_close_protected(
                                core, tab_id.c_str())};
            }
            result.status = CoreEventStatus::kApplied;
            return result;
          },
          generation, std::move(event_json)),
      base::BindOnce(
          [](base::OnceCallback<void(CoreEventResult)> reply,
             CoreEventResult result) {
            if (result.generation != GetCoreGeneration()) {
              result.status = CoreEventStatus::kStale;
            }
            if (result.status == CoreEventStatus::kApplied && result.tab) {
              PublishCoreTabFacts(result.generation, {*result.tab});
            }
            std::move(reply).Run(std::move(result));
          },
          std::move(reply)));
}

void SetCore(::MahoCore* core) {
  SetCoreForProfile(core, nullptr);
}

void SetCoreForProfile(::MahoCore* core, Profile* owner_profile) {
  SetCoreForProfile(core, owner_profile, {});
}

void SetCoreForProfile(::MahoCore* core, Profile* owner_profile,
                       std::vector<CoreTabFacts> facts) {
  uint64_t generation;
  {
    base::AutoLock lock(GetVaultBackendCreationLock());
    generation = g_core_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
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
  auto publish = base::BindOnce(
      [](uint64_t generation, bool ready, std::vector<CoreTabFacts> facts) {
        if (generation != GetCoreGeneration()) {
          return;
        }
        g_tab_facts->clear();
        PublishCoreTabFacts(generation, std::move(facts));
        if (ready) {
          NotifyCoreReady(generation);
        }
      },
      generation, core != nullptr, std::move(facts));
  if (!content::BrowserThread::IsThreadInitialized(content::BrowserThread::UI) ||
      content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    std::move(publish).Run();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(FROM_HERE, std::move(publish));
  }
}

base::CallbackListSubscription AddCoreReadyCallback(
    base::RepeatingClosure callback) {
  DCHECK(!content::BrowserThread::IsThreadInitialized(
             content::BrowserThread::UI) ||
         content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  return GetCoreReadyCallbackList().Add(std::move(callback));
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
  std::promise<void> drained;
  auto completion = drained.get_future();
  {
    base::AutoLock admission(GetCoreTaskAdmissionLock());
    if (g_core_tasks_quiesced.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    if (!GetCoreTaskRunner()->PostTask(
            FROM_HERE, base::BindOnce(
                           [](std::promise<void> done) { done.set_value(); },
                           std::move(drained)))) {
      return;
    }
  }
  // Chromium services may already be torn down. Do not dispatch pending UI IPC
  // while draining the core sequence during shutdown.
  completion.wait();
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
