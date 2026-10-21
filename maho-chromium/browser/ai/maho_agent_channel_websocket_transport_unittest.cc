// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_channel_websocket_transport.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

struct FakeConnectionState {
  std::vector<std::string> sent;
  int close_count = 0;
  bool closed_flag = false;
  // Delivers an inbound frame to the transport; reset when the connection
  // object is destroyed.
  base::RepeatingCallback<void(std::string)> receive;
  // Simulates the server closing the socket; reset when the connection
  // object is destroyed.
  base::RepeatingClosure remote_close;
  void Receive(std::string text) {
    ASSERT_TRUE(receive) << "connection already destroyed";
    receive.Run(std::move(text));
  }
  void RemoteClose() {
    ASSERT_TRUE(remote_close) << "connection already destroyed";
    remote_close.Run();
  }
};

class FakeConnection final : public ChannelWebSocketConnection {
 public:
  FakeConnection(ChannelWebSocketFrameCallback frame,
                 ChannelWebSocketClosedCallback closed,
                 std::shared_ptr<FakeConnectionState> state)
      : frame_(std::move(frame)),
        closed_(std::move(closed)),
        state_(std::move(state)) {
    state_->receive = frame_;
    state_->remote_close = closed_;
  }
  ~FakeConnection() override {
    state_->receive.Reset();
    state_->remote_close.Reset();
  }
  void SendText(std::string text) override {
    state_->sent.push_back(std::move(text));
  }
  void Close() override {
    if (state_->closed_flag) return;
    state_->closed_flag = true;
    ++state_->close_count;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                              closed_);
  }

 private:
  ChannelWebSocketFrameCallback frame_;
  ChannelWebSocketClosedCallback closed_;
  std::shared_ptr<FakeConnectionState> state_;
};

class WebSocketTransportTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  ChannelWebSocketFactory Factory() {
    return base::BindLambdaForTesting(
        [this](const GURL& url, ChannelWebSocketFrameCallback frame,
               ChannelWebSocketClosedCallback closed)
            -> std::unique_ptr<ChannelWebSocketConnection> {
          auto state = std::make_shared<FakeConnectionState>();
          auto socket = std::make_unique<FakeConnection>(
              std::move(frame), std::move(closed), state);
          sockets.push_back(std::move(state));
          return socket;
        });
  }

  std::vector<std::shared_ptr<FakeConnectionState>> sockets;
};

TEST_F(WebSocketTransportTest, SlackAcknowledgesEnvelopeAndReopensOnDisconnect) {
  int opens = 0;
  auto opener = base::BindLambdaForTesting(
      [&opens](const std::string& token,
               base::OnceCallback<void(std::optional<std::string>)> callback) {
        EXPECT_EQ(token, "xapp-test");
        ++opens;
        std::move(callback).Run("wss://socket.slack.test/" +
                                std::to_string(opens));
      });
  auto transport = CreateSlackSocketModeTransportForTesting(
      nullptr, ChannelToken("xapp-test"), Factory(), opener);
  ChannelConfig config("slack", ChannelProvider::kSlack, "Slack");
  bool fetched = false;
  transport->FetchEvents(config, std::nullopt, base::BindLambdaForTesting(
      [&fetched](std::vector<ChannelEvent> events,
                 std::optional<std::string> error) {
        EXPECT_FALSE(error);
        fetched = true;
      }));
  ASSERT_EQ(opens, 1);
  ASSERT_EQ(sockets.size(), 1u);
  sockets.back()->Receive(R"({"type":"hello","data":{"heartbeat_interval":30000}})");
  sockets.back()->Receive(R"({"envelope_id":"env1","payload":{"event":{"type":"message","user":"U1","channel":"C1","text":"hi","ts":"123.4"}}})");
  ASSERT_EQ(sockets.back()->sent.size(), 1u);
  auto ack = base::JSONReader::ReadDict(sockets.back()->sent.front(),
                                        base::JSON_PARSE_RFC);
  ASSERT_TRUE(ack);
  const std::string* envelope_id = ack->FindString("envelope_id");
  ASSERT_TRUE(envelope_id);
  EXPECT_EQ(*envelope_id, "env1");
  EXPECT_TRUE(fetched);
  sockets.back()->Receive(R"({"type":"disconnect"})");
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(opens, 2);
  EXPECT_EQ(sockets.size(), 2u);
}

TEST_F(WebSocketTransportTest, DiscordHeartbeatCadenceAndResumeAfterReconnect) {
  auto transport = CreateDiscordGatewayTransportForTesting(
      nullptr, ChannelToken("bot-token"), Factory(),
      "wss://gateway.discord.test/?v=10&encoding=json");
  ChannelConfig config("discord", ChannelProvider::kDiscord, "Discord");
  transport->FetchEvents(config, std::nullopt, base::BindOnce(
      [](std::vector<ChannelEvent>, std::optional<std::string>) {}));
  ASSERT_EQ(sockets.size(), 1u);
  sockets.back()->Receive(R"({"op":10,"d":{"heartbeat_interval":1000}})");
  ASSERT_EQ(sockets.back()->sent.size(), 1u);
  auto identify = base::JSONReader::ReadDict(sockets.back()->sent.back(),
                                             base::JSON_PARSE_RFC);
  ASSERT_TRUE(identify);
  EXPECT_EQ(identify->FindInt("op").value_or(-1), 2);
  const std::optional<int> intents = identify->FindDict("d")->FindInt("intents");
  ASSERT_TRUE(intents);
  EXPECT_EQ(*intents, 4609);
  task_environment_.FastForwardBy(base::Milliseconds(999));
  EXPECT_EQ(sockets.front()->sent.size(), 1u);
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(sockets.front()->sent.size(), 2u);
  sockets.front()->Receive(R"({"op":11})");
  sockets.front()->Receive(R"({"op":0,"s":42,"t":"READY","d":{"session_id":"sid","resume_gateway_url":"wss://resume.discord.test"}})");
  sockets.front()->RemoteClose();
  task_environment_.FastForwardBy(base::Seconds(1));
  ASSERT_EQ(sockets.size(), 2u);
  EXPECT_EQ(sockets.back()->sent.size(), 0u);
  sockets.back()->Receive(R"({"op":10,"d":{"heartbeat_interval":1000}})");
  ASSERT_EQ(sockets.back()->sent.size(), 1u);
  auto resume = base::JSONReader::ReadDict(sockets.back()->sent.back(),
                                           base::JSON_PARSE_RFC);
  ASSERT_TRUE(resume);
  EXPECT_EQ(resume->FindInt("op").value_or(-1), 6);
  const base::DictValue* data = resume->FindDict("d");
  ASSERT_TRUE(data);
  const std::string* session_id = data->FindString("session_id");
  ASSERT_TRUE(session_id);
  EXPECT_EQ(*session_id, "sid");
  EXPECT_EQ(data->FindInt("seq").value_or(-1), 42);
}

TEST_F(WebSocketTransportTest, DiscordNonresumableInvalidSessionReidentifies) {
  auto transport = CreateDiscordGatewayTransportForTesting(
      nullptr, ChannelToken("bot-token"), Factory(), "wss://gateway.discord.test/");
  ChannelConfig config("discord", ChannelProvider::kDiscord, "Discord");
  transport->FetchEvents(config, std::nullopt, base::BindOnce(
      [](std::vector<ChannelEvent>, std::optional<std::string>) {}));
  sockets.back()->Receive(R"({"op":10,"d":{"heartbeat_interval":1000}})");
  sockets.back()->Receive(R"({"op":0,"s":9,"t":"READY","d":{"session_id":"old","resume_gateway_url":"wss://resume.test"}})");
  sockets.back()->Receive(R"({"op":9,"d":false})");
  task_environment_.FastForwardBy(base::Seconds(1));
  ASSERT_EQ(sockets.size(), 2u);
  sockets.back()->Receive(R"({"op":10,"d":{"heartbeat_interval":1000}})");
  ASSERT_EQ(sockets.back()->sent.size(), 1u);
  auto identify = base::JSONReader::ReadDict(sockets.back()->sent.back(),
                                             base::JSON_PARSE_RFC);
  ASSERT_TRUE(identify);
  EXPECT_EQ(identify->FindInt("op").value_or(-1), 2);
}

TEST_F(WebSocketTransportTest, DiscordMissedHeartbeatAckReconnects) {
  auto transport = CreateDiscordGatewayTransportForTesting(
      nullptr, ChannelToken("bot-token"), Factory(), "wss://gateway.discord.test/");
  ChannelConfig config("discord", ChannelProvider::kDiscord, "Discord");
  transport->FetchEvents(config, std::nullopt, base::BindOnce(
      [](std::vector<ChannelEvent>, std::optional<std::string>) {}));
  sockets.back()->Receive(R"({"op":10,"d":{"heartbeat_interval":1000}})");
  task_environment_.FastForwardBy(base::Seconds(1));
  ASSERT_EQ(sockets.front()->sent.size(), 2u);
  task_environment_.FastForwardBy(base::Milliseconds(999));
  EXPECT_EQ(sockets.size(), 1u);
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(sockets.front()->close_count, 1);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(sockets.size(), 2u);
}

}  // namespace
}  // namespace maho::ai
