// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_EXTRACT_PROMPT_MESSAGES_H_
#define MAHO_BROWSER_AI_MAHO_EXTRACT_PROMPT_MESSAGES_H_

#include <optional>

#include "base/values.h"

namespace maho::ai {

// Extracts prompt messages from a Rust/engine response dict.
//
// The engine may return `prompt` in several shapes:
//   1. A JSON list of message objects (most common).
//   2. A JSON dict containing a `messages` key with a list.
//   3. A stringified JSON list (double-encoded).
//   4. A stringified JSON dict with a `messages` key.
//
// Returns the extracted list on success, or std::nullopt if the
// prompt field is missing or unparseable.
std::optional<base::ListValue> ExtractPromptMessages(
    const base::DictValue& llm_request);

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_EXTRACT_PROMPT_MESSAGES_H_
