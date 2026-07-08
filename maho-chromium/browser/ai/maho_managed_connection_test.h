// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_MANAGED_CONNECTION_TEST_H_
#define MAHO_BROWSER_AI_MAHO_MANAGED_CONNECTION_TEST_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"

class PrefService;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace os_crypt_async {
class Encryptor;
}  // namespace os_crypt_async

namespace maho::ai {

class MahoManagedConnectionTest {
 public:
  using TestCallback = base::OnceCallback<void(bool success, const std::string& message)>;

  MahoManagedConnectionTest(PrefService* prefs,
                             scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoManagedConnectionTest();

  MahoManagedConnectionTest(const MahoManagedConnectionTest&) = delete;
  MahoManagedConnectionTest& operator=(const MahoManagedConnectionTest&) = delete;

  void RunTest(const os_crypt_async::Encryptor* encryptor, TestCallback callback);

 private:
  void StartRequest(const std::string& access_token, TestCallback callback, bool is_retry);
  void OnRequestComplete(TestCallback callback,
                         bool is_retry,
                         std::unique_ptr<network::SimpleURLLoader> loader,
                         std::optional<std::string> response_body);
  void OnRefreshComplete(TestCallback callback,
                         bool success,
                         int http_status,
                         std::optional<maho::auth::RefreshedTokens> tokens);

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  raw_ptr<const os_crypt_async::Encryptor> encryptor_ = nullptr;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoManagedConnectionTest> weak_factory_{this};
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_MANAGED_CONNECTION_TEST_H_
