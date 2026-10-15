// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_text_insertion_policy.h"

namespace maho {

std::vector<TextInsertionStrategy> ChooseInsertionStrategies(
    const std::string& element_kind,
    const std::string& editor_hint) {
  if (element_kind == "contenteditable" || editor_hint == "contenteditable") {
    return {TextInsertionStrategy::kBeforeInput,
            TextInsertionStrategy::kKeyboard,
            TextInsertionStrategy::kPaste,
            TextInsertionStrategy::kExecCommand};
  }

  return {TextInsertionStrategy::kKeyboard,
          TextInsertionStrategy::kBeforeInput,
          TextInsertionStrategy::kPaste,
          TextInsertionStrategy::kExecCommand};
}

TextInsertionResult ExecuteTextInsertionCascade(
    const std::vector<TextInsertionStrategy>& strategies,
    const std::function<void(TextInsertionStrategy)>& attempt,
    const std::function<bool()>& readback) {
  for (TextInsertionStrategy strategy : strategies) {
    attempt(strategy);
    if (readback()) {
      switch (strategy) {
        case TextInsertionStrategy::kKeyboard:
          return TextInsertionResult::kKeyboard;
        case TextInsertionStrategy::kBeforeInput:
          return TextInsertionResult::kBeforeInput;
        case TextInsertionStrategy::kPaste:
          return TextInsertionResult::kPaste;
        case TextInsertionStrategy::kExecCommand:
          return TextInsertionResult::kExecCommand;
      }
    }
  }
  return TextInsertionResult::kInsertionNotObserved;
}

}  // namespace maho
