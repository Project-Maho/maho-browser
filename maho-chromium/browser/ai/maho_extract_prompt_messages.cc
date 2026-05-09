// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_extract_prompt_messages.h"

#include "base/json/json_reader.h"

namespace maho::ai {

std::optional<base::ListValue> ExtractPromptMessages(
    const base::DictValue& llm_request) {
  const base::Value* prompt_value = llm_request.Find("prompt");
  if (!prompt_value) {
    return std::nullopt;
  }

  if (prompt_value->is_list()) {
    return prompt_value->GetList().Clone();
  }

  if (prompt_value->is_dict()) {
    const base::ListValue* messages =
        prompt_value->GetDict().FindList("messages");
    if (messages) {
      return messages->Clone();
    }
    return std::nullopt;
  }

  if (prompt_value->is_string()) {
    auto parsed = base::JSONReader::Read(prompt_value->GetString(),
                                         base::JSON_PARSE_RFC);
    if (!parsed) {
      return std::nullopt;
    }
    if (parsed->is_list()) {
      return std::move(parsed->GetList());
    }
    if (parsed->is_dict()) {
      const base::ListValue* messages =
          parsed->GetDict().FindList("messages");
      if (messages) {
        return messages->Clone();
      }
    }
    return std::nullopt;
  }

  return std::nullopt;
}

}  // namespace maho::ai
