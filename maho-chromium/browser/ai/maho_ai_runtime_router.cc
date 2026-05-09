// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <utility>

#include "base/logging.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_opencode_adapter.h"

MahoAiRuntimeRouter::MahoAiRuntimeRouter(
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : opencode_adapter_(
          std::make_unique<MahoOpenCodeAdapter>(browser,
                                                prefs,
                                                url_loader_factory)) {}

MahoAiRuntimeRouter::~MahoAiRuntimeRouter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MahoAiRuntimeAdapter* MahoAiRuntimeRouter::GetActiveAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return opencode_adapter_.get();
}

std::string MahoAiRuntimeRouter::GetActiveAdapterName() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return opencode_adapter_->GetAdapterName();
}

bool MahoAiRuntimeRouter::IsAvailable() const {
  return opencode_adapter_->IsAvailable();
}
