// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_CORE_HOLDER_H_
#define MAHO_BROWSER_MAHO_CORE_HOLDER_H_

#include <string>
#include <utility>

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

namespace maho {

::MahoCore* GetCore();

void SetCore(::MahoCore* core);
scoped_refptr<base::SequencedTaskRunner> GetCoreTaskRunner();
base::Lock& GetCoreTaskAdmissionLock();
bool IsCoreTaskWorkQuiesced();
void QuiesceCoreTasksAndWait();

maho::core::VaultBackendSession CreateVaultBackendSession();
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
