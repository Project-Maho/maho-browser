#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"

#include "base/functional/callback.h"

namespace maho {

void LaunchMahoMini(content::BrowserContext* /*context*/,
                    MahoMiniRequest /*request*/) {
  // DEFERRED on Windows and Linux for v1.
}

void DecideLinkDestination(
    const GURL& /*url*/,
    bool /*is_external*/,
    base::OnceCallback<void(LinkDestinationResult)> callback) {
  LinkDestinationResult default_result;
  std::move(callback).Run(default_result);
}

void RegisterGlobalShortcut(Profile* /*profile*/) {}

}  // namespace maho

// static
void MahoMiniWindow::Open(Browser* /*browser*/, const GURL& /*url*/) {
  // DEFERRED on Windows and Linux for v1.
  // See .omo/plans/cross-platform-completion-plan-2026-07-10.md B.2.
}

// static
void MahoMiniWindow::Open(Profile* /*profile*/, const GURL& /*url*/) {
  // DEFERRED on Windows and Linux for v1.
}

// static
void MahoMiniWindow::PromoteToTab(Browser* /*browser*/) {
  // DEFERRED on Windows and Linux for v1.
}
