// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_H_
#define MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"

namespace maho {

// FfiCallbackHandle<T>: typed RAII wrapper for the `void* user_data` pointer
// passed to Rust FFI callback APIs (maho_core_chat_set_event_sink,
// maho_agent_send_message, maho_routines_run).
//
// Audit findings closed: C5 (user_data_handle_ leak), H6 (no callback
// lifetime contract), part of C2 (UAF when async callbacks fire after
// owner destruction).
//
// === Ownership model (Option D) ===
//
// On construction, the handle allocates a shared_ptr<Pointee> and a heap
// bridge that owns another shared_ptr copy. Bridges start C++-owned, so
// Cancel() and destruction delete them locally.
//
// HandOffToProducer() transfers bridge deletion to the FFI producer. A leased
// bridge remains valid after the wrapper is destroyed, but its Pointee is
// cancelled. The producer must call ReleaseFromProducer() exactly once after
// it can no longer invoke callbacks. Local shared_ptr copies may outlive that
// release.
//
// Contract:
//   - Construct with the owner's WeakPtr<T> and a SequencedTaskRunner.
//   - Pass user_data() as the C ABI's `void* user_data` parameter.
//   - In callback thunks, call FromUserData(user_data) to get a local
//     shared_ptr<Pointee>. Check cancelled.load() to short-circuit.
//     If alive, PostTask onto the sequence using the WeakPtr.
//   - Call HandOffToProducer() only after the producer accepts user_data(); if
//     that call fails synchronously, release the producer lease immediately.
//   - After ReleaseFromProducer(), user_data is invalid and must not be read.
template <typename T>
class FfiCallbackHandle {
 public:
  struct Pointee {
    explicit Pointee(uint64_t generation = 0) : generation(generation) {}

    const uint64_t generation;
    std::atomic<bool> cancelled{false};
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    base::WeakPtr<T> owner;
  };

  FfiCallbackHandle(scoped_refptr<base::SequencedTaskRunner> runner,
                    base::WeakPtr<T> owner,
                    uint64_t generation = 0);
  ~FfiCallbackHandle();

  FfiCallbackHandle(const FfiCallbackHandle&) = delete;
  FfiCallbackHandle& operator=(const FfiCallbackHandle&) = delete;
  FfiCallbackHandle(FfiCallbackHandle&&) = delete;
  FfiCallbackHandle& operator=(FfiCallbackHandle&&) = delete;

  // Returns the void* to hand to Rust as `user_data`.
  // Caller MUST NOT free this pointer while the handle owns it.
  // Callers MAY pass it to a Rust FFI function that re-enters via the
  // callback signature `void (*cb)(void* user_data, ...)`.
  void* user_data() const;

  // Atomically marks the underlying pointee cancelled. C++-owned bridges are
  // freed locally; producer-leased bridges remain valid until released by the
  // producer.
  // Idempotent: second call is a no-op.
  void Cancel();

  bool is_cancelled() const;

  // Transfers one bridge deletion obligation to the producer. The wrapper
  // relinquishes its raw bridge pointer, so it remains safe if the producer
  // releases before this wrapper is destroyed.
  void HandOffToProducer();

  // Releases a bridge previously handed to the producer. Invalid or repeated
  // releases DCHECK in debug builds and return without dereferencing user_data.
  static void ReleaseFromProducer(void* user_data);

  // For callback implementations: retrieves the shared Pointee from a
  // user_data void*. The caller receives a shared_ptr<Pointee> that keeps
  // the Pointee alive regardless of handle lifetime. Caller must hold the
  // resulting shared_ptr for the duration of any work using it.
  //
  // SAFETY: `user_data` MUST be a pointer previously returned by user_data()
  // whose bridge has not been released. A producer-leased bridge remains valid
  // after wrapper destruction but not after ReleaseFromProducer().
  static std::shared_ptr<Pointee> FromUserData(void* user_data);

 private:
  struct Bridge {
    explicit Bridge(std::shared_ptr<Pointee> pointee)
        : pointee(std::move(pointee)) {}

    std::shared_ptr<Pointee> pointee;
    std::atomic<bool> producer_leased{false};
  };

  static base::Lock& ProducerLeaseLock();
  static std::set<Bridge*>& ProducerLeases();

  std::shared_ptr<Pointee> pointee_;

  raw_ptr<Bridge> user_data_bridge_ = nullptr;
};

}  // namespace maho

// Template definitions — must be header-resident.
#include "maho/browser/ai/maho_ffi_callback_handle_inl.h"

#endif  // MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_H_
