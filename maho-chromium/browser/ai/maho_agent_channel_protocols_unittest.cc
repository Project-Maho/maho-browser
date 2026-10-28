// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_channel_gateway.h"

#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {

TEST(MahoAgentChannelProtocolsTest, BackoffCapsAndResets) {
  ChannelBackoff backoff(base::Seconds(2), base::Seconds(5));
  EXPECT_EQ(backoff.OnFailure(), base::Seconds(2));
  EXPECT_EQ(backoff.OnFailure(), base::Seconds(4));
  EXPECT_EQ(backoff.OnFailure(), base::Seconds(5));
  backoff.OnSuccess();
  EXPECT_EQ(backoff.current(), base::Seconds(2));
}

TEST(MahoAgentChannelProtocolsTest, TelegramParsesUpdatesAndComputesNextOffset) {
  ChannelConfig config("telegram", ChannelProvider::kTelegram, "Telegram");
  std::vector<ChannelEvent> events;
  std::string next_offset;
  std::string error;
  EXPECT_TRUE(ParseTelegramUpdates(
      R"({"ok":true,"result":[{"update_id":8,"message":{"date":1700000000,"text":"hello","from":{"id":4,"username":"alice"},"chat":{"id":7}}}]})",
      config, &events, &next_offset, &error));
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].sender_id, "4");
  EXPECT_EQ(events[0].cursor, "9");
  EXPECT_EQ(next_offset, "9");
}

TEST(MahoAgentChannelProtocolsTest, SlackHelloAndEnvelopeAcknowledgement) {
  SlackSocketProtocol slack;
  EXPECT_TRUE(slack.OnHello(R"({"type":"hello","data":{"heartbeat_interval":30000}})"));
  EXPECT_EQ(slack.heartbeat_interval_ms(), 30000u);
  EXPECT_TRUE(slack.OnFrame(R"({"envelope_id":"e1","payload":{"event":{"type":"message"}}})"));
  const auto acknowledgements = slack.TakeAcknowledgements();
  ASSERT_EQ(acknowledgements.size(), 1u);
  EXPECT_NE(acknowledgements[0].find("e1"), std::string::npos);
}

TEST(MahoAgentChannelProtocolsTest, DiscordHelloHeartbeatResumeAndInvalidSession) {
  DiscordGatewayProtocol discord("token");
  EXPECT_EQ(discord.OnFrame(R"({"op":10,"d":{"heartbeat_interval":45000}})").outbound_frames.size(), 1u);
  discord.OnFrame(R"({"op":0,"s":12,"t":"READY","d":{"session_id":"sid","resume_gateway_url":"wss://resume.test"}})");
  EXPECT_TRUE(discord.should_resume());
  EXPECT_EQ(discord.OnFrame(R"({"op":7})").reconnect, true);
  EXPECT_EQ(discord.ResumeFrames().size(), 1u);
  const auto invalid = discord.OnFrame(R"({"op":9,"d":false})");
  EXPECT_TRUE(invalid.invalid_session);
  EXPECT_FALSE(discord.should_resume());
}

}  // namespace maho::ai
