// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_omnibox_agent_router.h"

#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom-shared.h"

namespace maho::ai {

OmniboxClassification::OmniboxClassification() = default;
OmniboxClassification::~OmniboxClassification() = default;
OmniboxClassification::OmniboxClassification(const OmniboxClassification&) = default;
OmniboxClassification& OmniboxClassification::operator=(const OmniboxClassification&) = default;
OmniboxClassification::OmniboxClassification(OmniboxClassification&&) = default;
OmniboxClassification& OmniboxClassification::operator=(OmniboxClassification&&) = default;

AgentSuggestion::AgentSuggestion() = default;
AgentSuggestion::~AgentSuggestion() = default;
AgentSuggestion::AgentSuggestion(const AgentSuggestion&) = default;
AgentSuggestion& AgentSuggestion::operator=(const AgentSuggestion&) = default;
AgentSuggestion::AgentSuggestion(AgentSuggestion&&) = default;
AgentSuggestion& AgentSuggestion::operator=(AgentSuggestion&&) = default;

namespace {

constexpr std::string_view kAgentPrefix = "> ";
constexpr std::u16string_view kAgentPrefix16 = u"> ";

std::string TrimLeadingAgentMarker(std::string_view input) {
  std::string trimmed;
  base::TrimWhitespaceASCII(input, base::TRIM_ALL, &trimmed);
  if (base::StartsWith(trimmed, ">")) {
    trimmed = trimmed.substr(1);
    base::TrimWhitespaceASCII(trimmed, base::TRIM_ALL, &trimmed);
  }
  return trimmed;
}

}  // namespace

// static
bool MahoOmniboxAgentRouter::IsAgentPrefix(std::string_view input) {
  std::string trimmed;
  base::TrimWhitespaceASCII(input, base::TRIM_LEADING, &trimmed);
  return base::StartsWith(trimmed, kAgentPrefix) || trimmed == ">";
}

// static
bool MahoOmniboxAgentRouter::IsAgentPrefix(std::u16string_view input) {
  std::u16string trimmed;
  base::TrimWhitespace(input, base::TRIM_LEADING, &trimmed);
  return base::StartsWith(trimmed, kAgentPrefix16) || trimmed == u">";
}

// static
OmniboxClassification MahoOmniboxAgentRouter::ClassifyInput(
    std::string_view input) {
  OmniboxClassification classification;
  classification.raw_input = std::string(input);

  if (!IsAgentPrefix(input)) {
    classification.is_agent_query = false;
    classification.extracted_prompt.clear();
    return classification;
  }

  classification.is_agent_query = true;
  classification.extracted_prompt = TrimLeadingAgentMarker(input);
  classification.suggested_action = "submit";
  return classification;
}

// static
OmniboxClassification MahoOmniboxAgentRouter::ClassifyInput(
    std::u16string_view input) {
  return ClassifyInput(base::UTF16ToUTF8(input));
}

// static
std::optional<AgentSuggestion> MahoOmniboxAgentRouter::CreateAgentSuggestion(
    std::string_view input) {
  OmniboxClassification classification = ClassifyInput(input);
  if (!classification.is_agent_query) {
    return std::nullopt;
  }

  AgentSuggestion suggestion;
  suggestion.prompt = classification.extracted_prompt;
  if (suggestion.prompt.empty()) {
    suggestion.display_title = "Ask Maho AI Agent...";
  } else {
    suggestion.display_title = "Ask Maho Agent: " + suggestion.prompt;
  }
  suggestion.destination = "ai_panel";
  suggestion.is_agent_query = true;
  return suggestion;
}

// static
std::optional<AgentSuggestion> MahoOmniboxAgentRouter::CreateAgentSuggestion(
    std::u16string_view input) {
  return CreateAgentSuggestion(base::UTF16ToUTF8(input));
}

// static
std::vector<AgentSuggestion>
MahoOmniboxAgentRouter::CreateSuggestedTaskSuggestions(
    const std::vector<AgentSuggestedTask>& tasks) {
  std::vector<AgentSuggestion> suggestions;
  suggestions.reserve(tasks.size());
  for (const auto& task : tasks) {
    if (task.title.empty() && task.prompt.empty()) {
      continue;
    }
    AgentSuggestion suggestion;
    suggestion.display_title = task.title.empty()
        ? "Ask Maho Agent: " + task.prompt
        : task.title;
    suggestion.prompt = task.prompt;
    suggestion.destination = "ai_panel";
    suggestion.is_agent_query = true;
    suggestions.push_back(std::move(suggestion));
  }
  return suggestions;
}

// static
bool MahoOmniboxAgentRouter::RouteToAgentSession(
    MahoAiRuntimeAdapter* adapter,
    const std::string& prompt,
    bool attach_page_context,
    MahoAiRuntimeAdapter::RuntimeEventCallback callback) {
  if (!adapter || !adapter->IsAvailable()) {
    return false;
  }

  std::string trimmed;
  base::TrimWhitespaceASCII(prompt, base::TRIM_ALL, &trimmed);
  if (trimmed.empty()) {
    return false;
  }

  // Follow-up proof: when a turn is already active, route to SubmitFollowUp
  if (adapter->IsTurnActive()) {
    return adapter->SubmitFollowUp(trimmed, "queue");
  }

  if (callback.is_null()) {
    callback = base::DoNothing();
  }

  adapter->SubmitMessage(
      trimmed,
      maho_ai::mojom::ChatIntent::kFreeform,
      attach_page_context,
      maho_ai::mojom::InteractionMode::kAssistant,
      std::move(callback));
  return true;
}

}  // namespace maho::ai
