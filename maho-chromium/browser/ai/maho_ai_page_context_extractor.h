#ifndef MAHO_BROWSER_AI_MAHO_AI_PAGE_CONTEXT_EXTRACTOR_H_
#define MAHO_BROWSER_AI_MAHO_AI_PAGE_CONTEXT_EXTRACTOR_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

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

  using PageContextCallback =
      base::OnceCallback<void(PageContextResult page_context)>;

  explicit MahoAiPageContextExtractor(
      Browser* browser,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>());
  MahoAiPageContextExtractor(const MahoAiPageContextExtractor&) = delete;
  MahoAiPageContextExtractor& operator=(
      const MahoAiPageContextExtractor&) = delete;
  ~MahoAiPageContextExtractor();

  void GetPageContext(PageContextCallback callback);

  void GetPageContextForTabId(int tab_id, PageContextCallback callback);

 private:
  static std::string StatusToString(PageContextResult::Status status);
  static std::vector<std::string> ExtractStringList(const base::Value* value);
  static PageContextResult BuildResultFromJsValue(const std::string& title,
                                                  const std::string& url,
                                                  base::Value result);

  void OnPageContextExtracted(const std::string& title,
                              const std::string& url,
                              PageContextCallback callback,
                              base::Value result);

  content::WebContents* GetActiveWebContents() const;
  content::WebContents* GetWebContentsForTabId(int tab_id) const;

  raw_ptr<Browser> browser_;
  base::RepeatingCallback<bool()> ai_gate_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoAiPageContextExtractor> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_PAGE_CONTEXT_EXTRACTOR_H_
