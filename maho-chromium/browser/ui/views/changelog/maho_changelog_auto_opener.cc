#include "maho/browser/ui/views/changelog/maho_changelog_auto_opener.h"

namespace maho {

MahoChangelogAutoOpener::MahoChangelogAutoOpener(Profile* profile)
    : observation_(this), profile_(profile->GetWeakPtr()) {}

MahoChangelogAutoOpener::~MahoChangelogAutoOpener() = default;

void MahoChangelogAutoOpener::OnBrowserCreated(
    BrowserWindowInterface* window) {
  if (handled_) {
    return;
  }
  if (MahoChangelogEvaluateAndOpenForBrowser(window)) {
    handled_ = true;
    observation_.Reset();
  }
}

}  // namespace maho
