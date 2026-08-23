// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_capability_principal.h"

#include <utility>

namespace maho::ai {

ExecutableIdentity::ExecutableIdentity() = default;
ExecutableIdentity::ExecutableIdentity(const ExecutableIdentity&) = default;
ExecutableIdentity::ExecutableIdentity(ExecutableIdentity&&) noexcept = default;
ExecutableIdentity& ExecutableIdentity::operator=(const ExecutableIdentity&) = default;
ExecutableIdentity& ExecutableIdentity::operator=(ExecutableIdentity&&) noexcept = default;
ExecutableIdentity::~ExecutableIdentity() = default;

DeviceIdentity::DeviceIdentity() = default;
DeviceIdentity::DeviceIdentity(const DeviceIdentity&) = default;
DeviceIdentity::DeviceIdentity(DeviceIdentity&&) noexcept = default;
DeviceIdentity& DeviceIdentity::operator=(const DeviceIdentity&) = default;
DeviceIdentity& DeviceIdentity::operator=(DeviceIdentity&&) noexcept = default;
DeviceIdentity::~DeviceIdentity() = default;

CapabilityPrincipal::CapabilityPrincipal() = default;
CapabilityPrincipal::CapabilityPrincipal(const CapabilityPrincipal&) = default;
CapabilityPrincipal::CapabilityPrincipal(CapabilityPrincipal&&) noexcept = default;
CapabilityPrincipal& CapabilityPrincipal::operator=(const CapabilityPrincipal&) = default;
CapabilityPrincipal& CapabilityPrincipal::operator=(CapabilityPrincipal&&) noexcept = default;
CapabilityPrincipal::~CapabilityPrincipal() = default;

}  // namespace maho::ai
