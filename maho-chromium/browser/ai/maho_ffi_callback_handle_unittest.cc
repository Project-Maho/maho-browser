// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "maho/browser/ai/maho_ffi_callback_handle.h"

#include <memory>

#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

// A minimal owner type that provides WeakPtr support.
class FakeOwner {
 public:
  FakeOwner() = default;
  ~FakeOwner() = default;

  int callback_count() const { return callback_count_; }
  void IncrementCallbackCount() { ++callback_count_; }

  base::WeakPtr<FakeOwner> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  int callback_count_ = 0;
  base::WeakPtrFactory<FakeOwner> weak_factory_{this};
};

class FfiCallbackHandleTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

template <typename Handle>
void ReleaseFromProducer(void* producer_user_data) {
  Handle::ReleaseFromProducer(producer_user_data);
}

// Verifies that constructing and destroying a handle does not leak memory.
// We observe the Pointee's shared_ptr refcount via a weak_ptr<Pointee>.
TEST_F(FfiCallbackHandleTest, ConstructAndDestroy_NoLeak) {
  auto owner = std::make_unique<FakeOwner>();
  std::weak_ptr<FfiCallbackHandle<FakeOwner>::Pointee> observer;

  {
    FfiCallbackHandle<FakeOwner> handle(
        base::SequencedTaskRunner::GetCurrentDefault(),
        owner->AsWeakPtr());

    // Grab a weak observer to the internal pointee via FromUserData.
    auto sp = FfiCallbackHandle<FakeOwner>::FromUserData(handle.user_data());
    observer = sp;
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(sp->generation, 0u);
    // sp goes out of scope here, dropping one refcount.
  }

  // After handle destruction: the bridge is deleted, pointee_ in handle is
  // gone. Since no in-flight callbacks hold a shared_ptr, observer expires.
  EXPECT_TRUE(observer.expired());
}

TEST_F(FfiCallbackHandleTest,
       ProducerLeaseAllowsCallbackCopyAfterWrapperDestruction) {
  auto owner = std::make_unique<FakeOwner>();
  void* producer_user_data = nullptr;

  {
    FfiCallbackHandle<FakeOwner> handle(
        base::SequencedTaskRunner::GetCurrentDefault(), owner->AsWeakPtr());
    producer_user_data = handle.user_data();
    handle.HandOffToProducer();
  }

  auto callback_copy =
      FfiCallbackHandle<FakeOwner>::FromUserData(producer_user_data);
  std::weak_ptr<FfiCallbackHandle<FakeOwner>::Pointee> observer(callback_copy);
  EXPECT_FALSE(observer.expired());
  EXPECT_TRUE(callback_copy->cancelled.load(std::memory_order_acquire));

  ReleaseFromProducer<FfiCallbackHandle<FakeOwner>>(producer_user_data);
  EXPECT_FALSE(observer.expired());
  callback_copy.reset();
  EXPECT_TRUE(observer.expired());
}

TEST_F(FfiCallbackHandleTest,
       ProducerReleaseReclaimsBridgeAfterLocalCallbackCopiesDrop) {
  auto owner = std::make_unique<FakeOwner>();
  std::shared_ptr<FfiCallbackHandle<FakeOwner>::Pointee> callback_copy;
  std::weak_ptr<FfiCallbackHandle<FakeOwner>::Pointee> observer;
  void* producer_user_data = nullptr;

  {
    FfiCallbackHandle<FakeOwner> handle(
        base::SequencedTaskRunner::GetCurrentDefault(), owner->AsWeakPtr());
    producer_user_data = handle.user_data();
    callback_copy =
        FfiCallbackHandle<FakeOwner>::FromUserData(producer_user_data);
    observer = callback_copy;
    handle.HandOffToProducer();
  }

  callback_copy.reset();
  EXPECT_FALSE(observer.expired());

  ReleaseFromProducer<FfiCallbackHandle<FakeOwner>>(producer_user_data);
  EXPECT_TRUE(observer.expired());
}

TEST_F(FfiCallbackHandleTest, UnacceptedProducerLeaseIsReclaimedLocally) {
  auto owner = std::make_unique<FakeOwner>();
  std::weak_ptr<FfiCallbackHandle<FakeOwner>::Pointee> observer;

  {
    FfiCallbackHandle<FakeOwner> handle(
        base::SequencedTaskRunner::GetCurrentDefault(), owner->AsWeakPtr());
    auto callback_copy =
        FfiCallbackHandle<FakeOwner>::FromUserData(handle.user_data());
    observer = callback_copy;
    EXPECT_FALSE(observer.expired());
  }

  EXPECT_TRUE(observer.expired());
}

// Verifies Cancel() sets the cancelled flag.
TEST_F(FfiCallbackHandleTest, CancelMarksCancelled) {
  auto owner = std::make_unique<FakeOwner>();
  FfiCallbackHandle<FakeOwner> handle(
      base::SequencedTaskRunner::GetCurrentDefault(),
      owner->AsWeakPtr());

  EXPECT_FALSE(handle.is_cancelled());
  handle.Cancel();
  EXPECT_TRUE(handle.is_cancelled());

  // Idempotent: second cancel is safe.
  handle.Cancel();
  EXPECT_TRUE(handle.is_cancelled());
}

// Simulates a Rust callback arriving after Cancel(). The callback should
// find cancelled==true and short-circuit (no work performed).
TEST_F(FfiCallbackHandleTest, CallbackAfterCancelIsNoOp) {
  auto owner = std::make_unique<FakeOwner>();
  FfiCallbackHandle<FakeOwner> handle(
      base::SequencedTaskRunner::GetCurrentDefault(),
      owner->AsWeakPtr());

  // Simulate: Rust copies the user_data pointer before Cancel.
  void* captured_user_data = handle.user_data();

  // Simulate: Rust's callback implementation loads the shared_ptr.
  auto pointee_sp =
      FfiCallbackHandle<FakeOwner>::FromUserData(captured_user_data);

  // Now cancel the handle.
  handle.Cancel();

  // The callback checks cancelled BEFORE doing work.
  if (!pointee_sp->cancelled.load(std::memory_order_acquire)) {
    // This branch should NOT execute.
    owner->IncrementCallbackCount();
  }

  EXPECT_EQ(owner->callback_count(), 0);
  EXPECT_TRUE(pointee_sp->cancelled.load(std::memory_order_acquire));
}

// Proves the Pointee outlives the handle when an in-flight callback holds a
// shared_ptr<Pointee>. This is the core UAF-prevention guarantee.
TEST_F(FfiCallbackHandleTest, PointeeOutlivesCallbackInFlight) {
  auto owner = std::make_unique<FakeOwner>();
  std::shared_ptr<FfiCallbackHandle<FakeOwner>::Pointee> in_flight_sp;
  std::weak_ptr<FfiCallbackHandle<FakeOwner>::Pointee> observer;

  {
    FfiCallbackHandle<FakeOwner> handle(
        base::SequencedTaskRunner::GetCurrentDefault(),
        owner->AsWeakPtr());

    // Simulate Rust callback grabbing a shared_ptr before handle dies.
    in_flight_sp =
        FfiCallbackHandle<FakeOwner>::FromUserData(handle.user_data());
    observer = in_flight_sp;

    // Handle goes out of scope here — Cancel() runs, bridge is deleted.
  }

  // Despite handle destruction, the Pointee is still alive because
  // in_flight_sp holds a reference.
  EXPECT_FALSE(observer.expired());
  EXPECT_TRUE(in_flight_sp->cancelled.load(std::memory_order_acquire));

  // Simulate callback finishing — drop the shared_ptr.
  in_flight_sp.reset();

  // Now the Pointee is truly gone.
  EXPECT_TRUE(observer.expired());
}

// Verifies that the handle stores the correct task runner and that a callback
// can post work to it.
TEST_F(FfiCallbackHandleTest, PostsToSequenceRunner) {
  auto owner = std::make_unique<FakeOwner>();
  auto runner = base::SequencedTaskRunner::GetCurrentDefault();

  FfiCallbackHandle<FakeOwner> handle(runner, owner->AsWeakPtr());

  // Simulate a Rust callback: load the pointee, check not cancelled, then
  // PostTask to the owner's sequence.
  auto pointee_sp =
      FfiCallbackHandle<FakeOwner>::FromUserData(handle.user_data());

  ASSERT_FALSE(pointee_sp->cancelled.load(std::memory_order_acquire));

  // Post a task via the stored task_runner, binding through the WeakPtr.
  base::WeakPtr<FakeOwner> weak_owner = pointee_sp->owner;
  pointee_sp->task_runner->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<FakeOwner> weak) {
                       if (weak) {
                         weak->IncrementCallbackCount();
                       }
                     },
                     weak_owner));

  // Run pending tasks.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(owner->callback_count(), 1);
}

}  // namespace
}  // namespace maho
