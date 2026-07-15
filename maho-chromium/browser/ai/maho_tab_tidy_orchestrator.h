// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_TAB_TIDY_ORCHESTRATOR_H_
#define MAHO_BROWSER_AI_MAHO_TAB_TIDY_ORCHESTRATOR_H_

#include <string>

#include "base/functional/callback.h"

class Browser;

namespace maho {

class MahoTabTidyOrchestrator {
 public:
  using ResultCallback =
      base::OnceCallback<void(int folder_count,
                              const std::string& error_message)>;

  // R-9 fail-closed gate: a non-null |ai_gate| that returns false stops the
  // run before any MahoCore/LLM ingress; null preserves regular behavior.
  static void RunOneShot(
      Browser* browser,
      ResultCallback callback,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>());

 private:
  MahoTabTidyOrchestrator() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_TAB_TIDY_ORCHESTRATOR_H_
