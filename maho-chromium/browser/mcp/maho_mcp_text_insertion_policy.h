// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_TEXT_INSERTION_POLICY_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_TEXT_INSERTION_POLICY_H_

#include <functional>
#include <string>
#include <vector>

namespace maho {

enum class TextInsertionStrategy {
  kKeyboard,
  kBeforeInput,
  kPaste,
  kExecCommand,
};

enum class TextInsertionResult {
  kKeyboard,
  kBeforeInput,
  kPaste,
  kExecCommand,
  kInsertionNotObserved,
};

std::vector<TextInsertionStrategy> ChooseInsertionStrategies(
    const std::string& element_kind,
    const std::string& editor_hint);

TextInsertionResult ExecuteTextInsertionCascade(
    const std::vector<TextInsertionStrategy>& strategies,
    const std::function<void(TextInsertionStrategy)>& attempt,
    const std::function<bool()>& readback);

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_TEXT_INSERTION_POLICY_H_
