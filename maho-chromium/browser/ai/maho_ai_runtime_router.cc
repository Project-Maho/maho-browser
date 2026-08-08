// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <utility>

#include "base/logging.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_unified_agent_adapter.h"

MahoAiRuntimeRouter::MahoAiRuntimeRouter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    base::RepeatingCallback<bool()> ai_gate,
    base::RepeatingCallback<Browser*()> bound_browser_resolver)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      ai_gate_(std::move(ai_gate)),
      bound_browser_resolver_(std::move(bound_browser_resolver)) {
  RebuildAdapterIfStale();
}

MahoAiRuntimeRouter::~MahoAiRuntimeRouter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MahoAiRuntimeAdapter* MahoAiRuntimeRouter::GetActiveAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RebuildAdapterIfStale();
  return active_adapter_.get();
}

std::string MahoAiRuntimeRouter::GetActiveAdapterName() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_adapter_) {
    return "disabled";
  }
  return active_adapter_->GetAdapterName();
}

bool MahoAiRuntimeRouter::IsAvailable() const {
  return active_adapter_ && active_adapter_->IsAvailable();
}

void MahoAiRuntimeRouter::RebuildAdapterIfStale() {
  if (!ai_gate_ || !ai_gate_.Run()) {
    active_adapter_.reset();
    return;
  }
  if (active_adapter_) {
    return;
  }
  active_adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(
      prefs_, url_loader_factory_, ai_gate_, bound_browser_resolver_);
}
