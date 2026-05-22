// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <utility>

#include "base/logging.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_opencode_acp_adapter.h"

MahoAiRuntimeRouter::MahoAiRuntimeRouter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)) {
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
  return active_adapter_->GetAdapterName();
}

bool MahoAiRuntimeRouter::IsAvailable() const {
  return active_adapter_->IsAvailable();
}

void MahoAiRuntimeRouter::RebuildAdapterIfStale() {
  if (active_adapter_) {
    return;
  }
  active_adapter_ =
      std::make_unique<MahoOpenCodeAcpAdapter>(prefs_, url_loader_factory_);
}
