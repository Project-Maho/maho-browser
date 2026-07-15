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

 private:
  MahoPrivateContextClass context_class_;
  base::WeakPtr<BrowserWindowInterface> browser_window_;
  base::WeakPtr<content::WebContents> web_contents_;
  raw_ptr<Profile> profile_ = nullptr;
  bool had_browser_window_ = false;
  bool had_web_contents_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
};

MahoPrivateContextClass ClassifyProfileForTesting(Profile* profile);

#endif  // MAHO_BROWSER_MAHO_PRIVATE_CONTEXT_POLICY_H_
