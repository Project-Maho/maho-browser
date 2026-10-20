#include "maho/browser/ai/maho_browse_episode_observer.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "components/sessions/content/session_tab_helper.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "url/gurl.h"

namespace {

constexpr base::TimeDelta kMinimumDwell = base::Seconds(20);

bool IsEligibleUrl(const GURL& url) {
  return url.is_valid() && (url.SchemeIsHTTPOrHTTPS());
}

}  // namespace

MahoBrowseEpisodeDwellPolicy::EventResult
MahoBrowseEpisodeDwellPolicy::OnNavigation(const std::string& document_id,
                                          const std::string& url,
                                          bool is_off_the_record,
                                          base::TimeTicks now) {
  if (document_id == document_id_ && !document_id.empty()) {
    return EventResult::kIgnore;
  }
  document_id_ = document_id;
  eligible_ = !document_id.empty() && !is_off_the_record &&
              IsEligibleUrl(GURL(url));
  recorded_ = false;
  visible_since_ = base::TimeTicks();
  if (!eligible_) {
    visible_ = false;
    return EventResult::kIgnore;
  }
  visible_ = true;
  visible_since_ = now;
  return EventResult::kStartDwell;
}

MahoBrowseEpisodeDwellPolicy::EventResult
MahoBrowseEpisodeDwellPolicy::OnVisibilityChanged(bool visible,
                                                  base::TimeTicks now) {
  if (!eligible_ || recorded_ || visible_ == visible) {
    return EventResult::kIgnore;
  }
  visible_ = visible;
  if (!visible_) {
    visible_since_ = base::TimeTicks();
    return EventResult::kIgnore;
  }
  visible_since_ = now;
  return EventResult::kStartDwell;
}

MahoBrowseEpisodeDwellPolicy::EventResult
MahoBrowseEpisodeDwellPolicy::OnClock(base::TimeTicks now) {
  return MaybeRecord(now);
}

MahoBrowseEpisodeDwellPolicy::EventResult
MahoBrowseEpisodeDwellPolicy::MaybeRecord(base::TimeTicks now) {
  if (!eligible_ || !visible_ || recorded_ ||
      now - visible_since_ < kMinimumDwell) {
    return EventResult::kIgnore;
  }
  recorded_ = true;
  return EventResult::kRecord;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoBrowseEpisodeObserver);

MahoBrowseEpisodeObserver::MahoBrowseEpisodeObserver(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoBrowseEpisodeObserver>(*web_contents) {}

MahoBrowseEpisodeObserver::~MahoBrowseEpisodeObserver() = default;

void MahoBrowseEpisodeObserver::PrimaryPageChanged(content::Page& page) {
  dwell_timer_.Stop();
  page_context_extractor_.reset();
  weak_factory_.InvalidateWeakPtrs();
  content::WebContents* contents = web_contents();
  Profile* profile = contents ? Profile::FromBrowserContext(
                                    contents->GetBrowserContext())
                              : nullptr;
  const bool is_off_the_record = !profile || profile->IsOffTheRecord();
  if (!contents) {
    return;
  }
  const std::string document_id = std::to_string(++document_sequence_);
  const bool visible = contents->GetVisibility() == content::Visibility::VISIBLE;
  auto result = policy_.OnNavigation(document_id,
                                     contents->GetLastCommittedURL().spec(),
                                     is_off_the_record, base::TimeTicks::Now());
  if (!visible) {
    dwell_timer_.Stop();
    policy_.OnVisibilityChanged(false, base::TimeTicks::Now());
    return;
  }
  MaybeRecord(result);
}

void MahoBrowseEpisodeObserver::OnVisibilityChanged(
    content::Visibility visibility) {
  const bool visible = visibility == content::Visibility::VISIBLE;
  if (!visible) {
    dwell_timer_.Stop();
  }
  auto result = policy_.OnVisibilityChanged(visible, base::TimeTicks::Now());
  if (visible && policy_.OnClock(base::TimeTicks::Now()) ==
                     MahoBrowseEpisodeDwellPolicy::EventResult::kRecord) {
    result = MahoBrowseEpisodeDwellPolicy::EventResult::kRecord;
  }
  MaybeRecord(result);
}

void MahoBrowseEpisodeObserver::WebContentsDestroyed() {
  dwell_timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
}

void MahoBrowseEpisodeObserver::MaybeRecord(
    MahoBrowseEpisodeDwellPolicy::EventResult result) {
  if (result == MahoBrowseEpisodeDwellPolicy::EventResult::kStartDwell) {
    dwell_timer_.Start(FROM_HERE, base::Seconds(20), this,
                       &MahoBrowseEpisodeObserver::OnDwellTimer);
    return;
  }
  if (result == MahoBrowseEpisodeDwellPolicy::EventResult::kRecord)
    OnDwellTimer();
}

void MahoBrowseEpisodeObserver::OnDwellTimer() {
  if (policy_.OnClock(base::TimeTicks::Now()) !=
      MahoBrowseEpisodeDwellPolicy::EventResult::kRecord) {
    return;
  }
  content::WebContents* contents = web_contents();
  if (!contents || !contents->GetBrowserContext()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents));
  const std::string title = base::UTF16ToUTF8(contents->GetTitle());
  const std::string url = contents->GetLastCommittedURL().spec();
  if (!browser || !MahoIsCapabilityAllowed(profile, MahoPrivateCapability::kAI)) {
    RecordEpisode(title, url, std::string());
    return;
  }
  page_context_extractor_ =
      std::make_unique<MahoAiPageContextExtractor>(
          browser, base::BindRepeating(
                       [](Profile* profile) {
                         return MahoIsCapabilityAllowed(
                             profile, MahoPrivateCapability::kAI);
                       },
                       profile));
  const SessionID tab_id =
      sessions::SessionTabHelper::IdForTab(contents);
  if (tab_id.id() < 0) {
    RecordEpisode(title, url, std::string());
    return;
  }
  page_context_extractor_->GetPageContextForTabId(
      tab_id.id(),
      base::BindOnce(&MahoBrowseEpisodeObserver::OnPageContextExtracted,
                     weak_factory_.GetWeakPtr(), title, url));
}

void MahoBrowseEpisodeObserver::OnPageContextExtracted(
    std::string title,
    std::string url,
    MahoAiPageContextExtractor::PageContextResult page_context) {
  if (!web_contents())
    return;
  content::WebContents* contents = web_contents();
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord() ||
      !MahoIsCapabilityAllowed(profile, MahoPrivateCapability::kAI)) {
    return;
  }
  const std::string summary = page_context.HasUsableContent()
                                  ? page_context.main_text.substr(0, 20000)
                                  : std::string();
  if (title.empty() && !page_context.title.empty())
    title = page_context.title;
  RecordEpisode(std::move(title), std::move(url), summary);
}

void MahoBrowseEpisodeObserver::RecordEpisode(std::string title,
                                             std::string url,
                                             std::string summary) {
  base::DictValue episode;
  episode.Set("url", url);
  episode.Set("title", std::move(title));
  episode.Set("summary", summary);
  episode.Set("is_private_context", false);
  std::optional<std::string> json = base::WriteJson(episode);
  if (!json) {
    return;
  }
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(
          [](std::string payload) {
            if (MahoCore* core = maho::GetCore())
              maho::core::RecordEpisodeJson(core, payload);
          },
          std::move(*json)));
}
