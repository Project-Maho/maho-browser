// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_cosmetic_class_id_poller.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/render_frame_host.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"

namespace maho {

bool CosmeticClassIdBatchPolicy::ShouldPoll(bool generichide_enabled) const {
  return !generichide_enabled &&
         empty_batch_count_ < kEmptyBatchesBeforePause;
}

bool CosmeticClassIdBatchPolicy::ShouldReadMutationCounter(bool paused) const {
  return paused;
}

size_t CosmeticClassIdBatchPolicy::LimitBatchSize(size_t token_count) const {
  return token_count < kMaxTokensPerBatch ? token_count : kMaxTokensPerBatch;
}

void CosmeticClassIdBatchPolicy::RecordBatchSize(size_t token_count) {
  if (token_count == 0) {
    ++empty_batch_count_;
  } else {
    empty_batch_count_ = 0;
  }
}

void CosmeticClassIdBatchPolicy::OnDomMutationHint() {
  empty_batch_count_ = 0;
}

bool CosmeticClassIdBatchPolicy::HasMutationChanged(uint64_t counter) const {
  return counter != last_mutation_counter_;
}

void CosmeticClassIdBatchPolicy::RecordMutationCounter(uint64_t counter) {
  const bool changed = HasMutationChanged(counter);
  last_mutation_counter_ = counter;
  if (changed) {
    OnDomMutationHint();
  }
}

std::vector<std::string> CosmeticClassIdBatchPolicy::LimitTokens(
    const std::vector<std::string>& tokens) const {
  const size_t size = LimitBatchSize(tokens.size());
  return std::vector<std::string>(tokens.begin(), tokens.begin() + size);
}

namespace {

class FrameDelegate : public CosmeticClassIdPoller::Delegate {
 public:
  explicit FrameDelegate(content::RenderFrameHost* frame)
      : document_(frame->GetWeakDocumentPtr()) {}

  bool IsFrameAlive() override {
    content::RenderFrameHost* frame = document_.AsRenderFrameHostIfValid();
    return frame && frame->IsRenderFrameLive() && frame->IsActive();
  }

  uint64_t CoreGeneration() override { return maho::GetCoreGeneration(); }

  void RunScript(const std::u16string& script,
                 base::OnceCallback<void(base::Value)> done) override {
    content::RenderFrameHost* frame = document_.AsRenderFrameHostIfValid();
    if (!frame) {
      return;
    }
    frame->ExecuteJavaScriptInIsolatedWorld(
        script, std::move(done), kIsolatedWorldIdForCosmeticFilters);
  }

  void LookupSelectors(std::string request_json,
                       base::OnceCallback<void(std::string)> done) override {
    maho::PostCoreTask<std::string>(
        FROM_HERE,
        base::BindOnce(
            [](std::string json) {
              MahoCore* core = maho::GetCore();
              return core ? maho::core::GetHiddenClassIdSelectors(core,
                                                                  json.c_str())
                          : std::string("[]");
            },
            std::move(request_json)),
        std::move(done));
  }

 private:
  content::WeakDocumentPtr document_;
};

std::optional<uint64_t> ReadCounter(const base::Value* value) {
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int() && value->GetInt() >= 0) {
    return static_cast<uint64_t>(value->GetInt());
  }
  if (value->is_double() && value->GetDouble() >= 0) {
    return static_cast<uint64_t>(value->GetDouble());
  }
  return std::nullopt;
}

}  // namespace

// static
std::unique_ptr<CosmeticClassIdPoller::Delegate>
CosmeticClassIdPoller::CreateFrameDelegate(content::RenderFrameHost* frame) {
  return std::make_unique<FrameDelegate>(frame);
}

CosmeticClassIdPoller::CosmeticClassIdPoller(
    std::unique_ptr<Delegate> delegate,
    std::vector<std::string> exceptions,
    uint64_t generation)
    : delegate_(std::move(delegate)),
      exceptions_(std::move(exceptions)),
      generation_(generation) {}

CosmeticClassIdPoller::~CosmeticClassIdPoller() = default;

void CosmeticClassIdPoller::Start() {
  if (!stopped_) {
    return;
  }
  stopped_ = false;
  Resume();
}

void CosmeticClassIdPoller::Stop() {
  stopped_ = true;
  request_in_flight_ = false;
  poll_timer_.Stop();
  probe_timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
}

bool CosmeticClassIdPoller::StillValid() {
  if (stopped_) {
    return false;
  }
  if (generation_ != delegate_->CoreGeneration() ||
      !delegate_->IsFrameAlive()) {
    Stop();
    return false;
  }
  return true;
}

void CosmeticClassIdPoller::Resume() {
  probe_timer_.Stop();
  if (!poll_timer_.IsRunning()) {
    poll_timer_.Start(FROM_HERE, kPollInterval,
                      base::BindRepeating(&CosmeticClassIdPoller::Poll,
                                          base::Unretained(this)));
  }
}

void CosmeticClassIdPoller::Pause() {
  poll_timer_.Stop();
  if (!probe_timer_.IsRunning()) {
    probe_timer_.Start(
        FROM_HERE, kPausedProbeInterval,
        base::BindRepeating(&CosmeticClassIdPoller::ProbeMutationCounter,
                            base::Unretained(this)));
  }
}

void CosmeticClassIdPoller::Poll() {
  if (!StillValid() || request_in_flight_) {
    return;
  }
  request_in_flight_ = true;
  delegate_->RunScript(
      u"__mahoCosmetic.drainBatches()",
      base::BindOnce(&CosmeticClassIdPoller::OnDrainResult,
                     weak_factory_.GetWeakPtr()));
}

void CosmeticClassIdPoller::OnDrainResult(base::Value result) {
  request_in_flight_ = false;
  if (!StillValid()) {
    return;
  }
  std::vector<std::string> tokens;
  if (result.is_string()) {
    std::optional<base::Value> parsed = base::JSONReader::Read(
        result.GetString(), base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const base::DictValue& dict = parsed->GetDict();
      if (std::optional<uint64_t> mutations =
              ReadCounter(dict.Find("mutations"))) {
        policy_.RecordMutationCounter(*mutations);
      }
      if (const base::ListValue* batches = dict.FindList("batches")) {
        for (const base::Value& batch : *batches) {
          if (!batch.is_list()) {
            continue;
          }
          for (const base::Value& token : batch.GetList()) {
            if (token.is_string()) {
              tokens.push_back(token.GetString());
            }
          }
        }
      }
    }
  }
  tokens = policy_.LimitTokens(tokens);
  policy_.RecordBatchSize(tokens.size());

  base::ListValue classes;
  base::ListValue ids;
  for (const std::string& token : tokens) {
    if (token.size() > 1 && token[0] == '.') {
      classes.Append(token.substr(1));
    } else if (token.size() > 1 && token[0] == '#') {
      ids.Append(token.substr(1));
    }
  }
  if (!classes.empty() || !ids.empty()) {
    base::DictValue request;
    request.Set("classes", std::move(classes));
    request.Set("ids", std::move(ids));
    base::ListValue exception_values;
    for (const std::string& exception : exceptions_) {
      exception_values.Append(exception);
    }
    request.Set("exceptions", std::move(exception_values));
    delegate_->LookupSelectors(
        base::WriteJson(request).value_or("{}"),
        base::BindOnce(&CosmeticClassIdPoller::OnSelectorsJson,
                       weak_factory_.GetWeakPtr()));
  }

  if (!policy_.ShouldPoll(/*generichide_enabled=*/false)) {
    Pause();
  }
}

void CosmeticClassIdPoller::ProbeMutationCounter() {
  if (!StillValid() || request_in_flight_) {
    return;
  }
  request_in_flight_ = true;
  delegate_->RunScript(
      u"__mahoCosmetic.readMutationCounter()",
      base::BindOnce(&CosmeticClassIdPoller::OnMutationCounter,
                     weak_factory_.GetWeakPtr()));
}

void CosmeticClassIdPoller::OnMutationCounter(base::Value result) {
  request_in_flight_ = false;
  if (!StillValid()) {
    return;
  }
  std::optional<uint64_t> counter = ReadCounter(&result);
  if (!counter || !policy_.HasMutationChanged(*counter)) {
    return;
  }
  policy_.RecordMutationCounter(*counter);
  Resume();
  Poll();
}

void CosmeticClassIdPoller::OnSelectorsJson(std::string selectors_json) {
  if (!StillValid()) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(selectors_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list() || parsed->GetList().empty()) {
    return;
  }
  base::ListValue selectors;
  for (const base::Value& value : parsed->GetList()) {
    if (value.is_string()) {
      selectors.Append(value.GetString());
    }
  }
  if (selectors.empty()) {
    return;
  }
  const std::string json = base::WriteJson(selectors).value_or("[]");
  delegate_->RunScript(base::UTF8ToUTF16(base::StrCat(
                           {"__mahoCosmetic.addGenericSelectors(", json, ");"})),
                       base::DoNothing());
}

}  // namespace maho
