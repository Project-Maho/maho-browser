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
}

void MahoRoutinesScheduler::OnTick() {
  RunTick(base::Time::Now().InSecondsFSinceUnixEpoch());
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

void MahoRoutinesScheduler::PublishVaultLockState() {
  if (!core_) {
    return;
  }
  char* status_json = maho_vault_status_json(core_);
  if (!status_json) {
    // The status read itself failed; publish the fail-closed state rather than
    // leaving a stale "unlocked" broadcast in place.
    maho::NotifyVaultLockStateChanged(/*locked=*/true);
    return;
  }
  const std::string status(status_json);
  maho_string_free(status_json);
  maho::NotifyVaultLockStateChanged(IsLockedVaultStatusJson(status));
}

void MahoRoutinesScheduler::RunTick(int64_t now_sec) {
  if (!core_) {
    return;
  }
  maho_routines_tick(core_, now_sec);
  const int32_t archived_count =
      maho_core_auto_archive_conversations(core_, now_sec);
  // -2 means the user-disabled policy had no archive work. Other negative
  // values are genuine failures, so stop the pass rather than operating on a
  // core whose scheduled maintenance just failed.
  if (archived_count < 0 && archived_count != -2) {
    return;
  }

  // Desktop's only driver of the core periodic tick, which is where the Vault
  // inactivity auto-lock is evaluated (MahoCore::tick_at). The injected
  // `now_sec` reaches the same evaluation the production path uses (OnTick
  // passes the wall clock), so RunTickForTesting actually exercises auto-lock.
  // The tick itself is deliberately NOT counted as user activity by the core,
  // so running it on the scheduler cadence cannot postpone an auto-lock. The
  // returned update JSON is owned by the caller; nothing here consumes it, so
  // free it immediately.
  if (char* updates_json = maho_core_tick_at(core_, now_sec)) {
    maho_string_free(updates_json);
  }

  // Broadcast whatever lock state the tick left behind, so UI surfaces holding
  // transient secret state re-gate as soon as the Vault auto-locks.
  PublishVaultLockState();
}

}  // namespace maho
