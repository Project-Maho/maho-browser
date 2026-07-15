#include "maho/browser/ai/maho_ai_page_context_extractor.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/ai/maho_ai_security_utils.h"

namespace {

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
  const headings = Array.from(document.querySelectorAll('h1,h2,h3,h4,h5,h6'))
      .map((heading) => (heading.textContent || '').trim())
      .filter((heading) => heading.length > 0)
      .slice(0, 50);
  const links = Array.from(document.querySelectorAll('a[href]'))
      .map((link) => (link.href || '').trim())
      .filter((link) => link.length > 0)
      .slice(0, 20);
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
  dict.Set("url", url);
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
    links_value.Append(link);
  }
  dict.Set("links", std::move(links_value));

  base::ListValue warnings_value;
  for (const std::string& warning : extraction_warnings) {
    warnings_value.Append(warning);
  }
  dict.Set("extraction_warnings", std::move(warnings_value));

  return base::Value(std::move(dict));
}

MahoAiPageContextExtractor::MahoAiPageContextExtractor(
    Browser* browser,
    base::RepeatingCallback<bool()> ai_gate)
    : browser_(browser), ai_gate_(std::move(ai_gate)) {}

MahoAiPageContextExtractor::~MahoAiPageContextExtractor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
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
  return "extraction_failed";
}

std::vector<std::string> MahoAiPageContextExtractor::ExtractStringList(
    const base::Value* value) {
  std::vector<std::string> values;
  const auto* list = value ? value->GetIfList() : nullptr;
  if (!list) {
    return values;
  }
  for (const base::Value& item : *list) {
    const std::string* str = item.GetIfString();
    if (str && !str->empty()) {
      values.push_back(*str);
    }
  }
  return values;
}

MahoAiPageContextExtractor::PageContextResult
MahoAiPageContextExtractor::BuildResultFromJsValue(const std::string& title,
                                                   const std::string& url,
                                                   base::Value result) {
  PageContextResult page_context;
  page_context.title = title;
  page_context.url = url;

  const auto* dict = result.GetIfDict();
  if (!dict) {
    page_context.extraction_status =
        PageContextResult::Status::kExtractionFailed;
    page_context.extraction_warnings.push_back(kExtractionFailedMessage);
    return page_context;
  }

  if (const std::string* extracted_title = dict->FindString("title")) {
    page_context.title = NormalizeText(*extracted_title);
  }
  if (const std::string* extracted_url = dict->FindString("url")) {
    page_context.url = maho::ai_security::RedactUrlForAi(NormalizeText(*extracted_url));
  }
  if (const std::string* selected_text = dict->FindString("selected_text")) {
    page_context.selected_text = NormalizeText(*selected_text);
  }
  if (const std::string* main_text = dict->FindString("main_text")) {
    page_context.main_text = NormalizeText(*main_text);
  }
  if (const std::string* meta_description =
          dict->FindString("meta_description")) {
    page_context.meta_description = NormalizeText(*meta_description);
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
  page_context.links = ExtractStringList(dict->Find("links"));
  page_context.extraction_warnings =
      ExtractStringList(dict->Find("extraction_warnings"));

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
  return browser_->tab_strip_model()->GetActiveWebContents();
}

content::WebContents* MahoAiPageContextExtractor::GetWebContentsForTabId(
    int tab_id) const {
  if (!browser_) {
    return nullptr;
  }
  TabStripModel* tab_strip = browser_->tab_strip_model();
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

  if (ai_gate_ && !ai_gate_.Run()) {
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

  if (ai_gate_ && !ai_gate_.Run()) {
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
