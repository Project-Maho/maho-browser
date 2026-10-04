// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_ATTACHMENT_REGISTRY_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_ATTACHMENT_REGISTRY_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

namespace maho {

struct MailAttachmentMessageIdentity {
  MailAttachmentMessageIdentity();
  MailAttachmentMessageIdentity(std::string account_id,
                                int64_t email_uid,
                                std::string folder_id,
                                std::string part_id);
  ~MailAttachmentMessageIdentity();
  MailAttachmentMessageIdentity(const MailAttachmentMessageIdentity&);
  MailAttachmentMessageIdentity& operator=(
      const MailAttachmentMessageIdentity&);
  MailAttachmentMessageIdentity(MailAttachmentMessageIdentity&&);
  MailAttachmentMessageIdentity& operator=(
      MailAttachmentMessageIdentity&&);

  std::string account_id;
  int64_t email_uid = 0;
  std::string folder_id;
  std::string part_id;
};

// Profile-owned registry for files copied from the helper's download cache
// into a browser-managed staging root. Renderers see only random, one-use
// tokens; paths and file identity remain in the browser process.
class MahoMailAttachmentRegistry {
 public:
  using ResultCallback =
      base::OnceCallback<void(bool, std::string, std::string)>;
  using NowCallback = base::RepeatingCallback<base::Time()>;
  using TokenCallback = base::RepeatingCallback<std::string()>;
  using CleanupCallback = base::OnceCallback<void(bool)>;

  class ConsumedAttachment {
   public:
    ConsumedAttachment(base::FilePath path, base::File protection_file);
    ~ConsumedAttachment();

    ConsumedAttachment(const ConsumedAttachment&) = delete;
    ConsumedAttachment& operator=(const ConsumedAttachment&) = delete;

    const base::FilePath& path() const { return path_; }
    // Returns another handle to the validated launch inode. POSIX launchers
    // must hand this descriptor (or an inode-bound reference derived from it)
    // to the desktop instead of reopening |path()|.
    base::File DuplicateProtectionFile() const;

   private:
    friend class MahoMailAttachmentRegistry;
    base::FilePath TakePath();

    base::FilePath path_;
    // On Windows this handle denies write and delete/rename sharing
    // (opened with FLAG_WIN_EXCLUSIVE_WRITE and without FLAG_WIN_SHARE_DELETE)
    // until OpenItem's completion callback. POSIX launchers bind their platform
    // request to this validated inode instead of reopening |path_|.
    base::File protection_file_;
  };

  using ConsumeCallback = base::OnceCallback<void(
      std::unique_ptr<ConsumedAttachment>, std::string)>;

  MahoMailAttachmentRegistry(std::string profile_identity,
                             base::FilePath helper_download_root,
                             base::FilePath staging_root,
                             base::FilePath launch_root);
  ~MahoMailAttachmentRegistry();

  MahoMailAttachmentRegistry(const MahoMailAttachmentRegistry&) = delete;
  MahoMailAttachmentRegistry& operator=(
      const MahoMailAttachmentRegistry&) = delete;

  // Copies a helper-issued file into profile-local staging and returns an
  // opaque capability token. The source must canonicalize beneath the trusted
  // helper download root. |filename| is the single-component display name
  // validated by the service, independent of the helper cache basename.
  void StageDownloadedFile(const MailAttachmentMessageIdentity& message,
                           const base::FilePath& source_path,
                           const std::string& filename,
                           ResultCallback callback);

  // Atomically consumes a token, validates an open file handle, and copies
  // from that handle into the browser-only launch root. The returned object
  // owns cleanup of that launch copy after the platform open completes.
  void Consume(const std::string& token,
               const std::string& requesting_profile_identity,
               ConsumeCallback callback);

  // Saves validated bytes to a browser-selected directory. Reply order matches
  // SaveAttachment's Mojo contract: success, error, saved path.
  using SaveCallback = base::OnceCallback<void(bool, std::string, std::string)>;
  void SaveToDownloads(const std::string& token,
                       const std::string& requesting_profile_identity,
                       const base::FilePath& downloads_directory,
                       SaveCallback callback);

  static void DeleteConsumedAttachment(
      std::unique_ptr<ConsumedAttachment> attachment,
      CleanupCallback callback);

  void SetGeneration(uint64_t generation);
  void RevokeAccount(const std::string& account_id);
  void RevokeAll();

  void SetNowCallbackForTesting(NowCallback callback);
  void SetTokenCallbackForTesting(TokenCallback callback);
  void SetCapabilityLifetimeForTesting(base::TimeDelta lifetime);
  void SetValidatedHandleCallbackForTesting(base::RepeatingClosure callback);
  size_t capability_count_for_testing() const;

 private:
  struct FileIdentity {
    FileIdentity();
    ~FileIdentity();
    FileIdentity(const FileIdentity&);
    FileIdentity& operator=(const FileIdentity&);
    FileIdentity(FileIdentity&&);
    FileIdentity& operator=(FileIdentity&&);

    int64_t size = 0;
    base::Time creation_time;
    base::Time last_modified;
    std::string sha256;
  };

  struct Entry {
    Entry();
    ~Entry();
    Entry(Entry&&);
    Entry& operator=(Entry&&);

    std::string profile_identity;
    MailAttachmentMessageIdentity message;
    base::FilePath filename;
    base::FilePath staged_path;
    base::Time expires_at;
    uint64_t generation = 0;
    uint64_t account_epoch = 0;
    FileIdentity file_identity;
  };

  struct StageResult {
    StageResult();
    ~StageResult();
    StageResult(StageResult&&);
    StageResult& operator=(StageResult&&);

    bool ok = false;
    std::string error;
    base::FilePath filename;
    base::FilePath staged_path;
    FileIdentity file_identity;
  };

  struct ConsumeResult {
    ConsumeResult();
    ~ConsumeResult();
    ConsumeResult(ConsumeResult&&);
    ConsumeResult& operator=(ConsumeResult&&);

    bool ok = false;
    std::string error;
    base::FilePath staged_path;
    base::FilePath launch_path;
    base::File launch_file;
    std::string account_id;
    uint64_t generation = 0;
    uint64_t account_epoch = 0;
  };

  static StageResult StageOnWorker(const base::FilePath& helper_download_root,
                                   const base::FilePath& staging_root,
                                   const base::FilePath& source_path,
                                   const std::string& filename,
                                   const std::string& token);
  static ConsumeResult ValidateOnWorker(const base::FilePath& staging_root,
                                        const base::FilePath& launch_root,
                                        const std::string& token,
                                        base::RepeatingClosure
                                            validated_handle_callback,
                                        Entry entry);
  static bool ReadFileIdentity(base::File* file,
                               FileIdentity* identity);
  static bool CopyValidatedFile(base::File* source,
                                const base::FilePath& destination,
                                base::File* protection_file);
  static void DispatchStageResult(
      base::WeakPtr<MahoMailAttachmentRegistry> registry,
      MailAttachmentMessageIdentity message,
      uint64_t generation,
      uint64_t account_epoch,
      std::string token,
      ResultCallback callback,
      StageResult result);
  static void DispatchConsumeResult(
      base::WeakPtr<MahoMailAttachmentRegistry> registry,
      ConsumeCallback callback,
      ConsumeResult result);

  void OnStageComplete(MailAttachmentMessageIdentity message,
                       uint64_t generation,
                       uint64_t account_epoch,
                       std::string token,
                       ResultCallback callback,
                       StageResult result);
  void OnConsumeComplete(ConsumeCallback callback, ConsumeResult result);
  void DeleteExpiredEntries(base::Time now);
  base::Time Now() const;
  std::string NewToken();

  const std::string profile_identity_;
  const base::FilePath helper_download_root_;
  const base::FilePath staging_root_;
  const base::FilePath launch_root_;
  base::TimeDelta capability_lifetime_ = base::Minutes(10);
  NowCallback now_callback_;
  TokenCallback token_callback_;
  base::RepeatingClosure validated_handle_callback_for_testing_;
  std::map<std::string, Entry> entries_;
  std::set<std::string> in_flight_tokens_;
  std::map<std::string, uint64_t> account_epochs_;
  uint64_t active_generation_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMailAttachmentRegistry> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_ATTACHMENT_REGISTRY_H_
