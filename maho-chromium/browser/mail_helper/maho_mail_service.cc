// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_service.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/task/thread_pool.h"
#include "base/files/file_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "crypto/hash.h"
#include "crypto/random.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "maho/browser/mail_helper/maho_mail_dock_badge_mac.h"
#endif
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace maho {

MailBehaviorSnapshot::MailBehaviorSnapshot() = default;

MailBehaviorSnapshot::~MailBehaviorSnapshot() = default;

MailBehaviorSnapshot::MailBehaviorSnapshot(
    const MailBehaviorSnapshot& other)
    : version(other.version),
      revision(other.revision),
      desktop_notifications(other.desktop_notifications),
      notification_preview(other.notification_preview),
      unread_badge_enabled(other.unread_badge_enabled),
      muted_thread_ids(other.muted_thread_ids),
      values(other.values.Clone()) {}

MailBehaviorSnapshot& MailBehaviorSnapshot::operator=(
    const MailBehaviorSnapshot& other) {
  if (this != &other) {
    version = other.version;
    revision = other.revision;
    desktop_notifications = other.desktop_notifications;
    notification_preview = other.notification_preview;
    unread_badge_enabled = other.unread_badge_enabled;
    muted_thread_ids = other.muted_thread_ids;
    values = other.values.Clone();
  }
  return *this;
}

MailBehaviorSnapshot::MailBehaviorSnapshot(MailBehaviorSnapshot&&) = default;

MailBehaviorSnapshot& MailBehaviorSnapshot::operator=(
    MailBehaviorSnapshot&&) = default;

namespace {

MailNotificationPermissionStatusCallback&
GetMailNotificationPermissionStatusCallback() {
  static base::NoDestructor<MailNotificationPermissionStatusCallback> callback;
  return *callback;
}

MailNotificationPermissionRequestCallback&
GetMailNotificationPermissionRequestCallback() {
  static base::NoDestructor<MailNotificationPermissionRequestCallback> callback;
  return *callback;
}

}  // namespace

void SetMahoMailNotificationPermissionCallbacks(
    MailNotificationPermissionStatusCallback status_callback,
    MailNotificationPermissionRequestCallback request_callback) {
  GetMailNotificationPermissionStatusCallback() = std::move(status_callback);
  GetMailNotificationPermissionRequestCallback() = std::move(request_callback);
}

void GetMahoMailNotificationPermission(
    base::OnceCallback<void(MailOsNotificationPermission)> callback) {
  const auto& status_callback = GetMailNotificationPermissionStatusCallback();
  if (!status_callback) {
    std::move(callback).Run(MailOsNotificationPermission::kUnsupported);
    return;
  }
  status_callback.Run(std::move(callback));
}

void RequestMahoMailNotificationPermission(
    base::OnceCallback<void(MailOsNotificationPermission)> callback) {
  const auto& request_callback = GetMailNotificationPermissionRequestCallback();
  if (!request_callback) {
    std::move(callback).Run(MailOsNotificationPermission::kUnsupported);
    return;
  }
  request_callback.Run(std::move(callback));
}

int64_t ComputeInboxUnreadForBadge(const std::string& folders_json,
                                   std::string* out_inbox_folder_id) {
  if (out_inbox_folder_id) {
    out_inbox_folder_id->clear();
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(folders_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return 0;
  }
  int64_t inbox_unread = 0;
  for (const base::Value& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    const base::DictValue& dict = item.GetDict();
    const std::string* type = dict.FindString("folder_type");
    if (!type || *type != "Inbox") {
      continue;
    }
    inbox_unread += dict.FindInt("unread_count").value_or(0);
    if (out_inbox_folder_id && out_inbox_folder_id->empty()) {
      const std::string* id = dict.FindString("id");
      if (id) {
        *out_inbox_folder_id = *id;
      }
    }
  }
  return inbox_unread;
}

std::optional<bool> GetDesktopNotificationPreference(
    const std::string& behavior_prefs_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(behavior_prefs_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("desktop_notifications");
}

std::optional<bool> GetUnreadBadgePreference(
    const std::string& behavior_prefs_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(behavior_prefs_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("unread_badge_enabled");
}

std::optional<std::string> DecodeAppSettingRead(const std::string& result_json) {
  if (result_json.empty()) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_string()) {
    return std::nullopt;
  }
  return parsed->GetString();
}

std::optional<MailBehaviorSnapshot> ParseMailBehaviorSnapshot(
    const std::string& behavior_prefs_json,
    uint64_t fallback_revision) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(behavior_prefs_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  MailBehaviorSnapshot snapshot;
  snapshot.values = parsed->GetDict().Clone();
  snapshot.version = 1;
  snapshot.revision = static_cast<uint64_t>(
      std::max(0, snapshot.values.FindInt("revision").value_or(
                      static_cast<int>(fallback_revision))));
  snapshot.desktop_notifications =
      snapshot.values.FindBool("desktop_notifications").value_or(true);
  snapshot.unread_badge_enabled =
      snapshot.values.FindBool("unread_badge_enabled").value_or(true);

  const std::string* preview =
      snapshot.values.FindString("notification_preview");
  if (preview && *preview == "sender_only") {
    snapshot.notification_preview = MailNotificationPreview::kSenderOnly;
  } else if (preview && *preview == "generic") {
    snapshot.notification_preview = MailNotificationPreview::kGeneric;
  } else {
    snapshot.notification_preview = MailNotificationPreview::kSenderSubject;
  }

  if (const base::ListValue* muted =
          snapshot.values.FindList("muted_thread_ids")) {
    for (const base::Value& value : *muted) {
      if (value.is_string()) {
        snapshot.muted_thread_ids.push_back(value.GetString());
      }
    }
  }

  snapshot.values.Set("version", 1);
  snapshot.values.Set("revision", static_cast<int>(snapshot.revision));
  snapshot.values.Set("desktop_notifications",
                      snapshot.desktop_notifications);
  snapshot.values.Set(
      "notification_preview",
      snapshot.notification_preview == MailNotificationPreview::kSenderOnly
          ? "sender_only"
          : snapshot.notification_preview == MailNotificationPreview::kGeneric
                ? "generic"
                : "sender_subject");
  snapshot.values.Set("unread_badge_enabled",
                      snapshot.unread_badge_enabled);
  base::ListValue muted;
  for (const std::string& thread_id : snapshot.muted_thread_ids) {
    muted.Append(thread_id);
  }
  snapshot.values.Set("muted_thread_ids", std::move(muted));
  return snapshot;
}

std::string SerializeMailBehaviorSnapshot(const MailBehaviorSnapshot& snapshot) {
  std::string serialized;
  base::JSONWriter::Write(snapshot.values, &serialized);
  return serialized;
}

MailBehaviorUpdateResult ApplyMailBehaviorUpdate(
    const MailBehaviorSnapshot& current,
    uint64_t expected_revision,
    const std::string& key,
    const std::string& value_json) {
  MailBehaviorUpdateResult result;
  result.snapshot = current;
  if (current.revision != expected_revision) {
    result.status = MailBehaviorUpdateStatus::kConflict;
    return result;
  }
  if (key == "version" || key == "revision") {
    result.status = MailBehaviorUpdateStatus::kInvalid;
    return result;
  }

  std::optional<base::Value> value =
      base::JSONReader::Read(value_json, base::JSON_PARSE_RFC);
  if (!value || !(value->is_bool() || value->is_int() || value->is_double() ||
                  value->is_string() || value->is_list())) {
    result.status = MailBehaviorUpdateStatus::kInvalid;
    return result;
  }
  if (key == "notification_preview" &&
      (!value->is_string() ||
       (value->GetString() != "sender_subject" &&
        value->GetString() != "sender_only" &&
        value->GetString() != "generic"))) {
    result.status = MailBehaviorUpdateStatus::kInvalid;
    return result;
  }
  if ((key == "desktop_notifications" || key == "unread_badge_enabled") &&
      !value->is_bool()) {
    result.status = MailBehaviorUpdateStatus::kInvalid;
    return result;
  }

  result.snapshot.values.Set(key, std::move(*value));
  result.snapshot.values.Set("revision",
                             static_cast<int>(current.revision + 1));
  auto reparsed = ParseMailBehaviorSnapshot(
      SerializeMailBehaviorSnapshot(result.snapshot), current.revision + 1);
  if (!reparsed) {
    result.status = MailBehaviorUpdateStatus::kInvalid;
    result.snapshot = current;
    return result;
  }
  result.status = MailBehaviorUpdateStatus::kApplied;
  result.snapshot = std::move(*reparsed);
  return result;
}

MailOsNotificationPermission MapMailNotificationPermissionForTesting(
    bool platform_supported,
    bool permission_denied) {
  if (!platform_supported) {
    return MailOsNotificationPermission::kUnsupported;
  }
  return permission_denied ? MailOsNotificationPermission::kDenied
                           : MailOsNotificationPermission::kGranted;
}

namespace {

// Mail is gated behind the maho.mail.enabled pref (default false). While it is
// off, EnsureHelperLaunched is a no-op so the un-sandboxed mail helper never
// spawns and no remote MIME/IMAP parsing happens.

// Deterministic error returned when no helper can serve a read (never
// launched, disconnected, or given up after a crash loop).
constexpr char kMahoMailHelperUnavailable[] = "mail helper unavailable";

// Returned when a renderer-supplied identifier or file name contains path
// syntax. Deliberately non-revealing: it never echoes the rejected value.
constexpr char kMahoMailInvalidIdentifier[] = "invalid mail identifier";

// True when `value` is safe to embed in a filesystem path built by the helper.
// Mail identifiers (account/folder/part ids) are opaque tokens, so any path
// separator, parent reference, NUL, or absolute-path marker is rejected rather
// than sanitized - silently rewriting an id would address the wrong mailbox.
bool IsPathSafeIdentifier(const std::string& value) {
  if (value.empty() || value.size() > 512) {
    return false;
  }
  if (value.find('/') != std::string::npos ||
      value.find('\\') != std::string::npos ||
      value.find('\0') != std::string::npos ||
      value.find(':') != std::string::npos) {
    return false;
  }
  // Reject "." and ".." outright, plus any embedded parent reference.
  if (value == "." || value == ".." ||
      value.find("..") != std::string::npos) {
    return false;
  }
  return true;
}

// True when a renderer-supplied filesystem path is acceptable to hand to the
// helper. The path must be absolute and free of parent references; relative or
// "../" input would let the renderer redirect reads/writes elsewhere. This is a
// boundary check, not a permission check - the helper still confines the
// operation to the user-selected location.
[[maybe_unused]] bool IsSafeUserFilePath(const std::string& value) {
  if (value.empty() || value.size() > 4096 ||
      value.find('\0') != std::string::npos) {
    return false;
  }
  base::FilePath path = base::FilePath::FromUTF8Unsafe(value);
  return path.IsAbsolute() && !path.ReferencesParent();
}

// Same contract as above for a user-visible attachment file name. The name may
// contain dots (extensions) but must remain a single path component.
bool IsPathSafeFileName(const std::string& value) {
  // Leave room for the UUID plus separator in the native launch basename.
  if (value.empty() || value.size() > 218 ||
      value.back() == '.' || value.back() == ' ' ||
      value.find_first_of("/\\:<>\"|?*") != std::string::npos ||
      value.find("..") != std::string::npos) {
    return false;
  }
  for (unsigned char c : value) {
    if (c < 0x20 || c == 0x7f) {
      return false;
    }
  }
  // Windows device names remain reserved even when followed by an extension.
  std::string stem = value.substr(0, value.find('.'));
  while (!stem.empty() && stem.back() == ' ') {
    stem.pop_back();
  }
  for (char& c : stem) {
    if (c >= 'a' && c <= 'z') {
      c -= 'a' - 'A';
    }
  }
  if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
      stem == "CONIN$" || stem == "CONOUT$") {
    return false;
  }
  if ((stem.starts_with("COM") || stem.starts_with("LPT")) &&
      (stem.substr(3) == "\xc2\xb9" || stem.substr(3) == "\xc2\xb2" ||
       stem.substr(3) == "\xc2\xb3" ||
       (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9'))) {
    return false;
  }
  return true;
}

// Adapts a Mojo (bool, const std::string&) reply into the service's
// (bool, std::string) ReadCallback.
void ForwardReadReply(MahoMailService::ReadCallback callback,
                      bool ok,
                      const std::string& result_json) {
  std::move(callback).Run(ok, result_json);
}

// Wraps the outgoing Mojo reply so that if the helper drops the reply callback
// (disconnect, remote reset, or process shutdown) the caller still resolves
// exactly once with the deterministic unavailable error instead of never being
// invoked. Normal success/error replies forward unchanged through
// ForwardReadReply.
base::OnceCallback<void(bool, const std::string&)> WrapUntrackedReadReply(
    MahoMailService::ReadCallback callback) {
  return mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(&ForwardReadReply, std::move(callback)),
      /*ok=*/false, std::string(kMahoMailHelperUnavailable));
}

// BYOK pref keys. Canonical definitions live in
// maho/browser/ui/webui/maho_ai_prefs.h; duplicated here as string literals to
// avoid a GN dependency from the mail-helper broker onto the webui target.
// Keep in sync with that header.
constexpr char kByokOpenAIEncryptedB64[] = "maho.ai.byok.openai.encrypted_b64";
constexpr char kByokAnthropicEncryptedB64[] =
    "maho.ai.byok.anthropic.encrypted_b64";

// Mirrors the read side used by the AI subsystem (base64-decode, then OSCrypt
// DecryptString). Returns empty on any failure so a missing/undecryptable key
// simply means "no browser key".
std::string DecryptByokKey(const os_crypt_async::Encryptor& encryptor,
                           const std::string& encrypted_b64) {
  if (encrypted_b64.empty()) {
    return std::string();
  }
  std::string encrypted_bytes;
  if (!base::Base64Decode(encrypted_b64, &encrypted_bytes)) {
    return std::string();
  }
  std::string plaintext;
  if (encryptor.DecryptString(encrypted_bytes, &plaintext)) {
    return plaintext;
  }
  return std::string();
}

}  // namespace

MahoMailService::MahoMailService(
    Profile* profile,
    os_crypt_async::OSCryptAsync* os_crypt_async)
    : profile_(profile),
      launcher_factory_(base::BindRepeating(
          []() { return std::make_unique<MahoMailHelperLauncher>(); })) {
  if (profile_) {
    profile_identity_ = profile_->GetPath().AsUTF8Unsafe();
    const base::FilePath mail_root =
        profile_->GetPath().AppendASCII("MahoMail");
    attachment_registry_ = std::make_unique<MahoMailAttachmentRegistry>(
        profile_identity_, mail_root.AppendASCII("AttachmentCache"),
        mail_root.AppendASCII("AttachmentStaging"),
        profile_->GetPath().AppendASCII("MahoMailAttachmentLaunch"));
  }
  enabled_for_testing_ = profile_ == nullptr;
  if (profile_) {
    PrefService* prefs = profile_->GetPrefs();
    mail_pref_registrar_.Init(prefs);
    mail_pref_registrar_.Add(
        sidebar_prefs::kMahoMailEnabled,
        base::BindRepeating(&MahoMailService::OnMailEnabledPrefChanged,
                            weak_ptr_factory_.GetWeakPtr()));
#if BUILDFLAG(IS_MAC)
    SetMailDockBadge(
        prefs->GetInteger("maho.mail.unread_count"),
        sidebar_prefs::IsMahoMailEnabled(prefs) &&
            prefs->GetBoolean("maho.mail.badge_enabled"));
#endif
    lifecycle_state_ = sidebar_prefs::IsMahoMailEnabled(prefs)
                           ? LifecycleState::kStopped
                           : LifecycleState::kDisabled;
  }
  if (profile_ && os_crypt_async) {
    os_crypt_async->GetInstance(
        base::BindOnce(&MahoMailService::OnOsCryptReady,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

MahoMailService::~MahoMailService() = default;

// static
namespace {

// Mail keys normally live in the OS key store. This file is their home only
// after that store is proven unable to return what it stored.
// Identifies a key without storing it. The OS key store reports success while
// returning a different key than it was given, and the only other way to find
// out is that the mail database no longer opens - by which point the user's
// mail has already been quarantined.
std::string MailKeyDigest(const std::string& key) {
  return base::Base64Encode(crypto::hash::Sha256(key));
}

base::FilePath MailKeyFileFor(const base::FilePath& profile_path) {
  return profile_path.Append(FILE_PATH_LITERAL("MahoMail"))
      .Append(FILE_PATH_LITERAL("db_key"));
}

std::pair<std::string, std::string> ReadMailKeyFile(const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return {};
  }
  const std::vector<std::string> lines = base::SplitString(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (lines.size() < 2) {
    return {};
  }
  return {lines[0], lines[1]};
}

bool WriteMailKeyFile(const base::FilePath& path,
                      const std::string& sqlcipher_key,
                      const std::string& credential_key) {
  if (!base::CreateDirectory(path.DirName())) {
    return false;
  }
  if (!base::WriteFile(path,
                       base::StrCat({sqlcipher_key, "\n", credential_key, "\n"}))) {
    return false;
  }
#if BUILDFLAG(IS_POSIX)
  // The OS key store is unavailable here, so file permissions are the only
  // protection left for this key.
  return base::SetPosixFilePermissions(path, 0600);
#else
  return true;
#endif
}

}  // namespace

void MahoMailService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref("maho.mail.sqlcipher_key_encrypted", "");
  registry->RegisterBooleanPref("maho.mail.key_store_is_file", false);
  registry->RegisterStringPref("maho.mail.sqlcipher_key_digest", std::string());
  registry->RegisterStringPref("maho.mail.credential_key_encrypted", "");
  // Aggregate inbox unread count surfaced on the sidebar Mail icon badge.
  registry->RegisterIntegerPref("maho.mail.unread_count", 0);
  // When true, arrival of new inbox mail raises an OS notification.
  registry->RegisterBooleanPref("maho.mail.notifications_enabled", true);
  // When true, the aggregate unread count is shown on the sidebar Mail icon
  // and (macOS) the Dock tile badge.
  registry->RegisterBooleanPref("maho.mail.badge_enabled", true);
  registry->RegisterDictionaryPref("maho.mail.notification_watermarks");
  registry->RegisterListPref("maho.mail.notification_message_ids");
  registry->RegisterListPref("maho.mail.notification_muted_threads");
}

void MahoMailService::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  encryptor_ = std::move(encryptor);
  if (pending_launch_.has_value()) {
    PendingLaunch launch = std::move(*pending_launch_);
    pending_launch_.reset();
    EnsureHelperLaunched(launch.expected_version, launch.profile_path, launch.crashpad_database);
  }
}

void MahoMailService::EnsureHelperLaunched(
    const std::string& expected_version,
    const std::string& profile_path,
    const base::FilePath& crashpad_database) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  launch_inputs_ = PendingLaunch{expected_version, profile_path,
                                 crashpad_database};
  // Mail is an opt-in Beta: never spawn the helper until users enable the
  // canonical pref in Settings. This is the sole chokepoint that creates
  // launcher_ and calls Launch(), so returning here guarantees no helper
  // subprocess. Reads resolve with the deterministic "unavailable" error.
  if (!IsEnabled()) {
    lifecycle_state_ = LifecycleState::kDisabled;
    return;
  }
  if (lifecycle_state_ == LifecycleState::kDraining) {
    restart_after_drain_ = true;
    return;
  }
  if (lifecycle_state_ == LifecycleState::kStarting ||
      lifecycle_state_ == LifecycleState::kReady) {
    return;
  }

  std::string sqlcipher_key;
  std::string credential_key;

  if (!profile_) {
    // For tests, use test keys
    sqlcipher_key = "test-sqlcipher-key";
    credential_key = "dGVzdC1jcmVkZW50aWFsLWtleS1ub3QtZm9yLXByb2Q="; // base64 of "test-credential-key-not-for-prod"
  } else {
    if (!encryptor_) {
      pending_launch_ = PendingLaunch{expected_version, profile_path, crashpad_database};
      return;
    }

    PrefService* prefs = profile_->GetPrefs();
    // A key store that cannot hand back what it stored gives out a different
    // key on every launch, and the database created with the previous one can
    // never be opened again. Once that is proven the keys live in a
    // profile-local file, which stays authoritative from then on.
    if (prefs->GetBoolean("maho.mail.key_store_is_file")) {
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(&ReadMailKeyFile, MailKeyFileFor(profile_->GetPath())),
          base::BindOnce(&MahoMailService::OnMailKeysReadFromFile,
                         weak_ptr_factory_.GetWeakPtr()));
      return;
    }
    std::string sqlcipher_key_enc = prefs->GetString("maho.mail.sqlcipher_key_encrypted");
    std::string credential_key_enc = prefs->GetString("maho.mail.credential_key_encrypted");

    if (sqlcipher_key_enc.empty() || credential_key_enc.empty()) {
      // Generate new keys
      std::string raw_sqlcipher = base::Base64Encode(crypto::RandBytesAsVector(32));
      std::string raw_credential = base::Base64Encode(crypto::RandBytesAsVector(32));

      std::string cipher_sqlcipher;
      std::string cipher_credential;
      if (!encryptor_->EncryptString(raw_sqlcipher, &cipher_sqlcipher) ||
          !encryptor_->EncryptString(raw_credential, &cipher_credential)) {
        LOG(ERROR) << "[MahoMailService] Failed to encrypt keys!";
        return;
      }

      prefs->SetString("maho.mail.sqlcipher_key_encrypted", base::Base64Encode(cipher_sqlcipher));
      prefs->SetString("maho.mail.credential_key_encrypted", base::Base64Encode(cipher_credential));
      prefs->SetString("maho.mail.sqlcipher_key_digest",
                       MailKeyDigest(raw_sqlcipher));

      sqlcipher_key_ = std::move(raw_sqlcipher);
      credential_key_ = std::move(raw_credential);

      // Prove the store round-trips before a database is created with this
      // key; OSCrypt reports no error when it decrypts to something else.
      std::string stored_cipher;
      std::string verified_key;
      base::Base64Decode(prefs->GetString("maho.mail.sqlcipher_key_encrypted"),
                         &stored_cipher);
      if (!encryptor_->DecryptString(stored_cipher, &verified_key) ||
          verified_key != sqlcipher_key_) {
        LOG(ERROR) << "[MahoMailService] OS key store does not return the key "
                      "it stored; keeping mail keys in a profile-local file.";
        prefs->SetBoolean("maho.mail.key_store_is_file", true);
        prefs->CommitPendingWrite();
        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
            base::BindOnce(&WriteMailKeyFile, MailKeyFileFor(profile_->GetPath()),
                           sqlcipher_key_, credential_key_),
            base::BindOnce(&MahoMailService::OnMailKeyFileWritten,
                           weak_ptr_factory_.GetWeakPtr()));
        return;
      }

      // Pref writes are flushed lazily. The helper creates the SQLCipher
      // database with this key as soon as it starts, so a process death before
      // that flush leaves a database encrypted with a key that exists nowhere
      // any more. Start the helper only once the key has reached disk;
      // StartGeneration() re-checks its own preconditions.
      prefs->CommitPendingWrite(base::BindOnce(
          &MahoMailService::StartGeneration, weak_ptr_factory_.GetWeakPtr()));
      return;
    } else {
      // Decrypt keys
      std::string raw_sqlcipher_enc;
      std::string raw_credential_enc;
      base::Base64Decode(sqlcipher_key_enc, &raw_sqlcipher_enc);
      base::Base64Decode(credential_key_enc, &raw_credential_enc);

      if (!encryptor_->DecryptString(raw_sqlcipher_enc, &sqlcipher_key) ||
          !encryptor_->DecryptString(raw_credential_enc, &credential_key)) {
        LOG(ERROR) << "[MahoMailService] Failed to decrypt keys!";
        return;
      }

      const std::string stored_digest =
          prefs->GetString("maho.mail.sqlcipher_key_digest");
      if (stored_digest.empty()) {
        // Written by a build that predates the digest; adopt the key in hand.
        prefs->SetString("maho.mail.sqlcipher_key_digest",
                         MailKeyDigest(sqlcipher_key));
      } else if (stored_digest != MailKeyDigest(sqlcipher_key)) {
        LOG(ERROR) << "[MahoMailService] OS key store returned a different key "
                      "than it was given; the existing mail database can no "
                      "longer be opened. Moving the keys to a profile-local "
                      "file so this cannot repeat.";
        sqlcipher_key = base::Base64Encode(crypto::RandBytesAsVector(32));
        credential_key = base::Base64Encode(crypto::RandBytesAsVector(32));
        sqlcipher_key_ = sqlcipher_key;
        credential_key_ = credential_key;
        prefs->SetBoolean("maho.mail.key_store_is_file", true);
        prefs->SetString("maho.mail.sqlcipher_key_digest",
                         MailKeyDigest(sqlcipher_key));
        prefs->CommitPendingWrite();
        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
            base::BindOnce(&WriteMailKeyFile, MailKeyFileFor(profile_->GetPath()),
                           sqlcipher_key_, credential_key_),
            base::BindOnce(&MahoMailService::OnMailKeyFileWritten,
                           weak_ptr_factory_.GetWeakPtr()));
        return;
      }
    }
  }

  sqlcipher_key_ = std::move(sqlcipher_key);
  credential_key_ = std::move(credential_key);
  StartGeneration();
}

void MahoMailService::InjectDatabaseKeys(const std::string& sqlcipher_key,
                                         const std::string& credential_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!launcher_) {
    return;
  }
  launcher_->InjectDatabaseKeys(sqlcipher_key, credential_key);
}

void MahoMailService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mail_pref_registrar_.RemoveAll();
  pending_launch_.reset();
  shutdown_requested_ = true;
  restart_after_drain_ = false;
  ClearUnreadState();
  BeginDrain(LifecycleState::kStopped);
}

void MahoMailService::OnMailEnabledPrefChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsEnabled()) {
    if (lifecycle_state_ == LifecycleState::kDisabled ||
        lifecycle_state_ == LifecycleState::kStopped ||
        lifecycle_state_ == LifecycleState::kFailed) {
      if (launch_inputs_) {
        PendingLaunch launch = *launch_inputs_;
        EnsureHelperLaunched(launch.expected_version, launch.profile_path,
                             launch.crashpad_database);
      }
    } else if (lifecycle_state_ == LifecycleState::kDraining) {
      restart_after_drain_ = true;
    }
    return;
  }

  pending_launch_.reset();
  restart_after_drain_ = false;
  ClearUnreadState();
  BeginDrain(LifecycleState::kDisabled);
}

void MahoMailService::MarkRead(const std::string& email_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->MarkRead(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::MarkUnread(const std::string& email_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->MarkUnread(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ToggleStar(const std::string& email_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ToggleStar(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteEmail(const std::string& email_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteEmail(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::MoveEmail(const std::string& email_id, const std::string& target_folder_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->MoveEmail(email_id, target_folder_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::BatchMarkRead(const std::string& request_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->BatchMarkRead(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::BatchMarkUnread(const std::string& request_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->BatchMarkUnread(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::BatchDelete(const std::string& request_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->BatchDelete(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::BatchMove(const std::string& request_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->BatchMove(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::BatchToggleStar(const std::string& request_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->BatchToggleStar(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::SyncFolders(const std::string& account_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SyncFolders(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SyncFolder(const std::string& account_id, const std::string& folder_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SyncFolder(account_id, folder_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateFolder(const std::string& account_id, const std::string& folder_name, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateFolder(account_id, folder_name, WrapReadReply(std::move(callback)));
}

void MahoMailService::RenameFolder(const std::string& account_id, const std::string& folder_id, const std::string& new_name, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->RenameFolder(account_id, folder_id, new_name, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteFolder(const std::string& account_id, const std::string& folder_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteFolder(account_id, folder_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetFolderCounts(const std::string& account_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
}
  helper->GetFolderCounts(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::FlushPendingMutations(const std::string& account_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->FlushPendingMutations(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetPendingMutationCount(ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetPendingMutationCount(WrapReadReply(std::move(callback)));
}

void MahoMailService::ListPendingMutations(const std::string& account_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListPendingMutations(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SetLauncherForTesting(
    std::unique_ptr<MahoMailHelperLauncher> launcher) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  enabled_for_testing_ = true;
  if (launcher) {
    ClearUnreadState();
    ++generation_;
  }
  launcher_ = std::move(launcher);
  if (launcher_) {
    launcher_->SetObserver(this);
    lifecycle_state_ = launcher_->GetHelper() ? LifecycleState::kReady
                                              : LifecycleState::kStarting;
  }
}

void MahoMailService::SetLauncherFactoryForTesting(
    LauncherFactory launcher_factory) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  launcher_factory_ = std::move(launcher_factory);
}

void MahoMailService::SetEnabledForTesting(bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  enabled_for_testing_ = enabled;
  OnMailEnabledPrefChanged();
}

MahoMailService::ReadCallback
MahoMailService::StartFolderBadgeRefreshForTesting(
    const std::string& account_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t request_token = unread_badge_state_.BeginRefresh(account_id);
  return base::BindOnce(&MahoMailService::ApplyFolderBadgeReply,
                        weak_ptr_factory_.GetWeakPtr(), account_id, generation_,
                        request_token);
}

bool MahoMailService::IsEnabled() const {
  return profile_ ? sidebar_prefs::IsMahoMailEnabled(profile_->GetPrefs())
                  : enabled_for_testing_;
}

bool MahoMailService::IsCommandAvailable() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsEnabled() && lifecycle_state_ == LifecycleState::kReady &&
         launcher_ && !launcher_->has_given_up() && launcher_->GetHelper();
}

void MahoMailService::OnMailKeysReadFromFile(
    std::pair<std::string, std::string> keys) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (keys.first.empty() || keys.second.empty()) {
    LOG(ERROR) << "[MahoMailService] Mail key file is missing or unreadable; "
                  "mail stays unavailable rather than recreating its database.";
    return;
  }
  sqlcipher_key_ = std::move(keys.first);
  credential_key_ = std::move(keys.second);
  StartGeneration();
}

void MahoMailService::OnMailKeyFileWritten(bool written) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!written) {
    LOG(ERROR) << "[MahoMailService] Failed to persist mail keys; not starting "
                  "the helper with a key that would be lost.";
    return;
  }
  StartGeneration();
}

void MahoMailService::StartGeneration() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_requested_ || !IsEnabled() || !launch_inputs_ ||
      sqlcipher_key_.empty() || credential_key_.empty() ||
      lifecycle_state_ == LifecycleState::kStarting ||
      lifecycle_state_ == LifecycleState::kReady ||
      lifecycle_state_ == LifecycleState::kDraining) {
    return;
  }
  launcher_ = launcher_factory_.Run();
  launcher_->SetObserver(this);
  ++generation_;
  if (attachment_registry_) {
    attachment_registry_->SetGeneration(generation_);
  }
  lifecycle_state_ = LifecycleState::kStarting;
  NotifyLifecycleChanged();
  launcher_->InjectDatabaseKeys(sqlcipher_key_, credential_key_);
  launcher_->Launch(launch_inputs_->expected_version, launch_inputs_->profile_path,
                    launch_inputs_->crashpad_database);
}

void MahoMailService::BeginDrain(LifecycleState terminal_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attachment_registry_) {
    attachment_registry_->RevokeAll();
  }
  drain_terminal_state_ = terminal_state;
  if (lifecycle_state_ == LifecycleState::kDraining) {
    return;
  }
  if (!launcher_) {
    std::vector<base::OnceClosure> queued_work = std::move(queued_ready_work_);
    queued_ready_work_.clear();
    lifecycle_state_ = terminal_state;
    NotifyLifecycleChanged();
    for (auto& work : queued_work) {
      std::move(work).Run();
    }
    return;
  }
  lifecycle_state_ = LifecycleState::kDraining;
  NotifyLifecycleChanged();
  std::vector<base::OnceClosure> queued_work = std::move(queued_ready_work_);
  queued_ready_work_.clear();
  helper_drain_complete_ = false;
  launcher_->Shutdown(base::BindOnce(&MahoMailService::OnGenerationDrained,
                                     weak_ptr_factory_.GetWeakPtr(),
                                     generation_));
  for (auto& work : queued_work) {
    std::move(work).Run();
  }
}

void MahoMailService::OnGenerationDrained(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != generation_ || lifecycle_state_ != LifecycleState::kDraining) {
    return;
  }
  helper_drain_complete_ = true;
  if (tracked_replies_ != 0) {
    return;
  }
  launcher_.reset();
  lifecycle_state_ = drain_terminal_state_;
  NotifyLifecycleChanged();
  if (restart_after_drain_ && !shutdown_requested_ && IsEnabled()) {
    restart_after_drain_ = false;
    StartGeneration();
  }
}

base::OnceCallback<void(bool, const std::string&)>
MahoMailService::WrapReadReply(ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++tracked_replies_;
  return WrapUntrackedReadReply(base::BindOnce(
      [](base::WeakPtr<MahoMailService> service, ReadCallback callback, bool ok,
         std::string result) {
        std::move(callback).Run(ok, std::move(result));
        if (service) {
          service->OnTrackedReplyComplete();
        }
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

base::OnceCallback<void(bool)> MahoMailService::WrapCancelReply(
    CancelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++tracked_replies_;
  return mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(
          [](base::WeakPtr<MahoMailService> service, CancelCallback callback,
             bool accepted) {
            std::move(callback).Run(accepted);
            if (service) {
              service->OnTrackedReplyComplete();
            }
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      /*accepted=*/false);
}

void MahoMailService::OnTrackedReplyComplete() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GT(tracked_replies_, 0u);
  --tracked_replies_;
  if (lifecycle_state_ == LifecycleState::kDraining && helper_drain_complete_) {
    OnGenerationDrained(generation_);
  }
}

mojom::MahoMailHelper* MahoMailService::GetHelperOrNull() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsEnabled() || lifecycle_state_ != LifecycleState::kReady || !launcher_ ||
      launcher_->has_given_up()) {
    return nullptr;
  }
  return launcher_->GetHelper();
}

void MahoMailService::ListAccounts(ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if ((lifecycle_state_ == LifecycleState::kFailed ||
       lifecycle_state_ == LifecycleState::kStopped) &&
      IsEnabled() && !shutdown_requested_ && launch_inputs_ &&
      base::TimeTicks::Now() >= next_on_demand_launch_) {
    next_on_demand_launch_ = base::TimeTicks::Now() + base::Seconds(60);
    PendingLaunch launch = *launch_inputs_;
    EnsureHelperLaunched(launch.expected_version, launch.profile_path,
                         launch.crashpad_database);
  }
  if (lifecycle_state_ == LifecycleState::kStarting && IsEnabled()) {
    queued_ready_work_.push_back(base::BindOnce(
        [](base::WeakPtr<MahoMailService> service, ReadCallback callback) {
          if (service && service->IsCommandAvailable()) {
            service->ListAccounts(std::move(callback));
          } else {
            std::move(callback).Run(false, kMahoMailHelperUnavailable);
          }
        },
        weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
    return;
  }
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListAccounts(WrapReadReply(base::BindOnce(
      [](base::WeakPtr<MahoMailService> service, ReadCallback callback,
         bool ok, std::string result_json) {
        if (service && ok) {
          service->RefreshBadgesForAccounts(result_json);
        }
        std::move(callback).Run(ok, std::move(result_json));
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(callback))));
}

void MahoMailService::ListFolders(const std::string& account_id,
                                  ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  const uint64_t request_token = unread_badge_state_.BeginRefresh(account_id);
  helper->ListFolders(
      account_id,
      WrapReadReply(base::BindOnce(
          [](base::WeakPtr<MahoMailService> service, std::string account_id,
             uint64_t generation, uint64_t request_token,
             ReadCallback callback, bool ok,
             std::string result_json) {
            if (service && ok) {
              service->OnFoldersForBadge(account_id, generation,
                                         request_token, true, result_json);
            }
            std::move(callback).Run(ok, std::move(result_json));
          },
          weak_ptr_factory_.GetWeakPtr(), account_id, generation_,
          request_token,
          std::move(callback))));
}

void MahoMailService::ListEmails(const std::string& account_id,
                                 const std::string& folder_id,
                                 int64_t limit,
                                 int64_t offset,
                                 ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListEmails(account_id, folder_id, limit, offset,
                     WrapReadReply(std::move(callback)));
}

void MahoMailService::GetEmail(const std::string& email_id,
                               ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetEmail(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SearchEmails(const std::string& query_json,
                                   ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SearchEmails(query_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListThread(const std::string& account_id,
                                 const std::string& message_id,
                                 ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListThread(account_id, message_id,
                     WrapReadReply(std::move(callback)));
}

void MahoMailService::AddAccount(const std::string& request_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->AddAccount(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::TestConnection(const std::string& params_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->TestConnection(params_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteAccount(const std::string& account_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteAccount(
      account_id,
      WrapReadReply(base::BindOnce(
          [](base::WeakPtr<MahoMailService> service, std::string account_id,
             ReadCallback callback, bool ok, std::string result) {
            if (ok && service) {
              if (service->attachment_registry_) {
                service->attachment_registry_->RevokeAccount(account_id);
              }
              service->unread_badge_state_.RemoveAccount(account_id);
              service->UpdateUnreadCountPref();
              for (auto& observer : service->observers_) {
                observer.OnAccountRemoved(account_id);
              }
            }
            std::move(callback).Run(ok, std::move(result));
          },
          weak_ptr_factory_.GetWeakPtr(), account_id, std::move(callback))));
}

void MahoMailService::OAuthStartUrl(const std::string& provider,
                                    const std::string& client_id,
                                    const std::string& redirect_uri,
                                    const std::string& options_json,
                                    ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->OAuthStartUrl(provider, client_id, redirect_uri, options_json,
                        WrapReadReply(std::move(callback)));
}

void MahoMailService::OAuthLoopbackSignIn(
    const std::string& provider,
    const std::string& options_json,
    ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->OAuthLoopbackSignIn(provider, options_json,
                              WrapReadReply(std::move(callback)));
}

void MahoMailService::OAuthComplete(const std::string& state,
                                    const std::string& code,
                                    ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->OAuthComplete(state, code, WrapReadReply(std::move(callback)));
}

void MahoMailService::ReconnectAccount(const std::string& account_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ReconnectAccount(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ImportMigrationArchive(const std::string& archive_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ImportMigrationArchive(archive_json, WrapReadReply(std::move(callback)));
}

  // === [W-C..W-K.Typed] ===

void MahoMailService::StartAllAccountSync(MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->StartAllAccountSync(WrapReadReply(std::move(callback)));
}

void MahoMailService::SendEmail(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SendEmail(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::SaveDraft(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SaveDraft(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateDraft(const std::string& draft_id, const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateDraft(draft_id, request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetReplyContext(const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetReplyContext(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::QueueEmail(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->QueueEmail(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListOutbox(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListOutbox(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::RetryOutboxItem(const std::string& item_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->RetryOutboxItem(item_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteOutboxItem(const std::string& item_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteOutboxItem(item_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::FlushOutbox(MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->FlushOutbox(WrapReadReply(std::move(callback)));
}

void MahoMailService::DownloadAttachment(const std::string& account_id, int64_t email_uid, const std::string& folder_id, const std::string& part_id, const std::string& filename, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The helper joins these identifiers into an on-disk attachment path, so a
  // renderer-supplied ".." or separator would escape the intended directory.
  // Reject traversal at this process boundary before the values reach the
  // helper; identifiers are opaque tokens and never contain path syntax.
  if (!IsPathSafeIdentifier(account_id) || !IsPathSafeIdentifier(folder_id) ||
      !IsPathSafeIdentifier(part_id) || !IsPathSafeFileName(filename)) {
    std::move(callback).Run(false, kMahoMailInvalidIdentifier);
    return;
  }
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DownloadAttachment(
      account_id, email_uid, folder_id, part_id, filename,
      WrapReadReply(base::BindOnce(
          [](base::WeakPtr<MahoMailService> service,
             MailAttachmentMessageIdentity message, std::string filename,
             MailReadCallback callback, bool ok, std::string result_json) {
            if (!ok || !service || !service->attachment_registry_) {
              std::move(callback).Run(
                  false, ok ? "attachment staging unavailable"
                            : std::move(result_json));
              return;
            }
            std::optional<base::Value> parsed = base::JSONReader::Read(
                result_json, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_string()) {
              std::move(callback).Run(false, "invalid attachment staging reply");
              return;
            }
            service->attachment_registry_->StageDownloadedFile(
                message, base::FilePath::FromUTF8Unsafe(parsed->GetString()),
                filename, base::BindOnce(
                    [](MailReadCallback callback, bool staged,
                       std::string token, std::string error) {
                      if (!staged) {
                        std::move(callback).Run(false, std::move(error));
                        return;
                      }
                      std::string token_json;
                      base::JSONWriter::Write(base::Value(std::move(token)),
                                              &token_json);
                      std::move(callback).Run(true, std::move(token_json));
                    },
                    std::move(callback)));
          },
          weak_ptr_factory_.GetWeakPtr(),
          MailAttachmentMessageIdentity{account_id, email_uid, folder_id,
                                        part_id},
          filename, std::move(callback))));
}

void MahoMailService::OpenAttachment(
    const std::string& capability_token,
    MahoMailAttachmentRegistry::ConsumeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!attachment_registry_) {
    std::move(callback).Run(nullptr, "attachment staging unavailable");
    return;
  }
  attachment_registry_->Consume(capability_token, profile_identity_,
                                std::move(callback));
}

void MahoMailService::SaveAttachment(
    const std::string& capability_token,
    MahoMailAttachmentRegistry::SaveCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!attachment_registry_ || !profile_) {
    std::move(callback).Run(false, "attachment staging unavailable", std::string());
    return;
  }
  attachment_registry_->SaveToDownloads(
      capability_token, profile_identity_,
      profile_->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory),
      std::move(callback));
}

void MahoMailService::ExtractOtp(const std::string& account_id, const std::string& folder_id, const std::string& query, int64_t max_age_seconds, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ExtractOtp(account_id, folder_id, query, max_age_seconds, WrapReadReply(std::move(callback)));
}

void MahoMailService::SnoozeEmail(const std::string& email_id, const std::string& snooze_until, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SnoozeEmail(email_id, snooze_until, WrapReadReply(std::move(callback)));
}

void MahoMailService::UnsnoozeEmail(const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UnsnoozeEmail(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListSnoozedEmails(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListSnoozedEmails(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SetReminder(const std::string& email_id, const std::string& reminder_at, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SetReminder(email_id, reminder_at, WrapReadReply(std::move(callback)));
}

void MahoMailService::ClearReminder(const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ClearReminder(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListReminders(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListReminders(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::MuteThread(const std::string& account_id, const std::string& message_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->MuteThread(
      account_id, message_id,
      WrapReadReply(base::BindOnce(
          &MahoMailService::OnPersistedThreadMuteChanged,
          weak_ptr_factory_.GetWeakPtr(), account_id, message_id,
          /*muted=*/true, std::move(callback))));
}

void MahoMailService::UnmuteThread(const std::string& account_id, const std::string& message_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UnmuteThread(
      account_id, message_id,
      WrapReadReply(base::BindOnce(
          &MahoMailService::OnPersistedThreadMuteChanged,
          weak_ptr_factory_.GetWeakPtr(), account_id, message_id,
          /*muted=*/false, std::move(callback))));
}

void MahoMailService::OnPersistedThreadMuteChanged(
    const std::string& account_id,
    const std::string& message_id,
    bool muted,
    MailReadCallback callback,
    bool ok,
    std::string result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (ok) {
    for (auto& observer : observers_) {
      observer.OnThreadMuteChanged(account_id, message_id, muted);
    }
  }
  std::move(callback).Run(ok, std::move(result));
}

void MahoMailService::IsThreadMuted(const std::string& account_id, const std::string& message_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->IsThreadMuted(account_id, message_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListMutedThreads(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListMutedThreads(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::FilterMutedMessageIds(const std::string& account_id, const std::string& message_ids_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->FilterMutedMessageIds(account_id, message_ids_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::PinEmail(const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->PinEmail(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::UnpinEmail(const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UnpinEmail(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListPinnedEmails(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListPinnedEmails(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateMailRule(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateMailRule(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateMailRule(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateMailRule(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteMailRule(const std::string& rule_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteMailRule(rule_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListMailRules(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListMailRules(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ReorderMailRules(const std::string& account_id, const std::string& rule_ids_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ReorderMailRules(account_id, rule_ids_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListLabels(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListLabels(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateLabel(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateLabel(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteLabel(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteLabel(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::AddLabelToEmail(const std::string& email_id, const std::string& label_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->AddLabelToEmail(email_id, label_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::RemoveLabelFromEmail(const std::string& email_id, const std::string& label_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->RemoveLabelFromEmail(email_id, label_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListEmailLabels(const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListEmailLabels(email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SaveSearch(const std::string& name, const std::string& query, const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SaveSearch(name, query, account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListSavedSearches(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListSavedSearches(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteSavedSearch(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteSavedSearch(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ScheduleSend(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ScheduleSend(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::CancelScheduledSend(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CancelScheduledSend(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListScheduledSends(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListScheduledSends(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::StartScheduler(MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->StartScheduler(WrapReadReply(std::move(callback)));
}

void MahoMailService::StopScheduler(MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->StopScheduler(WrapReadReply(std::move(callback)));
}

void MahoMailService::SearchContacts(const std::string& account_id, const std::string& query, int64_t limit, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SearchContacts(account_id, query, limit, WrapReadReply(std::move(callback)));
}

void MahoMailService::ToggleVip(const std::string& contact_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ToggleVip(contact_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListVipContacts(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListVipContacts(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::PopulateContactsFromHistory(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->PopulateContactsFromHistory(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListContactGroups(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListContactGroups(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SearchContactGroups(const std::string& account_id, const std::string& query, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SearchContactGroups(account_id, query, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateContactGroup(const std::string& account_id, const std::string& name, const std::string& member_emails_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateContactGroup(account_id, name, member_emails_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateContactGroup(const std::string& group_id, const std::string& name, const std::string& member_emails_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateContactGroup(group_id, name, member_emails_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteContactGroup(const std::string& group_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteContactGroup(group_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListSignatures(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListSignatures(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateSignature(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateSignature(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateSignature(const std::string& id, const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateSignature(id, request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteSignature(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteSignature(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListTemplates(MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListTemplates(WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateTemplate(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateTemplate(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateTemplate(const std::string& id, const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateTemplate(id, request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteTemplate(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteTemplate(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::PushBrowserAiKeys(mojom::MahoMailHelper* helper) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!helper || !profile_ || !encryptor_) {
    return;
  }

  PrefService* prefs = profile_->GetPrefs();
  const std::string openai_key =
      DecryptByokKey(*encryptor_, prefs->GetString(kByokOpenAIEncryptedB64));
  const std::string anthropic_key =
      DecryptByokKey(*encryptor_, prefs->GetString(kByokAnthropicEncryptedB64));

  base::DictValue keys;
  keys.Set("openai", openai_key);
  keys.Set("anthropic", anthropic_key);
  std::string keys_json;
  if (!base::JSONWriter::Write(keys, &keys_json)) {
    return;
  }

  helper->SetBrowserAiKeys(keys_json, base::DoNothing());
}

void MahoMailService::GetEmailSummary(const std::string& account_id, const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->GetEmailSummary(account_id, request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetReplyDraft(const std::string& account_id, const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->GetReplyDraft(account_id, request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::AdjustTone(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->AdjustTone(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ClassifyEmail(const std::string& account_id, const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->ClassifyEmail(account_id, request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::NaturalLanguageSearch(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->NaturalLanguageSearch(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetAiActionHistory(const std::string& account_id, int64_t limit, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetAiActionHistory(account_id, limit, WrapReadReply(std::move(callback)));
}

void MahoMailService::SaveAiConfig(const std::string& config_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SaveAiConfig(config_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetAiConfig(const std::string& feature, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetAiConfig(feature, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteAiConfig(const std::string& feature, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteAiConfig(feature, WrapReadReply(std::move(callback)));
}

void MahoMailService::TestAiConnection(const std::string& config_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->TestAiConnection(config_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetAutoDraftForEmail(const std::string& account_id, const std::string& email_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->GetAutoDraftForEmail(account_id, email_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateAutoDraftStatus(const std::string& draft_id, const std::string& status, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateAutoDraftStatus(draft_id, status, WrapReadReply(std::move(callback)));
}

void MahoMailService::TranslateText(const std::string& text, const std::string& target_lang, const std::string& source_lang, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  PushBrowserAiKeys(helper);
  helper->TranslateText(text, target_lang, source_lang, WrapReadReply(std::move(callback)));
}

void MahoMailService::GeneratePgpKey(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GeneratePgpKey(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ImportPgpKey(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ImportPgpKey(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ExportPgpKey(const std::string& key_id, bool include_private, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ExportPgpKey(key_id, include_private, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListPgpKeys(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListPgpKeys(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeletePgpKey(const std::string& key_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeletePgpKey(key_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SetDefaultPgpKey(const std::string& account_id, const std::string& key_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SetDefaultPgpKey(account_id, key_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::EncryptEmailPgp(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->EncryptEmailPgp(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::EncryptAttachmentPgp(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->EncryptAttachmentPgp(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DecryptEmailPgp(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DecryptEmailPgp(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::SignEmailPgp(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SignEmailPgp(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::VerifyEmailPgp(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->VerifyEmailPgp(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ImportSmimeIdentity(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ImportSmimeIdentity(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListSmimeIdentities(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListSmimeIdentities(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteSmimeIdentity(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteSmimeIdentity(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SetDefaultSmimeIdentity(const std::string& account_id, const std::string& identity_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SetDefaultSmimeIdentity(account_id, identity_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ExportSmimeCert(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ExportSmimeCert(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SignEmailSmime(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SignEmailSmime(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::EncryptEmailSmime(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->EncryptEmailSmime(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::DecryptEmailSmime(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DecryptEmailSmime(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::VerifyEmailSmime(const std::string& signed_body, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->VerifyEmailSmime(signed_body, WrapReadReply(std::move(callback)));
}

void MahoMailService::CleanupSmimeForAccount(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CleanupSmimeForAccount(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ImportCalendarEvent(const std::string& account_id, const std::string& email_id, const std::string& ics_data, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ImportCalendarEvent(account_id, email_id, ics_data, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListCalendarEvents(const std::string& account_id, const std::string& from_date, const std::string& to_date, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListCalendarEvents(account_id, from_date, to_date, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetCalendarEvent(const std::string& event_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetCalendarEvent(event_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateRsvp(const std::string& event_id, const std::string& rsvp_status, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateRsvp(event_id, rsvp_status, WrapReadReply(std::move(callback)));
}

void MahoMailService::GenerateRsvpReply(const std::string& event_id, const std::string& rsvp_status, const std::string& account_email, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GenerateRsvpReply(event_id, rsvp_status, account_email, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteCalendarEvent(const std::string& event_id, const std::string& delete_scope, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteCalendarEvent(event_id, delete_scope, WrapReadReply(std::move(callback)));
}

void MahoMailService::AutoImportCalendarEvents(const std::string& account_id, const std::string& email_id, const std::string& body_html, const std::string& body_text, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->AutoImportCalendarEvents(account_id, email_id, body_html, body_text, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateCalendarEvent(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateCalendarEvent(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateCalendarEvent(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateCalendarEvent(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::SearchCalendarEvents(const std::string& account_id, const std::string& query, int64_t limit, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SearchCalendarEvents(account_id, query, limit, WrapReadReply(std::move(callback)));
}

void MahoMailService::CheckEventConflict(const std::string& account_id, const std::string& dtstart, const std::string& dtend, const std::string& exclude_event_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CheckEventConflict(account_id, dtstart, dtend, exclude_event_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::DuplicateCalendarEvent(const std::string& event_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DuplicateCalendarEvent(event_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::GoogleCalendarMoveEvent(const std::string& account_id, const std::string& calendar_id, const std::string& event_id, const std::string& destination_calendar_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GoogleCalendarMoveEvent(account_id, calendar_id, event_id, destination_calendar_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::ExportCalendarIcs(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ExportCalendarIcs(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListCalendarCategories(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListCalendarCategories(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::CreateCalendarCategory(const std::string& account_id, const std::string& name, const std::string& color, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CreateCalendarCategory(account_id, name, color, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateCalendarCategory(const std::string& id, const std::string& name, const std::string& color, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateCalendarCategory(id, name, color, WrapReadReply(std::move(callback)));
}

void MahoMailService::DeleteCalendarCategory(const std::string& id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->DeleteCalendarCategory(id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SyncGoogleCalendar(const std::string& account_id, bool _full_sync, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SyncGoogleCalendar(account_id, _full_sync, WrapReadReply(std::move(callback)));
}

void MahoMailService::ListAccountCalendars(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->ListAccountCalendars(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::SetCalendarVisibility(const std::string& calendar_row_id, bool visible, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SetCalendarVisibility(calendar_row_id, visible, WrapReadReply(std::move(callback)));
}

void MahoMailService::SubscribeHolidayCalendar(const std::string& account_id, const std::string& locale_code, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SubscribeHolidayCalendar(account_id, locale_code, WrapReadReply(std::move(callback)));
}

void MahoMailService::GoogleCalendarFreeBusy(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GoogleCalendarFreeBusy(request_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::GetAppSetting(const std::string& key, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetAppSetting(key, WrapReadReply(std::move(callback)));
}

void MahoMailService::SetAppSetting(const std::string& key, const std::string& value, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->SetAppSetting(
      key, value,
      WrapReadReply(base::BindOnce(
          [](base::WeakPtr<MahoMailService> service, std::string setting_key,
             std::string setting_value, MailReadCallback callback, bool ok,
             std::string result) {
            if (ok && service && service->profile_ &&
                setting_key == "mail_behavior_prefs") {
              const auto snapshot = ParseMailBehaviorSnapshot(setting_value);
              if (snapshot) {
                service->behavior_snapshot_ = *snapshot;
                PrefService* prefs = service->profile_->GetPrefs();
                prefs->SetBoolean("maho.mail.notifications_enabled",
                                  snapshot->desktop_notifications);
                prefs->SetBoolean("maho.mail.badge_enabled",
                                  snapshot->unread_badge_enabled);
                service->UpdateUnreadCountPref();
                if (service->behavior_mirror_callback_for_testing_) {
                  service->behavior_mirror_callback_for_testing_.Run(*snapshot);
                }
              }
            } else if (ok && service &&
                       setting_key == "mail_behavior_prefs" &&
                       service->behavior_mirror_callback_for_testing_) {
              const auto snapshot = ParseMailBehaviorSnapshot(setting_value);
              if (snapshot) {
                service->behavior_snapshot_ = *snapshot;
                service->behavior_mirror_callback_for_testing_.Run(*snapshot);
              }
            }
            std::move(callback).Run(ok, std::move(result));
          },
          weak_ptr_factory_.GetWeakPtr(), key, value, std::move(callback))));
}

void MahoMailService::GetBehaviorSnapshot(BehaviorReadCallback callback) {
  GetAppSetting(
      "mail_behavior_prefs",
      base::BindOnce(
          [](base::WeakPtr<MahoMailService> service,
             BehaviorReadCallback callback, bool ok, std::string result) {
            if (!ok) {
              std::move(callback).Run(false, MailBehaviorSnapshot());
              return;
            }
            // The read answers with the JSON encoding of an Option<String>,
            // so the stored dict arrives as a JSON *string* and an unset row
            // as `null`. Parsing the envelope directly always failed the
            // is_dict() check and made every behavior read report failure.
            std::string stored =
                DecodeAppSettingRead(result).value_or(std::string());
            if (stored.empty()) {
              stored = "{}";
            }
            const auto snapshot = ParseMailBehaviorSnapshot(stored);
            if (!snapshot) {
              std::move(callback).Run(false, MailBehaviorSnapshot());
              return;
            }
            if (service) {
              service->behavior_snapshot_ = *snapshot;
            }
            const std::string migrated =
                SerializeMailBehaviorSnapshot(*snapshot);
            if (migrated != stored) {
              // Best-effort canonicalization is intentionally deferred to the
              // next successful CAS write; reads remain side-effect free.
            }
            std::move(callback).Run(true, *snapshot);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoMailService::UpdateBehavior(uint64_t expected_revision,
                                     const std::string& key,
                                     const std::string& value_json,
                                     BehaviorUpdateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    MailBehaviorUpdateResult result;
    result.status = MailBehaviorUpdateStatus::kRejected;
    result.snapshot = behavior_snapshot_;
    std::move(callback).Run(std::move(result));
    return;
  }
  helper->UpdateBehaviorSetting(
      expected_revision, key, value_json,
      WrapUntrackedReadReply(base::BindOnce(
          [](base::WeakPtr<MahoMailService> service,
             BehaviorUpdateCallback callback, bool ok,
             std::string result_json) {
            MailBehaviorUpdateResult result;
            result.status = MailBehaviorUpdateStatus::kRejected;
            if (!ok) {
              result.snapshot = service ? service->behavior_snapshot_
                                        : MailBehaviorSnapshot();
              std::move(callback).Run(std::move(result));
              return;
            }
            std::optional<base::Value> parsed =
                base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
            const base::DictValue* dict =
                parsed && parsed->is_dict() ? &parsed->GetDict() : nullptr;
            const std::string* status = dict ? dict->FindString("status") : nullptr;
            const base::DictValue* snapshot_dict =
                dict ? dict->FindDict("snapshot") : nullptr;
            std::string snapshot_json;
            if (!status || !snapshot_dict ||
                !base::JSONWriter::Write(*snapshot_dict, &snapshot_json)) {
              result.snapshot = service ? service->behavior_snapshot_
                                        : MailBehaviorSnapshot();
              std::move(callback).Run(std::move(result));
              return;
            }
            std::optional<MailBehaviorSnapshot> snapshot =
                ParseMailBehaviorSnapshot(snapshot_json);
            if (!snapshot) {
              result.snapshot = service ? service->behavior_snapshot_
                                        : MailBehaviorSnapshot();
              std::move(callback).Run(std::move(result));
              return;
            }
            result.status = *status == "applied"
                                ? MailBehaviorUpdateStatus::kApplied
                                : *status == "conflict"
                                      ? MailBehaviorUpdateStatus::kConflict
                                      : *status == "invalid"
                                            ? MailBehaviorUpdateStatus::kInvalid
                                            : MailBehaviorUpdateStatus::kRejected;
            result.snapshot = *snapshot;
            if (service && result.status == MailBehaviorUpdateStatus::kApplied) {
              service->behavior_snapshot_ = *snapshot;
              if (service->profile_) {
                PrefService* prefs = service->profile_->GetPrefs();
                prefs->SetBoolean("maho.mail.notifications_enabled",
                                  snapshot->desktop_notifications);
                prefs->SetBoolean("maho.mail.badge_enabled",
                                  snapshot->unread_badge_enabled);
                service->UpdateUnreadCountPref();
              }
              if (service->behavior_mirror_callback_for_testing_) {
                service->behavior_mirror_callback_for_testing_.Run(*snapshot);
              }
            }
            std::move(callback).Run(std::move(result));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback))));
}

void MahoMailService::Md5Hash(const std::string& input, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->Md5Hash(input, WrapReadReply(std::move(callback)));
}


  // === [W-C.Additional.Typed] ===
void MahoMailService::GetAccount(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->GetAccount(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::UpdateAccount(const std::string& request_json, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->UpdateAccount(request_json, WrapReadReply(std::move(callback)));
}

  // === [W-C.Additional.Refresh.Typed] ===
void MahoMailService::RefreshOAuthToken(const std::string& account_id, MailReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->RefreshOAuthToken(account_id, WrapReadReply(std::move(callback)));
}

void MahoMailService::CallBackend(const std::string& command, const std::string& args_json, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false, kMahoMailHelperUnavailable);
    return;
  }
  helper->CallBackend(command, args_json, WrapReadReply(std::move(callback)));
}

void MahoMailService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void MahoMailService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void MahoMailService::OnAuthRequired(const std::string& account_id,
                                     const std::string& provider,
                                     const std::string& reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnAuthRequired(account_id, provider, reason);
  }
}

void MahoMailService::OnAuthRefreshSucceeded(const std::string& account_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnAuthRefreshSucceeded(account_id);
  }
}

void MahoMailService::OnAccountsChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnAccountsChanged();
  }
}

void MahoMailService::OnHelperReady() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (lifecycle_state_ != LifecycleState::kStarting || !IsEnabled()) {
    return;
  }
  lifecycle_state_ = LifecycleState::kReady;
  NotifyLifecycleChanged();
  std::vector<base::OnceClosure> queued_work = std::move(queued_ready_work_);
  queued_ready_work_.clear();
  for (auto& work : queued_work) {
    std::move(work).Run();
  }
  ListAccounts(base::BindOnce([](bool, std::string) {}));
}

void MahoMailService::OnHelperFailed(bool will_retry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (lifecycle_state_ == LifecycleState::kDraining) {
    return;
  }
  ClearUnreadState();
  lifecycle_state_ = will_retry ? LifecycleState::kStarting
                                : LifecycleState::kFailed;
  NotifyLifecycleChanged();
  if (!will_retry) {
    std::vector<base::OnceClosure> queued_work = std::move(queued_ready_work_);
    queued_ready_work_.clear();
    for (auto& work : queued_work) {
      std::move(work).Run();
    }
  }
}

void MahoMailService::NotifyLifecycleChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnLifecycleChanged(lifecycle_state_, generation_);
  }
}

void MahoMailService::RefreshBadgesForAccounts(
    const std::string& result_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return;
  }
  const base::ListValue* accounts = nullptr;
  if (parsed->is_list()) {
    accounts = &parsed->GetList();
  } else if (parsed->is_dict()) {
    accounts = parsed->GetDict().FindList("accounts");
  }
  if (!accounts) {
    return;
  }
  std::set<std::string> account_ids;
  for (const base::Value& account : *accounts) {
    if (!account.is_dict()) {
      continue;
    }
    const std::string* account_id = account.GetDict().FindString("id");
    if (account_id && !account_id->empty()) {
      account_ids.insert(*account_id);
    }
  }
  if (unread_badge_state_.RetainAccounts(account_ids)) {
    UpdateUnreadCountPref();
  }
  for (const std::string& account_id : account_ids) {
    RefreshMailBadge(account_id);
  }
}

void MahoMailService::OnSyncEvent(const std::string& event_type, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnSyncEvent(event_type, payload);
  }
}

void MahoMailService::OnBackfillEvent(const std::string& event_type, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnBackfillEvent(event_type, payload);
  }
}

void MahoMailService::OnStatusChanged(const std::string& account_id, bool connected) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnStatusChanged(account_id, connected);
  }
  if (connected) {
    RefreshMailBadge(account_id);
  }
}

void MahoMailService::OnNewMail(const std::string& account_id,
                                const std::string& email_id,
                                const std::string& message_id,
                                const std::string& sender,
                                const std::string& subject,
                                uint64_t cursor,
                                uint64_t epoch) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnNewMail(account_id, email_id, message_id, sender, subject,
                       cursor, epoch);
  }
  RefreshMailBadge(account_id);
}

void MahoMailService::OnMutation(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnMutation(account_id, payload);
  }
  // Flag mutations (mark read/unread, delete) change the unread total.
  RefreshMailBadge(account_id);
}

void MahoMailService::OnOutbox(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnOutbox(account_id, payload);
  }
}

void MahoMailService::OnScheduler(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnScheduler(account_id, payload);
  }
}

void MahoMailService::OnAgentStream(const std::string& session_id, const std::string& chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnAgentStream(session_id, chunk);
  }
}

void MahoMailService::OnCalendar(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnCalendar(account_id, payload);
  }
}

void MahoMailService::OnImport(const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnImport(payload);
  }
}

void MahoMailService::OAuthCancel(const std::string& state, CancelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojom::MahoMailHelper* helper = GetHelperOrNull();
  if (!helper) {
    std::move(callback).Run(false);
    return;
  }
  helper->OAuthCancel(state, WrapCancelReply(std::move(callback)));
}

// === Unread-badge + new-mail-notification pipeline ===

void MahoMailService::RefreshMailBadge(const std::string& account_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!profile_ || account_id.empty()) {
    return;
  }
  ListFolders(account_id, base::DoNothing());
}

void MahoMailService::OnFoldersForBadge(const std::string& account_id,
                                        uint64_t generation,
                                        uint64_t request_token,
                                        bool ok,
                                        std::string result_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!profile_) {
    return;
  }
  ApplyFolderBadgeReply(account_id, generation, request_token, ok,
                        std::move(result_json));
}

void MahoMailService::ApplyFolderBadgeReply(const std::string& account_id,
                                             uint64_t generation,
                                             uint64_t request_token,
                                             bool ok,
                                             std::string result_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ok || generation != generation_ ||
      lifecycle_state_ != LifecycleState::kReady) {
    return;
  }
  const int64_t inbox_unread =
      ComputeInboxUnreadForBadge(result_json, nullptr);
  if (!unread_badge_state_.CompleteRefresh(account_id, request_token,
                                           inbox_unread)) {
    return;
  }
  UpdateUnreadCountPref();
}

void MahoMailService::UpdateUnreadCountPref() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!profile_) {
    return;
  }
  const int64_t total = unread_badge_state_.total();
  profile_->GetPrefs()->SetInteger("maho.mail.unread_count",
                                   static_cast<int>(total));
#if BUILDFLAG(IS_MAC)
  SetMailDockBadge(
      static_cast<int>(total),
      sidebar_prefs::IsMahoMailEnabled(profile_->GetPrefs()) &&
          profile_->GetPrefs()->GetBoolean("maho.mail.badge_enabled"));
#endif
}

void MahoMailService::ClearUnreadState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  unread_badge_state_.Clear();
  UpdateUnreadCountPref();
}

}  // namespace maho
