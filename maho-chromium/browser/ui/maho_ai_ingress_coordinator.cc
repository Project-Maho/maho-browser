// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_ai_ingress_coordinator.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

namespace maho {

// --- IngressTransaction ---

MahoAiIngressCoordinator::IngressTransaction::IngressTransaction(
    maho_ai::mojom::AskMahoDispatchPtr d,
    TransactionStage s,
    int c_id,
    uint64_t d_id)
    : dispatch(std::move(d)), stage(s), consumer_id(c_id), delivery_id(d_id) {}

MahoAiIngressCoordinator::IngressTransaction::~IngressTransaction() = default;

MahoAiIngressCoordinator::IngressTransaction::IngressTransaction(
    IngressTransaction&&) noexcept = default;

MahoAiIngressCoordinator::IngressTransaction&
MahoAiIngressCoordinator::IngressTransaction::operator=(
    IngressTransaction&&) noexcept = default;

MahoAiIngressCoordinator::SessionHandoff::SessionHandoff(
    maho_ai::mojom::SessionInfoPtr session,
    std::vector<maho_ai::mojom::RuntimeEventPtr> replay_events,
    std::string ingress_request_id,
    uint64_t ingress_delivery_id)
    : session(std::move(session)),
      replay_events(std::move(replay_events)),
      ingress_request_id(std::move(ingress_request_id)),
      ingress_delivery_id(ingress_delivery_id) {}

MahoAiIngressCoordinator::SessionHandoff::SessionHandoff() = default;

MahoAiIngressCoordinator::SessionHandoff::~SessionHandoff() = default;

MahoAiIngressCoordinator::SessionHandoff::SessionHandoff(SessionHandoff&&) =
    default;

MahoAiIngressCoordinator::SessionHandoff&
MahoAiIngressCoordinator::SessionHandoff::operator=(SessionHandoff&&) =
    default;

// --- ConsumerRegistration ---

MahoAiIngressCoordinator::ConsumerRegistration::ConsumerRegistration() =
    default;

MahoAiIngressCoordinator::ConsumerRegistration::ConsumerRegistration(
    base::WeakPtr<MahoAiIngressCoordinator> coordinator,
    int id)
    : coordinator_(coordinator), id_(id) {}

MahoAiIngressCoordinator::ConsumerRegistration::~ConsumerRegistration() {
  Reset();
}

MahoAiIngressCoordinator::ConsumerRegistration::ConsumerRegistration(
    ConsumerRegistration&& other) noexcept
    : coordinator_(std::move(other.coordinator_)), id_(other.id_) {
  other.id_ = 0;
}

MahoAiIngressCoordinator::ConsumerRegistration&
MahoAiIngressCoordinator::ConsumerRegistration::operator=(
    ConsumerRegistration&& other) noexcept {
  if (this != &other) {
    Reset();
    coordinator_ = std::move(other.coordinator_);
    id_ = other.id_;
    other.id_ = 0;
  }
  return *this;
}

void MahoAiIngressCoordinator::ConsumerRegistration::Reset() {
  if (id_ != 0 && coordinator_) {
    coordinator_->UnregisterConsumer(id_);
  }
  id_ = 0;
  coordinator_.reset();
}

// --- MahoAiIngressCoordinator ---

MahoAiIngressCoordinator::MahoAiIngressCoordinator(Browser* browser)
    : BrowserUserData<MahoAiIngressCoordinator>(browser), browser_(browser) {
  if (browser_) {
    if (auto* collection = GlobalBrowserCollection::GetInstance()) {
      browser_collection_observation_.Observe(collection);
    }
  }
}

MahoAiIngressCoordinator::Consumer::Consumer(
    int consumer_id,
    ConsumerType consumer_type,
    DeliveryCallback consumer_callback)
    : id(consumer_id),
      type(consumer_type),
      callback(std::move(consumer_callback)) {}

MahoAiIngressCoordinator::Consumer::~Consumer() = default;

MahoAiIngressCoordinator::Consumer::Consumer(Consumer&&) = default;

MahoAiIngressCoordinator::Consumer&
MahoAiIngressCoordinator::Consumer::operator=(Consumer&&) = default;

MahoAiIngressCoordinator::~MahoAiIngressCoordinator() {
  base::UmaHistogramExactLinear("Maho.AI.Ingress.AbandonedPendingCount",
                                pending_dispatches_.size(), 100);
  base::UmaHistogramBoolean("Maho.AI.Ingress.AbandonedActive",
                            active_transaction_.has_value());
}

void MahoAiIngressCoordinator::Dispatch(
    maho_ai::mojom::AskMahoDispatchPtr dispatch) {
  if (!dispatch || dispatch->request_id.empty()) {
    return;
  }

  base::UmaHistogramBoolean("Maho.AI.Ingress.Dispatched", true);

  if (seen_request_ids_.contains(dispatch->request_id)) {
    base::UmaHistogramBoolean("Maho.AI.Ingress.Deduplicated", true);
    LOG(INFO) << "Ask Maho duplicate request ID ignored: "
              << dispatch->request_id;
    return;
  }

  seen_request_ids_.insert(dispatch->request_id);

  pending_dispatches_.push_back(std::move(dispatch));
  base::UmaHistogramExactLinear("Maho.AI.Ingress.QueuedDepth",
                                pending_dispatches_.size(), 100);

  TryDeliver();
}

MahoAiIngressCoordinator::ConsumerRegistration
MahoAiIngressCoordinator::RegisterConsumer(ConsumerType type,
                                           DeliveryCallback callback) {
  int id = next_consumer_id_++;
  consumers_.emplace_back(id, type, std::move(callback));

  TryDeliver();

  return ConsumerRegistration(weak_factory_.GetWeakPtr(), id);
}

bool MahoAiIngressCoordinator::HasConsumer() const {
  return !consumers_.empty();
}

base::WeakPtr<MahoAiIngressCoordinator> MahoAiIngressCoordinator::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void MahoAiIngressCoordinator::StoreSessionHandoff(SessionHandoff handoff) {
  session_handoff_ = std::move(handoff);
}

std::optional<MahoAiIngressCoordinator::SessionHandoff>
MahoAiIngressCoordinator::TakeSessionHandoff() {
  auto handoff = std::move(session_handoff_);
  session_handoff_.reset();
  return handoff;
}

void MahoAiIngressCoordinator::NotifyTerminal(uint64_t delivery_id) {
  if (!active_transaction_ || active_transaction_->delivery_id != delivery_id) {
    return;
  }

  base::UmaHistogramBoolean("Maho.AI.Ingress.Terminal", true);
  LOG(INFO) << "Ask Maho transaction reached terminal: "
            << active_transaction_->dispatch->request_id;

  active_transaction_.reset();
  TryDeliver();
}

void MahoAiIngressCoordinator::OnBrowserClosed(
    BrowserWindowInterface* browser) {
  if (!browser || browser != browser_) {
    return;
  }
  consumers_.clear();
  if (removal_posted_) {
    return;
  }
  removal_posted_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoAiIngressCoordinator::RemoveFromBrowser, browser_));
}

void MahoAiIngressCoordinator::UnregisterConsumer(int id) {
  auto it = std::find_if(consumers_.begin(), consumers_.end(),
                         [id](const Consumer& c) { return c.id == id; });
  if (it != consumers_.end()) {
    consumers_.erase(it);
  }

  if (active_transaction_ && active_transaction_->consumer_id == id) {
    if (active_transaction_->stage == TransactionStage::kDelivered) {
      base::UmaHistogramBoolean("Maho.AI.Ingress.Requeued", true);
      LOG(INFO)
          << "Active consumer unregistered while delivered but unaccepted. "
          << "Requeueing request: "
          << active_transaction_->dispatch->request_id;

      pending_dispatches_.push_front(std::move(active_transaction_->dispatch));
      active_transaction_.reset();
      TryDeliver();
    }
  }
}

void MahoAiIngressCoordinator::TryDeliver() {
  if (in_deliver_) {
    repump_requested_ = true;
    return;
  }

  do {
    repump_requested_ = false;

    if (active_transaction_ || pending_dispatches_.empty()) {
      return;
    }

    // Find the best consumer:
    // Priority 1: Newest live floating consumer (last in vector with kFloating)
    // Priority 2: Newest live sidebar consumer (last in vector with kSidebar)
    const Consumer* selected = nullptr;
    for (auto it = consumers_.rbegin(); it != consumers_.rend(); ++it) {
      if (it->type == ConsumerType::kFloating) {
        selected = &(*it);
        break;
      }
    }
    if (!selected) {
      for (auto it = consumers_.rbegin(); it != consumers_.rend(); ++it) {
        if (it->type == ConsumerType::kSidebar) {
          selected = &(*it);
          break;
        }
      }
    }

    if (!selected) {
      return;
    }

    DeliveryCallback callback = selected->callback;
    int consumer_id = selected->id;

    maho_ai::mojom::AskMahoDispatchPtr next =
        std::move(pending_dispatches_.front());
    pending_dispatches_.pop_front();

    std::string request_id = next->request_id;
    CHECK_NE(next_delivery_id_, 0u);
    const uint64_t delivery_id = next_delivery_id_++;
    active_transaction_.emplace(std::move(next), TransactionStage::kDelivered,
                                consumer_id, delivery_id);
    maho_ai::mojom::AskMahoDispatchPtr callback_dispatch =
        active_transaction_->dispatch.Clone();

    base::UmaHistogramBoolean("Maho.AI.Ingress.Delivered", true);
    LOG(INFO) << "Delivering Ask Maho request " << request_id << " to consumer "
              << consumer_id;

    base::WeakPtr<MahoAiIngressCoordinator> weak_this =
        weak_factory_.GetWeakPtr();
    in_deliver_ = true;
    callback.Run(*callback_dispatch, delivery_id,
                 base::BindOnce(&MahoAiIngressCoordinator::OnRequestAccepted,
                                weak_this, delivery_id));
    if (!weak_this) {
      return;
    }
    in_deliver_ = false;
  } while (repump_requested_);
}

void MahoAiIngressCoordinator::OnRequestAccepted(
    uint64_t delivery_id,
    const std::string& session_id) {
  if (!active_transaction_ || active_transaction_->delivery_id != delivery_id ||
      active_transaction_->stage != TransactionStage::kDelivered) {
    return;
  }

  active_transaction_->stage = TransactionStage::kAccepted;
  active_transaction_->session_id = session_id;

  base::UmaHistogramBoolean("Maho.AI.Ingress.Accepted", true);
  LOG(INFO) << "Ask Maho request " << active_transaction_->dispatch->request_id
            << " accepted by session " << session_id;
}

}  // namespace maho
