// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_

#include <string>
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"

namespace maho {

class MahoMailHelperLauncher {
 public:
  MahoMailHelperLauncher();
  ~MahoMailHelperLauncher();

  MahoMailHelperLauncher(const MahoMailHelperLauncher&) = delete;
  MahoMailHelperLauncher& operator=(const MahoMailHelperLauncher&) = delete;

  void Launch(const std::string& version_token, const std::string& profile_path);
  void InjectDatabaseKeys(const std::string& sqlcipher_key, const std::string& credential_key);
  void Shutdown();

  mojom::MahoMailHelper* GetHelper() const { return helper_.get(); }

 private:
  void OnConnectionError();

  base::Process process_;
  mojo::Remote<mojom::MahoMailHelper> helper_;

  base::WeakPtrFactory<MahoMailHelperLauncher> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_
