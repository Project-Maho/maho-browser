// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"

#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

namespace {

struct RunRoutineContext {
  base::WeakPtr<MahoRoutinesPageHandler> handler;
  MahoRoutinesPageHandler::RunRoutineCallback callback;
  scoped_refptr<base::SequencedTaskRunner> task_runner;
};

void OnRoutineComplete(void* user_data, const char* result_json) {
  auto* ctx = static_cast<RunRoutineContext*>(user_data);
  std::string json_str(result_json ? result_json : "");
  ctx->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoRoutinesPageHandler> handler,
             MahoRoutinesPageHandler::RunRoutineCallback cb,
             std::string json) {
            if (handler) {
              handler->CompleteRoutine(std::move(cb), std::move(json));
            }
          },
          ctx->handler, std::move(ctx->callback), std::move(json_str)));
  delete ctx;
}

void OnRoutineError(void* user_data, const char* error) {
  auto* ctx = static_cast<RunRoutineContext*>(user_data);
  std::string error_str(error ? error : "Unknown error");
  ctx->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoRoutinesPageHandler> handler,
             MahoRoutinesPageHandler::RunRoutineCallback cb,
             std::string err) {
            if (handler) {
              handler->FailRoutine(std::move(cb), std::move(err));
            }
          },
          ctx->handler, std::move(ctx->callback), std::move(error_str)));
  delete ctx;
}

}  // namespace

MahoRoutinesPageHandler::MahoRoutinesPageHandler(
    mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_routines::mojom::Page> page,
    BrowserWindowInterface* browser_window,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      context_token_(
          browser_window,
          (browser_window && web_contents &&
           browser_window->GetTabStripModel()->GetIndexOfWebContents(web_contents) != TabStripModel::kNoTab)
              ? web_contents
              : nullptr),
      host_web_contents_(web_contents ? web_contents->GetWeakPtr() : nullptr) {}

MahoRoutinesPageHandler::~MahoRoutinesPageHandler() = default;

#define ROUTINES_REVALIDATE_OR_RETURN(default_val)                           \
  if (!context_token_.Revalidate(                                            \
          MahoPrivateCapability::kProcessGlobalWebUI)) {                     \
    receiver_.reset();                                                       \
    page_.reset();                                                           \
    return;                                                                  \
  }

void MahoRoutinesPageHandler::ListRoutines(ListRoutinesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(std::vector<maho_routines::mojom::RoutineInfoPtr>());
  char* json = nullptr;
  maho_routines_list(&json);

  std::vector<maho_routines::mojom::RoutineInfoPtr> routines;

  if (json) {
    maho_string_free(json);

    auto r1 = maho_routines::mojom::RoutineInfo::New();
    r1->id = "morning_briefing";
    r1->name = "Morning Briefing";
    r1->cron = "0 8 * * *";
    r1->description = "Summarize top 5 most-read tabs from yesterday + headlines from bookmarked news sites.";
    routines.push_back(std::move(r1));

    auto r2 = maho_routines::mojom::RoutineInfo::New();
    r2->id = "close_old_tabs";
    r2->name = "Close Old Tabs";
    r2->cron = "0 22 * * 0";
    r2->description = "Suggest closing tabs unopened for 14+ days; user confirms before close.";
    routines.push_back(std::move(r2));

    auto r3 = maho_routines::mojom::RoutineInfo::New();
    r3->id = "summarize_reading_list";
    r3->name = "Summarize Reading List";
    r3->cron = "0 9 * * 6";
    r3->description = "Generate 1-paragraph summary for each unread bookmark added this week.";
    routines.push_back(std::move(r3));

    auto r4 = maho_routines::mojom::RoutineInfo::New();
    r4->id = "weekly_tab_tidy";
    r4->name = "Weekly Tab Tidy";
    r4->cron = "0 18 * * 5";
    r4->description = "Apply Tab Tidy across all open tabs (uses request_tidy_tabs in llm_manager).";
    routines.push_back(std::move(r4));
  }

  std::move(callback).Run(std::move(routines));
}

void MahoRoutinesPageHandler::RunRoutine(const std::string& id,
                                         RunRoutineCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }

  auto* ctx = new RunRoutineContext{
      weak_factory_.GetWeakPtr(),
      std::move(callback),
      base::SequencedTaskRunner::GetCurrentDefault()};

  MahoCore* core = maho::GetCore();
  int32_t tier = maho_account_get_tier(core);
  if (tier < 0) {
    tier = 0;
  }
  maho_routines_run(core, id.c_str(), tier, &OnRoutineComplete, &OnRoutineError, ctx);
}

void MahoRoutinesPageHandler::GetUserTier(GetUserTierCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(0);
  MahoCore* core = maho::GetCore();
  int32_t tier = maho_account_get_tier(core);
  if (tier < 0) {
    tier = 0;
  }
  std::move(callback).Run(tier);
}

void MahoRoutinesPageHandler::ListAllRoutines(
    ListAllRoutinesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(std::vector<maho_routines::mojom::RoutineInfoPtr>());
  std::vector<maho_routines::mojom::RoutineInfoPtr> routines;

  MahoCore* core = maho::GetCore();
  char* json = maho_routines_list_all(core);
  if (!json) {
    std::move(callback).Run(std::move(routines));
    return;
  }
  std::string json_str(json);
  maho_string_free(json);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    std::move(callback).Run(std::move(routines));
    return;
  }

  for (const auto& item : parsed->GetList()) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    auto info = maho_routines::mojom::RoutineInfo::New();
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    const std::string* description = dict->FindString("description");
    const std::string* cron = dict->FindString("cron");
    const std::string* trigger = dict->FindString("trigger");
    const std::string* source = dict->FindString("source");
    info->id = id ? *id : "";
    info->name = name ? *name : "";
    info->description = description ? *description : "";
    info->cron = cron ? *cron : "";
    info->schedule = cron ? std::optional<std::string>(*cron) : std::nullopt;
    info->trigger =
        trigger ? std::optional<std::string>(*trigger) : std::nullopt;
    info->is_custom = source && *source == "custom";
    info->enabled = dict->FindBool("enabled").value_or(true);
    routines.push_back(std::move(info));
  }

  std::move(callback).Run(std::move(routines));
}

void MahoRoutinesPageHandler::CreateRoutine(
    const std::string& name,
    const std::string& prompt,
    const std::optional<std::string>& schedule,
    const std::optional<std::string>& trigger,
    CreateRoutineCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(false);
  base::DictValue dict;
  dict.Set("name", name);
  dict.Set("prompt", prompt);
  if (schedule && !schedule->empty()) {
    dict.Set("schedule", *schedule);
  }
  if (trigger && !trigger->empty()) {
    dict.Set("trigger", *trigger);
  }
  std::string json;
  if (!base::JSONWriter::Write(dict, &json)) {
    std::move(callback).Run(false);
    return;
  }

  MahoCore* core = maho::GetCore();
  int32_t rc = maho_routines_create_custom(core, json.c_str());
  std::move(callback).Run(rc == 0);
}

void MahoRoutinesPageHandler::DeleteRoutine(const std::string& id,
                                            DeleteRoutineCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(false);
  MahoCore* core = maho::GetCore();
  int32_t rc = maho_routines_delete_custom(core, id.c_str());
  std::move(callback).Run(rc == 0);
}

void MahoRoutinesPageHandler::CompleteRoutine(RunRoutineCallback callback, std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  auto result = maho_routines::mojom::RoutineRunResult::New();
  result->content = std::move(json);
  std::move(callback).Run(std::move(result), std::nullopt);
}

void MahoRoutinesPageHandler::FailRoutine(RunRoutineCallback callback, std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  std::move(callback).Run(nullptr, std::move(error));
}
