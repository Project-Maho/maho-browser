#include "maho/browser/ai/maho_agent_channel_telegram_transport.h"

#include <string>
#include <vector>
#include <optional>

#include "base/json/json_reader.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "maho/browser/ai/maho_agent_channel_gateway.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
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
  factory_.AddResponse(
      "https://api.telegram.org/botsafe-token/getUpdates?timeout=25&"
      "allowed_updates=%5B%22message%22%5D",
      body);
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
  factory_.AddResponse(
      "https://api.telegram.org/botsecret-bot-token/getUpdates?timeout=25&"
      "allowed_updates=%5B%22message%22%5D",
      "{}", net::HTTP_SERVICE_UNAVAILABLE);
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
  factory_.AddResponse("https://api.telegram.org/botsend-token/sendMessage",
                       R"({"ok":true})");
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(sent);
}

// Todo 45: the gateway driven end to end over the real Telegram HTTP transport,
// pointed at a local fake Bot API through the constructor seam (no runtime
// switch exists). An unpaired sender only gets a pairing code back; after the
// code is approved the same sender's next message is accepted.
TEST_F(TelegramChannelTransportTest, FakeBotApiPairingRoundTrip) {
  constexpr char kFakeApi[] = "http://127.0.0.1:18080";
  const std::string get_updates =
      std::string(kFakeApi) +
      "/botqa-token/getUpdates?timeout=25&allowed_updates=%5B%22message%22%5D";
  const std::string send_message =
      std::string(kFakeApi) + "/botqa-token/sendMessage";

  std::vector<std::string> sent_texts;
  factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        if (request.url.spec() != send_message) {
          return;
        }
        auto body = base::JSONReader::ReadDict(network::GetUploadData(request),
                                               base::JSON_PARSE_RFC);
        ASSERT_TRUE(body);
        if (const std::string* text = body->FindString("text")) {
          sent_texts.push_back(*text);
        }
      }));

  MahoAgentChannelGateway gateway;
  gateway.SetPairingRequired(true);
  ChannelConfig config("tg", ChannelProvider::kTelegram, "Telegram");
  config.token = ChannelToken("qa-token");
  config.allowed_sender_ids = {"8"};
  gateway.RegisterChannel(config);
  gateway.SetProviderTransport(
      "tg", std::make_unique<TelegramChannelHttpTransport>(
                CreateTelegramUrlLoaderRequester(factory_.GetSafeWeakWrapper()),
                kFakeApi));
  int submitted = 0;
  gateway.SetTriggerCallback(base::BindLambdaForTesting(
      [&](const AgentSessionTrigger&) {
        ++submitted;
        return true;
      }));

  // 1. Unpaired message: nothing is submitted; the reply is a pairing code.
  factory_.AddResponse(
      get_updates,
      R"({"ok":true,"result":[{"update_id":41,"message":{"date":1700000000,)"
      R"("text":"open example.com","from":{"id":8},"chat":{"id":9}}}]})");
  factory_.AddResponse(send_message, R"({"ok":true})");
  gateway.PollChannelOnce("tg");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(submitted, 0);
  ASSERT_EQ(sent_texts.size(), 1u);
  const size_t pos = sent_texts[0].find("pairing code ");
  ASSERT_NE(pos, std::string::npos);
  const std::string code = sent_texts[0].substr(pos + 13, 6);
  EXPECT_FALSE(gateway.IsSenderPaired("tg", "8"));

  // 2. Approval in Maho pairs exactly that sender.
  EXPECT_EQ(gateway.ApprovePairing("tg", "8", code),
            MahoAgentChannelPairing::ConfirmResult::kApproved);
  EXPECT_TRUE(gateway.IsSenderPaired("tg", "8"));

  // 3. The paired sender's next message is accepted for the agent.
  factory_.ClearResponses();
  factory_.AddResponse(
      std::string(kFakeApi) +
          "/botqa-token/getUpdates?timeout=25&allowed_updates=%5B%22message%22%5D"
          "&offset=42",
      R"({"ok":true,"result":[{"update_id":42,"message":{"date":1700000001,)"
      R"("text":"open example.com","from":{"id":8},"chat":{"id":9}}}]})");
  gateway.PollChannelOnce("tg");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(submitted, 1);
}

}
}
