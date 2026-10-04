// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_sync_relay_client.h"
// allow: SIZE_OK - test-only observer/clock injection in the existing linked
// client; this regression lane cannot refactor the production transport.

#include <cstdlib>
#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "net/base/net_errors.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/sync/maho_sync_relay_protocol.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/base/isolation_info.h"
#include "net/storage_access_api/status.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/constants.h"
#include "services/network/public/cpp/originating_process_id.h"
#include "services/network/public/mojom/client_security_state.mojom.h"
#include "services/network/public/mojom/ip_address_space.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
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

MahoSyncRelayClient::MahoSyncRelayClient(Profile* profile,
                                       const base::TickClock* tick_clock)
    : profile_(profile),
      poll_timer_(tick_clock),
      snapshot_timer_(tick_clock),
      readable_watcher_(FROM_HERE,
                        mojo::SimpleWatcher::ArmingPolicy::MANUAL,
                        base::SequencedTaskRunner::GetCurrentDefault()),
      writable_watcher_(FROM_HERE,
                        mojo::SimpleWatcher::ArmingPolicy::MANUAL,
                        base::SequencedTaskRunner::GetCurrentDefault()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoSyncRelayClient::~MahoSyncRelayClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopPolling();
  Disconnect();
}

void MahoSyncRelayClient::StartPolling() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (polling_) {
    return;
  }
  polling_ = true;
  ++poll_generation_;
  next_poll_delay_ = base::Seconds(2);
  core_ready_subscription_ = AddCoreReadyCallback(base::BindRepeating(
      [](MahoSyncRelayClient* client) {
        ++client->poll_generation_;
        client->WakePolling();
      },
      base::Unretained(this)));
  SchedulePoll();
}

void MahoSyncRelayClient::StopPolling() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  poll_timer_.Stop();
  snapshot_timer_.Stop();
  core_ready_subscription_ = {};
  polling_ = false;
  poll_wake_pending_ = false;
  ++poll_generation_;
}

void MahoSyncRelayClient::SchedulePoll() {
  if (polling_) {
    poll_timer_.Start(FROM_HERE, next_poll_delay_,
                     base::BindOnce(&MahoSyncRelayClient::Poll,
                                    base::Unretained(this)));
  }
}

void MahoSyncRelayClient::WakePolling() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!polling_) {
    return;
  }
  next_poll_delay_ = base::Seconds(2);
  poll_timer_.Stop();
  if (poll_in_flight_) {
    poll_wake_pending_ = true;
    return;
  }
  Poll();
}

void MahoSyncRelayClient::DrainReceivedTabs() {
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
            
            ProfileBrowserCollection* collection =
                ProfileBrowserCollection::GetForProfile(profile_);
            BrowserWindowInterface* bwi =
                collection ? collection->GetLastActiveBrowser() : nullptr;
            Browser* browser = static_cast<Browser*>(bwi);
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
}

MahoSyncRelayClient::PollResult::PollResult() = default;
MahoSyncRelayClient::PollResult::PollResult(const PollResult&) = default;
MahoSyncRelayClient::PollResult::PollResult(PollResult&&) noexcept = default;
MahoSyncRelayClient::PollResult& MahoSyncRelayClient::PollResult::operator=(
    const PollResult&) = default;
MahoSyncRelayClient::PollResult& MahoSyncRelayClient::PollResult::operator=(
    PollResult&&) noexcept = default;
MahoSyncRelayClient::PollResult::~PollResult() = default;

void MahoSyncRelayClient::Poll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!polling_ || poll_in_flight_) {
    return;
  }
  poll_in_flight_ = true;
  if (poll_observer_for_testing_) {
    poll_observer_for_testing_.Run();
  }
  PostCoreTask<PollResult>(
      FROM_HERE,
      base::BindOnce(
          [](bool can_send, std::string connected_key) {
            PollResult result;
            ::MahoCore* core = GetCore();
            if (!core) {
              return result;
            }
            std::unique_ptr<char, decltype(&maho_core_free_string)> status(
                maho_core_get_sync_status(core), &maho_core_free_string);
            auto parsed = status
                              ? base::JSONReader::Read(status.get(),
                                                       base::JSON_PARSE_RFC)
                              : std::nullopt;
            const std::string* kind =
                parsed && parsed->is_dict()
                    ? parsed->GetDict().FindString("kind")
                    : nullptr;
            if (!kind) {
              return result;
            }
            result.valid = true;
            result.idle = *kind == "idle";
            if (result.idle) {
              return result;
            }
            std::unique_ptr<char, decltype(&maho_core_free_string)> server_url(
                maho_core_get_sync_server_url(core), &maho_core_free_string);
            std::unique_ptr<char, decltype(&maho_core_free_string)> key(
                maho_core_get_sync_key(core), &maho_core_free_string);
            if (server_url) {
              result.server_url = server_url.get();
            }
            if (key) {
              result.key = key.get();
            }
            if (can_send && result.key == connected_key) {
              std::unique_ptr<char, decltype(&maho_core_free_string)> outgoing(
                  maho_core_drain_outgoing_envelopes(core),
                  &maho_core_free_string);
              auto messages = outgoing
                                  ? base::JSONReader::Read(
                                        outgoing.get(), base::JSON_PARSE_RFC)
                                  : std::nullopt;
              if (messages && messages->is_list()) {
                for (const auto& message : messages->GetList()) {
                  std::string json;
                  if (base::JSONWriter::Write(message, &json)) {
                    result.outgoing.push_back(std::move(json));
                  }
                }
              }
            }
            return result;
          },
          state_ == State::kConnected && authenticated_ &&
              pending_writes_.empty(),
          active_key_),
      base::BindOnce(&MahoSyncRelayClient::OnPollComplete,
                     weak_ptr_factory_.GetWeakPtr(), poll_generation_));
}

void MahoSyncRelayClient::OnPollComplete(uint64_t generation,
                                       PollResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  poll_in_flight_ = false;
  if (!polling_) {
    return;
  }
  if (generation != poll_generation_) {
    poll_wake_pending_ = false;
    WakePolling();
    return;
  }
  if (result.valid && result.idle) {
    Disconnect();
    pending_writes_.clear();
    snapshot_timer_.Stop();
    next_poll_delay_ = std::min(next_poll_delay_ * 2, base::Seconds(30));
  } else if (result.valid) {
    next_poll_delay_ = base::Seconds(2);
    if (pending_write_key_ != result.key) {
      Disconnect();
      pending_writes_.clear();
      pending_write_key_ = result.key;
    }
    if (!snapshot_timer_.IsRunning()) {
      snapshot_timer_.Start(
          FROM_HERE, base::Minutes(15),
          base::BindRepeating(&MahoSyncRelayClient::PublishSnapshotIfNeeded,
                              base::Unretained(this)));
    }
    for (auto& message : result.outgoing) {
      pending_writes_.push_back(std::move(message));
    }
    if (state_ == State::kDisconnected) {
      Connect(result.server_url, result.key);
    } else if (state_ == State::kConnected && authenticated_) {
      FlushWriteQueue();
    }
  }
  if (poll_wake_pending_) {
    poll_wake_pending_ = false;
    WakePolling();
  } else {
    SchedulePoll();
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
      /*network_restrictions_id=*/network::GetNoOpNetworkRestrictionsId(),
      /*target_address_space=*/network::mojom::IPAddressSpace::kUnknown);
}

void MahoSyncRelayClient::Disconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == State::kDisconnected) return;

  state_ = State::kDisconnected;
  authenticated_ = false;
  active_key_.clear();

  readable_watcher_.Cancel();
  writable_watcher_.Cancel();
  readable_.reset();
  writable_.reset();
  websocket_.reset();
  handshake_receiver_.reset();
  client_receiver_.reset();

  pending_read_data_.clear();
  pending_read_data_index_ = 0;
  pending_read_finished_ = false;
  pending_auth_.clear();
  pending_write_offset_ = 0;
  write_in_flight_ = false;
  write_header_sent_ = false;
}

void MahoSyncRelayClient::SendAuthMessage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ::MahoCore* core = maho::GetCore();
  if (!core) return;

  char* token_c = maho_core_get_sync_auth_token(core);
  if (!token_c) return;
  std::string token(token_c);
  maho_core_free_string(token_c);
  const uint64_t cursor =
      maho_core_get_sync_receive_cursor(core, active_key_.c_str());

  base::DictValue auth_msg;
  auth_msg.Set("type", "auth");
  auth_msg.Set("key", active_key_);
  auth_msg.Set("token", token);
  auth_msg.Set("since_seq", static_cast<double>(cursor));

  std::string auth_json;
  if (base::JSONWriter::Write(base::Value(std::move(auth_msg)), &auth_json)) {
    pending_auth_ = std::move(auth_json);
    FlushWriteQueue();
  }
}

void MahoSyncRelayClient::WriteData(const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kConnected) return;
  pending_writes_.push_back(data);
  FlushWriteQueue();
}

void MahoSyncRelayClient::FlushWriteQueue() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (state_ == State::kConnected && !write_in_flight_) {
    const bool sending_auth = !pending_auth_.empty();
    if (!sending_auth && (!authenticated_ || pending_writes_.empty())) {
      return;
    }
    const std::string& data =
        sending_auth ? pending_auth_ : pending_writes_.front();
    if (!write_header_sent_) {
      websocket_->SendMessage(network::mojom::WebSocketMessageType::TEXT,
                              data.size());
      write_header_sent_ = true;
    }
    size_t written = 0;
    MojoResult result = writable_->WriteData(
        base::as_byte_span(data).subspan(pending_write_offset_),
        MOJO_WRITE_DATA_FLAG_NONE, written);
    if (result == MOJO_RESULT_OK) {
      pending_write_offset_ += written;
      if (pending_write_offset_ == data.size()) {
        if (sending_auth) {
          pending_auth_.clear();
        } else {
          pending_writes_.pop_front();
        }
        pending_write_offset_ = 0;
        write_header_sent_ = false;
      }
    } else if (result == MOJO_RESULT_SHOULD_WAIT) {
      write_in_flight_ = true;
      writable_watcher_.ArmOrNotify();
    } else {
      Disconnect();
      return;
    }
  }
}

void MahoSyncRelayClient::OnWritable(
    MojoResult result,
    const mojo::HandleSignalsState& state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (result != MOJO_RESULT_OK || state_ != State::kConnected) {
    Disconnect();
    return;
  }
  write_in_flight_ = false;
  FlushWriteQueue();
}

void MahoSyncRelayClient::HandleRelayFrame(const std::string& frame) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const MahoSyncRelayFrameKind frame_kind =
      ClassifyMahoSyncRelayFrame(frame);
  if (frame_kind == MahoSyncRelayFrameKind::kInvalid) {
    Disconnect();
    return;
  }

  if (frame_kind == MahoSyncRelayFrameKind::kEnvelope) {
    ::MahoCore* core = maho::GetCore();
    if (!core ||
        maho_core_handle_incoming_sync_envelope(core, frame.c_str()) != 0) {
      Disconnect();
      return;
    }
    DrainReceivedTabs();
    return;
  }

  if (frame_kind == MahoSyncRelayFrameKind::kAuthOk) {
    authenticated_ = true;
    FlushWriteQueue();
    WakePolling();
    PublishSnapshotIfNeeded();
    return;
  }

  if (frame_kind == MahoSyncRelayFrameKind::kCatchupDone) {
    return;
  }

  std::optional<base::Value> value =
      base::JSONReader::Read(frame, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    Disconnect();
    return;
  }
  const base::DictValue& dict = value->GetDict();
  const std::string* delivery_id = dict.FindString("delivery_id");
  const std::optional<int> seq = dict.FindInt("seq");
  DCHECK(delivery_id);
  DCHECK(seq);
  ::MahoCore* core = maho::GetCore();
  if (!core ||
      maho_core_ack_sync_delivery(core, delivery_id->c_str(),
                                  static_cast<uint64_t>(*seq)) != true) {
    Disconnect();
  }
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
  CHECK_EQ(writable_watcher_.Watch(
               writable_.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
               MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
               base::BindRepeating(&MahoSyncRelayClient::OnWritable,
                                   base::Unretained(this))),
           MOJO_RESULT_OK);

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
        HandleRelayFrame(message_str);
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

void MahoSyncRelayClient::PublishSnapshotIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kConnected || !authenticated_ || snapshot_in_flight_) {
    return;
  }

  ::MahoCore* core = maho::GetCore();
  if (!core) return;

  char* snapshot_json = maho_core_export_sync_snapshot(core);
  if (!snapshot_json) return;

  std::string snapshot(snapshot_json);
  maho_core_free_string(snapshot_json);

  if (snapshot == last_published_snapshot_) {
    return;
  }

  UploadSnapshot(std::move(snapshot));
}

void MahoSyncRelayClient::UploadSnapshot(std::string snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ::MahoCore* core = maho::GetCore();
  if (!core || !profile_) return;

  char* token_c = maho_core_get_sync_auth_token(core);
  if (!token_c) return;
  std::string token(token_c);
  maho_core_free_string(token_c);

  char* server_url_c = maho_core_get_sync_server_url(core);
  std::string server_url = server_url_c ? server_url_c : "";
  if (server_url_c) maho_core_free_string(server_url_c);
  if (server_url.empty()) return;

  if (server_url.rfind("wss://", 0) == 0) {
    server_url.replace(0, 6, "https://");
  } else if (server_url.rfind("ws://", 0) == 0) {
    server_url.replace(0, 5, "http://");
  }

  std::optional<base::Value> parsed = base::JSONReader::Read(snapshot, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return;

  const std::string* blob_str = parsed->GetDict().FindString("blob");
  if (!blob_str || blob_str->empty()) return;

  base::DictValue body;
  body.Set("room_id", active_key_);
  if (auto ceiling = parsed->GetDict().FindDouble("hlc_ts_ceiling")) {
    body.Set("hlc_ts_ceiling", *ceiling);
  } else if (auto ceiling_int = parsed->GetDict().FindInt("hlc_ts_ceiling")) {
    body.Set("hlc_ts_ceiling", static_cast<double>(*ceiling_int));
  } else {
    body.Set("hlc_ts_ceiling", static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch()));
  }
  body.Set("blob", *blob_str);

  std::string body_json;
  if (!base::JSONWriter::Write(base::Value(std::move(body)), &body_json)) {
    return;
  }

  std::string upload_url = server_url;
  if (!upload_url.empty() && upload_url.back() == '/') {
    upload_url += "sync/snapshot/upload";
  } else {
    upload_url += "/sync/snapshot/upload";
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(upload_url);
  request->method = "POST";
  request->headers.SetHeader("Authorization", "Bearer " + token);
  request->headers.SetHeader("Content-Type", "application/json");
  request->headers.SetHeader("Accept", "application/json");
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  snapshot_in_flight_ = true;
  std::string snapshot_copy = snapshot;

  auto loader = network::SimpleURLLoader::Create(
      std::move(request), kSyncTrafficAnnotation);
  loader->AttachStringForUpload(body_json, "application/json");
  loader->SetTimeoutDuration(base::Seconds(30));
  loader->SetAllowHttpErrorResults(true);

  auto* raw_loader = loader.get();
  content::StoragePartition* partition = profile_->GetDefaultStoragePartition();
  if (!partition) {
    snapshot_in_flight_ = false;
    return;
  }

  raw_loader->DownloadToString(
      partition->GetURLLoaderFactoryForBrowserProcess().get(),
      base::BindOnce(
          [](base::WeakPtr<MahoSyncRelayClient> client,
             std::string published,
             std::unique_ptr<network::SimpleURLLoader> loader,
             std::optional<std::string> response_body) {
            if (!client) return;
            client->snapshot_in_flight_ = false;
            const auto* info = loader->ResponseInfo();
            const int status =
                info && info->headers ? info->headers->response_code() : 0;
            if (loader->NetError() == net::OK && (status == 200 || status == 201)) {
              client->last_published_snapshot_ = std::move(published);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(snapshot_copy),
          std::move(loader)),
      1024 * 1024);
}

}  // namespace maho
