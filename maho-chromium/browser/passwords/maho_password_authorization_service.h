// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_AUTHORIZATION_SERVICE_H_
#define MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_AUTHORIZATION_SERVICE_H_

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace device_reauth {
class DeviceAuthenticator;
}

namespace maho::passwords {

// Every authorization is bound to one profile identity, exact origin scope,
// and one narrowly-defined password operation. Do not collapse these actions:
// a successful copy reauth must never authorize a Vault mutation or fill.
enum class PasswordAuthorizationAction {
  kReadAll,
  kFill,
  kAdd,
  kUpdate,
  kDelete,
  kRangeDelete,
  kCopy,
  kImportCommit,
  kPolicyUpdate,
  kAgentCredentialUse,
};

// Browser-process authority for device-reauthenticated password operations.
//
// This deliberately owns short-lived grants rather than relying on
// DeviceAuthenticator's profile-wide cache. A grant always includes the
// immutable profile identity, canonical origin scope, and action. The service
// revokes grants on Vault lock, explicit invalidation, and test teardown.
class MahoPasswordAuthorizationService {
public:
  static constexpr base::TimeDelta kAuthorizationLifetime = base::Seconds(60);
  static constexpr char kAllOriginsScope[] = "maho://all-origins";
  static constexpr char kRangeDeleteScope[] = "maho://range-delete";

  static MahoPasswordAuthorizationService *Get();

  MahoPasswordAuthorizationService(const MahoPasswordAuthorizationService &) =
      delete;
  MahoPasswordAuthorizationService &
  operator=(const MahoPasswordAuthorizationService &) = delete;
  ~MahoPasswordAuthorizationService();

  // Production issuer for PasswordStore backend operations. It resolves the
  // immutable core-owner profile and creates Chromium's password-manager
  // device authenticator before delegating to Authorize().
  void AuthorizeForProfile(const std::string &profile_key,
                           const std::string &origin_scope,
                           PasswordAuthorizationAction action,
                           const std::u16string &message,
                           base::OnceCallback<void(bool)> callback);

  // Starts one device-authentication flow. `authenticator` remains owned by
  // this service until it completes or the matching profile is invalidated.
  // Invalid arguments fail closed without invoking the authenticator.
  void
  Authorize(const std::string &profile_key, const std::string &origin_scope,
            PasswordAuthorizationAction action,
            std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
            const std::u16string &message,
            base::OnceCallback<void(bool)> callback);

  bool ConsumeAuthorization(const std::string &profile_key,
                            const std::string &origin_scope,
                            PasswordAuthorizationAction action);
  void RevokeProfile(const std::string &profile_key);
  void RevokeAll();
  void CopySecretToClipboard(std::u16string secret);
  void ClearMahoOwnedClipboard();

  static bool
  ShouldClearMahoOwnedClipboardForTesting(const std::u16string &expected,
                                          const std::u16string &current);

  void GrantForTesting(const std::string &profile_key,
                       const std::string &origin_scope,
                       PasswordAuthorizationAction action,
                       base::TimeTicks expires_at);
  void ResetForTesting();
  void SetVaultLockedForTesting(bool locked);
  void SetInteractiveAuthorizationEnabledForTesting(bool enabled);
  // Test seam for the core-owned `autofill.vaultRequireDeviceAuth` setting.
  void SetDeviceReauthRequiredForTesting(bool required);
  size_t grant_count_for_testing() const;

private:
  friend class base::NoDestructor<MahoPasswordAuthorizationService>;

  struct AuthorizationKey {
    std::string profile_key;
    std::string origin_scope;
    PasswordAuthorizationAction action;

    auto operator<=>(const AuthorizationKey &) const = default;
  };

  struct PendingAuthorization {
    PendingAuthorization(
        AuthorizationKey key,
        std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
        base::OnceCallback<void(bool)> callback);
    PendingAuthorization(PendingAuthorization &&);
    PendingAuthorization &operator=(PendingAuthorization &&);
    ~PendingAuthorization();

    AuthorizationKey key;
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator;
    base::OnceCallback<void(bool)> callback;
  };

  MahoPasswordAuthorizationService();

  // Device re-authentication requirement, resolved from core settings unless a
  // test override is installed.
  bool RequiresDeviceReauth();

  static std::string CanonicalizeOriginScope(const std::string &origin_scope);
  void OnDeviceAuthenticationComplete(uint64_t request_id, bool success);
  void InvalidateMatchingProfile(const std::string *profile_key);
  void OnVaultLockStateChanged(bool locked);
  void ClearMahoOwnedClipboardIfUnchanged();

  uint64_t next_request_id_ = 1;
  std::map<AuthorizationKey, base::TimeTicks> grants_;
  std::map<uint64_t, PendingAuthorization> pending_authorizations_;
  base::CallbackListSubscription vault_lock_state_subscription_;
  std::optional<bool> vault_locked_override_;
  std::optional<bool> device_reauth_required_for_testing_;
  bool interactive_authorization_enabled_for_testing_ = true;
  std::optional<std::u16string> clipboard_secret_;
  base::OneShotTimer clipboard_clear_timer_;
};

} // namespace maho::passwords

#endif // MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_AUTHORIZATION_SERVICE_H_
