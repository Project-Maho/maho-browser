// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_sync_relay_client.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/base/isolation_info.h"
#include "net/storage_access_api/status.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/originating_process_id.h"
#include "services/network/public/mojom/client_security_state.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"

namespace maho {

namespace {

constexpr net::NetworkTrafficAnnotationTag kSyncTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_sync_relay", R"(
        semantics {
          sender: "Maho Sync Relay Client"
          description: "Syncs spaces, tabs, bookmarks, and settings across devices."
          trigger: "User enables sync or browser changes sync status."
          data: "Encrypted sync payloads."
          destination: OTHER
          destination_other: "Maho Sync Relay Server"
        }
        policy {
          cookies_allowed: NO
          setting: "This feature can be disabled via settings."
          policy_exception_justification: "Not implemented."
        })");

std::string GetWebSocketUrl(const std::string& server_url, const std::string& key) {
  std::string ws_url = server_url;
  if (ws_url.rfind("https://", 0) == 0) {
    ws_url.replace(0, 8, "wss://");
  } else if (ws_url.rfind("http://", 0) == 0) {
    ws_url.replace(0, 7, "ws://");
  }
  if (ws_url.empty()) return "";
  if (ws_url.back() == '/') {
    ws_url += "ws?key=" + key;
  } else {
    ws_url += "/ws?key=" + key;
  }
  return ws_url;
}

}  // namespace

MahoSyncRelayClient::MahoSyncRelayClient(Profile* profile)
    : profile_(profile),
      readable_watcher_(FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::MANUAL, base::SequencedTaskRunner::GetCurrentDefault()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoSyncRelayClient::~MahoSyncRelayClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopPolling();
  Disconnect();
}

void MahoSyncRelayClient::StartPolling() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (poll_timer_.IsRunning()) return;
  poll_timer_.Start(FROM_HERE, base::Seconds(2),
                    base::BindRepeating(&MahoSyncRelayClient::Poll, base::Unretained(this)));
}

void MahoSyncRelayClient::StopPolling() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  poll_timer_.Stop();
}

void MahoSyncRelayClient::Poll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ::MahoCore* core = maho::GetCore();
  if (!core) return;

  char* received_tabs_json = maho_core_drain_received_tabs(core);
  if (received_tabs_json) {
    std::optional<base::Value> val = base::JSONReader::Read(received_tabs_json, base::JSON_PARSE_RFC);
    maho_core_free_string(received_tabs_json);
    if (val && val->is_list()) {
      for (const auto& tab_val : val->GetList()) {
        if (tab_val.is_dict()) {
          const std::string* title = tab_val.GetDict().FindString("title");
          const std::string* url = tab_val.GetDict().FindString("url");
          const std::string* sender_name = tab_val.GetDict().FindString("senderName");
          if (url) {
            std::u16string utitle =
                (sender_name && !sender_name->empty())
                    ? u"Tab received from " + base::UTF8ToUTF16(*sender_name)
                    : u"Tab received from your device";
            std::u16string ubody = title ? base::UTF8ToUTF16(*title) : base::UTF8ToUTF16(*url);
            
            BrowserWindowInterface* bwi = chrome::FindLastActiveWithProfile(profile_);
            Browser* browser = bwi ? bwi->GetBrowserForMigrationOnly() : nullptr;
            if (browser) {
              chrome::AddSelectedTabWithURL(browser, GURL(*url), ui::PAGE_TRANSITION_LINK);
              
              MahoNotificationOverlay* overlay =
                  MahoNotificationOverlay::GetOrCreateForBrowser(browser);
              if (overlay) {
                overlay->Show(utitle, ubody);
              }
            }
          }
        }
      }
    }
  }

  char* status_c = maho_core_get_sync_status(core);
  if (!status_c) return;
  std::string status(status_c);
  maho_core_free_string(status_c);

  if (status == "idle") {
    Disconnect();
    return;
  }

  if (state_ == State::kDisconnected) {
    char* server_url_c = maho_core_get_sync_server_url(core);
    char* key_c = maho_core_get_sync_key(core);
    if (server_url_c && key_c) {
      Connect(server_url_c, key_c);
    }
    if (server_url_c) maho_core_free_string(server_url_c);
    if (key_c) maho_core_free_string(key_c);
  } else if (state_ == State::kConnected && authenticated_) {
    SendOutgoingMessages();
  }
}

void MahoSyncRelayClient::Connect(const std::string& server_url, const std::string& key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kDisconnected) return;

  std::string ws_url_str = GetWebSocketUrl(server_url, key);
  GURL ws_url(ws_url_str);
  if (!ws_url.is_valid()) return;

  content::StoragePartition* partition = profile_->GetDefaultStoragePartition();
  if (!partition) return;
  network::mojom::NetworkContext* network_context = partition->GetNetworkContext();
  if (!network_context) return;

  state_ = State::kConnecting;
  active_key_ = key;
  authenticated_ = false;

  mojo::PendingRemote<network::mojom::WebSocketHandshakeClient> handshake_remote =
      handshake_receiver_.BindNewPipeAndPassRemote();
  handshake_receiver_.set_disconnect_handler(base::BindOnce(
      &MahoSyncRelayClient::OnMojoPipeDisconnect, base::Unretained(this)));

  std::vector<std::string> requested_protocols;
  std::vector<network::mojom::HttpHeaderPtr> additional_headers;

  network_context->CreateWebSocket(
      ws_url, requested_protocols, net::StorageAccessApiStatus::kNone,
      net::IsolationInfo::CreateForInternalRequest(url::Origin::Create(ws_url)),
      std::move(additional_headers), network::OriginatingProcessId::browser(),
      url::Origin::Create(ws_url), network::mojom::ClientSecurityState::New(),
      network::mojom::kWebSocketOptionBlockAllCookies,
      net::MutableNetworkTrafficAnnotationTag(kSyncTrafficAnnotation),
      std::move(handshake_remote),
      /*url_loader_network_observer=*/mojo::NullRemote(),
      /*auth_handler=*/mojo::NullRemote(),
      /*header_client=*/mojo::NullRemote(),
      /*throttling_profile_id=*/std::nullopt,
      /*network_restrictions_id=*/std::nullopt);
}

void MahoSyncRelayClient::Disconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == State::kDisconnected) return;

  state_ = State::kDisconnected;
  authenticated_ = false;
  active_key_.clear();

  readable_watcher_.Cancel();
  readable_.reset();
  writable_.reset();
  websocket_.reset();
  handshake_receiver_.reset();
  client_receiver_.reset();

  pending_read_data_.clear();
  pending_read_data_index_ = 0;
  pending_read_finished_ = false;
}

void MahoSyncRelayClient::SendAuthMessage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ::MahoCore* core = maho::GetCore();
  if (!core) return;

  char* token_c = maho_core_get_sync_auth_token(core);
  if (!token_c) return;
  std::string token(token_c);
  maho_core_free_string(token_c);

  base::DictValue auth_msg;
  auth_msg.Set("type", "auth");
  auth_msg.Set("key", active_key_);
  auth_msg.Set("token", token);

  std::string auth_json;
  if (base::JSONWriter::Write(base::Value(std::move(auth_msg)), &auth_json)) {
    WriteData(auth_json);
  }
}

void MahoSyncRelayClient::SendOutgoingMessages() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ::MahoCore* core = maho::GetCore();
  if (!core) return;

  char* outgoing_json = maho_core_drain_outgoing(core);
  if (!outgoing_json) return;

  std::optional<base::Value> val = base::JSONReader::Read(outgoing_json, base::JSON_PARSE_RFC);
  maho_core_free_string(outgoing_json);

  if (val && val->is_list()) {
    for (const auto& msg : val->GetList()) {
      std::string msg_json;
      if (base::JSONWriter::Write(msg, &msg_json)) {
        WriteData(msg_json);
      }
    }
  }
}

void MahoSyncRelayClient::WriteData(const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kConnected) return;

  websocket_->SendMessage(network::mojom::WebSocketMessageType::TEXT, data.size());
  size_t actually_written = 0;
  writable_->WriteData(base::as_byte_span(data), MOJO_WRITE_DATA_FLAG_NONE, actually_written);
}

void MahoSyncRelayClient::OnOpeningHandshakeStarted(
    network::mojom::WebSocketHandshakeRequestPtr request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoSyncRelayClient::OnFailure(const std::string& message,
                                    int net_error,
                                    int response_code) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Disconnect();
}

void MahoSyncRelayClient::OnConnectionEstablished(
    mojo::PendingRemote<network::mojom::WebSocket> socket,
    mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
    network::mojom::WebSocketHandshakeResponsePtr response,
    mojo::ScopedDataPipeConsumerHandle readable,
    mojo::ScopedDataPipeProducerHandle writable) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kConnecting) {
    Disconnect();
    return;
  }

  websocket_.Bind(std::move(socket));
  readable_ = std::move(readable);
  writable_ = std::move(writable);
  client_receiver_.Bind(std::move(client_receiver));

  handshake_receiver_.reset();

  client_receiver_.set_disconnect_handler(base::BindOnce(
      &MahoSyncRelayClient::OnMojoPipeDisconnect, base::Unretained(this)));

  CHECK_EQ(readable_watcher_.Watch(
               readable_.get(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
               base::BindRepeating(&MahoSyncRelayClient::ReadFromDataPipe,
                                   base::Unretained(this))),
           MOJO_RESULT_OK);
  readable_watcher_.ArmOrNotify();

  websocket_->StartReceiving();
  state_ = State::kConnected;

  SendAuthMessage();
}

void MahoSyncRelayClient::OnDataFrame(bool finish,
                                      network::mojom::WebSocketMessageType type,
                                      uint64_t data_len) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kConnected) return;

  if (data_len == 0) {
    return;
  }

  size_t old_size = pending_read_data_index_;
  size_t new_size = old_size + data_len;
  pending_read_data_.resize(new_size);
  pending_read_finished_ = finish;
  client_receiver_.Pause();
  ReadFromDataPipe(MOJO_RESULT_OK, mojo::HandleSignalsState());
}

void MahoSyncRelayClient::OnDropChannel(bool was_clean,
                                       uint16_t code,
                                       const std::string& reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Disconnect();
}

void MahoSyncRelayClient::OnClosingHandshake() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoSyncRelayClient::ReadFromDataPipe(MojoResult result,
                                           const mojo::HandleSignalsState& state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kConnected) return;

  size_t actually_read_bytes = 0;
  base::span<uint8_t> dest_span = base::span<uint8_t>(pending_read_data_).subspan(pending_read_data_index_);
  MojoResult mojo_res = readable_->ReadData(MOJO_READ_DATA_FLAG_NONE, dest_span, actually_read_bytes);
  if (mojo_res == MOJO_RESULT_OK) {
    pending_read_data_index_ += actually_read_bytes;
    if (pending_read_data_index_ < pending_read_data_.size()) {
      readable_watcher_.ArmOrNotify();
    } else {
      client_receiver_.Resume();
      if (pending_read_finished_) {
        std::string message_str(pending_read_data_.begin(), pending_read_data_.end());
        pending_read_data_.clear();
        pending_read_data_index_ = 0;
        pending_read_finished_ = false;

        if (!authenticated_) {
          std::optional<base::Value> val = base::JSONReader::Read(message_str, base::JSON_PARSE_RFC);
          if (val && val->is_dict()) {
            const std::string* type_val = val->GetDict().FindString("type");
            if (type_val && *type_val == "authOk") {
              authenticated_ = true;
              SendOutgoingMessages();
            } else {
              Disconnect();
            }
          } else {
            Disconnect();
          }
        } else {
          ::MahoCore* core = maho::GetCore();
          if (core) {
            maho_core_handle_incoming_websocket_message(core, message_str.c_str());
          }
        }
      }
    }
  } else if (mojo_res == MOJO_RESULT_SHOULD_WAIT) {
    readable_watcher_.ArmOrNotify();
  } else {
    Disconnect();
  }
}

void MahoSyncRelayClient::OnMojoPipeDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Disconnect();
}

}  // namespace maho
