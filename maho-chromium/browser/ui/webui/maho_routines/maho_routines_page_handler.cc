// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

struct RunRoutineContext {
  MahoRoutinesPageHandler::RunRoutineCallback callback;
  scoped_refptr<base::SequencedTaskRunner> task_runner;
};

void OnRoutineComplete(void* user_data, const char* result_json) {
  auto* ctx = static_cast<RunRoutineContext*>(user_data);
  std::string json_str(result_json ? result_json : "");
  ctx->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](MahoRoutinesPageHandler::RunRoutineCallback cb,
             std::string json) {
            auto result = maho_routines::mojom::RoutineRunResult::New();
            result->content = std::move(json);
            std::move(cb).Run(std::move(result), std::nullopt);
          },
          std::move(ctx->callback), std::move(json_str)));
  delete ctx;
}

void OnRoutineError(void* user_data, const char* error) {
  auto* ctx = static_cast<RunRoutineContext*>(user_data);
  std::string error_str(error ? error : "Unknown error");
  ctx->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](MahoRoutinesPageHandler::RunRoutineCallback cb,
             std::string err) {
            std::move(cb).Run(nullptr, std::move(err));
          },
          std::move(ctx->callback), std::move(error_str)));
  delete ctx;
}

}  // namespace

MahoRoutinesPageHandler::MahoRoutinesPageHandler(
    mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_routines::mojom::Page> page)
    : receiver_(this, std::move(receiver)), page_(std::move(page)) {}

MahoRoutinesPageHandler::~MahoRoutinesPageHandler() = default;

void MahoRoutinesPageHandler::ListRoutines(ListRoutinesCallback callback) {
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
  auto* ctx = new RunRoutineContext{
      std::move(callback),
      base::SequencedTaskRunner::GetCurrentDefault()};

  int32_t tier = maho_account_get_tier(nullptr);
  if (tier < 0) {
    tier = 0;
  }
  maho_routines_run(id.c_str(), tier, &OnRoutineComplete, &OnRoutineError, ctx);
}

void MahoRoutinesPageHandler::GetUserTier(GetUserTierCallback callback) {
  int32_t tier = maho_account_get_tier(nullptr);
  if (tier < 0) {
    tier = 0;
  }
  std::move(callback).Run(tier);
}
