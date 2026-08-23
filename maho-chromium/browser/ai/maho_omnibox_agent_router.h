// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_OMNIBOX_AGENT_ROUTER_H_
#define MAHO_BROWSER_AI_MAHO_OMNIBOX_AGENT_ROUTER_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"

namespace maho::ai {

// Result of classifying an omnibox / address bar input.
struct OmniboxClassification {
  OmniboxClassification();
  ~OmniboxClassification();
  OmniboxClassification(const OmniboxClassification&);
  OmniboxClassification& operator=(const OmniboxClassification&);
  OmniboxClassification(OmniboxClassification&&);
  OmniboxClassification& operator=(OmniboxClassification&&);

  bool is_agent_query = false;
  std::string raw_input;
  std::string extracted_prompt;
  std::string suggested_action = "submit";  // "submit", "queue", "steer"
};

// Agent suggestion payload for UI / AI panel consumption.
struct AgentSuggestion {
  AgentSuggestion();
  ~AgentSuggestion();
  AgentSuggestion(const AgentSuggestion&);
  AgentSuggestion& operator=(const AgentSuggestion&);
  AgentSuggestion(AgentSuggestion&&);
  AgentSuggestion& operator=(AgentSuggestion&&);

  std::string display_title;
  std::string prompt;
  std::string destination = "ai_panel";
  bool is_agent_query = true;
};

// Router and classifier for agent queries initiated from the omnibox/command bar.
//
// Omnibox agent prefix syntax:
//   "> <query>"  e.g., "> summarize my recent docs about project X"
//
// Plain text or URLs (e.g. "search query", "https://google.com") are NOT classified
// as agent queries and pass through to standard navigation/search.
class MahoOmniboxAgentRouter {
 public:
  // Returns true if the input starts with the agent prefix ("> ").
  static bool IsAgentPrefix(std::string_view input);
  static bool IsAgentPrefix(std::u16string_view input);

  // Classifies an input string.
  static OmniboxClassification ClassifyInput(std::string_view input);
  static OmniboxClassification ClassifyInput(std::u16string_view input);

  // Creates an AgentSuggestion if the input matches the agent prefix pattern.
  static std::optional<AgentSuggestion> CreateAgentSuggestion(std::string_view input);
  static std::optional<AgentSuggestion> CreateAgentSuggestion(std::u16string_view input);

  // Routes an agent query to the active AI runtime adapter.
  // Returns true if successfully routed/submitted to the adapter.
  static bool RouteToAgentSession(
      MahoAiRuntimeAdapter* adapter,
      const std::string& prompt,
      bool attach_page_context = true,
      MahoAiRuntimeAdapter::RuntimeEventCallback callback =
          MahoAiRuntimeAdapter::RuntimeEventCallback());
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_OMNIBOX_AGENT_ROUTER_H_
