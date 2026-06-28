// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_CORE_HOLDER_H_
#define MAHO_BROWSER_MAHO_CORE_HOLDER_H_

#include <utility>

#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"

struct MahoCore;

namespace maho {

::MahoCore* GetCore();

void SetCore(::MahoCore* core);

inline void PostCoreClosure(const base::Location& from_here,
                            base::OnceClosure closure) {
  base::ThreadPool::PostTask(
      from_here,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      std::move(closure));
}

inline void PostCoreTaskAndReply(const base::Location& from_here,
                                 base::OnceClosure task,
                                 base::OnceClosure reply) {
  base::ThreadPool::PostTaskAndReply(
      from_here,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      std::move(task),
      std::move(reply));
}

template <typename R>
void PostCoreTaskAndReplyWithResult(const base::Location& from_here,
                                    base::OnceCallback<R()> task,
                                    base::OnceCallback<void(R)> reply) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      from_here,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      std::move(task),
      std::move(reply));
}

template <typename R>
void PostCoreTask(const base::Location& from_here,
                  base::OnceCallback<R()> core_work,
                  base::OnceCallback<void(R)> reply) {
  PostCoreTaskAndReplyWithResult<R>(from_here, std::move(core_work),
                                    std::move(reply));
}

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_CORE_HOLDER_H_
