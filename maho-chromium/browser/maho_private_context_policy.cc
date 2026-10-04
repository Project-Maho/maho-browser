#include "maho/browser/maho_private_context_policy.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

namespace {

MahoPrivateContextClass ClassifyProfile(Profile* profile) {
  if (!profile) {
    return MahoPrivateContextClass::kNull;
  }
  Profile* original = profile->GetOriginalProfile();
  if (profile->IsGuestSession() || (original && original->IsGuestSession())) {
    return MahoPrivateContextClass::kGuest;
  }
  if (profile->IsSystemProfile() || (original && original->IsSystemProfile())) {
    return MahoPrivateContextClass::kSystem;
  }
  if (profile->IsDevToolsOTRProfile()) {
    return MahoPrivateContextClass::kDevToolsOtr;
  }
  if (profile->IsIncognitoProfile() && profile->IsPrimaryOTRProfile()) {
    return MahoPrivateContextClass::kPrimaryIncognito;
  }
  if (profile->IsOffTheRecord()) {
    return MahoPrivateContextClass::kOtherOtr;
  }
  return MahoPrivateContextClass::kRegular;
}

Profile* GetProfileFromInputs(BrowserWindowInterface* browser_window,
                              content::WebContents* web_contents) {
  if (web_contents) {
    return Profile::FromBrowserContext(web_contents->GetBrowserContext());
  }
  if (browser_window) {
    return browser_window->GetProfile();
  }
  return nullptr;
}

bool IsCapabilityAllowed(MahoPrivateContextClass context_class,
                         MahoPrivateCapability capability) {
  if (context_class == MahoPrivateContextClass::kRegular) {
    return capability != MahoPrivateCapability::kPrivateVisuals;
  }
  if (context_class == MahoPrivateContextClass::kPrimaryIncognito) {
    return capability == MahoPrivateCapability::kWindowLocalTabs ||
           capability == MahoPrivateCapability::kPrivateVisuals;
  }
  return false;
}

}  // namespace

MahoPrivateContextToken::MahoPrivateContextToken(
    BrowserWindowInterface* browser_window,
    content::WebContents* web_contents) {
  had_browser_window_ = (browser_window != nullptr);
  had_web_contents_ = (web_contents != nullptr);

  browser_window_ = browser_window ? browser_window->GetWeakPtr() : nullptr;
  web_contents_ = web_contents ? web_contents->GetWeakPtr() : nullptr;

  Profile* profile = GetProfileFromInputs(browser_window, web_contents);
  profile_ = profile;
  context_class_ = ClassifyProfile(profile);
}

MahoPrivateContextToken::~MahoPrivateContextToken() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool MahoPrivateContextToken::Revalidate(MahoPrivateCapability capability) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (had_browser_window_ && !browser_window_) {
    return false;
  }
  if (had_web_contents_ && !web_contents_) {
    return false;
  }

  Profile* current_profile = nullptr;
  if (web_contents_) {
    current_profile = Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  } else if (browser_window_) {
    current_profile = browser_window_->GetProfile();
  }

  if (current_profile != profile_) {
    return false;
  }

  MahoPrivateContextClass current_class = ClassifyProfile(current_profile);
  if (current_class != context_class_) {
    return false;
  }

  if (browser_window_ && web_contents_) {
    TabStripModel* tab_strip = browser_window_->GetTabStripModel();
    if (!tab_strip || tab_strip->GetIndexOfWebContents(web_contents_.get()) == TabStripModel::kNoTab) {
      return false;
    }
  }

  return IsCapabilityAllowed(context_class_, capability);
}

MahoPrivateContextClass MahoClassifyProfile(Profile* profile) {
  return ClassifyProfile(profile);
}

bool MahoIsCapabilityAllowed(Profile* profile,
                             MahoPrivateCapability capability) {
  return IsCapabilityAllowed(ClassifyProfile(profile), capability);
}
