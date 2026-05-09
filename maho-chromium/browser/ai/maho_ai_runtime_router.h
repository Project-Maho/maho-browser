// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_
#define MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"

class Browser;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

// Owns the OpenCode runtime adapter and returns it as the sole active adapter.
class MahoAiRuntimeRouter {
 public:
  MahoAiRuntimeRouter(
      Browser* browser,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoAiRuntimeRouter();

  MahoAiRuntimeRouter(const MahoAiRuntimeRouter&) = delete;
  MahoAiRuntimeRouter& operator=(const MahoAiRuntimeRouter&) = delete;

  MahoAiRuntimeAdapter* GetActiveAdapter();
  std::string GetActiveAdapterName() const;
  bool IsAvailable() const;

 private:
  SEQUENCE_CHECKER(sequence_checker_);
  std::unique_ptr<MahoAiRuntimeAdapter> opencode_adapter_;
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_
