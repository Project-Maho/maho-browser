// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_PROTOCOLS_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_PROTOCOLS_H_

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/time/time.h"

namespace maho::ai {

class ChannelToken;
struct ChannelConfig;
struct ChannelEvent;

class ChannelBackoff {
 public:
  explicit ChannelBackoff(base::TimeDelta initial = base::Seconds(1),
                          base::TimeDelta cap = base::Seconds(30));
  base::TimeDelta OnFailure();
  void OnSuccess();
  base::TimeDelta current() const { return current_; }

 private:
  base::TimeDelta initial_;
  base::TimeDelta cap_;
  base::TimeDelta current_;
};

bool ParseTelegramUpdates(std::string_view json,
                          const ChannelConfig& config,
                          std::vector<ChannelEvent>* events,
                          std::string* next_offset,
                          std::string* error);

class ChannelWebSocketConnection {
 public:
  virtual ~ChannelWebSocketConnection() = default;
  virtual void SendText(std::string frame) = 0;
  virtual void Close() = 0;
};

class SlackSocketProtocol {
 public:
  bool OnHello(std::string_view frame);
  bool OnFrame(std::string_view frame);
  std::vector<std::string> TakeEvents();
  std::vector<std::string> TakeAcknowledgements();
  uint64_t heartbeat_interval_ms() const { return heartbeat_interval_ms_; }
  bool reconnect_requested() const { return reconnect_requested_; }
  void ResetReconnectRequest() { reconnect_requested_ = false; }

 private:
  std::vector<std::string> acknowledgements_;
  std::vector<std::string> events_;
  uint64_t heartbeat_interval_ms_ = 0;
  bool reconnect_requested_ = false;
};

class DiscordGatewayProtocol {
 public:
  explicit DiscordGatewayProtocol(std::string token = {});
  void SetToken(std::string token);
  struct Decision {
    std::vector<std::string> outbound_frames;
    bool reconnect = false;
    bool invalid_session = false;
  };
  Decision OnFrame(std::string_view frame);
  void OnDisconnected();
  std::vector<std::string> ResumeFrames() const;
  bool should_resume() const { return !session_id_.empty() && sequence_.has_value(); }
  const std::string& session_id() const { return session_id_; }
  std::optional<uint64_t> sequence() const { return sequence_; }

 private:
  std::string session_id_;
  std::optional<uint64_t> sequence_;
  uint64_t heartbeat_interval_ms_ = 0;
  bool identified_ = false;
  std::string token_;
  bool resume_requested_ = false;
};

// Pairing of external chat senders ("<channel>:<sender>" principals). Each
// unknown principal gets its own 6-digit code, valid 10 minutes and for at
// most 5 confirmation attempts; a code only pairs the principal it was issued
// to.
class MahoAgentChannelPairing {
 public:
  enum class ConfirmResult { kApproved, kWrongCode, kExpired, kAttemptsExhausted };
  using CodeGenerator = std::function<std::string()>;
  static constexpr base::TimeDelta kCodeLifetime = base::Minutes(10);
  static constexpr int kMaxAttempts = 5;

  explicit MahoAgentChannelPairing(CodeGenerator generator = nullptr);
  ~MahoAgentChannelPairing();
  // Returns the principal's live code, issuing a new one when it has none or
  // the old one expired. Empty for an already paired principal.
  std::string IssueCode(const std::string& principal, base::TimeTicks now);
  ConfirmResult Confirm(const std::string& principal,
                        std::string_view code,
                        base::TimeTicks now);
  bool IsPaired(std::string_view principal) const;
  bool AuthorizeInbound(std::string_view principal) const;
  void Revoke(std::string_view principal);

 private:
  struct PendingCode {
    std::string value;
    base::TimeTicks expires;
    int attempts = 0;
  };
  CodeGenerator generator_;
  std::map<std::string, PendingCode, std::less<>> pending_;
  std::set<std::string, std::less<>> paired_principals_;
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_PROTOCOLS_H_
