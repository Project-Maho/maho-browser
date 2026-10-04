// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_MODEL_LIST_FETCHER_H_
#define MAHO_BROWSER_AI_MAHO_MODEL_LIST_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/sequence_checker.h"
#include "components/os_crypt/async/common/encryptor.h"

class PrefService;
class MahoModelListFetcherTestHelper;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace maho::ai {

class MahoModelListFetcher {
 public:
  using ModelsCallback = base::OnceCallback<void(const std::vector<std::string>& models, bool from_cache)>;
  using RefreshedCallback = base::RepeatingCallback<void(const std::string& provider_id, const std::vector<std::string>& models)>;

  MahoModelListFetcher(PrefService* prefs,
                       scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
                       base::RepeatingCallback<bool()> ai_gate =
                           base::RepeatingCallback<bool()>());
  ~MahoModelListFetcher();

  MahoModelListFetcher(const MahoModelListFetcher&) = delete;
  MahoModelListFetcher& operator=(const MahoModelListFetcher&) = delete;

  // Returns cached list synchronously via callback if valid; triggers background refresh if stale (>1h).
  void FetchModels(const std::string& provider_id, ModelsCallback callback);

  // Force-refresh a specific provider (used by "Test connection" flow indirectly or user manual refresh).
  void RefreshModels(const std::string& provider_id, ModelsCallback callback);

  // Subscribe to background refresh events (Mojo push).
  void SetRefreshedCallback(RefreshedCallback callback);

  // Invalidate cache entry for a specific provider.
  void InvalidateCache(const std::string& provider_id);

  // Offline fallback models shared by selector surfaces that cannot initiate a
  // model-list fetch themselves.
  static std::vector<std::string> GetHardcodedFallback(
      const std::string& provider_id);

 private:
  friend class ::MahoModelListFetcherTestHelper;

  struct PendingFetch {
    PendingFetch();
    PendingFetch(std::string pid, ModelsCallback cb);
    PendingFetch(PendingFetch&&);
    PendingFetch& operator=(PendingFetch&&);
    ~PendingFetch();

    std::string provider_id;
    ModelsCallback callback;
  };

  void PerformFetch(const std::string& provider_id, ModelsCallback callback);
  void OnFetchComplete(const std::string& provider_id,
                       ModelsCallback callback,
                       std::unique_ptr<network::SimpleURLLoader> loader,
                       std::optional<std::string> response_body);

  // Cache read/write:
  std::optional<std::vector<std::string>> ReadCache(const std::string& provider_id, base::Time* fetched_at);
  void WriteCache(const std::string& provider_id, const std::vector<std::string>& models);

  void OnOsCryptReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  std::string DecryptKey(const std::string& encrypted_b64);
  std::vector<std::string> ParseOpenAICompatibleModels(const std::string& response_body);
  std::vector<std::string> ParseOllamaTags(const std::string& response_body);

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  RefreshedCallback refreshed_callback_;
  base::RepeatingCallback<bool()> ai_gate_;

  scoped_refptr<os_crypt_async::Encryptor> local_encryptor_;
  std::vector<PendingFetch> pending_fetches_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoModelListFetcher> weak_factory_{this};
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_MODEL_LIST_FETCHER_H_
