// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_WEBSOCKET_TRANSPORT_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_WEBSOCKET_TRANSPORT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "maho/browser/ai/maho_agent_channel_gateway.h"
#include "url/gurl.h"

class Profile;

namespace maho::ai {

using ChannelWebSocketFrameCallback = base::RepeatingCallback<void(std::string)>;
using ChannelWebSocketClosedCallback = base::RepeatingClosure;
using ChannelWebSocketFactory = base::RepeatingCallback<
    std::unique_ptr<ChannelWebSocketConnection>(
        const GURL&, ChannelWebSocketFrameCallback,
        ChannelWebSocketClosedCallback)>;
using SlackConnectionsOpenCallback = base::RepeatingCallback<
    void(const std::string&, base::OnceCallback<void(std::optional<std::string>)>)>;

std::unique_ptr<ChannelTransport> CreateSlackSocketModeTransport(
    Profile* profile,
    ChannelToken app_token);

std::unique_ptr<ChannelTransport> CreateDiscordGatewayTransport(
    Profile* profile,
    ChannelToken bot_token);

std::unique_ptr<ChannelTransport> CreateSlackSocketModeTransportForTesting(
    Profile* profile,
    ChannelToken app_token,
    ChannelWebSocketFactory websocket_factory,
    SlackConnectionsOpenCallback connections_open);
std::unique_ptr<ChannelTransport> CreateDiscordGatewayTransportForTesting(
    Profile* profile,
    ChannelToken bot_token,
    ChannelWebSocketFactory websocket_factory,
    std::string gateway_url);

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_WEBSOCKET_TRANSPORT_H_
