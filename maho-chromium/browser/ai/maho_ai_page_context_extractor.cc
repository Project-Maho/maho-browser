#include "maho/browser/ai/maho_ai_page_context_extractor.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"
#include "maho/browser/ai/maho_credential_redaction.h"
#include "maho/browser/ai/maho_ai_security_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/mcp/maho_mcp_session.h"

extern "C" {
char* maho_memory_get_l1_briefing(MahoCore* core);
void maho_core_free_string(char* s);
}

namespace {

MahoAiPageContextExtractor::MemoryBriefingFetcher*
    g_memory_briefing_fetcher_for_testing = nullptr;

constexpr size_t kMaxMainTextLength = 50000;
constexpr size_t kMaxHeadings = 50;
constexpr size_t kMaxLinks = 20;

const char kNoActiveTabMessage[] =
    "There is no active page to analyze right now.";
const char kCannotAccessPageMessage[] =
    "Maho AI couldn't access the current page content. Try again once the page finishes loading.";
const char kExtractionFailedMessage[] =
    "Maho AI couldn't extract readable content from the current page.";

constexpr char kPageContextScript[] = R"JS(
(() => {
  const warnings = [];
  const title = document.title || '';
  const raw_href = location.href || '';
  const url = raw_href ? raw_href.split('#')[0] : '';
  const selected_text = window.getSelection ? String(window.getSelection() || '') : '';
  const meta = document.querySelector('meta[name="description"]');
  const meta_description = meta ? (meta.getAttribute('content') || '') : '';
  // Collect the first `cap` non-empty values in document order, stopping as
  // soon as the cap is reached. The previous map/filter/slice chain read every
  // matching node and built three full intermediate arrays before discarding
  // all but the first `cap` entries — costly on link-heavy pages. Results are
  // identical.
  const collectCapped = (selector, extract, cap) => {
    const out = [];
    const nodes = document.querySelectorAll(selector);
    for (let i = 0; i < nodes.length && out.length < cap; i++) {
      const value = (extract(nodes[i]) || '').trim();
      if (value.length > 0) {
        out.push(value);
      }
    }
    return out;
  };
  const headings = collectCapped('h1,h2,h3,h4,h5,h6', (n) => n.textContent, 50);
  const links = collectCapped('a[href]', (n) => n.href, 20);
  const body_text = document.body ? (document.body.innerText || '') : '';
  const main_text = body_text.length > 50000 ? body_text.slice(0, 50000) : body_text;

  if (!document.body) {
    warnings.push('document.body is missing.');
  }
  if (!main_text.trim()) {
    warnings.push('No readable body text was found.');
  }
  if (body_text.length > 50000) {
    warnings.push('Body text was truncated to the extractor limit.');
  }
  if (!headings.length) {
    warnings.push('No headings were found.');
  }

  const has_content = main_text.trim().length > 0 || selected_text.trim().length > 0 ||
                      headings.length > 0 || meta_description.trim().length > 0 ||
                      links.length > 0;
  const extraction_status = has_content
      ? (warnings.length > 0 ? 'partial' : 'success')
      : 'extraction_failed';

  return {
    title,
    url,
    selected_text,
    main_text,
    headings,
    meta_description,
    links,
    extraction_warnings: warnings,
    extraction_status,
  };
})()
)JS";

std::string NormalizeText(const std::string& text) {
  std::string trimmed;
  base::TrimWhitespaceASCII(text, base::TRIM_ALL, &trimmed);
  return trimmed;
}

}  // namespace

MahoAiPageContextExtractor::PageContextResult::PageContextResult() = default;
MahoAiPageContextExtractor::PageContextResult::~PageContextResult() = default;
MahoAiPageContextExtractor::PageContextResult::PageContextResult(
    PageContextResult&&) = default;
MahoAiPageContextExtractor::PageContextResult&
MahoAiPageContextExtractor::PageContextResult::operator=(PageContextResult&&) =
    default;

bool MahoAiPageContextExtractor::PageContextResult::HasUsableContent() const {
  return extraction_status == Status::kSuccess ||
         extraction_status == Status::kPartial;
}

std::string MahoAiPageContextExtractor::PageContextResult::GetLegacyText()
    const {
  if (!main_text.empty()) {
    return main_text;
  }
  if (!selected_text.empty()) {
    return selected_text;
  }
  if (!headings.empty()) {
    return base::JoinString(headings, "\n");
  }
  if (!meta_description.empty()) {
    return meta_description;
  }
  return std::string();
}

std::string
MahoAiPageContextExtractor::PageContextResult::GetExtractionStatusString()
    const {
  return StatusToString(extraction_status);
}

base::Value MahoAiPageContextExtractor::PageContextResult::ToValue() const {
  base::DictValue dict;
  dict.Set("extraction_status", GetExtractionStatusString());
  dict.Set("title", title);
  dict.Set("url", maho::ai_security::RedactUrlForAi(url));
  dict.Set("selected_text", selected_text);
  dict.Set("main_text", main_text);
  dict.Set("meta_description", meta_description);

  base::ListValue headings_value;
  for (const std::string& heading : headings) {
    headings_value.Append(heading);
  }
  dict.Set("headings", std::move(headings_value));

  base::ListValue links_value;
  for (const std::string& link : links) {
    links_value.Append(maho::ai_security::RedactUrlForAi(link));
  }
  dict.Set("links", std::move(links_value));

  base::ListValue warnings_value;
  for (const std::string& warning : extraction_warnings) {
    warnings_value.Append(warning);
  }
  dict.Set("extraction_warnings", std::move(warnings_value));

  base::Value serialized_value(std::move(dict));
  maho::credential_redaction::RedactStructuredValue(serialized_value);
  return serialized_value;
}

base::DictValue
MahoAiPageContextExtractor::ContextBlockProvenance::ToValue() const {
  base::DictValue dict;
  dict.Set("source", source);
  dict.Set("origin_url", origin_url);
  dict.Set("memory_key", memory_key);
  dict.Set("gathered_at_epoch_ms", static_cast<double>(gathered_at_epoch_ms));
  dict.Set("confidence_score", confidence_score);
  return dict;
}

base::DictValue
MahoAiPageContextExtractor::HistoryItemContext::ToValue() const {
  base::DictValue dict;
  dict.Set("title", title);
  dict.Set("url", url);
  dict.Set("snippet", snippet);
  dict.Set("last_visit_time_ms", static_cast<double>(last_visit_time_ms));
  dict.Set("provenance", provenance.ToValue());
  return dict;
}

base::DictValue
MahoAiPageContextExtractor::MemoryBriefingContext::ToValue() const {
  base::DictValue dict;
  dict.Set("available", available);
  dict.Set("status_message", status_message);
  dict.Set("briefing_text", briefing_text);
  dict.Set("provenance", provenance.ToValue());
  return dict;
}

base::DictValue
MahoAiPageContextExtractor::SearchContextSlot::ToValue() const {
  base::DictValue dict;
  dict.Set("available", available);
  dict.Set("query", query);
  dict.Set("reason", reason);
  base::ListValue snippets_list;
  for (const auto& snippet : snippets) {
    snippets_list.Append(snippet);
  }
  dict.Set("snippets", std::move(snippets_list));
  dict.Set("provenance", provenance.ToValue());
  return dict;
}

MahoAiPageContextExtractor::ContextBlockProvenance::ContextBlockProvenance() = default;
MahoAiPageContextExtractor::ContextBlockProvenance::~ContextBlockProvenance() = default;
MahoAiPageContextExtractor::ContextBlockProvenance::ContextBlockProvenance(const ContextBlockProvenance&) = default;
MahoAiPageContextExtractor::ContextBlockProvenance&
MahoAiPageContextExtractor::ContextBlockProvenance::operator=(const ContextBlockProvenance&) = default;
MahoAiPageContextExtractor::ContextBlockProvenance::ContextBlockProvenance(ContextBlockProvenance&&) = default;
MahoAiPageContextExtractor::ContextBlockProvenance&
MahoAiPageContextExtractor::ContextBlockProvenance::operator=(ContextBlockProvenance&&) = default;

MahoAiPageContextExtractor::HistoryItemContext::HistoryItemContext() = default;
MahoAiPageContextExtractor::HistoryItemContext::~HistoryItemContext() = default;
MahoAiPageContextExtractor::HistoryItemContext::HistoryItemContext(const HistoryItemContext&) = default;
MahoAiPageContextExtractor::HistoryItemContext&
MahoAiPageContextExtractor::HistoryItemContext::operator=(const HistoryItemContext&) = default;
MahoAiPageContextExtractor::HistoryItemContext::HistoryItemContext(HistoryItemContext&&) = default;
MahoAiPageContextExtractor::HistoryItemContext&
MahoAiPageContextExtractor::HistoryItemContext::operator=(HistoryItemContext&&) = default;

MahoAiPageContextExtractor::MemoryBriefingContext::MemoryBriefingContext() = default;
MahoAiPageContextExtractor::MemoryBriefingContext::~MemoryBriefingContext() = default;
MahoAiPageContextExtractor::MemoryBriefingContext::MemoryBriefingContext(const MemoryBriefingContext&) = default;
MahoAiPageContextExtractor::MemoryBriefingContext&
MahoAiPageContextExtractor::MemoryBriefingContext::operator=(const MemoryBriefingContext&) = default;
MahoAiPageContextExtractor::MemoryBriefingContext::MemoryBriefingContext(MemoryBriefingContext&&) = default;
MahoAiPageContextExtractor::MemoryBriefingContext&
MahoAiPageContextExtractor::MemoryBriefingContext::operator=(MemoryBriefingContext&&) = default;

MahoAiPageContextExtractor::SearchContextSlot::SearchContextSlot() = default;
MahoAiPageContextExtractor::SearchContextSlot::~SearchContextSlot() = default;
MahoAiPageContextExtractor::SearchContextSlot::SearchContextSlot(const SearchContextSlot&) = default;
MahoAiPageContextExtractor::SearchContextSlot&
MahoAiPageContextExtractor::SearchContextSlot::operator=(const SearchContextSlot&) = default;
MahoAiPageContextExtractor::SearchContextSlot::SearchContextSlot(SearchContextSlot&&) = default;
MahoAiPageContextExtractor::SearchContextSlot&
MahoAiPageContextExtractor::SearchContextSlot::operator=(SearchContextSlot&&) = default;

MahoAiPageContextExtractor::GatherOptions::GatherOptions() = default;
MahoAiPageContextExtractor::GatherOptions::~GatherOptions() = default;
MahoAiPageContextExtractor::GatherOptions::GatherOptions(const GatherOptions&) = default;
MahoAiPageContextExtractor::GatherOptions&
MahoAiPageContextExtractor::GatherOptions::operator=(const GatherOptions&) = default;
MahoAiPageContextExtractor::GatherOptions::GatherOptions(GatherOptions&&) = default;
MahoAiPageContextExtractor::GatherOptions&
MahoAiPageContextExtractor::GatherOptions::operator=(GatherOptions&&) = default;

MahoAiPageContextExtractor::GatheredContextResult::GatheredContextResult() =
    default;
MahoAiPageContextExtractor::GatheredContextResult::~GatheredContextResult() =
    default;
MahoAiPageContextExtractor::GatheredContextResult::GatheredContextResult(
    GatheredContextResult&&) = default;
MahoAiPageContextExtractor::GatheredContextResult&
MahoAiPageContextExtractor::GatheredContextResult::operator=(
    GatheredContextResult&&) = default;

base::DictValue
MahoAiPageContextExtractor::GatheredContextResult::ToValue() const {
  base::DictValue dict;
  dict.Set("success", success);
  dict.Set("total_estimated_tokens", static_cast<int>(total_estimated_tokens));
  dict.Set("token_budget", static_cast<int>(token_budget));
  dict.Set("truncated", truncated);
  dict.Set("page", page.ToValue());
  dict.Set("page_provenance", page_provenance.ToValue());

  base::ListValue history_list;
  for (const auto& item : history_items) {
    history_list.Append(item.ToValue());
  }
  dict.Set("history_items", std::move(history_list));
  dict.Set("memory", memory.ToValue());
  dict.Set("search", search.ToValue());
  dict.Set("suggested_question", suggested_question);

  base::ListValue warnings_list;
  for (const auto& warning : gather_warnings) {
    warnings_list.Append(warning);
  }
  dict.Set("gather_warnings", std::move(warnings_list));
  return dict;
}

MahoAiPageContextExtractor::MahoAiPageContextExtractor(
    Browser* browser,
    base::RepeatingCallback<bool()> ai_gate,
    InteractionDelegate interaction_delegate)
    : browser_(browser),
      ai_gate_(std::move(ai_gate)),
      interaction_delegate_(std::move(interaction_delegate)) {}

MahoAiPageContextExtractor::~MahoAiPageContextExtractor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
size_t MahoAiPageContextExtractor::EstimateTokens(std::string_view text) {
  if (text.empty()) {
    return 0;
  }
  return std::max<size_t>(1, (text.size() + 3) / 4);
}

// static
void MahoAiPageContextExtractor::ApplyBudgetCap(
    GatheredContextResult* result,
    size_t max_tokens) {
  if (!result) {
    return;
  }
  result->token_budget = max_tokens;
  size_t current_tokens = 0;

  // Priority 1: Page context (largest priority)
  size_t page_meta_tokens = EstimateTokens(result->page.title) +
                            EstimateTokens(result->page.url) +
                            EstimateTokens(result->page.selected_text);
  size_t main_text_tokens = EstimateTokens(result->page.main_text);

  if (page_meta_tokens + main_text_tokens > max_tokens) {
    // Page exceeds budget; truncate main_text to fit remaining budget
    if (page_meta_tokens < max_tokens) {
      size_t allowed_main_text_tokens = max_tokens - page_meta_tokens;
      size_t max_chars = allowed_main_text_tokens * 4;
      if (result->page.main_text.size() > max_chars) {
        result->page.main_text.resize(max_chars);
        result->page.extraction_warnings.push_back(
            "Page text was truncated to fit gatherer token budget.");
      }
      current_tokens = max_tokens;
    } else {
      result->page.main_text.clear();
      current_tokens = page_meta_tokens;
    }
    result->history_items.clear();
    if (result->memory.available) {
      result->memory.briefing_text.clear();
    }
    result->truncated = true;
    result->total_estimated_tokens = current_tokens;
    return;
  }

  current_tokens += (page_meta_tokens + main_text_tokens);

  // Priority 2: History items
  std::vector<HistoryItemContext> bounded_history;
  for (auto& item : result->history_items) {
    size_t item_tokens = EstimateTokens(item.title) +
                         EstimateTokens(item.url) +
                         EstimateTokens(item.snippet);
    if (current_tokens + item_tokens <= max_tokens) {
      current_tokens += item_tokens;
      bounded_history.push_back(std::move(item));
    } else {
      result->truncated = true;
      break;
    }
  }
  result->history_items = std::move(bounded_history);

  // Priority 3: Memory briefing
  if (result->memory.available) {
    size_t memory_tokens = EstimateTokens(result->memory.briefing_text);
    if (current_tokens + memory_tokens <= max_tokens) {
      current_tokens += memory_tokens;
    } else if (current_tokens < max_tokens) {
      size_t allowed_tokens = max_tokens - current_tokens;
      size_t max_chars = allowed_tokens * 4;
      if (result->memory.briefing_text.size() > max_chars) {
        result->memory.briefing_text.resize(max_chars);
      }
      current_tokens = max_tokens;
      result->truncated = true;
    } else {
      result->memory.briefing_text.clear();
      result->truncated = true;
    }
  }

  result->total_estimated_tokens = current_tokens;
}

std::string MahoAiPageContextExtractor::StatusToString(
    PageContextResult::Status status) {
  switch (status) {
    case PageContextResult::Status::kSuccess:
      return "success";
    case PageContextResult::Status::kPartial:
      return "partial";
    case PageContextResult::Status::kNoActiveTab:
      return "no_active_tab";
    case PageContextResult::Status::kCannotAccess:
      return "cannot_access";
    case PageContextResult::Status::kExtractionFailed:
      return "extraction_failed";
  }
}

std::vector<std::string> MahoAiPageContextExtractor::ExtractStringList(
    const base::Value* value) {
  std::vector<std::string> result;
  if (!value || !value->is_list()) {
    return result;
  }
  const base::ListValue& list = value->GetList();
  for (const base::Value& entry : list) {
    if (!entry.is_string()) {
      continue;
    }
    const std::string text = NormalizeText(entry.GetString());
    if (!text.empty()) {
      result.push_back(text);
    }
  }
  return result;
}

MahoAiPageContextExtractor::PageContextResult
MahoAiPageContextExtractor::BuildResultFromJsValue(const std::string& title,
                                                   const std::string& url,
                                                   base::Value result) {
  PageContextResult page_context;
  page_context.title = maho::ai_security::RedactCredentialTokens(title);
  page_context.url = maho::ai_security::RedactUrlForAi(url);

  if (!result.is_dict()) {
    page_context.extraction_status =
        PageContextResult::Status::kExtractionFailed;
    page_context.extraction_warnings.push_back(kExtractionFailedMessage);
    return page_context;
  }

  const base::DictValue* dict = result.GetIfDict();
  if (const std::string* selected_text = dict->FindString("selected_text")) {
    page_context.selected_text =
        maho::ai_security::RedactCredentialTokens(NormalizeText(*selected_text));
  }
  if (const std::string* main_text = dict->FindString("main_text")) {
    page_context.main_text =
        maho::ai_security::RedactCredentialTokens(NormalizeText(*main_text));
  }
  if (const std::string* meta_description =
          dict->FindString("meta_description")) {
    page_context.meta_description =
        maho::ai_security::RedactCredentialTokens(
            NormalizeText(*meta_description));
  }
  if (const std::string* extraction_status =
          dict->FindString("extraction_status")) {
    if (*extraction_status == "success") {
      page_context.extraction_status = PageContextResult::Status::kSuccess;
    } else if (*extraction_status == "partial") {
      page_context.extraction_status = PageContextResult::Status::kPartial;
    }
  }

  page_context.headings = ExtractStringList(dict->Find("headings"));
  for (std::string& heading : page_context.headings) {
    heading = maho::ai_security::RedactCredentialTokens(heading);
  }
  page_context.links = ExtractStringList(dict->Find("links"));
  for (std::string& link : page_context.links) {
    link = maho::ai_security::RedactUrlForAi(link);
  }
  page_context.extraction_warnings =
      ExtractStringList(dict->Find("extraction_warnings"));
  for (std::string& warning : page_context.extraction_warnings) {
    warning = maho::ai_security::RedactCredentialTokens(warning);
  }

  if (page_context.headings.size() > kMaxHeadings) {
    page_context.headings.resize(kMaxHeadings);
    page_context.extraction_warnings.push_back(
        "Headings were truncated to the extractor limit.");
  }
  if (page_context.links.size() > kMaxLinks) {
    page_context.links.resize(kMaxLinks);
    page_context.extraction_warnings.push_back(
        "Links were truncated to the extractor limit.");
  }

  page_context.headings.erase(
      std::remove_if(page_context.headings.begin(), page_context.headings.end(),
                     [](const std::string& value) { return value.empty(); }),
      page_context.headings.end());
  page_context.links.erase(
      std::remove_if(page_context.links.begin(), page_context.links.end(),
                     [](const std::string& value) { return value.empty(); }),
      page_context.links.end());
  page_context.extraction_warnings.erase(
      std::remove_if(page_context.extraction_warnings.begin(),
                     page_context.extraction_warnings.end(),
                     [](const std::string& value) { return value.empty(); }),
      page_context.extraction_warnings.end());

  const bool has_textual_content = !page_context.main_text.empty() ||
                                   !page_context.selected_text.empty() ||
                                   !page_context.headings.empty() ||
                                   !page_context.meta_description.empty() ||
                                   !page_context.links.empty();
  if (!has_textual_content) {
    page_context.extraction_status =
        PageContextResult::Status::kExtractionFailed;
    if (page_context.extraction_warnings.empty()) {
      page_context.extraction_warnings.push_back(kExtractionFailedMessage);
    }
    return page_context;
  }

  if (page_context.extraction_status ==
      PageContextResult::Status::kExtractionFailed) {
    page_context.extraction_status = PageContextResult::Status::kSuccess;
  }

  if (!page_context.main_text.empty() &&
      page_context.main_text.size() >= kMaxMainTextLength) {
    page_context.extraction_warnings.push_back(
        "Body text was truncated to the extractor limit.");
  }

  return page_context;
}

content::WebContents* MahoAiPageContextExtractor::GetActiveWebContents() const {
  if (!browser_) {
    return nullptr;
  }
  return browser_->GetTabStripModel()->GetActiveWebContents();
}

content::WebContents* MahoAiPageContextExtractor::GetWebContentsForTabId(
    int tab_id) const {
  if (!browser_) {
    return nullptr;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (!tab_strip) {
    return nullptr;
  }
  const int count = tab_strip->count();
  for (int i = 0; i < count; ++i) {
    content::WebContents* wc = tab_strip->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    sessions::SessionTabHelper* helper =
        sessions::SessionTabHelper::FromWebContents(wc);
    if (!helper) {
      continue;
    }
    if (helper->session_id().id() == tab_id) {
      return wc;
    }
  }
  return nullptr;
}

void MahoAiPageContextExtractor::GetPageContext(PageContextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ai_gate_ || !ai_gate_.Run()) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kCannotAccess;
    std::move(callback).Run(std::move(result));
    return;
  }

  content::WebContents* active_wc = GetActiveWebContents();
  if (!active_wc) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kNoActiveTab;
    result.extraction_warnings.push_back(kNoActiveTabMessage);
    std::move(callback).Run(std::move(result));
    return;
  }

  const GURL& visible_url = active_wc->GetVisibleURL();
  if (maho::ai_security::IsUrlBlocked(visible_url)) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kCannotAccess;
    result.title = base::UTF16ToUTF8(active_wc->GetTitle());
    result.url = maho::ai_security::RedactUrlForAi(visible_url);
    result.extraction_warnings.push_back(
        "Maho AI cannot analyze internal browser pages.");
    std::move(callback).Run(std::move(result));
    return;
  }

  const std::string title = base::UTF16ToUTF8(active_wc->GetTitle());
  const std::string url = maho::ai_security::RedactUrlForAi(active_wc->GetVisibleURL());

  content::RenderFrameHost* rfh = active_wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kCannotAccess;
    result.title = title;
    result.url = url;
    result.extraction_warnings.push_back(kCannotAccessPageMessage);
    std::move(callback).Run(std::move(result));
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string_view(kPageContextScript)),
      base::BindOnce(&MahoAiPageContextExtractor::OnPageContextExtracted,
                     weak_factory_.GetWeakPtr(), title, url,
                     std::move(callback)),
      content::ISOLATED_WORLD_ID_CONTENT_END + 1);
}

void MahoAiPageContextExtractor::OnPageContextExtracted(
    const std::string& title,
    const std::string& url,
    PageContextCallback callback,
    base::Value result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      BuildResultFromJsValue(title, url, std::move(result)));
}

void MahoAiPageContextExtractor::GetPageContextForTabId(
    int tab_id,
    PageContextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ai_gate_ || !ai_gate_.Run()) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kCannotAccess;
    std::move(callback).Run(std::move(result));
    return;
  }

  content::WebContents* wc = GetWebContentsForTabId(tab_id);
  if (!wc) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kNoActiveTab;
    result.extraction_warnings.push_back(kNoActiveTabMessage);
    std::move(callback).Run(std::move(result));
    return;
  }

  const GURL& visible_url = wc->GetVisibleURL();
  if (maho::ai_security::IsUrlBlocked(visible_url)) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kCannotAccess;
    result.title = base::UTF16ToUTF8(wc->GetTitle());
    result.url = maho::ai_security::RedactUrlForAi(visible_url);
    result.extraction_warnings.push_back(
        "Maho AI cannot analyze internal browser pages.");
    std::move(callback).Run(std::move(result));
    return;
  }

  const std::string title = base::UTF16ToUTF8(wc->GetTitle());
  const std::string url = maho::ai_security::RedactUrlForAi(wc->GetVisibleURL());

  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    PageContextResult result;
    result.extraction_status = PageContextResult::Status::kCannotAccess;
    result.title = title;
    result.url = url;
    result.extraction_warnings.push_back(kCannotAccessPageMessage);
    std::move(callback).Run(std::move(result));
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string_view(kPageContextScript)),
      base::BindOnce(&MahoAiPageContextExtractor::OnPageContextExtracted,
                     weak_factory_.GetWeakPtr(), title, url,
                     std::move(callback)),
      content::ISOLATED_WORLD_ID_CONTENT_END + 1);
}

void MahoAiPageContextExtractor::GatherContext(
    GatherOptions options,
    GatherContextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (options.include_page) {
    auto on_page = base::BindOnce(
        &MahoAiPageContextExtractor::OnGatherPageExtracted,
        weak_factory_.GetWeakPtr(), options, std::move(callback));
    if (options.tab_id >= 0) {
      GetPageContextForTabId(options.tab_id, std::move(on_page));
    } else {
      GetPageContext(std::move(on_page));
    }
  } else {
    PageContextResult empty_page;
    OnGatherPageExtracted(options, std::move(callback), std::move(empty_page));
  }
}

// static
void MahoAiPageContextExtractor::SetMemoryBriefingFetcherForTesting(
    MemoryBriefingFetcher fetcher) {
  delete g_memory_briefing_fetcher_for_testing;
  g_memory_briefing_fetcher_for_testing =
      new MemoryBriefingFetcher(std::move(fetcher));
}

// static
void MahoAiPageContextExtractor::ResetMemoryBriefingFetcherForTesting() {
  delete g_memory_briefing_fetcher_for_testing;
  g_memory_briefing_fetcher_for_testing = nullptr;
}

void MahoAiPageContextExtractor::OnGatherPageExtracted(
    GatherOptions options,
    GatherContextCallback callback,
    PageContextResult page_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  GatheredContextResult result;
  result.token_budget = options.max_total_tokens;
  const int64_t now_ms =
      base::Time::Now().InMillisecondsSinceUnixEpoch();

  // 1. Page context and provenance
  result.page = std::move(page_result);
  result.page_provenance.source = "active_tab";
  result.page_provenance.origin_url = result.page.url;
  result.page_provenance.gathered_at_epoch_ms = now_ms;
  result.page_provenance.confidence_score =
      result.page.HasUsableContent() ? 1.0 : 0.0;

  // 2. History search results via DesktopAgent capability registry
  if (options.include_history && !options.history_query.empty()) {
    result.history_items = GatherHistoryViaRegistry(
        options.history_query, options.max_history_results);
  }

  // 3. Memory L1 briefing context slot (wired via FFI / testing hook)
  if (options.include_memory) {
    result.memory.provenance.source = "memory";
    result.memory.provenance.gathered_at_epoch_ms = now_ms;

    if (g_memory_briefing_fetcher_for_testing &&
        *g_memory_briefing_fetcher_for_testing) {
      std::optional<std::string> test_briefing =
          g_memory_briefing_fetcher_for_testing->Run();
      if (test_briefing.has_value()) {
        result.memory.available = true;
        result.memory.briefing_text = std::move(*test_briefing);
        result.memory.status_message.clear();
        result.memory.provenance.confidence_score = 1.0;
      } else {
        result.memory.available = false;
        result.memory.status_message = "No L1 memory briefing available";
        result.memory.provenance.confidence_score = 0.0;
      }
    } else {
      ::MahoCore* core = maho::GetCore();
      if (!core) {
        result.memory.available = false;
        result.memory.status_message =
            "Memory L1 briefing unavailable (MahoCore uninitialized)";
        result.memory.provenance.confidence_score = 0.0;
      } else {
        char* briefing_c = maho_memory_get_l1_briefing(core);
        if (briefing_c) {
          result.memory.available = true;
          result.memory.briefing_text = std::string(briefing_c);
          result.memory.status_message.clear();
          result.memory.provenance.confidence_score = 1.0;
          maho_core_free_string(briefing_c);
        } else {
          result.memory.available = false;
          result.memory.status_message = "No L1 memory briefing available";
          result.memory.provenance.confidence_score = 0.0;
        }
      }
    }
  }

  // 4. Search source slot (derived query, honest unavailable)
  if (options.include_search) {
    std::string search_query;
    if (!result.page.selected_text.empty() && !result.page.title.empty()) {
      search_query = result.page.title + " " + result.page.selected_text;
    } else if (!result.page.selected_text.empty()) {
      search_query = result.page.selected_text;
    } else if (!result.page.title.empty()) {
      search_query = result.page.title;
    }

    result.search.query = search_query;
    result.search.available = false;
    result.search.reason =
        "Direct agent tool execution via FFI not yet exposed in C++ runtime "
        "(Wave-next call site: maho_agent_execute_tool/web_search)";
    result.search.provenance.source = "web_search";
    result.search.provenance.gathered_at_epoch_ms = now_ms;
    result.search.provenance.confidence_score = 0.0;
  }

  // 5. Question Fallback (plan #23)
  constexpr size_t kLowContextTokenThreshold = 30;
  const bool minimal_page_context =
      !result.page.HasUsableContent() ||
      EstimateTokens(result.page.main_text) < kLowContextTokenThreshold;

  if (minimal_page_context) {
    if (!result.page.selected_text.empty()) {
      result.suggested_question =
          "What would you like to know about \"" + result.page.selected_text + "\"?";
    } else if (!result.page.title.empty()) {
      result.suggested_question =
          "What would you like to know about " + result.page.title + "?";
    } else if (!result.page.url.empty()) {
      result.suggested_question = "How can I help you with this page?";
    } else {
      result.suggested_question = "How can I help you today?";
    }

    if (interaction_delegate_ && !result.suggested_question.empty()) {
      // Surface suggested_question through the existing interaction request path
      // as a structured ask_user_question-style prompt (NOT auto-submitting; UI-visible request).
      std::string interaction_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      interaction_delegate_.Run(result.suggested_question, interaction_id);
    }
  }

  // Determine overall success
  result.success = result.page.HasUsableContent() ||
                   !result.history_items.empty() ||
                   result.memory.available ||
                   result.search.available;

  // Apply bounded budget cap with largest-first priority: Page > History > Memory
  ApplyBudgetCap(&result, options.max_total_tokens);

  std::move(callback).Run(std::move(result));
}

std::vector<MahoAiPageContextExtractor::HistoryItemContext>
MahoAiPageContextExtractor::GatherHistoryViaRegistry(
    const std::string& query,
    size_t max_results) {
  std::vector<HistoryItemContext> items;
  const auto* cap =
      MahoBrowserToolRegistry::FindCapability("browser_history_search_desktop");
  if (!cap) {
    return items;
  }

  maho::MahoMcpBrowserDelegate* delegate =
      maho::MahoMcpSession::GetBrowserDelegateForBrowserActions();
  if (!delegate) {
    return items;
  }

  const int64_t now_ms =
      base::Time::Now().InMillisecondsSinceUnixEpoch();
  const auto entries = delegate->SearchHistory(query, max_results);
  items.reserve(entries.size());
  for (const auto& entry : entries) {
    HistoryItemContext item;
    item.title = maho::ai_security::RedactCredentialTokens(entry.title);
    item.url = maho::ai_security::RedactUrlForAi(entry.url);
    item.snippet = item.title;
    item.last_visit_time_ms = static_cast<int64_t>(entry.visited_at * 1000.0);
    item.provenance.source = "browser_history";
    item.provenance.origin_url = item.url;
    item.provenance.gathered_at_epoch_ms = now_ms;
    item.provenance.confidence_score = 0.85;
    items.push_back(std::move(item));
  }
  return items;
}
