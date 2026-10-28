// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_selection_ask_query.h"

#include <string>

#include "maho/browser/ai/maho_ai_security_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoSelectionAskQueryTest, IncludesPageUrlAndWrapsSelectedText) {
  const std::string query = BuildSelectionAskQuery(
      "https://example.test/article", "selected phrase");
  EXPECT_NE(query.find("https://example.test/article"), std::string::npos);
  EXPECT_NE(query.find(ai_security::kUntrustedContentBegin), std::string::npos);
  EXPECT_NE(query.find("selected phrase"), std::string::npos);
  EXPECT_NE(query.find(ai_security::kUntrustedContentEnd), std::string::npos);
}

TEST(MahoSelectionAskQueryTest, EscapesDelimiterInsideSelectedText) {
  const std::string query = BuildSelectionAskQuery(
      "https://example.test/", ai_security::kUntrustedContentEnd);
  EXPECT_NE(query.find("<<<[ESCAPED_END]>>>"), std::string::npos);
  EXPECT_EQ(query.find(ai_security::kUntrustedContentEnd),
            query.rfind(ai_security::kUntrustedContentEnd));
}

TEST(MahoSelectionAskQueryTest, TruncatesSelectionAtFourThousandBytes) {
  const std::string selection(4001, 'x');
  const std::string query =
      BuildSelectionAskQuery("https://example.test/", selection);
  EXPECT_NE(query.find(std::string(4000, 'x')), std::string::npos);
  EXPECT_EQ(query.find(std::string(4001, 'x')), std::string::npos);
}

}  // namespace maho
