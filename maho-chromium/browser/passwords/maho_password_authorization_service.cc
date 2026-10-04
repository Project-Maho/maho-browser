// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_authorization_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/device_reauth/device_authenticator.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "url/gurl.h"

namespace maho::passwords {

MahoPasswordAuthorizationService *MahoPasswordAuthorizationService::Get() {
  static base::NoDestructor<MahoPasswordAuthorizationService> service;
  return service.get();
}

MahoPasswordAuthorizationService::PendingAuthorization::PendingAuthorization(
    AuthorizationKey key,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    base::OnceCallback<void(bool)> callback)
    : key(std::move(key)), authenticator(std::move(authenticator)),
      callback(std::move(callback)) {}

MahoPasswordAuthorizationService::PendingAuthorization::PendingAuthorization(
    PendingAuthorization &&) = default;
MahoPasswordAuthorizationService::PendingAuthorization &
MahoPasswordAuthorizationService::PendingAuthorization::operator=(
    PendingAuthorization &&) = default;
MahoPasswordAuthorizationService::PendingAuthorization::
    ~PendingAuthorization() = default;

MahoPasswordAuthorizationService::MahoPasswordAuthorizationService() {
  vault_lock_state_subscription_ =
      maho::AddVaultLockStateChangeCallback(base::BindRepeating(
          &MahoPasswordAuthorizationService::OnVaultLockStateChanged,
          base::Unretained(this)));
}

MahoPasswordAuthorizationService::~MahoPasswordAuthorizationService() = default;

void MahoPasswordAuthorizationService::AuthorizeForProfile(
    const std::string &profile_key, const std::string &origin_scope,
    PasswordAuthorizationAction action, const std::u16string &message,
    base::OnceCallback<void(bool)> callback) {
  if (!interactive_authorization_enabled_for_testing_) {
    std::move(callback).Run(false);
    return;
  }
  Profile *profile = maho::GetCoreOwnerProfile();
  if (!profile || profile_key.empty() ||
      profile_key != maho::GetProfileIdentityKey(profile->GetPath(),
                                                 profile->GetPrefs())) {
    std::move(callback).Run(false);
    return;
  }

  if (!RequiresDeviceReauth()) {
    std::move(callback).Run(true);
    return;
  }

  device_reauth::DeviceAuthParams params(
      kAuthorizationLifetime, device_reauth::DeviceAuthSource::kPasswordManager,
      "PasswordManager.ReauthToAccessPasswordInSettings");
  Authorize(profile_key, origin_scope, action,
            ChromeDeviceAuthenticatorFactory::GetForProfile(
                profile, gfx::NativeWindow(), params),
            message, std::move(callback));
}

void MahoPasswordAuthorizationService::Authorize(
    const std::string &profile_key, const std::string &origin_scope,
    PasswordAuthorizationAction action,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    const std::u16string &message, base::OnceCallback<void(bool)> callback) {
  const std::string canonical_scope = CanonicalizeOriginScope(origin_scope);
  if (profile_key.empty() || canonical_scope.empty()) {
    std::move(callback).Run(false);
    return;
  }

  const AuthorizationKey key{profile_key, canonical_scope, action};
  // An existing grant, or an unlocked Vault while device reauth is not
  // required, authorizes without a platform authenticator. Platforms without
  // one (Linux desktop) pass a null authenticator.
  if (ConsumeAuthorization(profile_key, canonical_scope, action)) {
    std::move(callback).Run(true);
    return;
  }
  if (!authenticator) {
    std::move(callback).Run(false);
    return;
  }

  const bool vault_locked =
      vault_locked_override_.value_or(maho::IsVaultLockedForUi());
  if (vault_locked) {
    std::move(callback).Run(false);
    return;
  }

  const uint64_t request_id = next_request_id_++;
  auto [it, inserted] = pending_authorizations_.emplace(
      std::piecewise_construct, std::forward_as_tuple(request_id),
      std::forward_as_tuple(key, std::move(authenticator),
                            std::move(callback)));
  DCHECK(inserted);
  it->second.authenticator->AuthenticateWithMessage(
      message,
      base::BindOnce(
          &MahoPasswordAuthorizationService::OnDeviceAuthenticationComplete,
          base::Unretained(this), request_id));
}

bool MahoPasswordAuthorizationService::ConsumeAuthorization(
    const std::string &profile_key, const std::string &origin_scope,
    PasswordAuthorizationAction action) {
  const std::string canonical_scope = CanonicalizeOriginScope(origin_scope);
  const bool vault_locked =
      vault_locked_override_.value_or(maho::IsVaultLockedForUi());
  if (profile_key.empty() || canonical_scope.empty() || vault_locked) {
    return false;
  }

  // While the device re-authentication requirement is disabled, an unlocked
  // Vault authorizes every operation; the unlock itself is the boundary.
  if (!RequiresDeviceReauth()) {
    return true;
  }

  const AuthorizationKey key{profile_key, canonical_scope, action};
  auto it = grants_.find(key);
  if (it == grants_.end() && canonical_scope != kAllOriginsScope &&
      canonical_scope != kRangeDeleteScope) {
    const AuthorizationKey all_origins_key{profile_key, kAllOriginsScope,
                                           action};
    it = grants_.find(all_origins_key);
  }
  if (it == grants_.end()) {
    return false;
  }
  if (it->second <= base::TimeTicks::Now()) {
    grants_.erase(it);
    return false;
  }
  return true;
}

void MahoPasswordAuthorizationService::RevokeProfile(
    const std::string &profile_key) {
  if (profile_key.empty()) {
    return;
  }
  InvalidateMatchingProfile(&profile_key);
}

void MahoPasswordAuthorizationService::RevokeAll() {
  InvalidateMatchingProfile(nullptr);
  ClearMahoOwnedClipboard();
}

void MahoPasswordAuthorizationService::CopySecretToClipboard(
    std::u16string secret) {
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(secret);
  clipboard_secret_ = std::move(secret);
  clipboard_clear_timer_.Start(
      FROM_HERE, kAuthorizationLifetime,
      base::BindOnce(
          &MahoPasswordAuthorizationService::ClearMahoOwnedClipboardIfUnchanged,
          base::Unretained(this)));
}

void MahoPasswordAuthorizationService::ClearMahoOwnedClipboard() {
  ClearMahoOwnedClipboardIfUnchanged();
}

// static
bool MahoPasswordAuthorizationService::ShouldClearMahoOwnedClipboardForTesting(
    const std::u16string &expected, const std::u16string &current) {
  return expected == current;
}

void MahoPasswordAuthorizationService::GrantForTesting(
    const std::string &profile_key, const std::string &origin_scope,
    PasswordAuthorizationAction action, base::TimeTicks expires_at) {
  const std::string canonical_scope = CanonicalizeOriginScope(origin_scope);
  DCHECK(!profile_key.empty());
  DCHECK(!canonical_scope.empty());
  grants_[AuthorizationKey{profile_key, canonical_scope, action}] = expires_at;
}

void MahoPasswordAuthorizationService::ResetForTesting() {
  RevokeAll();
  next_request_id_ = 1;
  vault_locked_override_.reset();
  device_reauth_required_for_testing_.reset();
  interactive_authorization_enabled_for_testing_ = false;
}

void MahoPasswordAuthorizationService::SetDeviceReauthRequiredForTesting(
    bool required) {
  device_reauth_required_for_testing_ = required;
}

bool MahoPasswordAuthorizationService::RequiresDeviceReauth() {
  if (device_reauth_required_for_testing_.has_value()) {
    return *device_reauth_required_for_testing_;
  }
  return maho::passwords::IsVaultDeviceReauthRequired();
}

void MahoPasswordAuthorizationService::
    SetInteractiveAuthorizationEnabledForTesting(bool enabled) {
  interactive_authorization_enabled_for_testing_ = enabled;
}

void MahoPasswordAuthorizationService::SetVaultLockedForTesting(bool locked) {
  vault_locked_override_ = locked;
  if (locked) {
    RevokeAll();
  }
}

size_t MahoPasswordAuthorizationService::grant_count_for_testing() const {
  return grants_.size();
}

std::string MahoPasswordAuthorizationService::CanonicalizeOriginScope(
    const std::string &origin_scope) {
  if (origin_scope == kAllOriginsScope || origin_scope == kRangeDeleteScope) {
    return origin_scope;
  }
  if (origin_scope.starts_with("android://")) {
    // Android signon realms are already exact, opaque credential scopes.
    return origin_scope;
  }

  const GURL url(origin_scope);
  if (!url.is_valid() || !url.has_host()) {
    return std::string();
  }
  return url.DeprecatedGetOriginAsURL().spec();
}

void MahoPasswordAuthorizationService::OnDeviceAuthenticationComplete(
    uint64_t request_id, bool success) {
  auto it = pending_authorizations_.find(request_id);
  if (it == pending_authorizations_.end()) {
    return;
  }

  PendingAuthorization pending = std::move(it->second);
  pending_authorizations_.erase(it);
  const bool vault_locked =
      vault_locked_override_.value_or(maho::IsVaultLockedForUi());
  const bool authorized = success && !vault_locked;
  if (authorized) {
    grants_[pending.key] = base::TimeTicks::Now() + kAuthorizationLifetime;
  }
  std::move(pending.callback).Run(authorized);
}

void MahoPasswordAuthorizationService::InvalidateMatchingProfile(
    const std::string *profile_key) {
  for (auto it = grants_.begin(); it != grants_.end();) {
    if (!profile_key || it->first.profile_key == *profile_key) {
      it = grants_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = pending_authorizations_.begin();
       it != pending_authorizations_.end();) {
    if (profile_key && it->second.key.profile_key != *profile_key) {
      ++it;
      continue;
    }
    PendingAuthorization pending = std::move(it->second);
    it = pending_authorizations_.erase(it);
    pending.authenticator->Cancel();
    std::move(pending.callback).Run(false);
  }
}

void MahoPasswordAuthorizationService::OnVaultLockStateChanged(bool locked) {
  vault_locked_override_ = locked;
  if (locked) {
    RevokeAll();
  }
}

void MahoPasswordAuthorizationService::ClearMahoOwnedClipboardIfUnchanged() {
  clipboard_clear_timer_.Stop();
  if (!clipboard_secret_) {
    return;
  }

  std::u16string expected = std::move(*clipboard_secret_);
  clipboard_secret_.reset();
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, std::nullopt,
      base::BindOnce(
          [](std::u16string expected, std::u16string current) {
            if (ShouldClearMahoOwnedClipboardForTesting(expected, current)) {
              ui::Clipboard::GetForCurrentThread()->Clear(
                  ui::ClipboardBuffer::kCopyPaste);
            }
            std::fill(expected.begin(), expected.end(), u'\0');
            std::fill(current.begin(), current.end(), u'\0');
          },
          std::move(expected)));
}

} // namespace maho::passwords
