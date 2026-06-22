// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_import_session_bridge.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"

namespace maho {

MahoImportSessionBridge::MahoImportSessionBridge(
    base::WeakPtr<MahoWelcomePageHandler> handler,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner)
    : handler_(std::move(handler)),
      ui_task_runner_(std::move(ui_task_runner)) {}

MahoImportSessionBridge::MahoImportSessionBridge(
    ProgressCallback progress_callback,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner)
    : progress_callback_(std::move(progress_callback)),
      ui_task_runner_(std::move(ui_task_runner)) {}

MahoImportSessionBridge::~MahoImportSessionBridge() {
  if (session_) {
    // If destroyed while still active (e.g. page handler torn down),
    // cancel and free the session.  maho_import_orchestrator_free blocks
    // until the background thread finishes, which is acceptable during
    // page handler destruction.
    maho_import_orchestrator_cancel(session_);
    maho_import_orchestrator_free(session_);
    session_ = nullptr;
  }
}

bool MahoImportSessionBridge::Start(MahoCore* core,
                                    const std::string& browser_json,
                                    uint32_t items_bitmask,
                                    const std::string& essentials_json) {
  DCHECK(!session_) << "Import session already active";

  const char* essentials_ptr =
      essentials_json.empty() ? nullptr : essentials_json.c_str();

  session_ = maho_import_orchestrator_start(
      core, browser_json.c_str(), items_bitmask, essentials_ptr,
      &MahoImportSessionBridge::OnProgress, this);

  if (!session_) {
    LOG(ERROR) << "MahoImportSessionBridge: orchestrator_start returned null";
    return false;
  }
  return true;
}

void MahoImportSessionBridge::Cancel() {
  if (session_) {
    maho_import_orchestrator_cancel(session_);
  }
}

// static — called from Rust background thread.
void MahoImportSessionBridge::OnProgress(uint32_t kind,
                                         uint32_t import_type,
                                         int32_t count,
                                         const char* message,
                                         void* user_data) {
  auto* self = static_cast<MahoImportSessionBridge*>(user_data);
  std::string msg_copy = message ? message : "";

  self->ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoImportSessionBridge::HandleProgressOnUI,
                     base::Unretained(self), kind, import_type, count,
                     std::move(msg_copy)));
}

void MahoImportSessionBridge::HandleProgressOnUI(uint32_t kind,
                                                   uint32_t import_type,
                                                   int32_t count,
                                                   std::string message) {
  if (terminated_) return;

  const bool is_terminal = (kind == 3 || kind == 4);

  if (is_terminal) {
    terminated_ = true;
    if (session_) {
      maho_import_orchestrator_free(session_);
      session_ = nullptr;
    }
  }

  if (progress_callback_) {
    // Generic callback mode.
    switch (kind) {
      case 0:  // Starting
        progress_callback_.Run(import_type, 0, "", false);
        break;
      case 1:  // Update
        progress_callback_.Run(import_type, count, "", false);
        break;
      case 2:  // TypeComplete
        progress_callback_.Run(import_type, count, message, true);
        break;
      case 3:  // AllComplete
        progress_callback_.Run(0, count, "", true);
        break;
      case 4:  // Error
        progress_callback_.Run(import_type, count, message, true);
        break;
    }
  } else if (handler_) {
    // Legacy MahoWelcomePageHandler mode.
    switch (kind) {
      case 0:  // Starting
        handler_->NotifyImportProgress(import_type, 0, "", false);
        break;
      case 1:  // Update
        handler_->NotifyImportProgress(import_type, count, "", false);
        break;
      case 2:  // TypeComplete
        handler_->NotifyImportProgress(import_type, count, "", false);
        break;
      case 3:  // AllComplete
        handler_->NotifyImportProgress(import_type, count, "", true);
        break;
      case 4:  // Error
        handler_->NotifyImportProgress(import_type, count, message, true);
        break;
    }
  }
}

}  // namespace maho
