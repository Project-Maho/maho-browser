// Copyright 2026 Maho Browser. All rights reserved.

extern "C" {

bool MahoMailInitialize(const char* version_token, const char* profile_path) {
  return true;
}

bool MahoMailInjectKeys(const char* sqlcipher_key, const char* credential_key) {
  return true;
}

bool MahoMailStartSync(const char* account_id) {
  return true;
}

bool MahoMailStopSync() {
  return true;
}

bool MahoMailStartBackfill(const char* account_id) {
  return true;
}

}
