// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/sequence_checker.h"
#include "components/keyed_service/core/keyed_service.h"

namespace base {
class FilePath;
}  // namespace base

namespace maho {

class MahoMailHelperLauncher;

namespace mojom {
class MahoMailHelper;
}  // namespace mojom

// Profile-keyed broker that owns the out-of-process mail helper launcher for a
// single regular profile and proxies the helper's asynchronous JSON read
// methods. The browser side never touches the mail database directly; the
// helper process remains its sole owner. Sequence-affine: every method must run
// on the sequence that created the service (the UI thread in production).
class MahoMailService : public KeyedService {
 public:
  // Sequenced reply for every read method: (ok, json_or_error). Matches the
  // MCP delegate's MailReadCallback so results forward without adaptation.
  using ReadCallback = base::OnceCallback<void(bool, std::string)>;

  MahoMailService();
  ~MahoMailService() override;

  MahoMailService(const MahoMailService&) = delete;
  MahoMailService& operator=(const MahoMailService&) = delete;

  // Lazily creates (if needed) and launches the helper for this profile. Idem-
  // potent: the launcher ignores a Launch call while a process is already live.
  // See MahoMailHelperLauncher::Launch for the argument contract.
  void EnsureHelperLaunched(const std::string& expected_version,
                            const std::string& profile_path,
                            const base::FilePath& crashpad_database);

  // Forwards HC3 / DB-decrypt keys to the launcher, which caches them (so a
  // post-crash respawn can re-inject) and injects into the live helper. Secrets
  // live only inside the launcher's cache; the service never copies them.
  void InjectDatabaseKeys(const std::string& sqlcipher_key,
                          const std::string& credential_key);

  // KeyedService:
  void Shutdown() override;

  // Asynchronous JSON read proxies. Each checks helper availability and the
  // crash-loop give-up state, then forwards to Mojo without blocking. When the
  // helper is unavailable the callback runs with (false, deterministic error).
  void ListAccounts(ReadCallback callback);
  void ListFolders(const std::string& account_id, ReadCallback callback);
  void ListEmails(const std::string& account_id,
                  const std::string& folder_id,
                  int64_t limit,
                  int64_t offset,
                  ReadCallback callback);
  void GetEmail(const std::string& email_id, ReadCallback callback);
  void SearchEmails(const std::string& query_json, ReadCallback callback);
  void ListThread(const std::string& account_id,
                  const std::string& message_id,
                  ReadCallback callback);

  // Test-only: installs `launcher` as this service's launcher, replacing any
  // existing one, so unit tests can drive the read proxies against a fake
  // helper without spawning a process via EnsureHelperLaunched().
  void SetLauncherForTesting(std::unique_ptr<MahoMailHelperLauncher> launcher);

 private:
  // Returns the bound helper interface, or nullptr when the helper is missing,
  // unbound, or the launcher has given up after exceeding its crash-loop cap.
  mojom::MahoMailHelper* GetHelperOrNull();

  SEQUENCE_CHECKER(sequence_checker_);
  std::unique_ptr<MahoMailHelperLauncher> launcher_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_
