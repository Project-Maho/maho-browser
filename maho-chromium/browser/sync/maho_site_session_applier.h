// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_APPLIER_H_
#define MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_APPLIER_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

class Profile;

namespace maho {

struct SiteSessionApplyResult {
  std::string origin;
  size_t applied = 0;
  std::vector<std::string> failures;
};

class MahoSiteSessionApplier {
 public:
  using CompletionCallback = base::OnceCallback<void(SiteSessionApplyResult)>;

  explicit MahoSiteSessionApplier(Profile* profile);
  MahoSiteSessionApplier(const MahoSiteSessionApplier&) = delete;
  MahoSiteSessionApplier& operator=(const MahoSiteSessionApplier&) = delete;
  ~MahoSiteSessionApplier();

  void Apply(const std::string& record_json, CompletionCallback callback);
  void Cancel();

 private:
  void SetNextCookie(std::string origin,
                     std::vector<std::string> cookies,
                     size_t index,
                     SiteSessionApplyResult result,
                     CompletionCallback callback);
  void FinishApply(SiteSessionApplyResult result,
                   CompletionCallback callback);
  bool IsOriginOptedIn(const std::string& origin) const;
  void ApplyRecord(const std::string& record_json,
                   CompletionCallback callback);

  raw_ptr<Profile> profile_;
  mojo::Remote<network::mojom::CookieManager> cookie_manager_;
  bool pending_apply_ = false;
  std::string last_record_json_;
  base::WeakPtrFactory<MahoSiteSessionApplier> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_APPLIER_H_
