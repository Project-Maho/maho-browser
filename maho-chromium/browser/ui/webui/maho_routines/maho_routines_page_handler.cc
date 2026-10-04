// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/webui/maho_routines/maho_trace_recorder_client.h"

struct MahoRoutineStatusContext {
  base::WeakPtr<MahoRoutinesPageHandler> handler;
  scoped_refptr<base::SequencedTaskRunner> task_runner;
};

namespace {

struct RunRoutineContext {
  base::WeakPtr<MahoRoutinesPageHandler> handler;
  MahoRoutinesPageHandler::RunRoutineCallback callback;
  scoped_refptr<base::SequencedTaskRunner> task_runner;
};

void OnRoutineStatus(void* user_data, const char* status_json) {
  auto* context = static_cast<MahoRoutineStatusContext*>(user_data);
  const std::string json(status_json ? status_json : "");
  context->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoRoutinesPageHandler::OnRoutineRunStatusJson,
                     context->handler, json));
}

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

// Parses one `RoutineRunRecord` row as emitted by `maho_routines_history` /
// `maho_routines_latest`. The Rust type serializes with `rename_all =
// "camelCase"`, so the wire keys are `resultId` / `routineId` / `ranAt` /
// `success` / `content` / `source`. Returns null for anything that is not a
// dict, so a malformed element is skipped rather than crashing the page.
maho_routines::mojom::RoutineRunRecordPtr ParseRunRecord(
    const base::Value& item) {
  const base::DictValue* dict = item.GetIfDict();
  if (!dict) {
    return nullptr;
  }
  auto record = maho_routines::mojom::RoutineRunRecord::New();
  // `resultId` / `ranAt` are i64 in Rust; base::Value stores integers beyond
  // 32 bits as doubles, so accept either representation.
  if (std::optional<double> result_id = dict->FindDouble("resultId")) {
    record->result_id = static_cast<int64_t>(*result_id);
  }
  if (std::optional<double> ran_at = dict->FindDouble("ranAt")) {
    record->ran_at = static_cast<int64_t>(*ran_at);
  }
  const std::string* routine_id = dict->FindString("routineId");
  record->routine_id = routine_id ? *routine_id : "";
  record->success = dict->FindBool("success").value_or(false);
  const std::string* content = dict->FindString("content");
  record->content = content ? *content : "";
  const std::string* source = dict->FindString("source");
  record->source = source ? *source : "";
  return record;
}

// Parses a whole `maho_routines_history` payload (a JSON array). A payload that
// is absent, unparseable, or not an array yields an empty history rather than
// an error: the page renders "no runs yet" instead of breaking.
std::vector<maho_routines::mojom::RoutineRunRecordPtr> ParseRunHistoryJson(
    const std::string& json) {
  std::vector<maho_routines::mojom::RoutineRunRecordPtr> records;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return records;
  }
  for (const auto& item : parsed->GetList()) {
    if (auto record = ParseRunRecord(item)) {
      records.push_back(std::move(record));
    }
  }
  return records;
}

// Parses a whole `maho_routines_latest` payload (a bare JSON object). Returns
// null for an absent, unparseable, or non-object payload -- "never ran" and
// "backend error" are both surfaced as null rather than a phantom record.
maho_routines::mojom::RoutineRunRecordPtr ParseLatestRunJson(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return nullptr;
  }
  return ParseRunRecord(*parsed);
}

// Parses a `maho_routines_run` completion payload: a serialized Rust
// `RoutineResult`, which uses serde's default (verbatim) field naming --
// {"id","ran_at","content","success"} -- NOT the camelCase used by the
// routine-inbox records above.
std::optional<maho_routines::mojom::RoutineRunState> ParseRunState(
    const std::string& state) {
  if (state == "queued") {
    return maho_routines::mojom::RoutineRunState::kQueued;
  }
  if (state == "running") {
    return maho_routines::mojom::RoutineRunState::kRunning;
  }
  if (state == "awaiting_approval") {
    return maho_routines::mojom::RoutineRunState::kAwaitingApproval;
  }
  if (state == "succeeded") {
    return maho_routines::mojom::RoutineRunState::kSucceeded;
  }
  if (state == "failed") {
    return maho_routines::mojom::RoutineRunState::kFailed;
  }
  return std::nullopt;
}

bool IsIntegralInInt64Range(double value) {
  return std::isfinite(value) && std::trunc(value) == value &&
         value >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
         value <= static_cast<double>(std::numeric_limits<int64_t>::max());
}

maho_routines::mojom::RoutineRunStatusPtr ParseRunStatus(
    const base::Value& item) {
  const base::DictValue* dict = item.GetIfDict();
  if (!dict) {
    return nullptr;
  }
  const std::string* run_id = dict->FindString("runId");
  const std::string* routine_id = dict->FindString("routineId");
  const std::string* source = dict->FindString("source");
  const std::string* state = dict->FindString("state");
  std::optional<double> revision = dict->FindDouble("revision");
  std::optional<double> started_at = dict->FindDouble("startedAt");
  std::optional<double> updated_at = dict->FindDouble("updatedAt");
  const auto parsed_state = state ? ParseRunState(*state) : std::nullopt;
  constexpr double kMaxExactInteger = 9007199254740991.0;
  if (!run_id || run_id->empty() || !routine_id || routine_id->empty() ||
      !source || (*source != "manual" && *source != "scheduled" &&
                  *source != "event") ||
      !parsed_state || !revision || !std::isfinite(*revision) ||
      std::trunc(*revision) != *revision || *revision < 1 ||
      *revision > kMaxExactInteger || !started_at ||
      !IsIntegralInInt64Range(*started_at) || !updated_at ||
      !IsIntegralInInt64Range(*updated_at)) {
    return nullptr;
  }
  auto status = maho_routines::mojom::RoutineRunStatus::New();
  status->run_id = *run_id;
  status->routine_id = *routine_id;
  status->source = *source;
  status->state = *parsed_state;
  status->revision = static_cast<uint64_t>(*revision);
  status->started_at = static_cast<int64_t>(*started_at);
  status->updated_at = static_cast<int64_t>(*updated_at);
  if (std::optional<double> finished_at = dict->FindDouble("finishedAt");
      finished_at && IsIntegralInInt64Range(*finished_at)) {
    status->finished_at = static_cast<int64_t>(*finished_at);
  }
  if (const std::string* result = dict->FindString("result")) {
    status->result = *result;
  }
  if (const std::string* error = dict->FindString("error")) {
    status->error = *error;
  }
  if (const base::DictValue* approval = dict->FindDict("approval")) {
    const std::string* approval_id = approval->FindString("approvalId");
    const std::string* tool_name = approval->FindString("toolName");
    const std::string* sensitivity = approval->FindString("sensitivity");
    if (approval_id && !approval_id->empty() && tool_name &&
        !tool_name->empty() && sensitivity && !sensitivity->empty()) {
      auto parsed = maho_routines::mojom::RoutineRunApproval::New();
      parsed->approval_id = *approval_id;
      parsed->tool_name = *tool_name;
      parsed->sensitivity = *sensitivity;
      status->approval = std::move(parsed);
    }
  }
  if (*parsed_state == maho_routines::mojom::RoutineRunState::kAwaitingApproval &&
      !status->approval) {
    return nullptr;
  }
  return status;
}

std::vector<maho_routines::mojom::RoutineRunStatusPtr> ParseRunStatusesJson(
    const std::string& json) {
  std::vector<maho_routines::mojom::RoutineRunStatusPtr> statuses;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return statuses;
  }
  for (const auto& item : parsed->GetList()) {
    if (auto status = ParseRunStatus(item)) {
      statuses.push_back(std::move(status));
    }
  }
  return statuses;
}

maho_routines::mojom::RoutineRunStatusPtr ParseRunStatusJson(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  return parsed ? ParseRunStatus(*parsed) : nullptr;
}

maho_routines::mojom::RoutineRunResultPtr ParseRunResultJson(
    const std::string& json) {
  auto result = maho_routines::mojom::RoutineRunResult::New();
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;
  if (!dict) {
    // Unparseable payload: keep the raw text visible rather than silently
    // showing an empty result, and never claim success.
    result->content = json;
    result->success = false;
    return result;
  }
  const std::string* id = dict->FindString("id");
  result->id = id ? *id : "";
  if (std::optional<double> ran_at = dict->FindDouble("ran_at")) {
    result->ran_at = static_cast<int64_t>(*ran_at);
  }
  const std::string* content = dict->FindString("content");
  result->content = content ? *content : "";
  result->success = dict->FindBool("success").value_or(false);
  return result;
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
      browser_window_(browser_window),
      host_web_contents_(web_contents ? web_contents->GetWeakPtr() : nullptr),
      owning_task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {
  status_callback_context_ = std::make_unique<MahoRoutineStatusContext>(
      MahoRoutineStatusContext{weak_factory_.GetWeakPtr(),
                               owning_task_runner_});
  status_callback_core_ = maho::GetCore();
  status_callback_token_ = maho_routines_register_status_callback(
      status_callback_core_, status_callback_context_.get(), &OnRoutineStatus);
  if (status_callback_token_ == 0) {
    status_callback_core_ = nullptr;
    status_callback_context_.reset();
  }
}

MahoRoutinesPageHandler::~MahoRoutinesPageHandler() {
  maho_routines_unregister_status_callback(status_callback_core_,
                                            status_callback_token_);
}

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
  int rc = maho_routines_run(core, id.c_str(), tier, &OnRoutineComplete,
                             &OnRoutineError, ctx);
  if (rc != 0) {
    auto cb = std::move(ctx->callback);
    delete ctx;
    FailRoutine(std::move(cb),
                tier < 2 ? "Routines are available on Max tier only"
                         : "Failed to start routine");
  }
}

void MahoRoutinesPageHandler::StartRoutine(const std::string& id,
                                           StartRoutineCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  MahoCore* core = maho::GetCore();
  int32_t tier = maho_account_get_tier(core);
  if (tier < 0) {
    tier = 0;
  }
  char* json = maho_routines_start(core, id.c_str(), tier, "manual");
  if (!json) {
    std::move(callback).Run(std::nullopt,
                            std::optional<std::string>("Could not start routine"));
    return;
  }
  std::string json_string(json);
  maho_string_free(json);
  auto status = ParseRunStatusJson(json_string);
  if (!status) {
    std::move(callback).Run(std::nullopt,
                            std::optional<std::string>("Invalid run status"));
    return;
  }
  std::move(callback).Run(std::optional<std::string>(status->run_id),
                          std::nullopt);
}

void MahoRoutinesPageHandler::GetRoutineRunStatuses(
    GetRoutineRunStatusesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(
      std::vector<maho_routines::mojom::RoutineRunStatusPtr>());
  char* json = maho_routines_active_runs(maho::GetCore());
  if (!json) {
    std::move(callback).Run({});
    return;
  }
  std::string json_string(json);
  maho_string_free(json);
  std::move(callback).Run(ParseRunStatusesJson(json_string));
}

void MahoRoutinesPageHandler::RespondToRoutineApproval(
    const std::string& run_id,
    const std::string& approval_id,
    bool approved,
    RespondToRoutineApprovalCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(false);
  const bool ok = maho_routines_respond_to_approval(
      maho::GetCore(), run_id.c_str(), approval_id.c_str(), approved);
  std::move(callback).Run(ok);
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

void MahoRoutinesPageHandler::ListRunHistory(
    const std::optional<std::string>& routine_id,
    uint32_t limit,
    ListRunHistoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(
      std::vector<maho_routines::mojom::RoutineRunRecordPtr>());
  std::vector<maho_routines::mojom::RoutineRunRecordPtr> records;

  MahoCore* core = maho::GetCore();
  char* json = maho_routines_history(
      core, routine_id ? routine_id->c_str() : nullptr, limit);
  if (!json) {
    // Null means "no core / storage unavailable" on the Rust side; surface an
    // empty history rather than an error so the page still renders.
    std::move(callback).Run(std::move(records));
    return;
  }
  std::string json_str(json);
  maho_string_free(json);

  std::move(callback).Run(ParseRunHistoryJson(json_str));
}

// static
std::vector<maho_routines::mojom::RoutineRunRecordPtr>
MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting(
    const std::string& json) {
  return ParseRunHistoryJson(json);
}

// static
maho_routines::mojom::RoutineRunRecordPtr
MahoRoutinesPageHandler::ParseLatestRunJsonForTesting(const std::string& json) {
  return ParseLatestRunJson(json);
}

void MahoRoutinesPageHandler::GetLatestRun(const std::string& routine_id,
                                           GetLatestRunCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(maho_routines::mojom::RoutineRunRecordPtr());

  MahoCore* core = maho::GetCore();
  char* json = maho_routines_latest(core, routine_id.c_str());
  if (!json) {
    // Null is the documented "never ran" answer as well as the error answer.
    std::move(callback).Run(nullptr);
    return;
  }
  std::string json_str(json);
  maho_string_free(json);

  std::move(callback).Run(ParseLatestRunJson(json_str));
}

void MahoRoutinesPageHandler::StartTraceRecording(
    int64_t tab_id,
    StartTraceRecordingCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(false);

  content::WebContents* target_contents = nullptr;
  if (browser_window_ && browser_window_->GetTabStripModel()) {
    TabStripModel* tab_strip = browser_window_->GetTabStripModel();
    if (tab_id <= 0) {
      target_contents = tab_strip->GetActiveWebContents();
    } else {
      for (int i = 0; i < tab_strip->count(); ++i) {
        content::WebContents* contents = tab_strip->GetWebContentsAt(i);
        if (contents) {
          sessions::SessionTabHelper* helper =
              sessions::SessionTabHelper::FromWebContents(contents);
          if (helper && helper->session_id().id() == tab_id) {
            target_contents = contents;
            break;
          }
        }
      }
    }
  }
  if (!target_contents) {
    target_contents = host_web_contents_.get();
  }

  bool ok =
      maho::MahoTraceRecorderClient::GetInstance()->Start(target_contents, tab_id);
  std::move(callback).Run(ok);
}

void MahoRoutinesPageHandler::StopTraceRecording(
    StopTraceRecordingCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(false);

  std::string trace_json =
      maho::MahoTraceRecorderClient::GetInstance()->StopAndExportJson();
  if (trace_json.empty() || trace_json == "{}") {
    std::move(callback).Run(std::nullopt, "No active recording or empty trace");
    return;
  }
  std::move(callback).Run(trace_json, std::nullopt);
}

void MahoRoutinesPageHandler::CreateRoutineFromTrace(
    const std::string& name,
    const std::string& description,
    const std::string& trace_json,
    const std::optional<std::string>& schedule,
    const std::optional<std::string>& trigger,
    CreateRoutineFromTraceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ROUTINES_REVALIDATE_OR_RETURN(false);

  if (name.empty() || trace_json.empty()) {
    std::move(callback).Run(false);
    return;
  }

  std::string prompt = description;
  if (!prompt.empty()) {
    prompt += "\n\n";
  }
  prompt += "Replay user trace:\n```json\n" + trace_json + "\n```";

  CreateRoutine(name, prompt, schedule, trigger, std::move(callback));
}

void MahoRoutinesPageHandler::IsTraceRecording(
    IsTraceRecordingCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho::MahoTraceRecorderClient::GetInstance()->is_recording(),
      maho::MahoTraceRecorderClient::GetInstance()->tab_id());
}

void MahoRoutinesPageHandler::CompleteRoutine(RunRoutineCallback callback, std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  // Previously the whole JSON blob was dumped into `content` and every other
  // field was left at its default -- notably `success = false` for successful
  // runs. Each field now lands in its own mojom slot.
  std::move(callback).Run(ParseRunResultJson(json), std::nullopt);
}

// static
maho_routines::mojom::RoutineRunResultPtr
MahoRoutinesPageHandler::ParseRunResultJsonForTesting(const std::string& json) {
  return ParseRunResultJson(json);
}

// static
maho_routines::mojom::RoutineRunStatusPtr
MahoRoutinesPageHandler::ParseRunStatusJsonForTesting(const std::string& json) {
  return ParseRunStatusJson(json);
}

// static
std::vector<maho_routines::mojom::RoutineRunStatusPtr>
MahoRoutinesPageHandler::ParseRunStatusesJsonForTesting(
    const std::string& json) {
  return ParseRunStatusesJson(json);
}

void MahoRoutinesPageHandler::OnRoutineRunStatusJson(std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!page_.is_bound()) {
    return;
  }
  if (host_web_contents_ &&
      !context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  if (auto status = ParseRunStatusJson(json)) {
    Profile* profile = host_web_contents_
                           ? Profile::FromBrowserContext(
                                 host_web_contents_->GetBrowserContext())
                           : nullptr;
    maho::ai::MahoControlActivityService* activity_service =
        profile ? maho::ai::MahoControlActivityService::GetForProfile(profile)
                : nullptr;
    if (activity_service) {
      const std::string activity_id = "routine-" + status->run_id;
      std::optional<maho::ai::ControlActivity> activity =
          activity_service->GetActivity(activity_id);
      if (!activity) {
        maho::ai::StartControlActivityParams params;
        params.session_id = activity_id;
        params.controller_session_id = status->routine_id;
        params.controller_display_name = "Maho Routine";
        params.controller_type = maho::ai::ControllerType::kAutomation;
        params.control_plane = maho::ai::ControlPlane::kLocalAgent;
        params.category = maho::ai::ActivityCategory::kOther;
        params.sensitivity =
            status->approval ? maho::ai::ActivitySensitivity::kHigh
                             : maho::ai::ActivitySensitivity::kMedium;
        params.start_receipt = {maho::ai::ActivityReceiptKind::kStart,
                                "started", std::string()};
        activity_service->StartActivity(std::move(params));
        activity = activity_service->GetActivity(activity_id);
      }

      auto transition =
          [&](maho::ai::ControlActivityState state,
              maho::ai::ApprovalOutcome approval,
              std::optional<maho::ai::ActivityReceipt> receipt = std::nullopt) {
            activity = activity_service->GetActivity(activity_id);
            if (!activity || activity->state == state) {
              return;
            }
            maho::ai::ControlActivityUpdate update;
            update.event_revision = activity->event_revision + 1;
            update.state = state;
            update.approval_outcome = approval;
            update.receipt = std::move(receipt);
            activity_service->ApplyUpdate(activity_id, std::move(update));
          };

      switch (status->state) {
        case maho_routines::mojom::RoutineRunState::kQueued:
        case maho_routines::mojom::RoutineRunState::kRunning:
          transition(maho::ai::ControlActivityState::kActing,
                     maho::ai::ApprovalOutcome::kNotRequested);
          break;
        case maho_routines::mojom::RoutineRunState::kAwaitingApproval:
          transition(maho::ai::ControlActivityState::kActing,
                     maho::ai::ApprovalOutcome::kNotRequested);
          transition(maho::ai::ControlActivityState::kWaitingApproval,
                     maho::ai::ApprovalOutcome::kPending);
          break;
        case maho_routines::mojom::RoutineRunState::kSucceeded:
          if (activity &&
              activity->state ==
                  maho::ai::ControlActivityState::kWaitingApproval) {
            transition(maho::ai::ControlActivityState::kActing,
                       maho::ai::ApprovalOutcome::kApproved);
          }
          transition(maho::ai::ControlActivityState::kCompleted,
                     maho::ai::ApprovalOutcome::kNotRequested,
                     maho::ai::ActivityReceipt{
                         maho::ai::ActivityReceiptKind::kResult, "done",
                         std::string()});
          break;
        case maho_routines::mojom::RoutineRunState::kFailed:
          transition(
              maho::ai::ControlActivityState::kFailed,
              activity &&
                      activity->state ==
                          maho::ai::ControlActivityState::kWaitingApproval
                  ? maho::ai::ApprovalOutcome::kDenied
                  : maho::ai::ApprovalOutcome::kNotRequested,
              maho::ai::ActivityReceipt{
                  maho::ai::ActivityReceiptKind::kFailure, "provider_error",
                  std::string()});
          break;
      }
    }
    page_->OnRoutineRunStatusChanged(std::move(status));
  }
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
