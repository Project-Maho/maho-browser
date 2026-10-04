// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_BROWSER_USER_DATA_H_
#define MAHO_BROWSER_UI_BROWSER_USER_DATA_H_

#include <map>
#include <memory>

#include "base/no_destructor.h"

class Browser;

// Custom BrowserUserData implementation since desktop Chromium doesn't inherit
// Browser from SupportsUserData. Provides per-Browser singleton ownership:
// the controller is destroyed when RemoveFromBrowser is called or (if the
// caller arranges it) on Browser teardown.
template <typename T>
class BrowserUserData {
 public:
  static void CreateForBrowser(Browser* browser) {
    if (!FromBrowser(browser)) {
      GetInstances()[browser] = std::unique_ptr<T>(new T(browser));
    }
  }

  static T* FromBrowser(Browser* browser) {
    auto& instances = GetInstances();
    auto it = instances.find(browser);
    return it != instances.end() ? it->second.get() : nullptr;
  }

  static T* GetOrCreateForBrowser(Browser* browser) {
    CreateForBrowser(browser);
    return FromBrowser(browser);
  }

  static void RemoveFromBrowser(Browser* browser) {
    GetInstances().erase(browser);
  }

 protected:
  explicit BrowserUserData(Browser* browser) {}
  virtual ~BrowserUserData() = default;

  static std::map<Browser*, std::unique_ptr<T>>& GetInstances() {
    static base::NoDestructor<std::map<Browser*, std::unique_ptr<T>>> instances;
    return *instances;
  }
};

#endif  // MAHO_BROWSER_UI_BROWSER_USER_DATA_H_
