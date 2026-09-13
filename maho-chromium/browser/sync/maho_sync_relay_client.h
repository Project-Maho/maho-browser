// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_CLIENT_H_
#define MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_CLIENT_H_

#include <memory>
#include <deque>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/callback_list.h"
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
  explicit MahoSyncRelayClient(Profile* profile,
                              const base::TickClock* tick_clock = nullptr);
  ~MahoSyncRelayClient() override;

  MahoSyncRelayClient(const MahoSyncRelayClient&) = delete;
  MahoSyncRelayClient& operator=(const MahoSyncRelayClient&) = delete;

  void StartPolling();
  void StopPolling();
  void WakePolling();
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
  friend class MemoryThreadSyncPeer;
  // Inert unless a test subscribes; observes actual reconciliation entry.
  base::RepeatingClosure poll_observer_for_testing_;

  enum class State {
    kDisconnected,
    kConnecting,
    kConnected,
  };

  struct PollResult {
    PollResult();
    PollResult(const PollResult&);
    PollResult(PollResult&&) noexcept;
    PollResult& operator=(const PollResult&);
    PollResult& operator=(PollResult&&) noexcept;
    ~PollResult();

    bool valid = false;
    bool idle = true;
    std::string server_url;
    std::string key;
    std::vector<std::string> outgoing;
  };

  void Poll();
  void OnPollComplete(uint64_t generation, PollResult result);
  void SchedulePoll();
  void Connect(const std::string& server_url, const std::string& key);
  void Disconnect();

  void SendAuthMessage();
  void WriteData(const std::string& data);
  void FlushWriteQueue();
  void OnWritable(MojoResult result, const mojo::HandleSignalsState& state);
  void HandleRelayFrame(const std::string& frame);

  void ReadFromDataPipe(MojoResult result,
                        const mojo::HandleSignalsState& state);
  void OnMojoPipeDisconnect();

  void PublishSnapshotIfNeeded();
  void UploadSnapshot(std::string snapshot);

  const raw_ptr<Profile> profile_;
  State state_ = State::kDisconnected;
  std::string active_key_;

  base::OneShotTimer poll_timer_;
  base::RepeatingTimer snapshot_timer_;
  base::CallbackListSubscription core_ready_subscription_;
  bool polling_ = false;
  bool poll_in_flight_ = false;
  bool poll_wake_pending_ = false;
  uint64_t poll_generation_ = 0;
  base::TimeDelta next_poll_delay_ = base::Seconds(2);
  bool authenticated_ = false;

  std::string last_published_snapshot_;
  bool snapshot_in_flight_ = false;

  std::vector<uint8_t> pending_read_data_;
  size_t pending_read_data_index_ = 0;
  bool pending_read_finished_ = false;

  mojo::Receiver<network::mojom::WebSocketHandshakeClient> handshake_receiver_{this};
  mojo::Receiver<network::mojom::WebSocketClient> client_receiver_{this};
  mojo::Remote<network::mojom::WebSocket> websocket_;
  mojo::ScopedDataPipeConsumerHandle readable_;
  mojo::SimpleWatcher readable_watcher_;
  mojo::ScopedDataPipeProducerHandle writable_;
  mojo::SimpleWatcher writable_watcher_;
  std::deque<std::string> pending_writes_;
  std::string pending_auth_;
  std::string pending_write_key_;
  size_t pending_write_offset_ = 0;
  bool write_in_flight_ = false;
  bool write_header_sent_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSyncRelayClient> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_CLIENT_H_
