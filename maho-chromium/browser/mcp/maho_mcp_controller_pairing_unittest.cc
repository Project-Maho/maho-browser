// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_controller_pairing.h"

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoMcpControllerPairingTest, IssueVerifyAndPersistDigestOnly) {
  base::DictValue prefs;
  const std::string token = MahoMcpControllerPairing::Issue(prefs);

  ASSERT_EQ(token.size(), 64u);
  EXPECT_TRUE(MahoMcpControllerPairing::Verify(prefs, token));
  const base::ListValue* hashes =
      prefs.FindList(MahoMcpControllerPairing::kPrefName);
  ASSERT_TRUE(hashes);
  ASSERT_EQ(hashes->size(), 1u);
  const std::string* stored_hash = hashes->front().GetIfString();
  ASSERT_TRUE(stored_hash);
  EXPECT_NE(*stored_hash, token);
  EXPECT_EQ(stored_hash->size(), 64u);
}

TEST(MahoMcpControllerPairingTest, RejectsForgedAndRevokedTokens) {
  base::DictValue prefs;
  const std::string token = MahoMcpControllerPairing::Issue(prefs);

  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, token + "00"));
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, "forged"));
  EXPECT_TRUE(MahoMcpControllerPairing::Revoke(prefs, token));
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, token));
  EXPECT_FALSE(MahoMcpControllerPairing::Revoke(prefs, token));
}

TEST(MahoMcpControllerPairingTest, MissingOrMalformedPreferenceFailsClosed) {
  base::DictValue prefs;
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, "token"));
  EXPECT_FALSE(MahoMcpControllerPairing::Revoke(prefs, "token"));

  prefs.Set(MahoMcpControllerPairing::kPrefName, "malformed");
  EXPECT_FALSE(MahoMcpControllerPairing::Verify(prefs, "token"));
  EXPECT_FALSE(MahoMcpControllerPairing::Revoke(prefs, "token"));
}

}  // namespace
}  // namespace maho
