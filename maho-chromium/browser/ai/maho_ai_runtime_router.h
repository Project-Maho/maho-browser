// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_
#define MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"

class Browser;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

// Owns the single ACP runtime adapter. Reads the `maho.ai.runtime_command`
// pref for the binary path. The adapter is constructed lazily on the first
// call to GetActiveAdapter().
class MahoAiRuntimeRouter {
 public:
  MahoAiRuntimeRouter(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>(),
      base::RepeatingCallback<Browser*()> bound_browser_resolver =
          base::RepeatingCallback<Browser*()>());
  ~MahoAiRuntimeRouter();

  MahoAiRuntimeRouter(const MahoAiRuntimeRouter&) = delete;
  MahoAiRuntimeRouter& operator=(const MahoAiRuntimeRouter&) = delete;

  MahoAiRuntimeAdapter* GetActiveAdapter();
  std::string GetActiveAdapterName() const;
  bool IsAvailable() const;

 private:
  void RebuildAdapterIfStale();

  SEQUENCE_CHECKER(sequence_checker_);
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  base::RepeatingCallback<bool()> ai_gate_;
  base::RepeatingCallback<Browser*()> bound_browser_resolver_;

  std::unique_ptr<MahoAiRuntimeAdapter> active_adapter_;
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_
