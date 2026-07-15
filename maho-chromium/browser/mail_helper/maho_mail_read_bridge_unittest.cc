// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_read_bridge.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/threading/platform_thread.h"
#include "maho/third_party/maho/maho_mail_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

// Deterministic error the bridge surfaces when the FFI rejects the call. Must
// stay in sync with kMahoMailReadRejected in maho_mail_read_bridge.cc.
constexpr char kRejectedError[] = "maho mail read rejected by backend";

// Move-only sentinel that tracks how many instances are alive via a shared
// atomic counter. Bound into a ReplyCallback, it lets a test observe whether
// the callback's bound state (which the bridge's reference-counted context
// transitively owns until it resolves) is destroyed exactly once. A leaked
// context that never releases -- and therefore never moves the reply out --
// keeps the probe alive, so a non-zero live count after RunUntilIdle() is a
// leak signal.
class LiveProbe {
 public:
  explicit LiveProbe(std::atomic<int>* live) : live_(live) {
    live_->fetch_add(1, std::memory_order_relaxed);
  }
  LiveProbe(LiveProbe&& other) : live_(other.live_) { other.live_ = nullptr; }
  LiveProbe& operator=(LiveProbe&&) = delete;
  LiveProbe(const LiveProbe&) = delete;
  LiveProbe& operator=(const LiveProbe&) = delete;
  ~LiveProbe() {
    if (live_) {
      live_->fetch_sub(1, std::memory_order_relaxed);
    }
  }

 private:
  raw_ptr<std::atomic<int>> live_;
};

// Captures the (callback, user_data) pair the bridge hands to the FFI so the
// test can fire the Rust-side callback later, on whichever thread it chooses.
struct CapturedCall {
  MahoMailReadCallback callback = nullptr;
  // Opaque FFI user_data. Intentionally may dangle in the teardown test after
  // the FFI callback releases the bridge's context; the test never
  // dereferences it, so raw_ptr's dangling checks would be inappropriate here.
  RAW_PTR_EXCLUSION void* user_data = nullptr;
  bool accepted = false;
};

// Builds a fake FfiInvoker that records the handed-over callback/user_data and
// returns `accept` as the FFI acceptance bool. It never calls the callback
// itself, so the test drives callback timing explicitly.
MahoMailReadBridge::FfiInvoker MakeCapturingInvoker(CapturedCall* out,
                                                    bool accept) {
  return base::BindOnce(
      [](CapturedCall* out, bool accept, MahoMailReadCallback callback,
         void* user_data) {
        out->callback = callback;
        out->user_data = user_data;
        out->accepted = accept;
        return accept;
      },
      out, accept);
}

// Records the single sequenced reply for assertions and confirms the reply ran
// on the originating sequence. Carries a LiveProbe so ownership release is
// observable.
struct ReplySink {
  int calls = 0;
  bool ok = false;
  std::string json;
  bool on_origin_sequence = false;
};

MahoMailReadBridge::ReplyCallback MakeReply(
    ReplySink* sink,
    std::atomic<int>* probe_live,
    scoped_refptr<base::SequencedTaskRunner> origin) {
  return base::BindOnce(
      [](LiveProbe, ReplySink* sink,
         scoped_refptr<base::SequencedTaskRunner> origin, bool ok,
         std::string json) {
        ++sink->calls;
        sink->ok = ok;
        sink->json = std::move(json);
        sink->on_origin_sequence = origin->RunsTasksInCurrentSequence();
      },
      LiveProbe(probe_live), sink, std::move(origin));
}

class MahoMailReadBridgeTest : public testing::Test {
 protected:
  scoped_refptr<base::SequencedTaskRunner> origin_runner() {
    return base::SequencedTaskRunner::GetCurrentDefault();
  }

  // Real thread pool so a worker-thread Rust callback is genuinely exercised;
  // RunUntilIdle() drains pool + main sequence deterministically (no sleeps).
  base::test::TaskEnvironment task_environment_;
};

// (1) An accepted call whose Rust callback fires on a worker thread posts the
// reply back to the originating sequence exactly once, carrying a COPY of the
// C string (proven by corrupting the source buffer after the trampoline
// returns).
TEST_F(MahoMailReadBridgeTest, WorkerThreadCallbackPostsReplyOnceToOrigin) {
  const base::PlatformThreadId origin_thread =
      base::PlatformThread::CurrentId();
  std::atomic<int> probe_live{0};
  ReplySink sink;
  CapturedCall captured;

  MahoMailReadBridge::Start(
      MakeCapturingInvoker(&captured, /*accept=*/true),
      MakeReply(&sink, &probe_live, origin_runner()));

  ASSERT_TRUE(captured.accepted);
  ASSERT_NE(nullptr, captured.callback);
  ASSERT_NE(nullptr, captured.user_data);
  // The reply is posted, never run inline: nothing has resolved yet.
  EXPECT_EQ(0, sink.calls);

  bool callback_ran_off_origin = false;
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING},
      base::BindLambdaForTesting([&] {
        callback_ran_off_origin =
            base::PlatformThread::CurrentId() != origin_thread;
        const std::string source = "{\"folders\":[\"inbox\"]}";
        // Heap-owned, NUL-terminated buffer valid only for the callback's
        // duration -- exactly the FFI ownership contract.
        std::vector<char> buffer(source.begin(), source.end());
        buffer.push_back('\0');
        captured.callback(true, buffer.data(), captured.user_data);
        // The trampoline has returned; a correct bridge already copied the
        // string synchronously. Corrupt the source: a dangling-pointer bug
        // would now surface as a garbled reply below.
        std::fill(buffer.begin(), buffer.end(), 'X');
      }));

  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, sink.calls);
  EXPECT_TRUE(sink.ok);
  EXPECT_EQ("{\"folders\":[\"inbox\"]}", sink.json);
  EXPECT_TRUE(sink.on_origin_sequence);
  EXPECT_TRUE(callback_ran_off_origin);
  EXPECT_EQ(0, probe_live.load(std::memory_order_relaxed));
}

// (2) A rejected call (invoker returns false) resolves deterministically once
// with the sentinel error, WITHOUT the bridge invoking the Rust callback, and
// releases its reference cleanly.
//
// Note: we deliberately do NOT fire captured.callback afterward. Per the FFI
// contract a rejected call never calls back; firing it post-rejection would
// exercise the known out-of-scope defensive-hardening path (theoretical
// double-Release under a contract violation), which this task must not touch.
TEST_F(MahoMailReadBridgeTest, RejectedInvokerResolvesOnceWithoutCallback) {
  std::atomic<int> probe_live{0};
  ReplySink sink;
  CapturedCall captured;

  MahoMailReadBridge::Start(
      MakeCapturingInvoker(&captured, /*accept=*/false),
      MakeReply(&sink, &probe_live, origin_runner()));

  // Rejection resolves via a post to the origin sequence; drain it.
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, sink.calls);
  EXPECT_FALSE(sink.ok);
  EXPECT_EQ(kRejectedError, sink.json);
  EXPECT_TRUE(sink.on_origin_sequence);
  EXPECT_EQ(0, probe_live.load(std::memory_order_relaxed));
}

// (3) When the Rust callback races ahead of the invoker's return -- firing on
// the same stack BEFORE the invoker reports acceptance -- the reply still
// resolves exactly once, and with the callback's value (not the rejection
// error). This proves the idempotent guard and that the accepted path does not
// double-resolve.
TEST_F(MahoMailReadBridgeTest, ExactlyOnceWhenCallbackRacesInvokerReturn) {
  std::atomic<int> probe_live{0};
  ReplySink sink;

  MahoMailReadBridge::Start(
      base::BindOnce([](MahoMailReadCallback callback, void* user_data) {
        // Fire synchronously, then report acceptance: the callback wins the
        // race with the invoker return.
        const std::string source = "{\"raced\":true}";
        callback(true, source.c_str(), user_data);
        return true;
      }),
      MakeReply(&sink, &probe_live, origin_runner()));

  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, sink.calls);
  EXPECT_TRUE(sink.ok);
  EXPECT_EQ("{\"raced\":true}", sink.json);
  EXPECT_TRUE(sink.on_origin_sequence);
  EXPECT_EQ(0, probe_live.load(std::memory_order_relaxed));
}

// Consumer whose reply handler is WeakPtr-bound, modelling an origin context
// that can be torn down while an FFI call is still outstanding.
class ReplyConsumer {
 public:
  explicit ReplyConsumer(int* calls) : calls_(calls) {}

  void OnReply(bool ok, std::string json) {
    ++*calls_;
    last_ok_ = ok;
    last_json_ = std::move(json);
  }

  base::WeakPtr<ReplyConsumer> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  raw_ptr<int> calls_;
  bool last_ok_ = false;
  std::string last_json_;
  base::WeakPtrFactory<ReplyConsumer> weak_factory_{this};
};

// (4) A late callback whose WeakPtr-bound origin was already torn down must not
// use-after-free and must not double-run: the WeakPtr is invalidated, the
// posted reply is a clean no-op, and the reference is released without leak.
TEST_F(MahoMailReadBridgeTest, LateCallbackAfterOriginTeardownIsSafeNoOp) {
  std::atomic<int> probe_live{0};
  int reply_calls = 0;
  CapturedCall captured;
  auto consumer = std::make_unique<ReplyConsumer>(&reply_calls);

  MahoMailReadBridge::Start(
      MakeCapturingInvoker(&captured, /*accept=*/true),
      base::BindOnce(
          [](LiveProbe, base::WeakPtr<ReplyConsumer> consumer, bool ok,
             std::string json) {
            if (consumer) {
              consumer->OnReply(ok, std::move(json));
            }
          },
          LiveProbe(&probe_live), consumer->AsWeakPtr()));

  ASSERT_TRUE(captured.accepted);
  ASSERT_NE(nullptr, captured.callback);

  // Tear the origin consumer down BEFORE the late worker-thread callback.
  consumer.reset();

  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING},
      base::BindLambdaForTesting([&] {
        const std::string source = "{\"late\":true}";
        captured.callback(true, source.c_str(), captured.user_data);
      }));

  task_environment_.RunUntilIdle();

  // WeakPtr invalidated -> handler never ran, no double-run, no UAF.
  EXPECT_EQ(0, reply_calls);
  // Reply bound-state (and thus the context's held reference) released cleanly.
  EXPECT_EQ(0, probe_live.load(std::memory_order_relaxed));
}

}  // namespace
}  // namespace maho
