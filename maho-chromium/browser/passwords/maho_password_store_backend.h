// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_STORE_BACKEND_H_
#define MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_STORE_BACKEND_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_store_backend.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#if __has_include("components/password_manager/core/browser/password_store/stored_credential.h")
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/password_manager/core/browser/password_string.h"
#define MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL 1
#else
#define MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL 0
#endif
#include "maho/chromium_src/components/password_manager/core/browser/password_fill_request.h"

#if !MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
namespace password_manager {
using BackendLoginsResult = LoginsResult;
using BackendLoginsResultOrError = LoginsResultOrError;
}
#endif

namespace maho {
namespace passwords {

#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
using PasswordStoreBackendCredential = password_manager::StoredCredential;
using ChromiumLoginsOrErrorReply =
    password_manager::BackendLoginsOrErrorReply;
using ChromiumBackendLoginsResult = password_manager::BackendLoginsResult;
#else
using PasswordStoreBackendCredential = password_manager::PasswordForm;
using ChromiumLoginsOrErrorReply = password_manager::LoginsOrErrorReply;
using ChromiumBackendLoginsResult = password_manager::LoginsResult;
#endif

enum class PasswordAuthorizationAction;

// Task 4: the Vault-side representation of one credential. It carries every
// `password_manager::PasswordForm` field this backend round-trips, plus the
// Vault identity (`id` = item UUID, `revision` = CAS revision) that the
// mutation paths need.
//
// Fields NOT round-tripped (they fail closed to Chromium's defaults on read;
// see kUnsupportedFields in the .cc for the authoritative list):
// affiliated_web_realm, app_display_name, app_icon_url,
// previously_associated_sync_account_email, moving_blocked_for_list,
// password_issues, generation_upload_status, keychain_identifier, sender_*,
// sharing_notification_displayed, actor_login_approved.
struct MahoMutationResult {
  std::string id;
  uint64_t revision = 0;
};

struct MahoPasswordForm {
  // Vault identity.
  std::string id;
  uint64_t revision = 0;

  // Identification & URLs.
  password_manager::PasswordForm::Scheme scheme =
      password_manager::PasswordForm::Scheme::kHtml;
  std::string signon_realm;
  std::string url;
  std::string action;
  std::string federation_origin;

  // Elements.
  std::string submit_element;
  std::string username_element;
  std::string password_element;

  // Values.
  std::string username_value;
  std::string password_value;
  std::vector<password_manager::AlternativeElement> all_alternative_usernames;

  // Timestamps.
  base::Time date_created;
  base::Time date_last_used;
  base::Time date_last_filled;
  base::Time date_password_modified;
  base::Time date_received;

  // Metadata.
  bool blocked_by_user = false;
  password_manager::PasswordForm::Type type =
      password_manager::PasswordForm::Type::kFormSubmission;
  int times_used_in_html_form = 0;
  std::u16string display_name;
  std::string icon_url;
  std::optional<password_manager::PasswordForm::MatchType> match_type;
  bool skip_zero_click = false;
  password_manager::PasswordForm::Store in_store =
      password_manager::PasswordForm::Store::kNotSet;

  // Form data, carried as a base64 `autofill::SerializeFormData` pickle. The
  // Vault stores it opaquely and never interprets it.
  std::string form_data_base64;

  std::vector<password_manager::PasswordNote> notes;

  MahoPasswordForm();
  ~MahoPasswordForm();

  MahoPasswordForm(const MahoPasswordForm &);
  MahoPasswordForm &operator=(const MahoPasswordForm &);

  MahoPasswordForm(MahoPasswordForm &&other) noexcept;
  MahoPasswordForm &operator=(MahoPasswordForm &&other) noexcept;

  void Zeroize() {
    if (!password_value.empty()) {
      std::fill_n(static_cast<volatile char *>(&password_value[0]),
                  password_value.size(), 0);
      password_value.clear();
    }
  }
};

// Task 4: the `password_manager::PasswordForm` fields this backend does NOT
// round-trip. They fail closed to Chromium's own defaults on read instead of
// being reconstructed from a guess, so "lossless" is an enumerated claim rather
// than a blanket one. See the definition in the .cc for why each is excluded.
std::vector<std::string> UnsupportedPasswordFormFields();

// Task 8 test seam. Production leaves this unset; tests use it to verify that
// transient request, response, and UTF-8 secret buffers are overwritten before
// release without retaining their contents.
using SecureZeroizeObserverForTesting =
    base::RepeatingCallback<void(const std::string &)>;
void SetSecureZeroizeObserverForTesting(
    SecureZeroizeObserverForTesting observer);

// Task 4: authoritative mapping between Chromium's integer `FormPrimaryKey` and
// the Vault's string item UUID + CAS revision.
//
// Chromium's `PasswordForm::primary_key` is an `int` allocated by the login
// database; the Vault's identity is a UUID string with an independent
// monotonically-increasing revision. Neither can be derived from the other, so
// this backend owns the bijection: the FIRST time a UUID is observed on a read,
// it is assigned the next unused positive int; that assignment is stable for
// the lifetime of the backend instance. Every read stamps the current revision,
// so a later update/remove carries the revision that was actually observed and
// the Vault CAS rejects a stale mutation instead of clobbering a concurrent
// one.
class MahoCredentialIdentityMap {
public:
  MahoCredentialIdentityMap();
  ~MahoCredentialIdentityMap();

  MahoCredentialIdentityMap(const MahoCredentialIdentityMap &) = delete;
  MahoCredentialIdentityMap &
  operator=(const MahoCredentialIdentityMap &) = delete;

  // Returns the primary key for this exact Vault UUID/revision snapshot. A new
  // revision receives a new key, so a PasswordForm retained by a caller
  // keeps the revision it originally observed for CAS.
  int Observe(const std::string &vault_id, uint64_t revision);

  // Resolves a Chromium primary key back to the Vault UUID, or nullopt when the
  // key was never observed by this backend instance (e.g. a form that came from
  // another store).
  std::optional<std::string> VaultIdForPrimaryKey(int primary_key) const;

  // Primary key previously assigned to this exact Vault UUID/revision snapshot,
  // or nullopt when this backend has never read that snapshot.
  std::optional<int>
  PrimaryKeyForVaultIdAndRevision(const std::string &vault_id,
                                  uint64_t revision) const;

  // Last revision observed for `vault_id`, or nullopt when unknown.
  std::optional<uint64_t> RevisionForVaultId(const std::string &vault_id) const;
  // Revision bound to this specific primary-key snapshot, or nullopt when the
  // key was not issued by this backend.
  std::optional<uint64_t> RevisionForPrimaryKey(int primary_key) const;

  void Clear();

private:
  std::map<std::pair<std::string, uint64_t>, int> snapshot_to_key_;
  std::map<int, std::string> key_to_vault_id_;
  std::map<int, uint64_t> key_to_revision_;
  std::map<std::string, uint64_t> vault_id_to_revision_;
  int next_key_ = 1;
};

// MahoPasswordStoreBackend implements Chromium's PasswordStoreBackend contract
// adapting it to Maho's typed, persistence-aware Vault core via FFI.
class MahoPasswordStoreBackend : public password_manager::PasswordStoreBackend {
public:
  using LoginsOrErrorReply =
      base::OnceCallback<void(std::vector<MahoPasswordForm>, bool)>;
  using PasswordChangesOrErrorReply = base::OnceCallback<void(bool)>;
  using MutationResultReply =
      base::OnceCallback<void(std::optional<MahoPasswordForm>)>;

  explicit MahoPasswordStoreBackend(bool enabled = true,
                                    std::string profile_key = {});
  ~MahoPasswordStoreBackend() override;

  MahoPasswordStoreBackend(const MahoPasswordStoreBackend &) = delete;
  MahoPasswordStoreBackend &
  operator=(const MahoPasswordStoreBackend &) = delete;

  // password_manager::PasswordStoreBackend virtual interface implementation:
  void
  InitBackend(RemoteChangesReceived remote_form_changes_received,
              base::RepeatingClosure sync_enabled_or_disabled_cb,
              base::OnceCallback<void(bool)> completion) override;
  void Shutdown(base::OnceClosure shutdown_completed) override;
  password_manager::ActionableError GetError() override;
  void GetAllLoginsAsync(ChromiumLoginsOrErrorReply callback) override;
  void GetAllLoginsWithAffiliationAndBrandingAsync(
      ChromiumLoginsOrErrorReply callback) override;
  void GetAutofillableLoginsAsync(
      ChromiumLoginsOrErrorReply callback) override;
  void FillMatchingLoginsAsync(
      ChromiumLoginsOrErrorReply callback, bool include_psl,
      const std::vector<password_manager::PasswordFormDigest> &forms) override;
  void GetGroupedMatchingLoginsAsync(
      const password_manager::PasswordFormDigest &form_digest,
      ChromiumLoginsOrErrorReply callback) override;
  void GetGroupedMatchingLoginsAsync(
      const password_manager::PasswordFormDigest &form_digest,
      const password_manager::PasswordFillRequestContext &context,
      ChromiumLoginsOrErrorReply callback) override;
  void ResolvePasswordFill(
      const password_manager::PasswordFillRequestContext &context,
      const password_manager::PasswordFillSelection &selection,
      password_manager::PasswordFillResolver resolver) override;
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  void AddLoginAsync(PasswordStoreBackendCredential form,
#else
  void AddLoginAsync(const PasswordStoreBackendCredential &form,
#endif
      password_manager::PasswordChangesOrErrorReply callback) override;
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  void UpdateLoginAsync(PasswordStoreBackendCredential form,
#else
  void UpdateLoginAsync(const PasswordStoreBackendCredential &form,
#endif
      password_manager::PasswordChangesOrErrorReply callback) override;
  void RemoveLoginAsync(
      const base::Location &location,
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
      PasswordStoreBackendCredential form,
#else
      const PasswordStoreBackendCredential &form,
#endif
      password_manager::PasswordChangesOrErrorReply callback) override;
  void RemoveLoginsCreatedBetweenAsync(
      const base::Location &location, base::Time delete_begin,
      base::Time delete_end,
      password_manager::PasswordChangesOrErrorReply callback) override;
  void DisableAutoSignInForOriginsAsync(
      const base::RepeatingCallback<bool(const GURL &)> &origin_filter,
      base::OnceClosure completion) override;
  password_manager::SmartBubbleStatsStore *GetSmartBubbleStatsStore() override;
  std::unique_ptr<syncer::DataTypeControllerDelegate>
  CreateSyncControllerDelegate() override;
  void OnSyncServiceInitialized(syncer::SyncService *sync_service) override;
  base::WeakPtr<password_manager::PasswordStoreBackend> AsWeakPtr() override;

  // Convenience Maho-specific API overloads:
  void InitBackend(base::OnceCallback<void(bool)> completion);
  bool IsInitialized() const;
  void GetAllLoginsMahoAsync(LoginsOrErrorReply callback);
  void GetAutofillableLoginsMahoAsync(LoginsOrErrorReply callback);
  void FillMatchingLoginsMahoAsync(
      const std::string &signon_realm,
      password_manager::PasswordForm::Scheme scheme,
      LoginsOrErrorReply callback);
  void AddLoginMahoAsync(const MahoPasswordForm &form,
                         PasswordChangesOrErrorReply callback);
  void UpdateLoginMahoAsync(const MahoPasswordForm &form,
                            PasswordChangesOrErrorReply callback);
  void RemoveLoginMahoAsync(const MahoPasswordForm &form,
                            PasswordChangesOrErrorReply callback);
  void
  RemoveLoginsCreatedBetweenMahoAsync(base::Time delete_begin,
                                      base::Time delete_end,
                                      PasswordChangesOrErrorReply callback);

  // Task 7 test seam: verifies the immutable identity captured by the factory.
  const std::string &profile_key_for_testing() const { return profile_key_; }

  // Task 4 test/inspection seam: the identity map this backend instance owns.
  const MahoCredentialIdentityMap &identity_map_for_testing() const {
    return identity_map_;
  }
  void ObserveIdentityForTesting(const std::string &item_id,
                                 uint64_t revision) {
    identity_map_.Observe(item_id, revision);
  }

private:
  // Records the identity of every form returned by a read so a later mutation
  // can carry the right UUID + revision.
  void ObserveIdentities(const std::vector<MahoPasswordForm> &forms);
  void EnsureAuthorized(const std::string &origin_scope,
                        PasswordAuthorizationAction action,
                        const std::u16string &message,
                        base::OnceCallback<void(bool)> callback);
  void ReadLoginsForOperationMahoAsync(
      const std::optional<std::string> &origin,
      const std::string &authorization_scope, PasswordAuthorizationAction action,
      std::optional<password_manager::PasswordForm::Scheme> scheme,
      LoginsOrErrorReply callback);
  void AddLoginWithResultMahoAsync(const MahoPasswordForm &form,
                                   MutationResultReply callback);
  void UpdateLoginWithResultMahoAsync(const MahoPasswordForm &form,
                                      MutationResultReply callback);

  // Task 8: context is captured only by the additive discovery overload. The
  // resolver accepts a selection only when the exact snapshot was discovered
  // for that opaque document token and canonical requesting origin.
  using FillContextKey =
      std::pair<std::string, password_manager::PasswordManagerDocumentToken>;
  struct FillContextSnapshot {
    uint64_t revision = 0;
    std::string signon_realm;
  };
  void
  RecordFillContext(const password_manager::PasswordFillRequestContext &context,
                    const std::vector<MahoPasswordForm> &forms);
  bool WasSnapshotDiscoveredForContext(
      const password_manager::PasswordFillRequestContext &context,
      int primary_key, uint64_t revision) const;

  // Resolves the Vault identity (UUID + revision) a mutation must target.
  // `cred.primary_key` wins when this backend assigned it; otherwise the
  // signon_realm + username_value + (when set) username_element tuple selects
  // the matching duplicate from `candidates`.
  static std::optional<MahoPasswordForm>
  ResolveTarget(const PasswordStoreBackendCredential &form,
                const MahoCredentialIdentityMap &identity_map,
                const std::vector<MahoPasswordForm> &candidates);
  // Task 3: the effective password provider is consulted PER OPERATION (never
  // cached at construction) so a runtime provider change is honored without
  // recreating the store. Returns true only while Maho Native is the effective
  // provider for the profile that owns the browser-global core; an external
  // provider (Bitwarden/1Password), a disabled provider, a missing core, or
  // malformed provider state all return false, in which case this backend must
  // not read, write, or fill the Maho Vault.
  //
  // Provider selection remains runtime-evaluated. Vault session authorization,
  // separately, is bound to `profile_key_`, which the factory captures from the
  // profile path + PrefService identity at construction.
  static bool IsMahoVaultRoutingActive();

  // Captured from the factory's profile path + PrefService construction
  // context. Never rebound from the browser-global core owner.
  const std::string profile_key_;
  bool initialized_ = false;
  bool enabled_ = true;
  // Task 4: `InitBackend()` records the real reason initialization failed so
  // `GetError()` reports it instead of silently claiming success.
  password_manager::ActionableError last_error_ =
      password_manager::ActionableError::kNoError;
  MahoCredentialIdentityMap identity_map_;
  std::map<FillContextKey, std::map<int, FillContextSnapshot>>
      fill_context_snapshots_;
  base::WeakPtrFactory<MahoPasswordStoreBackend> weak_ptr_factory_{this};
};

} // namespace passwords
} // namespace maho

#endif // MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_STORE_BACKEND_H_
