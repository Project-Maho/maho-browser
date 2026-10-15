// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_channel_protocols.h"

#include <algorithm>
#include <utility>

#include "base/json/json_reader.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "maho/browser/ai/maho_agent_channel_gateway.h"

namespace maho::ai {

ChannelBackoff::ChannelBackoff(base::TimeDelta initial, base::TimeDelta cap)
    : initial_(initial), cap_(cap), current_(initial) {}

base::TimeDelta ChannelBackoff::OnFailure() {
  const base::TimeDelta result = current_;
  current_ = std::min(cap_, current_ * 2);
  return result;
}

void ChannelBackoff::OnSuccess() {
  current_ = initial_;
}

namespace {
std::string GeneratePairingCode() {
  return base::StringPrintf("%06d", base::RandInt(0, 999999));
}

const base::DictValue* AsDict(const base::Value* value) {
  return value ? value->GetIfDict() : nullptr;
}
}  // namespace

bool ParseTelegramUpdates(std::string_view json,
                          const ChannelConfig& config,
                          std::vector<ChannelEvent>* events,
                          std::string* next_offset,
                          std::string* error) {
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!root || !root->FindBool("ok").value_or(false)) {
    *error = "Telegram response was invalid";
    return false;
  }
  const base::ListValue* updates = root->FindList("result");
  if (!updates) {
    *error = "Telegram response did not contain updates";
    return false;
  }
  for (const base::Value& update_value : *updates) {
    const base::DictValue* update = AsDict(&update_value);
    const base::DictValue* message = update ? update->FindDict("message") : nullptr;
    const base::DictValue* from = message ? message->FindDict("from") : nullptr;
    const base::DictValue* chat = message ? message->FindDict("chat") : nullptr;
    const std::string* text = message ? message->FindString("text") : nullptr;
    if (!update || !message || !from || !chat || !text) continue;
    const std::optional<int> update_id = update->FindInt("update_id");
    const std::optional<int> sender_id = from->FindInt("id");
    const std::optional<int> chat_id = chat->FindInt("id");
    if (!update_id || !sender_id || !chat_id || *update_id < 0) continue;
    ChannelEvent event;
    event.id = base::NumberToString(*update_id);
    event.channel_id = config.id;
    event.provider = ChannelProvider::kTelegram;
    event.sender_id = base::NumberToString(*sender_id);
    event.reply_channel_id = base::NumberToString(*chat_id);
    if (const std::string* name = from->FindString("username")) {
      event.sender_name = *name;
    }
    event.text = *text;
    event.cursor = base::NumberToString(static_cast<uint64_t>(*update_id) + 1);
    event.timestamp = static_cast<uint64_t>(std::max(1, message->FindInt("date").value_or(1)));
    events->push_back(std::move(event));
    const uint64_t offset = static_cast<uint64_t>(*update_id) + 1;
    *next_offset = base::NumberToString(offset);
  }
  return true;
}

MahoAgentChannelPairing::MahoAgentChannelPairing(CodeGenerator generator)
    : generator_(generator ? std::move(generator) : CodeGenerator(&GeneratePairingCode)) {}

MahoAgentChannelPairing::~MahoAgentChannelPairing() = default;

std::string MahoAgentChannelPairing::IssueCode(const std::string& principal,
                                               base::TimeTicks now) {
  if (principal.empty() || IsPaired(principal)) {
    return std::string();
  }
  auto it = pending_.find(principal);
  if (it != pending_.end() && now < it->second.expires) {
    return it->second.value;
  }
  PendingCode code{generator_(), now + kCodeLifetime, 0};
  pending_.insert_or_assign(principal, code);
  return code.value;
}

MahoAgentChannelPairing::ConfirmResult MahoAgentChannelPairing::Confirm(
    const std::string& principal,
    std::string_view code,
    base::TimeTicks now) {
  auto it = pending_.find(principal);
  if (it == pending_.end()) {
    return ConfirmResult::kExpired;
  }
  if (now >= it->second.expires) {
    pending_.erase(it);
    return ConfirmResult::kExpired;
  }
  if (code != it->second.value) {
    if (++it->second.attempts >= kMaxAttempts) {
      pending_.erase(it);
      return ConfirmResult::kAttemptsExhausted;
    }
    return ConfirmResult::kWrongCode;
  }
  pending_.erase(it);
  paired_principals_.insert(principal);
  return ConfirmResult::kApproved;
}

bool MahoAgentChannelPairing::IsPaired(std::string_view principal) const {
  return paired_principals_.contains(principal);
}

bool MahoAgentChannelPairing::AuthorizeInbound(std::string_view principal) const {
  return IsPaired(principal);
}

void MahoAgentChannelPairing::Revoke(std::string_view principal) {
  if (auto it = paired_principals_.find(principal);
      it != paired_principals_.end()) {
    paired_principals_.erase(it);
  }
  if (auto it = pending_.find(principal); it != pending_.end()) {
    pending_.erase(it);
  }
}

bool SlackSocketProtocol::OnFrame(std::string_view frame) {
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(frame, base::JSON_PARSE_RFC);
  if (!root) return false;
  if (const std::string* envelope_id = root->FindString("envelope_id")) {
    base::DictValue ack;
    ack.Set("envelope_id", *envelope_id);
    acknowledgements_.push_back(base::WriteJson(ack).value_or("{}"));
  }
  if (const base::DictValue* payload = root->FindDict("payload")) {
    events_.push_back(base::WriteJson(*payload).value_or("{}"));
  } else if (root->FindString("type")) {
    events_.push_back(std::string(frame));
  }
  if (const std::string* type = root->FindString("type");
      type && *type == "disconnect") {
    reconnect_requested_ = true;
  }
  return true;
}

bool SlackSocketProtocol::OnHello(std::string_view frame) {
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(frame, base::JSON_PARSE_RFC);
  if (!root) return false;
  const base::DictValue* data = root->FindDict("data");
  if (!data) return false;
  heartbeat_interval_ms_ = static_cast<uint64_t>(
      data->FindInt("heartbeat_interval").value_or(0));
  return heartbeat_interval_ms_ > 0;
}

std::vector<std::string> SlackSocketProtocol::TakeEvents() {
  std::vector<std::string> result = std::move(events_);
  events_.clear();
  return result;
}

std::vector<std::string> SlackSocketProtocol::TakeAcknowledgements() {
  std::vector<std::string> result = std::move(acknowledgements_);
  acknowledgements_.clear();
  return result;
}

DiscordGatewayProtocol::DiscordGatewayProtocol(std::string token)
    : token_(std::move(token)) {}

void DiscordGatewayProtocol::SetToken(std::string token) {
  token_ = std::move(token);
}

DiscordGatewayProtocol::Decision DiscordGatewayProtocol::OnFrame(
    std::string_view frame) {
  Decision decision;
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(frame, base::JSON_PARSE_RFC);
  if (!root) return decision;
  const int op = root->FindInt("op").value_or(-1);
  const base::DictValue* data = root->FindDict("d");
  if (const std::optional<int> seq = root->FindInt("s")) {
    sequence_ = static_cast<uint64_t>(*seq);
  }
  if (op == 10 && data) {
    heartbeat_interval_ms_ = static_cast<uint64_t>(data->FindInt("heartbeat_interval").value_or(0));
    if (resume_requested_ && should_resume()) {
      std::vector<std::string> resume = ResumeFrames();
      decision.outbound_frames.insert(decision.outbound_frames.end(),
                                      resume.begin(), resume.end());
    } else {
      base::DictValue identify;
      identify.Set("op", 2);
      base::DictValue identify_data;
      identify_data.Set("token", token_);
      identify_data.Set("intents", 4609);
      base::DictValue properties;
      properties.Set("os", "browser");
      properties.Set("browser", "Maho");
      properties.Set("device", "Maho");
      identify_data.Set("properties", std::move(properties));
      identify.Set("d", std::move(identify_data));
      decision.outbound_frames.push_back(base::WriteJson(identify).value_or("{}"));
    }
    identified_ = true;
  } else if (op == 7) {
    resume_requested_ = true;
    decision.reconnect = true;
  } else if (op == 9) {
    decision.invalid_session = true;
    resume_requested_ = root->FindBool("d").value_or(false);
    if (!resume_requested_) {
      session_id_.clear();
      sequence_.reset();
    }
    decision.reconnect = true;
  } else if (op == 0 && data) {
    if (const std::string* type = root->FindString("t"); type && *type == "READY") {
      if (const std::string* id = data->FindString("session_id"))
        session_id_ = *id;
    }
  } else if (op == 11) {
  }
  return decision;
}

void DiscordGatewayProtocol::OnDisconnected() {
  identified_ = false;
}

std::vector<std::string> DiscordGatewayProtocol::ResumeFrames() const {
  if (!should_resume()) return {};
  base::DictValue resume;
  resume.Set("op", 6);
  base::DictValue data;
  data.Set("token", token_);
  data.Set("session_id", session_id_);
  data.Set("seq", static_cast<int>(*sequence_));
  resume.Set("d", std::move(data));
  return {base::WriteJson(resume).value_or("{}")};
}

}  // namespace maho::ai
