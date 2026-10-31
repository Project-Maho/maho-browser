// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_MAHO_AI_INGRESS_COORDINATOR_H_
#define MAHO_BROWSER_UI_MAHO_AI_INGRESS_COORDINATOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "maho/browser/ui/browser_user_data.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

class Browser;
class BrowserWindowInterface;
class GlobalBrowserCollection;

namespace maho {

class MahoAiIngressCoordinator
    : public BrowserUserData<MahoAiIngressCoordinator>,
      public BrowserCollectionObserver {
 public:
  enum class TransactionStage {
    kDelivered,
    kAccepted,
  };

  struct IngressTransaction {
    IngressTransaction(maho_ai::mojom::AskMahoDispatchPtr d,
                       TransactionStage s,
                       int c_id,
                       uint64_t d_id);
    ~IngressTransaction();
    IngressTransaction(IngressTransaction&&) noexcept;
    IngressTransaction& operator=(IngressTransaction&&) noexcept;
    IngressTransaction(const IngressTransaction&) = delete;
    IngressTransaction& operator=(const IngressTransaction&) = delete;

    maho_ai::mojom::AskMahoDispatchPtr dispatch;
    TransactionStage stage;
    std::string session_id;
    int consumer_id;
    uint64_t delivery_id;
  };

  enum class ConsumerType {
    kSidebar,
    kFloating,
  };

  struct SessionHandoff {
    SessionHandoff(maho_ai::mojom::SessionInfoPtr session,
                   std::vector<maho_ai::mojom::RuntimeEventPtr> replay_events,
                   std::string ingress_request_id,
                   uint64_t ingress_delivery_id);
    SessionHandoff();
    ~SessionHandoff();
    SessionHandoff(SessionHandoff&&);
    SessionHandoff& operator=(SessionHandoff&&);
    SessionHandoff(const SessionHandoff&) = delete;
    SessionHandoff& operator=(const SessionHandoff&) = delete;

    maho_ai::mojom::SessionInfoPtr session;
    std::vector<maho_ai::mojom::RuntimeEventPtr> replay_events;
    std::string ingress_request_id;
    uint64_t ingress_delivery_id = 0;
  };

  class ConsumerRegistration {
   public:
    ConsumerRegistration();
    ConsumerRegistration(base::WeakPtr<MahoAiIngressCoordinator> coordinator,
                         int id);
    ~ConsumerRegistration();

    ConsumerRegistration(ConsumerRegistration&& other) noexcept;
    ConsumerRegistration& operator=(ConsumerRegistration&& other) noexcept;

    ConsumerRegistration(const ConsumerRegistration&) = delete;
    ConsumerRegistration& operator=(const ConsumerRegistration&) = delete;

    void Reset();

   private:
    base::WeakPtr<MahoAiIngressCoordinator> coordinator_;
    int id_ = 0;
  };

  ~MahoAiIngressCoordinator() override;

  void Dispatch(maho_ai::mojom::AskMahoDispatchPtr dispatch);

  using AcceptanceCallback =
      base::OnceCallback<void(const std::string& session_id)>;
  using DeliveryCallback =
      base::RepeatingCallback<void(const maho_ai::mojom::AskMahoDispatch&,
                                   uint64_t delivery_id,
                                   AcceptanceCallback)>;
  ConsumerRegistration RegisterConsumer(ConsumerType type,
                                        DeliveryCallback callback);
  bool HasConsumer() const;

  void StoreSessionHandoff(SessionHandoff handoff);
  std::optional<SessionHandoff> TakeSessionHandoff();

  base::WeakPtr<MahoAiIngressCoordinator> GetWeakPtr();
  void NotifyTerminal(uint64_t delivery_id);

  // BrowserCollectionObserver:
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

 private:
  friend class BrowserUserData<MahoAiIngressCoordinator>;
  explicit MahoAiIngressCoordinator(Browser* browser);

  void UnregisterConsumer(int id);
  void TryDeliver();
  void OnRequestAccepted(uint64_t delivery_id, const std::string& session_id);

  raw_ptr<Browser> browser_;

  struct Consumer {
    Consumer(int consumer_id,
             ConsumerType consumer_type,
             DeliveryCallback consumer_callback);
    ~Consumer();
    Consumer(Consumer&&);
    Consumer& operator=(Consumer&&);
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;

    int id;
    ConsumerType type;
    DeliveryCallback callback;
  };

  std::vector<Consumer> consumers_;
  int next_consumer_id_ = 1;
  uint64_t next_delivery_id_ = 1;

  std::optional<IngressTransaction> active_transaction_;
  std::optional<SessionHandoff> session_handoff_;
  base::flat_set<std::string> seen_request_ids_;
  base::circular_deque<maho_ai::mojom::AskMahoDispatchPtr> pending_dispatches_;

  bool in_deliver_ = false;
  bool repump_requested_ = false;
  bool removal_posted_ = false;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  base::WeakPtrFactory<MahoAiIngressCoordinator> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_MAHO_AI_INGRESS_COORDINATOR_H_
