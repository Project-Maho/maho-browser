// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_CAPABILITY_PRINCIPAL_H_
#define MAHO_BROWSER_AI_MAHO_CAPABILITY_PRINCIPAL_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "maho/browser/ai/maho_capability_types.h"

namespace maho::ai {

enum class PrincipalKind {
  kUnknown,
  kInBrowserAgent,
  kLocalMcpPeer,
  kRoutineRunner,
  kControlPlane,
  kCliAgent,
  kCliGeneric,
  kExtensionHelper,
  kUtilityProcessAgent,
};

enum class AuthenticationStrength {
  kNone,
  kUnixUidMatch,
  kCryptographicToken,
  kHardwareSigned,
  kInternalTrusted,
};

struct ExecutableIdentity {
  ExecutableIdentity();
  ExecutableIdentity(const ExecutableIdentity&);
  ExecutableIdentity(ExecutableIdentity&&) noexcept;
  ExecutableIdentity& operator=(const ExecutableIdentity&);
  ExecutableIdentity& operator=(ExecutableIdentity&&) noexcept;
  ~ExecutableIdentity();

  std::string executable_path;
  std::string bundle_id;
  std::string signing_id;
  std::string code_hash;
  bool is_first_party = false;
  bool is_platform_signed = false;
};

struct DeviceIdentity {
  DeviceIdentity();
  DeviceIdentity(const DeviceIdentity&);
  DeviceIdentity(DeviceIdentity&&) noexcept;
  DeviceIdentity& operator=(const DeviceIdentity&);
  DeviceIdentity& operator=(DeviceIdentity&&) noexcept;
  ~DeviceIdentity();

  std::string device_id;
  std::string key_id;
  MinimumDeviceProtection protection_level = MinimumDeviceProtection::kNone;
  bool is_hardware_backed = false;
};

struct CapabilityPrincipal {
  CapabilityPrincipal();
  CapabilityPrincipal(const CapabilityPrincipal&);
  CapabilityPrincipal(CapabilityPrincipal&&) noexcept;
  CapabilityPrincipal& operator=(const CapabilityPrincipal&);
  CapabilityPrincipal& operator=(CapabilityPrincipal&&) noexcept;
  ~CapabilityPrincipal();

  std::string principal_id;
  PrincipalKind kind = PrincipalKind::kUnknown;
  AuthenticationStrength auth_strength = AuthenticationStrength::kNone;
  ExecutableIdentity executable_identity;
  DeviceIdentity device_identity;

  bool IsInternalTrusted() const {
    return auth_strength == AuthenticationStrength::kInternalTrusted;
  }

  bool IsFirstParty() const {
    return IsInternalTrusted() || executable_identity.is_first_party;
  }

  bool MeetsMinimumProtection(MinimumDeviceProtection required) const {
    if (required == MinimumDeviceProtection::kNone) {
      return true;
    }
    if (required == MinimumDeviceProtection::kOSKeystore) {
      return device_identity.protection_level ==
                 MinimumDeviceProtection::kOSKeystore ||
             device_identity.protection_level ==
                 MinimumDeviceProtection::kHardwareKey ||
             device_identity.protection_level ==
                 MinimumDeviceProtection::kSecureEnclaveOrTpm;
    }
    if (required == MinimumDeviceProtection::kHardwareKey) {
      return device_identity.protection_level ==
                 MinimumDeviceProtection::kHardwareKey ||
             device_identity.protection_level ==
                 MinimumDeviceProtection::kSecureEnclaveOrTpm;
    }
    if (required == MinimumDeviceProtection::kSecureEnclaveOrTpm) {
      return device_identity.protection_level ==
             MinimumDeviceProtection::kSecureEnclaveOrTpm;
    }
    return false;
  }

  static CapabilityPrincipal MakeInternalAgent(std::string_view session_id) {
    CapabilityPrincipal p;
    p.principal_id = std::string(session_id);
    p.kind = PrincipalKind::kInBrowserAgent;
    p.auth_strength = AuthenticationStrength::kInternalTrusted;
    p.executable_identity.is_first_party = true;
    p.executable_identity.is_platform_signed = true;
    p.device_identity.protection_level =
        MinimumDeviceProtection::kSecureEnclaveOrTpm;
    p.device_identity.is_hardware_backed = true;
    return p;
  }

  static CapabilityPrincipal MakeControlPlane() {
    CapabilityPrincipal p;
    p.principal_id = "maho_control_plane";
    p.kind = PrincipalKind::kControlPlane;
    p.auth_strength = AuthenticationStrength::kInternalTrusted;
    p.executable_identity.is_first_party = true;
    p.executable_identity.is_platform_signed = true;
    p.device_identity.protection_level =
        MinimumDeviceProtection::kSecureEnclaveOrTpm;
    p.device_identity.is_hardware_backed = true;
    return p;
  }

  static CapabilityPrincipal MakeRoutineRunner(std::string_view routine_id) {
    CapabilityPrincipal p;
    p.principal_id = std::string(routine_id);
    p.kind = PrincipalKind::kRoutineRunner;
    p.auth_strength = AuthenticationStrength::kInternalTrusted;
    p.executable_identity.is_first_party = true;
    p.executable_identity.is_platform_signed = true;
    return p;
  }

  static CapabilityPrincipal MakeLocalMcp(
      std::string_view controller_id,
      AuthenticationStrength strength,
      ExecutableIdentity exec_id,
      DeviceIdentity dev_id = {}) {
    CapabilityPrincipal p;
    p.principal_id = std::string(controller_id);
    p.kind = PrincipalKind::kLocalMcpPeer;
    p.auth_strength = strength;
    p.executable_identity = std::move(exec_id);
    p.device_identity = std::move(dev_id);
    return p;
  }

  static CapabilityPrincipal MakeCliAgent(
      std::string_view controller_id,
      AuthenticationStrength strength,
      ExecutableIdentity exec_id) {
    CapabilityPrincipal p;
    p.principal_id = std::string(controller_id);
    p.kind = PrincipalKind::kCliAgent;
    p.auth_strength = strength;
    p.executable_identity = std::move(exec_id);
    p.executable_identity.is_first_party = true;
    return p;
  }

  static CapabilityPrincipal MakeCliGeneric(
      std::string_view controller_id,
      AuthenticationStrength strength) {
    CapabilityPrincipal p;
    p.principal_id = std::string(controller_id);
    p.kind = PrincipalKind::kCliGeneric;
    p.auth_strength = strength;
    return p;
  }

  static CapabilityPrincipal MakeUtilityProcessAgent(
      std::string_view process_id,
      std::string_view session_id) {
    CapabilityPrincipal p;
    p.principal_id = std::string(process_id) + ":" + std::string(session_id);
    p.kind = PrincipalKind::kUtilityProcessAgent;
    p.auth_strength = AuthenticationStrength::kInternalTrusted;
    p.executable_identity.is_first_party = true;
    p.executable_identity.is_platform_signed = true;
    return p;
  }
};

inline constexpr std::string_view PrincipalKindName(PrincipalKind kind) {
  switch (kind) {
    case PrincipalKind::kUnknown:
      return "unknown";
    case PrincipalKind::kInBrowserAgent:
      return "in_browser_agent";
    case PrincipalKind::kLocalMcpPeer:
      return "local_mcp_peer";
    case PrincipalKind::kRoutineRunner:
      return "routine_runner";
    case PrincipalKind::kControlPlane:
      return "control_plane";
    case PrincipalKind::kCliAgent:
      return "cli_agent";
    case PrincipalKind::kCliGeneric:
      return "cli_generic";
    case PrincipalKind::kExtensionHelper:
      return "extension_helper";
    case PrincipalKind::kUtilityProcessAgent:
      return "utility_process_agent";
  }
  return "unknown";
}

inline constexpr std::string_view AuthenticationStrengthName(
    AuthenticationStrength strength) {
  switch (strength) {
    case AuthenticationStrength::kNone:
      return "none";
    case AuthenticationStrength::kUnixUidMatch:
      return "unix_uid_match";
    case AuthenticationStrength::kCryptographicToken:
      return "cryptographic_token";
    case AuthenticationStrength::kHardwareSigned:
      return "hardware_signed";
    case AuthenticationStrength::kInternalTrusted:
      return "internal_trusted";
  }
  return "none";
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_CAPABILITY_PRINCIPAL_H_
