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

  static void RunOneShot(Browser* browser, ResultCallback callback);

 private:
  MahoTabTidyOrchestrator() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_TAB_TIDY_ORCHESTRATOR_H_
