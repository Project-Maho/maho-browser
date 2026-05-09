// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_EXECUTOR_H_
#define MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_EXECUTOR_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"

class Browser;

namespace content {
class WebContents;
}

class MahoBrowserToolExecutor {
 public:
  using ToolResultCallback = base::OnceCallback<void(base::DictValue result)>;

  explicit MahoBrowserToolExecutor(Browser* browser,
                                   bool browser_tools_v1_enabled = true);
  MahoBrowserToolExecutor(const MahoBrowserToolExecutor&) = delete;
  MahoBrowserToolExecutor& operator=(const MahoBrowserToolExecutor&) = delete;
  ~MahoBrowserToolExecutor();

  void Execute(const std::string& tool_name,
               const base::DictValue& arguments,
               ToolResultCallback callback);

  static base::DictValue BuildErrorResult(const std::string& tool_name,
                                            const std::string& error);

 private:
  void ExecuteReadCurrentPage(ToolResultCallback callback);
  void ExecuteGetSelectedText(ToolResultCallback callback);
  void ExecuteGetActiveTab(ToolResultCallback callback);
  void ExecuteSearchInPage(const base::DictValue& arguments,
                           ToolResultCallback callback);
  void ExecuteExtractStructuredPageContext(ToolResultCallback callback);

  void OnReadCurrentPageContext(ToolResultCallback callback,
                               MahoAiPageContextExtractor::PageContextResult result);
  void OnSearchInPageContext(ToolResultCallback callback,
                             std::string query,
                             int max_results,
                             MahoAiPageContextExtractor::PageContextResult result);
  void OnExtractStructuredPageContext(
      ToolResultCallback callback,
      MahoAiPageContextExtractor::PageContextResult result);
  void OnSelectedTextFromContext(ToolResultCallback callback,
                                 MahoAiPageContextExtractor::PageContextResult result);

  static base::DictValue BuildActiveTabResult(Browser* browser);
  static base::DictValue BuildReadCurrentPageResult(
      const MahoAiPageContextExtractor::PageContextResult& result);
  static base::DictValue BuildSearchResult(const std::string& query,
                                             const std::string& page_text,
                                             size_t max_results);
  static std::string GetStringArgument(const base::DictValue& arguments,
                                       const std::string& key);
  static int GetIntArgument(const base::DictValue& arguments,
                            const std::string& key,
                            int default_value);

  content::WebContents* GetActiveWebContents() const;

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<Browser> browser_;
  bool browser_tools_v1_enabled_ = true;
  std::unique_ptr<MahoAiPageContextExtractor> page_context_extractor_;
  base::WeakPtrFactory<MahoBrowserToolExecutor> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_EXECUTOR_H_
