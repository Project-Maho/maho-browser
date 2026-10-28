// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_COLLECTOR_H_
#define MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_COLLECTOR_H_

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/cookies/canonical_cookie.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/mojom/dom_storage/storage_area.mojom.h"

class Profile;

namespace maho {

class MahoSiteSessionCollector {
 public:
  using CompletionCallback = base::OnceCallback<void(std::vector<std::string>)>;

  explicit MahoSiteSessionCollector(Profile* profile);
  MahoSiteSessionCollector(const MahoSiteSessionCollector&) = delete;
  MahoSiteSessionCollector& operator=(const MahoSiteSessionCollector&) = delete;
  ~MahoSiteSessionCollector();

  void Collect(CompletionCallback callback);
  void Cancel();

 private:
  void OnCookies(std::set<std::string> opted_origins,
                 CompletionCallback callback,
                 const std::vector<net::CanonicalCookie>& cookies);
  void CollectStorageOrigin(size_t index,
                            std::vector<std::string> origins,
                            std::vector<std::string> records,
                            CompletionCallback callback);
  void OnStorageCollected(size_t index,
                          std::vector<std::string> origins,
                          std::vector<std::string> records,
                          CompletionCallback callback,
                          std::vector<blink::mojom::KeyValuePtr> entries);

  raw_ptr<Profile> profile_;
  mojo::Remote<network::mojom::CookieManager> cookie_manager_;
  mojo::Remote<blink::mojom::StorageArea> storage_area_;
  base::WeakPtrFactory<MahoSiteSessionCollector> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_COLLECTOR_H_
