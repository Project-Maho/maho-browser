#ifndef MAHO_BROWSER_AI_MAHO_AI_PAGE_CONTEXT_EXTRACTOR_H_
#define MAHO_BROWSER_AI_MAHO_AI_PAGE_CONTEXT_EXTRACTOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

// Context Gatherer and Page Context Extractor for Maho AI Agent.
//
// Gathers multi-source context across:
// 1. Current/targeted tab page context (DOM, headings, selection, meta)
// 2. Browser history search results (via DesktopAgent tool registry capability)
// 3. Memory L1 briefing (structured slot; unavailable in C++ runtime, honest TODO)
//
// Enforces a strict bounded token budget cap with largest-first priority:
//   Page Context > History Results > Memory Briefing
//
// Prompt-Injection Safety Invariant:
// Gathered page and history contents are treated strictly as untrusted DATA,
// wrapped in data envelopes and never parsed or executed as instruction authority.
class MahoAiPageContextExtractor {
 public:
  struct PageContextResult {
    enum class Status {
      kSuccess,
      kPartial,
      kNoActiveTab,
      kCannotAccess,
      kExtractionFailed,
    };

    PageContextResult();
    ~PageContextResult();
    PageContextResult(PageContextResult&&);
    PageContextResult& operator=(PageContextResult&&);

    bool HasUsableContent() const;
    std::string GetLegacyText() const;
    std::string GetExtractionStatusString() const;
    base::Value ToValue() const;

    Status extraction_status = Status::kExtractionFailed;
    std::string title;
    std::string url;
    std::string selected_text;
    std::string main_text;
    std::vector<std::string> headings;
    std::string meta_description;
    std::vector<std::string> links;
    std::vector<std::string> extraction_warnings;
  };

  // Typed provenance metadata for context blocks.
  struct ContextBlockProvenance {
    ContextBlockProvenance();
    ~ContextBlockProvenance();
    ContextBlockProvenance(const ContextBlockProvenance&);
    ContextBlockProvenance& operator=(const ContextBlockProvenance&);
    ContextBlockProvenance(ContextBlockProvenance&&);
    ContextBlockProvenance& operator=(ContextBlockProvenance&&);

    std::string source;      // "active_tab", "browser_history", "memory"
    std::string origin_url;  // URL if available
    std::string memory_key;  // key if memory
    int64_t gathered_at_epoch_ms = 0;
    double confidence_score = 1.0;

    base::DictValue ToValue() const;
  };

  // Structured browser history search context item.
  struct HistoryItemContext {
    HistoryItemContext();
    ~HistoryItemContext();
    HistoryItemContext(const HistoryItemContext&);
    HistoryItemContext& operator=(const HistoryItemContext&);
    HistoryItemContext(HistoryItemContext&&);
    HistoryItemContext& operator=(HistoryItemContext&&);

    std::string title;
    std::string url;
    std::string snippet;
    int64_t last_visit_time_ms = 0;
    ContextBlockProvenance provenance;

    base::DictValue ToValue() const;
  };

  // Structured memory L1 briefing context slot.
  struct MemoryBriefingContext {
    MemoryBriefingContext();
    ~MemoryBriefingContext();
    MemoryBriefingContext(const MemoryBriefingContext&);
    MemoryBriefingContext& operator=(const MemoryBriefingContext&);
    MemoryBriefingContext(MemoryBriefingContext&&);
    MemoryBriefingContext& operator=(MemoryBriefingContext&&);

    bool available = false;
    std::string status_message;  // Honest status/reason when unavailable
    std::string briefing_text;
    ContextBlockProvenance provenance;

    base::DictValue ToValue() const;
  };

  // Structured search source slot.
  struct SearchContextSlot {
    SearchContextSlot();
    ~SearchContextSlot();
    SearchContextSlot(const SearchContextSlot&);
    SearchContextSlot& operator=(const SearchContextSlot&);
    SearchContextSlot(SearchContextSlot&&);
    SearchContextSlot& operator=(SearchContextSlot&&);

    bool available = false;
    std::string query;
    std::string reason;  // Honest status/reason when unavailable
    std::vector<std::string> snippets;
    ContextBlockProvenance provenance;

    base::DictValue ToValue() const;
  };

  // Aggregated multi-source context gathered result.
  struct GatheredContextResult {
    GatheredContextResult();
    ~GatheredContextResult();
    GatheredContextResult(GatheredContextResult&&);
    GatheredContextResult& operator=(GatheredContextResult&&);

    bool success = false;
    size_t total_estimated_tokens = 0;
    size_t token_budget = 4000;
    bool truncated = false;

    // Structured multi-source slots
    PageContextResult page;
    std::vector<HistoryItemContext> history_items;
    MemoryBriefingContext memory;
    SearchContextSlot search;

    // Suggested fallback question when context is minimal (< threshold)
    std::string suggested_question;

    // Provenance per source
    ContextBlockProvenance page_provenance;

    std::vector<std::string> gather_warnings;

    base::DictValue ToValue() const;
  };

  struct GatherOptions {
    GatherOptions();
    ~GatherOptions();
    GatherOptions(const GatherOptions&);
    GatherOptions& operator=(const GatherOptions&);
    GatherOptions(GatherOptions&&);
    GatherOptions& operator=(GatherOptions&&);

    size_t max_total_tokens = 4000;
    std::string history_query;
    size_t max_history_results = 5;
    bool include_page = true;
    bool include_history = true;
    bool include_memory = true;
    bool include_search = true;
    int tab_id = -1;  // -1 for active tab
  };

  using PageContextCallback =
      base::OnceCallback<void(PageContextResult page_context)>;
  using GatherContextCallback =
      base::OnceCallback<void(GatheredContextResult gathered_context)>;
  using MemoryBriefingFetcher =
      base::RepeatingCallback<std::optional<std::string>()>;
  using InteractionDelegate =
      base::RepeatingCallback<void(const std::string& question,
                                   const std::string& interaction_id)>;

  static void SetMemoryBriefingFetcherForTesting(
      MemoryBriefingFetcher fetcher);
  static void ResetMemoryBriefingFetcherForTesting();

  explicit MahoAiPageContextExtractor(
      Browser* browser,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>(),
      InteractionDelegate interaction_delegate = InteractionDelegate());
  MahoAiPageContextExtractor(const MahoAiPageContextExtractor&) = delete;
  MahoAiPageContextExtractor& operator=(
      const MahoAiPageContextExtractor&) = delete;
  ~MahoAiPageContextExtractor();

  void SetInteractionDelegate(InteractionDelegate delegate) {
    interaction_delegate_ = std::move(delegate);
  }
  const InteractionDelegate& interaction_delegate() const {
    return interaction_delegate_;
  }

  // Legacy page extraction methods
  void GetPageContext(PageContextCallback callback);
  void GetPageContextForTabId(int tab_id, PageContextCallback callback);

  // Multi-source proactive context gatherer
  void GatherContext(GatherOptions options, GatherContextCallback callback);

  // Token estimation helper: ~4 chars per token for Latin/ASCII text.
  static size_t EstimateTokens(std::string_view text);

  // Budget cap enforcement with largest-first priority: Page > History > Memory.
  static void ApplyBudgetCap(GatheredContextResult* result, size_t max_tokens);

 private:
  friend class MahoAiPageContextExtractorTest;
  FRIEND_TEST_ALL_PREFIXES(
      MahoAiPageContextExtractorTest,
      MinimalContextSurfacesSuggestedQuestionViaInteractionDelegate);

  static std::string StatusToString(PageContextResult::Status status);
  static std::vector<std::string> ExtractStringList(const base::Value* value);
  static PageContextResult BuildResultFromJsValue(const std::string& title,
                                                  const std::string& url,
                                                  base::Value result);

  void OnPageContextExtracted(const std::string& title,
                              const std::string& url,
                              PageContextCallback callback,
                              base::Value result);

  void OnGatherPageExtracted(GatherOptions options,
                             GatherContextCallback callback,
                             PageContextResult page_result);

  std::vector<HistoryItemContext> GatherHistoryViaRegistry(
      const std::string& query,
      size_t max_results);

  content::WebContents* GetActiveWebContents() const;
  content::WebContents* GetWebContentsForTabId(int tab_id) const;

  raw_ptr<Browser> browser_;
  base::RepeatingCallback<bool()> ai_gate_;
  InteractionDelegate interaction_delegate_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoAiPageContextExtractor> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_PAGE_CONTEXT_EXTRACTOR_H_
