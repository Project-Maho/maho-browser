// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_omnibox_agent_router.h"

#include <string>

#include "base/functional/callback_helpers.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

class MahoOmniboxAgentRouterTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

// Tests classifier distinguishes "> " agent queries from plain text and URLs.
TEST_F(MahoOmniboxAgentRouterTest, ClassifiesAgentPrefixCorrectly) {
  // Agent prefix queries
  EXPECT_TRUE(MahoOmniboxAgentRouter::IsAgentPrefix("> summarize docs"));
  EXPECT_TRUE(MahoOmniboxAgentRouter::IsAgentPrefix("> "));
  EXPECT_TRUE(MahoOmniboxAgentRouter::IsAgentPrefix(">"));
  EXPECT_TRUE(MahoOmniboxAgentRouter::IsAgentPrefix("  > find my email"));

  // Plain queries and URLs must NOT be classified as agent queries
  EXPECT_FALSE(MahoOmniboxAgentRouter::IsAgentPrefix("plain query"));
  EXPECT_FALSE(MahoOmniboxAgentRouter::IsAgentPrefix("https://example.com"));
  EXPECT_FALSE(MahoOmniboxAgentRouter::IsAgentPrefix("www.google.com"));
  EXPECT_FALSE(MahoOmniboxAgentRouter::IsAgentPrefix("help me > please"));
}

// Tests extracted prompt trimming and suggestion construction.
TEST_F(MahoOmniboxAgentRouterTest, ExtractsPromptAndBuildsSuggestion) {
  const std::string input = "> summarize my recent docs about project X";
  OmniboxClassification classification = MahoOmniboxAgentRouter::ClassifyInput(input);

  EXPECT_TRUE(classification.is_agent_query);
  EXPECT_EQ(classification.extracted_prompt, "summarize my recent docs about project X");

  auto suggestion = MahoOmniboxAgentRouter::CreateAgentSuggestion(input);
  ASSERT_TRUE(suggestion.has_value());
  EXPECT_EQ(suggestion->prompt, "summarize my recent docs about project X");
  EXPECT_EQ(suggestion->destination, "ai_panel");
  EXPECT_EQ(suggestion->display_title, "Ask Maho Agent: summarize my recent docs about project X");
  EXPECT_TRUE(suggestion->is_agent_query);
}

// Tests empty agent prefix prompt produces default suggestion.
TEST_F(MahoOmniboxAgentRouterTest, EmptyAgentPrefixHasDefaultTitle) {
  auto suggestion = MahoOmniboxAgentRouter::CreateAgentSuggestion("> ");
  ASSERT_TRUE(suggestion.has_value());
  EXPECT_EQ(suggestion->prompt, "");
  EXPECT_EQ(suggestion->display_title, "Ask Maho AI Agent...");
}

// Tests plain text returns no suggestion.
TEST_F(MahoOmniboxAgentRouterTest, PlainQueryReturnsNoAgentSuggestion) {
  OmniboxClassification classification =
      MahoOmniboxAgentRouter::ClassifyInput("best restaurants nearby");
  EXPECT_FALSE(classification.is_agent_query);
  EXPECT_TRUE(classification.extracted_prompt.empty());

  auto suggestion =
      MahoOmniboxAgentRouter::CreateAgentSuggestion("best restaurants nearby");
  EXPECT_FALSE(suggestion.has_value());
}

// Tests UTF-16 overload for native Omnibox string view.
TEST_F(MahoOmniboxAgentRouterTest, Utf16OverloadWorks) {
  std::u16string input = u"> 한국어 검색 요청";
  EXPECT_TRUE(MahoOmniboxAgentRouter::IsAgentPrefix(input));

  auto suggestion = MahoOmniboxAgentRouter::CreateAgentSuggestion(input);
  ASSERT_TRUE(suggestion.has_value());
  EXPECT_EQ(suggestion->prompt, "한국어 검색 요청");
}

// Tests route to null / unavailable adapter fails closed.
TEST_F(MahoOmniboxAgentRouterTest, RouteToNullAdapterFailsClosed) {
  EXPECT_FALSE(MahoOmniboxAgentRouter::RouteToAgentSession(
      nullptr, "summarize this page"));
}

class FakeOmniboxAiRuntimeAdapter : public MahoAiRuntimeAdapter {
 public:
  std::string GetAdapterName() const override { return "FakeOmniboxAdapter"; }
  bool IsAvailable() const override { return is_available_; }
  bool IsTurnActive() const override { return is_turn_active_; }

  void SubmitMessage(const std::string& message,
                     maho_ai::mojom::ChatIntent chat_intent,
                     bool attach_browser_context,
                     maho_ai::mojom::InteractionMode mode,
                     RuntimeEventCallback on_event) override {
    submitted_messages_.push_back(message);
  }

  bool SubmitFollowUp(const std::string& message,
                      const std::string& intent) override {
    submitted_follow_ups_.push_back({message, intent});
    return true;
  }

  void CancelCurrentTurn() override {}
  void RespondToApproval(const std::string& approval_id, bool approved) override {}
  std::string GetRuntimeSessionId() const override { return "mock_session"; }
  void Reset() override {}

  bool is_available_ = true;
  bool is_turn_active_ = false;
  std::vector<std::string> submitted_messages_;
  std::vector<std::pair<std::string, std::string>> submitted_follow_ups_;
};

// Tests omnibox routing uses SubmitMessage when no turn is active,
// and SubmitFollowUp when a turn is already active.
TEST_F(MahoOmniboxAgentRouterTest, OmniboxFollowUpVsNewTurnRouting) {
  FakeOmniboxAiRuntimeAdapter adapter;

  // Case 1: Inactive turn -> routes to SubmitMessage
  adapter.is_turn_active_ = false;
  EXPECT_TRUE(MahoOmniboxAgentRouter::RouteToAgentSession(
      &adapter, "start new task"));
  EXPECT_EQ(adapter.submitted_messages_.size(), 1u);
  EXPECT_EQ(adapter.submitted_messages_[0], "start new task");
  EXPECT_EQ(adapter.submitted_follow_ups_.size(), 0u);

  // Case 2: Active turn -> routes to SubmitFollowUp with queue intent
  adapter.is_turn_active_ = true;
  EXPECT_TRUE(MahoOmniboxAgentRouter::RouteToAgentSession(
      &adapter, "also check email"));
  EXPECT_EQ(adapter.submitted_messages_.size(), 1u);  // SubmitMessage was not called
  ASSERT_EQ(adapter.submitted_follow_ups_.size(), 1u);
  EXPECT_EQ(adapter.submitted_follow_ups_[0].first, "also check email");
  EXPECT_EQ(adapter.submitted_follow_ups_[0].second, "queue");
}

}  // namespace
}  // namespace maho::ai
