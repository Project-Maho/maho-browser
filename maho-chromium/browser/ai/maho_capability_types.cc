// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_capability_types.h"

namespace maho::ai {

CapabilityDescriptor::CapabilityDescriptor() = default;
CapabilityDescriptor::CapabilityDescriptor(const CapabilityDescriptor&) =
    default;
CapabilityDescriptor::CapabilityDescriptor(CapabilityDescriptor&&) noexcept =
    default;
CapabilityDescriptor& CapabilityDescriptor::operator=(
    const CapabilityDescriptor&) = default;
CapabilityDescriptor& CapabilityDescriptor::operator=(
    CapabilityDescriptor&&) noexcept = default;
CapabilityDescriptor::~CapabilityDescriptor() = default;

}  // namespace maho::ai
