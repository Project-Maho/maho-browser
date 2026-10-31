// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_

#include <memory>

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class BrowserWindowInterface;
struct MahoCore;
struct MahoRoutineStatusContext;

#include "maho/browser/maho_private_context_policy.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"

class MahoRoutinesPageHandler : public maho_routines::mojom::PageHandler {
 public:
  MahoRoutinesPageHandler(
      mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_routines::mojom::Page> page,
      BrowserWindowInterface* browser_window,
      content::WebContents* web_contents);
  MahoRoutinesPageHandler(const MahoRoutinesPageHandler&) = delete;
  MahoRoutinesPageHandler& operator=(const MahoRoutinesPageHandler&) = delete;
  ~MahoRoutinesPageHandler() override;

  void ListRoutines(ListRoutinesCallback callback) override;
  void ListAllRoutines(ListAllRoutinesCallback callback) override;
  void CreateRoutine(const std::string& name,
                     const std::string& prompt,
                     const std::optional<std::string>& schedule,
                     const std::optional<std::string>& trigger,
                     CreateRoutineCallback callback) override;
  void DeleteRoutine(const std::string& id,
                     DeleteRoutineCallback callback) override;
  void RunRoutine(const std::string& id, RunRoutineCallback callback) override;
  void StartRoutine(const std::string& id, StartRoutineCallback callback) override;
  void GetRoutineRunStatuses(GetRoutineRunStatusesCallback callback) override;
  void RespondToRoutineApproval(
      const std::string& run_id,
      const std::string& approval_id,
      bool approved,
      RespondToRoutineApprovalCallback callback) override;
  void GetUserTier(GetUserTierCallback callback) override;
  void ListRunHistory(const std::optional<std::string>& routine_id,
                      uint32_t limit,
                      ListRunHistoryCallback callback) override;
  void GetLatestRun(const std::string& routine_id,
                    GetLatestRunCallback callback) override;

  void StartTraceRecording(int64_t tab_id,
                           StartTraceRecordingCallback callback) override;
  void StopTraceRecording(StopTraceRecordingCallback callback) override;
  void CreateRoutineFromTrace(const std::string& name,
                              const std::string& description,
                              const std::string& trace_json,
                              const std::optional<std::string>& schedule,
                              const std::optional<std::string>& trigger,
                              CreateRoutineFromTraceCallback callback) override;
  void IsTraceRecording(IsTraceRecordingCallback callback) override;

  void CompleteRoutine(RunRoutineCallback callback, std::string json);
  void FailRoutine(RunRoutineCallback callback, std::string error);

  // Parses a `maho_routines_history` / `maho_routines_latest` payload into
  // mojom records. Exposed for tests because the FFI offers no way to seed the
  // durable routine inbox from C++ (recording a run requires a Max-tier
  // account and a live agent backend), so the JSON->mojom mapping is verified
  // against literal backend payloads instead.
  static std::vector<maho_routines::mojom::RoutineRunRecordPtr>
  ParseRunHistoryJsonForTesting(const std::string& json);
  static maho_routines::mojom::RoutineRunRecordPtr ParseLatestRunJsonForTesting(
      const std::string& json);
  // Parses a `maho_routines_run` completion payload. Exposed for the same
  // reason: a unit-test-constructed handler has no BrowserWindowInterface, so
  // MahoPrivateContextToken::Revalidate() rejects every instance method before
  // it reaches the parsing logic.
  static maho_routines::mojom::RoutineRunResultPtr
  ParseRunResultJsonForTesting(const std::string& json);
  static maho_routines::mojom::RoutineRunStatusPtr
  ParseRunStatusJsonForTesting(const std::string& json);
  static std::vector<maho_routines::mojom::RoutineRunStatusPtr>
  ParseRunStatusesJsonForTesting(const std::string& json);
  void OnRoutineRunStatusJson(std::string json);
  bool HasStatusSubscriptionForTesting() const {
    return status_callback_token_ != 0;
  }

 private:
  raw_ptr<MahoCore> status_callback_core_ = nullptr;
  uint64_t status_callback_token_ = 0;
  std::unique_ptr<MahoRoutineStatusContext> status_callback_context_;
  mojo::Receiver<maho_routines::mojom::PageHandler> receiver_;
  mojo::Remote<maho_routines::mojom::Page> page_;
  MahoPrivateContextToken context_token_;
  raw_ptr<BrowserWindowInterface> browser_window_ = nullptr;
  base::WeakPtr<content::WebContents> host_web_contents_;

  SEQUENCE_CHECKER(sequence_checker_);
  scoped_refptr<base::SequencedTaskRunner> owning_task_runner_;
  base::WeakPtrFactory<MahoRoutinesPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_
