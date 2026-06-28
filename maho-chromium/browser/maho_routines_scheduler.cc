// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_routines_scheduler.h"

#include "base/time/time.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

MahoRoutinesScheduler::MahoRoutinesScheduler(MahoCore* core) : core_(core) {}

MahoRoutinesScheduler::~MahoRoutinesScheduler() {
  Stop();
}

void MahoRoutinesScheduler::Start() {
  timer_.Start(FROM_HERE, base::Minutes(1),
               base::BindRepeating(&MahoRoutinesScheduler::OnTick,
                                   base::Unretained(this)));
}

void MahoRoutinesScheduler::Stop() {
  timer_.Stop();
}

void MahoRoutinesScheduler::OnTick() {
  if (!core_) {
    return;
  }
  int64_t now_sec = base::Time::Now().InSecondsFSinceUnixEpoch();
  maho_routines_tick(core_, now_sec);
}

}  // namespace maho
