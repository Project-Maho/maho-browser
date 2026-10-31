// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_tab_tidy_orchestrator.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace maho {

namespace {

const base::DictValue* FindUpdateOfKind(const base::Value& parsed,
                                        const std::string& wanted_kind) {
  if (parsed.is_list()) {
    for (const auto& item : parsed.GetList()) {
      const auto* d = item.GetIfDict();
      if (!d) {
        continue;
      }
      const std::string* kind = d->FindString("kind");
      if (kind && *kind == wanted_kind) {
        return d;
      }
    }
    return nullptr;
  }
  if (parsed.is_dict()) {
    const std::string* kind = parsed.GetDict().FindString("kind");
    if (kind && *kind == wanted_kind) {
      return &parsed.GetDict();
    }
  }
  return nullptr;
}

class TidyRun {
 public:
  TidyRun(Browser* browser,
          MahoTabTidyOrchestrator::ResultCallback callback,
          base::RepeatingCallback<bool()> ai_gate)
      : browser_(browser),
        callback_(std::move(callback)),
        ai_gate_(std::move(ai_gate)) {}

  void Start() {
    if (!ai_gate_ || !ai_gate_.Run()) {
      Finish(0, "AI is not available in this context");
      return;
    }
    if (!browser_) {
      Finish(0, "No browser context");
      return;
    }
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    if (!bridge) {
      Finish(0, "Space bridge unavailable");
      return;
    }
    space_id_ = bridge->GetActiveSpaceId(browser_);
    if (space_id_.empty()) {
      Finish(0, "No active space");
      return;
    }

    MahoCore* core = maho::GetCore();
    if (!core) {
      Finish(0, "Maho core not initialized");
      return;
    }

    base::DictValue event_dict;
    event_dict.Set("kind", "request_tidy_tabs");
    event_dict.Set("space_id", space_id_);
    std::string event_json;
    base::JSONWriter::Write(event_dict, &event_json);

    std::string updates = maho::core::HandleEvent(core, event_json.c_str());
    InvalidateSidebarCoreCacheForUpdatesJson(updates);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
    if (!parsed) {
      Finish(0, "Failed to parse core response");
      return;
    }

    const base::DictValue* llm_request =
        FindUpdateOfKind(*parsed, "request_llm_completion");
    if (!llm_request) {
      // Most likely cause: fewer than 6 unpinned tabs in the active space
      // (RequestTidyTabs returns vec![] below that threshold).
      Finish(0, "Need at least 6 tabs in this space");
      return;
    }

    const std::string* request_id_ptr = llm_request->FindString("request_id");
    if (!request_id_ptr) {
      Finish(0, "LLM request missing request_id");
      return;
    }
    request_id_ = *request_id_ptr;

    auto prompt_messages = maho::ai::ExtractPromptMessages(*llm_request);
    if (!prompt_messages) {
      Finish(0, "Failed to extract LLM prompt");
      return;
    }

    auto route_res = maho::ai::ResolveMahoAiModelRoute(
        browser_->GetProfile()->GetPrefs(), maho::ai::MahoAiTask::kTabTidy);
    if (!route_res.is_ok()) {
      Finish(0, "Tab Tidy model is not configured");
      return;
    }

    auto url_loader_factory = browser_->GetProfile()
                                  ->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    llm_client_ = std::make_unique<MahoAiLlmClient>(
        browser_->GetProfile()->GetPrefs(),
        url_loader_factory.get(),
        nullptr,
        /*provider_adapter_v2_enabled=*/false,
        ai_gate_);

    MahoAiLlmClient::CompletionRequest request;
    request.provider_id = route_res.route.provider_id;
    request.endpoint = route_res.route.endpoint;
    request.model = route_res.route.model_id;
    request.prompt_messages = base::ListValue(std::move(*prompt_messages));

    llm_client_->StartCompletion(
        std::move(request),
        base::BindRepeating([](const std::string&) {}),
        base::BindOnce(&TidyRun::OnLLMComplete, weak_factory_.GetWeakPtr()),
        base::BindOnce(&TidyRun::OnLLMError, weak_factory_.GetWeakPtr()));
  }

 private:
  void OnLLMComplete(MahoAiLlmClient::CompletionResult result) {
    MahoCore* core = maho::GetCore();
    if (!core) {
      Finish(0, "Maho core not initialized");
      return;
    }

    base::DictValue llm_result_dict;
    llm_result_dict.Set("kind", "llm_result");
    llm_result_dict.Set("request_id", request_id_);
    llm_result_dict.Set("result", result.full_text);
    std::string llm_result_json;
    base::JSONWriter::Write(llm_result_dict, &llm_result_json);

    std::string updates =
        maho::core::HandleEvent(core, llm_result_json.c_str());
    InvalidateSidebarCoreCacheForUpdatesJson(updates);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
    if (!parsed) {
      Finish(0, "Failed to parse LLM result response");
      return;
    }

    if (!FindUpdateOfKind(*parsed, "tidy_tabs_ready")) {
      Finish(0, "LLM did not return valid tidy folders");
      return;
    }

    base::DictValue apply_dict;
    apply_dict.Set("kind", "apply_tidy_tabs");
    apply_dict.Set("space_id", space_id_);
    std::string apply_json;
    base::JSONWriter::Write(apply_dict, &apply_json);

    std::string apply_updates =
        maho::core::HandleEvent(core, apply_json.c_str());
    InvalidateSidebarCoreCacheForUpdatesJson(apply_updates);
    std::optional<base::Value> apply_parsed =
        base::JSONReader::Read(apply_updates, base::JSON_PARSE_RFC);

    int folder_count = 0;
    if (apply_parsed && apply_parsed->is_list()) {
      for (const auto& item : apply_parsed->GetList()) {
        const auto* d = item.GetIfDict();
        if (!d) {
          continue;
        }
        const std::string* kind = d->FindString("kind");
        if (kind && *kind == "folder_created") {
          folder_count++;
        }
      }
    }

    if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
      bridge->NotifyChanged();
    }

    if (folder_count == 0) {
      Finish(0, "No folders were created");
      return;
    }
    Finish(folder_count, std::string());
  }

  void OnLLMError(const std::string& error) {
    LOG(ERROR) << "[maho-tidy] LLM error: " << error;
    Finish(0, error.empty() ? std::string("LLM call failed") : error);
  }

  void Finish(int folder_count, const std::string& error_message) {
    if (callback_) {
      std::move(callback_).Run(folder_count, error_message);
    }
    delete this;
  }

  raw_ptr<Browser> browser_;
  MahoTabTidyOrchestrator::ResultCallback callback_;
  base::RepeatingCallback<bool()> ai_gate_;
  std::string space_id_;
  std::string request_id_;
  std::unique_ptr<MahoAiLlmClient> llm_client_;
  base::WeakPtrFactory<TidyRun> weak_factory_{this};
};

}  // namespace

// static
void MahoTabTidyOrchestrator::RunOneShot(Browser* browser,
                                         ResultCallback callback,
                                         base::RepeatingCallback<bool()> ai_gate) {
  auto* run = new TidyRun(browser, std::move(callback), std::move(ai_gate));
  run->Start();
}

}  // namespace maho
