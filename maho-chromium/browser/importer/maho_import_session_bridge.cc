// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_import_session_bridge.h"

#include <string>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/webdata_services/web_data_service_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/favicon/core/favicon_service.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

namespace maho {

MahoImportSessionBridge::MahoImportSessionBridge(
    base::WeakPtr<MahoWelcomePageHandler> handler,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    Profile* profile)
    : handler_(std::move(handler)),
      ui_task_runner_(std::move(ui_task_runner)),
      profile_(profile) {}

MahoImportSessionBridge::MahoImportSessionBridge(
    ProgressCallback progress_callback,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    Profile* profile)
    : progress_callback_(std::move(progress_callback)),
      ui_task_runner_(std::move(ui_task_runner)),
      profile_(profile) {}

MahoImportSessionBridge::~MahoImportSessionBridge() {
  if (session_) {
    maho_import_orchestrator_cancel(session_);
    ReleaseSession();
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
      &MahoImportSessionBridge::OnProgress,
      &MahoImportSessionBridge::OnCookieFromRust,
      &MahoImportSessionBridge::OnAutofillFromRust,
      &MahoImportSessionBridge::OnFaviconFromRust,
      this);

  if (!session_) {
    LOG(ERROR) << "MahoImportSessionBridge: orchestrator_start returned null";
    return false;
  }
  return true;
}

void MahoImportSessionBridge::Cancel() {
  if (session_ && terminal_state_ == ImportTerminalState::kRunning) {
    terminal_state_ = ImportTerminalState::kCancelled;
    maho_import_orchestrator_cancel(session_);
  }
}

void MahoImportSessionBridge::SetHydrationCallbackForTesting(
    base::RepeatingClosure callback) {
  hydration_callback_for_testing_ = std::move(callback);
}

void MahoImportSessionBridge::SimulateProgressForTesting(
    uint32_t kind,
    uint32_t import_type,
    int32_t count,
    std::string message) {
  HandleProgressOnUI(kind, import_type, count, std::move(message));
}

void MahoImportSessionBridge::SimulateCancelledForTesting() {
  terminal_state_ = ImportTerminalState::kCancelled;
}

void MahoImportSessionBridge::HandleProgressOnUI(uint32_t kind,
                                                 uint32_t import_type,
                                                 int32_t count,
                                                 std::string message) {
  for (ImportProgressAction action :
       AdvanceImportProgress(kind, &terminal_state_)) {
    switch (action) {
      case ImportProgressAction::kReleaseSession:
        ReleaseSession();
        break;
      case ImportProgressAction::kHydrateSpaces:
        HydrateAfterSuccessfulImport();
        break;
      case ImportProgressAction::kForwardProgress:
        ForwardProgress(kind, import_type, count, message);
        break;
    }
  }
}

void MahoImportSessionBridge::ReleaseSession() {
  if (!session_) {
    return;
  }
  maho_import_orchestrator_free(session_);
  session_ = nullptr;
}

void MahoImportSessionBridge::HydrateAfterSuccessfulImport() {
  DCHECK(ui_task_runner_->RunsTasksInCurrentSequence());
  if (hydration_callback_for_testing_) {
    hydration_callback_for_testing_.Run();
    return;
  }
  MahoSpaceProfileBridge::GetInstance()->HydrateFromCore();
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
                     self->weak_factory_.GetWeakPtr(), kind, import_type, count,
                     std::move(msg_copy)));
}

// static — called from Rust background thread.
void MahoImportSessionBridge::OnCookieFromRust(
    const char* host, const char* name, const char* value, const char* path,
    int64_t expires, bool is_secure, bool is_httponly, int32_t same_site,
    void* user_data) {
  auto* self = static_cast<MahoImportSessionBridge*>(user_data);
  std::string host_s(host ? host : "");
  std::string name_s(name ? name : "");
  std::string value_s(value ? value : "");
  std::string path_s(path ? path : "");

  self->ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoImportSessionBridge::HandleCookie,
                     self->weak_factory_.GetWeakPtr(), std::move(host_s),
                     std::move(name_s), std::move(value_s), std::move(path_s),
                     expires, is_secure, is_httponly, same_site));
}

// static — called from Rust background thread.
void MahoImportSessionBridge::OnAutofillFromRust(
    const char* field_name, const char* value, int32_t times_used,
    int64_t first_used, int64_t last_used, void* user_data) {
  auto* self = static_cast<MahoImportSessionBridge*>(user_data);
  std::u16string name_s = base::UTF8ToUTF16(field_name ? field_name : "");
  std::u16string value_s = base::UTF8ToUTF16(value ? value : "");

  self->ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoImportSessionBridge::HandleAutofill,
                     self->weak_factory_.GetWeakPtr(), std::move(name_s),
                     std::move(value_s), times_used, first_used, last_used));
}

// static — called from Rust background thread.
void MahoImportSessionBridge::OnFaviconFromRust(
    const char* url, const uint8_t* png_bytes, size_t png_len, void* user_data) {
  auto* self = static_cast<MahoImportSessionBridge*>(user_data);
  std::string url_s(url ? url : "");
  std::vector<uint8_t> png_copy;
  if (png_bytes && png_len > 0) {
    auto span = UNSAFE_BUFFERS(base::span<const uint8_t>(png_bytes, png_len));
    png_copy.assign(span.begin(), span.end());
  }

  self->ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoImportSessionBridge::HandleFavicon,
                     self->weak_factory_.GetWeakPtr(), std::move(url_s),
                     std::move(png_copy)));
}

void MahoImportSessionBridge::ForwardProgress(uint32_t kind,
                                              uint32_t import_type,
                                              int32_t count,
                                              const std::string& message) {
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

void MahoImportSessionBridge::HandleCookie(
    std::string host, std::string name, std::string value, std::string path,
    int64_t expires, bool is_secure, bool is_httponly, int32_t same_site) {
  if (!profile_) return;

  std::string check_host = host;
  if (check_host.rfind(".", 0) == 0) {
    check_host = check_host.substr(1);
  }
  GURL source_url("https://" + check_host);

  auto cookie = net::CanonicalCookie::CreateSanitizedCookie(
      source_url, name, value, host, path,
      /*creation*/ base::Time::Now(),
      /*expiration*/ expires > 0 ? base::Time::FromTimeT(expires) : base::Time(),
      /*last_access*/ base::Time::Now(),
      is_secure, is_httponly,
      static_cast<net::CookieSameSite>(same_site),
      net::COOKIE_PRIORITY_DEFAULT,
      /*partition_key*/ std::nullopt,
      /*status*/ nullptr);
  if (!cookie) return;

  auto* cm = profile_->GetDefaultStoragePartition()
                 ->GetCookieManagerForBrowserProcess();
  cm->SetCanonicalCookie(*cookie, source_url,
                         net::CookieOptions::MakeAllInclusive(),
                         base::DoNothing());
}

void MahoImportSessionBridge::HandleAutofill(
    std::u16string field_name, std::u16string value, int32_t times_used,
    int64_t first_used, int64_t last_used) {
  if (!profile_) return;

  scoped_refptr<autofill::AutofillWebDataService> svc =
      WebDataServiceFactory::GetAutofillWebDataForProfile(
          profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!svc) return;

  autofill::FormFieldData f;
  f.set_name(field_name);
  f.set_value(value);
  std::vector<autofill::FormFieldData> fields{f};
  svc->AddFormFields(fields);
}

void MahoImportSessionBridge::HandleFavicon(
    std::string url, std::vector<uint8_t> png_bytes) {
  if (!profile_ || png_bytes.empty()) return;
  GURL gurl(url);
  if (!gurl.is_valid()) return;

  SkBitmap bitmap = gfx::PNGCodec::Decode(png_bytes);
  if (bitmap.isNull()) {
    return;
  }
  gfx::Image image = gfx::Image::CreateFrom1xBitmap(bitmap);

  favicon::FaviconService* svc = FaviconServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!svc) return;

  svc->SetFavicons({gurl}, gurl, favicon_base::IconType::kFavicon, image);
}

}  // namespace maho
