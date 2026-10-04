// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_read_bridge.h"

#include <atomic>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"

namespace maho {

namespace {

// Deterministic error surfaced when the FFI rejects the call outright (its
// callback contract then guarantees the callback will not fire).
constexpr char kMahoMailReadRejected[] = "maho mail read rejected by backend";

// Reference-counted, thread-safe reply carrier. Holds the sequenced reply and
// the origin task runner. Resolve() is idempotent and always posts the reply
// back to the captured sequence, so the reply never runs on a Rust thread.
class ReadContext : public base::RefCountedThreadSafe<ReadContext> {
 public:
  ReadContext(scoped_refptr<base::SequencedTaskRunner> reply_runner,
              MahoMailReadBridge::ReplyCallback reply)
      : reply_runner_(std::move(reply_runner)), reply_(std::move(reply)) {}

  ReadContext(const ReadContext&) = delete;
  ReadContext& operator=(const ReadContext&) = delete;

  // Resolves the reply exactly once. Safe to call from any thread; the first
  // caller wins and posts, later callers no-op.
  void Resolve(bool ok, std::string json) {
    if (resolved_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    reply_runner_->PostTask(
        FROM_HERE, base::BindOnce(std::move(reply_), ok, std::move(json)));
  }

 private:
  friend class base::RefCountedThreadSafe<ReadContext>;
  ~ReadContext() = default;

  const scoped_refptr<base::SequencedTaskRunner> reply_runner_;
  MahoMailReadBridge::ReplyCallback reply_;
  std::atomic<bool> resolved_{false};
};

// extern-C trampoline handed to the FFI. `user_data` owns exactly one
// reference to the ReadContext; this function releases it after resolving. The
// Rust-owned C string is copied into a std::string before returning, per the
// callback-duration ownership contract.
extern "C" void OnMahoMailFfiRead(bool ok, const char* json, void* user_data) {
  ReadContext* context = static_cast<ReadContext*>(user_data);
  std::string copied = json ? std::string(json) : std::string();
  context->Resolve(ok, std::move(copied));
  context->Release();
}

}  // namespace

// static
void MahoMailReadBridge::Start(FfiInvoker invoker, ReplyCallback reply) {
  auto context = base::MakeRefCounted<ReadContext>(
      base::SequencedTaskRunner::GetCurrentDefault(), std::move(reply));

  // Hand one reference to the FFI as opaque user_data. The local `context`
  // scoped_refptr keeps the object alive across invoke.Run() even if the Rust
  // runtime fires (and releases via the trampoline) on another thread before
  // Run() returns.
  context->AddRef();
  void* user_data = context.get();

  const bool accepted = std::move(invoker).Run(&OnMahoMailFfiRead, user_data);
  if (!accepted) {
    // Contract: the callback will not fire. Reclaim the FFI reference and
    // resolve deterministically. Resolve() is idempotent.
    context->Resolve(false, kMahoMailReadRejected);
    context->Release();
  }
}

}  // namespace maho
