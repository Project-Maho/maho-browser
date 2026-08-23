#include "maho/browser/ai/maho_ai_runtime_adapter.h"
// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_GATEWAY_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_GATEWAY_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "maho/browser/ai/maho_authenticated_service_api_broker.h"

#if !defined(MAHO_STANDALONE_TEST)
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#else
#ifndef SEQUENCE_CHECKER
#define SEQUENCE_CHECKER(name)
#endif
#ifndef DCHECK_CALLED_ON_VALID_SEQUENCE
#define DCHECK_CALLED_ON_VALID_SEQUENCE(name) ((void)0)
#endif

namespace base {
template <typename T>
class WeakPtr {
 public:
  WeakPtr() : flag_(nullptr) {}
  explicit WeakPtr(std::shared_ptr<T*> flag) : flag_(std::move(flag)) {}
  T* get() const { return (flag_ && *flag_) ? *flag_ : nullptr; }
  T& operator*() const { return *get(); }
  T* operator->() const { return get(); }
  explicit operator bool() const { return get() != nullptr; }

 private:
  std::shared_ptr<T*> flag_;
};

template <typename T>
class WeakPtrFactory {
 public:
  explicit WeakPtrFactory(T* ptr) : ptr_(ptr), flag_(std::make_shared<T*>(ptr)) {}
  ~WeakPtrFactory() { InvalidateWeakPtrs(); }
  WeakPtr<T> GetWeakPtr() const { return WeakPtr<T>(flag_); }
  void InvalidateWeakPtrs() {
    if (flag_) {
      *flag_ = nullptr;
      flag_ = std::make_shared<T*>(ptr_);
    }
  }

 private:
  T* ptr_ = nullptr;
  mutable std::shared_ptr<T*> flag_;
};

template <typename R, typename C, typename... MethodArgs, typename... Args>
auto BindOnce(R (C::*method)(MethodArgs...), WeakPtr<C> weak_ptr, Args&&... bound_args) {
  return [method, weak_ptr, ... args = std::forward<Args>(bound_args)](auto&&... rest) mutable -> R {
    if (auto* ptr = weak_ptr.get()) {
      return (ptr->*method)(args..., std::forward<decltype(rest)>(rest)...);
    }
    if constexpr (!std::is_void_v<R>) {
      return R();
    }
  };
}

template <typename R, typename C, typename... MethodArgs, typename... Args>
auto BindRepeating(R (C::*method)(MethodArgs...), WeakPtr<C> weak_ptr, Args&&... bound_args) {
  return [method, weak_ptr, ... args = std::forward<Args>(bound_args)](auto&&... rest) -> R {
    if (auto* ptr = weak_ptr.get()) {
      return (ptr->*method)(args..., std::forward<decltype(rest)>(rest)...);
    }
    if constexpr (!std::is_void_v<R>) {
      return R();
    }
  };
}
}  // namespace base
#endif

class PrefService;
class MahoAiRuntimeAdapter;

namespace maho::ai {

/// External messaging platform provider.
enum class ChannelProvider {
  kSlack,
  kDiscord,
  kTelegram,
  kCustom,
};

std::string_view ChannelProviderToString(ChannelProvider provider);
ChannelProvider ChannelProviderFromString(std::string_view name);

/// Category of event received from an external channel.
enum class ChannelEventKind {
  kMessageReceived,
  kReactionAdded,
  kThreadReply,
  kChannelJoined,
};

std::string_view ChannelEventKindToString(ChannelEventKind kind);
ChannelEventKind ChannelEventKindFromString(std::string_view name);

/// Health status category for a channel connection.
enum class ChannelHealthStatus {
  kHealthy,
  kStale,
  kDegraded,
  kDisconnected,
  kUnconfigured,
};

std::string_view ChannelHealthStatusToString(ChannelHealthStatus status);
ChannelHealthStatus ChannelHealthStatusFromString(std::string_view name);

/// Channel credential token with zeroization on drop and strict redaction.
class ChannelToken {
 public:
  ChannelToken() = default;
  explicit ChannelToken(std::string secret);
  ~ChannelToken();

  ChannelToken(const ChannelToken& other);
  ChannelToken& operator=(const ChannelToken& other);
  ChannelToken(ChannelToken&& other) noexcept;
  ChannelToken& operator=(ChannelToken&& other) noexcept;

  const std::string& expose_secret() const { return secret_; }
  std::string ToString() const { return "[REDACTED]"; }
  std::string ToDebugString() const { return "[REDACTED]"; }

  bool operator==(const ChannelToken& other) const {
    return secret_ == other.secret_;
  }
  bool operator!=(const ChannelToken& other) const {
    return !(*this == other);
  }

 private:
  std::string secret_;
};

/// Configuration for a configured channel gateway connection.
struct ChannelConfig {
  ChannelConfig();
  ChannelConfig(std::string id,
                ChannelProvider provider,
                std::string name,
                bool enabled = true);
  ~ChannelConfig();
  ChannelConfig(const ChannelConfig&);
  ChannelConfig& operator=(const ChannelConfig&);
  ChannelConfig(ChannelConfig&&) noexcept;
  ChannelConfig& operator=(ChannelConfig&&) noexcept;

  std::string id;
  ChannelProvider provider = ChannelProvider::kSlack;
  std::string name;
  bool enabled = true;
  std::optional<ChannelToken> token;
  std::optional<std::string> account_id;
  std::optional<std::string> opaque_auth_handle;
  uint32_t poll_interval_seconds = 30;

  bool operator==(const ChannelConfig& other) const;
  bool operator!=(const ChannelConfig& other) const { return !(*this == other); }

  std::string ToDebugString() const;
  std::string ToJson(bool redact_secrets = true) const;
  static base::expected<ChannelConfig, std::string> FromJson(std::string_view json_str);
};

/// Comprehensive health report for a channel.
struct ChannelHealth {
  ChannelHealth();
  ~ChannelHealth();
  ChannelHealth(const ChannelHealth&);
  ChannelHealth& operator=(const ChannelHealth&);
  ChannelHealth(ChannelHealth&&) noexcept;
  ChannelHealth& operator=(ChannelHealth&&) noexcept;

  ChannelHealthStatus status = ChannelHealthStatus::kUnconfigured;
  std::optional<uint64_t> last_poll_timestamp;
  std::optional<std::string> details;

  bool operator==(const ChannelHealth& other) const {
    return status == other.status &&
           last_poll_timestamp == other.last_poll_timestamp &&
           details == other.details;
  }
};

/// Inbound event received from a channel.
struct ChannelEvent {
  ChannelEvent();
  ~ChannelEvent();
  ChannelEvent(const ChannelEvent&);
  ChannelEvent& operator=(const ChannelEvent&);
  ChannelEvent(ChannelEvent&&) noexcept;
  ChannelEvent& operator=(ChannelEvent&&) noexcept;

  std::string id;
  std::string channel_id;
  ChannelProvider provider = ChannelProvider::kSlack;
  ChannelEventKind kind = ChannelEventKind::kMessageReceived;
  std::string sender_id;
  std::optional<std::string> sender_name;
  std::string text;
  std::string cursor;
  uint64_t timestamp = 0;

  bool operator==(const ChannelEvent& other) const;
  std::string ToJson() const;
};

/// Trigger payload dispatched to the agent runtime when an inbound event is processed.
struct AgentSessionTrigger {
  AgentSessionTrigger();
  ~AgentSessionTrigger();
  AgentSessionTrigger(const AgentSessionTrigger&);
  AgentSessionTrigger& operator=(const AgentSessionTrigger&);
  AgentSessionTrigger(AgentSessionTrigger&&) noexcept;
  AgentSessionTrigger& operator=(AgentSessionTrigger&&) noexcept;

  std::string channel_id;
  ChannelProvider provider = ChannelProvider::kSlack;
  std::string sender_id;
  std::optional<std::string> sender_name;
  std::string message_text;
  std::string event_id;
  std::string cursor;
  uint64_t timestamp = 0;

  bool operator==(const AgentSessionTrigger& other) const;
  std::string ToJson() const;
  std::string ToFollowUpPrompt() const;
};

/// Typed outcome resulting from attempting to process an inbound channel event.
struct InboundProcessOutcome {
  InboundProcessOutcome();
  ~InboundProcessOutcome();
  InboundProcessOutcome(const InboundProcessOutcome&);
  InboundProcessOutcome& operator=(const InboundProcessOutcome&);
  InboundProcessOutcome(InboundProcessOutcome&&) noexcept;
  InboundProcessOutcome& operator=(InboundProcessOutcome&&) noexcept;

  enum class Status {
    kTriggered,
    kDuplicateCursor,
    kMalformedEvent,
    kChannelNotFound,
    kChannelDisabled,
  };

  static InboundProcessOutcome Triggered(AgentSessionTrigger trigger);
  static InboundProcessOutcome DuplicateCursor(std::string cursor);
  static InboundProcessOutcome MalformedEvent(std::string reason);
  static InboundProcessOutcome ChannelNotFound(std::string channel_id);
  static InboundProcessOutcome ChannelDisabled(std::string channel_id);

  Status status = Status::kMalformedEvent;
  std::optional<AgentSessionTrigger> trigger;
  std::string cursor;
  std::string reason;
  std::string channel_id;

  bool is_triggered() const { return status == Status::kTriggered; }
  bool is_duplicate() const { return status == Status::kDuplicateCursor; }
  bool is_malformed() const { return status == Status::kMalformedEvent; }
  bool is_not_found() const { return status == Status::kChannelNotFound; }
  bool is_disabled() const { return status == Status::kChannelDisabled; }
};

/// Outbound message payload to be sent to a channel.
struct OutboundChannelMessage {
  OutboundChannelMessage();
  ~OutboundChannelMessage();
  OutboundChannelMessage(const OutboundChannelMessage&);
  OutboundChannelMessage& operator=(const OutboundChannelMessage&);
  OutboundChannelMessage(OutboundChannelMessage&&) noexcept;
  OutboundChannelMessage& operator=(OutboundChannelMessage&&) noexcept;

  std::string channel_id;
  ChannelProvider provider = ChannelProvider::kSlack;
  std::string text;
  std::optional<std::string> reply_to_event_id;
};

/// Outbound command wrapping a direct API operation with confirmation requirement.
struct DirectApiOp {
  DirectApiOpDescriptor operation;
  DirectApiExecutionContext context;
  bool requires_confirmation = true;
};

/// Typed receipt returned upon successful outbound send.
struct OutboundSendReceipt {
  OutboundSendReceipt();
  ~OutboundSendReceipt();
  OutboundSendReceipt(const OutboundSendReceipt&);
  OutboundSendReceipt& operator=(const OutboundSendReceipt&);
  OutboundSendReceipt(OutboundSendReceipt&&) noexcept;
  OutboundSendReceipt& operator=(OutboundSendReceipt&&) noexcept;

  std::string channel_id;
  std::optional<std::string> message_id;
  uint64_t timestamp = 0;

  bool operator==(const OutboundSendReceipt& other) const {
    return channel_id == other.channel_id &&
           message_id == other.message_id &&
           timestamp == other.timestamp;
  }
};

/// Errors originating in channel gateway operations.
struct ChannelError {
  enum class Type {
    kChannelNotFound,
    kChannelDisabled,
    kUnsupportedProvider,
    kUnavailable,
    kHardFailure,
    kMalformedEvent,
  };

  static ChannelError MakeNotFound(std::string channel_id);
  static ChannelError MakeDisabled(std::string channel_id);
  static ChannelError MakeUnsupportedProvider(ChannelProvider provider);
  static ChannelError MakeUnavailable(std::string reason, bool can_fallback_to_tabs);
  static ChannelError MakeHardFailure(std::string code, std::string message, bool is_policy_denial);
  static ChannelError MakeMalformed(std::string reason);

  Type type = Type::kMalformedEvent;
  std::string message;
  std::string code;
  bool can_fallback_to_tabs = false;
  bool is_policy_denial = false;
};

/// Cursor comparison helper: compares numeric or single-alpha prefix (e.g. C5 vs C6).
int CompareCursorPositions(std::string_view a, std::string_view b);

/// Constructs a typed DirectApiOp for outbound message sending.
/// Ensures credentials are not included in payload parameters and that
/// requires_confirmation is strictly set to true.
base::expected<DirectApiOp, ChannelError> SendViaDirectApi(
    const ChannelConfig& channel,
    const OutboundChannelMessage& message);

/// Maps DirectApiOutcome results into typed OutboundSendReceipt or ChannelError.
base::expected<OutboundSendReceipt, ChannelError> HandleDirectApiOutcome(
    const DirectApiExecutionOutcome& outcome);

/// Per-channel runtime state maintained in the registry.
struct ChannelEntry {
  ChannelEntry();
  ~ChannelEntry();
  ChannelEntry(const ChannelEntry&);
  ChannelEntry& operator=(const ChannelEntry&);
  ChannelEntry(ChannelEntry&&) noexcept;
  ChannelEntry& operator=(ChannelEntry&&) noexcept;

  ChannelConfig config;
  std::optional<std::string> last_cursor;
  std::optional<uint64_t> last_poll_timestamp;
  std::unordered_set<std::string> seen_cursors;
  bool is_active = false;
};

/// Registry managing active channel registrations and their cursor deduplication state.
class ChannelRegistry {
 public:
  ChannelRegistry();
  ~ChannelRegistry();
  ChannelRegistry(const ChannelRegistry&);
  ChannelRegistry& operator=(const ChannelRegistry&);
  ChannelRegistry(ChannelRegistry&&) noexcept;
  ChannelRegistry& operator=(ChannelRegistry&&) noexcept;

  void RegisterChannel(ChannelConfig config);
  std::optional<ChannelConfig> UnregisterChannel(const std::string& channel_id);
  const ChannelConfig* GetChannel(const std::string& channel_id) const;
  ChannelConfig* GetChannelMutable(const std::string& channel_id);
  std::vector<ChannelConfig> GetAllChannels() const;
  size_t ChannelCount() const { return channels_.size(); }

  std::optional<std::string> GetLastCursor(const std::string& channel_id) const;
  void SetLastCursor(const std::string& channel_id, std::string cursor);
  void RecordPoll(const std::string& channel_id, uint64_t timestamp);

  InboundProcessOutcome ProcessInboundEvent(const ChannelEvent& event);

  ChannelHealth EvaluateHealth(const std::string& channel_id,
                               uint64_t now_timestamp_secs,
                               uint64_t stale_threshold_secs = 30) const;

  std::string SerializeToJson(bool redact_secrets = true) const;
  static base::expected<ChannelRegistry, std::string> DeserializeFromJson(
      std::string_view json_str);

 private:
  std::unordered_map<std::string, ChannelEntry> channels_;
};

/// Channel transport interface (abstract — tests inject FakeChannelTransport).
class ChannelTransport {
 public:
  virtual ~ChannelTransport() = default;
  using FetchCallback = base::OnceCallback<void(
      std::vector<ChannelEvent> events,
      std::optional<std::string> error)>;

  virtual void FetchEvents(const ChannelConfig& config,
                           const std::optional<std::string>& cursor,
                           FetchCallback callback) = 0;
};

/// Production HTTP transport stub (TODO: Wire real HTTP transport with URL loader).
class DefaultChannelHttpTransport : public ChannelTransport {
 public:
  DefaultChannelHttpTransport();
  ~DefaultChannelHttpTransport() override;

  void FetchEvents(const ChannelConfig& config,
                   const std::optional<std::string>& cursor,
                   FetchCallback callback) override;
};

/// Main Gateway lifecycle coordinator for external messaging channels (Gap #19).
class MahoAgentChannelGateway {
 public:
  using TriggerCallback = base::RepeatingCallback<bool(const AgentSessionTrigger&)>;
  using SendCallback =
      base::OnceCallback<void(base::expected<OutboundSendReceipt, ChannelError>)>;

  MahoAgentChannelGateway();
  explicit MahoAgentChannelGateway(
      std::unique_ptr<ChannelTransport> transport,
      MahoAuthenticatedServiceApiBroker* broker = nullptr,
      base::RepeatingCallback<uint64_t()> clock_source = {});
  ~MahoAgentChannelGateway();

  MahoAgentChannelGateway(const MahoAgentChannelGateway&) = delete;
  MahoAgentChannelGateway& operator=(const MahoAgentChannelGateway&) = delete;

  // Channel Registration
  void RegisterChannel(ChannelConfig config);
  std::optional<ChannelConfig> UnregisterChannel(const std::string& channel_id);
  const ChannelConfig* GetChannel(const std::string& channel_id) const;
  std::vector<ChannelConfig> GetChannels() const;

  // Lifecycle
  bool StartChannel(const std::string& channel_id);
  bool StopChannel(const std::string& channel_id);
  void StartAll();
  void StopAll();
  bool IsChannelRunning(const std::string& channel_id) const;

  // Inbound Turn-Submission Integration
  void SetTriggerCallback(TriggerCallback callback);
  void SetAiRuntimeAdapter(::MahoAiRuntimeAdapter* adapter);
  InboundProcessOutcome ProcessEventDirect(const ChannelEvent& event);

  // Outbound Dispatch via Direct API Broker
  void SendMessage(const OutboundChannelMessage& message,
                   const std::optional<std::string>& approval_token,
                   SendCallback callback);

  // Health Status
  ChannelHealth GetHealth(const std::string& channel_id) const;
  std::unordered_map<std::string, ChannelHealth> GetAllHealth() const;
  void SetStaleThresholdSeconds(uint64_t threshold_secs) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    stale_threshold_secs_ = threshold_secs;
  }

  // Persistence (Profile Prefs / JSON File)
  void SetPersistencePath(base::FilePath path);
  void SetPersistencePathString(std::string path);
  bool SavePersistence() const;
  bool LoadPersistence();
  std::string ExportStateJson(bool redact_secrets = true) const;
  bool ImportStateJson(const std::string& json_str);

  // Polling Configuration and Manual Drives
  void SetTransport(std::unique_ptr<ChannelTransport> transport);
  void SetPollInterval(base::TimeDelta interval);
  void PollChannelOnce(const std::string& channel_id);
  void PollAllOnce();

  // Test Injections
  void SetClockSourceForTesting(base::RepeatingCallback<uint64_t()> clock_source) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    clock_source_ = std::move(clock_source);
  }
  void RunTimerTickForTesting() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    OnPollTimerTick();
  }
  ChannelRegistry& registry_for_testing() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return registry_;
  }

 private:
  void OnPollTimerTick();
  void OnEventsFetched(const std::string& channel_id,
                       std::vector<ChannelEvent> events,
                       std::optional<std::string> error);
  uint64_t GetCurrentTimeSeconds() const;

  SEQUENCE_CHECKER(sequence_checker_);

  ChannelRegistry registry_;
  std::unique_ptr<ChannelTransport> transport_;
  MahoAuthenticatedServiceApiBroker* broker_ = nullptr;
  ::MahoAiRuntimeAdapter* runtime_adapter_ = nullptr;
  TriggerCallback trigger_callback_;

  base::FilePath persistence_path_;
  std::string persistence_path_str_;
  uint64_t stale_threshold_secs_ = 30;
  base::TimeDelta poll_interval_;
  base::RepeatingTimer poll_timer_;
  base::RepeatingCallback<uint64_t()> clock_source_;
  std::set<std::string> active_channels_;

  base::WeakPtrFactory<MahoAgentChannelGateway> weak_factory_{this};
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_GATEWAY_H_
