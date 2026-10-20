// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_text_insertion_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoMcpTextInsertionPolicyTest, ContentEditablePrefersBeforeInput) {
  EXPECT_EQ(ChooseInsertionStrategies("contenteditable", ""),
            (std::vector<TextInsertionStrategy>{
                TextInsertionStrategy::kBeforeInput,
                TextInsertionStrategy::kKeyboard,
                TextInsertionStrategy::kPaste,
                TextInsertionStrategy::kExecCommand}));
}

TEST(MahoMcpTextInsertionPolicyTest, UnknownKindFallsBackToKeyboard) {
  EXPECT_EQ(ChooseInsertionStrategies("unknown", ""),
            (std::vector<TextInsertionStrategy>{
                TextInsertionStrategy::kKeyboard,
                TextInsertionStrategy::kBeforeInput,
                TextInsertionStrategy::kPaste,
                TextInsertionStrategy::kExecCommand}));
}

}  // namespace maho
