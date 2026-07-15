// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_service.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace maho {

namespace {

// Deterministic error returned when no helper can serve a read (never
// launched, disconnected, or given up after a crash loop).
constexpr char kMahoMailHelperUnavailable[] = "mail helper unavailable";

// Adapts a Mojo (bool, const std::string&) reply into the service's
// (bool, std::string) ReadCallback.
void ForwardReadReply(MahoMailService::ReadCallback callback,
                      bool ok,
                      const std::string& result_json) {
  std::move(callback).Run(ok, result_json);
}

// Wraps the outgoing Mojo reply so that if the helper drops the reply callback
// (disconnect, remote reset, or process shutdown) the caller still resolves
// exactly once with the deterministic unavailable error instead of never being
// invoked. Normal success/error replies forward unchanged through
// ForwardReadReply.
base::OnceCallback<void(bool, const std::string&)> WrapReadReply(
    MahoMailService::ReadCallback callback) {
  return mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(&ForwardReadReply, std::move(callback)),
      /*ok=*/false, std::string(kMahoMailHelperUnavailable));
}

}  // namespace

MahoMailService::MahoMailService() = default;

MahoMailService::~MahoMailService() = default;

void MahoMailService::EnsureHelperLaunched(
    const std::string& expected_version,
    const std::string& profile_path,
    const base::FilePath& crashpad_database) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!launcher_) {
    launcher_ = std::make_unique<MahoMailHelperLauncher>();
  }
  launcher_->Launch(expected_version, profile_path, crashpad_database);
}

void MahoMailService::InjectDatabaseKeys(const std::string& sqlcipher_key,
                                         const std::string& credential_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!launcher_) {
    return;
  }
  launcher_->InjectDatabaseKeys(sqlcipher_key, credential_key);
}

void MahoMailService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (launcher_) {
    launcher_->Shutdown();
    launcher_.reset();
  }
}

void MahoMailService::SetLauncherForTesting(
    std::unique_ptr<MahoMailHelperLauncher> launcher) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  launcher_ = std::move(launcher);
}

mojom::MahoMailHelper* MahoMailService::GetHelperOrNull() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!launcher_ || launcher_->has_given_up()) {
    return nullptr;
  }
  return launcher_->GetHelper();
}

void MahoMailService::ListAccounts(ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListAccounts(WrapReadReply(std::move(callback)));
}

void MahoMailService::ListFolders(const std::string& account_id,
                                  ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListFolders(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListEmails(const std::string& account_id,
                                 const std::string& folder_id,
                                 int64_t limit,
                                 int64_t offset,
                                 ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListEmails(account_id, folder_id, limit, offset,
                     WrapReadReply(std::move(callback)));
}

void MahoMailService::GetEmail(const std::string& email_id,
                               ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetEmail(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SearchEmails(const std::string& query_json,
                                   ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SearchEmails(query_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListThread(const std::string& account_id,
                                 const std::string& message_id,
                                 ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListThread(account_id, message_id,
                     WrapReadReply(std::move(callback)));
}

}  // namespace maho
