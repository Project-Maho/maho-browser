#ifndef MAHO_BROWSER_MAHO_PRIVATE_CONTEXT_POLICY_H_
#define MAHO_BROWSER_MAHO_PRIVATE_CONTEXT_POLICY_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

enum class MahoPrivateContextClass {
  kNull,
  kGuest,
  kSystem,
  kDevToolsOtr,
  kPrimaryIncognito,
  kOtherOtr,
  kRegular,
};

enum class MahoPrivateCapability {
  kWindowLocalTabs,
  kPrivateVisuals,
  kProcessGlobalWebUI,
  kSavedReadMutate,
  kPersistentSplit,
  kMahoSearchReadMutate,
  kPreview,
  kAI,
  kMCP,
  kExtensionSnapshot,
  kMahoDownloadMetadata,
};

class MahoPrivateContextToken {
 public:
  MahoPrivateContextToken(BrowserWindowInterface* browser_window,
                          content::WebContents* web_contents);
  ~MahoPrivateContextToken();

  bool Revalidate(MahoPrivateCapability capability);

  MahoPrivateContextClass context_class() const { return context_class_; }

  content::WebContents* web_contents() const { return web_contents_.get(); }

 private:
  MahoPrivateContextClass context_class_;
  base::WeakPtr<BrowserWindowInterface> browser_window_;
  base::WeakPtr<content::WebContents> web_contents_;
  raw_ptr<Profile> profile_ = nullptr;
  bool had_browser_window_ = false;
  bool had_web_contents_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
};

MahoPrivateContextClass MahoClassifyProfile(Profile* profile);

// Profile-layer capability check. Applies the SAME policy as
// MahoPrivateContextToken::Revalidate() (via MahoClassifyProfile +
// IsCapabilityAllowed) but without the window/WebContents liveness
// re-validation, for boundaries that operate at the Profile layer (services,
// bridges, extension/split APIs) where no live browser window exists. WebUI
// handlers that DO hold a window/WebContents should keep using the token.
bool MahoIsCapabilityAllowed(Profile* profile, MahoPrivateCapability capability);

#endif  // MAHO_BROWSER_MAHO_PRIVATE_CONTEXT_POLICY_H_
