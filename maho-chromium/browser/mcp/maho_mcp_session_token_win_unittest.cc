// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session_token_win.h"

#if BUILDFLAG(IS_WIN)

#include <windows.h>

#include <dpapi.h>

#include <array>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoMcpSessionTokenWinTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath GetTempTokenPath() {
    return temp_dir_.GetPath().AppendASCII("mcp-session-token");
  }

  base::ScopedTempDir temp_dir_;
};

// Two calls to Create() produce different 32-byte tokens.
TEST_F(MahoMcpSessionTokenWinTest, GeneratesUniqueTokens) {
  auto token1 = MahoMcpSessionToken::Create();
  auto token2 = MahoMcpSessionToken::Create();
  ASSERT_TRUE(token1);
  ASSERT_TRUE(token2);

  auto raw1 = token1->raw_token_for_testing();
  auto raw2 = token2->raw_token_for_testing();

  ASSERT_EQ(raw1.size(), MahoMcpSessionToken::kTokenSize);
  ASSERT_EQ(raw2.size(), MahoMcpSessionToken::kTokenSize);

  // Tokens must differ (probability of collision is 2^-256).
  EXPECT_NE(std::vector<uint8_t>(raw1.begin(), raw1.end()),
            std::vector<uint8_t>(raw2.begin(), raw2.end()));
}

// Write encrypted token to disk, read ciphertext back, decrypt with
// CryptUnprotectData, and verify the plaintext matches the original.
TEST_F(MahoMcpSessionTokenWinTest, WriteThenDecryptRoundTrip) {
  auto token = MahoMcpSessionToken::Create();
  ASSERT_TRUE(token);

  base::FilePath path = GetTempTokenPath();
  ASSERT_TRUE(token->WriteEncryptedToDisk(path));

  // Verify file exists and is non-empty.
  ASSERT_TRUE(base::PathExists(path));
  std::string ciphertext;
  ASSERT_TRUE(base::ReadFileToString(path, &ciphertext));
  ASSERT_GT(ciphertext.size(), 0u);

  // Decrypt using CryptUnprotectData.
  DATA_BLOB input_blob;
  input_blob.pbData = reinterpret_cast<BYTE*>(ciphertext.data());
  input_blob.cbData = static_cast<DWORD>(ciphertext.size());

  DATA_BLOB output_blob = {};
  ASSERT_TRUE(::CryptUnprotectData(&input_blob, nullptr, nullptr, nullptr,
                                   nullptr, 0, &output_blob));

  ASSERT_EQ(output_blob.cbData, MahoMcpSessionToken::kTokenSize);

  auto raw = token->raw_token_for_testing();
  EXPECT_EQ(memcmp(output_blob.pbData, raw.data(), raw.size()), 0);

  ::LocalFree(output_blob.pbData);
}

// Matches() returns true for identical token, false for different.
TEST_F(MahoMcpSessionTokenWinTest, MatchesConstantTime) {
  auto token = MahoMcpSessionToken::Create();
  ASSERT_TRUE(token);

  auto raw = token->raw_token_for_testing();
  std::vector<uint8_t> correct(raw.begin(), raw.end());
  std::vector<uint8_t> wrong(MahoMcpSessionToken::kTokenSize, 0xFF);

  // Correct token matches.
  EXPECT_TRUE(token->Matches(base::span(correct)));

  // Wrong token does not match.
  EXPECT_FALSE(token->Matches(base::span(wrong)));
}

// Matches() rejects tokens with wrong length (shorter or longer).
TEST_F(MahoMcpSessionTokenWinTest, WrongLengthRejected) {
  auto token = MahoMcpSessionToken::Create();
  ASSERT_TRUE(token);

  auto raw = token->raw_token_for_testing();

  // Shorter by 1 byte.
  std::vector<uint8_t> shorter(raw.begin(), raw.end() - 1);
  EXPECT_FALSE(token->Matches(base::span(shorter)));

  // Longer by 1 byte.
  std::vector<uint8_t> longer(raw.begin(), raw.end());
  longer.push_back(0x42);
  EXPECT_FALSE(token->Matches(base::span(longer)));

  // Empty.
  std::vector<uint8_t> empty;
  EXPECT_FALSE(token->Matches(base::span(empty)));
}

}  // namespace
}  // namespace maho

#endif  // BUILDFLAG(IS_WIN)
