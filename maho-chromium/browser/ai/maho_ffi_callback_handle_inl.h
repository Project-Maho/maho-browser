// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_INL_H_
#define MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_INL_H_

// This file is included by maho_ffi_callback_handle.h. Do not include
// directly; include maho_ffi_callback_handle.h instead.

namespace maho {

template <typename T>
FfiCallbackHandle<T>::FfiCallbackHandle(
    scoped_refptr<base::SequencedTaskRunner> runner,
    base::WeakPtr<T> owner)
    : pointee_(std::make_shared<Pointee>()) {
  pointee_->task_runner = std::move(runner);
  pointee_->owner = std::move(owner);

  // Allocate the bridge: a heap shared_ptr whose address is the user_data
  // pointer given to Rust. Equivalent to Rust's Box::into_raw(Box::new(sp)).
  user_data_bridge_ = new std::shared_ptr<Pointee>(pointee_);
}

template <typename T>
FfiCallbackHandle<T>::~FfiCallbackHandle() {
  Cancel();
}

template <typename T>
void* FfiCallbackHandle<T>::user_data() const {
  return static_cast<void*>(user_data_bridge_);
}

template <typename T>
void FfiCallbackHandle<T>::Cancel() {
  // Mark cancelled first — ensures any in-flight callback that loads the
  // flag after this point will short-circuit.
  pointee_->cancelled.store(true, std::memory_order_release);

  // Delete the bridge pointer exactly once. After this, Rust holds a
  // dangling pointer — but since cancelled is true, well-behaved callbacks
  // will have already loaded their shared_ptr copy and will find
  // cancelled==true when they check.
  if (!freed_) {
    freed_ = true;
    delete user_data_bridge_;
    user_data_bridge_ = nullptr;
  }
}

template <typename T>
bool FfiCallbackHandle<T>::is_cancelled() const {
  return pointee_->cancelled.load(std::memory_order_acquire);
}

template <typename T>
std::shared_ptr<typename FfiCallbackHandle<T>::Pointee>
FfiCallbackHandle<T>::FromUserData(void* user_data) {
  // Copy the shared_ptr out of the bridge. This increments the refcount,
  // keeping the Pointee alive for the caller even if Cancel() runs
  // concurrently and deletes the bridge.
  auto* bridge = static_cast<std::shared_ptr<Pointee>*>(user_data);
  return *bridge;
}

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_INL_H_
