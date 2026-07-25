// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_STORE_BACKEND_H_
#define MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_STORE_BACKEND_H_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/password_manager/core/browser/password_store/password_store_backend.h"

namespace maho {
namespace passwords {

struct MahoPasswordForm {
  std::string id;
  std::string signon_realm;
  std::string username_value;
  std::string password_value;
  base::Time date_created;
  base::Time date_last_used;
  uint64_t revision = 0;

  MahoPasswordForm() = default;
  ~MahoPasswordForm() {
    Zeroize();
  }

  MahoPasswordForm(const MahoPasswordForm&) = default;
  MahoPasswordForm& operator=(const MahoPasswordForm&) = default;

  MahoPasswordForm(MahoPasswordForm&& other) noexcept
      : id(std::move(other.id)),
        signon_realm(std::move(other.signon_realm)),
        username_value(std::move(other.username_value)),
        password_value(std::move(other.password_value)),
        date_created(other.date_created),
        date_last_used(other.date_last_used),
        revision(other.revision) {}

  MahoPasswordForm& operator=(MahoPasswordForm&& other) noexcept {
    if (this != &other) {
      Zeroize();
      id = std::move(other.id);
      signon_realm = std::move(other.signon_realm);
      username_value = std::move(other.username_value);
      password_value = std::move(other.password_value);
      date_created = other.date_created;
      date_last_used = other.date_last_used;
      revision = other.revision;
    }
    return *this;
  }

  void Zeroize() {
    if (!password_value.empty()) {
      std::fill_n(static_cast<volatile char*>(&password_value[0]), password_value.size(), 0);
      password_value.clear();
    }
  }
};

// MahoPasswordStoreBackend implements Chromium's PasswordStoreBackend contract
// adapting it to Maho's typed, persistence-aware Vault core via FFI.
class MahoPasswordStoreBackend : public password_manager::PasswordStoreBackend {
 public:
  using LoginsOrErrorReply = base::OnceCallback<void(std::vector<MahoPasswordForm>, bool)>;
  using PasswordChangesOrErrorReply = base::OnceCallback<void(bool)>;

  explicit MahoPasswordStoreBackend(bool enabled = true);
  ~MahoPasswordStoreBackend() override;

  MahoPasswordStoreBackend(const MahoPasswordStoreBackend&) = delete;
  MahoPasswordStoreBackend& operator=(const MahoPasswordStoreBackend&) = delete;

  // password_manager::PasswordStoreBackend virtual interface implementation:
  void InitBackend(password_manager::AffiliatedMatchHelper* affiliated_match_helper,
                   RemoteChangesReceived remote_form_changes_received,
                   base::RepeatingClosure sync_enabled_or_disabled_cb,
                   base::OnceCallback<void(bool)> completion) override;
  void Shutdown(base::OnceClosure shutdown_completed) override;
  password_manager::ActionableError GetError() override;
  void GetAllLoginsAsync(password_manager::BackendLoginsOrErrorReply callback) override;
  void GetAllLoginsWithAffiliationAndBrandingAsync(
      password_manager::BackendLoginsOrErrorReply callback) override;
  void GetAutofillableLoginsAsync(
      password_manager::BackendLoginsOrErrorReply callback) override;
  void FillMatchingLoginsAsync(
      password_manager::BackendLoginsOrErrorReply callback,
      bool include_psl,
      const std::vector<password_manager::PasswordFormDigest>& forms) override;
  void GetGroupedMatchingLoginsAsync(
      const password_manager::PasswordFormDigest& form_digest,
      password_manager::BackendLoginsOrErrorReply callback) override;
  void AddLoginAsync(password_manager::StoredCredential cred,
                     password_manager::PasswordChangesOrErrorReply callback) override;
  void UpdateLoginAsync(password_manager::StoredCredential cred,
                        password_manager::PasswordChangesOrErrorReply callback) override;
  void RemoveLoginAsync(const base::Location& location,
                        password_manager::StoredCredential cred,
                        password_manager::PasswordChangesOrErrorReply callback) override;
  void RemoveLoginsCreatedBetweenAsync(
      const base::Location& location,
      base::Time delete_begin,
      base::Time delete_end,
      base::OnceCallback<void(bool)> sync_completion,
      password_manager::PasswordChangesOrErrorReply callback) override;
  void DisableAutoSignInForOriginsAsync(
      const base::RepeatingCallback<bool(const GURL&)>& origin_filter,
      base::OnceClosure completion) override;
  password_manager::SmartBubbleStatsStore* GetSmartBubbleStatsStore() override;
  std::unique_ptr<syncer::DataTypeControllerDelegate> CreateSyncControllerDelegate() override;
  void OnSyncServiceInitialized(syncer::SyncService* sync_service) override;
  base::WeakPtr<password_manager::PasswordStoreBackend> AsWeakPtr() override;

  // Convenience Maho-specific API overloads:
  void InitBackend(base::OnceCallback<void(bool)> completion);
  bool IsInitialized() const;
  void GetAllLoginsMahoAsync(LoginsOrErrorReply callback);
  void GetAutofillableLoginsMahoAsync(LoginsOrErrorReply callback);
  void FillMatchingLoginsMahoAsync(const std::string& signon_realm, LoginsOrErrorReply callback);
  void AddLoginMahoAsync(const MahoPasswordForm& form, PasswordChangesOrErrorReply callback);
  void UpdateLoginMahoAsync(const MahoPasswordForm& form, PasswordChangesOrErrorReply callback);
  void RemoveLoginMahoAsync(const MahoPasswordForm& form, PasswordChangesOrErrorReply callback);
  void RemoveLoginsCreatedBetweenMahoAsync(base::Time delete_begin,
                                            base::Time delete_end,
                                            PasswordChangesOrErrorReply callback);

 private:
  bool initialized_ = false;
  bool enabled_ = true;
  base::WeakPtrFactory<MahoPasswordStoreBackend> weak_ptr_factory_{this};
};

}  // namespace passwords
}  // namespace maho

#endif  // MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_STORE_BACKEND_H_
