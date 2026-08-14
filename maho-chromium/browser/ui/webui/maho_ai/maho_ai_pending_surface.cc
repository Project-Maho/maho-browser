// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_pending_surface.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

#include "base/no_destructor.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"

namespace maho::ai {
namespace {

using ConsumerMap = std::map<PrefService*, MahoAiPendingSurface*>;

ConsumerMap& Consumers() {
  static base::NoDestructor<ConsumerMap> consumers;
  return *consumers;
}

maho_ai::mojom::SurfaceRequestPtr ChatFallback(uint64_t generation) {
  return maho_ai::mojom::SurfaceRequest::New(
      maho_ai::mojom::CompactSurface::kChat, generation);
}

}  // namespace

MahoAiPendingSurface::MahoAiPendingSurface(PrefService* prefs)
    : prefs_(prefs) {}

MahoAiPendingSurface::~MahoAiPendingSurface() {
  auto it = Consumers().find(prefs_);
  if (it != Consumers().end() && it->second == this) {
    Consumers().erase(it);
  }
}

// static
uint64_t MahoAiPendingSurface::Request(PrefService* prefs,
                                       maho_ai::mojom::CompactSurface surface) {
  if (!prefs) {
    return 0;
  }
  const int64_t previous = prefs->GetInt64(ai_prefs::kPendingSurfaceGeneration);
  const int64_t generation =
      previous == std::numeric_limits<int64_t>::max() ? previous : previous + 1;
  prefs->SetInt64(ai_prefs::kPendingSurfaceGeneration, generation);
  prefs->SetInteger(ai_prefs::kPendingSurface, static_cast<int>(surface));

  auto it = Consumers().find(prefs);
  if (it != Consumers().end() && it->second->callback_) {
    auto request = it->second->Consume(/*last_seen_generation=*/0);
    if (request->surface != maho_ai::mojom::CompactSurface::kChat) {
      it->second->callback_.Run(std::move(request));
    }
  }
  return static_cast<uint64_t>(generation);
}

maho_ai::mojom::SurfaceRequestPtr MahoAiPendingSurface::Consume(
    uint64_t last_seen_generation) {
  const int surface_value = prefs_->GetInteger(ai_prefs::kPendingSurface);
  const uint64_t generation = static_cast<uint64_t>(std::max<int64_t>(
      0, prefs_->GetInt64(ai_prefs::kPendingSurfaceGeneration)));

  // Clear before returning or notifying so re-entrant rendering cannot consume
  // the same request twice.
  prefs_->SetInteger(ai_prefs::kPendingSurface,
                     static_cast<int>(maho_ai::mojom::CompactSurface::kChat));

  if (generation <= last_seen_generation ||
      surface_value !=
          static_cast<int>(maho_ai::mojom::CompactSurface::kRoutines)) {
    return ChatFallback(generation);
  }
  return maho_ai::mojom::SurfaceRequest::New(
      maho_ai::mojom::CompactSurface::kRoutines, generation);
}

void MahoAiPendingSurface::SetConsumer(RequestCallback callback) {
  callback_ = std::move(callback);
  Consumers()[prefs_] = this;
}

}  // namespace maho::ai
