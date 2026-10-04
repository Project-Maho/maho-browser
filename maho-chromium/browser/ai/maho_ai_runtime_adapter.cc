// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_adapter.h"

MahoAiRuntimeEvent::MahoAiRuntimeEvent() = default;
MahoAiRuntimeEvent::MahoAiRuntimeEvent(MahoAiRuntimeEvent&&) = default;
MahoAiRuntimeEvent& MahoAiRuntimeEvent::operator=(MahoAiRuntimeEvent&&) =
    default;
MahoAiRuntimeEvent::~MahoAiRuntimeEvent() = default;

// Defaults mirror the broker's CapabilityRequestContext defaults and the
// maho-ffi AgentRuntimeConfig::default(): guard / confirm-on / proactive-off.
MahoAiRuntimeConfig::MahoAiRuntimeConfig()
    : permission_tier("guard"), final_confirm(true), proactive_mode(false) {}
MahoAiRuntimeConfig::MahoAiRuntimeConfig(const MahoAiRuntimeConfig&) = default;
MahoAiRuntimeConfig& MahoAiRuntimeConfig::operator=(
    const MahoAiRuntimeConfig&) = default;
MahoAiRuntimeConfig::~MahoAiRuntimeConfig() = default;

void MahoAiRuntimeAdapter::RespondToInteraction(
    const std::string& interaction_id,
    const std::string& answer_json) {}

bool MahoAiRuntimeAdapter::GetRuntimeConfig(
    MahoAiRuntimeConfig* out_config) const {
  return false;
}

bool MahoAiRuntimeAdapter::SetRuntimeConfig(
    const MahoAiRuntimeConfig& config) {
  return false;
}

bool MahoAiRuntimeAdapter::ReplayAfter(const std::string& run_id,
                                       uint64_t after_seq,
                                       RuntimeEventCallback on_event) {
  return false;
}

uint64_t MahoAiRuntimeAdapter::GetLastConsumedEventSeq(
    const std::string& run_id) const {
  return 0;
}

bool MahoAiRuntimeAdapter::IsTurnActive() const {
  return false;
}

bool MahoAiRuntimeAdapter::SubmitFollowUp(const std::string& message,
                                         const std::string& intent) {
  return false;
}

uint32_t MahoAiRuntimeAdapter::GetTurnQueueDepth() const {
  return 0;
}

bool MahoAiRuntimeAdapter::WakeForNotification(const std::string& run_id,
                                              const std::string& event_json) {
  return false;
}

std::string MahoAiRuntimeAdapter::RegisterWait(const std::string& run_id,
                                              const std::string& filter_json,
                                              uint64_t timeout_ms) {
  return "";
}

void MahoAiRuntimeAdapter::StartSession(const std::string& session_id) {}

void MahoAiRuntimeAdapter::ResetSession(const std::string& session_id) {
  StartSession(session_id);
}

