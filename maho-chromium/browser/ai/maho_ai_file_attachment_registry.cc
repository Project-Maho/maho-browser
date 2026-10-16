// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_file_attachment_registry.h"

#include <optional>

#include "base/files/file_util.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "chrome/browser/profiles/profile.h"

namespace maho::ai {
namespace {

const void* const kRegistryUserDataKey = &kRegistryUserDataKey;

bool IsWithinSizeLimit(const base::FilePath& path) {
  const std::optional<int64_t> size = base::GetFileSize(path);
  return size.has_value() && *size >= 0 &&
         *size <= MahoAiFileAttachmentRegistry::kMaxFileBytes;
}

}  // namespace

MahoAiFileAttachmentRegistry::MahoAiFileAttachmentRegistry() = default;
MahoAiFileAttachmentRegistry::~MahoAiFileAttachmentRegistry() = default;

std::string MahoAiFileAttachmentRegistry::IssueToken(
    const base::FilePath& selected_path) {
  if (!IsAllowedAiFileAttachment(selected_path) ||
      !IsWithinSizeLimit(selected_path)) {
    return {};
  }
  const std::string token = base::UnguessableToken::Create().ToString();
  entries_[token] = {selected_path, base::Time::Now() + kTokenLifetime};
  return token;
}

std::optional<base::FilePath> MahoAiFileAttachmentRegistry::Resolve(
    const std::string& token) {
  auto it = entries_.find(token);
  if (it == entries_.end())
    return std::nullopt;
  Entry entry = std::move(it->second);
  entries_.erase(it);
  if (entry.expires_at <= base::Time::Now())
    return std::nullopt;
  if (!IsAllowedAiFileAttachment(entry.path) ||
      !IsWithinSizeLimit(entry.path)) {
    return std::nullopt;
  }
  return entry.path;
}

bool MahoAiFileAttachmentRegistry::HasToken(const std::string& token) const {
  auto it = entries_.find(token);
  return it != entries_.end() && it->second.expires_at > base::Time::Now();
}

bool MahoAiFileAttachmentRegistry::CanResolve(
    const std::string& token) const {
  auto it = entries_.find(token);
  if (it == entries_.end() || it->second.expires_at <= base::Time::Now())
    return false;
  return IsAllowedAiFileAttachment(it->second.path) &&
         IsWithinSizeLimit(it->second.path);
}

bool MahoAiFileAttachmentRegistry::IsValidToken(
    const std::string& token) const {
  return HasToken(token);
}

MahoAiFileAttachmentRegistry* GetMahoAiFileAttachmentRegistry(
    Profile* profile) {
  if (!profile)
    return nullptr;
  auto* data = static_cast<MahoAiFileAttachmentRegistryUserData*>(
      profile->GetUserData(kRegistryUserDataKey));
  if (!data) {
    auto owned = std::make_unique<MahoAiFileAttachmentRegistryUserData>();
    data = owned.get();
    profile->SetUserData(kRegistryUserDataKey, std::move(owned));
  }
  return &data->registry;
}

bool IsAllowedAiFileAttachment(const base::FilePath& path) {
  // Only text formats are supported; no verified PDF/image extraction path
  // produces text for ContextAttachment, so raw binary bytes must never pass.
  // MatchesExtension is case-insensitive.
  return path.MatchesExtension(FILE_PATH_LITERAL(".txt")) ||
         path.MatchesExtension(FILE_PATH_LITERAL(".md")) ||
         path.MatchesExtension(FILE_PATH_LITERAL(".csv")) ||
         path.MatchesExtension(FILE_PATH_LITERAL(".json"));
}

}  // namespace maho::ai
