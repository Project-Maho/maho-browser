// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_oauth_session.h"

#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

class MahoMailOAuthSessionTest : public content::RenderViewHostTestHarness {};

TEST_F(MahoMailOAuthSessionTest, CancelOnDestruction) {
  int cancel_calls = 0;
  auto cancel_closure = base::BindOnce([](int* calls) { (*calls)++; }, &cancel_calls);

  std::unique_ptr<content::WebContents> test_web_contents = CreateTestWebContents();
  MahoMailOAuthSession::CreateForWebContents(test_web_contents.get(), "state_123", std::move(cancel_closure));

  MahoMailOAuthSession* session = MahoMailOAuthSession::FromWebContents(test_web_contents.get());
  ASSERT_NE(nullptr, session);
  EXPECT_EQ("state_123", session->state());

  test_web_contents.reset();
  EXPECT_EQ(1, cancel_calls);
}

TEST_F(MahoMailOAuthSessionTest, NoCancelOnComplete) {
  int cancel_calls = 0;
  auto cancel_closure = base::BindOnce([](int* calls) { (*calls)++; }, &cancel_calls);

  std::unique_ptr<content::WebContents> test_web_contents = CreateTestWebContents();
  MahoMailOAuthSession::CreateForWebContents(test_web_contents.get(), "state_123", std::move(cancel_closure));

  MahoMailOAuthSession* session = MahoMailOAuthSession::FromWebContents(test_web_contents.get());
  ASSERT_NE(nullptr, session);
  session->MarkComplete();

  test_web_contents.reset();
  EXPECT_EQ(0, cancel_calls);
}

TEST_F(MahoMailOAuthSessionTest, MarkCompleteForMatchingState) {
  int cancel_calls = 0;
  auto cancel_closure =
      base::BindOnce([](int* calls) { (*calls)++; }, &cancel_calls);

  std::unique_ptr<content::WebContents> test_web_contents =
      CreateTestWebContents();
  MahoMailOAuthSession::CreateForWebContents(
      test_web_contents.get(), "state_123", std::move(cancel_closure));

  EXPECT_TRUE(MahoMailOAuthSession::MarkCompleteForWebContents(
      test_web_contents.get(), "state_123"));

  test_web_contents.reset();
  EXPECT_EQ(0, cancel_calls);
}

TEST_F(MahoMailOAuthSessionTest, MarkCompleteRejectsMismatchedState) {
  int cancel_calls = 0;
  auto cancel_closure =
      base::BindOnce([](int* calls) { (*calls)++; }, &cancel_calls);

  std::unique_ptr<content::WebContents> test_web_contents =
      CreateTestWebContents();
  MahoMailOAuthSession::CreateForWebContents(
      test_web_contents.get(), "state_123", std::move(cancel_closure));

  EXPECT_FALSE(MahoMailOAuthSession::MarkCompleteForWebContents(
      test_web_contents.get(), "other_state"));

  test_web_contents.reset();
  EXPECT_EQ(1, cancel_calls);
}

TEST_F(MahoMailOAuthSessionTest, ParseStartResultAcceptsHttpsUrlAndState) {
  MahoMailOAuthSession::StartResult result =
      MahoMailOAuthSession::ParseStartResult(
          R"({"auth_url":"https://accounts.google.com/o/oauth2/v2/auth","state":"state_123"})");

  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.error_json.empty());
  EXPECT_EQ("state_123", result.state);
  EXPECT_EQ(GURL("https://accounts.google.com/o/oauth2/v2/auth"),
            result.auth_url);
}

TEST_F(MahoMailOAuthSessionTest, ParseStartResultRejectsHttpUrl) {
  MahoMailOAuthSession::StartResult result =
      MahoMailOAuthSession::ParseStartResult(
          R"({"auth_url":"http://accounts.google.com/o/oauth2/v2/auth","state":"state_123"})");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ("Invalid authorization URL", result.error_json);
}

TEST_F(MahoMailOAuthSessionTest, ParseStartResultRejectsInvalidUrl) {
  MahoMailOAuthSession::StartResult result =
      MahoMailOAuthSession::ParseStartResult(
          R"({"auth_url":"not a url","state":"state_123"})");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ("Invalid authorization URL", result.error_json);
}

TEST_F(MahoMailOAuthSessionTest, ParseStartResultRequiresState) {
  MahoMailOAuthSession::StartResult result =
      MahoMailOAuthSession::ParseStartResult(
          R"({"auth_url":"https://accounts.google.com/o/oauth2/v2/auth"})");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ("No OAuth state", result.error_json);
}

TEST_F(MahoMailOAuthSessionTest, IsLoopbackSuccessPredicate) {
  EXPECT_TRUE(MahoMailOAuthSession::IsLoopbackSuccess(200));
  EXPECT_FALSE(MahoMailOAuthSession::IsLoopbackSuccess(400));
  EXPECT_FALSE(MahoMailOAuthSession::IsLoopbackSuccess(404));
  EXPECT_FALSE(MahoMailOAuthSession::IsLoopbackSuccess(500));
}

}  // namespace maho
