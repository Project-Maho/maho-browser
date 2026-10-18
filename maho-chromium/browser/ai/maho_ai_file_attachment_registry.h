// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_FILE_ATTACHMENT_REGISTRY_H_
#define MAHO_BROWSER_AI_MAHO_AI_FILE_ATTACHMENT_REGISTRY_H_

#include <map>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "base/supports_user_data.h"

class Profile;

namespace maho::ai {

class MahoAiFileAttachmentRegistry {
 public:
  static constexpr int64_t kMaxFileBytes = 10 * 1024 * 1024;
  static constexpr base::TimeDelta kTokenLifetime = base::Minutes(30);

  MahoAiFileAttachmentRegistry();
  ~MahoAiFileAttachmentRegistry();

  std::string IssueToken(const base::FilePath& selected_path);
  bool CanResolve(const std::string& token) const;
  std::optional<base::FilePath> Resolve(const std::string& token);
  bool HasToken(const std::string& token) const;
  bool IsValidToken(const std::string& token) const;

  // Page captures the browser itself took (e.g. the screenshot dialog's
  // "Ask Maho"). Only PNG bytes up to kMaxFileBytes are accepted; the token is
  // single-use and never handed to the renderer.
  std::string IssueCapturedImageToken(std::vector<uint8_t> png_bytes);
  std::optional<std::vector<uint8_t>> TakeCapturedImage(
      const std::string& token);

 private:
  struct Entry {
    base::FilePath path;
    base::Time expires_at;
  };
  struct ImageEntry {
    std::vector<uint8_t> png_bytes;
    base::Time expires_at;
  };
  std::map<std::string, Entry> entries_;
  std::map<std::string, ImageEntry> images_;
};

class MahoAiFileAttachmentRegistryUserData
    : public base::SupportsUserData::Data {
 public:
  MahoAiFileAttachmentRegistry registry;
};

MahoAiFileAttachmentRegistry* GetMahoAiFileAttachmentRegistry(Profile* profile);
bool IsAllowedAiFileAttachment(const base::FilePath& path);
bool IsPngImage(const std::vector<uint8_t>& bytes);

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AI_FILE_ATTACHMENT_REGISTRY_H_
