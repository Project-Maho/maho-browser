// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_H_
#define MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_H_

#include <atomic>
#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
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
// On construction, the handle allocates:
//   1. A shared_ptr<Pointee> (the reference-counted payload).
//   2. A heap-allocated `std::shared_ptr<Pointee>*` (the "user_data bridge").
//
// `user_data()` returns the raw address of (2). Rust holds this pointer and
// passes it back in callbacks. In callback thunks, C++ does:
//   auto sp = *static_cast<std::shared_ptr<Pointee>*>(user_data);
// This copies the shared_ptr into a local, incrementing the refcount. The
// local keeps the Pointee alive for the duration of the callback, even if
// the handle is concurrently destroyed.
//
// On Cancel() / destruction:
//   - cancelled.store(true) atomically marks future callbacks as no-ops.
//   - The bridge pointer is deleted exactly once (guarded by freed_ flag),
//     decrementing the shared_ptr refcount. The Pointee is destroyed only
//     when the last in-flight callback drops its local shared_ptr copy.
//
// This mirrors Rust's `Box::into_raw` pattern: one heap allocation per
// handle, one delete per handle, refcounting via shared_ptr handles
// in-flight callbacks.
//
// Contract:
//   - Construct with the owner's WeakPtr<T> and a SequencedTaskRunner.
//   - Pass user_data() as the C ABI's `void* user_data` parameter.
//   - In callback thunks, call FromUserData(user_data) to get a local
//     shared_ptr<Pointee>. Check cancelled.load() to short-circuit.
//     If alive, PostTask onto the sequence using the WeakPtr.
//   - On handle destruction, Cancel() marks callbacks as no-ops and
//     frees the bridge pointer. The Pointee outlives any in-flight
//     callback because they hold their own shared_ptr copies.
template <typename T>
class FfiCallbackHandle {
 public:
  struct Pointee {
    std::atomic<bool> cancelled{false};
    scoped_refptr<base::SequencedTaskRunner> task_runner;
    base::WeakPtr<T> owner;
  };

  FfiCallbackHandle(scoped_refptr<base::SequencedTaskRunner> runner,
                    base::WeakPtr<T> owner);
  ~FfiCallbackHandle();

  FfiCallbackHandle(const FfiCallbackHandle&) = delete;
  FfiCallbackHandle& operator=(const FfiCallbackHandle&) = delete;
  FfiCallbackHandle(FfiCallbackHandle&&) = delete;
  FfiCallbackHandle& operator=(FfiCallbackHandle&&) = delete;

  // Returns the void* to hand to Rust as `user_data`.
  // Caller MUST NOT free this pointer; the handle owns it.
  // Callers MAY pass it to a Rust FFI function that re-enters via the
  // callback signature `void (*cb)(void* user_data, ...)`.
  void* user_data() const;

  // Atomically marks the underlying pointee cancelled and frees the
  // bridge pointer. Any subsequent Rust callback that calls FromUserData
  // on a stale copy will find cancelled==true and short-circuit.
  // Idempotent: second call is a no-op.
  void Cancel();

  bool is_cancelled() const;

  // For callback implementations: retrieves the shared Pointee from a
  // user_data void*. The caller receives a shared_ptr<Pointee> that keeps
  // the Pointee alive regardless of handle lifetime. Caller must hold the
  // resulting shared_ptr for the duration of any work using it.
  //
  // SAFETY: `user_data` MUST be a pointer previously returned by
  // user_data() on a live (non-cancelled) handle. After Cancel(), the
  // pointer is deleted; calling FromUserData on it is UB.
  static std::shared_ptr<Pointee> FromUserData(void* user_data);

 private:
  std::shared_ptr<Pointee> pointee_;

  // Heap-allocated bridge: a shared_ptr<Pointee>* whose raw address is
  // the user_data given to Rust. Deleted exactly once in Cancel().
  raw_ptr<std::shared_ptr<Pointee>> user_data_bridge_ = nullptr;

  // Guards one-shot deletion of user_data_bridge_.
  bool freed_ = false;
};

}  // namespace maho

// Template definitions — must be header-resident.
#include "maho/browser/ai/maho_ffi_callback_handle_inl.h"

#endif  // MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_H_
