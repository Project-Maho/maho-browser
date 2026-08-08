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
    base::WeakPtr<T> owner,
    uint64_t generation)
    : pointee_(std::make_shared<Pointee>(generation)) {
  pointee_->task_runner = std::move(runner);
  pointee_->owner = std::move(owner);

  user_data_bridge_ = new Bridge(pointee_);
}

template <typename T>
FfiCallbackHandle<T>::~FfiCallbackHandle() {
  Cancel();
}

template <typename T>
void* FfiCallbackHandle<T>::user_data() const {
  return static_cast<void*>(user_data_bridge_.get());
}

template <typename T>
void FfiCallbackHandle<T>::Cancel() {
  pointee_->cancelled.store(true, std::memory_order_release);

  Bridge* bridge = user_data_bridge_.get();
  if (!bridge) {
    return;
  }

  if (bridge->producer_leased.load(std::memory_order_acquire)) {
    user_data_bridge_ = nullptr;
    return;
  }

  delete bridge;
  user_data_bridge_ = nullptr;
}

template <typename T>
bool FfiCallbackHandle<T>::is_cancelled() const {
  return pointee_->cancelled.load(std::memory_order_acquire);
}

template <typename T>
void FfiCallbackHandle<T>::HandOffToProducer() {
  Bridge* bridge = user_data_bridge_.get();
  DCHECK(bridge);
  if (!bridge) {
    return;
  }

  bool was_leased =
      bridge->producer_leased.exchange(true, std::memory_order_acq_rel);
  DCHECK(!was_leased);
  if (was_leased) {
    return;
  }

  base::AutoLock lock(ProducerLeaseLock());
  const bool inserted = ProducerLeases().insert(bridge).second;
  DCHECK(inserted);
  if (!inserted) {
    bridge->producer_leased.store(false, std::memory_order_release);
    return;
  }
  user_data_bridge_ = nullptr;
}

template <typename T>
void FfiCallbackHandle<T>::ReleaseFromProducer(void* user_data) {
  Bridge* bridge = static_cast<Bridge*>(user_data);
  DCHECK(bridge);
  if (!bridge) {
    return;
  }

  {
    base::AutoLock lock(ProducerLeaseLock());
    auto& leases = ProducerLeases();
    auto it = leases.find(bridge);
    DCHECK(it != leases.end());
    if (it == leases.end()) {
      return;
    }
    leases.erase(it);
  }

  DCHECK(bridge->producer_leased.load(std::memory_order_acquire));
  delete bridge;
}

template <typename T>
std::shared_ptr<typename FfiCallbackHandle<T>::Pointee>
FfiCallbackHandle<T>::FromUserData(void* user_data) {
  auto* bridge = static_cast<Bridge*>(user_data);
  return bridge->pointee;
}

template <typename T>
base::Lock& FfiCallbackHandle<T>::ProducerLeaseLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

template <typename T>
std::set<typename FfiCallbackHandle<T>::Bridge*>&
FfiCallbackHandle<T>::ProducerLeases() {
  static base::NoDestructor<std::set<Bridge*>> leases;
  return *leases;
}

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_FFI_CALLBACK_HANDLE_INL_H_
