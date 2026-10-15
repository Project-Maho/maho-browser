#include "maho/browser/ai/maho_agent_channel_telegram_transport.h"

#include <string>
#include <vector>
#include <optional>

#include "base/test/task_environment.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {
namespace {

class TelegramChannelTransportTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
};

TEST_F(TelegramChannelTransportTest, PollOffsetUsesLastUpdateIdPlusOne) {
  auto shared = factory_.GetSafeWeakWrapper();
  TelegramChannelHttpTransport transport(CreateTelegramUrlLoaderRequester(shared));
  ChannelConfig config("tg", ChannelProvider::kTelegram, "Telegram");
  config.token = ChannelToken("safe-token");
  std::vector<ChannelEvent> events;
  std::optional<std::string> error;
  transport.FetchEvents(config, std::nullopt,
      base::BindOnce([](std::vector<ChannelEvent>* events,
                        std::optional<std::string>* error,
                        std::vector<ChannelEvent> result,
                        std::optional<std::string> failure) {
        *events = std::move(result);
        *error = std::move(failure);
      }, &events, &error));
  const std::string body = R"({"ok":true,"result":[{"update_id":41,"message":{"date":1700000000,"text":"hi","from":{"id":8},"chat":{"id":9}}}]})";
  auto headers = base::MakeRefCounted<net::HttpResponseHeaders>("HTTP/1.1 200 OK\0\0");
  factory_.AddResponse(GURL("https://api.telegram.org/botsafe-token/getUpdates?timeout=25&allowed_updates=%5B%22message%22%5D"), headers, body);
  task_environment_.RunUntilIdle();
  ASSERT_FALSE(error.has_value());
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].cursor, "42");
}

TEST_F(TelegramChannelTransportTest, NonSuccessResponseIsReportedWithoutToken) {
  TelegramChannelHttpTransport transport(CreateTelegramUrlLoaderRequester(
      factory_.GetSafeWeakWrapper()));
  ChannelConfig config("tg", ChannelProvider::kTelegram, "Telegram");
  config.token = ChannelToken("secret-bot-token");
  std::optional<std::string> error;
  transport.FetchEvents(config, std::nullopt,
      base::BindOnce([](std::optional<std::string>* error,
                        std::vector<ChannelEvent>,
                        std::optional<std::string> failure) {
        *error = std::move(failure);
      }, &error));
  auto headers = base::MakeRefCounted<net::HttpResponseHeaders>("HTTP/1.1 503 Error\0\0");
  factory_.AddResponse(GURL("https://api.telegram.org/botsecret-bot-token/getUpdates?timeout=25&allowed_updates=%5B%22message%22%5D"), headers, "{}");
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->find("secret-bot-token"), std::string::npos);
}

TEST_F(TelegramChannelTransportTest, SendMessagePostsJsonChatAndText) {
  auto shared = factory_.GetSafeWeakWrapper();
  TelegramChannelHttpTransport transport(CreateTelegramUrlLoaderRequester(shared));
  ChannelConfig config("tg", ChannelProvider::kTelegram, "Telegram");
  config.token = ChannelToken("send-token");
  bool sent = false;
  transport.SendMessage(config, "8", "pair 123456",
      base::BindOnce([](bool* sent, bool ok) { *sent = ok; }, &sent));
  auto* pending = factory_.GetPendingRequest(0);
  ASSERT_NE(pending, nullptr);
  EXPECT_EQ(pending->request.method, "POST");
  EXPECT_NE(pending->request.url.spec().find("send-token"), std::string::npos);
  EXPECT_TRUE(pending->request.request_body);
  factory_.AddResponse(GURL("https://api.telegram.org/botsend-token/sendMessage"), R"({"ok":true})");
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(sent);
}

}
}
