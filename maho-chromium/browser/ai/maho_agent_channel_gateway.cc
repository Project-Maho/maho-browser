// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_channel_gateway.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "base/strings/string_number_conversions.h"

#if !defined(MAHO_STANDALONE_TEST)
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_credential_redaction.h"
#endif

namespace maho::ai {

ChannelHealth::ChannelHealth() = default;
ChannelHealth::~ChannelHealth() = default;
ChannelHealth::ChannelHealth(const ChannelHealth&) = default;
ChannelHealth& ChannelHealth::operator=(const ChannelHealth&) = default;
ChannelHealth::ChannelHealth(ChannelHealth&&) noexcept = default;
ChannelHealth& ChannelHealth::operator=(ChannelHealth&&) noexcept = default;

ChannelEvent::ChannelEvent() = default;
ChannelEvent::~ChannelEvent() = default;
ChannelEvent::ChannelEvent(const ChannelEvent&) = default;
ChannelEvent& ChannelEvent::operator=(const ChannelEvent&) = default;
ChannelEvent::ChannelEvent(ChannelEvent&&) noexcept = default;
ChannelEvent& ChannelEvent::operator=(ChannelEvent&&) noexcept = default;

AgentSessionTrigger::AgentSessionTrigger() = default;
AgentSessionTrigger::~AgentSessionTrigger() = default;
AgentSessionTrigger::AgentSessionTrigger(const AgentSessionTrigger&) = default;
AgentSessionTrigger& AgentSessionTrigger::operator=(const AgentSessionTrigger&) = default;
AgentSessionTrigger::AgentSessionTrigger(AgentSessionTrigger&&) noexcept = default;
AgentSessionTrigger& AgentSessionTrigger::operator=(AgentSessionTrigger&&) noexcept = default;

InboundProcessOutcome::InboundProcessOutcome() = default;
InboundProcessOutcome::~InboundProcessOutcome() = default;
InboundProcessOutcome::InboundProcessOutcome(const InboundProcessOutcome&) = default;
InboundProcessOutcome& InboundProcessOutcome::operator=(const InboundProcessOutcome&) = default;
InboundProcessOutcome::InboundProcessOutcome(InboundProcessOutcome&&) noexcept = default;
InboundProcessOutcome& InboundProcessOutcome::operator=(InboundProcessOutcome&&) noexcept = default;

OutboundChannelMessage::OutboundChannelMessage() = default;
OutboundChannelMessage::~OutboundChannelMessage() = default;
OutboundChannelMessage::OutboundChannelMessage(const OutboundChannelMessage&) = default;
OutboundChannelMessage& OutboundChannelMessage::operator=(const OutboundChannelMessage&) = default;
OutboundChannelMessage::OutboundChannelMessage(OutboundChannelMessage&&) noexcept = default;
OutboundChannelMessage& OutboundChannelMessage::operator=(OutboundChannelMessage&&) noexcept = default;

OutboundSendReceipt::OutboundSendReceipt() = default;
OutboundSendReceipt::~OutboundSendReceipt() = default;
OutboundSendReceipt::OutboundSendReceipt(const OutboundSendReceipt&) = default;
OutboundSendReceipt& OutboundSendReceipt::operator=(const OutboundSendReceipt&) = default;
OutboundSendReceipt::OutboundSendReceipt(OutboundSendReceipt&&) noexcept = default;
OutboundSendReceipt& OutboundSendReceipt::operator=(OutboundSendReceipt&&) noexcept = default;

ChannelEntry::ChannelEntry() = default;
ChannelEntry::~ChannelEntry() = default;
ChannelEntry::ChannelEntry(const ChannelEntry&) = default;
ChannelEntry& ChannelEntry::operator=(const ChannelEntry&) = default;
ChannelEntry::ChannelEntry(ChannelEntry&&) noexcept = default;
ChannelEntry& ChannelEntry::operator=(ChannelEntry&&) noexcept = default;


namespace {

std::string TrimWhitespace(std::string_view s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

std::string EscapeJson(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          output += buf;
        } else {
          output += c;
        }
        break;
    }
  }
  return output;
}

std::optional<std::string> ExtractJsonField(std::string_view json,
                                            std::string_view field_name) {
  std::string search = "\"" + std::string(field_name) + "\"";
  size_t key_pos = json.find(search);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  size_t colon_pos = json.find(':', key_pos + search.size());
  if (colon_pos == std::string_view::npos) {
    return std::nullopt;
  }
  size_t val_start = json.find_first_not_of(" \t\r\n", colon_pos + 1);
  if (val_start == std::string_view::npos) {
    return std::nullopt;
  }
  if (json[val_start] == '\"') {
    size_t val_end = val_start + 1;
    bool escaped = false;
    while (val_end < json.size()) {
      if (escaped) {
        escaped = false;
      } else if (json[val_end] == '\\') {
        escaped = true;
      } else if (json[val_end] == '\"') {
        break;
      }
      ++val_end;
    }
    if (val_end >= json.size()) {
      return std::nullopt;
    }
    return std::string(json.substr(val_start + 1, val_end - val_start - 1));
  }
  if (json.substr(val_start, 4) == "null") {
    return std::nullopt;
  }
  size_t val_end = json.find_first_of(",}\n\r", val_start);
  if (val_end == std::string_view::npos) {
    val_end = json.size();
  }
  return TrimWhitespace(json.substr(val_start, val_end - val_start));
}

}  // namespace

// ChannelProvider String Conversions
std::string_view ChannelProviderToString(ChannelProvider provider) {
  switch (provider) {
    case ChannelProvider::kSlack: return "slack";
    case ChannelProvider::kDiscord: return "discord";
    case ChannelProvider::kTelegram: return "telegram";
    case ChannelProvider::kCustom: return "custom";
  }
  return "custom";
}

ChannelProvider ChannelProviderFromString(std::string_view name) {
  if (name == "slack") return ChannelProvider::kSlack;
  if (name == "discord") return ChannelProvider::kDiscord;
  if (name == "telegram") return ChannelProvider::kTelegram;
  return ChannelProvider::kCustom;
}

// ChannelEventKind String Conversions
std::string_view ChannelEventKindToString(ChannelEventKind kind) {
  switch (kind) {
    case ChannelEventKind::kMessageReceived: return "message_received";
    case ChannelEventKind::kReactionAdded: return "reaction_added";
    case ChannelEventKind::kThreadReply: return "thread_reply";
    case ChannelEventKind::kChannelJoined: return "channel_joined";
  }
  return "message_received";
}

ChannelEventKind ChannelEventKindFromString(std::string_view name) {
  if (name == "message_received") return ChannelEventKind::kMessageReceived;
  if (name == "reaction_added") return ChannelEventKind::kReactionAdded;
  if (name == "thread_reply") return ChannelEventKind::kThreadReply;
  if (name == "channel_joined") return ChannelEventKind::kChannelJoined;
  return ChannelEventKind::kMessageReceived;
}

// ChannelHealthStatus String Conversions
std::string_view ChannelHealthStatusToString(ChannelHealthStatus status) {
  switch (status) {
    case ChannelHealthStatus::kHealthy: return "healthy";
    case ChannelHealthStatus::kStale: return "stale";
    case ChannelHealthStatus::kDegraded: return "degraded";
    case ChannelHealthStatus::kDisconnected: return "disconnected";
    case ChannelHealthStatus::kUnconfigured: return "unconfigured";
  }
  return "unconfigured";
}

ChannelHealthStatus ChannelHealthStatusFromString(std::string_view name) {
  if (name == "healthy") return ChannelHealthStatus::kHealthy;
  if (name == "stale") return ChannelHealthStatus::kStale;
  if (name == "degraded") return ChannelHealthStatus::kDegraded;
  if (name == "disconnected") return ChannelHealthStatus::kDisconnected;
  return ChannelHealthStatus::kUnconfigured;
}

// ChannelToken Implementation
ChannelToken::ChannelToken(std::string secret) : secret_(std::move(secret)) {}

ChannelToken::~ChannelToken() {
  for (char& c : secret_) {
    c = '\0';
  }
}

ChannelToken::ChannelToken(const ChannelToken& other) : secret_(other.secret_) {}

ChannelToken& ChannelToken::operator=(const ChannelToken& other) {
  if (this != &other) {
    for (char& c : secret_) {
      c = '\0';
    }
    secret_ = other.secret_;
  }
  return *this;
}

ChannelToken::ChannelToken(ChannelToken&& other) noexcept
    : secret_(std::move(other.secret_)) {}

ChannelToken& ChannelToken::operator=(ChannelToken&& other) noexcept {
  if (this != &other) {
    for (char& c : secret_) {
      c = '\0';
    }
    secret_ = std::move(other.secret_);
  }
  return *this;
}

// ChannelConfig Implementation
ChannelConfig::ChannelConfig() = default;

ChannelConfig::ChannelConfig(std::string id,
                             ChannelProvider provider,
                             std::string name,
                             bool enabled)
    : id(std::move(id)),
      provider(provider),
      name(std::move(name)),
      enabled(enabled) {}

ChannelConfig::~ChannelConfig() = default;
ChannelConfig::ChannelConfig(const ChannelConfig&) = default;
ChannelConfig& ChannelConfig::operator=(const ChannelConfig&) = default;
ChannelConfig::ChannelConfig(ChannelConfig&&) noexcept = default;
ChannelConfig& ChannelConfig::operator=(ChannelConfig&&) noexcept = default;

bool ChannelConfig::operator==(const ChannelConfig& other) const {
  return id == other.id &&
         provider == other.provider &&
         name == other.name &&
         enabled == other.enabled &&
         token == other.token &&
         account_id == other.account_id &&
         opaque_auth_handle == other.opaque_auth_handle &&
         poll_interval_seconds == other.poll_interval_seconds;
}

std::string ChannelConfig::ToDebugString() const {
  std::ostringstream ss;
  ss << "ChannelConfig{id=\"" << id
     << "\", provider=\"" << ChannelProviderToString(provider)
     << "\", name=\"" << name
     << "\", enabled=" << (enabled ? "true" : "false")
     << ", token=" << (token.has_value() ? "[REDACTED]" : "none")
     << ", account_id=" << (account_id ? *account_id : "none")
     << ", opaque_auth_handle=" << (opaque_auth_handle ? *opaque_auth_handle : "none")
     << ", poll_interval_seconds=" << poll_interval_seconds << "}";
  return ss.str();
}

std::string ChannelConfig::ToJson(bool redact_secrets) const {
  std::ostringstream ss;
  ss << "{"
     << "\"id\":\"" << EscapeJson(id) << "\","
     << "\"provider\":\"" << ChannelProviderToString(provider) << "\","
     << "\"name\":\"" << EscapeJson(name) << "\","
     << "\"enabled\":" << (enabled ? "true" : "false") << ",";
  if (token.has_value()) {
    if (redact_secrets) {
      ss << "\"token\":\"[REDACTED]\",";
    } else {
      ss << "\"token\":\"" << EscapeJson(token->expose_secret()) << "\",";
    }
  }
  if (account_id.has_value()) {
    ss << "\"account_id\":\"" << EscapeJson(*account_id) << "\",";
  }
  if (opaque_auth_handle.has_value()) {
    ss << "\"opaque_auth_handle\":\"" << EscapeJson(*opaque_auth_handle) << "\",";
  }
  ss << "\"poll_interval_seconds\":" << poll_interval_seconds
     << "}";
  return ss.str();
}

base::expected<ChannelConfig, std::string> ChannelConfig::FromJson(
    std::string_view json_str) {
  auto id_opt = ExtractJsonField(json_str, "id");
  if (!id_opt || id_opt->empty()) {
    return base::unexpected("Missing channel id in config JSON");
  }
  auto prov_opt = ExtractJsonField(json_str, "provider");
  ChannelProvider prov = prov_opt ? ChannelProviderFromString(*prov_opt)
                                  : ChannelProvider::kSlack;
  auto name_opt = ExtractJsonField(json_str, "name");
  std::string name = name_opt.value_or("");
  auto enabled_opt = ExtractJsonField(json_str, "enabled");
  bool enabled = enabled_opt.value_or("true") != "false";

  ChannelConfig config(std::move(*id_opt), prov, std::move(name), enabled);
  auto tok_opt = ExtractJsonField(json_str, "token");
  if (tok_opt && !tok_opt->empty() && *tok_opt != "[REDACTED]") {
    config.token = ChannelToken(std::move(*tok_opt));
  }
  auto acc_opt = ExtractJsonField(json_str, "account_id");
  if (acc_opt && !acc_opt->empty()) {
    config.account_id = std::move(acc_opt);
  }
  auto auth_opt = ExtractJsonField(json_str, "opaque_auth_handle");
  if (auth_opt && !auth_opt->empty()) {
    config.opaque_auth_handle = std::move(auth_opt);
  }
  auto poll_opt = ExtractJsonField(json_str, "poll_interval_seconds");
  if (poll_opt) {
    uint32_t parsed_poll = 0;
    if (base::StringToUint(*poll_opt, &parsed_poll)) {
      config.poll_interval_seconds = parsed_poll;
    }
  }
  return config;
}

// ChannelEvent Implementation
bool ChannelEvent::operator==(const ChannelEvent& other) const {
  return id == other.id &&
         channel_id == other.channel_id &&
         provider == other.provider &&
         kind == other.kind &&
         sender_id == other.sender_id &&
         sender_name == other.sender_name &&
         text == other.text &&
         cursor == other.cursor &&
         timestamp == other.timestamp;
}

std::string ChannelEvent::ToJson() const {
  std::ostringstream ss;
  ss << "{"
     << "\"id\":\"" << EscapeJson(id) << "\","
     << "\"channel_id\":\"" << EscapeJson(channel_id) << "\","
     << "\"provider\":\"" << ChannelProviderToString(provider) << "\","
     << "\"kind\":\"" << ChannelEventKindToString(kind) << "\","
     << "\"sender_id\":\"" << EscapeJson(sender_id) << "\",";
  if (sender_name.has_value()) {
    ss << "\"sender_name\":\"" << EscapeJson(*sender_name) << "\",";
  }
  ss << "\"text\":\"" << EscapeJson(text) << "\","
     << "\"cursor\":\"" << EscapeJson(cursor) << "\","
     << "\"timestamp\":" << timestamp
     << "}";
  return ss.str();
}

// AgentSessionTrigger Implementation
bool AgentSessionTrigger::operator==(const AgentSessionTrigger& other) const {
  return channel_id == other.channel_id &&
         provider == other.provider &&
         sender_id == other.sender_id &&
         sender_name == other.sender_name &&
         message_text == other.message_text &&
         event_id == other.event_id &&
         cursor == other.cursor &&
         timestamp == other.timestamp;
}

std::string AgentSessionTrigger::ToJson() const {
  std::ostringstream ss;
  ss << "{"
     << "\"channel_id\":\"" << EscapeJson(channel_id) << "\","
     << "\"provider\":\"" << ChannelProviderToString(provider) << "\","
     << "\"sender_id\":\"" << EscapeJson(sender_id) << "\",";
  if (sender_name.has_value()) {
    ss << "\"sender_name\":\"" << EscapeJson(*sender_name) << "\",";
  }
  ss << "\"message_text\":\"" << EscapeJson(message_text) << "\","
     << "\"event_id\":\"" << EscapeJson(event_id) << "\","
     << "\"cursor\":\"" << EscapeJson(cursor) << "\","
     << "\"timestamp\":" << timestamp
     << "}";
  return ss.str();
}

std::string AgentSessionTrigger::ToFollowUpPrompt() const {
  std::ostringstream ss;
  ss << "[Channel Inbound: " << ChannelProviderToString(provider) << " #" << channel_id << "] ";
  if (sender_name.has_value() && !sender_name->empty()) {
    ss << *sender_name << " (" << sender_id << "): ";
  } else {
    ss << sender_id << ": ";
  }
  ss << message_text;
  return ss.str();
}

// InboundProcessOutcome Factories
InboundProcessOutcome InboundProcessOutcome::Triggered(AgentSessionTrigger trigger) {
  InboundProcessOutcome o;
  o.status = Status::kTriggered;
  o.cursor = trigger.cursor;
  o.channel_id = trigger.channel_id;
  o.trigger = std::move(trigger);
  return o;
}

InboundProcessOutcome InboundProcessOutcome::DuplicateCursor(std::string cursor) {
  InboundProcessOutcome o;
  o.status = Status::kDuplicateCursor;
  o.cursor = std::move(cursor);
  return o;
}

InboundProcessOutcome InboundProcessOutcome::MalformedEvent(std::string reason) {
  InboundProcessOutcome o;
  o.status = Status::kMalformedEvent;
  o.reason = std::move(reason);
  return o;
}

InboundProcessOutcome InboundProcessOutcome::ChannelNotFound(std::string channel_id) {
  InboundProcessOutcome o;
  o.status = Status::kChannelNotFound;
  o.channel_id = std::move(channel_id);
  return o;
}

InboundProcessOutcome InboundProcessOutcome::ChannelDisabled(std::string channel_id) {
  InboundProcessOutcome o;
  o.status = Status::kChannelDisabled;
  o.channel_id = std::move(channel_id);
  return o;
}

// ChannelError Factories
ChannelError ChannelError::MakeNotFound(std::string channel_id) {
  ChannelError err;
  err.type = Type::kChannelNotFound;
  err.message = "Channel not found: " + channel_id;
  return err;
}

ChannelError ChannelError::MakeDisabled(std::string channel_id) {
  ChannelError err;
  err.type = Type::kChannelDisabled;
  err.message = "Channel disabled: " + channel_id;
  return err;
}

ChannelError ChannelError::MakeUnsupportedProvider(ChannelProvider provider) {
  ChannelError err;
  err.type = Type::kUnsupportedProvider;
  err.message = "Unsupported provider: " + std::string(ChannelProviderToString(provider));
  return err;
}

ChannelError ChannelError::MakeUnavailable(std::string reason, bool can_fallback_to_tabs) {
  ChannelError err;
  err.type = Type::kUnavailable;
  err.message = "Direct API unavailable: " + reason;
  err.can_fallback_to_tabs = can_fallback_to_tabs;
  return err;
}

ChannelError ChannelError::MakeHardFailure(std::string code,
                                           std::string message,
                                           bool is_policy_denial) {
  ChannelError err;
  err.type = Type::kHardFailure;
  err.code = std::move(code);
  err.message = std::move(message);
  err.is_policy_denial = is_policy_denial;
  return err;
}

ChannelError ChannelError::MakeMalformed(std::string reason) {
  ChannelError err;
  err.type = Type::kMalformedEvent;
  err.message = "Malformed event: " + reason;
  return err;
}

// Cursor Comparison
int CompareCursorPositions(std::string_view a, std::string_view b) {
  if (a == b) {
    return 0;
  }

  auto parse_u64 = [](std::string_view s) -> std::optional<uint64_t> {
    if (s.empty()) return std::nullopt;
    uint64_t val = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return std::nullopt;
      val = val * 10 + static_cast<uint64_t>(c - '0');
    }
    return val;
  };

  auto num_a = parse_u64(a);
  auto num_b = parse_u64(b);
  if (num_a && num_b) {
    if (*num_a < *num_b) return -1;
    if (*num_a > *num_b) return 1;
    return 0;
  }

  auto strip_prefix = [parse_u64](std::string_view s) -> std::optional<std::pair<char, uint64_t>> {
    if (s.size() < 2) return std::nullopt;
    char first = s[0];
    if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
      auto rest = parse_u64(s.substr(1));
      if (rest) {
        return std::make_pair(first, *rest);
      }
    }
    return std::nullopt;
  };

  auto pref_a = strip_prefix(a);
  auto pref_b = strip_prefix(b);
  if (pref_a && pref_b && pref_a->first == pref_b->first) {
    if (pref_a->second < pref_b->second) return -1;
    if (pref_a->second > pref_b->second) return 1;
    return 0;
  }

  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

// SendViaDirectApi
base::expected<DirectApiOp, ChannelError> SendViaDirectApi(
    const ChannelConfig& channel,
    const OutboundChannelMessage& message) {
  if (!channel.enabled) {
    return base::unexpected(ChannelError::MakeDisabled(channel.id));
  }
  if (channel.id != message.channel_id) {
    return base::unexpected(ChannelError::MakeNotFound(message.channel_id));
  }

  DirectApiServiceKind service = DirectApiServiceKind::kUnknown;
  switch (message.provider) {
    case ChannelProvider::kSlack:
      service = DirectApiServiceKind::kSlack;
      break;
    case ChannelProvider::kDiscord:
      service = DirectApiServiceKind::kDiscord;
      break;
    case ChannelProvider::kTelegram:
      service = DirectApiServiceKind::kTelegram;
      break;
    case ChannelProvider::kCustom:
      return base::unexpected(ChannelError::MakeUnsupportedProvider(message.provider));
  }

  std::ostringstream params_ss;
  params_ss << "{"
            << "\"channel_id\":\"" << EscapeJson(message.channel_id) << "\","
            << "\"text\":\"" << EscapeJson(message.text) << "\"";
  if (message.reply_to_event_id.has_value() && !message.reply_to_event_id->empty()) {
    params_ss << ",\"reply_to_event_id\":\"" << EscapeJson(*message.reply_to_event_id) << "\"";
  }
  params_ss << "}";

  DirectApiOp op;
  op.operation.service = service;
  op.operation.operation_name = "channels.send_message";
  op.operation.parameters_json = params_ss.str();
  op.operation.read_only = false;
  op.requires_confirmation = true;

  op.context.session_id = "channel-session-" + message.channel_id;
  if (channel.account_id.has_value()) {
    op.context.account_id = *channel.account_id;
  }
  if (channel.opaque_auth_handle.has_value()) {
    op.context.opaque_auth_handle = *channel.opaque_auth_handle;
  }

  return op;
}

// HandleDirectApiOutcome
base::expected<OutboundSendReceipt, ChannelError> HandleDirectApiOutcome(
    const DirectApiExecutionOutcome& outcome) {
  if (outcome.status == DirectApiOutcomeStatus::kOk) {
    OutboundSendReceipt receipt;
    auto ch_opt = ExtractJsonField(outcome.payload_json, "channel_id");
    receipt.channel_id = ch_opt.value_or("unknown");
    receipt.message_id = ExtractJsonField(outcome.payload_json, "message_id");
    auto ts_opt = ExtractJsonField(outcome.payload_json, "timestamp");
    if (ts_opt) {
      uint64_t parsed_ts = 0;
      if (base::StringToUint64(*ts_opt, &parsed_ts)) {
        receipt.timestamp = parsed_ts;
      } else {
        receipt.timestamp = 0;
      }
    }
    return receipt;
  }

  if (outcome.status == DirectApiOutcomeStatus::kTypedUnavailable) {
    return base::unexpected(ChannelError::MakeUnavailable(
        outcome.reason, outcome.can_fallback_to_tabs));
  }

  std::string code_str = std::string(ErrorCodeToString(outcome.error_code));
  if (code_str == "ok") {
    code_str = outcome.status == DirectApiOutcomeStatus::kForbidden ? "FORBIDDEN" : "ERROR";
  }
  return base::unexpected(ChannelError::MakeHardFailure(
      code_str,
      outcome.error_message.empty() ? outcome.reason : outcome.error_message,
      outcome.is_policy_denial));
}

// ChannelRegistry Implementation
ChannelRegistry::ChannelRegistry() = default;
ChannelRegistry::~ChannelRegistry() = default;
ChannelRegistry::ChannelRegistry(const ChannelRegistry&) = default;
ChannelRegistry& ChannelRegistry::operator=(const ChannelRegistry&) = default;
ChannelRegistry::ChannelRegistry(ChannelRegistry&&) noexcept = default;
ChannelRegistry& ChannelRegistry::operator=(ChannelRegistry&&) noexcept = default;

void ChannelRegistry::RegisterChannel(ChannelConfig config) {
  std::string id = config.id;
  auto it = channels_.find(id);
  if (it != channels_.end()) {
    it->second.config = std::move(config);
  } else {
    ChannelEntry entry;
    entry.config = std::move(config);
    channels_[id] = std::move(entry);
  }
}

std::optional<ChannelConfig> ChannelRegistry::UnregisterChannel(
    const std::string& channel_id) {
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return std::nullopt;
  }
  ChannelConfig config = std::move(it->second.config);
  channels_.erase(it);
  return config;
}

const ChannelConfig* ChannelRegistry::GetChannel(
    const std::string& channel_id) const {
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return nullptr;
  }
  return &it->second.config;
}

ChannelConfig* ChannelRegistry::GetChannelMutable(
    const std::string& channel_id) {
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return nullptr;
  }
  return &it->second.config;
}

std::vector<ChannelConfig> ChannelRegistry::GetAllChannels() const {
  std::vector<ChannelConfig> result;
  result.reserve(channels_.size());
  for (const auto& [_, entry] : channels_) {
    result.push_back(entry.config);
  }
  return result;
}

std::optional<std::string> ChannelRegistry::GetLastCursor(
    const std::string& channel_id) const {
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return std::nullopt;
  }
  return it->second.last_cursor;
}

void ChannelRegistry::SetLastCursor(const std::string& channel_id,
                                    std::string cursor) {
  auto it = channels_.find(channel_id);
  if (it != channels_.end()) {
    it->second.seen_cursors.insert(cursor);
    it->second.last_cursor = std::move(cursor);
  }
}

void ChannelRegistry::RecordPoll(const std::string& channel_id,
                                 uint64_t timestamp) {
  auto it = channels_.find(channel_id);
  if (it != channels_.end()) {
    it->second.last_poll_timestamp = timestamp;
  }
}

InboundProcessOutcome ChannelRegistry::ProcessInboundEvent(
    const ChannelEvent& event) {
  // Validate malformed event fields
  if (TrimWhitespace(event.id).empty()) {
    return InboundProcessOutcome::MalformedEvent("Event id is empty");
  }
  if (TrimWhitespace(event.channel_id).empty()) {
    return InboundProcessOutcome::MalformedEvent("Channel id is empty");
  }
  if (TrimWhitespace(event.sender_id).empty()) {
    return InboundProcessOutcome::MalformedEvent("Sender id is empty");
  }
  if (TrimWhitespace(event.cursor).empty()) {
    return InboundProcessOutcome::MalformedEvent("Cursor position is empty");
  }
  if (event.timestamp == 0) {
    return InboundProcessOutcome::MalformedEvent("Event timestamp is zero or invalid");
  }

  auto it = channels_.find(event.channel_id);
  if (it == channels_.end()) {
    return InboundProcessOutcome::ChannelNotFound(event.channel_id);
  }

  ChannelEntry& entry = it->second;
  if (!entry.config.enabled) {
    return InboundProcessOutcome::ChannelDisabled(event.channel_id);
  }

  // Deduplication check: seen set or <= last_cursor
  if (entry.seen_cursors.find(event.cursor) != entry.seen_cursors.end()) {
    return InboundProcessOutcome::DuplicateCursor(event.cursor);
  }

  if (entry.last_cursor.has_value()) {
    if (CompareCursorPositions(event.cursor, *entry.last_cursor) <= 0) {
      return InboundProcessOutcome::DuplicateCursor(event.cursor);
    }
  }

  // Update cursor tracking
  entry.seen_cursors.insert(event.cursor);
  entry.last_cursor = event.cursor;
  entry.last_poll_timestamp = event.timestamp;

  AgentSessionTrigger trigger;
  trigger.channel_id = event.channel_id;
  trigger.provider = event.provider;
  trigger.sender_id = event.sender_id;
  trigger.sender_name = event.sender_name;
  trigger.message_text = event.text;
  trigger.event_id = event.id;
  trigger.cursor = event.cursor;
  trigger.timestamp = event.timestamp;

  return InboundProcessOutcome::Triggered(std::move(trigger));
}

ChannelHealth ChannelRegistry::EvaluateHealth(
    const std::string& channel_id,
    uint64_t now_timestamp_secs,
    uint64_t stale_threshold_secs) const {
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    ChannelHealth h;
    h.status = ChannelHealthStatus::kUnconfigured;
    h.details = "channel not registered";
    return h;
  }

  const ChannelEntry& entry = it->second;
  if (!entry.config.enabled) {
    ChannelHealth h;
    h.status = ChannelHealthStatus::kUnconfigured;
    h.last_poll_timestamp = entry.last_poll_timestamp;
    h.details = "channel disabled";
    return h;
  }

  if (!entry.config.token.has_value() && !entry.config.opaque_auth_handle.has_value()) {
    ChannelHealth h;
    h.status = ChannelHealthStatus::kUnconfigured;
    h.last_poll_timestamp = entry.last_poll_timestamp;
    h.details = "no credentials or auth handle configured";
    return h;
  }

  if (!entry.last_poll_timestamp.has_value()) {
    ChannelHealth h;
    h.status = ChannelHealthStatus::kDisconnected;
    h.details = "channel has not polled yet";
    return h;
  }

  uint64_t last_poll = *entry.last_poll_timestamp;
  uint64_t diff = (now_timestamp_secs > last_poll) ? (now_timestamp_secs - last_poll) : 0;
  if (diff > stale_threshold_secs) {
    ChannelHealth h;
    h.status = ChannelHealthStatus::kStale;
    h.last_poll_timestamp = last_poll;
    std::ostringstream ss;
    ss << "poll interval exceeded: " << diff << "s since last poll (threshold: "
       << stale_threshold_secs << "s)";
    h.details = ss.str();
    return h;
  }

  ChannelHealth h;
  h.status = ChannelHealthStatus::kHealthy;
  h.last_poll_timestamp = last_poll;
  return h;
}

std::string ChannelRegistry::SerializeToJson(bool redact_secrets) const {
  std::ostringstream ss;
  ss << "{\"channels\":{";
  bool first = true;
  for (const auto& [id, entry] : channels_) {
    if (!first) ss << ",";
    first = false;
    ss << "\"" << EscapeJson(id) << "\":{";
    ss << "\"config\":" << entry.config.ToJson(redact_secrets);
    if (entry.last_cursor.has_value()) {
      ss << ",\"last_cursor\":\"" << EscapeJson(*entry.last_cursor) << "\"";
    }
    if (entry.last_poll_timestamp.has_value()) {
      ss << ",\"last_poll_timestamp\":" << *entry.last_poll_timestamp;
    }
    ss << "}";
  }
  ss << "}}";
  return ss.str();
}

base::expected<ChannelRegistry, std::string> ChannelRegistry::DeserializeFromJson(
    std::string_view json_str) {
  ChannelRegistry reg;
  size_t ch_key = json_str.find("\"channels\"");
  if (ch_key == std::string_view::npos) {
    return reg;
  }
  size_t brace = json_str.find('{', ch_key);
  if (brace == std::string_view::npos) {
    return reg;
  }

  // Scan channel objects
  size_t pos = brace + 1;
  while (pos < json_str.size()) {
    size_t key_quote = json_str.find('\"', pos);
    if (key_quote == std::string_view::npos) break;
    size_t key_end = json_str.find('\"', key_quote + 1);
    if (key_end == std::string_view::npos) break;
    std::string ch_id(json_str.substr(key_quote + 1, key_end - key_quote - 1));

    size_t obj_open = json_str.find('{', key_end);
    if (obj_open == std::string_view::npos) break;
    int depth = 1;
    size_t obj_close = obj_open + 1;
    while (obj_close < json_str.size() && depth > 0) {
      if (json_str[obj_close] == '{') depth++;
      else if (json_str[obj_close] == '}') depth--;
      obj_close++;
    }
    std::string_view entry_json = json_str.substr(obj_open, obj_close - obj_open);

    size_t cfg_key = entry_json.find("\"config\"");
    if (cfg_key != std::string_view::npos) {
      size_t cfg_brace = entry_json.find('{', cfg_key);
      if (cfg_brace != std::string_view::npos) {
        int cdepth = 1;
        size_t cfg_close = cfg_brace + 1;
        while (cfg_close < entry_json.size() && cdepth > 0) {
          if (entry_json[cfg_close] == '{') cdepth++;
          else if (entry_json[cfg_close] == '}') cdepth--;
          cfg_close++;
        }
        std::string_view cfg_str = entry_json.substr(cfg_brace, cfg_close - cfg_brace);
        auto parsed_cfg = ChannelConfig::FromJson(cfg_str);
        if (parsed_cfg.has_value()) {
          reg.RegisterChannel(std::move(*parsed_cfg));
        }
      }
    }

    auto cursor_opt = ExtractJsonField(entry_json, "last_cursor");
    if (cursor_opt && !cursor_opt->empty()) {
      reg.SetLastCursor(ch_id, std::move(*cursor_opt));
    }
    auto ts_opt = ExtractJsonField(entry_json, "last_poll_timestamp");
    if (ts_opt) {
      uint64_t ts = 0;
      if (base::StringToUint64(*ts_opt, &ts)) {
        reg.RecordPoll(ch_id, ts);
      }
    }

    pos = obj_close;
  }

  return reg;
}

// DefaultChannelHttpTransport Stub Implementation
DefaultChannelHttpTransport::DefaultChannelHttpTransport() = default;
DefaultChannelHttpTransport::~DefaultChannelHttpTransport() = default;

void DefaultChannelHttpTransport::FetchEvents(
    const ChannelConfig& config,
    const std::optional<std::string>& cursor,
    FetchCallback callback) {
  // TODO: Wire real HTTP transport with network::SharedURLLoaderFactory
  // when external provider endpoints are configured for live traffic.
  std::vector<ChannelEvent> empty_events;
  std::move(callback).Run(std::move(empty_events), std::nullopt);
}

// MahoAgentChannelGateway Implementation
MahoAgentChannelGateway::MahoAgentChannelGateway()
    : MahoAgentChannelGateway(std::make_unique<DefaultChannelHttpTransport>(),
                              nullptr,
                              {}) {}

MahoAgentChannelGateway::MahoAgentChannelGateway(
    std::unique_ptr<ChannelTransport> transport,
    MahoAuthenticatedServiceApiBroker* broker,
    base::RepeatingCallback<uint64_t()> clock_source)
    : transport_(std::move(transport)),
      broker_(broker),
      poll_interval_(base::Seconds(30)),
      clock_source_(std::move(clock_source)) {}

MahoAgentChannelGateway::~MahoAgentChannelGateway() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  StopAll();
}

void MahoAgentChannelGateway::RegisterChannel(ChannelConfig config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  registry_.RegisterChannel(std::move(config));
  SavePersistence();
}

std::optional<ChannelConfig> MahoAgentChannelGateway::UnregisterChannel(
    const std::string& channel_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopChannel(channel_id);
  auto res = registry_.UnregisterChannel(channel_id);
  SavePersistence();
  return res;
}

const ChannelConfig* MahoAgentChannelGateway::GetChannel(
    const std::string& channel_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return registry_.GetChannel(channel_id);
}

std::vector<ChannelConfig> MahoAgentChannelGateway::GetChannels() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return registry_.GetAllChannels();
}

bool MahoAgentChannelGateway::StartChannel(const std::string& channel_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const ChannelConfig* config = registry_.GetChannel(channel_id);
  if (!config || !config->enabled) {
    return false;
  }
  active_channels_.insert(channel_id);
  if (!poll_timer_.IsRunning()) {
    poll_timer_.Start(
        FROM_HERE, poll_interval_,
        base::BindRepeating(&MahoAgentChannelGateway::OnPollTimerTick,
                            weak_factory_.GetWeakPtr()));
  }
  return true;
}

bool MahoAgentChannelGateway::StopChannel(const std::string& channel_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_channels_.erase(channel_id);
  if (active_channels_.empty() && poll_timer_.IsRunning()) {
    poll_timer_.Stop();
  }
  return true;
}

void MahoAgentChannelGateway::StartAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& config : registry_.GetAllChannels()) {
    if (config.enabled) {
      active_channels_.insert(config.id);
    }
  }
  if (!active_channels_.empty() && !poll_timer_.IsRunning()) {
    poll_timer_.Start(
        FROM_HERE, poll_interval_,
        base::BindRepeating(&MahoAgentChannelGateway::OnPollTimerTick,
                            weak_factory_.GetWeakPtr()));
  }
}

void MahoAgentChannelGateway::StopAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_channels_.clear();
  poll_timer_.Stop();
}

bool MahoAgentChannelGateway::IsChannelRunning(const std::string& channel_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return active_channels_.find(channel_id) != active_channels_.end();
}

void MahoAgentChannelGateway::SetTriggerCallback(TriggerCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  trigger_callback_ = std::move(callback);
}

void MahoAgentChannelGateway::SetAiRuntimeAdapter(::MahoAiRuntimeAdapter* adapter) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  runtime_adapter_ = adapter;
}

InboundProcessOutcome MahoAgentChannelGateway::ProcessEventDirect(
    const ChannelEvent& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InboundProcessOutcome outcome = registry_.ProcessInboundEvent(event);
  if (outcome.is_triggered() && outcome.trigger.has_value()) {
    if (trigger_callback_) {
      trigger_callback_.Run(*outcome.trigger);
    }
#if !defined(MAHO_STANDALONE_TEST)
    if (runtime_adapter_) {
      runtime_adapter_->SubmitFollowUp(outcome.trigger->ToFollowUpPrompt(), "queue");
    }
#endif
    SavePersistence();
  }
  return outcome;
}

void MahoAgentChannelGateway::SendMessage(
    const OutboundChannelMessage& message,
    const std::optional<std::string>& approval_token,
    SendCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const ChannelConfig* config = registry_.GetChannel(message.channel_id);
  if (!config) {
    std::move(callback).Run(
        base::unexpected(ChannelError::MakeNotFound(message.channel_id)));
    return;
  }

  auto direct_api_op = SendViaDirectApi(*config, message);
  if (!direct_api_op.has_value()) {
    std::move(callback).Run(base::unexpected(direct_api_op.error()));
    return;
  }

  if (!broker_) {
    // Direct API broker not attached: return unavailable
    std::move(callback).Run(base::unexpected(
        ChannelError::MakeUnavailable("Broker instance not attached", false)));
    return;
  }

  // Dispatch through MahoAuthenticatedServiceApiBroker
  // TODO: Wired to parallel MahoAuthenticatedServiceApiBroker implementation
  auto cb_holder = std::make_shared<SendCallback>(std::move(callback));
  broker_->ExecuteOp(
      direct_api_op->operation,
      direct_api_op->context,
      approval_token,
      base::BindOnce(
          [](std::shared_ptr<SendCallback> cb_holder,
             DirectApiExecutionOutcome outcome) {
            auto result = HandleDirectApiOutcome(outcome);
            if (*cb_holder) {
              std::move(*cb_holder).Run(std::move(result));
            }
          },
          cb_holder));
}

ChannelHealth MahoAgentChannelGateway::GetHealth(
    const std::string& channel_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  uint64_t now = GetCurrentTimeSeconds();
  return registry_.EvaluateHealth(channel_id, now, stale_threshold_secs_);
}

std::unordered_map<std::string, ChannelHealth> MahoAgentChannelGateway::GetAllHealth() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::unordered_map<std::string, ChannelHealth> result;
  uint64_t now = GetCurrentTimeSeconds();
  for (const auto& config : registry_.GetAllChannels()) {
    result[config.id] = registry_.EvaluateHealth(config.id, now, stale_threshold_secs_);
  }
  return result;
}

void MahoAgentChannelGateway::SetPersistencePath(base::FilePath path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  persistence_path_ = std::move(path);
  persistence_path_str_ = persistence_path_.AsUTF8Unsafe();
}

void MahoAgentChannelGateway::SetPersistencePathString(std::string path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  persistence_path_str_ = std::move(path);
  persistence_path_ = base::FilePath::FromUTF8Unsafe(persistence_path_str_);
}

bool MahoAgentChannelGateway::SavePersistence() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (persistence_path_str_.empty()) {
    return false;
  }
  std::string data = registry_.SerializeToJson(/*redact_secrets=*/true);
  std::ofstream out(persistence_path_str_, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  out << data;
  return out.good();
}

bool MahoAgentChannelGateway::LoadPersistence() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (persistence_path_str_.empty()) {
    return false;
  }
  std::ifstream in(persistence_path_str_);
  if (!in.is_open()) {
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string data = ss.str();
  auto reloaded = ChannelRegistry::DeserializeFromJson(data);
  if (!reloaded.has_value()) {
    return false;
  }
  registry_ = std::move(*reloaded);
  return true;
}

std::string MahoAgentChannelGateway::ExportStateJson(bool redact_secrets) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return registry_.SerializeToJson(redact_secrets);
}

bool MahoAgentChannelGateway::ImportStateJson(const std::string& json_str) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto reloaded = ChannelRegistry::DeserializeFromJson(json_str);
  if (!reloaded.has_value()) {
    return false;
  }
  registry_ = std::move(*reloaded);
  SavePersistence();
  return true;
}

void MahoAgentChannelGateway::SetTransport(
    std::unique_ptr<ChannelTransport> transport) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  transport_ = std::move(transport);
}

void MahoAgentChannelGateway::SetPollInterval(base::TimeDelta interval) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  poll_interval_ = interval;
}

void MahoAgentChannelGateway::PollChannelOnce(const std::string& channel_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const ChannelConfig* config = registry_.GetChannel(channel_id);
  if (!config || !config->enabled || !transport_) {
    return;
  }
  auto last_cursor = registry_.GetLastCursor(channel_id);
  transport_->FetchEvents(
      *config,
      last_cursor,
      base::BindOnce(&MahoAgentChannelGateway::OnEventsFetched,
                     weak_factory_.GetWeakPtr(),
                     channel_id));
}

void MahoAgentChannelGateway::PollAllOnce() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const std::string& id : active_channels_) {
    PollChannelOnce(id);
  }
}

void MahoAgentChannelGateway::OnPollTimerTick() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PollAllOnce();
}

void MahoAgentChannelGateway::OnEventsFetched(
    const std::string& channel_id,
    std::vector<ChannelEvent> events,
    std::optional<std::string> error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  uint64_t now = GetCurrentTimeSeconds();
  registry_.RecordPoll(channel_id, now);

  if (error.has_value()) {
    return;
  }

  for (const auto& event : events) {
    ProcessEventDirect(event);
  }
}

uint64_t MahoAgentChannelGateway::GetCurrentTimeSeconds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (clock_source_) {
    return clock_source_.Run();
  }
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

}  // namespace maho::ai
