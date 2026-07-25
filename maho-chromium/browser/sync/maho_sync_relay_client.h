// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_CLIENT_H_
#define MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "services/network/public/mojom/websocket.mojom.h"

class Profile;

namespace maho {

class MahoSyncRelayClient : public network::mojom::WebSocketHandshakeClient,
                             public network::mojom::WebSocketClient {
 public:
  explicit MahoSyncRelayClient(Profile* profile);
  ~MahoSyncRelayClient() override;

  MahoSyncRelayClient(const MahoSyncRelayClient&) = delete;
  MahoSyncRelayClient& operator=(const MahoSyncRelayClient&) = delete;

  void StartPolling();
  void StopPolling();
  // Pulls tabs core has queued from inbound relay messages and surfaces them.
  // Driven by the socket read path; the poll timer no longer calls it.
  void DrainReceivedTabs();

  // WebSocketHandshakeClient:
  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr request) override;
  void OnFailure(const std::string& message,
                 int net_error,
                 int response_code) override;
  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> socket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr response,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override;

  // WebSocketClient:
  void OnDataFrame(bool finish,
                   network::mojom::WebSocketMessageType type,
                   uint64_t data_len) override;
  void OnDropChannel(bool was_clean,
                     uint16_t code,
                     const std::string& reason) override;
  void OnClosingHandshake() override;

 private:
  enum class State {
    kDisconnected,
    kConnecting,
    kConnected,
  };

  void Poll();
  void Connect(const std::string& server_url, const std::string& key);
  void Disconnect();

  void SendAuthMessage();
  void SendOutgoingMessages();
  void WriteData(const std::string& data);

  void ReadFromDataPipe(MojoResult result,
                        const mojo::HandleSignalsState& state);
  void OnMojoPipeDisconnect();

  const raw_ptr<Profile> profile_;
  State state_ = State::kDisconnected;
  std::string active_key_;

  base::RepeatingTimer poll_timer_;
  bool authenticated_ = false;

  std::vector<uint8_t> pending_read_data_;
  size_t pending_read_data_index_ = 0;
  bool pending_read_finished_ = false;

  mojo::Receiver<network::mojom::WebSocketHandshakeClient> handshake_receiver_{this};
  mojo::Receiver<network::mojom::WebSocketClient> client_receiver_{this};
  mojo::Remote<network::mojom::WebSocket> websocket_;
  mojo::ScopedDataPipeConsumerHandle readable_;
  mojo::SimpleWatcher readable_watcher_;
  mojo::ScopedDataPipeProducerHandle writable_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSyncRelayClient> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_CLIENT_H_
