#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include "chrome/browser/profiles/profile.h"
#include "maho/browser/maho_private_context_policy.h"

bool MahoIsWebUIEnabled(content::BrowserContext* context) {
  if (!context) {
    return false;
  }
  Profile* profile = Profile::FromBrowserContext(context);
  MahoPrivateContextClass cls = MahoClassifyProfile(profile);
  return cls == MahoPrivateContextClass::kRegular;
}
