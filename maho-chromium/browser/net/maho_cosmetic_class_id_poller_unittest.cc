// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_cosmetic_class_id_poller.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

TEST(MahoCosmeticClassIdPollerTest, ReturnsBatchAndKeepsPolling) {
  maho::CosmeticClassIdBatchPolicy policy;

  EXPECT_TRUE(policy.ShouldPoll(false));
  EXPECT_EQ(policy.LimitBatchSize(3u), 3u);
  policy.RecordBatchSize(3u);
  EXPECT_TRUE(policy.ShouldPoll(false));
}

TEST(MahoCosmeticClassIdPollerTest, GenerichideDisabledSkipsPolls) {
  maho::CosmeticClassIdBatchPolicy policy;

  EXPECT_FALSE(policy.ShouldPoll(true));
}

TEST(MahoCosmeticClassIdPollerTest, MutationCounterIsReadOnlyWhilePaused) {
  maho::CosmeticClassIdBatchPolicy policy;
  EXPECT_FALSE(policy.ShouldReadMutationCounter(false));
  for (size_t i = 0; i < 10; ++i) policy.RecordBatchSize(0);
  EXPECT_TRUE(policy.ShouldReadMutationCounter(true));
  EXPECT_TRUE(policy.HasMutationChanged(2));
  policy.RecordMutationCounter(2);
  EXPECT_TRUE(policy.ShouldPoll(false));
}

TEST(MahoCosmeticClassIdPollerTest, PausedCounterReadWakesOnlyOnChange) {
  maho::CosmeticClassIdBatchPolicy policy;
  for (size_t i = 0; i < maho::CosmeticClassIdBatchPolicy::kEmptyBatchesBeforePause;
       ++i) {
    policy.RecordBatchSize(0);
  }
  policy.RecordMutationCounter(8);
  EXPECT_FALSE(policy.ShouldPoll(false));
  EXPECT_TRUE(policy.ShouldReadMutationCounter(true));
  EXPECT_FALSE(policy.HasMutationChanged(8));
  EXPECT_TRUE(policy.HasMutationChanged(9));
  policy.RecordMutationCounter(9);
  EXPECT_TRUE(policy.ShouldPoll(false));
}

TEST(MahoCosmeticClassIdPollerTest, MutationCounterWakesPausedPoller) {
  maho::CosmeticClassIdBatchPolicy policy;
  for (size_t i = 0; i < 10; ++i) policy.RecordBatchSize(0);
  EXPECT_FALSE(policy.ShouldPoll(false));
  EXPECT_FALSE(policy.HasMutationChanged(0));
  EXPECT_TRUE(policy.HasMutationChanged(1));
  policy.RecordMutationCounter(1);
  EXPECT_TRUE(policy.ShouldPoll(false));
  EXPECT_FALSE(policy.HasMutationChanged(1));
}

TEST(MahoCosmeticClassIdPollerTest, TruncatesBatchAtTokenLimit) {
  maho::CosmeticClassIdBatchPolicy policy;

  EXPECT_EQ(policy.LimitBatchSize(2049u), 2048u);
  EXPECT_EQ(policy.LimitBatchSize(2048u), 2048u);
  std::vector<std::string> tokens(2049, ".class");
  EXPECT_EQ(policy.LimitTokens(tokens).size(), 2048u);
}

TEST(MahoCosmeticClassIdPollerTest, EmptyBatchLimitPausesUntilMutationHint) {
  maho::CosmeticClassIdBatchPolicy policy;

  for (size_t i = 0; i < 9; ++i) {
    policy.RecordBatchSize(0);
    EXPECT_TRUE(policy.ShouldPoll(false));
  }
  policy.RecordBatchSize(0);
  EXPECT_FALSE(policy.ShouldPoll(false));

  policy.OnDomMutationHint();
  EXPECT_TRUE(policy.ShouldPoll(false));
}

TEST(MahoCosmeticClassIdPollerTest, EmptyBatchLimitPausesAfterTenBatches) {
  maho::CosmeticClassIdBatchPolicy policy;
  for (size_t i = 0; i < 9; ++i) {
    policy.RecordBatchSize(0);
    EXPECT_TRUE(policy.ShouldPoll(false));
  }
  policy.RecordBatchSize(0);
  EXPECT_FALSE(policy.ShouldPoll(false));
  EXPECT_TRUE(policy.ShouldReadMutationCounter(true));
}

TEST(MahoCosmeticClassIdPollerTest, NonemptyBatchResetsEmptyBatchCount) {
  maho::CosmeticClassIdBatchPolicy policy;
  for (size_t i = 0; i < 9; ++i) {
    policy.RecordBatchSize(0);
  }

  policy.RecordBatchSize(1);
  EXPECT_TRUE(policy.ShouldPoll(false));
  for (size_t i = 0; i < 9; ++i) {
    policy.RecordBatchSize(0);
  }
  EXPECT_TRUE(policy.ShouldPoll(false));
}


// Fake frame + core: records scripts, answers drains from a queue.
class FakeDelegate : public maho::CosmeticClassIdPoller::Delegate {
 public:
  struct State {
    bool alive = true;
    uint64_t generation = 7;
    std::vector<std::string> drains;  // JSON strings returned by drainBatches
    uint64_t counter = 0;
    std::vector<std::u16string> scripts;
    std::vector<std::string> lookups;
    std::string selectors_reply = "[]";
  };
  explicit FakeDelegate(State* state) : state_(state) {}
  bool IsFrameAlive() override { return state_->alive; }
  uint64_t CoreGeneration() override { return state_->generation; }
  void RunScript(const std::u16string& script,
                 base::OnceCallback<void(base::Value)> done) override {
    state_->scripts.push_back(script);
    if (script == u"__mahoCosmetic.drainBatches()") {
      std::string reply = R"({"batches":[],"mutations":0})";
      if (!state_->drains.empty()) {
        reply = state_->drains.front();
        state_->drains.erase(state_->drains.begin());
      }
      std::move(done).Run(base::Value(reply));
    } else if (script == u"__mahoCosmetic.readMutationCounter()") {
      std::move(done).Run(base::Value(static_cast<int>(state_->counter)));
    } else {
      std::move(done).Run(base::Value());
    }
  }
  void LookupSelectors(std::string request_json,
                       base::OnceCallback<void(std::string)> done) override {
    state_->lookups.push_back(request_json);
    std::move(done).Run(state_->selectors_reply);
  }

 private:
  raw_ptr<State> state_;
};

size_t CountScripts(const FakeDelegate::State& state,
                    const std::u16string& prefix) {
  size_t count = 0;
  for (const auto& script : state.scripts) {
    if (script.starts_with(prefix)) {
      ++count;
    }
  }
  return count;
}

class MahoCosmeticClassIdPollerLoopTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(MahoCosmeticClassIdPollerLoopTest, DrainsLooksUpAndInjectsSelectors) {
  FakeDelegate::State state;
  state.drains.push_back(
      R"({"batches":[[".ad-slot","#banner","bogus"]],"mutations":3})");
  state.selectors_reply = R"([".ad-slot"])";
  maho::CosmeticClassIdPoller poller(std::make_unique<FakeDelegate>(&state),
                                     {"#banner"}, 7);
  poller.Start();
  task_environment_.FastForwardBy(maho::CosmeticClassIdPoller::kPollInterval);

  ASSERT_EQ(state.lookups.size(), 1u);
  auto request = base::JSONReader::Read(state.lookups[0]);
  ASSERT_TRUE(request && request->is_dict());
  EXPECT_EQ(*request->GetDict().FindList("classes")->front().GetIfString(),
            "ad-slot");
  EXPECT_EQ(*request->GetDict().FindList("ids")->front().GetIfString(),
            "banner");
  EXPECT_EQ(*request->GetDict().FindList("exceptions")->front().GetIfString(),
            "#banner");
  EXPECT_EQ(CountScripts(state, u"__mahoCosmetic.addGenericSelectors("), 1u);
  EXPECT_EQ(state.scripts.back(),
            u"__mahoCosmetic.addGenericSelectors([\".ad-slot\"]);");
}

TEST_F(MahoCosmeticClassIdPollerLoopTest, PausesAfterTenEmptyPollsThenResumes) {
  FakeDelegate::State state;
  maho::CosmeticClassIdPoller poller(std::make_unique<FakeDelegate>(&state),
                                     {}, 7);
  poller.Start();
  task_environment_.FastForwardBy(
      maho::CosmeticClassIdPoller::kPollInterval *
      maho::CosmeticClassIdBatchPolicy::kEmptyBatchesBeforePause);
  EXPECT_EQ(CountScripts(state, u"__mahoCosmetic.drainBatches()"),
            maho::CosmeticClassIdBatchPolicy::kEmptyBatchesBeforePause);
  EXPECT_TRUE(poller.is_paused());
  EXPECT_FALSE(poller.is_polling());
  EXPECT_TRUE(state.lookups.empty());

  // While paused only the cheap counter is read, and nothing changes.
  task_environment_.FastForwardBy(
      maho::CosmeticClassIdPoller::kPausedProbeInterval * 3);
  EXPECT_EQ(CountScripts(state, u"__mahoCosmetic.readMutationCounter()"), 3u);
  EXPECT_EQ(CountScripts(state, u"__mahoCosmetic.drainBatches()"),
            maho::CosmeticClassIdBatchPolicy::kEmptyBatchesBeforePause);

  // A DOM mutation wakes it: an immediate drain, then regular polling.
  state.counter = 5;
  state.drains.push_back(R"({"batches":[[".late-ad"]],"mutations":5})");
  task_environment_.FastForwardBy(
      maho::CosmeticClassIdPoller::kPausedProbeInterval);
  EXPECT_TRUE(poller.is_polling());
  EXPECT_FALSE(poller.is_paused());
  EXPECT_EQ(state.lookups.size(), 1u);
}

TEST_F(MahoCosmeticClassIdPollerLoopTest, StopsWhenFrameGoesAwayOrGenerationChanges) {
  FakeDelegate::State state;
  maho::CosmeticClassIdPoller poller(std::make_unique<FakeDelegate>(&state),
                                     {}, 7);
  poller.Start();
  task_environment_.FastForwardBy(maho::CosmeticClassIdPoller::kPollInterval);
  state.generation = 8;
  task_environment_.FastForwardBy(maho::CosmeticClassIdPoller::kPollInterval);
  EXPECT_TRUE(poller.is_stopped());
  const size_t scripts = state.scripts.size();
  task_environment_.FastForwardBy(base::Seconds(10));
  EXPECT_EQ(state.scripts.size(), scripts);

  FakeDelegate::State gone;
  maho::CosmeticClassIdPoller other(std::make_unique<FakeDelegate>(&gone), {},
                                    7);
  other.Start();
  gone.alive = false;
  task_environment_.FastForwardBy(maho::CosmeticClassIdPoller::kPollInterval);
  EXPECT_TRUE(other.is_stopped());
  EXPECT_TRUE(gone.scripts.empty());
}

}  // namespace
