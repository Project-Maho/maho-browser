// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "maho/browser/ui/webui/maho_routines/maho_trace_recorder_client.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// RAII swap of the process-global MahoCore so the handler's maho::GetCore()
// resolves to our temp-sqlite-backed core for the duration of a test.
class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

 private:
  raw_ptr<MahoCore> saved_;
};

// Collects the routines returned by a ListAllRoutines() call. The handler runs
// the callback synchronously (FFI is blocking), so no RunLoop is required.
std::vector<maho_routines::mojom::RoutineInfoPtr> ListAll(
    MahoRoutinesPageHandler* handler) {
  std::vector<maho_routines::mojom::RoutineInfoPtr> out;
  handler->ListAllRoutines(base::BindLambdaForTesting(
      [&](std::vector<maho_routines::mojom::RoutineInfoPtr> routines) {
        out = std::move(routines);
      }));
  return out;
}

// Parses a run-completion payload exactly as CompleteRoutine() does. Uses the
// static hook rather than the instance method because a unit-test handler has
// no BrowserWindowInterface, so MahoPrivateContextToken::Revalidate() rejects
// every instance method before the parsing logic runs.
maho_routines::mojom::RoutineRunResultPtr Complete(const std::string& json) {
  return MahoRoutinesPageHandler::ParseRunResultJsonForTesting(json);
}

class TestRoutinesPage : public maho_routines::mojom::Page {
 public:
  TestRoutinesPage() : receiver_(this, remote_.BindNewPipeAndPassReceiver()) {}

  mojo::PendingRemote<maho_routines::mojom::Page> TakeRemote() {
    return remote_.Unbind();
  }

  void OnRoutineComplete(
      maho_routines::mojom::RoutineRunResultPtr result) override {}
  void OnRoutineError(const std::string& id,
                      const std::string& error) override {}
  void OnRoutineRunStatusChanged(
      maho_routines::mojom::RoutineRunStatusPtr status) override {
    last_status = std::move(status);
  }

  maho_routines::mojom::RoutineRunStatusPtr last_status;

 private:
  mojo::Remote<maho_routines::mojom::Page> remote_;
  mojo::Receiver<maho_routines::mojom::Page> receiver_;
};

class MahoRoutinesPageHandlerTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const base::FilePath db =
        temp_dir_.GetPath().AppendASCII("maho_routines_test.sqlite");
    core_ = maho_core_new_with_storage(db.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    scoped_core_ = std::make_unique<ScopedCoreOverride>(core_);
    AddTab(browser(), GURL("chrome://newtab"));
  }

  void TearDown() override {
    scoped_core_.reset();
    if (core_) {
      maho_core_free(core_);
      core_ = nullptr;
    }
    BrowserWithTestWindowTest::TearDown();
  }

  // Builds a page handler bound to throwaway mojo pipes.
  std::unique_ptr<MahoRoutinesPageHandler> MakeHandler(
      mojo::PendingRemote<maho_routines::mojom::Page> page_remote = {}) {
    if (!page_remote.is_valid()) {
      std::ignore = page_remote.InitWithNewPipeAndPassReceiver();
    }
    content::WebContents* contents =
        browser() && browser()->GetTabStripModel()
            ? browser()->GetTabStripModel()->GetActiveWebContents()
            : nullptr;
    return std::make_unique<MahoRoutinesPageHandler>(
        handler_remote_.BindNewPipeAndPassReceiver(), std::move(page_remote),
        browser(), contents);
  }

  base::ScopedTempDir temp_dir_;
  raw_ptr<MahoCore> core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> scoped_core_;
  mojo::Remote<maho_routines::mojom::PageHandler> handler_remote_;
};

TEST_F(MahoRoutinesPageHandlerTest,
       SharedHandlerPreservesTierAndBackendResultContract) {
  auto handler = MakeHandler();

  int32_t tier = -1;
  handler->GetUserTier(base::BindLambdaForTesting([&](int32_t value) {
    tier = value;
  }));
  EXPECT_EQ(tier, 0);

  maho_routines::mojom::RoutineRunResultPtr run_result;
  std::optional<std::string> run_error;
  base::RunLoop run_loop;
  handler->RunRoutine("morning_briefing",
                      base::BindLambdaForTesting(
                          [&](maho_routines::mojom::RoutineRunResultPtr result,
                              const std::optional<std::string>& error) {
                            run_result = std::move(result);
                            run_error = error;
                            run_loop.Quit();
                          }));
  run_loop.Run();
  EXPECT_FALSE(run_result);
  EXPECT_EQ(run_error, std::optional<std::string>(
                           "Routines are available on Max tier only"));

  auto parsed = MahoRoutinesPageHandler::ParseRunResultJsonForTesting(
      R"({"id":"morning_briefing","ran_at":1750000000,)"
      R"("content":"Here is your briefing.","success":true})");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->id, "morning_briefing");
  EXPECT_EQ(parsed->ran_at, 1750000000);
  EXPECT_EQ(parsed->content, "Here is your briefing.");
  EXPECT_TRUE(parsed->success);
}

// Create a custom routine, confirm it surfaces in ListAllRoutines() with
// is_custom=true and the expected fields, then delete it and confirm it is
// gone. Mirrors the Rust-side FFI roundtrip test at the mojom-handler boundary.
TEST_F(MahoRoutinesPageHandlerTest, CreateListDeleteRoundtrip) {
  auto handler = MakeHandler();

  // Baseline: only built-in routines, none custom.
  std::vector<maho_routines::mojom::RoutineInfoPtr> baseline =
      ListAll(handler.get());
  const size_t baseline_count = baseline.size();
  for (const auto& info : baseline) {
    EXPECT_FALSE(info->is_custom) << "built-in routine must not be is_custom";
  }

  // Create a valid cron-scheduled custom routine.
  bool create_ok = false;
  handler->CreateRoutine(
      "My Recipe", "Summarize tabs", std::optional<std::string>("0 9 * * *"),
      std::nullopt,
      base::BindLambdaForTesting([&](bool ok) { create_ok = ok; }));
  EXPECT_TRUE(create_ok);

  // ListAll now contains exactly one custom routine with our fields.
  std::vector<maho_routines::mojom::RoutineInfoPtr> after_create =
      ListAll(handler.get());
  EXPECT_EQ(after_create.size(), baseline_count + 1);

  std::string custom_id;
  int custom_count = 0;
  for (const auto& info : after_create) {
    if (info->is_custom) {
      ++custom_count;
      custom_id = info->id;
      EXPECT_EQ(info->name, "My Recipe");
      EXPECT_EQ(info->cron, "0 9 * * *");
      ASSERT_TRUE(info->schedule.has_value());
      EXPECT_EQ(info->schedule.value(), "0 9 * * *");
    }
  }
  EXPECT_EQ(custom_count, 1);
  ASSERT_FALSE(custom_id.empty());

  // Delete it.
  bool delete_ok = false;
  handler->DeleteRoutine(
      custom_id, base::BindLambdaForTesting([&](bool ok) { delete_ok = ok; }));
  EXPECT_TRUE(delete_ok);

  // Deleting again reports failure (not found).
  bool delete_again_ok = true;
  handler->DeleteRoutine(custom_id, base::BindLambdaForTesting([&](bool ok) {
                           delete_again_ok = ok;
                         }));
  EXPECT_FALSE(delete_again_ok);

  // ListAll is back to baseline with no custom routines.
  std::vector<maho_routines::mojom::RoutineInfoPtr> after_delete =
      ListAll(handler.get());
  EXPECT_EQ(after_delete.size(), baseline_count);
  for (const auto& info : after_delete) {
    EXPECT_FALSE(info->is_custom);
  }
}

// An invalid cron schedule must be rejected by CreateRoutine.
TEST_F(MahoRoutinesPageHandlerTest, CreateRejectsInvalidCron) {
  auto handler = MakeHandler();

  bool ok = true;
  handler->CreateRoutine(
      "Bad", "x", std::optional<std::string>("not a cron"), std::nullopt,
      base::BindLambdaForTesting([&](bool result) { ok = result; }));
  EXPECT_FALSE(ok);

  // The rejected routine must not appear in ListAll.
  std::vector<maho_routines::mojom::RoutineInfoPtr> routines =
      ListAll(handler.get());
  for (const auto& info : routines) {
    EXPECT_FALSE(info->is_custom);
  }
}

// Empty name/prompt must be rejected.
TEST_F(MahoRoutinesPageHandlerTest, CreateRejectsEmptyNameOrPrompt) {
  auto handler = MakeHandler();

  bool ok = true;
  handler->CreateRoutine(
      "", "prompt", std::nullopt, std::nullopt,
      base::BindLambdaForTesting([&](bool result) { ok = result; }));
  EXPECT_FALSE(ok);
}

// Deleting an id that does not exist must report failure.
TEST_F(MahoRoutinesPageHandlerTest, DeleteUnknownReturnsFalse) {
  auto handler = MakeHandler();

  bool ok = true;
  handler->DeleteRoutine(
      "does-not-exist",
      base::BindLambdaForTesting([&](bool result) { ok = result; }));
  EXPECT_FALSE(ok);
}

// A successful run's callback JSON is a serialized Rust `RoutineResult` using
// serde's default (verbatim) field names. Every field must land in its own
// mojom slot -- in particular `success` must be true, not the struct default.
TEST_F(MahoRoutinesPageHandlerTest, CompleteRoutineParsesResultFields) {
  maho_routines::mojom::RoutineRunResultPtr result =
      Complete(R"({"id":"morning_briefing","ran_at":1750000000,)"
               R"("content":"Here is your briefing.","success":true})");

  ASSERT_TRUE(result);
  EXPECT_EQ(result->id, "morning_briefing");
  EXPECT_EQ(result->ran_at, 1750000000);
  EXPECT_EQ(result->content, "Here is your briefing.");
  EXPECT_TRUE(result->success);
  // Regression guard: the whole JSON blob must never end up in `content`.
  EXPECT_EQ(result->content.find('{'), std::string::npos);
}

// A recorded failure arrives through the same completion path with
// success=false and the error text in `content`.
TEST_F(MahoRoutinesPageHandlerTest, CompleteRoutineParsesFailedRun) {
  maho_routines::mojom::RoutineRunResultPtr result =
      Complete(R"({"id":"close_old_tabs","ran_at":42,)"
               R"("content":"backend exploded","success":false})");

  ASSERT_TRUE(result);
  EXPECT_EQ(result->id, "close_old_tabs");
  EXPECT_EQ(result->ran_at, 42);
  EXPECT_EQ(result->content, "backend exploded");
  EXPECT_FALSE(result->success);
}

// Malformed FFI payloads must not crash and must not masquerade as success.
TEST_F(MahoRoutinesPageHandlerTest, CompleteRoutineHandlesMalformedJson) {
  for (const char* payload : {"", "not json at all", "[1,2,3]", "{",
                              R"({"id":123,"success":"yes"})"}) {
    maho_routines::mojom::RoutineRunResultPtr result = Complete(payload);
    ASSERT_TRUE(result) << payload;
    EXPECT_FALSE(result->success) << payload;
  }

  // A non-dict payload keeps the raw text so the failure is diagnosable.
  EXPECT_EQ(Complete("not json at all")->content, "not json at all");

  // A dict with wrong-typed fields degrades per field instead of wholesale.
  maho_routines::mojom::RoutineRunResultPtr typed =
      Complete(R"({"id":123,"success":"yes"})");
  EXPECT_EQ(typed->id, "");
  EXPECT_EQ(typed->ran_at, 0);
  EXPECT_EQ(typed->content, "");
  EXPECT_FALSE(typed->success);
}

// An empty routine inbox -- the FFI's null/empty answer -- must surface as an
// empty array, never an error and never a crash. (An end-to-end assertion
// through the handler instance is not possible here: a unit-test handler has no
// BrowserWindowInterface, so MahoPrivateContextToken::Revalidate() drops every
// instance call before it reaches this logic; see the fixture note above.)
TEST_F(MahoRoutinesPageHandlerTest, EmptyHistoryPayloadYieldsEmptyArray) {
  // `maho_routines_history` returns "[]" for a routine that has never run.
  EXPECT_TRUE(
      MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting("[]").empty());
  // A null FFI return is handled by the caller, which passes through the same
  // empty-array result rather than fabricating a row.
  EXPECT_TRUE(
      MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting("").empty());

  // A routine that has never run yields null from GetLatestRun, not a
  // default-constructed record the page would render as a phantom run.
  EXPECT_FALSE(MahoRoutinesPageHandler::ParseLatestRunJsonForTesting(""));
  EXPECT_FALSE(MahoRoutinesPageHandler::ParseLatestRunJsonForTesting("null"));
}

// Every camelCase field of the Rust `RoutineRunRecord` must land in its own
// distinctly named mojom slot, and backend ordering (newest first) must be
// preserved verbatim -- the handler must not reorder or renumber rows.
TEST_F(MahoRoutinesPageHandlerTest, RunHistoryJsonMapsEveryField) {
  const std::string payload = R"([
    {"resultId":31,"routineId":"morning_briefing","ranAt":3000,
     "success":true,"content":"newest","source":"manual"},
    {"resultId":22,"routineId":"close_old_tabs","ranAt":2000,
     "success":false,"content":"boom","source":"event"},
    {"resultId":13,"routineId":"morning_briefing","ranAt":1000,
     "success":true,"content":"oldest","source":"scheduled"}
  ])";

  std::vector<maho_routines::mojom::RoutineRunRecordPtr> records =
      MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting(payload);
  ASSERT_EQ(records.size(), 3u);

  // Backend order preserved.
  EXPECT_EQ(records[0]->ran_at, 3000);
  EXPECT_EQ(records[1]->ran_at, 2000);
  EXPECT_EQ(records[2]->ran_at, 1000);

  // Full field mapping on a successful row.
  EXPECT_EQ(records[0]->result_id, 31);
  EXPECT_EQ(records[0]->routine_id, "morning_briefing");
  EXPECT_EQ(records[0]->content, "newest");
  EXPECT_EQ(records[0]->source, "manual");
  EXPECT_TRUE(records[0]->success);

  // A failed row keeps success=false and carries the error text in content.
  EXPECT_EQ(records[1]->result_id, 22);
  EXPECT_EQ(records[1]->routine_id, "close_old_tabs");
  EXPECT_FALSE(records[1]->success);
  EXPECT_EQ(records[1]->content, "boom");
  EXPECT_EQ(records[1]->source, "event");

  // result_id is row identity, never the routine id: two rows for the same
  // routine must carry different result_ids.
  EXPECT_EQ(records[2]->routine_id, records[0]->routine_id);
  EXPECT_NE(records[2]->result_id, records[0]->result_id);
}

// int64 ids/timestamps beyond 32 bits survive the base::Value double round
// trip; they are exactly representable well past any realistic row count or
// unix timestamp.
TEST_F(MahoRoutinesPageHandlerTest, RunHistoryJsonKeepsLargeIntegers) {
  const std::string payload = R"([
    {"resultId":9007199254740991,"routineId":"r","ranAt":4102444800,
     "success":true,"content":"c","source":"scheduled"}
  ])";

  std::vector<maho_routines::mojom::RoutineRunRecordPtr> records =
      MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting(payload);
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0]->result_id, 9007199254740991);
  EXPECT_EQ(records[0]->ran_at, 4102444800);
}

// Malformed history payloads degrade to an empty list instead of crashing, and
// a single bad element never discards the good ones around it.
TEST_F(MahoRoutinesPageHandlerTest, RunHistoryJsonHandlesMalformedPayloads) {
  for (const char* payload :
       {"", "not json", "{", "null", "42", R"({"resultId":1})"}) {
    EXPECT_TRUE(
        MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting(payload).empty())
        << payload;
  }

  // Non-dict elements are skipped; the surrounding valid rows survive.
  std::vector<maho_routines::mojom::RoutineRunRecordPtr> mixed =
      MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting(
          R"([{"resultId":1,"routineId":"a","ranAt":10,"success":true,
               "content":"x","source":"manual"},
              "garbage", 7, null,
              {"resultId":2,"routineId":"b","ranAt":20,"success":false,
               "content":"y","source":"event"}])");
  ASSERT_EQ(mixed.size(), 2u);
  EXPECT_EQ(mixed[0]->routine_id, "a");
  EXPECT_EQ(mixed[1]->routine_id, "b");

  // Missing/wrong-typed fields degrade per field rather than dropping the row.
  std::vector<maho_routines::mojom::RoutineRunRecordPtr> partial =
      MahoRoutinesPageHandler::ParseRunHistoryJsonForTesting(
          R"([{"routineId":42,"ranAt":"soon","success":"yes"}])");
  ASSERT_EQ(partial.size(), 1u);
  EXPECT_EQ(partial[0]->result_id, 0);
  EXPECT_EQ(partial[0]->routine_id, "");
  EXPECT_EQ(partial[0]->ran_at, 0);
  EXPECT_EQ(partial[0]->content, "");
  EXPECT_EQ(partial[0]->source, "");
  EXPECT_FALSE(partial[0]->success);
}

// The latest-run payload is a bare object (not an array) and maps through the
// same field contract; malformed input yields null rather than a phantom row.
TEST_F(MahoRoutinesPageHandlerTest, RunStatusJsonMapsAndRejectsMalformedRows) {
  auto status = MahoRoutinesPageHandler::ParseRunStatusJsonForTesting(
      R"({"runId":"run-1","routineId":"morning_briefing","source":"manual",)"
      R"("state":"running","revision":2,"startedAt":100,"updatedAt":101,)"
      R"("finishedAt":null,"result":null,"error":null,"approval":null})");
  ASSERT_TRUE(status);
  EXPECT_EQ(status->run_id, "run-1");
  EXPECT_EQ(status->routine_id, "morning_briefing");
  EXPECT_EQ(status->source, "manual");
  EXPECT_EQ(status->state, maho_routines::mojom::RoutineRunState::kRunning);
  EXPECT_EQ(status->revision, 2u);
  EXPECT_EQ(status->started_at, 100);
  EXPECT_EQ(status->updated_at, 101);
  EXPECT_FALSE(status->finished_at.has_value());

  for (
      const char* payload : {
          R"({"runId":"r","routineId":"x","source":"manual","state":"unknown","revision":1,"startedAt":1,"updatedAt":1})",
          R"({"runId":"r","routineId":"x","source":"manual","state":"queued","revision":0,"startedAt":1,"updatedAt":1})",
          R"({"runId":"r","routineId":"x","source":"manual","state":"queued","revision":1.5,"startedAt":1,"updatedAt":1})",
          R"({"runId":"r","routineId":"x","source":"other","state":"queued","revision":1,"startedAt":1,"updatedAt":1})",
          R"({"runId":"r","routineId":"x","source":"manual","state":"queued","revision":1,"updatedAt":1})",
      }) {
    EXPECT_FALSE(MahoRoutinesPageHandler::ParseRunStatusJsonForTesting(payload))
        << payload;
  }
}

TEST_F(MahoRoutinesPageHandlerTest,
       AwaitingApprovalStatusMapsOnlySanitizedFields) {
  constexpr char kRawSentinel[] = "RAW-ROUTINE-SECRET-9F4C";
  const std::string payload =
      R"({"runId":"run-approval","routineId":"close_old_tabs","source":"manual",)"
      R"("state":"awaiting_approval","revision":3,"startedAt":100,"updatedAt":101,)"
      R"("approval":{"approvalId":"approval-1","toolName":"browser_type",)"
      R"("sensitivity":"sensitive"}})";
  ASSERT_EQ(payload.find(kRawSentinel), std::string::npos);
  ASSERT_EQ(payload.find("arguments"), std::string::npos);

  auto status = MahoRoutinesPageHandler::ParseRunStatusJsonForTesting(payload);
  ASSERT_TRUE(status);
  EXPECT_EQ(status->state,
            maho_routines::mojom::RoutineRunState::kAwaitingApproval);
  ASSERT_TRUE(status->approval);
  EXPECT_EQ(status->approval->approval_id, "approval-1");
  EXPECT_EQ(status->approval->tool_name, "browser_type");
  EXPECT_EQ(status->approval->sensitivity, "sensitive");
}

TEST_F(MahoRoutinesPageHandlerTest,
       AwaitingApprovalWithoutApprovalIdIsRejected) {
  auto status = MahoRoutinesPageHandler::ParseRunStatusJsonForTesting(
      R"({"runId":"run-approval","routineId":"close_old_tabs","source":"manual",)"
      R"("state":"awaiting_approval","revision":3,"startedAt":100,"updatedAt":101,)"
      R"("approval":{"toolName":"browser_type","sensitivity":"sensitive"}})");
  EXPECT_FALSE(status);
}

TEST_F(MahoRoutinesPageHandlerTest, RunStatusPushReachesBoundPage) {
  TestRoutinesPage page;
  auto handler = MakeHandler(page.TakeRemote());
  handler->OnRoutineRunStatusJson(
      R"({"runId":"run-push","routineId":"close_old_tabs","source":"event",)"
      R"("state":"failed","revision":3,"startedAt":100,"updatedAt":102,)"
      R"("finishedAt":102,"result":null,"error":"boom","approval":null})");
  task_environment()->RunUntilIdle();

  ASSERT_TRUE(page.last_status);
  EXPECT_EQ(page.last_status->run_id, "run-push");
  EXPECT_EQ(page.last_status->state,
            maho_routines::mojom::RoutineRunState::kFailed);
  EXPECT_EQ(page.last_status->error, std::optional<std::string>("boom"));
}

TEST_F(MahoRoutinesPageHandlerTest,
       StatusCallbackUnregistersFromRegistrationCoreAfterGlobalSwap) {
  TestRoutinesPage page;
  auto handler = MakeHandler(page.TakeRemote());
  char* baseline_status_json =
      maho_routines_start(core_, "morning_briefing", 2, "manual");
  ASSERT_NE(baseline_status_json, nullptr);
  maho_string_free(baseline_status_json);
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(page.last_status);
  page.last_status.reset();
  MahoCore* registration_core = core_;

  scoped_core_.reset();
  handler.reset();

  char* status_json =
      maho_routines_start(registration_core, "morning_briefing", 2, "manual");
  ASSERT_NE(status_json, nullptr);
  maho_string_free(status_json);
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(page.last_status);
}

TEST_F(MahoRoutinesPageHandlerTest, LatestRunJsonMapsObjectPayload) {
  maho_routines::mojom::RoutineRunRecordPtr record =
      MahoRoutinesPageHandler::ParseLatestRunJsonForTesting(
          R"({"resultId":7,"routineId":"morning_briefing","ranAt":1750000000,
              "success":false,"content":"agent error","source":"scheduled"})");
  ASSERT_TRUE(record);
  EXPECT_EQ(record->result_id, 7);
  EXPECT_EQ(record->routine_id, "morning_briefing");
  EXPECT_EQ(record->ran_at, 1750000000);
  EXPECT_FALSE(record->success);
  EXPECT_EQ(record->content, "agent error");
  EXPECT_EQ(record->source, "scheduled");

  for (const char* payload : {"", "not json", "[]", "null", "3"}) {
    EXPECT_FALSE(MahoRoutinesPageHandler::ParseLatestRunJsonForTesting(payload))
        << payload;
  }
}

TEST_F(MahoRoutinesPageHandlerTest, TraceRecorderClientRecordsEventsAndExportsJson) {
  auto* client = maho::MahoTraceRecorderClient::GetInstance();
  ASSERT_TRUE(client);

  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_TRUE(client->Start(contents, 101));
  EXPECT_TRUE(client->is_recording());
  EXPECT_EQ(client->tab_id(), 101);

  client->RecordClick(std::string("@e1"), 120.0, 240.0);
  client->RecordFill("test query", std::string("#search"));
  client->RecordNavigate("https://example.org/search");

  std::string json = client->StopAndExportJson();
  EXPECT_FALSE(client->is_recording());
  EXPECT_EQ(client->tab_id(), 0);

  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->FindInt("version"), 3);
  const base::ListValue* steps = parsed->FindList("steps");
  ASSERT_TRUE(steps);
  EXPECT_GE(steps->size(), 3u);
}

TEST_F(MahoRoutinesPageHandlerTest, StartAndStopTraceRecordingViaHandler) {
  TestRoutinesPage page;
  auto handler = MakeHandler(page.TakeRemote());

  base::test::TestFuture<bool> start_future;
  handler->StartTraceRecording(0, start_future.GetCallback());
  EXPECT_TRUE(start_future.Get());

  base::test::TestFuture<bool, int64_t> status_future;
  handler->IsTraceRecording(status_future.GetCallback());
  auto [is_rec, active_tab] = status_future.Take();
  EXPECT_TRUE(is_rec);

  base::test::TestFuture<const std::optional<std::string>&,
                         const std::optional<std::string>&>
      stop_future;
  handler->StopTraceRecording(stop_future.GetCallback());
  auto [trace_json, error] = stop_future.Take();
  EXPECT_TRUE(trace_json.has_value());
  EXPECT_FALSE(error.has_value());
}

TEST_F(MahoRoutinesPageHandlerTest, CreateRoutineFromTraceCreatesCustomRoutine) {
  TestRoutinesPage page;
  auto handler = MakeHandler(page.TakeRemote());

  const std::string sample_trace =
      R"({"version":3,"steps":[{"at_ms":0,"step":{"kind":"navigate","url":"https://example.com"}}]})";

  base::test::TestFuture<bool> create_future;
  handler->CreateRoutineFromTrace(
      "Recorded Automation", "Automatically open example", sample_trace,
      "0 12 * * *", std::nullopt, create_future.GetCallback());
  EXPECT_TRUE(create_future.Get());

  auto routines = ListAll(handler.get());
  auto it = std::find_if(
      routines.begin(), routines.end(),
      [](const auto& r) { return r->name == "Recorded Automation"; });
  ASSERT_NE(it, routines.end());
  EXPECT_TRUE((*it)->is_custom);
  EXPECT_EQ((*it)->schedule, "0 12 * * *");
}

}  // namespace
