// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_acp_event_decoder.h"

#include <cstring>
#include <vector>

#include "base/compiler_specific.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

std::vector<uint8_t> MakeBlob(const std::string& json) {
  uint32_t len = static_cast<uint32_t>(json.size());
  std::vector<uint8_t> blob(4 + json.size());
  UNSAFE_BUFFERS(std::memcpy(blob.data(), &len, 4));
  UNSAFE_BUFFERS(
      std::memcpy(blob.data() + 4, json.data(), json.size()));
  return blob;
}

TEST(MahoAcpEventDecoderTest, WellFormedBlob) {
  auto blob = MakeBlob(R"({"kind":"InitializeOk","data":{}})");
  auto result = MahoAcpEventDecoder::Decode(blob);
  ASSERT_TRUE(result.has_value());
  const std::string* kind = result->FindString("kind");
  ASSERT_TRUE(kind);
  EXPECT_EQ(*kind, "InitializeOk");
}

TEST(MahoAcpEventDecoderTest, TruncatedLengthPrefix) {
  std::vector<uint8_t> blob = {0x01, 0x00, 0x00};
  auto result = MahoAcpEventDecoder::Decode(blob);
  EXPECT_FALSE(result.has_value());
}

TEST(MahoAcpEventDecoderTest, LengthLargerThanBuffer) {
  std::string json = R"({"kind":"x"})";
  uint32_t fake_len = 9999;
  std::vector<uint8_t> blob(4 + json.size());
  UNSAFE_BUFFERS(std::memcpy(blob.data(), &fake_len, 4));
  UNSAFE_BUFFERS(
      std::memcpy(blob.data() + 4, json.data(), json.size()));
  auto result = MahoAcpEventDecoder::Decode(blob);
  EXPECT_FALSE(result.has_value());
}

TEST(MahoAcpEventDecoderTest, InvalidJson) {
  auto blob = MakeBlob("{not valid json!!!");
  auto result = MahoAcpEventDecoder::Decode(blob);
  EXPECT_FALSE(result.has_value());
}

TEST(MahoAcpEventDecoderTest, EmptyBlob) {
  std::vector<uint8_t> blob;
  auto result = MahoAcpEventDecoder::Decode(blob);
  EXPECT_FALSE(result.has_value());
}

TEST(MahoAcpEventDecoderTest, JsonArrayRejected) {
  auto blob = MakeBlob(R"([1,2,3])");
  auto result = MahoAcpEventDecoder::Decode(blob);
  EXPECT_FALSE(result.has_value());
}

}  // namespace
