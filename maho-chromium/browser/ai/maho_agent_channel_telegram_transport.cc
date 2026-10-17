#include "maho/browser/ai/maho_agent_channel_telegram_transport.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ai/maho_agent_channel_protocols.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "url/gurl.h"

namespace maho::ai {
namespace {

net::NetworkTrafficAnnotationTag TrafficAnnotation() {
  return net::DefineNetworkTrafficAnnotation("maho_telegram_channel", R"(
    semantics {
      sender: "Maho Telegram Channel"
      description: "Fetches updates and sends pairing replies for a configured Telegram bot."
      trigger: "The user enables a Telegram channel or an unpaired sender messages it."
      data: "Telegram bot token, update offset, chat identifier, and message text."
      destination: OTHER
    }
    policy {
      cookies_allowed: NO
      setting: "Telegram connections are configured by the user."
      policy_exception_justification: "Telegram bot API access is user configured."
    })");
}

constexpr char kTelegramApiBase[] = "https://api.telegram.org";

std::string TelegramUrl(std::string_view api_base,
                        const ChannelConfig& config,
                        std::string_view method) {
  return std::string(api_base) + "/bot" + config.token->expose_secret() + "/" +
         std::string(method);
}
}

TelegramChannelHttpTransport::TelegramChannelHttpTransport(Requester requester)
    : TelegramChannelHttpTransport(std::move(requester), kTelegramApiBase) {}

TelegramChannelHttpTransport::TelegramChannelHttpTransport(
    Requester requester,
    std::string api_base)
    : requester_(std::move(requester)), api_base_(std::move(api_base)) {}
TelegramChannelHttpTransport::~TelegramChannelHttpTransport() = default;

void TelegramChannelHttpTransport::FetchEvents(
    const ChannelConfig& config,
    const std::optional<std::string>& cursor,
    FetchCallback callback) {
  if (!config.token || !requester_) {
    std::move(callback).Run({}, "Telegram credentials or network unavailable");
    return;
  }
  std::string url = TelegramUrl(api_base_, config, "getUpdates?timeout=25&allowed_updates=%5B%22message%22%5D");
  if (cursor && !cursor->empty()) url += "&offset=" + *cursor;
  requester_.Run(GURL(url), {}, base::BindOnce(
      [](base::WeakPtr<TelegramChannelHttpTransport> self, ChannelConfig config,
         FetchCallback callback, int status, std::optional<std::string> body) {
        if (!self) return;
        if (status != 200 || !body) {
          self->backoff_.OnFailure();
          std::move(callback).Run({}, "Telegram update request failed");
          return;
        }
        std::vector<ChannelEvent> events;
        std::string offset;
        std::string error;
        if (!ParseTelegramUpdates(*body, config, &events, &offset, &error)) {
          self->backoff_.OnFailure();
          std::move(callback).Run({}, "Telegram update response was invalid");
          return;
        }
        self->backoff_.OnSuccess();
        std::move(callback).Run(std::move(events), std::nullopt);
      }, weak_factory_.GetWeakPtr(), [&config] {
        ChannelConfig metadata = config;
        metadata.token.reset();
        metadata.outbound_token.reset();
        return metadata;
      }(), std::move(callback)));
}

void TelegramChannelHttpTransport::SendMessage(
    const ChannelConfig& config,
    std::string chat_id,
    std::string text,
    base::OnceCallback<void(bool)> callback) {
  if (!config.token || !requester_) {
    std::move(callback).Run(false);
    return;
  }
  base::DictValue payload;
  payload.Set("chat_id", std::move(chat_id));
  payload.Set("text", std::move(text));
  std::string body;
  if (!base::JSONWriter::Write(payload, &body)) {
    std::move(callback).Run(false);
    return;
  }
  requester_.Run(GURL(TelegramUrl(api_base_, config, "sendMessage")), std::move(body),
      base::BindOnce([](base::OnceCallback<void(bool)> callback, int status,
                        std::optional<std::string> response) {
        std::move(callback).Run(status == 200 && response.has_value());
      }, std::move(callback)));
}

void TelegramChannelHttpTransport::SendText(
    const ChannelConfig& config,
    std::string recipient,
    std::string text,
    base::OnceCallback<void(bool)> callback) {
  SendMessage(config, std::move(recipient), std::move(text), std::move(callback));
}

TelegramChannelHttpTransport::Requester CreateTelegramUrlLoaderRequester(
    scoped_refptr<network::SharedURLLoaderFactory> factory) {
  return base::BindRepeating(
      [](scoped_refptr<network::SharedURLLoaderFactory> factory,
         const GURL& url, std::string body, TelegramChannelHttpTransport::Completion callback) {
        auto request = std::make_unique<network::ResourceRequest>();
        request->url = url;
        request->credentials_mode = network::mojom::CredentialsMode::kOmit;
        if (!body.empty()) request->method = "POST";
        auto loader = network::SimpleURLLoader::Create(
            std::move(request), TrafficAnnotation());
        if (!body.empty()) loader->AttachStringForUpload(std::move(body), "application/json");
        loader->SetTimeoutDuration(base::Seconds(35));
        auto* raw = loader.get();
        raw->DownloadToString(factory.get(), base::BindOnce(
            [](std::unique_ptr<network::SimpleURLLoader> loader,
               TelegramChannelHttpTransport::Completion callback,
               std::optional<std::string> response) {
              int status = loader->ResponseInfo() && loader->ResponseInfo()->headers
                               ? loader->ResponseInfo()->headers->response_code() : 0;
              if (loader->NetError() != net::OK) status = 0;
              std::move(callback).Run(status, std::move(response));
            }, std::move(loader), std::move(callback)), 1024 * 1024);
      }, std::move(factory));
}

std::unique_ptr<ChannelTransport> CreateTelegramChannelTransport(
    scoped_refptr<network::SharedURLLoaderFactory> factory) {
  return std::make_unique<TelegramChannelHttpTransport>(
      CreateTelegramUrlLoaderRequester(std::move(factory)));
}

}
