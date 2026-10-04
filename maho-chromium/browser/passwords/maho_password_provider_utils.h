// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_PROVIDER_UTILS_H_
#define MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_PROVIDER_UTILS_H_

#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"

class PrefRegistrySimple;
class PrefService;

namespace maho::passwords {

namespace prefs {

// Local-state boolean release kill switch. Defaults to true: the Maho native
// provider is the shipped password experience. While false, every native
// password WRITE (add/update) and FILL path no-ops instead of touching the
// Vault, and the browser shows no save/update prompt at all.
inline constexpr char kMahoNativePasswordWriteEnabled[] =
    "maho.passwords.native_write_enabled";

}  // namespace prefs

// Registers the Maho password prefs owned by this file on a local-state
// registry.
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Returns whether native password write/fill is enabled. Fails closed: a null
// PrefService, or a PrefService where the kill-switch pref has not been
// registered, yields false.
bool IsNativePasswordWriteEnabled(PrefService* prefs);

// Returns the PrefService the browser-process kill switch is read from
// (local state), or the test override when one is installed. May be null
// before/after browser-process startup, in which case writes stay disabled.
PrefService* GetNativePasswordWritePrefs();

// Test-only scoped override of the PrefService returned by
// GetNativePasswordWritePrefs().
class ScopedNativePasswordWritePrefsForTesting {
 public:
  explicit ScopedNativePasswordWritePrefsForTesting(PrefService* prefs);
  ~ScopedNativePasswordWritePrefsForTesting();

  ScopedNativePasswordWritePrefsForTesting(
      const ScopedNativePasswordWritePrefsForTesting&) = delete;
  ScopedNativePasswordWritePrefsForTesting& operator=(
      const ScopedNativePasswordWritePrefsForTesting&) = delete;

 private:
  raw_ptr<PrefService> previous_ = nullptr;
};

enum class EffectivePasswordProvider {
  kDisabled,
  kMahoNative,
  kBitwarden,
  kOnePassword,
};

// Additive detail for an effective-provider decision. Existing callers may
// continue to inspect only `provider`; callers that render recovery guidance
// can distinguish key loss, corruption, and plaintext residue from every
// existing generic kDisabled branch.
enum class EffectivePasswordProviderDisabledReason {
  kNone,
  kGenericDisabled,
  kVaultUnrecoverableKey,
  kVaultStructuralCorruption,
  kVaultPlaintextResidue,
};

struct EffectivePasswordProviderResult {
  EffectivePasswordProvider provider = EffectivePasswordProvider::kDisabled;
  EffectivePasswordProviderDisabledReason disabled_reason =
      EffectivePasswordProviderDisabledReason::kGenericDisabled;
};

// Normalizes known provider aliases. Unknown values fail closed to
// "disabled" rather than silently selecting Maho Native.
std::string NormalizePasswordProviderMode(std::string_view mode);

// Computes the browser-process effective provider from MahoCore settings,
// the provider registry, and the installed-extension snapshot for
// `profile_key`. Missing core/state or malformed JSON always returns
// kDisabled.
EffectivePasswordProvider GetEffectivePasswordProviderForProfileKey(
    std::string_view profile_key);

// Returns the same effective provider plus an additive typed disabled reason.
// Fatal Vault preflight states disable only Maho Native (or enrich an already
// disabled result); available external providers retain their Task-2 result.
EffectivePasswordProviderResult
GetEffectivePasswordProviderResultForProfileKey(std::string_view profile_key);

// Test seam for malformed/untrusted payload coverage without requiring FFI to
// accept invalid state.
EffectivePasswordProvider ComputeEffectivePasswordProviderForTesting(
    std::string_view settings_json,
    std::string_view registry_json,
    std::string_view installed_extensions_json);

std::string GetActivePasswordProviderMode();
std::string GetActivePasswordProviderModeForProfileKey(
    std::string_view profile_key);
bool IsNativePasswordProviderActive();
// Whether Vault/password operations must be device-reauthenticated (Touch ID
// or the platform authenticator) before each use. Missing or unreadable core
// settings fail closed to `true`.
bool IsVaultDeviceReauthRequired();
bool ShouldEnableChromiumPasswordManager();
bool IsBitwardenExtensionAvailable();
bool IsOnePasswordExtensionAvailable();
bool IsPasswordProviderAvailable(std::string_view mode);

bool IsChromiumAccountStoreDisabled();
bool IsGooglePasswordsSyncDisabled();

}  // namespace maho::passwords

#endif  // MAHO_BROWSER_PASSWORDS_MAHO_PASSWORD_PROVIDER_UTILS_H_
