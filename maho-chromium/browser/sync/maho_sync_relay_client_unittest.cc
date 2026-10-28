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

TEST(MahoSyncRelayClientTest, RecognizesRoomRetired) {
  EXPECT_EQ(MahoSyncRelayFrameKind::kRoomRetired,
            ClassifyMahoSyncRelayFrame(
                R"({"type":"error","msg":"room_retired"})"));
  EXPECT_EQ(MahoSyncRelayFrameKind::kInvalid,
            ClassifyMahoSyncRelayFrame(
                R"({"type":"error","msg":"room_owned_by_another_user"})"));
  EXPECT_TRUE(IsMahoSyncRoomRetiredResponse(
      410, R"({"error":"room_retired","code":"room_retired"})"));
  EXPECT_TRUE(IsMahoSyncRoomRetiredResponse(410, R"({"code":"room_retired"})"));
  EXPECT_FALSE(IsMahoSyncRoomRetiredResponse(410, ""));
  EXPECT_FALSE(IsMahoSyncRoomRetiredResponse(410, R"({"code":"gone"})"));
  EXPECT_FALSE(IsMahoSyncRoomRetiredResponse(409, R"({"code":"client_held"})"));
  EXPECT_FALSE(
      IsMahoSyncRoomRetiredResponse(200, R"({"code":"room_retired"})"));
}

}  // namespace maho
