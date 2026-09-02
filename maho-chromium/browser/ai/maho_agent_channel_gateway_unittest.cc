// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_channel_gateway.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef MAHO_STANDALONE_TEST
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {

namespace {

// base::Bind forbids capturing lambdas; these stateless-helper bindings hold
// the test state the old lambdas captured.
class MockClock {
 public:
  uint64_t Now() const { return now; }
  uint64_t now = 0;
};

class TriggerRecorder {
 public:
  explicit TriggerRecorder(std::vector<AgentSessionTrigger>* sink)
      : sink_(sink) {}
  bool Record(const AgentSessionTrigger& trigger) {
    sink_->push_back(trigger);
    return true;
  }

 private:
  std::vector<AgentSessionTrigger>* sink_;
};

class FakeChannelTransport : public ChannelTransport {
 public:
  FakeChannelTransport() = default;
  ~FakeChannelTransport() override = default;

  void QueueEvents(std::vector<ChannelEvent> events) {
    queued_events_.insert(queued_events_.end(), events.begin(), events.end());
  }

  void SetError(std::string error) {
    error_ = std::move(error);
  }

  size_t fetch_count() const { return fetch_count_; }
  const std::optional<std::string>& last_fetch_cursor() const {
    return last_fetch_cursor_;
  }

  void FetchEvents(const ChannelConfig& config,
                   const std::optional<std::string>& cursor,
                   FetchCallback callback) override {
    ++fetch_count_;
    last_fetch_cursor_ = cursor;
    if (error_.has_value()) {
      std::move(callback).Run({}, error_);
      error_.reset();
      return;
    }
    std::vector<ChannelEvent> events = std::move(queued_events_);
    queued_events_.clear();
    std::move(callback).Run(std::move(events), std::nullopt);
  }

 private:
  size_t fetch_count_ = 0;
  std::optional<std::string> last_fetch_cursor_;
  std::vector<ChannelEvent> queued_events_;
  std::optional<std::string> error_;
};

}  // namespace

TEST(MahoAgentChannelGatewayTest, CompareCursorPositionsOrdering) {
  EXPECT_EQ(CompareCursorPositions("C5", "C5"), 0);
  EXPECT_LT(CompareCursorPositions("2", "10"), 0);
  EXPECT_GT(CompareCursorPositions("10", "2"), 0);
  EXPECT_LT(CompareCursorPositions("C2", "C10"), 0);
  EXPECT_GT(CompareCursorPositions("C10", "C2"), 0);
  EXPECT_LT(CompareCursorPositions("C5", "C6"), 0);
  EXPECT_GT(CompareCursorPositions("C6", "C5"), 0);
  EXPECT_LT(CompareCursorPositions("A1", "B1"), 0);
  EXPECT_GT(CompareCursorPositions("msg_b", "msg_a"), 0);
}

TEST(MahoAgentChannelGatewayTest, OutboundSendViaDirectApiOp) {
  ChannelConfig channel("slack-general", ChannelProvider::kSlack, "General Slack");
  channel.token = ChannelToken("xoxb-secret-token-12345");
  channel.account_id = "acc-123";
  channel.opaque_auth_handle = "auth-handle-xyz";

  OutboundChannelMessage msg;
  msg.channel_id = "slack-general";
  msg.provider = ChannelProvider::kSlack;
  msg.text = "Hello team";
  msg.reply_to_event_id = "evt-001";

  auto op_res = SendViaDirectApi(channel, msg);
  ASSERT_TRUE(op_res.has_value());
  const DirectApiOp& op = *op_res;

  EXPECT_EQ(op.operation.service, DirectApiServiceKind::kSlack);
  EXPECT_EQ(op.operation.operation_name, "channels.send_message");
  EXPECT_FALSE(op.operation.read_only);
  EXPECT_TRUE(op.requires_confirmation);
  EXPECT_EQ(op.context.opaque_auth_handle, "auth-handle-xyz");
  EXPECT_EQ(op.context.account_id, "acc-123");
  EXPECT_EQ(op.context.session_id, "channel-session-slack-general");

  // Invariant: parameters must not contain credentials
  EXPECT_EQ(op.operation.parameters_json.find("xoxb-secret-token"), std::string::npos);
  EXPECT_NE(op.operation.parameters_json.find("Hello team"), std::string::npos);
}

TEST(MahoAgentChannelGatewayTest, OutboundDirectApiOutcomeHandling) {
  // 1. Supported outcome
  DirectApiExecutionOutcome supported_outcome =
      DirectApiExecutionOutcome::MakeOk(
          DirectApiServiceKind::kSlack,
          "channels.send_message",
          "{\"channel_id\":\"slack-general\",\"message_id\":\"msg-99\",\"timestamp\":1700000010}");
  auto receipt_res = HandleDirectApiOutcome(supported_outcome);
  ASSERT_TRUE(receipt_res.has_value());
  EXPECT_EQ(receipt_res->channel_id, "slack-general");
  EXPECT_EQ(receipt_res->message_id, std::optional<std::string>("msg-99"));
  EXPECT_EQ(receipt_res->timestamp, 1700000010u);

  // 2. TypedUnavailable outcome
  DirectApiExecutionOutcome unavail_outcome =
      DirectApiExecutionOutcome::MakeTypedUnavailable("API rate limited", true);
  auto unavail_res = HandleDirectApiOutcome(unavail_outcome);
  ASSERT_FALSE(unavail_res.has_value());
  EXPECT_EQ(unavail_res.error().type, ChannelError::Type::kUnavailable);
  EXPECT_TRUE(unavail_res.error().can_fallback_to_tabs);

  // 3. HardFailure outcome
  DirectApiExecutionOutcome hard_outcome =
      DirectApiExecutionOutcome::MakeTypedError(
          DirectApiErrorCode::kScopeDenied, "Revoked token", false);
  hard_outcome.is_policy_denial = true;
  auto hard_res = HandleDirectApiOutcome(hard_outcome);
  ASSERT_FALSE(hard_res.has_value());
  EXPECT_EQ(hard_res.error().type, ChannelError::Type::kHardFailure);
  EXPECT_TRUE(hard_res.error().is_policy_denial);
}

TEST(MahoAgentChannelGatewayTest, InboundCursorDeduplication) {
  ChannelRegistry registry;
  ChannelConfig channel("discord-alerts", ChannelProvider::kDiscord, "Discord Alerts");
  channel.opaque_auth_handle = "auth-discord";
  registry.RegisterChannel(channel);

  ChannelEvent event1;
  event1.id = "evt-1";
  event1.channel_id = "discord-alerts";
  event1.provider = ChannelProvider::kDiscord;
  event1.kind = ChannelEventKind::kMessageReceived;
  event1.sender_id = "user-42";
  event1.sender_name = "Alice";
  event1.text = "Deploy started";
  event1.cursor = "C1";
  event1.timestamp = 1700000001;

  auto outcome1 = registry.ProcessInboundEvent(event1);
  EXPECT_TRUE(outcome1.is_triggered());
  ASSERT_TRUE(outcome1.trigger.has_value());
  EXPECT_EQ(outcome1.trigger->cursor, "C1");
  EXPECT_EQ(outcome1.trigger->message_text, "Deploy started");

  // Duplicate same cursor C1
  auto outcome2 = registry.ProcessInboundEvent(event1);
  EXPECT_TRUE(outcome2.is_duplicate());
  EXPECT_EQ(outcome2.cursor, "C1");

  // Older cursor C0
  ChannelEvent event0 = event1;
  event0.id = "evt-0";
  event0.cursor = "C0";
  auto outcome0 = registry.ProcessInboundEvent(event0);
  EXPECT_TRUE(outcome0.is_duplicate());

  // Newer cursor C2
  ChannelEvent event2 = event1;
  event2.id = "evt-2";
  event2.cursor = "C2";
  event2.text = "Deploy succeeded";
  auto outcome3 = registry.ProcessInboundEvent(event2);
  EXPECT_TRUE(outcome3.is_triggered());
  ASSERT_TRUE(outcome3.trigger.has_value());
  EXPECT_EQ(outcome3.trigger->cursor, "C2");
}

TEST(MahoAgentChannelGatewayTest, RestartSimulationLastCursorPersistence) {
  ChannelRegistry registry;
  ChannelConfig channel("tg-feed", ChannelProvider::kTelegram, "Telegram Feed");
  channel.token = ChannelToken("tg-bot-secret-999");
  registry.RegisterChannel(channel);

  // Process C1 through C5
  for (int i = 1; i <= 5; ++i) {
    ChannelEvent evt;
    evt.id = "tg-evt-" + std::to_string(i);
    evt.channel_id = "tg-feed";
    evt.provider = ChannelProvider::kTelegram;
    evt.kind = ChannelEventKind::kMessageReceived;
    evt.sender_id = "user-tg";
    evt.text = "Msg " + std::to_string(i);
    evt.cursor = "C" + std::to_string(i);
    evt.timestamp = 1700000000 + i;
    registry.ProcessInboundEvent(evt);
  }

  // Persist to JSON string
  std::string serialized = registry.SerializeToJson(false);

  // Reload into a new registry instance
  auto reloaded_res = ChannelRegistry::DeserializeFromJson(serialized);
  ASSERT_TRUE(reloaded_res.has_value());
  ChannelRegistry reloaded = std::move(*reloaded_res);

  // C5 must NOT be reprocessed
  ChannelEvent event_c5;
  event_c5.id = "tg-evt-5";
  event_c5.channel_id = "tg-feed";
  event_c5.provider = ChannelProvider::kTelegram;
  event_c5.kind = ChannelEventKind::kMessageReceived;
  event_c5.sender_id = "user-tg";
  event_c5.text = "Msg 5";
  event_c5.cursor = "C5";
  event_c5.timestamp = 1700000005;
  auto outcome_c5 = reloaded.ProcessInboundEvent(event_c5);
  EXPECT_TRUE(outcome_c5.is_duplicate());

  // C4 must NOT be reprocessed
  ChannelEvent event_c4 = event_c5;
  event_c4.id = "tg-evt-4";
  event_c4.cursor = "C4";
  auto outcome_c4 = reloaded.ProcessInboundEvent(event_c4);
  EXPECT_TRUE(outcome_c4.is_duplicate());

  // C6 must be processed
  ChannelEvent event_c6 = event_c5;
  event_c6.id = "tg-evt-6";
  event_c6.cursor = "C6";
  event_c6.text = "Msg 6";
  event_c6.timestamp = 1700000006;
  auto outcome_c6 = reloaded.ProcessInboundEvent(event_c6);
  EXPECT_TRUE(outcome_c6.is_triggered());
  ASSERT_TRUE(outcome_c6.trigger.has_value());
  EXPECT_EQ(outcome_c6.trigger->cursor, "C6");
}

TEST(MahoAgentChannelGatewayTest, FilePersistenceRoundTrip) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath persist_file = temp_dir.GetPath().AppendASCII("channels_state.json");

  MahoAgentChannelGateway gw1;
  gw1.SetPersistencePath(persist_file);

  ChannelConfig cfg("slack-persisted", ChannelProvider::kSlack, "Persisted Slack");
  cfg.token = ChannelToken("tok-persist");
  gw1.RegisterChannel(cfg);

  ChannelEvent ev;
  ev.id = "ev-1";
  ev.channel_id = "slack-persisted";
  ev.provider = ChannelProvider::kSlack;
  ev.kind = ChannelEventKind::kMessageReceived;
  ev.sender_id = "user-1";
  ev.text = "Message 1";
  ev.cursor = "C1";
  ev.timestamp = 1700000001;
  gw1.ProcessEventDirect(ev);

  EXPECT_TRUE(gw1.SavePersistence());

  // Verify token was redacted and not written in plaintext to disk
  std::string file_contents;
  EXPECT_TRUE(base::ReadFileToString(persist_file, &file_contents));
  EXPECT_EQ(file_contents.find("tok-persist"), std::string::npos);
  EXPECT_NE(file_contents.find("[REDACTED]"), std::string::npos);

  // Create gw2 and load persistence
  MahoAgentChannelGateway gw2;
  gw2.SetPersistencePath(persist_file);
  EXPECT_TRUE(gw2.LoadPersistence());

  const ChannelConfig* loaded_cfg = gw2.GetChannel("slack-persisted");
  ASSERT_NE(loaded_cfg, nullptr);
  EXPECT_EQ(loaded_cfg->name, "Persisted Slack");
  EXPECT_FALSE(loaded_cfg->token.has_value());

  // Health evaluates to Unconfigured because token is absent after reload (fail-closed)
  ChannelHealth health_before = gw2.GetHealth("slack-persisted");
  EXPECT_EQ(health_before.status, ChannelHealthStatus::kUnconfigured);

  // Inbound C1 must be rejected on gw2 (cursor tracking persisted)
  auto dup_outcome = gw2.ProcessEventDirect(ev);
  EXPECT_TRUE(dup_outcome.is_duplicate());

  // Re-provision channel with credential via vault/broker
  ChannelConfig reprov_cfg = *loaded_cfg;
  reprov_cfg.token = ChannelToken("tok-reprov");
  gw2.RegisterChannel(reprov_cfg);

  // Inbound C2 must be accepted
  ev.id = "ev-2";
  ev.cursor = "C2";
  ev.text = "Message 2";
  ev.timestamp = 1700000002;
  auto ok_outcome = gw2.ProcessEventDirect(ev);
  EXPECT_TRUE(ok_outcome.is_triggered());

  ChannelHealth health_after = gw2.GetHealth("slack-persisted");
  EXPECT_EQ(health_after.status, ChannelHealthStatus::kHealthy);
}

TEST(MahoAgentChannelGatewayTest, TokenRedactionProbe) {
  const std::string secret_token = "super-secret-slack-token-xyz987";
  ChannelConfig config("slack-secret", ChannelProvider::kSlack, "Secret Slack");
  config.token = ChannelToken(secret_token);
  config.account_id = "acc-sec";
  config.opaque_auth_handle = "auth-sec";

  // Assert Debug redaction
  std::string debug_str = config.ToDebugString();
  EXPECT_EQ(debug_str.find(secret_token), std::string::npos);
  EXPECT_NE(debug_str.find("[REDACTED]"), std::string::npos);

  // Assert JSON serialization redaction
  std::string json_str = config.ToJson(true);
  EXPECT_EQ(json_str.find(secret_token), std::string::npos);
  EXPECT_NE(json_str.find("[REDACTED]"), std::string::npos);

  // Assert DirectApiOp debug & parameters JSON
  OutboundChannelMessage msg;
  msg.channel_id = "slack-secret";
  msg.provider = ChannelProvider::kSlack;
  msg.text = "secret message";

  auto op_res = SendViaDirectApi(config, msg);
  ASSERT_TRUE(op_res.has_value());
  EXPECT_EQ(op_res->operation.parameters_json.find(secret_token), std::string::npos);
}

TEST(MahoAgentChannelGatewayTest, AdversarialMalformedInputHandling) {
  ChannelRegistry registry;
  ChannelConfig channel("slack-malformed", ChannelProvider::kSlack, "Malformed Test");
  channel.token = ChannelToken("tok-123");
  registry.RegisterChannel(channel);

  // 1. Empty event ID
  ChannelEvent bad_event1;
  bad_event1.id = "   ";
  bad_event1.channel_id = "slack-malformed";
  bad_event1.provider = ChannelProvider::kSlack;
  bad_event1.sender_id = "user-1";
  bad_event1.cursor = "C1";
  bad_event1.timestamp = 1700000000;
  EXPECT_TRUE(registry.ProcessInboundEvent(bad_event1).is_malformed());

  // 2. Empty sender ID
  ChannelEvent bad_event2 = bad_event1;
  bad_event2.id = "evt-2";
  bad_event2.sender_id = "";
  EXPECT_TRUE(registry.ProcessInboundEvent(bad_event2).is_malformed());

  // 3. Empty cursor
  ChannelEvent bad_event3 = bad_event1;
  bad_event3.id = "evt-3";
  bad_event3.cursor = "  ";
  EXPECT_TRUE(registry.ProcessInboundEvent(bad_event3).is_malformed());

  // 4. Zero timestamp
  ChannelEvent bad_event4 = bad_event1;
  bad_event4.id = "evt-4";
  bad_event4.timestamp = 0;
  EXPECT_TRUE(registry.ProcessInboundEvent(bad_event4).is_malformed());

  // Cursor should still be uninitialized since all bad events were rejected
  EXPECT_FALSE(registry.GetLastCursor("slack-malformed").has_value());

  // 5. Unknown channel
  ChannelEvent unknown_event = bad_event1;
  unknown_event.id = "evt-5";
  unknown_event.channel_id = "non-existent";
  EXPECT_TRUE(registry.ProcessInboundEvent(unknown_event).is_not_found());
}

TEST(MahoAgentChannelGatewayTest, ChannelHealthEvaluationWithInjectedClock) {
  ChannelRegistry registry;
  const std::string channel_id = "slack-health";
  ChannelConfig channel(channel_id, ChannelProvider::kSlack, "Health Test");
  channel.token = ChannelToken("tok-health");
  registry.RegisterChannel(channel);

  // Before any poll -> Disconnected
  ChannelHealth health1 = registry.EvaluateHealth(channel_id, 1000, 30);
  EXPECT_EQ(health1.status, ChannelHealthStatus::kDisconnected);

  // Record poll at t=1000
  registry.RecordPoll(channel_id, 1000);

  // At t=1020 (diff 20s <= 30s threshold) -> Healthy
  ChannelHealth health2 = registry.EvaluateHealth(channel_id, 1020, 30);
  EXPECT_EQ(health2.status, ChannelHealthStatus::kHealthy);

  // At t=1035 (diff 35s > 30s threshold) -> Stale
  ChannelHealth health3 = registry.EvaluateHealth(channel_id, 1035, 30);
  EXPECT_EQ(health3.status, ChannelHealthStatus::kStale);

  // Disable channel -> Unconfigured
  ChannelConfig* mut_cfg = registry.GetChannelMutable(channel_id);
  ASSERT_NE(mut_cfg, nullptr);
  mut_cfg->enabled = false;
  ChannelHealth health4 = registry.EvaluateHealth(channel_id, 1035, 30);
  EXPECT_EQ(health4.status, ChannelHealthStatus::kUnconfigured);
}

TEST(MahoAgentChannelGatewayTest, GatewayLifecycleAndPollingWithFakeTransport) {
  auto fake_transport = std::make_unique<FakeChannelTransport>();
  FakeChannelTransport* transport_ptr = fake_transport.get();

  uint64_t current_time = 1700000000;
  MockClock clock;
  clock.now = current_time;
  MahoAgentChannelGateway gateway(
      std::move(fake_transport),
      nullptr,
      base::BindRepeating(&MockClock::Now, base::Unretained(&clock)));

  ChannelConfig config("test-channel", ChannelProvider::kSlack, "Test Channel");
  config.token = ChannelToken("tok-live");
  gateway.RegisterChannel(config);

  std::vector<AgentSessionTrigger> triggered;
  TriggerRecorder triggered_recorder(&triggered);
  gateway.SetTriggerCallback(base::BindRepeating(
      &TriggerRecorder::Record, base::Unretained(&triggered_recorder)));

  EXPECT_TRUE(gateway.StartChannel("test-channel"));
  EXPECT_TRUE(gateway.IsChannelRunning("test-channel"));

  // Queue an event into fake transport
  ChannelEvent event;
  event.id = "ev-100";
  event.channel_id = "test-channel";
  event.provider = ChannelProvider::kSlack;
  event.kind = ChannelEventKind::kMessageReceived;
  event.sender_id = "user-alice";
  event.sender_name = "Alice";
  event.text = "Hello from channel gateway!";
  event.cursor = "C100";
  event.timestamp = 1700000005;

  transport_ptr->QueueEvents({event});

  // Run a poll cycle
  gateway.PollAllOnce();

  EXPECT_EQ(transport_ptr->fetch_count(), 1u);
  ASSERT_EQ(triggered.size(), 1u);
  EXPECT_EQ(triggered[0].message_text, "Hello from channel gateway!");
  EXPECT_EQ(triggered[0].cursor, "C100");

  // Health should now be Healthy at t=current_time
  ChannelHealth health = gateway.GetHealth("test-channel");
  EXPECT_EQ(health.status, ChannelHealthStatus::kHealthy);

  // Advance time past staleness threshold (e.g. +60s)
  clock.now += 60;
  ChannelHealth stale_health = gateway.GetHealth("test-channel");
  EXPECT_EQ(stale_health.status, ChannelHealthStatus::kStale);

  gateway.StopAll();
  EXPECT_FALSE(gateway.IsChannelRunning("test-channel"));
}

TEST(MahoAgentChannelGatewayTest, WeakPtrInvalidationOnSetTransport) {
  auto saved_callback = std::make_shared<ChannelTransport::FetchCallback>();

  class DeferredFakeTransport : public ChannelTransport {
   public:
    explicit DeferredFakeTransport(std::shared_ptr<FetchCallback> cb_holder)
        : cb_holder_(std::move(cb_holder)) {}
    void FetchEvents(const ChannelConfig& config,
                     const std::optional<std::string>& cursor,
                     FetchCallback callback) override {
      *cb_holder_ = std::move(callback);
    }
   private:
    std::shared_ptr<FetchCallback> cb_holder_;
  };

  MahoAgentChannelGateway gateway(
      std::make_unique<DeferredFakeTransport>(saved_callback), nullptr, {});
  ChannelConfig config("ch-1", ChannelProvider::kSlack, "Ch 1");
  gateway.RegisterChannel(config);

  std::vector<AgentSessionTrigger> triggers;
  TriggerRecorder triggers_recorder(&triggers);
  gateway.SetTriggerCallback(base::BindRepeating(
      &TriggerRecorder::Record, base::Unretained(&triggers_recorder)));

  gateway.PollChannelOnce("ch-1");
  EXPECT_TRUE(static_cast<bool>(*saved_callback));

  // Replace transport -> InvalidateWeakPtrs() called
  gateway.SetTransport(
      std::make_unique<DeferredFakeTransport>(std::make_shared<ChannelTransport::FetchCallback>()));

  // Deliver event using old saved callback
  ChannelEvent ev;
  ev.id = "ev-deferred";
  ev.channel_id = "ch-1";
  ev.provider = ChannelProvider::kSlack;
  ev.sender_id = "user-1";
  ev.cursor = "C1";
  ev.timestamp = 1700000000;

  std::vector<ChannelEvent> events;
  events.push_back(ev);
  std::move(*saved_callback).Run(std::move(events), std::nullopt);

  // Since weak ptr was invalidated, trigger callback was not executed
  EXPECT_TRUE(triggers.empty());
}

TEST(MahoAgentChannelGatewayTest, WeakPtrInvalidationOnGatewayDestruction) {
  auto saved_callback = std::make_shared<ChannelTransport::FetchCallback>();

  class DeferredFakeTransport : public ChannelTransport {
   public:
    explicit DeferredFakeTransport(std::shared_ptr<FetchCallback> cb_holder)
        : cb_holder_(std::move(cb_holder)) {}
    void FetchEvents(const ChannelConfig& config,
                     const std::optional<std::string>& cursor,
                     FetchCallback callback) override {
      *cb_holder_ = std::move(callback);
    }
   private:
    std::shared_ptr<FetchCallback> cb_holder_;
  };

  std::vector<AgentSessionTrigger> triggers;
  TriggerRecorder triggers_recorder(&triggers);
  {
    MahoAgentChannelGateway gateway(
        std::make_unique<DeferredFakeTransport>(saved_callback), nullptr, {});
    ChannelConfig config("ch-2", ChannelProvider::kSlack, "Ch 2");
    gateway.RegisterChannel(config);
    gateway.SetTriggerCallback(base::BindRepeating(
        &TriggerRecorder::Record, base::Unretained(&triggers_recorder)));
    gateway.PollChannelOnce("ch-2");
    EXPECT_TRUE(static_cast<bool>(*saved_callback));
    // gateway destroyed at end of block
  }

  // Deliver event to destroyed gateway callback
  ChannelEvent ev;
  ev.id = "ev-destruct";
  ev.channel_id = "ch-2";
  ev.provider = ChannelProvider::kSlack;
  ev.sender_id = "user-1";
  ev.cursor = "C1";
  ev.timestamp = 1700000000;

  std::vector<ChannelEvent> events;
  events.push_back(ev);
  std::move(*saved_callback).Run(std::move(events), std::nullopt);

  // No crash, triggers empty
  EXPECT_TRUE(triggers.empty());
}

}  // namespace maho::ai

#else

int main() {
  using namespace maho::ai;
  std::cout << "[RUN] MahoAgentChannelGateway standalone tests..." << std::endl;

  // Test 0: CompareCursorPositions
  {
    assert(CompareCursorPositions("C5", "C5") == 0);
    assert(CompareCursorPositions("2", "10") < 0);
    assert(CompareCursorPositions("10", "2") > 0);
    assert(CompareCursorPositions("C2", "C10") < 0);
    assert(CompareCursorPositions("C10", "C2") > 0);
    assert(CompareCursorPositions("C5", "C6") < 0);
    assert(CompareCursorPositions("C6", "C5") > 0);
    assert(CompareCursorPositions("A1", "B1") < 0);
    assert(CompareCursorPositions("msg_b", "msg_a") > 0);
  }

  // Test 1: Outbound DirectApiOp construction
  {
    ChannelConfig channel("slack-general", ChannelProvider::kSlack, "General Slack");
    channel.token = ChannelToken("xoxb-secret-token-12345");
    channel.account_id = "acc-123";
    channel.opaque_auth_handle = "auth-handle-xyz";

    OutboundChannelMessage msg;
    msg.channel_id = "slack-general";
    msg.provider = ChannelProvider::kSlack;
    msg.text = "Hello team";
    msg.reply_to_event_id = "evt-001";

    auto op_res = SendViaDirectApi(channel, msg);
    assert(op_res.has_value());
    const DirectApiOp& op = *op_res;
    assert(op.operation.service == DirectApiServiceKind::kSlack);
    assert(op.operation.operation_name == "channels.send_message");
    assert(!op.operation.read_only);
    assert(op.requires_confirmation);
    assert(op.context.opaque_auth_handle == "auth-handle-xyz");
    assert(op.context.account_id == "acc-123");
    assert(op.context.session_id == "channel-session-slack-general");
    assert(op.operation.parameters_json.find("xoxb-secret-token") == std::string::npos);
    assert(op.operation.parameters_json.find("Hello team") != std::string::npos);
  }

  // Test 2: Inbound cursor deduplication
  {
    ChannelRegistry registry;
    ChannelConfig channel("discord-alerts", ChannelProvider::kDiscord, "Discord Alerts");
    channel.opaque_auth_handle = "auth-discord";
    registry.RegisterChannel(channel);

    ChannelEvent event1;
    event1.id = "evt-1";
    event1.channel_id = "discord-alerts";
    event1.provider = ChannelProvider::kDiscord;
    event1.kind = ChannelEventKind::kMessageReceived;
    event1.sender_id = "user-42";
    event1.sender_name = "Alice";
    event1.text = "Deploy started";
    event1.cursor = "C1";
    event1.timestamp = 1700000001;

    auto outcome1 = registry.ProcessInboundEvent(event1);
    assert(outcome1.is_triggered());
    assert(outcome1.trigger.has_value());
    assert(outcome1.trigger->cursor == "C1");
    assert(outcome1.trigger->message_text == "Deploy started");

    auto outcome2 = registry.ProcessInboundEvent(event1);
    assert(outcome2.is_duplicate());
    assert(outcome2.cursor == "C1");

    ChannelEvent event0 = event1;
    event0.id = "evt-0";
    event0.cursor = "C0";
    auto outcome0 = registry.ProcessInboundEvent(event0);
    assert(outcome0.is_duplicate());

    ChannelEvent event2 = event1;
    event2.id = "evt-2";
    event2.cursor = "C2";
    event2.text = "Deploy succeeded";
    auto outcome3 = registry.ProcessInboundEvent(event2);
    assert(outcome3.is_triggered());
    assert(outcome3.trigger.has_value());
    assert(outcome3.trigger->cursor == "C2");
  }

  // Test 3: Restart simulation last cursor persistence
  {
    ChannelRegistry registry;
    ChannelConfig channel("tg-feed", ChannelProvider::kTelegram, "Telegram Feed");
    channel.token = ChannelToken("tg-bot-secret-999");
    registry.RegisterChannel(channel);

    for (int i = 1; i <= 5; ++i) {
      ChannelEvent evt;
      evt.id = "tg-evt-" + std::to_string(i);
      evt.channel_id = "tg-feed";
      evt.provider = ChannelProvider::kTelegram;
      evt.kind = ChannelEventKind::kMessageReceived;
      evt.sender_id = "user-tg";
      evt.text = "Msg " + std::to_string(i);
      evt.cursor = "C" + std::to_string(i);
      evt.timestamp = 1700000000 + i;
      registry.ProcessInboundEvent(evt);
    }

    std::string serialized = registry.SerializeToJson(false);
    auto reloaded_res = ChannelRegistry::DeserializeFromJson(serialized);
    assert(reloaded_res.has_value());
    ChannelRegistry reloaded = std::move(*reloaded_res);

    ChannelEvent event_c5;
    event_c5.id = "tg-evt-5";
    event_c5.channel_id = "tg-feed";
    event_c5.provider = ChannelProvider::kTelegram;
    event_c5.kind = ChannelEventKind::kMessageReceived;
    event_c5.sender_id = "user-tg";
    event_c5.text = "Msg 5";
    event_c5.cursor = "C5";
    event_c5.timestamp = 1700000005;
    auto outcome_c5 = reloaded.ProcessInboundEvent(event_c5);
    assert(outcome_c5.is_duplicate());

    ChannelEvent event_c4 = event_c5;
    event_c4.id = "tg-evt-4";
    event_c4.cursor = "C4";
    auto outcome_c4 = reloaded.ProcessInboundEvent(event_c4);
    assert(outcome_c4.is_duplicate());

    ChannelEvent event_c6 = event_c5;
    event_c6.id = "tg-evt-6";
    event_c6.cursor = "C6";
    event_c6.text = "Msg 6";
    event_c6.timestamp = 1700000006;
    auto outcome_c6 = reloaded.ProcessInboundEvent(event_c6);
    assert(outcome_c6.is_triggered());
    assert(outcome_c6.trigger.has_value());
    assert(outcome_c6.trigger->cursor == "C6");
  }

  // Test 4: File persistence round-trip with secret redaction and re-provisioning
  {
    std::string temp_path = "/tmp/maho_channel_test_persist_" + std::to_string(std::rand()) + ".json";
    uint64_t mock_time = 1700000005;
    MahoAgentChannelGateway gw1(nullptr, nullptr, base::BindRepeating([&mock_time]() { return mock_time; }));
    gw1.SetPersistencePathString(temp_path);

    ChannelConfig cfg("slack-persisted", ChannelProvider::kSlack, "Persisted Slack");
    cfg.token = ChannelToken("tok-persist");
    gw1.RegisterChannel(cfg);

    ChannelEvent ev;
    ev.id = "ev-1";
    ev.channel_id = "slack-persisted";
    ev.provider = ChannelProvider::kSlack;
    ev.kind = ChannelEventKind::kMessageReceived;
    ev.sender_id = "user-1";
    ev.text = "Message 1";
    ev.cursor = "C1";
    ev.timestamp = 1700000001;
    gw1.ProcessEventDirect(ev);

    assert(gw1.SavePersistence());

    // Verify token was redacted and not written in plaintext to disk
    std::ifstream file_in(temp_path);
    std::stringstream file_ss;
    file_ss << file_in.rdbuf();
    std::string file_content = file_ss.str();
    assert(file_content.find("tok-persist") == std::string::npos);
    assert(file_content.find("[REDACTED]") != std::string::npos);

    MahoAgentChannelGateway gw2(nullptr, nullptr, base::BindRepeating([&mock_time]() { return mock_time; }));
    gw2.SetPersistencePathString(temp_path);
    assert(gw2.LoadPersistence());

    const ChannelConfig* loaded_cfg = gw2.GetChannel("slack-persisted");
    assert(loaded_cfg != nullptr);
    assert(loaded_cfg->name == "Persisted Slack");
    assert(!loaded_cfg->token.has_value());

    // Health evaluates to Unconfigured because token is absent after reload (fail-closed)
    ChannelHealth health_before = gw2.GetHealth("slack-persisted");
    assert(health_before.status == ChannelHealthStatus::kUnconfigured);

    // Inbound C1 must be rejected on gw2 (cursor was preserved)
    auto dup_outcome = gw2.ProcessEventDirect(ev);
    assert(dup_outcome.is_duplicate());

    // Re-provision channel with credential via vault/broker
    ChannelConfig reprov_cfg = *loaded_cfg;
    reprov_cfg.token = ChannelToken("tok-reprov");
    gw2.RegisterChannel(reprov_cfg);

    ev.id = "ev-2";
    ev.cursor = "C2";
    ev.text = "Message 2";
    ev.timestamp = 1700000002;
    auto ok_outcome = gw2.ProcessEventDirect(ev);
    assert(ok_outcome.is_triggered());

    ChannelHealth health_after = gw2.GetHealth("slack-persisted");
    assert(health_after.status == ChannelHealthStatus::kHealthy);

    std::remove(temp_path.c_str());
  }

  // Test 5: Token redaction probe
  {
    const std::string secret_token = "super-secret-slack-token-xyz987";
    ChannelConfig config("slack-secret", ChannelProvider::kSlack, "Secret Slack");
    config.token = ChannelToken(secret_token);
    config.account_id = "acc-sec";
    config.opaque_auth_handle = "auth-sec";

    std::string debug_str = config.ToDebugString();
    assert(debug_str.find(secret_token) == std::string::npos);
    assert(debug_str.find("[REDACTED]") != std::string::npos);

    std::string json_str = config.ToJson(true);
    assert(json_str.find(secret_token) == std::string::npos);
    assert(json_str.find("[REDACTED]") != std::string::npos);

    OutboundChannelMessage msg;
    msg.channel_id = "slack-secret";
    msg.provider = ChannelProvider::kSlack;
    msg.text = "secret message";

    auto op_res = SendViaDirectApi(config, msg);
    assert(op_res.has_value());
    assert(op_res->operation.parameters_json.find(secret_token) == std::string::npos);
  }

  // Test 6: Adversarial malformed inputs
  {
    ChannelRegistry registry;
    ChannelConfig channel("slack-malformed", ChannelProvider::kSlack, "Malformed Test");
    channel.token = ChannelToken("tok-123");
    registry.RegisterChannel(channel);

    ChannelEvent bad_event1;
    bad_event1.id = "   ";
    bad_event1.channel_id = "slack-malformed";
    bad_event1.provider = ChannelProvider::kSlack;
    bad_event1.sender_id = "user-1";
    bad_event1.cursor = "C1";
    bad_event1.timestamp = 1700000000;
    assert(registry.ProcessInboundEvent(bad_event1).is_malformed());

    ChannelEvent bad_event2 = bad_event1;
    bad_event2.id = "evt-2";
    bad_event2.sender_id = "";
    assert(registry.ProcessInboundEvent(bad_event2).is_malformed());

    ChannelEvent bad_event3 = bad_event1;
    bad_event3.id = "evt-3";
    bad_event3.cursor = "  ";
    assert(registry.ProcessInboundEvent(bad_event3).is_malformed());

    ChannelEvent bad_event4 = bad_event1;
    bad_event4.id = "evt-4";
    bad_event4.timestamp = 0;
    assert(registry.ProcessInboundEvent(bad_event4).is_malformed());

    assert(!registry.GetLastCursor("slack-malformed").has_value());

    ChannelEvent unknown_event = bad_event1;
    unknown_event.id = "evt-5";
    unknown_event.channel_id = "non-existent";
    assert(registry.ProcessInboundEvent(unknown_event).is_not_found());
  }

  // Test 7: Health transitions with fake clock
  {
    ChannelRegistry registry;
    const std::string channel_id = "slack-health";
    ChannelConfig channel(channel_id, ChannelProvider::kSlack, "Health Test");
    channel.token = ChannelToken("tok-health");
    registry.RegisterChannel(channel);

    ChannelHealth health1 = registry.EvaluateHealth(channel_id, 1000, 30);
    assert(health1.status == ChannelHealthStatus::kDisconnected);

    registry.RecordPoll(channel_id, 1000);

    ChannelHealth health2 = registry.EvaluateHealth(channel_id, 1020, 30);
    assert(health2.status == ChannelHealthStatus::kHealthy);

    ChannelHealth health3 = registry.EvaluateHealth(channel_id, 1035, 30);
    assert(health3.status == ChannelHealthStatus::kStale);

    ChannelConfig* mut_cfg = registry.GetChannelMutable(channel_id);
    assert(mut_cfg != nullptr);
    mut_cfg->enabled = false;
    ChannelHealth health4 = registry.EvaluateHealth(channel_id, 1035, 30);
    assert(health4.status == ChannelHealthStatus::kUnconfigured);
  }

  // Test 8: Gateway lifecycle with custom transport
  {
    class StandaloneFakeTransport : public ChannelTransport {
     public:
      void FetchEvents(const ChannelConfig& config,
                       const std::optional<std::string>& cursor,
                       FetchCallback callback) override {
        std::vector<ChannelEvent> events;
        ChannelEvent ev;
        ev.id = "st-ev-1";
        ev.channel_id = config.id;
        ev.provider = config.provider;
        ev.kind = ChannelEventKind::kMessageReceived;
        ev.sender_id = "standalone-user";
        ev.text = "Standalone message";
        ev.cursor = "C50";
        ev.timestamp = 1700000100;
        events.push_back(ev);
        callback.Run(std::move(events), std::nullopt);
      }
    };

    uint64_t mock_time = 1700000100;
    MahoAgentChannelGateway gateway(
        std::make_unique<StandaloneFakeTransport>(),
        nullptr,
        base::BindRepeating([&mock_time]() { return mock_time; }));

    ChannelConfig config("test-st", ChannelProvider::kSlack, "Test Standalone");
    config.token = ChannelToken("tok-st");
    gateway.RegisterChannel(config);

    std::vector<AgentSessionTrigger> triggers;
    gateway.SetTriggerCallback(base::BindRepeating(
        [&triggers](const AgentSessionTrigger& t) {
          triggers.push_back(t);
          return true;
        }));

    assert(gateway.StartChannel("test-st"));
    assert(gateway.IsChannelRunning("test-st"));

    gateway.PollChannelOnce("test-st");
    assert(triggers.size() == 1);
    assert(triggers[0].cursor == "C50");
    assert(triggers[0].message_text == "Standalone message");

    ChannelHealth health = gateway.GetHealth("test-st");
    assert(health.status == ChannelHealthStatus::kHealthy);

    gateway.StopAll();
    assert(!gateway.IsChannelRunning("test-st"));
  }

  // Test 9: WeakPtr invalidation on SetTransport
  {
    auto saved_callback = std::make_shared<ChannelTransport::FetchCallback>();

    class DeferredFakeTransport : public ChannelTransport {
     public:
      explicit DeferredFakeTransport(std::shared_ptr<FetchCallback> cb_holder)
          : cb_holder_(std::move(cb_holder)) {}
      void FetchEvents(const ChannelConfig& config,
                       const std::optional<std::string>& cursor,
                       FetchCallback callback) override {
        *cb_holder_ = std::move(callback);
      }
     private:
      std::shared_ptr<FetchCallback> cb_holder_;
    };

    MahoAgentChannelGateway gateway(
        std::make_unique<DeferredFakeTransport>(saved_callback), nullptr, {});
    ChannelConfig config("ch-1", ChannelProvider::kSlack, "Ch 1");
    gateway.RegisterChannel(config);

    std::vector<AgentSessionTrigger> triggers;
    gateway.SetTriggerCallback(base::BindRepeating(
        [&triggers](const AgentSessionTrigger& t) {
          triggers.push_back(t);
          return true;
        }));

    gateway.PollChannelOnce("ch-1");
    assert(static_cast<bool>(*saved_callback));

    gateway.SetTransport(
        std::make_unique<DeferredFakeTransport>(std::make_shared<ChannelTransport::FetchCallback>()));

    ChannelEvent ev;
    ev.id = "ev-deferred";
    ev.channel_id = "ch-1";
    ev.provider = ChannelProvider::kSlack;
    ev.sender_id = "user-1";
    ev.cursor = "C1";
    ev.timestamp = 1700000000;

    std::vector<ChannelEvent> events;
    events.push_back(ev);
    std::move(*saved_callback).Run(std::move(events), std::nullopt);

    assert(triggers.empty());
  }

  // Test 10: WeakPtr invalidation on Gateway destruction
  {
    auto saved_callback = std::make_shared<ChannelTransport::FetchCallback>();

    class DeferredFakeTransport : public ChannelTransport {
     public:
      explicit DeferredFakeTransport(std::shared_ptr<FetchCallback> cb_holder)
          : cb_holder_(std::move(cb_holder)) {}
      void FetchEvents(const ChannelConfig& config,
                       const std::optional<std::string>& cursor,
                       FetchCallback callback) override {
        *cb_holder_ = std::move(callback);
      }
     private:
      std::shared_ptr<FetchCallback> cb_holder_;
    };

    std::vector<AgentSessionTrigger> triggers;
    {
      MahoAgentChannelGateway gateway(
          std::make_unique<DeferredFakeTransport>(saved_callback), nullptr, {});
      ChannelConfig config("ch-2", ChannelProvider::kSlack, "Ch 2");
      gateway.RegisterChannel(config);
      gateway.SetTriggerCallback(base::BindRepeating(
          [&triggers](const AgentSessionTrigger& t) {
            triggers.push_back(t);
            return true;
          }));
      gateway.PollChannelOnce("ch-2");
      assert(static_cast<bool>(*saved_callback));
    }

    ChannelEvent ev;
    ev.id = "ev-destruct";
    ev.channel_id = "ch-2";
    ev.provider = ChannelProvider::kSlack;
    ev.sender_id = "user-1";
    ev.cursor = "C1";
    ev.timestamp = 1700000000;

    std::vector<ChannelEvent> events;
    events.push_back(ev);
    std::move(*saved_callback).Run(std::move(events), std::nullopt);

    assert(triggers.empty());
  }

  std::cout << "[PASS] All MahoAgentChannelGateway standalone tests passed!" << std::endl;
  return 0;
}

#endif
