// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_routines_scheduler.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

MahoRoutinesScheduler::MahoRoutinesScheduler(MahoCore* core) : core_(core) {}

MahoRoutinesScheduler::~MahoRoutinesScheduler() {
  Stop();
}

void MahoRoutinesScheduler::Start() {
  timer_.Start(FROM_HERE, kStatusTickInterval,
               base::BindRepeating(&MahoRoutinesScheduler::OnTick,
                                   base::Unretained(this)));
}

void MahoRoutinesScheduler::Stop() {
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  tick_in_flight_ = false;
}

void MahoRoutinesScheduler::OnTick() {
  if (tick_in_flight_) {
    return;
  }
  tick_in_flight_ = true;
  const uint64_t generation = GetCoreGeneration();
  PostCoreTask<std::optional<bool>>(
      FROM_HERE,
      base::BindOnce([](uint64_t generation, int64_t now_sec) {
        if (generation != GetCoreGeneration()) {
          return std::optional<bool>();
        }
        return EvaluateTick(GetCore(), now_sec);
      }, generation, base::Time::Now().InSecondsFSinceUnixEpoch()),
      base::BindOnce([](base::WeakPtr<MahoRoutinesScheduler> owner,
                        uint64_t generation, std::optional<bool> locked) {
        if (!owner) {
          return;
        }
        owner->tick_in_flight_ = false;
        if (generation == GetCoreGeneration() && locked) {
          NotifyVaultLockStateChanged(*locked);
        }
      }, weak_factory_.GetWeakPtr(), generation));
}

void MahoRoutinesScheduler::RunTickForTesting(int64_t now_sec) {
  RunTick(now_sec);
}

// static
bool MahoRoutinesScheduler::IsLockedVaultStatusJson(
    const std::string& status_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(status_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return true;
  }
  const base::DictValue& root = parsed->GetDict();
  if (!root.FindBool("ok").value_or(false)) {
    return true;
  }
  const base::DictValue* data = root.FindDict("data");
  if (!data) {
    return true;
  }
  const std::string* lock_state = data->FindString("lockState");
  return !lock_state || *lock_state != "unlocked";
}

void MahoRoutinesScheduler::RunTick(int64_t now_sec) {
  if (auto locked = EvaluateTick(core_, now_sec)) {
    NotifyVaultLockStateChanged(*locked);
  }
}

std::optional<bool> MahoRoutinesScheduler::EvaluateTick(
    MahoCore* core, int64_t now_sec) {
  if (!core) {
    return std::nullopt;
  }
  maho_routines_tick(core, now_sec);
  const int32_t archived_count =
      maho_core_auto_archive_conversations(core, now_sec);
  // -2 means the user-disabled policy had no archive work. Other negative
  // values are genuine failures, so stop the pass rather than operating on a
  // core whose scheduled maintenance just failed.
  if (archived_count < 0 && archived_count != -2) {
    return std::nullopt;
  }

  // Desktop's only driver of the core periodic tick, which is where the Vault
  // inactivity auto-lock is evaluated (MahoCore::tick_at). The injected
  // `now_sec` reaches the same evaluation the production path uses (OnTick
  // passes the wall clock), so RunTickForTesting actually exercises auto-lock.
  // The tick itself is deliberately NOT counted as user activity by the core,
  // so running it on the scheduler cadence cannot postpone an auto-lock. The
  // returned update JSON is owned by the caller; nothing here consumes it, so
  // free it immediately.
  if (char* updates_json = maho_core_tick_at(core, now_sec)) {
    maho_string_free(updates_json);
  }

  // Broadcast whatever lock state the tick left behind, so UI surfaces holding
  // transient secret state re-gate as soon as the Vault auto-locks.
  char* status_json = maho_vault_status_json(core);
  if (!status_json) {
    return true;
  }
  const bool locked = IsLockedVaultStatusJson(status_json);
  maho_string_free(status_json);
  return locked;
}

}  // namespace maho
