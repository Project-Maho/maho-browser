// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_MAHO_IMPORT_SESSION_BRIDGE_H_
#define MAHO_BROWSER_IMPORTER_MAHO_IMPORT_SESSION_BRIDGE_H_

#include <cstdint>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "maho/browser/importer/maho_import_session_completion.h"
#include "maho/third_party/maho/maho_ffi.h"

class Profile;
class MahoWelcomePageHandler;

namespace maho {

// Bridges asynchronous Rust import orchestrator callbacks (fired from a
// background thread) to the UI-thread consumer.
//
// Two modes:
// 1. Legacy: WeakPtr<MahoWelcomePageHandler> — calls NotifyImportProgress.
// 2. Generic: A RepeatingCallback receiving (type, items_imported, error,
//    complete) — suitable for any consumer (e.g. MahoMigrationDialogView).
//
// Lifetime: Created on the UI thread and owned by the consumer via
// unique_ptr.  If destroyed while an import is in flight, the destructor
// cancels + frees the Rust session synchronously.
//
// Thread safety: The static |OnProgress| callback is invoked on a Rust
// background thread.  It posts all work to the captured UI task runner.
class MahoImportSessionBridge {
 public:
  // Progress callback signature: (type, items_imported, error, complete).
  using ProgressCallback = base::RepeatingCallback<
      void(uint32_t, int32_t, const std::string&, bool)>;

  // Legacy constructor for MahoWelcomePageHandler.
  MahoImportSessionBridge(
      base::WeakPtr<MahoWelcomePageHandler> handler,
      scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
      Profile* profile);

  // Generic constructor — |progress_callback| is invoked on |ui_task_runner|.
  MahoImportSessionBridge(
      ProgressCallback progress_callback,
      scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
      Profile* profile);

  ~MahoImportSessionBridge();

  MahoImportSessionBridge(const MahoImportSessionBridge&) = delete;
  MahoImportSessionBridge& operator=(const MahoImportSessionBridge&) = delete;

  // Starts the import via the Rust FFI orchestrator.  Returns true on success.
  // |core| must remain valid until the terminal callback fires.
  bool Start(MahoCore* core,
             const std::string& browser_json,
             uint32_t items_bitmask,
             const std::string& essentials_json);

  // Requests cancellation of the in-flight import.
  void Cancel();

  // Returns true if the session has been started and has not yet terminated.
  bool is_active() const {
    return session_ != nullptr &&
           terminal_state_ == ImportTerminalState::kRunning;
  }

  void SetHydrationCallbackForTesting(base::RepeatingClosure callback);
  void SimulateProgressForTesting(uint32_t kind,
                                  uint32_t import_type,
                                  int32_t count,
                                  std::string message);
  void SimulateCancelledForTesting();

 private:
  // Static C-linkage callback passed to Rust.  |user_data| is |this|.
  static void OnProgress(uint32_t kind,
                         uint32_t import_type,
                         int32_t count,
                         const char* message,
                         void* user_data);

  static void OnCookieFromRust(const char* host,
                               const char* name,
                               const char* value,
                               const char* path,
                               int64_t expires,
                               bool is_secure,
                               bool is_httponly,
                               int32_t same_site,
                               void* user_data);

  static void OnAutofillFromRust(const char* field_name,
                                 const char* value,
                                 int32_t times_used,
                                 int64_t first_used,
                                 int64_t last_used,
                                 void* user_data);

  static void OnFaviconFromRust(const char* url,
                                const uint8_t* png_bytes,
                                size_t png_len,
                                void* user_data);

  // UI-thread handler for progress events.
  void HandleProgressOnUI(uint32_t kind,
                          uint32_t import_type,
                          int32_t count,
                          std::string message);
  void ForwardProgress(uint32_t kind,
                       uint32_t import_type,
                       int32_t count,
                       const std::string& message);
  void ReleaseSession();
  void HydrateAfterSuccessfulImport();

  void HandleCookie(std::string host,
                    std::string name,
                    std::string value,
                    std::string path,
                    int64_t expires,
                    bool is_secure,
                    bool is_httponly,
                    int32_t same_site);

  void HandleAutofill(std::u16string field_name,
                      std::u16string value,
                      int32_t times_used,
                      int64_t first_used,
                      int64_t last_used);

  void HandleFavicon(std::string url,
                     std::vector<uint8_t> png_bytes);

  // Legacy handler (mode 1). Null when using generic callback.
  base::WeakPtr<MahoWelcomePageHandler> handler_;
  // Generic progress callback (mode 2). Null when using legacy handler.
  ProgressCallback progress_callback_;
  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  raw_ptr<Profile> profile_ = nullptr;
  RAW_PTR_EXCLUSION MahoImportSession* session_ = nullptr;
  ImportTerminalState terminal_state_ = ImportTerminalState::kRunning;
  base::RepeatingClosure hydration_callback_for_testing_;
  base::WeakPtrFactory<MahoImportSessionBridge> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_MAHO_IMPORT_SESSION_BRIDGE_H_
