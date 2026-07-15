// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_browser_tool_executor.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"

namespace {

constexpr size_t kDefaultSearchResultLimit = 5;
constexpr size_t kSnippetRadius = 80;

bool HasUsablePageContext(
    const MahoAiPageContextExtractor::PageContextResult& result) {
  return result.HasUsableContent();
}

std::string ToolStatusToString(
    MahoAiPageContextExtractor::PageContextResult::Status status) {
  switch (status) {
    case MahoAiPageContextExtractor::PageContextResult::Status::kSuccess:
      return "success";
    case MahoAiPageContextExtractor::PageContextResult::Status::kPartial:
      return "partial";
    case MahoAiPageContextExtractor::PageContextResult::Status::kNoActiveTab:
      return "no_active_tab";
    case MahoAiPageContextExtractor::PageContextResult::Status::kCannotAccess:
      return "cannot_access";
    case MahoAiPageContextExtractor::PageContextResult::Status::kExtractionFailed:
      return "extraction_failed";
  }
  return "extraction_failed";
}

}  // namespace

MahoBrowserToolExecutor::MahoBrowserToolExecutor(
    Browser* browser,
    bool browser_tools_v1_enabled,
    base::RepeatingCallback<bool()> ai_gate)
    : browser_(browser),
      browser_tools_v1_enabled_(browser_tools_v1_enabled),
      ai_gate_(ai_gate),
      page_context_extractor_(
          std::make_unique<MahoAiPageContextExtractor>(browser, ai_gate)) {}

MahoBrowserToolExecutor::~MahoBrowserToolExecutor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
base::DictValue MahoBrowserToolExecutor::BuildErrorResult(
    const std::string& tool_name,
    const std::string& error) {
  base::DictValue result;
  result.Set("ok", false);
  result.Set("tool", tool_name);
  result.Set("error", error);
  result.Set("context_scope", "active_tab");
  return result;
}

void MahoBrowserToolExecutor::Execute(const std::string& tool_name,
                                      const base::DictValue& arguments,
                                      ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (ai_gate_ && !ai_gate_.Run()) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, "AI is not available in this context."));
    return;
  }

  if (!browser_tools_v1_enabled_) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, "Browser tools v1 is disabled."));
    return;
  }

  const MahoBrowserToolRegistry::ToolSchema* schema =
      MahoBrowserToolRegistry::FindToolSchema(tool_name);
  if (!schema) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, "Unknown browser tool: " + tool_name));
    return;
  }

  if (tool_name == "read_current_page") {
    ExecuteReadCurrentPage(std::move(callback));
    return;
  }
  if (tool_name == "get_selected_text") {
    ExecuteGetSelectedText(std::move(callback));
    return;
  }
  if (tool_name == "get_active_tab") {
    ExecuteGetActiveTab(std::move(callback));
    return;
  }
  if (tool_name == "search_in_page") {
    ExecuteSearchInPage(arguments, std::move(callback));
    return;
  }
  if (tool_name == "extract_structured_page_context") {
    ExecuteExtractStructuredPageContext(std::move(callback));
    return;
  }

  std::move(callback).Run(
      BuildErrorResult(tool_name, "Browser tool is registered but not implemented."));
}

void MahoBrowserToolExecutor::ExecuteReadCurrentPage(ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnReadCurrentPageContext,
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBrowserToolExecutor::ExecuteGetSelectedText(ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnSelectedTextFromContext,
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBrowserToolExecutor::ExecuteGetActiveTab(ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildActiveTabResult(browser_));
}

void MahoBrowserToolExecutor::ExecuteSearchInPage(
    const base::DictValue& arguments,
    ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::string query = GetStringArgument(arguments, "query");
  if (query.empty()) {
    std::move(callback).Run(
        BuildErrorResult("search_in_page", "Missing required argument: query"));
    return;
  }

  const int max_results = GetIntArgument(arguments, "max_results", kDefaultSearchResultLimit);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnSearchInPageContext,
      weak_factory_.GetWeakPtr(), std::move(callback), query, max_results));
}

void MahoBrowserToolExecutor::ExecuteExtractStructuredPageContext(
    ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnExtractStructuredPageContext,
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBrowserToolExecutor::OnReadCurrentPageContext(
    ToolResultCallback callback,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildReadCurrentPageResult(result));
}

void MahoBrowserToolExecutor::OnSearchInPageContext(
    ToolResultCallback callback,
    std::string query,
    int max_results,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!HasUsablePageContext(result)) {
    std::move(callback).Run(
        BuildErrorResult("search_in_page", "Unable to read page for search."));
    return;
  }
  std::move(callback).Run(
      BuildSearchResult(query, result.main_text, static_cast<size_t>(max_results)));
}

void MahoBrowserToolExecutor::OnExtractStructuredPageContext(
    ToolResultCallback callback,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::DictValue output;
  output.Set("tool", "extract_structured_page_context");
  output.Set("context_scope", "active_tab");
  output.Set("ok", HasUsablePageContext(result));
  output.Set("page_status", ToolStatusToString(result.extraction_status));
  if (!HasUsablePageContext(result)) {
    output.Set("error", "Unable to extract structured page context.");
  } else {
    base::Value data = result.ToValue();
    output.Set("data", std::move(data));
  }
  std::move(callback).Run(std::move(output));
}

void MahoBrowserToolExecutor::OnSelectedTextFromContext(
    ToolResultCallback callback,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::DictValue output;
  output.Set("tool", "get_selected_text");
  output.Set("context_scope", "active_tab");
  output.Set("ok", HasUsablePageContext(result));
  output.Set("page_status", ToolStatusToString(result.extraction_status));
  if (!HasUsablePageContext(result)) {
    output.Set("error", "Unable to read the selected text.");
  } else {
    output.Set("selected_text", result.selected_text);
  }
  std::move(callback).Run(std::move(output));
}

// static
base::DictValue MahoBrowserToolExecutor::BuildActiveTabResult(Browser* browser) {
  base::DictValue result;
  result.Set("tool", "get_active_tab");
  result.Set("context_scope", "active_tab");

  if (!browser || !browser->tab_strip_model()) {
    result.Set("ok", false);
    result.Set("error", "No active browser tab is available.");
    return result;
  }

  content::WebContents* web_contents = browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    result.Set("ok", false);
    result.Set("error", "No active browser tab is available.");
    return result;
  }

  result.Set("ok", true);
  result.Set("title", base::UTF16ToUTF8(web_contents->GetTitle()));
  result.Set("url", web_contents->GetVisibleURL().spec());
  result.Set("active_index", browser->tab_strip_model()->active_index());
  result.Set("tab_count", browser->tab_strip_model()->count());
  return result;
}

// static
base::DictValue MahoBrowserToolExecutor::BuildReadCurrentPageResult(
    const MahoAiPageContextExtractor::PageContextResult& result) {
  base::DictValue output;
  output.Set("tool", "read_current_page");
  output.Set("context_scope", "active_tab");
  output.Set("ok", HasUsablePageContext(result));
  output.Set("page_status", ToolStatusToString(result.extraction_status));
  if (!HasUsablePageContext(result)) {
    output.Set("error", "Unable to read the current page.");
  } else {
    output.Set("title", result.title);
    output.Set("url", result.url);
    output.Set("text", result.main_text);
    output.Set("selected_text", result.selected_text);
  }
  return output;
}

// static
base::DictValue MahoBrowserToolExecutor::BuildSearchResult(
    const std::string& query,
    const std::string& page_text,
    size_t max_results) {
  base::DictValue output;
  output.Set("ok", true);
  output.Set("tool", "search_in_page");
  output.Set("context_scope", "active_tab");
  output.Set("query", query);

  base::ListValue matches;
  std::string lowered_text = base::ToLowerASCII(page_text);
  std::string lowered_query = base::ToLowerASCII(query);
  size_t cursor = 0;
  while (matches.size() < max_results) {
    const size_t found = lowered_text.find(lowered_query, cursor);
    if (found == std::string::npos) {
      break;
    }

    const size_t snippet_start = found > kSnippetRadius ? found - kSnippetRadius : 0;
    const size_t snippet_end = std::min(page_text.size(),
                                        found + query.size() + kSnippetRadius);
    base::DictValue match;
    match.Set("index", static_cast<int>(found));
    match.Set("snippet", page_text.substr(snippet_start, snippet_end - snippet_start));
    matches.Append(std::move(match));

    cursor = found + std::max<size_t>(1, query.size());
  }

  output.Set("matches", std::move(matches));
  output.Set("match_count", static_cast<int>(matches.size()));
  return output;
}

// static
std::string MahoBrowserToolExecutor::GetStringArgument(
    const base::DictValue& arguments,
    const std::string& key) {
  const std::string* value = arguments.FindString(key);
  return value ? *value : std::string();
}

// static
int MahoBrowserToolExecutor::GetIntArgument(const base::DictValue& arguments,
                                            const std::string& key,
                                            int default_value) {
  std::optional<int> value = arguments.FindInt(key);
  return value.value_or(default_value);
}

content::WebContents* MahoBrowserToolExecutor::GetActiveWebContents() const {
  if (!browser_ || !browser_->tab_strip_model()) {
    return nullptr;
  }
  return browser_->tab_strip_model()->GetActiveWebContents();
}
