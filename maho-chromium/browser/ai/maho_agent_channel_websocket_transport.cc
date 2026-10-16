// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_channel_websocket_transport.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/strings/string_view_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/ai/maho_agent_channel_gateway.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/storage_access_api/status.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/constants.h"
#include "services/network/public/cpp/originating_process_id.h"
#include "services/network/public/mojom/client_security_state.mojom.h"
#include "services/network/public/mojom/ip_address_space.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace maho::ai {
namespace {

std::string_view FindStringOrEmpty(const base::DictValue& dict,
                                   std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? std::string_view(*value) : std::string_view();
}

net::MutableNetworkTrafficAnnotationTag ChannelSocketTrafficAnnotation() {
  return net::MutableNetworkTrafficAnnotationTag(
      net::DefineNetworkTrafficAnnotation("maho_agent_channel_websocket", R"(
    semantics {
      sender: "Maho Agent Channel Gateway"
      description: "Receives messages from configured Slack or Discord channels."
      trigger: "The user enables a channel connection."
      data: "Channel credentials and channel messages."
      destination: OTHER
      destination_other: "Slack or Discord"
    }
    policy {
      cookies_allowed: NO
      setting: "Channel connections are configured by the user."
      policy_exception_justification: "The user explicitly configures the channel."
    })"));
}

std::optional<ChannelEvent> ParseDiscordEvent(std::string_view json,
                                              const ChannelConfig& config) {
  auto root = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!root || FindStringOrEmpty(*root, "t") != "MESSAGE_CREATE")
    return std::nullopt;
  const base::DictValue* event = root->FindDict("d");
  const base::DictValue* author = event ? event->FindDict("author") : nullptr;
  const std::string* user = author ? author->FindString("id") : nullptr;
  const std::string* channel = event ? event->FindString("channel_id") : nullptr;
  const std::string* text = event ? event->FindString("content") : nullptr;
  const std::string* id = event ? event->FindString("id") : nullptr;
  if (!event || !author || !user || !channel || !text || !id ||
      author->FindBool("bot").value_or(false)) {
    return std::nullopt;
  }
  ChannelEvent result;
  result.id = *id;
  result.cursor = *id;
  result.channel_id = config.id;
  result.reply_channel_id = *channel;
  result.provider = ChannelProvider::kDiscord;
  result.sender_id = *user;
  if (const std::string* name = author->FindString("username"))
    result.sender_name = *name;
  result.text = *text;
  result.timestamp = 1;
  return result;
}

class ProfileWebSocketConnection final
    : public ChannelWebSocketConnection,
      public network::mojom::WebSocketHandshakeClient,
      public network::mojom::WebSocketClient {
 public:
  ProfileWebSocketConnection(Profile* profile,
                             GURL url,
                             base::RepeatingCallback<void(std::string)> frame_cb,
                             base::RepeatingClosure closed_cb)
      : profile_(profile),
        url_(std::move(url)),
        frame_cb_(std::move(frame_cb)),
        closed_cb_(std::move(closed_cb)),
        readable_watcher_(FROM_HERE,
                          mojo::SimpleWatcher::ArmingPolicy::MANUAL),
        writable_watcher_(FROM_HERE,
                          mojo::SimpleWatcher::ArmingPolicy::MANUAL) {
    Connect();
  }

  ~ProfileWebSocketConnection() override { CloseInternal(false); }

  void SendText(std::string frame) override {
    if (closed_) return;
    writes_.push_back(std::move(frame));
    FlushWrites();
  }

  void Close() override {
    if (closed_) return;
    if (websocket_.is_bound()) websocket_->StartClosingHandshake(1000, "normal closure");
    CloseInternal(true);
  }

  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr request) override {
    (void)request;
  }

  void OnFailure(const std::string& message,
                 int net_error,
                 int response_code) override {
    (void)message;
    (void)net_error;
    (void)response_code;
    CloseInternal(true);
  }

  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> socket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr response,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override {
    if (closed_) return;
    websocket_.Bind(std::move(socket));
    readable_ = std::move(readable);
    writable_ = std::move(writable);
    client_receiver_.Bind(std::move(client_receiver));
    handshake_receiver_.reset();
    client_receiver_.set_disconnect_handler(base::BindOnce(
        &ProfileWebSocketConnection::OnPipeClosed, weak_factory_.GetWeakPtr()));
    if (readable_watcher_.Watch(
            readable_.get(), MOJO_HANDLE_SIGNAL_READABLE,
            MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
            base::BindRepeating(&ProfileWebSocketConnection::ReadAvailable,
                                weak_factory_.GetWeakPtr())) != MOJO_RESULT_OK ||
        writable_watcher_.Watch(
            writable_.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
            MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
            base::BindRepeating(&ProfileWebSocketConnection::OnWritable,
                                weak_factory_.GetWeakPtr())) != MOJO_RESULT_OK) {
      CloseInternal(true);
      return;
    }
    websocket_->StartReceiving();
    connected_ = true;
    FlushWrites();
    readable_watcher_.ArmOrNotify();
  }

  void OnDataFrame(bool finish,
                   network::mojom::WebSocketMessageType type,
                   uint64_t data_length) override {
    if (closed_) return;
    if (type == network::mojom::WebSocketMessageType::BINARY) {
      CloseInternal(true);
      return;
    }
    if (data_length == 0) return;
    if (data_length > 16 * 1024 * 1024 ||
        read_pending_.size() + data_length > 16 * 1024 * 1024) {
      CloseInternal(true);
      return;
    }
    read_finish_ = read_finish_ || finish;
    pending_read_size_ += static_cast<size_t>(data_length);
    client_receiver_.Pause();
    ReadAvailable(MOJO_RESULT_OK, mojo::HandleSignalsState());
  }

  void OnDropChannel(bool was_clean, uint16_t code,
                     const std::string& reason) override {
    (void)was_clean;
    (void)code;
    (void)reason;
    CloseInternal(true);
  }

  void OnClosingHandshake() override {}

 private:
  void Connect() {
    if (!profile_ || !url_.is_valid()) {
      CloseInternal(true);
      return;
    }
    content::StoragePartition* partition = profile_->GetDefaultStoragePartition();
    if (!partition) {
      CloseInternal(true);
      return;
    }
    network::mojom::NetworkContext* context = partition->GetNetworkContext();
    if (!context) {
      CloseInternal(true);
      return;
    }
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient> handshake =
        handshake_receiver_.BindNewPipeAndPassRemote();
    handshake_receiver_.set_disconnect_handler(base::BindOnce(
        &ProfileWebSocketConnection::OnPipeClosed, weak_factory_.GetWeakPtr()));
    context->CreateWebSocket(
        url_, {}, net::StorageAccessApiStatus::kNone,
        net::IsolationInfo::CreateForInternalRequest(url::Origin::Create(url_)),
        {}, network::OriginatingProcessId::browser(), url::Origin::Create(url_),
        network::mojom::ClientSecurityState::New(),
        network::mojom::kWebSocketOptionBlockAllCookies,
        ChannelSocketTrafficAnnotation(), std::move(handshake), mojo::NullRemote(),
        mojo::NullRemote(), mojo::NullRemote(), std::nullopt,
        network::GetNoOpNetworkRestrictionsId(),
        network::mojom::IPAddressSpace::kUnknown);
  }

  void ReadAvailable(MojoResult result,
                     const mojo::HandleSignalsState& state) {
    (void)result;
    (void)state;
    if (closed_ || !pending_read_size_) return;
    std::vector<uint8_t> chunk(pending_read_size_ - read_pending_.size());
    size_t read = 0;
    const MojoResult read_result = readable_->ReadData(
        MOJO_READ_DATA_FLAG_NONE, base::span<uint8_t>(chunk), read);
    if (read_result == MOJO_RESULT_SHOULD_WAIT) {
      readable_watcher_.ArmOrNotify();
      return;
    }
    if (read_result != MOJO_RESULT_OK || !read) {
      CloseInternal(true);
      return;
    }
    read_pending_.append(
        base::as_string_view(base::span(chunk).first(read)));
    if (read_pending_.size() < pending_read_size_) {
      readable_watcher_.ArmOrNotify();
      return;
    }
    client_receiver_.Resume();
    if (read_finish_) {
      std::string complete = std::move(read_pending_);
      read_pending_.clear();
      read_finish_ = false;
      pending_read_size_ = 0;
      pending_read_size_ = 0;
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(frame_cb_, std::move(complete)));
    }
  }

  void FlushWrites() {
    while (!closed_ && connected_ && !writing_ && !writes_.empty()) {
      const std::string& frame = writes_.front();
      if (!write_header_sent_) {
        websocket_->SendMessage(network::mojom::WebSocketMessageType::TEXT,
                                frame.size());
        write_header_sent_ = true;
      }
      size_t written = 0;
      const MojoResult result = writable_->WriteData(
          base::as_byte_span(frame).subspan(write_offset_),
          MOJO_WRITE_DATA_FLAG_NONE, written);
      if (result == MOJO_RESULT_OK) {
        write_offset_ += written;
        if (write_offset_ == frame.size()) {
          writes_.pop_front();
          write_offset_ = 0;
          write_header_sent_ = false;
        }
      } else if (result == MOJO_RESULT_SHOULD_WAIT) {
        writing_ = true;
        writable_watcher_.ArmOrNotify();
      } else {
        CloseInternal(true);
      }
    }
  }

  void OnWritable(MojoResult result,
                  const mojo::HandleSignalsState& state) {
    (void)state;
    if (result != MOJO_RESULT_OK) {
      CloseInternal(true);
      return;
    }
    writing_ = false;
    FlushWrites();
  }

  void OnPipeClosed() { CloseInternal(true); }

  void CloseInternal(bool notify) {
    if (closed_) return;
    closed_ = true;
    connected_ = false;
    readable_watcher_.Cancel();
    writable_watcher_.Cancel();
    readable_.reset();
    writable_.reset();
    websocket_.reset();
    client_receiver_.reset();
    handshake_receiver_.reset();
    writes_.clear();
    if (notify && closed_cb_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, closed_cb_);
    }
  }

  raw_ptr<Profile> profile_;
  GURL url_;
  base::RepeatingCallback<void(std::string)> frame_cb_;
  base::RepeatingClosure closed_cb_;
  bool connected_ = false;
  bool closed_ = false;
  bool writing_ = false;
  bool write_header_sent_ = false;
  size_t write_offset_ = 0;
  std::deque<std::string> writes_;
  size_t pending_read_size_ = 0;
  bool read_finish_ = false;
  std::string read_pending_;
  mojo::Receiver<network::mojom::WebSocketHandshakeClient> handshake_receiver_{this};
  mojo::Receiver<network::mojom::WebSocketClient> client_receiver_{this};
  mojo::Remote<network::mojom::WebSocket> websocket_;
  mojo::ScopedDataPipeConsumerHandle readable_;
  mojo::ScopedDataPipeProducerHandle writable_;
  mojo::SimpleWatcher readable_watcher_;
  mojo::SimpleWatcher writable_watcher_;
  base::WeakPtrFactory<ProfileWebSocketConnection> weak_factory_{this};
};

class SlackTransport final : public ChannelTransport {
 public:
  SlackTransport(Profile* profile, ChannelToken token,
                 ChannelWebSocketFactory socket_factory = {},
                 SlackConnectionsOpenCallback opener = {})
      : profile_(profile),
        token_(std::move(token)),
        socket_factory_(std::move(socket_factory)),
        opener_(std::move(opener)) {}

  void FetchEvents(const ChannelConfig& config,
                   const std::optional<std::string>& cursor,
                   FetchCallback callback) override {
    config_ = config;
    config_.token = token_;
    if ((!profile_ && !opener_) || !config_.token) {
      std::move(callback).Run({}, "Slack credentials or profile unavailable");
      return;
    }
    if (cursor) cursor_ = cursor;
    if (pending_callback_) {
      std::move(callback).Run({}, "Slack fetch already pending");
      return;
    }
    pending_callback_ = std::move(callback);
    if (!socket_) OpenSocket();
  }

  void SendText(const ChannelConfig& config,
                std::string recipient,
                std::string text,
                base::OnceCallback<void(bool)> callback) override {
    config_ = config;
    if (!profile_ || !config_.token) {
      std::move(callback).Run(false);
      return;
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL("https://slack.com/api/chat.postMessage");
    request->method = "POST";
    request->headers.SetHeader("Authorization", "Bearer " + token_.expose_secret());
    request->headers.SetHeader("Content-Type", "application/json; charset=utf-8");
    base::DictValue payload;
    payload.Set("channel", std::move(recipient));
    payload.Set("text", std::move(text));
    auto loader = network::SimpleURLLoader::Create(
        std::move(request),
        net::NetworkTrafficAnnotationTag(ChannelSocketTrafficAnnotation()));
    loader->AttachStringForUpload(base::WriteJson(payload).value_or("{}"),
                                  "application/json");
    auto factory = profile_->GetDefaultStoragePartition()
                       ->GetURLLoaderFactoryForBrowserProcess();
    loader->DownloadToString(factory.get(),
        base::BindOnce([](std::unique_ptr<network::SimpleURLLoader> loader,
                          base::OnceCallback<void(bool)> callback,
                          std::optional<std::string> body) {
          auto response = body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC) : std::nullopt;
          std::move(callback).Run(loader->NetError() == net::OK && response &&
                                  response->FindBool("ok").value_or(false));
        }, std::move(loader), std::move(callback)), 1024 * 1024);
  }

 private:
  void OpenSocket() {
    loader_.reset();
    if (socket_) return;
    if (!config_.token) return;
    if (opener_) {
      opener_.Run(token_.expose_secret(), base::BindOnce(
          &SlackTransport::OnTestUrl, weak_factory_.GetWeakPtr()));
      return;
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL("https://slack.com/api/apps.connections.open");
    request->method = "POST";
    request->headers.SetHeader("Authorization",
                               "Bearer " + config_.token->expose_secret());
    request->headers.SetHeader("Content-Type", "application/x-www-form-urlencoded");
    loader_ = network::SimpleURLLoader::Create(
        std::move(request),
        net::NetworkTrafficAnnotationTag(ChannelSocketTrafficAnnotation()));
    auto factory = profile_->GetDefaultStoragePartition()
                       ->GetURLLoaderFactoryForBrowserProcess();
    if (!factory) { FailOrRetry("Slack network loader unavailable"); return; }
    loader_->DownloadToString(factory.get(),
        base::BindOnce(&SlackTransport::OnConnectionsOpen,
                       weak_factory_.GetWeakPtr()), 1024 * 1024);
  }

  void OnTestUrl(std::optional<std::string> url) {
    if (!url) { FailOrRetry("Slack connections.open failed"); return; }
    OnSocketUrl(GURL(*url));
  }

  void OnConnectionsOpen(std::optional<std::string> body) {
    loader_.reset();
    if (!body) { FailOrRetry("Slack connections.open failed"); return; }
    auto json = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
    const std::string* url = json ? json->FindString("url") : nullptr;
    if (!url || !json->FindBool("ok").value_or(false)) {
      FailOrRetry("Slack connections.open returned an invalid response");
      return;
    }
    OnSocketUrl(GURL(*url));
  }

  void OnSocketUrl(const GURL& url) {
    auto frame = base::BindRepeating(&SlackTransport::OnFrame,
                                     weak_factory_.GetWeakPtr());
    auto closed = base::BindRepeating(&SlackTransport::OnClosed,
                                      weak_factory_.GetWeakPtr());
    if (socket_factory_) {
      socket_ = socket_factory_.Run(url, std::move(frame), std::move(closed));
    } else {
      socket_ = std::make_unique<ProfileWebSocketConnection>(
          profile_, url, std::move(frame), std::move(closed));
    }
  }

  void OnFrame(std::string frame) {
    auto root = base::JSONReader::ReadDict(frame, base::JSON_PARSE_RFC);
    if (root && FindStringOrEmpty(*root, "type") == "hello") {
      backoff_.OnSuccess();
      return;
    }
    if (!slack_.OnFrame(frame)) return;
    for (std::string& ack : slack_.TakeAcknowledgements())
      if (socket_) socket_->SendText(std::move(ack));
    for (const std::string& payload : slack_.TakeEvents()) {
      auto event_json = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
      if (!event_json) continue;
      const base::DictValue* event = event_json->FindDict("event");
      if (!event) event = &*event_json;
      if (FindStringOrEmpty(*event, "type") != "message") continue;
      const std::string* user = event->FindString("user");
      const std::string* channel = event->FindString("channel");
      const std::string* text = event->FindString("text");
      const std::string* ts = event->FindString("ts");
      if (!user || !channel || !text || !ts || event->FindString("bot_id") ||
          event->FindString("subtype")) continue;
      ChannelEvent parsed;
      parsed.id = *ts;
      parsed.cursor = *ts;
      parsed.channel_id = config_.id;
      parsed.reply_channel_id = *channel;
      parsed.provider = ChannelProvider::kSlack;
      parsed.sender_id = *user;
      parsed.text = *text;
      events_.push_back(std::move(parsed));
    }
    if (slack_.reconnect_requested()) {
      slack_.ResetReconnectRequest();
      if (socket_) socket_->Close();
      return;
    }
    CompleteIfReady();
  }

  void OnClosed() {
    socket_.reset();
    ScheduleReconnect();
  }

  void ScheduleReconnect() {
    if (reconnect_timer_.IsRunning()) return;
    reconnect_timer_.Start(FROM_HERE, backoff_.OnFailure(), this,
                           &SlackTransport::OpenSocket);
  }

  void FailOrRetry(std::string error) {
    if (pending_callback_) {
      std::move(pending_callback_).Run({}, std::move(error));
    } else {
      ScheduleReconnect();
    }
  }

  void CompleteIfReady() {
    if (!pending_callback_) return;
    std::vector<ChannelEvent> result;
    for (auto it = events_.begin(); it != events_.end();) {
      if (it->channel_id == config_.id) {
        result.push_back(std::move(*it));
        it = events_.erase(it);
      } else ++it;
    }
    std::move(pending_callback_).Run(std::move(result), std::nullopt);
  }

  raw_ptr<Profile> profile_;
  ChannelToken token_;
  ChannelWebSocketFactory socket_factory_;
  SlackConnectionsOpenCallback opener_;
  ChannelConfig config_;
  std::optional<std::string> cursor_;
  std::unique_ptr<ChannelWebSocketConnection> socket_;
  std::vector<ChannelEvent> events_;
  SlackSocketProtocol slack_;
  ChannelBackoff backoff_;
  base::OneShotTimer reconnect_timer_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  FetchCallback pending_callback_;
  base::WeakPtrFactory<SlackTransport> weak_factory_{this};
};

class DiscordTransport final : public ChannelTransport {
 public:
  DiscordTransport(Profile* profile, ChannelToken token,
                   ChannelWebSocketFactory socket_factory = {},
                   std::string gateway_url =
                       "wss://gateway.discord.gg/?v=10&encoding=json")
      : profile_(profile), token_(std::move(token)),
        socket_factory_(std::move(socket_factory)),
        gateway_url_(std::move(gateway_url)) {}

  void FetchEvents(const ChannelConfig& config,
                   const std::optional<std::string>& cursor,
                   FetchCallback callback) override {
    if (!profile_ && !socket_factory_) {
      std::move(callback).Run({}, "Discord profile unavailable");
      return;
    }
    if (pending_callback_) {
      std::move(callback).Run({}, "Discord fetch already pending");
      return;
    }
    config_ = config;
    config_.token = token_;
    if (cursor) cursor_ = cursor;
    protocol_.SetToken(token_.expose_secret());
    pending_callback_ = std::move(callback);
    if (!socket_ && !reconnect_timer_.IsRunning()) Connect();
  }

  void SendText(const ChannelConfig& config,
                std::string recipient,
                std::string text,
                base::OnceCallback<void(bool)> callback) override {
    config_ = config;
    if (!profile_ || !config_.token) {
      std::move(callback).Run(false);
      return;
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL("https://discord.com/api/v10/channels/" + recipient + "/messages");
    request->method = "POST";
    request->headers.SetHeader("Authorization", "Bot " + token_.expose_secret());
    request->headers.SetHeader("Content-Type", "application/json");
    base::DictValue payload;
    payload.Set("content", std::move(text));
    auto loader = network::SimpleURLLoader::Create(
        std::move(request),
        net::NetworkTrafficAnnotationTag(ChannelSocketTrafficAnnotation()));
    loader->AttachStringForUpload(base::WriteJson(payload).value_or("{}"),
                                  "application/json");
    auto factory = profile_->GetDefaultStoragePartition()
                       ->GetURLLoaderFactoryForBrowserProcess();
    loader->DownloadToString(factory.get(),
        base::BindOnce([](std::unique_ptr<network::SimpleURLLoader> loader,
                          base::OnceCallback<void(bool)> callback,
                          std::optional<std::string> body) {
          std::move(callback).Run(loader->NetError() == net::OK && body.has_value());
        }, std::move(loader), std::move(callback)), 1024 * 1024);
  }

 private:
  void Connect() {
    const GURL url = GURL(gateway_url_);
    auto frame = base::BindRepeating(&DiscordTransport::OnFrame,
                                     weak_factory_.GetWeakPtr());
    auto closed = base::BindRepeating(&DiscordTransport::OnClosed,
                                      weak_factory_.GetWeakPtr());
    if (socket_factory_)
      socket_ = socket_factory_.Run(url, std::move(frame), std::move(closed));
    else
      socket_ = std::make_unique<ProfileWebSocketConnection>(
          profile_, url, std::move(frame), std::move(closed));
    if (!socket_) ScheduleReconnect();
  }

  void OnFrame(std::string frame) {
    auto root = base::JSONReader::ReadDict(frame, base::JSON_PARSE_RFC);
    if (!root) return;
    const int op = root->FindInt("op").value_or(-1);
    if (op == 10) {
      const base::DictValue* data = root->FindDict("d");
      heartbeat_interval_ = data ? data->FindInt("heartbeat_interval").value_or(0) : 0;
      if (heartbeat_interval_ <= 0) { Reconnect(); return; }
      heartbeat_acknowledged_ = true;
      heartbeat_deadline_.Stop();
      heartbeat_timer_.Start(FROM_HERE, base::Milliseconds(heartbeat_interval_), this,
                             &DiscordTransport::SendHeartbeat);
      backoff_.OnSuccess();
    }
    const DiscordGatewayProtocol::Decision decision = protocol_.OnFrame(frame);
    if (const auto seq = root->FindInt("s")) sequence_ = *seq;
    const base::DictValue* payload = root->FindDict("d");
    if (op == 10 && payload) {
      for (const std::string& outbound : decision.outbound_frames)
        if (socket_) socket_->SendText(outbound);
      resume_requested_ = false;
    } else if (op == 7) {
      resume_requested_ = true;
      Reconnect();
      return;
    } else if (op == 9) {
      resume_requested_ = root->FindBool("d").value_or(false);
      resume_requested_ = decision.invalid_session && resume_requested_;
      if (!resume_requested_) { session_id_.clear(); sequence_.reset(); }
      Reconnect();
      return;
    }
    if (op == 11) {
      heartbeat_acknowledged_ = true;
      heartbeat_deadline_.Stop();
    }
    if (auto event = ParseDiscordEvent(frame, config_))
      events_.push_back(std::move(*event));
    if (FindStringOrEmpty(*root, "t") == "READY") {
      session_id_ = protocol_.session_id();
      if (const std::string* resume_url = payload ? payload->FindString("resume_gateway_url") : nullptr) {
        gateway_url_ = *resume_url;
        gateway_url_ += gateway_url_.find('?') == std::string::npos ?
                            "?v=10&encoding=json" : "&v=10&encoding=json";
      }
      backoff_.OnSuccess();
      CompleteIfReady();
    }
  }

  void SendHeartbeat() {
    if (!heartbeat_acknowledged_) { Reconnect(); return; }
    heartbeat_acknowledged_ = false;
    base::DictValue heartbeat;
    heartbeat.Set("op", 1);
    if (sequence_)
      heartbeat.Set("d", *sequence_);
    else heartbeat.Set("d", base::Value());
    if (socket_) socket_->SendText(base::WriteJson(heartbeat).value_or("{}"));
    heartbeat_deadline_.Start(FROM_HERE, base::Milliseconds(heartbeat_interval_), this,
                               &DiscordTransport::OnHeartbeatMissed);
  }

  void OnHeartbeatMissed() { Reconnect(); }

  void OnClosed() {
    socket_.reset();
    protocol_.OnDisconnected();
    heartbeat_timer_.Stop();
    heartbeat_deadline_.Stop();
    ScheduleReconnect();
  }

  void Reconnect() {
    heartbeat_timer_.Stop();
    heartbeat_deadline_.Stop();
    if (socket_) {
      auto old_socket = std::move(socket_);
      old_socket->Close();
    }
    if (!reconnect_timer_.IsRunning()) ScheduleReconnect();
  }

  void ScheduleReconnect() {
    if (reconnect_timer_.IsRunning()) return;
    reconnect_timer_.Start(FROM_HERE, backoff_.OnFailure(), this,
                           &DiscordTransport::Connect);
  }

  void CompleteIfReady() {
    if (!pending_callback_) return;
    std::vector<ChannelEvent> result;
    for (auto it = events_.begin(); it != events_.end();) {
      if (it->channel_id == config_.id) {
        result.push_back(std::move(*it));
        it = events_.erase(it);
      } else ++it;
    }
    std::move(pending_callback_).Run(std::move(result), std::nullopt);
  }

  raw_ptr<Profile> profile_;
  ChannelToken token_;
  ChannelWebSocketFactory socket_factory_;
  ChannelConfig config_;
  std::optional<std::string> cursor_;
  std::string gateway_url_;
  std::unique_ptr<ChannelWebSocketConnection> socket_;
  std::vector<ChannelEvent> events_;
  DiscordGatewayProtocol protocol_;
  std::string session_id_;
  std::optional<int> sequence_;
  bool resume_requested_ = false;
  int heartbeat_interval_ = 0;
  bool heartbeat_acknowledged_ = true;
  base::RepeatingTimer heartbeat_timer_;
  base::OneShotTimer heartbeat_deadline_;
  base::OneShotTimer reconnect_timer_;
  ChannelBackoff backoff_;
  FetchCallback pending_callback_;
  base::WeakPtrFactory<DiscordTransport> weak_factory_{this};
};

}  // namespace

std::unique_ptr<ChannelTransport> CreateSlackSocketModeTransport(
    Profile* profile,
    ChannelToken app_token) {
  return std::make_unique<SlackTransport>(profile, std::move(app_token));
}

std::unique_ptr<ChannelTransport> CreateDiscordGatewayTransport(
    Profile* profile,
    ChannelToken bot_token) {
  return std::make_unique<DiscordTransport>(profile, std::move(bot_token));
}

std::unique_ptr<ChannelTransport> CreateSlackSocketModeTransportForTesting(
    Profile* profile,
    ChannelToken app_token,
    ChannelWebSocketFactory socket_factory,
    SlackConnectionsOpenCallback opener) {
  return std::make_unique<SlackTransport>(profile, std::move(app_token),
                                          std::move(socket_factory),
                                          std::move(opener));
}

std::unique_ptr<ChannelTransport> CreateDiscordGatewayTransportForTesting(
    Profile* profile,
    ChannelToken bot_token,
    ChannelWebSocketFactory socket_factory,
    std::string gateway_url) {
  return std::make_unique<DiscordTransport>(profile, std::move(bot_token),
                                            std::move(socket_factory),
                                            std::move(gateway_url));
}

}  // namespace maho::ai
