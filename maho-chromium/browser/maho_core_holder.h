// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_CORE_HOLDER_H_
#define MAHO_BROWSER_MAHO_CORE_HOLDER_H_

#include <string>
#include <string_view>
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "maho/third_party/maho/maho_bridge.h"

struct MahoCore;
class PrefService;
class Profile;

namespace base {
class FilePath;
}  // namespace base

namespace maho {

::MahoCore* GetCore();
uint64_t GetCoreGeneration();

struct CoreTabFacts {
  std::string id;
  bool exists = false;
  bool pinned = false;
  bool close_protected = false;
};

enum class CoreEventStatus { kUnavailable, kStale, kFailed, kApplied };
struct CoreEventResult {
  CoreEventResult();
  CoreEventResult(const CoreEventResult&);
  CoreEventResult(CoreEventResult&&) noexcept;
  CoreEventResult& operator=(const CoreEventResult&);
  CoreEventResult& operator=(CoreEventResult&&) noexcept;
  ~CoreEventResult();

  CoreEventStatus status = CoreEventStatus::kUnavailable;
  uint64_t generation = 0;
  std::string updates_json;
  std::optional<CoreTabFacts> tab;
};

void DispatchCoreEvent(
    std::string event_json,
    base::OnceCallback<void(CoreEventResult)> reply);
std::optional<CoreTabFacts> GetCachedCoreTabFacts(std::string_view tab_id);
std::vector<CoreTabFacts> ReadCoreTabFactsSnapshot(::MahoCore* core);
void PublishCoreTabFacts(uint64_t generation,
                         std::vector<CoreTabFacts> facts);

void SetCore(::MahoCore* core);
void SetCoreForProfile(::MahoCore* core, Profile* owner_profile);
void SetCoreForProfile(::MahoCore* core, Profile* owner_profile,
                       std::vector<CoreTabFacts> facts);
Profile* GetCoreOwnerProfile();

// The single browser-process password profile-ownership check. Access is
// allowed only for the original regular Chromium profile that created the
// browser-global MahoCore; secondary and off-the-record profiles fail closed.
bool IsPasswordManagerAllowedForProfile(Profile* profile);

// Stable profile identity captured by factories that do not receive Profile*.
// The path distinguishes regular profiles; the PrefService pointer additionally
// distinguishes an OTR profile, whose GetPath() intentionally aliases its
// original profile. The pointer is used only as an opaque address string and is
// never dereferenced after construction.
std::string GetProfileIdentityKey(const base::FilePath& profile_path,
                                  const PrefService* profile_prefs);

scoped_refptr<base::SequencedTaskRunner> GetCoreTaskRunner();
base::Lock& GetCoreTaskAdmissionLock();
bool IsCoreTaskWorkQuiesced();
void QuiesceCoreTasksAndWait();

// Browser-process Vault lock-state broadcast. The periodic core tick is the
// only thing that can turn an unlocked Vault into an auto-locked one without a
// user-visible action, so the tick driver publishes every observed transition
// here and UI surfaces (settings panes, pending secret actions) subscribe
// instead of polling. `locked` is false only for the fully unlocked Vault;
// uninitialized/locked/auto-locked all report true so subscribers fail closed.
using VaultLockStateCallbackList = base::RepeatingCallbackList<void(bool)>;

// Published after a usable MahoCore replaces the unavailable state. Browser
// observers use this to retry work that intentionally failed closed while the
// core was not initialized.
using CoreReadyCallbackList = base::RepeatingCallbackList<void()>;

[[nodiscard]] base::CallbackListSubscription AddCoreReadyCallback(
    base::RepeatingClosure callback);

// Subscribes to Vault lock-state transitions. Callbacks run on the sequence
// that published the change (the UI thread for the tick driver). The returned
// subscription must be destroyed before the subscriber.
[[nodiscard]] base::CallbackListSubscription AddVaultLockStateChangeCallback(
    base::RepeatingCallback<void(bool locked)> callback);

// Publishes `locked` iff it differs from the last published value, so ordinary
// ticks that change nothing do not spam subscribers. Returns true when a change
// was actually broadcast.
bool NotifyVaultLockStateChanged(bool locked);

// Last published lock state (true == not usable for secret access). Defaults to
// true before any publication so a subscriber that queries before the first
// tick fails closed.
bool IsVaultLockedForUi();

maho::core::VaultBackendSession CreateVaultBackendSession();
maho::core::VaultBackendSession CreateVaultBackendSessionForProfile(
    Profile* profile);
maho::core::VaultBackendSession CreateVaultBackendSessionForProfileKey(
    std::string_view profile_key);
void QuiesceVaultBackendSessions();
bool IsVaultBackendSessionWorkQuiesced();

inline void PostCoreClosure(const base::Location& from_here,
                            base::OnceClosure closure) {
  base::AutoLock admission(GetCoreTaskAdmissionLock());
  if (IsCoreTaskWorkQuiesced()) {
    return;
  }
  GetCoreTaskRunner()->PostTask(from_here, std::move(closure));
}

inline void PostCoreTaskAndReply(const base::Location& from_here,
                                 base::OnceClosure task,
                                 base::OnceClosure reply) {
  bool posted = false;
  {
    base::AutoLock admission(GetCoreTaskAdmissionLock());
    if (!IsCoreTaskWorkQuiesced()) {
      auto split_reply = base::SplitOnceCallback(std::move(reply));
      posted = GetCoreTaskRunner()->PostTaskAndReply(
          from_here, std::move(task), std::move(split_reply.first));
      if (!posted) {
        reply = std::move(split_reply.second);
      }
    }
  }
  if (!posted) {
    std::move(reply).Run();
  }
}

template <typename R>
void PostCoreTaskAndReplyWithResult(const base::Location& from_here,
                                    base::OnceCallback<R()> task,
                                    base::OnceCallback<void(R)> reply) {
  bool posted = false;
  {
    base::AutoLock admission(GetCoreTaskAdmissionLock());
    if (!IsCoreTaskWorkQuiesced()) {
      auto split_reply = base::SplitOnceCallback(std::move(reply));
      posted = GetCoreTaskRunner()->PostTaskAndReplyWithResult(
          from_here, std::move(task), std::move(split_reply.first));
      if (!posted) {
        reply = std::move(split_reply.second);
      }
    }
  }
  if (!posted) {
    std::move(reply).Run(R{});
  }
}

template <typename R>
void PostCoreTask(const base::Location& from_here,
                  base::OnceCallback<R()> core_work,
                  base::OnceCallback<void(R)> reply) {
  PostCoreTaskAndReplyWithResult<R>(from_here, std::move(core_work),
                                    std::move(reply));
}

// Blocker-work quiesce contract: once quiesced, no new content-blocker
// compile/install work is accepted and late replies must not touch the core.
// Must be invoked during shutdown BEFORE SetCore(nullptr)/core destruction.
void QuiesceBlockerWork();
bool IsBlockerWorkQuiesced();
bool IsContentBlockerInstallResultSuccess(const std::string& result_json);

void PostBlockerEngineCompileAndInstall(const base::Location& from_here);

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_CORE_HOLDER_H_
