// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_ROUTINES_SCHEDULER_H_
#define MAHO_BROWSER_MAHO_ROUTINES_SCHEDULER_H_

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"

struct MahoCore;

namespace maho {

class MahoRoutinesScheduler {
 public:
  explicit MahoRoutinesScheduler(MahoCore* core);
  ~MahoRoutinesScheduler();

  MahoRoutinesScheduler(const MahoRoutinesScheduler&) = delete;
  MahoRoutinesScheduler& operator=(const MahoRoutinesScheduler&) = delete;

  void Start();
  void Stop();

 private:
  void OnTick();

  raw_ptr<MahoCore> core_;
  base::RepeatingTimer timer_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_ROUTINES_SCHEDULER_H_
