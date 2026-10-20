#ifndef MAHO_BROWSER_AI_MAHO_BROWSE_EPISODE_OBSERVER_H_
#define MAHO_BROWSER_AI_MAHO_BROWSE_EPISODE_OBSERVER_H_

#include <memory>
#include <cstdint>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"

class MahoBrowseEpisodeDwellPolicy {
 public:
  enum class EventResult { kIgnore, kStartDwell, kRecord };

  EventResult OnNavigation(const std::string& document_id,
                           const std::string& url,
                           bool is_off_the_record,
                           base::TimeTicks now);
  EventResult OnVisibilityChanged(bool visible, base::TimeTicks now);
  EventResult OnClock(base::TimeTicks now);

 private:
  EventResult MaybeRecord(base::TimeTicks now);

  std::string document_id_;
  base::TimeTicks visible_since_;
  bool eligible_ = false;
  bool visible_ = false;
  bool recorded_ = false;
};

class MahoBrowseEpisodeObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoBrowseEpisodeObserver> {
 public:
  ~MahoBrowseEpisodeObserver() override;

  MahoBrowseEpisodeObserver(const MahoBrowseEpisodeObserver&) = delete;
  MahoBrowseEpisodeObserver& operator=(const MahoBrowseEpisodeObserver&) = delete;

  void PrimaryPageChanged(content::Page& page) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<MahoBrowseEpisodeObserver>;
  explicit MahoBrowseEpisodeObserver(content::WebContents* web_contents);

  void OnDwellTimer();
  void MaybeRecord(MahoBrowseEpisodeDwellPolicy::EventResult result);
  void RecordEpisode(std::string title,
                     std::string url,
                     std::string summary);
  void OnPageContextExtracted(
      std::string title,
      std::string url,
      MahoAiPageContextExtractor::PageContextResult page_context);

  MahoBrowseEpisodeDwellPolicy policy_;
  base::OneShotTimer dwell_timer_;
  std::unique_ptr<MahoAiPageContextExtractor> page_context_extractor_;
  uint64_t document_sequence_ = 0;
  base::WeakPtrFactory<MahoBrowseEpisodeObserver> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif
