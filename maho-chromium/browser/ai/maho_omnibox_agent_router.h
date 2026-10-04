// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_OMNIBOX_AGENT_ROUTER_H_
#define MAHO_BROWSER_AI_MAHO_OMNIBOX_AGENT_ROUTER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// Backend suggested-task card payload (plan row 12). Mirrors the
// maho-agent suggestions.rs Suggestion{title, prompt} contract for UI ->
// router input.
struct AgentSuggestedTask {
  std::string title;
  std::string prompt;
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

  // Surfaces backend suggested-task cards as omnibox agent suggestions
  // (plan row 12). Read-only wiring: the returned suggestions feed router
  // input only — nothing here executes a task or routes a session, and the
  // existing agent-prefix trigger matching is untouched. An empty or fully
  // blank input list yields an empty result.
  static std::vector<AgentSuggestion> CreateSuggestedTaskSuggestions(
      const std::vector<AgentSuggestedTask>& tasks);

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
