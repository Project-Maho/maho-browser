// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <optional>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"

namespace base {
class FilePath;
}  // namespace base

class Profile;
class PrefRegistrySimple;

namespace os_crypt_async {
class OSCryptAsync;
}  // namespace os_crypt_async

namespace maho {

namespace mojom {
class MahoMailHelper;
}  // namespace mojom

class MahoMailService : public KeyedService,
                        public MahoMailHelperLauncher::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnAuthRequired(const std::string& account_id,
                                const std::string& provider,
                                const std::string& reason) {}
    virtual void OnAuthRefreshSucceeded(const std::string& account_id) {}
    virtual void OnAccountsChanged() {}
    virtual void OnSyncEvent(const std::string& event_type, const std::string& payload) {}
    virtual void OnBackfillEvent(const std::string& event_type, const std::string& payload) {}
    virtual void OnStatusChanged(const std::string& account_id, bool connected) {}
    virtual void OnNewMail(const std::string& account_id) {}
    virtual void OnMutation(const std::string& account_id, const std::string& payload) {}
    virtual void OnOutbox(const std::string& account_id, const std::string& payload) {}
    virtual void OnScheduler(const std::string& account_id, const std::string& payload) {}
    virtual void OnAgentStream(const std::string& session_id, const std::string& chunk) {}
    virtual void OnCalendar(const std::string& account_id, const std::string& payload) {}
    virtual void OnImport(const std::string& payload) {}
  };

  using ReadCallback = base::OnceCallback<void(bool, std::string)>;
  using MailReadCallback = ReadCallback;

  explicit MahoMailService(Profile* profile = nullptr,
                           os_crypt_async::OSCryptAsync* os_crypt_async = nullptr);
  ~MahoMailService() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // MahoMailHelperLauncher::Observer:
  void OnAuthRequired(const std::string& account_id,
                      const std::string& provider,
                      const std::string& reason) override;
  void OnAuthRefreshSucceeded(const std::string& account_id) override;
  void OnAccountsChanged() override;
  void OnHelperReady() override;
  void OnSyncEvent(const std::string& event_type, const std::string& payload) override;
  void OnBackfillEvent(const std::string& event_type, const std::string& payload) override;
  void OnStatusChanged(const std::string& account_id, bool connected) override;
  void OnNewMail(const std::string& account_id) override;
  void OnMutation(const std::string& account_id, const std::string& payload) override;
  void OnOutbox(const std::string& account_id, const std::string& payload) override;
  void OnScheduler(const std::string& account_id, const std::string& payload) override;
  void OnAgentStream(const std::string& session_id, const std::string& chunk) override;
  void OnCalendar(const std::string& account_id, const std::string& payload) override;
  void OnImport(const std::string& payload) override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

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

  // Onboarding methods
  void AddAccount(const std::string& request_json, ReadCallback callback);
  void TestConnection(const std::string& params_json, ReadCallback callback);
  void DeleteAccount(const std::string& account_id, ReadCallback callback);
  void OAuthStartUrl(const std::string& provider,
                     const std::string& client_id,
                     const std::string& redirect_uri,
                     const std::string& options_json,
                     ReadCallback callback);
  void OAuthLoopbackSignIn(const std::string& provider,
                           const std::string& options_json,
                           ReadCallback callback);
  void OAuthComplete(const std::string& state,
                     const std::string& code,
                     ReadCallback callback);
  void ReconnectAccount(const std::string& account_id, ReadCallback callback);
  void ImportMigrationArchive(const std::string& archive_json, ReadCallback callback);
  using CancelCallback = base::OnceCallback<void(bool)>;
  void OAuthCancel(const std::string& state, CancelCallback callback);

  // Wave C: Write Flags & Folders
  void MarkRead(const std::string& email_id, ReadCallback callback);
  void MarkUnread(const std::string& email_id, ReadCallback callback);
  void ToggleStar(const std::string& email_id, ReadCallback callback);
  void DeleteEmail(const std::string& email_id, ReadCallback callback);
  void MoveEmail(const std::string& email_id, const std::string& target_folder_id, ReadCallback callback);
  void BatchMarkRead(const std::string& request_json, ReadCallback callback);
  void BatchMarkUnread(const std::string& request_json, ReadCallback callback);
  void BatchDelete(const std::string& request_json, ReadCallback callback);
  void BatchMove(const std::string& request_json, ReadCallback callback);
  void BatchToggleStar(const std::string& request_json, ReadCallback callback);
  void SyncFolders(const std::string& account_id, ReadCallback callback);
  void SyncFolder(const std::string& account_id, const std::string& folder_id, ReadCallback callback);
  void CreateFolder(const std::string& account_id, const std::string& folder_name, ReadCallback callback);
  void RenameFolder(const std::string& account_id, const std::string& folder_id, const std::string& new_name, ReadCallback callback);
  void DeleteFolder(const std::string& account_id, const std::string& folder_id, ReadCallback callback);
  void GetFolderCounts(const std::string& account_id, ReadCallback callback);
  void FlushPendingMutations(const std::string& account_id, ReadCallback callback);
  void GetPendingMutationCount(ReadCallback callback);
  void ListPendingMutations(const std::string& account_id, ReadCallback callback);
  // === [W-C..W-K.Typed] ===
  void StartAllAccountSync(MailReadCallback callback);
  void SendEmail(const std::string& request_json, MailReadCallback callback);
  void SaveDraft(const std::string& request_json, MailReadCallback callback);
  void UpdateDraft(const std::string& draft_id, const std::string& request_json, MailReadCallback callback);
  void GetReplyContext(const std::string& email_id, MailReadCallback callback);
  void QueueEmail(const std::string& request_json, MailReadCallback callback);
  void ListOutbox(const std::string& account_id, MailReadCallback callback);
  void RetryOutboxItem(const std::string& item_id, MailReadCallback callback);
  void DeleteOutboxItem(const std::string& item_id, MailReadCallback callback);
  void FlushOutbox(MailReadCallback callback);
  void DownloadAttachment(const std::string& account_id, int64_t email_uid, const std::string& folder_id, const std::string& part_id, const std::string& filename, MailReadCallback callback);
  void ExtractOtp(const std::string& account_id, const std::string& folder_id, const std::string& query, int64_t max_age_seconds, MailReadCallback callback);
  void SnoozeEmail(const std::string& email_id, const std::string& snooze_until, MailReadCallback callback);
  void UnsnoozeEmail(const std::string& email_id, MailReadCallback callback);
  void ListSnoozedEmails(const std::string& account_id, MailReadCallback callback);
  void SetReminder(const std::string& email_id, const std::string& reminder_at, MailReadCallback callback);
  void ClearReminder(const std::string& email_id, MailReadCallback callback);
  void ListReminders(const std::string& account_id, MailReadCallback callback);
  void MuteThread(const std::string& account_id, const std::string& message_id, MailReadCallback callback);
  void UnmuteThread(const std::string& account_id, const std::string& message_id, MailReadCallback callback);
  void IsThreadMuted(const std::string& account_id, const std::string& message_id, MailReadCallback callback);
  void ListMutedThreads(const std::string& account_id, MailReadCallback callback);
  void FilterMutedMessageIds(const std::string& account_id, const std::string& message_ids_json, MailReadCallback callback);
  void PinEmail(const std::string& email_id, MailReadCallback callback);
  void UnpinEmail(const std::string& email_id, MailReadCallback callback);
  void ListPinnedEmails(const std::string& account_id, MailReadCallback callback);
  void CreateMailRule(const std::string& request_json, MailReadCallback callback);
  void UpdateMailRule(const std::string& request_json, MailReadCallback callback);
  void DeleteMailRule(const std::string& rule_id, MailReadCallback callback);
  void ListMailRules(const std::string& account_id, MailReadCallback callback);
  void ReorderMailRules(const std::string& account_id, const std::string& rule_ids_json, MailReadCallback callback);
  void ListLabels(const std::string& account_id, MailReadCallback callback);
  void CreateLabel(const std::string& request_json, MailReadCallback callback);
  void DeleteLabel(const std::string& id, MailReadCallback callback);
  void AddLabelToEmail(const std::string& email_id, const std::string& label_id, MailReadCallback callback);
  void RemoveLabelFromEmail(const std::string& email_id, const std::string& label_id, MailReadCallback callback);
  void ListEmailLabels(const std::string& email_id, MailReadCallback callback);
  void SaveSearch(const std::string& name, const std::string& query, const std::string& account_id, MailReadCallback callback);
  void ListSavedSearches(const std::string& account_id, MailReadCallback callback);
  void DeleteSavedSearch(const std::string& id, MailReadCallback callback);
  void ScheduleSend(const std::string& request_json, MailReadCallback callback);
  void CancelScheduledSend(const std::string& id, MailReadCallback callback);
  void ListScheduledSends(const std::string& account_id, MailReadCallback callback);
  void StartScheduler(MailReadCallback callback);
  void StopScheduler(MailReadCallback callback);
  void SearchContacts(const std::string& account_id, const std::string& query, int64_t limit, MailReadCallback callback);
  void ToggleVip(const std::string& contact_id, MailReadCallback callback);
  void ListVipContacts(const std::string& account_id, MailReadCallback callback);
  void PopulateContactsFromHistory(const std::string& account_id, MailReadCallback callback);
  void ListContactGroups(const std::string& account_id, MailReadCallback callback);
  void SearchContactGroups(const std::string& account_id, const std::string& query, MailReadCallback callback);
  void CreateContactGroup(const std::string& account_id, const std::string& name, const std::string& member_emails_json, MailReadCallback callback);
  void UpdateContactGroup(const std::string& group_id, const std::string& name, const std::string& member_emails_json, MailReadCallback callback);
  void DeleteContactGroup(const std::string& group_id, MailReadCallback callback);
  void ListSignatures(const std::string& account_id, MailReadCallback callback);
  void CreateSignature(const std::string& request_json, MailReadCallback callback);
  void UpdateSignature(const std::string& id, const std::string& request_json, MailReadCallback callback);
  void DeleteSignature(const std::string& id, MailReadCallback callback);
  void ListTemplates(MailReadCallback callback);
  void CreateTemplate(const std::string& request_json, MailReadCallback callback);
  void UpdateTemplate(const std::string& id, const std::string& request_json, MailReadCallback callback);
  void DeleteTemplate(const std::string& id, MailReadCallback callback);
  void GetEmailSummary(const std::string& account_id, const std::string& request_json, MailReadCallback callback);
  void GetReplyDraft(const std::string& account_id, const std::string& request_json, MailReadCallback callback);
  void AdjustTone(const std::string& request_json, MailReadCallback callback);
  void ClassifyEmail(const std::string& account_id, const std::string& request_json, MailReadCallback callback);
  void NaturalLanguageSearch(const std::string& request_json, MailReadCallback callback);
  void GetAiActionHistory(const std::string& account_id, int64_t limit, MailReadCallback callback);
  void SaveAiConfig(const std::string& config_json, MailReadCallback callback);
  void GetAiConfig(const std::string& feature, MailReadCallback callback);
  void DeleteAiConfig(const std::string& feature, MailReadCallback callback);
  void TestAiConnection(const std::string& config_json, MailReadCallback callback);
  void GetAutoDraftForEmail(const std::string& account_id, const std::string& email_id, MailReadCallback callback);
  void UpdateAutoDraftStatus(const std::string& draft_id, const std::string& status, MailReadCallback callback);
  void TranslateText(const std::string& text, const std::string& target_lang, const std::string& source_lang, MailReadCallback callback);
  void GeneratePgpKey(const std::string& request_json, MailReadCallback callback);
  void ImportPgpKey(const std::string& request_json, MailReadCallback callback);
  void ExportPgpKey(const std::string& key_id, bool include_private, MailReadCallback callback);
  void ListPgpKeys(const std::string& account_id, MailReadCallback callback);
  void DeletePgpKey(const std::string& key_id, MailReadCallback callback);
  void SetDefaultPgpKey(const std::string& account_id, const std::string& key_id, MailReadCallback callback);
  void EncryptEmailPgp(const std::string& request_json, MailReadCallback callback);
  void EncryptAttachmentPgp(const std::string& request_json, MailReadCallback callback);
  void DecryptEmailPgp(const std::string& request_json, MailReadCallback callback);
  void SignEmailPgp(const std::string& request_json, MailReadCallback callback);
  void VerifyEmailPgp(const std::string& request_json, MailReadCallback callback);
  void ImportSmimeIdentity(const std::string& request_json, MailReadCallback callback);
  void ListSmimeIdentities(const std::string& account_id, MailReadCallback callback);
  void DeleteSmimeIdentity(const std::string& id, MailReadCallback callback);
  void SetDefaultSmimeIdentity(const std::string& account_id, const std::string& identity_id, MailReadCallback callback);
  void ExportSmimeCert(const std::string& id, MailReadCallback callback);
  void SignEmailSmime(const std::string& request_json, MailReadCallback callback);
  void EncryptEmailSmime(const std::string& request_json, MailReadCallback callback);
  void DecryptEmailSmime(const std::string& request_json, MailReadCallback callback);
  void VerifyEmailSmime(const std::string& signed_body, MailReadCallback callback);
  void CleanupSmimeForAccount(const std::string& account_id, MailReadCallback callback);
  void ImportCalendarEvent(const std::string& account_id, const std::string& email_id, const std::string& ics_data, MailReadCallback callback);
  void ListCalendarEvents(const std::string& account_id, const std::string& from_date, const std::string& to_date, MailReadCallback callback);
  void GetCalendarEvent(const std::string& event_id, MailReadCallback callback);
  void UpdateRsvp(const std::string& event_id, const std::string& rsvp_status, MailReadCallback callback);
  void GenerateRsvpReply(const std::string& event_id, const std::string& rsvp_status, const std::string& account_email, MailReadCallback callback);
  void DeleteCalendarEvent(const std::string& event_id, const std::string& delete_scope, MailReadCallback callback);
  void AutoImportCalendarEvents(const std::string& account_id, const std::string& email_id, const std::string& body_html, const std::string& body_text, MailReadCallback callback);
  void CreateCalendarEvent(const std::string& request_json, MailReadCallback callback);
  void UpdateCalendarEvent(const std::string& request_json, MailReadCallback callback);
  void SearchCalendarEvents(const std::string& account_id, const std::string& query, int64_t limit, MailReadCallback callback);
  void CheckEventConflict(const std::string& account_id, const std::string& dtstart, const std::string& dtend, const std::string& exclude_event_id, MailReadCallback callback);
  void DuplicateCalendarEvent(const std::string& event_id, MailReadCallback callback);
  void GoogleCalendarMoveEvent(const std::string& account_id, const std::string& calendar_id, const std::string& event_id, const std::string& destination_calendar_id, MailReadCallback callback);
  void ExportCalendarIcs(const std::string& request_json, MailReadCallback callback);
  void ListCalendarCategories(const std::string& account_id, MailReadCallback callback);
  void CreateCalendarCategory(const std::string& account_id, const std::string& name, const std::string& color, MailReadCallback callback);
  void UpdateCalendarCategory(const std::string& id, const std::string& name, const std::string& color, MailReadCallback callback);
  void DeleteCalendarCategory(const std::string& id, MailReadCallback callback);
  void SyncGoogleCalendar(const std::string& account_id, bool _full_sync, MailReadCallback callback);
  void ListAccountCalendars(const std::string& account_id, MailReadCallback callback);
  void SetCalendarVisibility(const std::string& calendar_row_id, bool visible, MailReadCallback callback);
  void SubscribeHolidayCalendar(const std::string& account_id, const std::string& locale_code, MailReadCallback callback);
  void GoogleCalendarFreeBusy(const std::string& request_json, MailReadCallback callback);
  void GetAppSetting(const std::string& key, MailReadCallback callback);
  void SetAppSetting(const std::string& key, const std::string& value, MailReadCallback callback);
  void Md5Hash(const std::string& input, MailReadCallback callback);

  // === [W-C.Additional.Typed] ===
  void GetAccount(const std::string& account_id, MailReadCallback callback);
  void UpdateAccount(const std::string& request_json, MailReadCallback callback);

  // === [W-C.Additional.Refresh.Typed] ===
  void RefreshOAuthToken(const std::string& account_id, MailReadCallback callback);

  void CallBackend(const std::string& command, const std::string& args_json, ReadCallback callback);

  // Test-only: installs `launcher` as this service's launcher, replacing any
  // existing one, so unit tests can drive the read proxies against a fake
  // helper without spawning a process via EnsureHelperLaunched().
  void SetLauncherForTesting(std::unique_ptr<MahoMailHelperLauncher> launcher);

 private:
  // Returns the bound helper interface, or nullptr when the helper is missing,
  // unbound, or the launcher has given up after exceeding its crash-loop cap.
  mojom::MahoMailHelper* GetHelperOrNull();

  // Decrypts the browser's OSCrypt-encrypted BYOK keys from profile prefs and
  // pushes them to the helper (browser->helper->mail-core only; never to a
  // renderer). No-op without a profile or a ready encryptor.
  void PushBrowserAiKeys(mojom::MahoMailHelper* helper);

  // Unread-badge + new-mail-notification pipeline. All no-op without a profile.
  // RefreshMailBadge fetches the account's folders, recomputes the inbox unread
  // total, and writes it to the `maho.mail.unread_count` pref (summed across
  // accounts). When `notify` is true and notifications are enabled it also
  // surfaces an OS notification for the newest inbox message.
  void RefreshMailBadge(const std::string& account_id, bool notify);
  void RefreshBadgesForAccounts(const std::string& result_json);
  void OnFoldersForBadge(const std::string& account_id,
                         bool notify,
                         bool ok,
                         std::string result_json);
  void OnLatestInboxEmailForNotify(const std::string& account_id,
                                   bool ok,
                                   std::string result_json);
  void UpdateUnreadCountPref();

  // Per-account inbox unread counts, summed into the pref. Keyed by account id.
  std::map<std::string, int64_t> account_unread_counts_;

  void OnOsCryptReady(os_crypt_async::Encryptor encryptor);

  SEQUENCE_CHECKER(sequence_checker_);
  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<MahoMailHelperLauncher> launcher_;

  std::optional<os_crypt_async::Encryptor> encryptor_;

  struct PendingLaunch {
    std::string expected_version;
    std::string profile_path;
    base::FilePath crashpad_database;
  };
  std::optional<PendingLaunch> pending_launch_;
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<MahoMailService> weak_ptr_factory_{this};
};

// Process-wide hook used to surface OS notifications for newly arrived mail.
// The mail service invokes this instead of calling NotificationDisplayService
// directly, so the mail_service GN target does not depend on
// //chrome/browser/notifications (which forms a dependency cycle). The browser
// layer installs the concrete implementation at startup.
using MailNotificationCallback =
    base::RepeatingCallback<void(Profile* profile,
                                 const std::string& title,
                                 const std::string& body,
                                 const std::string& notification_id)>;
void SetMahoMailNotificationCallback(MailNotificationCallback callback);

// Parses a ListFolders JSON array and returns the summed unread_count across
// Inbox-type folders. When `out_inbox_folder_id` is non-null it receives the id
// of the first Inbox folder (cleared/empty if none). Pure free function so the
// sidebar badge computation is unit-testable without a Profile.
int64_t ComputeInboxUnreadForBadge(const std::string& folders_json,
                                   std::string* out_inbox_folder_id);

std::optional<bool> GetDesktopNotificationPreference(
    const std::string& behavior_prefs_json);

std::optional<bool> GetUnreadBadgePreference(
    const std::string& behavior_prefs_json);

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_H_
