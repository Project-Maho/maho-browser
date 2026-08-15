// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_sync_relay_protocol.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoSyncRelayClientTest, AcceptsStrictV2Envelope) {
  EXPECT_EQ(
      MahoSyncRelayFrameKind::kEnvelope,
      ClassifyMahoSyncRelayFrame(
          R"({"protocol_version":2,"delivery_id":"550e8400-e29b-41d4-a716-446655440000","payload":"AA==","relay_seq":5})"));
}

TEST(MahoSyncRelayClientTest, RejectsMalformedV2Envelope) {
  EXPECT_EQ(
      MahoSyncRelayFrameKind::kInvalid,
      ClassifyMahoSyncRelayFrame(
          R"({"protocol_version":1,"delivery_id":"550e8400-e29b-41d4-a716-446655440000","payload":"AA==","relay_seq":5})"));
  EXPECT_EQ(
      MahoSyncRelayFrameKind::kInvalid,
      ClassifyMahoSyncRelayFrame(
          R"({"protocol_version":2,"delivery_id":"550e8400-e29b-41d4-a716-446655440000","payload":"AA=="})"));
}

TEST(MahoSyncRelayClientTest, ValidatesAcknowledgementSequence) {
  EXPECT_EQ(
      MahoSyncRelayFrameKind::kAck,
      ClassifyMahoSyncRelayFrame(
          R"({"type":"ack","delivery_id":"550e8400-e29b-41d4-a716-446655440000","seq":5})"));
  EXPECT_EQ(
      MahoSyncRelayFrameKind::kInvalid,
      ClassifyMahoSyncRelayFrame(
          R"({"type":"ack","delivery_id":"550e8400-e29b-41d4-a716-446655440000","seq":-1})"));
}

TEST(MahoSyncRelayClientTest, RecognizesControlFrames) {
  EXPECT_EQ(MahoSyncRelayFrameKind::kAuthOk,
            ClassifyMahoSyncRelayFrame(R"({"type":"auth_ok"})"));
  EXPECT_EQ(MahoSyncRelayFrameKind::kCatchupDone,
            ClassifyMahoSyncRelayFrame(R"({"type":"catchup_done"})"));
}

}  // namespace maho
