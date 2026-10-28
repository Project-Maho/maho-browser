// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_COSMETIC_CLASS_ID_POLLER_H_
#define MAHO_BROWSER_NET_MAHO_COSMETIC_CLASS_ID_POLLER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/common/isolated_world_ids.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace maho {

inline constexpr int kIsolatedWorldIdForCosmeticFilters =
    content::ISOLATED_WORLD_ID_CONTENT_END + 3;

// Pure pacing policy for generic class/id selector discovery.
class CosmeticClassIdBatchPolicy {
 public:
  static constexpr size_t kMaxTokensPerBatch = 2048;
  static constexpr size_t kEmptyBatchesBeforePause = 10;

  bool ShouldPoll(bool generichide_enabled) const;
  bool ShouldReadMutationCounter(bool paused) const;
  size_t LimitBatchSize(size_t token_count) const;
  void RecordBatchSize(size_t token_count);
  void OnDomMutationHint();
  uint64_t last_mutation_counter() const { return last_mutation_counter_; }
  bool HasMutationChanged(uint64_t counter) const;
  void RecordMutationCounter(uint64_t counter);
  std::vector<std::string> LimitTokens(
      const std::vector<std::string>& tokens) const;

 private:
  size_t empty_batch_count_ = 0;
  uint64_t last_mutation_counter_ = 0;
};

// Polls one frame's cosmetic runtime for newly seen class/id tokens, asks the
// core for matching generic hide selectors and pushes them back to the frame.
// Every frame and core access goes through Delegate so the loop is testable.
class CosmeticClassIdPoller {
 public:
  static constexpr base::TimeDelta kPollInterval = base::Milliseconds(250);
  static constexpr base::TimeDelta kPausedProbeInterval = base::Seconds(2);

  class Delegate {
   public:
    virtual ~Delegate() = default;
    // False once the frame is gone, inactive or not live.
    virtual bool IsFrameAlive() = 0;
    virtual uint64_t CoreGeneration() = 0;
    // Runs |script| in the cosmetic isolated world of the frame.
    virtual void RunScript(const std::u16string& script,
                           base::OnceCallback<void(base::Value)> done) = 0;
    // Resolves |request_json| ({classes, ids, exceptions}) to a JSON array of
    // selectors on the core sequence.
    virtual void LookupSelectors(
        std::string request_json,
        base::OnceCallback<void(std::string)> done) = 0;
  };

  // Production delegate for |frame| (weak document reference, core task).
  static std::unique_ptr<Delegate> CreateFrameDelegate(
      content::RenderFrameHost* frame);

  CosmeticClassIdPoller(std::unique_ptr<Delegate> delegate,
                        std::vector<std::string> exceptions,
                        uint64_t generation);
  ~CosmeticClassIdPoller();

  CosmeticClassIdPoller(const CosmeticClassIdPoller&) = delete;
  CosmeticClassIdPoller& operator=(const CosmeticClassIdPoller&) = delete;

  void Start();
  void Stop();

  bool is_stopped() const { return stopped_; }
  bool is_polling() const { return poll_timer_.IsRunning(); }
  bool is_paused() const { return probe_timer_.IsRunning(); }

 private:
  bool StillValid();
  void Poll();
  void OnDrainResult(base::Value result);
  void ProbeMutationCounter();
  void OnMutationCounter(base::Value result);
  void OnSelectorsJson(std::string selectors_json);
  void Pause();
  void Resume();

  std::unique_ptr<Delegate> delegate_;
  std::vector<std::string> exceptions_;
  base::RepeatingTimer poll_timer_;
  base::RepeatingTimer probe_timer_;
  CosmeticClassIdBatchPolicy policy_;
  uint64_t generation_ = 0;
  bool stopped_ = true;
  bool request_in_flight_ = false;
  base::WeakPtrFactory<CosmeticClassIdPoller> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_COSMETIC_CLASS_ID_POLLER_H_
