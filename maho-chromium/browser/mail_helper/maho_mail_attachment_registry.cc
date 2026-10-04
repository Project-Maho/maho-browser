// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_attachment_registry.h"

#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "crypto/hash.h"

namespace maho {
namespace {

constexpr char kInvalidSource[] = "attachment source is not trusted";
constexpr char kStageFailed[] = "attachment staging failed";
constexpr char kInvalidToken[] = "invalid attachment capability";
constexpr char kWrongProfile[] = "attachment capability profile mismatch";
constexpr char kExpired[] = "attachment capability expired";
constexpr char kMissingOrReplaced[] = "staged attachment changed";

bool IsStrictChild(const base::FilePath& root, const base::FilePath& path) {
  return !root.empty() && !path.empty() && root != path && root.IsParent(path);
}

}  // namespace

MailAttachmentMessageIdentity::MailAttachmentMessageIdentity() = default;

MailAttachmentMessageIdentity::MailAttachmentMessageIdentity(
    std::string account_id,
    int64_t email_uid,
    std::string folder_id,
    std::string part_id)
    : account_id(std::move(account_id)),
      email_uid(email_uid),
      folder_id(std::move(folder_id)),
      part_id(std::move(part_id)) {}

MailAttachmentMessageIdentity::~MailAttachmentMessageIdentity() = default;

MailAttachmentMessageIdentity::MailAttachmentMessageIdentity(
    const MailAttachmentMessageIdentity&) = default;

MailAttachmentMessageIdentity& MailAttachmentMessageIdentity::operator=(
    const MailAttachmentMessageIdentity&) = default;

MailAttachmentMessageIdentity::MailAttachmentMessageIdentity(
    MailAttachmentMessageIdentity&&) = default;

MailAttachmentMessageIdentity& MailAttachmentMessageIdentity::operator=(
    MailAttachmentMessageIdentity&&) = default;

MahoMailAttachmentRegistry::ConsumedAttachment::ConsumedAttachment(
    base::FilePath path,
    base::File protection_file)
    : path_(std::move(path)), protection_file_(std::move(protection_file)) {}

MahoMailAttachmentRegistry::ConsumedAttachment::~ConsumedAttachment() {
  protection_file_.Close();
  if (!path_.empty()) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile), path_));
  }
}

base::File
MahoMailAttachmentRegistry::ConsumedAttachment::DuplicateProtectionFile()
    const {
  return protection_file_.Duplicate();
}

base::FilePath MahoMailAttachmentRegistry::ConsumedAttachment::TakePath() {
  return std::exchange(path_, base::FilePath());
}

MahoMailAttachmentRegistry::FileIdentity::FileIdentity() = default;

MahoMailAttachmentRegistry::FileIdentity::~FileIdentity() = default;

MahoMailAttachmentRegistry::FileIdentity::FileIdentity(
    const FileIdentity&) = default;

MahoMailAttachmentRegistry::FileIdentity&
MahoMailAttachmentRegistry::FileIdentity::operator=(
    const FileIdentity&) = default;

MahoMailAttachmentRegistry::FileIdentity::FileIdentity(
    FileIdentity&&) = default;

MahoMailAttachmentRegistry::FileIdentity&
MahoMailAttachmentRegistry::FileIdentity::operator=(
    FileIdentity&&) = default;

MahoMailAttachmentRegistry::Entry::Entry() = default;

MahoMailAttachmentRegistry::Entry::~Entry() = default;

MahoMailAttachmentRegistry::Entry::Entry(Entry&&) = default;

MahoMailAttachmentRegistry::Entry&
MahoMailAttachmentRegistry::Entry::operator=(Entry&&) = default;

MahoMailAttachmentRegistry::StageResult::StageResult() = default;

MahoMailAttachmentRegistry::StageResult::~StageResult() = default;

MahoMailAttachmentRegistry::StageResult::StageResult(
    StageResult&&) = default;

MahoMailAttachmentRegistry::StageResult&
MahoMailAttachmentRegistry::StageResult::operator=(
    StageResult&&) = default;

MahoMailAttachmentRegistry::ConsumeResult::ConsumeResult() = default;

MahoMailAttachmentRegistry::ConsumeResult::~ConsumeResult() = default;

MahoMailAttachmentRegistry::ConsumeResult::ConsumeResult(
    ConsumeResult&&) = default;

MahoMailAttachmentRegistry::ConsumeResult&
MahoMailAttachmentRegistry::ConsumeResult::operator=(
    ConsumeResult&&) = default;

MahoMailAttachmentRegistry::MahoMailAttachmentRegistry(
    std::string profile_identity,
    base::FilePath helper_download_root,
    base::FilePath staging_root,
    base::FilePath launch_root)
    : profile_identity_(std::move(profile_identity)),
      helper_download_root_(std::move(helper_download_root)),
      staging_root_(std::move(staging_root)),
      launch_root_(std::move(launch_root)),
      now_callback_(base::BindRepeating([] { return base::Time::Now(); })),
      token_callback_(base::BindRepeating([] {
        return base::Uuid::GenerateRandomV4().AsLowercaseString();
      })) {}

MahoMailAttachmentRegistry::~MahoMailAttachmentRegistry() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  RevokeAll();
}

void MahoMailAttachmentRegistry::StageDownloadedFile(
    const MailAttachmentMessageIdentity& message,
    const base::FilePath& source_path,
    const std::string& filename,
    ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DeleteExpiredEntries(Now());
  if (active_generation_ == 0) {
    std::move(callback).Run(false, std::string(), kInvalidToken);
    return;
  }
  const uint64_t generation = active_generation_;
  const uint64_t account_epoch = account_epochs_[message.account_id];
  const std::string token = NewToken();
  in_flight_tokens_.insert(token);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MahoMailAttachmentRegistry::StageOnWorker,
                     helper_download_root_, staging_root_, source_path,
                     filename, token),
      base::BindOnce(&MahoMailAttachmentRegistry::DispatchStageResult,
                     weak_ptr_factory_.GetWeakPtr(), message, generation,
                     account_epoch, token, std::move(callback)));
}

void MahoMailAttachmentRegistry::Consume(
    const std::string& token,
    const std::string& requesting_profile_identity,
    ConsumeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::Time now = Now();
  auto it = entries_.find(token);
  if (it == entries_.end()) {
    DeleteExpiredEntries(now);
    std::move(callback).Run(nullptr, kInvalidToken);
    return;
  }

  // Erase before any asynchronous work. A concurrent/reentrant second open
  // cannot observe this capability, including when validation later fails.
  Entry entry = std::move(it->second);
  entries_.erase(it);
  if (active_generation_ == 0 || entry.generation != active_generation_ ||
      entry.account_epoch != account_epochs_[entry.message.account_id]) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       entry.staged_path));
    std::move(callback).Run(nullptr, kInvalidToken);
    return;
  }
  if (entry.profile_identity != requesting_profile_identity) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       entry.staged_path));
    std::move(callback).Run(nullptr, kWrongProfile);
    return;
  }
  if (entry.expires_at <= now) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       entry.staged_path));
    std::move(callback).Run(nullptr, kExpired);
    return;
  }
  if (entry.message.account_id.empty() || entry.message.email_uid <= 0 ||
      entry.message.folder_id.empty() || entry.message.part_id.empty()) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       entry.staged_path));
    std::move(callback).Run(nullptr, kInvalidToken);
    return;
  }
  DeleteExpiredEntries(now);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MahoMailAttachmentRegistry::ValidateOnWorker,
                     staging_root_, launch_root_, token,
                     validated_handle_callback_for_testing_, std::move(entry)),
      base::BindOnce(&MahoMailAttachmentRegistry::DispatchConsumeResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoMailAttachmentRegistry::SaveToDownloads(
    const std::string& token,
    const std::string& requesting_profile_identity,
    const base::FilePath& downloads_directory,
    SaveCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto it = entries_.find(token);
  const base::FilePath filename =
      it == entries_.end() ? base::FilePath() : it->second.filename;
  Consume(
      token, requesting_profile_identity,
      base::BindOnce(
          [](base::FilePath directory, base::FilePath filename,
             SaveCallback callback,
             std::unique_ptr<ConsumedAttachment> attachment,
             std::string error) {
            if (!attachment) {
              std::move(callback).Run(false, std::move(error), std::string());
              return;
            }
            base::ThreadPool::PostTaskAndReplyWithResult(
                FROM_HERE,
                {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
                base::BindOnce(
                    [](base::FilePath directory, base::FilePath filename,
                       std::unique_ptr<ConsumedAttachment> attachment)
                        -> std::pair<base::FilePath, std::string> {
                      if (!base::CreateDirectory(directory)) {
                        return {{}, "could not create download directory"};
                      }
                      base::File source = attachment->DuplicateProtectionFile();
                      // FLAG_CREATE in CopyValidatedFile atomically reserves
                      // each candidate, including against concurrent saves.
                      for (uint32_t suffix = 0; suffix < 10000; ++suffix) {
                        const base::FilePath destination = directory.Append(
                            suffix == 0 ? filename
                                        : filename.InsertBeforeExtensionASCII(
                                              " (" + base::NumberToString(suffix) +
                                              ")"));
                        base::File output;
                        if (CopyValidatedFile(&source, destination, &output)) {
                          return {destination, {}};
                        }
                        if (!output.IsValid() &&
                            output.error_details() == base::File::FILE_ERROR_EXISTS) {
                          continue;
                        }
                        if (output.IsValid()) {
                          output.Close();
                          if (!base::DeleteFile(destination)) {
                            return {{}, "attachment save failed; partial file cleanup failed"};
                          }
                        }
                        return {{}, "attachment save failed"};
                      }
                      return {{}, "too many attachment filename collisions"};
                    },
                    std::move(directory), std::move(filename),
                    std::move(attachment)),
                base::BindOnce(
                    [](SaveCallback callback,
                       std::pair<base::FilePath, std::string> result) {
                      const bool ok = !result.first.empty();
                      std::move(callback).Run(ok, std::move(result.second),
                                              result.first.AsUTF8Unsafe());
                    },
                    std::move(callback)));
          },
          downloads_directory, filename, std::move(callback)));
}

// static
void MahoMailAttachmentRegistry::DeleteConsumedAttachment(
    std::unique_ptr<ConsumedAttachment> attachment,
    CleanupCallback callback) {
  if (!attachment) {
    std::move(callback).Run(false);
    return;
  }
  const base::FilePath path = attachment->TakePath();
  attachment.reset();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&base::DeleteFile, path), std::move(callback));
}

void MahoMailAttachmentRegistry::SetNowCallbackForTesting(
    NowCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  now_callback_ = std::move(callback);
}

void MahoMailAttachmentRegistry::SetTokenCallbackForTesting(
    TokenCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  token_callback_ = std::move(callback);
}

void MahoMailAttachmentRegistry::SetCapabilityLifetimeForTesting(
    base::TimeDelta lifetime) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  capability_lifetime_ = lifetime;
}

void MahoMailAttachmentRegistry::SetValidatedHandleCallbackForTesting(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  validated_handle_callback_for_testing_ = std::move(callback);
}

void MahoMailAttachmentRegistry::SetGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_generation_ == generation) {
    return;
  }
  RevokeAll();
  active_generation_ = generation;
}

void MahoMailAttachmentRegistry::RevokeAccount(
    const std::string& account_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++account_epochs_[account_id];
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.message.account_id != account_id) {
      ++it;
      continue;
    }
    base::FilePath path = it->second.staged_path;
    it = entries_.erase(it);
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile), path));
  }
}

void MahoMailAttachmentRegistry::RevokeAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_generation_ = 0;
  for (const auto& [token, entry] : entries_) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       entry.staged_path));
  }
  entries_.clear();
}

size_t MahoMailAttachmentRegistry::capability_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return entries_.size();
}

// static
MahoMailAttachmentRegistry::StageResult
MahoMailAttachmentRegistry::StageOnWorker(
    const base::FilePath& helper_download_root,
    const base::FilePath& staging_root,
    const base::FilePath& source_path,
    const std::string& filename,
    const std::string& token) {
  StageResult result;
  result.filename = base::FilePath::FromUTF8Unsafe(filename);
  if (source_path.empty() || !source_path.IsAbsolute() ||
      source_path.ReferencesParent()) {
    result.error = kInvalidSource;
    return result;
  }

  const base::FilePath canonical_root =
      base::MakeAbsoluteFilePath(helper_download_root);
  const base::FilePath canonical_source =
      base::MakeAbsoluteFilePath(source_path);
  if (!IsStrictChild(canonical_root, canonical_source)) {
    result.error = kInvalidSource;
    return result;
  }

  base::File::Info source_info;
  if (!base::GetFileInfo(canonical_source, &source_info) ||
      source_info.is_directory || source_info.is_symbolic_link) {
    result.error = kInvalidSource;
    return result;
  }
  if (!base::CreateDirectory(staging_root.DirName())) {
    result.error = kStageFailed;
    return result;
  }
  const base::FilePath canonical_staging_parent =
      base::MakeAbsoluteFilePath(staging_root.DirName());
  if (!base::CreateDirectory(staging_root)) {
    result.error = kStageFailed;
    return result;
  }

  result.staged_path = staging_root.AppendASCII(token);
  if (!base::CopyFile(canonical_source, result.staged_path)) {
    result.error = kStageFailed;
    result.staged_path.clear();
    return result;
  }

  const base::FilePath canonical_staging_root =
      base::MakeAbsoluteFilePath(staging_root);
  const base::FilePath canonical_staged =
      base::MakeAbsoluteFilePath(result.staged_path);
  base::File staged_file(canonical_staged,
                         base::File::FLAG_OPEN | base::File::FLAG_READ |
                             base::File::FLAG_WIN_SHARE_DELETE);
  if (!IsStrictChild(canonical_staging_parent, canonical_staging_root) ||
      canonical_staging_root.BaseName() != staging_root.BaseName() ||
      !IsStrictChild(canonical_staging_root, canonical_staged) ||
      !ReadFileIdentity(&staged_file, &result.file_identity)) {
    base::DeleteFile(result.staged_path);
    result.error = kStageFailed;
    result.staged_path.clear();
    return result;
  }

  result.staged_path = canonical_staged;
  result.ok = true;
  return result;
}

// static
MahoMailAttachmentRegistry::ConsumeResult
MahoMailAttachmentRegistry::ValidateOnWorker(const base::FilePath& staging_root,
                                             const base::FilePath& launch_root,
                                             const std::string& token,
                                             base::RepeatingClosure
                                                 validated_handle_callback,
                                             Entry entry) {
  ConsumeResult result;
  result.account_id = entry.message.account_id;
  result.generation = entry.generation;
  result.account_epoch = entry.account_epoch;
  const base::FilePath canonical_root = base::MakeAbsoluteFilePath(staging_root);
  const base::FilePath canonical_path =
      base::MakeAbsoluteFilePath(entry.staged_path);
  base::File staged_file(canonical_path,
                         base::File::FLAG_OPEN | base::File::FLAG_READ |
                             base::File::FLAG_WIN_SHARE_DELETE);
  FileIdentity actual_identity;
  if (!IsStrictChild(canonical_root, canonical_path) ||
      canonical_path != entry.staged_path ||
      !ReadFileIdentity(&staged_file, &actual_identity) ||
      actual_identity.size != entry.file_identity.size ||
      actual_identity.creation_time != entry.file_identity.creation_time ||
      actual_identity.last_modified != entry.file_identity.last_modified ||
      actual_identity.sha256 != entry.file_identity.sha256) {
    base::DeleteFile(entry.staged_path);
    result.error = kMissingOrReplaced;
    return result;
  }
  if (validated_handle_callback) {
    validated_handle_callback.Run();
  }
  if (!base::CreateDirectory(launch_root)) {
    base::DeleteFile(entry.staged_path);
    result.error = kStageFailed;
    return result;
  }
  const base::FilePath canonical_launch_root =
      base::MakeAbsoluteFilePath(launch_root);
  // Keep the capability opaque while preserving the validated display name's
  // real extension. The token prefix isolates otherwise identical filenames.
  result.launch_path = launch_root.Append(
      base::FilePath::FromUTF8Unsafe(token + "-" +
                                    entry.filename.AsUTF8Unsafe()));
  const base::FilePath canonical_launch_path =
      base::MakeAbsoluteFilePath(result.launch_path.DirName()).Append(
          result.launch_path.BaseName());
  if (canonical_launch_root != base::MakeAbsoluteFilePath(launch_root) ||
      !IsStrictChild(canonical_launch_root, canonical_launch_path) ||
      !CopyValidatedFile(&staged_file, canonical_launch_path,
                         &result.launch_file)) {
    base::DeleteFile(entry.staged_path);
    base::DeleteFile(result.launch_path);
    result.error = kStageFailed;
    return result;
  }
  base::DeleteFile(entry.staged_path);
  result.ok = true;
  result.staged_path = canonical_path;
  result.launch_path = canonical_launch_path;
  return result;
}

// static
bool MahoMailAttachmentRegistry::ReadFileIdentity(
    base::File* file,
    FileIdentity* identity) {
  if (!file || !file->IsValid()) {
    return false;
  }
  base::File::Info info;
  if (!file->GetInfo(&info) || info.is_directory || info.is_symbolic_link) {
    return false;
  }
  std::vector<uint8_t> digest(crypto::hash::kSha256Size);
  if (!crypto::hash::HashFile(crypto::hash::kSha256, file, digest)) {
    return false;
  }
  identity->size = info.size;
  identity->creation_time = info.creation_time;
  identity->last_modified = info.last_modified;
  identity->sha256.assign(reinterpret_cast<const char*>(digest.data()),
                          digest.size());
  return true;
}

// static
bool MahoMailAttachmentRegistry::CopyValidatedFile(
    base::File* source,
    const base::FilePath& destination,
    base::File* protection_file) {
  if (!source || !source->IsValid() || source->Seek(base::File::FROM_BEGIN, 0) < 0) {
    return false;
  }
  *protection_file = base::File(
      destination, base::File::FLAG_CREATE | base::File::FLAG_READ |
                       base::File::FLAG_WRITE |
                       base::File::FLAG_WIN_EXCLUSIVE_WRITE);
  if (!protection_file->IsValid()) {
    return false;
  }
  std::vector<uint8_t> buffer(64 * 1024);
  for (;;) {
    const std::optional<size_t> read = source->ReadAtCurrentPos(buffer);
    if (!read.has_value()) {
      return false;
    }
    if (*read == 0) {
      return true;
    }
    if (protection_file->WriteAtCurrentPos(base::span(buffer).first(*read)) !=
        read) {
      return false;
    }
  }
}

void MahoMailAttachmentRegistry::DispatchStageResult(
    base::WeakPtr<MahoMailAttachmentRegistry> registry,
    MailAttachmentMessageIdentity message,
    uint64_t generation,
    uint64_t account_epoch,
    std::string token,
    ResultCallback callback,
    StageResult result) {
  if (!registry) {
    if (result.ok) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                         result.staged_path));
    }
    std::move(callback).Run(false, std::string(), kInvalidToken);
    return;
  }
  registry->OnStageComplete(std::move(message), generation, account_epoch,
                            std::move(token), std::move(callback),
                            std::move(result));
}

void MahoMailAttachmentRegistry::DispatchConsumeResult(
    base::WeakPtr<MahoMailAttachmentRegistry> registry,
    ConsumeCallback callback,
    ConsumeResult result) {
  if (!registry) {
    if (result.ok) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                         result.launch_path));
    }
    std::move(callback).Run(nullptr, kInvalidToken);
    return;
  }
  if (result.generation == 0 ||
      result.generation != registry->active_generation_ ||
      result.account_epoch !=
          registry->account_epochs_[result.account_id]) {
    if (result.ok) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                         result.launch_path));
    }
    std::move(callback).Run(nullptr, kInvalidToken);
    return;
  }
  registry->OnConsumeComplete(std::move(callback), std::move(result));
}

void MahoMailAttachmentRegistry::OnStageComplete(
    MailAttachmentMessageIdentity message,
    uint64_t generation,
    uint64_t account_epoch,
    std::string token,
    ResultCallback callback,
    StageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  in_flight_tokens_.erase(token);
  if (!result.ok) {
    std::move(callback).Run(false, std::string(), std::move(result.error));
    return;
  }
  if (generation == 0 || generation != active_generation_ ||
      account_epoch != account_epochs_[message.account_id]) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       result.staged_path));
    std::move(callback).Run(false, std::string(), kInvalidToken);
    return;
  }
  if (entries_.contains(token)) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                       result.staged_path));
    std::move(callback).Run(false, std::string(), kInvalidToken);
    return;
  }
  Entry entry;
  entry.profile_identity = profile_identity_;
  entry.message = std::move(message);
  entry.filename = std::move(result.filename);
  entry.staged_path = std::move(result.staged_path);
  entry.expires_at = Now() + capability_lifetime_;
  entry.generation = generation;
  entry.account_epoch = account_epoch;
  entry.file_identity = std::move(result.file_identity);
  entries_.emplace(token, std::move(entry));
  std::move(callback).Run(true, std::move(token), std::string());
}

void MahoMailAttachmentRegistry::OnConsumeComplete(ConsumeCallback callback,
                                                   ConsumeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result.ok) {
    std::move(callback).Run(nullptr, std::move(result.error));
    return;
  }
  std::move(callback).Run(
      std::make_unique<ConsumedAttachment>(std::move(result.launch_path),
                                           std::move(result.launch_file)),
      std::string());
}

void MahoMailAttachmentRegistry::DeleteExpiredEntries(base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.expires_at > now) {
      ++it;
      continue;
    }
    base::FilePath path = it->second.staged_path;
    it = entries_.erase(it);
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile), path));
  }
}

base::Time MahoMailAttachmentRegistry::Now() const {
  return now_callback_.Run();
}

std::string MahoMailAttachmentRegistry::NewToken() {
  std::string token = token_callback_.Run();
  while (token.empty() || entries_.contains(token) ||
         in_flight_tokens_.contains(token)) {
    token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  return token;
}

}  // namespace maho
