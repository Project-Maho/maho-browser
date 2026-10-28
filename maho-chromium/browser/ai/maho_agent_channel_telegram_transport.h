#ifndef MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_TELEGRAM_TRANSPORT_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_TELEGRAM_TRANSPORT_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ai/maho_agent_channel_gateway.h"
#include "maho/browser/ai/maho_agent_channel_protocols.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"
namespace maho::ai {

class TelegramChannelHttpTransport final : public ChannelTransport {
 public:
  using Completion = base::OnceCallback<void(int, std::optional<std::string>)>;
  using Requester = base::RepeatingCallback<void(
      const GURL&, std::string, Completion)>;

  explicit TelegramChannelHttpTransport(Requester requester);
  // Test seam: route requests to a local fake Bot API (e.g.
  // "http://127.0.0.1:8080") instead of api.telegram.org. Production code
  // uses the one-argument constructor; there is no runtime switch.
  TelegramChannelHttpTransport(Requester requester, std::string api_base);
  ~TelegramChannelHttpTransport() override;
  void FetchEvents(const ChannelConfig& config,
                   const std::optional<std::string>& cursor,
                   FetchCallback callback) override;
  base::TimeDelta NextBackoffDelay() { return backoff_.OnFailure(); }
  void ResetBackoff() { backoff_.OnSuccess(); }
  void SendMessage(const ChannelConfig& config,
                   std::string chat_id,
                   std::string text,
                   base::OnceCallback<void(bool)> callback);
  void SendText(const ChannelConfig& config,
                std::string recipient,
                std::string text,
                base::OnceCallback<void(bool)> callback) override;

 private:
  Requester requester_;
  std::string api_base_;
  ChannelBackoff backoff_;
  base::WeakPtrFactory<TelegramChannelHttpTransport> weak_factory_{this};
};

TelegramChannelHttpTransport::Requester CreateTelegramUrlLoaderRequester(
    scoped_refptr<network::SharedURLLoaderFactory> factory);
std::unique_ptr<ChannelTransport> CreateTelegramChannelTransport(
    scoped_refptr<network::SharedURLLoaderFactory> factory);

}

#endif
