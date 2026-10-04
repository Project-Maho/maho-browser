// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_PAGE_HANDLER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/files/file_path.h"

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"

#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/ai/maho_ffi_callback_handle.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

#include "maho/browser/mail_helper/maho_mail_service.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

class MahoMailPageHandler : public maho_mail::mojom::PageHandler,
                            public maho::MahoMailService::Observer {
 public:
  class Backend {
   public:
    virtual ~Backend() = default;
    virtual void GetAccount(
        const std::string& account_id,
        base::OnceCallback<void(bool, std::string)> callback) = 0;
    virtual void ReconnectAccount(
        const std::string& account_id,
        base::OnceCallback<void(bool, std::string)> callback) = 0;
    virtual void OAuthLoopbackSignIn(
        const std::string& provider,
        const std::string& options_json,
        base::OnceCallback<void(bool, std::string)> callback) = 0;
    virtual void ExportPgpKey(
        const std::string& key_id,
        bool include_private,
        base::OnceCallback<void(bool, std::string)> callback) = 0;
    virtual void CallBackend(
        const std::string& command,
        const std::string& args_json,
        base::OnceCallback<void(bool, std::string)> callback) = 0;
  };

  MahoMailPageHandler(
      mojo::PendingReceiver<maho_mail::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_mail::mojom::Page> page,
      Profile* profile,
      content::WebContents* web_contents);
  explicit MahoMailPageHandler(Backend* backend_for_testing);
  MahoMailPageHandler(const MahoMailPageHandler&) = delete;
  MahoMailPageHandler& operator=(const MahoMailPageHandler&) = delete;
  ~MahoMailPageHandler() override;

  // maho::MahoMailService::Observer:
  void OnAuthRequired(const std::string& account_id,
                      const std::string& provider,
                      const std::string& reason) override;
  void OnAuthRefreshSucceeded(const std::string& account_id) override;
  void OnAccountsChanged() override;
  void OnLifecycleChanged(maho::MahoMailService::LifecycleState state,
                          uint64_t generation) override;
  void OnSyncEvent(const std::string& event_type, const std::string& payload) override;
  void OnBackfillEvent(const std::string& event_type, const std::string& payload) override;
  void OnStatusChanged(const std::string& account_id, bool connected) override;
  void OnNewMail(const std::string& account_id,
                 const std::string& email_id,
                 const std::string& message_id,
                 const std::string& sender,
                 const std::string& subject,
                 uint64_t cursor,
                 uint64_t epoch) override;
  void OnMutation(const std::string& account_id, const std::string& payload) override;
  void OnOutbox(const std::string& account_id, const std::string& payload) override;
  void OnScheduler(const std::string& account_id, const std::string& payload) override;
  void OnAgentStream(const std::string& session_id, const std::string& chunk) override;
  void OnCalendar(const std::string& account_id, const std::string& payload) override;
  void OnImport(const std::string& payload) override;

  // maho_mail::mojom::PageHandler:
  void ListAccounts(ListAccountsCallback callback) override;
  void AddAccount(const std::string& request_json, AddAccountCallback callback) override;
  void TestConnection(const std::string& params_json, TestConnectionCallback callback) override;
  void DeleteAccount(const std::string& account_id, DeleteAccountCallback callback) override;
  void OAuthStartUrl(const std::string& provider,
                     const std::string& client_id,
                     const std::string& redirect_uri,
                     OAuthStartUrlCallback callback) override;
  void BeginOAuth(const std::string& provider,
                  const std::string& reauthorize_account_id,
                  BeginOAuthCallback callback) override;
  void OAuthComplete(const std::string& state,
                     const std::string& code,
                     OAuthCompleteCallback callback) override;
  void ReconnectAccount(const std::string& account_id, ReconnectAccountCallback callback) override;
  void CancelOAuth(const std::string& state, CancelOAuthCallback callback) override;
  void CloseOnboarding() override;

  void MarkRead(const std::string& email_id, MarkReadCallback callback) override;
  void MarkUnread(const std::string& email_id, MarkUnreadCallback callback) override;
  void ToggleStar(const std::string& email_id, ToggleStarCallback callback) override;
  void DeleteEmail(const std::string& email_id, DeleteEmailCallback callback) override;
  void MoveEmail(const std::string& email_id, const std::string& target_folder_id, MoveEmailCallback callback) override;
  void BatchMarkRead(const std::string& request_json, BatchMarkReadCallback callback) override;
  void BatchMarkUnread(const std::string& request_json, BatchMarkUnreadCallback callback) override;
  void BatchDelete(const std::string& request_json, BatchDeleteCallback callback) override;
  void BatchMove(const std::string& request_json, BatchMoveCallback callback) override;
  void BatchToggleStar(const std::string& request_json, BatchToggleStarCallback callback) override;
  void SyncFolders(const std::string& account_id, SyncFoldersCallback callback) override;
  void SyncFolder(const std::string& account_id, const std::string& folder_id, SyncFolderCallback callback) override;
  void CreateFolder(const std::string& account_id, const std::string& folder_name, CreateFolderCallback callback) override;
  void RenameFolder(const std::string& account_id, const std::string& folder_id, const std::string& new_name, RenameFolderCallback callback) override;
  void DeleteFolder(const std::string& account_id, const std::string& folder_id, DeleteFolderCallback callback) override;
  void GetFolderCounts(const std::string& account_id, GetFolderCountsCallback callback) override;
  void FlushPendingMutations(const std::string& account_id, FlushPendingMutationsCallback callback) override;
  void GetPendingMutationCount(GetPendingMutationCountCallback callback) override;
  void ListPendingMutations(const std::string& account_id, ListPendingMutationsCallback callback) override;

  // === [W-A] Read API WebUI Exposure ===
  void ListFolders(const std::string& account_id, ListFoldersCallback callback) override;
  void ListEmails(const std::string& account_id,
                  const std::string& folder_id,
                  int64_t limit,
                  int64_t offset,
                  ListEmailsCallback callback) override;
  void GetEmail(const std::string& email_id, GetEmailCallback callback) override;
  void SearchEmails(const std::string& query_json, SearchEmailsCallback callback) override;
  void ListThread(const std::string& account_id,
                  const std::string& message_id,
                  ListThreadCallback callback) override;
  // === [W-C..W-K.Typed] ===
  void StartAllAccountSync(StartAllAccountSyncCallback callback) override;
  void SendEmail(const std::string& request_json, SendEmailCallback callback) override;
  void SaveDraft(const std::string& request_json, SaveDraftCallback callback) override;
  void UpdateDraft(const std::string& draft_id, const std::string& request_json, UpdateDraftCallback callback) override;
  void GetReplyContext(const std::string& email_id, GetReplyContextCallback callback) override;
  void QueueEmail(const std::string& request_json, QueueEmailCallback callback) override;
  void ListOutbox(const std::string& account_id, ListOutboxCallback callback) override;
  void RetryOutboxItem(const std::string& item_id, RetryOutboxItemCallback callback) override;
  void DeleteOutboxItem(const std::string& item_id, DeleteOutboxItemCallback callback) override;
  void FlushOutbox(FlushOutboxCallback callback) override;
  void DownloadAttachment(const std::string& account_id, int64_t email_uid, const std::string& folder_id, const std::string& part_id, const std::string& filename, DownloadAttachmentCallback callback) override;
  void ExtractOtp(const std::string& account_id, const std::string& folder_id, const std::string& query, int64_t max_age_seconds, ExtractOtpCallback callback) override;
  void SnoozeEmail(const std::string& email_id, const std::string& snooze_until, SnoozeEmailCallback callback) override;
  void UnsnoozeEmail(const std::string& email_id, UnsnoozeEmailCallback callback) override;
  void ListSnoozedEmails(const std::string& account_id, ListSnoozedEmailsCallback callback) override;
  void SetReminder(const std::string& email_id, const std::string& reminder_at, SetReminderCallback callback) override;
  void ClearReminder(const std::string& email_id, ClearReminderCallback callback) override;
  void ListReminders(const std::string& account_id, ListRemindersCallback callback) override;
  void MuteThread(const std::string& account_id, const std::string& message_id, MuteThreadCallback callback) override;
  void UnmuteThread(const std::string& account_id, const std::string& message_id, UnmuteThreadCallback callback) override;
  void IsThreadMuted(const std::string& account_id, const std::string& message_id, IsThreadMutedCallback callback) override;
  void ListMutedThreads(const std::string& account_id, ListMutedThreadsCallback callback) override;
  void FilterMutedMessageIds(const std::string& account_id, const std::string& message_ids_json, FilterMutedMessageIdsCallback callback) override;
  void PinEmail(const std::string& email_id, PinEmailCallback callback) override;
  void UnpinEmail(const std::string& email_id, UnpinEmailCallback callback) override;
  void ListPinnedEmails(const std::string& account_id, ListPinnedEmailsCallback callback) override;
  void CreateMailRule(const std::string& request_json, CreateMailRuleCallback callback) override;
  void UpdateMailRule(const std::string& request_json, UpdateMailRuleCallback callback) override;
  void DeleteMailRule(const std::string& rule_id, DeleteMailRuleCallback callback) override;
  void ListMailRules(const std::string& account_id, ListMailRulesCallback callback) override;
  void ReorderMailRules(const std::string& account_id, const std::string& rule_ids_json, ReorderMailRulesCallback callback) override;
  void ListLabels(const std::string& account_id, ListLabelsCallback callback) override;
  void CreateLabel(const std::string& request_json, CreateLabelCallback callback) override;
  void DeleteLabel(const std::string& id, DeleteLabelCallback callback) override;
  void AddLabelToEmail(const std::string& email_id, const std::string& label_id, AddLabelToEmailCallback callback) override;
  void RemoveLabelFromEmail(const std::string& email_id, const std::string& label_id, RemoveLabelFromEmailCallback callback) override;
  void ListEmailLabels(const std::string& email_id, ListEmailLabelsCallback callback) override;
  void SaveSearch(const std::string& name, const std::string& query, const std::string& account_id, SaveSearchCallback callback) override;
  void ListSavedSearches(const std::string& account_id, ListSavedSearchesCallback callback) override;
  void DeleteSavedSearch(const std::string& id, DeleteSavedSearchCallback callback) override;
  void ScheduleSend(const std::string& request_json, ScheduleSendCallback callback) override;
  void CancelScheduledSend(const std::string& id, CancelScheduledSendCallback callback) override;
  void ListScheduledSends(const std::string& account_id, ListScheduledSendsCallback callback) override;
  void StartScheduler(StartSchedulerCallback callback) override;
  void StopScheduler(StopSchedulerCallback callback) override;
  void SearchContacts(const std::string& account_id, const std::string& query, int64_t limit, SearchContactsCallback callback) override;
  void ToggleVip(const std::string& contact_id, ToggleVipCallback callback) override;
  void ListVipContacts(const std::string& account_id, ListVipContactsCallback callback) override;
  void PopulateContactsFromHistory(const std::string& account_id, PopulateContactsFromHistoryCallback callback) override;
  void ListContactGroups(const std::string& account_id, ListContactGroupsCallback callback) override;
  void SearchContactGroups(const std::string& account_id, const std::string& query, SearchContactGroupsCallback callback) override;
  void CreateContactGroup(const std::string& account_id, const std::string& name, const std::string& member_emails_json, CreateContactGroupCallback callback) override;
  void UpdateContactGroup(const std::string& group_id, const std::string& name, const std::string& member_emails_json, UpdateContactGroupCallback callback) override;
  void DeleteContactGroup(const std::string& group_id, DeleteContactGroupCallback callback) override;
  void ListSignatures(const std::string& account_id, ListSignaturesCallback callback) override;
  void CreateSignature(const std::string& request_json, CreateSignatureCallback callback) override;
  void UpdateSignature(const std::string& id, const std::string& request_json, UpdateSignatureCallback callback) override;
  void DeleteSignature(const std::string& id, DeleteSignatureCallback callback) override;
  void ListTemplates(ListTemplatesCallback callback) override;
  void CreateTemplate(const std::string& request_json, CreateTemplateCallback callback) override;
  void UpdateTemplate(const std::string& id, const std::string& request_json, UpdateTemplateCallback callback) override;
  void DeleteTemplate(const std::string& id, DeleteTemplateCallback callback) override;
  void GetEmailSummary(const std::string& account_id, const std::string& request_json, GetEmailSummaryCallback callback) override;
  void GetReplyDraft(const std::string& account_id, const std::string& request_json, GetReplyDraftCallback callback) override;
  void AdjustTone(const std::string& request_json, AdjustToneCallback callback) override;
  void ClassifyEmail(const std::string& account_id, const std::string& request_json, ClassifyEmailCallback callback) override;
  void NaturalLanguageSearch(const std::string& request_json, NaturalLanguageSearchCallback callback) override;
  void GetAiActionHistory(const std::string& account_id, int64_t limit, GetAiActionHistoryCallback callback) override;
  void SaveAiConfig(const std::string& config_json, SaveAiConfigCallback callback) override;
  void GetAiConfig(const std::string& feature, GetAiConfigCallback callback) override;
  void DeleteAiConfig(const std::string& feature, DeleteAiConfigCallback callback) override;
  void TestAiConnection(const std::string& config_json, TestAiConnectionCallback callback) override;
  void GetAutoDraftForEmail(const std::string& account_id, const std::string& email_id, GetAutoDraftForEmailCallback callback) override;
  void UpdateAutoDraftStatus(const std::string& draft_id, const std::string& status, UpdateAutoDraftStatusCallback callback) override;
  void TranslateText(const std::string& text, const std::string& target_lang, const std::string& source_lang, TranslateTextCallback callback) override;
  void GeneratePgpKey(const std::string& request_json, GeneratePgpKeyCallback callback) override;
  void ImportPgpKey(const std::string& request_json, ImportPgpKeyCallback callback) override;
  void ExportPgpKey(const std::string& key_id, bool include_private, ExportPgpKeyCallback callback) override;
  void ListPgpKeys(const std::string& account_id, ListPgpKeysCallback callback) override;
  void DeletePgpKey(const std::string& key_id, DeletePgpKeyCallback callback) override;
  void SetDefaultPgpKey(const std::string& account_id, const std::string& key_id, SetDefaultPgpKeyCallback callback) override;
  void EncryptEmailPgp(const std::string& request_json, EncryptEmailPgpCallback callback) override;
  void EncryptAttachmentPgp(const std::string& request_json, EncryptAttachmentPgpCallback callback) override;
  void DecryptEmailPgp(const std::string& request_json, DecryptEmailPgpCallback callback) override;
  void SignEmailPgp(const std::string& request_json, SignEmailPgpCallback callback) override;
  void VerifyEmailPgp(const std::string& request_json, VerifyEmailPgpCallback callback) override;
  void ImportSmimeIdentity(const std::string& request_json, ImportSmimeIdentityCallback callback) override;
  void ListSmimeIdentities(const std::string& account_id, ListSmimeIdentitiesCallback callback) override;
  void DeleteSmimeIdentity(const std::string& id, DeleteSmimeIdentityCallback callback) override;
  void SetDefaultSmimeIdentity(const std::string& account_id, const std::string& identity_id, SetDefaultSmimeIdentityCallback callback) override;
  void ExportSmimeCert(const std::string& id, ExportSmimeCertCallback callback) override;
  void SignEmailSmime(const std::string& request_json, SignEmailSmimeCallback callback) override;
  void EncryptEmailSmime(const std::string& request_json, EncryptEmailSmimeCallback callback) override;
  void DecryptEmailSmime(const std::string& request_json, DecryptEmailSmimeCallback callback) override;
  void VerifyEmailSmime(const std::string& signed_body, VerifyEmailSmimeCallback callback) override;
  void CleanupSmimeForAccount(const std::string& account_id, CleanupSmimeForAccountCallback callback) override;
  void ImportCalendarEvent(const std::string& account_id, const std::string& email_id, const std::string& ics_data, ImportCalendarEventCallback callback) override;
  void ListCalendarEvents(const std::string& account_id, const std::string& from_date, const std::string& to_date, ListCalendarEventsCallback callback) override;
  void GetCalendarEvent(const std::string& event_id, GetCalendarEventCallback callback) override;
  void UpdateRsvp(const std::string& event_id, const std::string& rsvp_status, UpdateRsvpCallback callback) override;
  void GenerateRsvpReply(const std::string& event_id, const std::string& rsvp_status, const std::string& account_email, GenerateRsvpReplyCallback callback) override;
  void DeleteCalendarEvent(const std::string& event_id, const std::string& delete_scope, DeleteCalendarEventCallback callback) override;
  void AutoImportCalendarEvents(const std::string& account_id, const std::string& email_id, const std::string& body_html, const std::string& body_text, AutoImportCalendarEventsCallback callback) override;
  void CreateCalendarEvent(const std::string& request_json, CreateCalendarEventCallback callback) override;
  void UpdateCalendarEvent(const std::string& request_json, UpdateCalendarEventCallback callback) override;
  void SearchCalendarEvents(const std::string& account_id, const std::string& query, int64_t limit, SearchCalendarEventsCallback callback) override;
  void CheckEventConflict(const std::string& account_id, const std::string& dtstart, const std::string& dtend, const std::string& exclude_event_id, CheckEventConflictCallback callback) override;
  void DuplicateCalendarEvent(const std::string& event_id, DuplicateCalendarEventCallback callback) override;
  void GoogleCalendarMoveEvent(const std::string& account_id, const std::string& calendar_id, const std::string& event_id, const std::string& destination_calendar_id, GoogleCalendarMoveEventCallback callback) override;
  void ExportCalendarIcs(const std::string& request_json, ExportCalendarIcsCallback callback) override;
  void ListCalendarCategories(const std::string& account_id, ListCalendarCategoriesCallback callback) override;
  void CreateCalendarCategory(const std::string& account_id, const std::string& name, const std::string& color, CreateCalendarCategoryCallback callback) override;
  void UpdateCalendarCategory(const std::string& id, const std::string& name, const std::string& color, UpdateCalendarCategoryCallback callback) override;
  void DeleteCalendarCategory(const std::string& id, DeleteCalendarCategoryCallback callback) override;
  void SyncGoogleCalendar(const std::string& account_id, bool _full_sync, SyncGoogleCalendarCallback callback) override;
  void ListAccountCalendars(const std::string& account_id, ListAccountCalendarsCallback callback) override;
  void SetCalendarVisibility(const std::string& calendar_row_id, bool visible, SetCalendarVisibilityCallback callback) override;
  void SubscribeHolidayCalendar(const std::string& account_id, const std::string& locale_code, SubscribeHolidayCalendarCallback callback) override;
  void GoogleCalendarFreeBusy(const std::string& request_json, GoogleCalendarFreeBusyCallback callback) override;
  void GetAppSetting(const std::string& key, GetAppSettingCallback callback) override;
  void SetAppSetting(const std::string& key, const std::string& value, SetAppSettingCallback callback) override;
  void Md5Hash(const std::string& input, Md5HashCallback callback) override;

  // === [W-C.Additional.Typed] ===
  void GetAccount(const std::string& account_id, GetAccountCallback callback) override;
  void UpdateAccount(const std::string& request_json, UpdateAccountCallback callback) override;

  // === [W-C.OpenAttachment] ===
  void OpenAttachment(const std::string& capability_token,
                      OpenAttachmentCallback callback) override;
  void OnAttachmentCapabilityConsumed(OpenAttachmentCallback callback,
                                      std::unique_ptr<maho::MahoMailAttachmentRegistry::ConsumedAttachment>
                                          attachment,
                                      std::string error);
  void SaveAttachment(const std::string& capability_token,
                      SaveAttachmentCallback callback) override;
  void GetDownloadDir(GetDownloadDirCallback callback) override;

  // === [W-C.Additional.Refresh.Typed] ===
  void RefreshOAuthToken(const std::string& account_id, RefreshOAuthTokenCallback callback) override;

  void CallBackend(const std::string& command,
                   const std::string& args_json,
                   CallBackendCallback callback) override;

  // === [Browser UI Prefs Bridge] ===
  void GetBrowserUiPrefs(GetBrowserUiPrefsCallback callback) override;

 private:
  // Returns the profile service only while the canonical Mail pref is enabled.
  // This is the common fail-closed boundary for all backend requests.
  maho::MahoMailService* GetMailService();

  // PrefChangeRegistrar target for the canonical Mail enabled pref. Disabling
  // tears down all privileged handler state and closes both Mojo endpoints.
  void OnMailEnabledPrefChanged();
  void DisableMailSurface();

  // Builds the browser UI prefs JSON:
  // {"theme":"system"|"light"|"dark","density":"comfortable"|"compact"}.
  std::string BuildBrowserUiPrefsJson();

  // PrefChangeRegistrar target for prefs::kBrowserColorScheme (theme).
  void OnBrowserUiPrefChanged();

  // Coalesced sink for browser UI prefs pushes: rebuilds the JSON and fires
  // OnBrowserUiPrefsChanged only when it differs from the last emit, so a
  // theme and a density change resolving to the same snapshot don't
  // double-notify. All observation sources funnel through here.
  void MaybePushBrowserUiPrefs();

  // FFI thunk for maho-core's settings-change callback
  // (maho_core_register_settings_change_callback). Fires on an arbitrary
  // maho-core thread, so it MUST NOT touch page_/members directly: it recovers
  // a weak handle via the FfiCallbackHandle bridge and PostTasks
  // MaybePushBrowserUiPrefs() onto the owning sequence. `settings_json` is
  // ignored; MaybePushBrowserUiPrefs() re-reads authoritative state.
  static void OnSettingsChangedThunk(void* user_data,
                                     const char* settings_json);

  mojo::Receiver<maho_mail::mojom::PageHandler> receiver_;
  mojo::Remote<maho_mail::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<content::WebContents> web_contents_;
  raw_ptr<Backend> backend_for_testing_ = nullptr;

  PrefChangeRegistrar pref_registrar_;
  base::ScopedObservation<maho::MahoMailService,
                          maho::MahoMailService::Observer>
      mail_service_observation_{this};

  // Last browser UI prefs JSON pushed to the WebUI, used to coalesce/dedupe
  // redundant OnBrowserUiPrefsChanged emits. Empty until the first push.
  std::string last_browser_ui_prefs_json_;

  // Weak-lifetime bridge for maho-core's settings-change FFI callback, so
  // OnSettingsChangedThunk can recover this handler and PostTask to the owning
  // sequence without UAF if the handler is destroyed mid-callback.
  std::unique_ptr<maho::FfiCallbackHandle<MahoMailPageHandler>>
      settings_change_handle_;

  // Token returned by maho_core_register_settings_change_callback; 0 when not
  // registered. Unregistered in the destructor before the handle is cancelled.
  uint64_t settings_change_callback_token_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMailPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_PAGE_HANDLER_H_
