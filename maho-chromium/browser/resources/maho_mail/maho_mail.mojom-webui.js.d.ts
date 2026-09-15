// Copyright 2026 Maho Browser. All rights reserved.

export interface MailHandlerResult {
  readonly ok: boolean;
  readonly resultJson: string;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  removeListener(id: number): void;
  onLifecycleChanged: {
    addListener(listener: (state: string, generation: bigint) => void): number;
    removeListener(id: number): void;
  };
  onNewMail: {
    addListener(listener: (accountId: string) => void): number;
    removeListener(id: number): void;
  };
  onMutation: {
    addListener(listener: (accountId: string, payload: string) => void): number;
    removeListener(id: number): void;
  };
  onOutbox: {
    addListener(listener: (accountId: string, payload: string) => void): number;
    removeListener(id: number): void;
  };
  onScheduler: {
    addListener(listener: (accountId: string, payload: string) => void): number;
    removeListener(id: number): void;
  };
  onAgentStream: {
    addListener(listener: (sessionId: string, chunk: string) => void): number;
    removeListener(id: number): void;
  };
  onCalendar: {
    addListener(listener: (accountId: string, payload: string) => void): number;
    removeListener(id: number): void;
  };
  onImport: {
    addListener(listener: (payload: string) => void): number;
    removeListener(id: number): void;
  };
  onSyncEvent: {
    addListener(listener: (eventType: string, payload: string) => void): number;
    removeListener(id: number): void;
  };
  onBackfillEvent: {
    addListener(listener: (eventType: string, payload: string) => void): number;
    removeListener(id: number): void;
  };
  onStatusChanged: {
    addListener(listener: (accountId: string, connected: boolean) => void): number;
    removeListener(id: number): void;
  };
  onBrowserUiPrefsChanged: {
    addListener(listener: (prefsJson: string) => void): number;
    removeListener(id: number): void;
  };
  onAccountsChanged: {
    addListener(listener: () => void): number;
    removeListener(id: number): void;
  };
  onAuthRequired: {
    addListener(listener: (accountId: string, provider: string, reason: string) => void): number;
    removeListener(id: number): void;
  };
  onAuthRefreshSucceeded: {
    addListener(listener: (accountId: string) => void): number;
    removeListener(id: number): void;
  };
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  listAccounts(): Promise<MailHandlerResult>;
  addAccount(requestJson: string): Promise<MailHandlerResult>;
  testConnection(paramsJson: string): Promise<MailHandlerResult>;
  deleteAccount(accountId: string): Promise<MailHandlerResult>;
  oAuthStartUrl(provider: string, clientId: string, redirectUri: string): Promise<MailHandlerResult>;
  beginOAuth(provider: string, reauthorizeAccountId: string): Promise<{ ok: boolean; errorJson: string; state: string }>;
  oAuthComplete(state: string, code: string): Promise<MailHandlerResult>;
  reconnectAccount(accountId: string): Promise<MailHandlerResult>;
  cancelOAuth(state: string): Promise<{ accepted: boolean }>;
  closeOnboarding(): void;

  markRead(emailId: string): Promise<MailHandlerResult>;
  markUnread(emailId: string): Promise<MailHandlerResult>;
  toggleStar(emailId: string): Promise<MailHandlerResult>;
  deleteEmail(emailId: string): Promise<MailHandlerResult>;
  moveEmail(emailId: string, targetFolderId: string): Promise<MailHandlerResult>;
  batchMarkRead(requestJson: string): Promise<MailHandlerResult>;
  batchMarkUnread(requestJson: string): Promise<MailHandlerResult>;
  batchDelete(requestJson: string): Promise<MailHandlerResult>;
  batchMove(requestJson: string): Promise<MailHandlerResult>;
  batchToggleStar(requestJson: string): Promise<MailHandlerResult>;
  syncFolders(accountId: string): Promise<MailHandlerResult>;
  syncFolder(accountId: string, folderId: string): Promise<MailHandlerResult>;
  createFolder(accountId: string, folderName: string): Promise<MailHandlerResult>;
  renameFolder(accountId: string, folderId: string, newName: string): Promise<MailHandlerResult>;
  deleteFolder(accountId: string, folderId: string): Promise<MailHandlerResult>;
  getFolderCounts(accountId: string): Promise<MailHandlerResult>;
  flushPendingMutations(accountId: string): Promise<MailHandlerResult>;
  getPendingMutationCount(): Promise<MailHandlerResult>;
  listPendingMutations(accountId: string): Promise<MailHandlerResult>;

  // === [W-A] Read API WebUI Exposure ===
  listFolders(accountId: string): Promise<MailHandlerResult>;
  listEmails(accountId: string, folderId: string, limit: bigint, offset: bigint): Promise<MailHandlerResult>;
  getEmail(emailId: string): Promise<MailHandlerResult>;
  searchEmails(queryJson: string): Promise<MailHandlerResult>;
  listThread(accountId: string, messageId: string): Promise<MailHandlerResult>;
  // === [W-C..W-K.Typed] ===
  startAllAccountSync(): Promise<MailHandlerResult>;
  registerEventCallback(): Promise<MailHandlerResult>;
  sendEmail(request_json: string): Promise<MailHandlerResult>;
  saveDraft(request_json: string): Promise<MailHandlerResult>;
  updateDraft(draft_id: string, request_json: string): Promise<MailHandlerResult>;
  getReplyContext(email_id: string): Promise<MailHandlerResult>;
  queueEmail(request_json: string): Promise<MailHandlerResult>;
  listOutbox(account_id: string): Promise<MailHandlerResult>;
  retryOutboxItem(item_id: string): Promise<MailHandlerResult>;
  deleteOutboxItem(item_id: string): Promise<MailHandlerResult>;
  flushOutbox(): Promise<MailHandlerResult>;
  downloadAttachment(account_id: string, email_uid: bigint, folder_id: string, part_id: string, filename: string): Promise<MailHandlerResult>;
  extractOtp(account_id: string, folder_id: string, query: string, max_age_seconds: bigint): Promise<MailHandlerResult>;
  snoozeEmail(email_id: string, snooze_until: string): Promise<MailHandlerResult>;
  unsnoozeEmail(email_id: string): Promise<MailHandlerResult>;
  listSnoozedEmails(account_id: string): Promise<MailHandlerResult>;
  setReminder(email_id: string, reminder_at: string): Promise<MailHandlerResult>;
  clearReminder(email_id: string): Promise<MailHandlerResult>;
  listReminders(account_id: string): Promise<MailHandlerResult>;
  muteThread(account_id: string, message_id: string): Promise<MailHandlerResult>;
  unmuteThread(account_id: string, message_id: string): Promise<MailHandlerResult>;
  isThreadMuted(account_id: string, message_id: string): Promise<MailHandlerResult>;
  listMutedThreads(account_id: string): Promise<MailHandlerResult>;
  filterMutedMessageIds(account_id: string, message_ids_json: string): Promise<MailHandlerResult>;
  pinEmail(email_id: string): Promise<MailHandlerResult>;
  unpinEmail(email_id: string): Promise<MailHandlerResult>;
  listPinnedEmails(account_id: string): Promise<MailHandlerResult>;
  createMailRule(request_json: string): Promise<MailHandlerResult>;
  updateMailRule(request_json: string): Promise<MailHandlerResult>;
  deleteMailRule(rule_id: string): Promise<MailHandlerResult>;
  listMailRules(account_id: string): Promise<MailHandlerResult>;
  reorderMailRules(account_id: string, rule_ids_json: string): Promise<MailHandlerResult>;
  listLabels(account_id: string): Promise<MailHandlerResult>;
  createLabel(request_json: string): Promise<MailHandlerResult>;
  deleteLabel(id: string): Promise<MailHandlerResult>;
  addLabelToEmail(email_id: string, label_id: string): Promise<MailHandlerResult>;
  removeLabelFromEmail(email_id: string, label_id: string): Promise<MailHandlerResult>;
  listEmailLabels(email_id: string): Promise<MailHandlerResult>;
  saveSearch(name: string, query: string, account_id: string): Promise<MailHandlerResult>;
  listSavedSearches(account_id: string): Promise<MailHandlerResult>;
  deleteSavedSearch(id: string): Promise<MailHandlerResult>;
  scheduleSend(request_json: string): Promise<MailHandlerResult>;
  cancelScheduledSend(id: string): Promise<MailHandlerResult>;
  listScheduledSends(account_id: string): Promise<MailHandlerResult>;
  startScheduler(): Promise<MailHandlerResult>;
  stopScheduler(): Promise<MailHandlerResult>;
  searchContacts(account_id: string, query: string, limit: bigint): Promise<MailHandlerResult>;
  toggleVip(contact_id: string): Promise<MailHandlerResult>;
  listVipContacts(account_id: string): Promise<MailHandlerResult>;
  populateContactsFromHistory(account_id: string): Promise<MailHandlerResult>;
  listContactGroups(account_id: string): Promise<MailHandlerResult>;
  searchContactGroups(account_id: string, query: string): Promise<MailHandlerResult>;
  createContactGroup(account_id: string, name: string, member_emails_json: string): Promise<MailHandlerResult>;
  updateContactGroup(group_id: string, name: string, member_emails_json: string): Promise<MailHandlerResult>;
  deleteContactGroup(group_id: string): Promise<MailHandlerResult>;
  listSignatures(account_id: string): Promise<MailHandlerResult>;
  createSignature(request_json: string): Promise<MailHandlerResult>;
  updateSignature(id: string, request_json: string): Promise<MailHandlerResult>;
  deleteSignature(id: string): Promise<MailHandlerResult>;
  listTemplates(): Promise<MailHandlerResult>;
  createTemplate(request_json: string): Promise<MailHandlerResult>;
  updateTemplate(id: string, request_json: string): Promise<MailHandlerResult>;
  deleteTemplate(id: string): Promise<MailHandlerResult>;
  getEmailSummary(account_id: string, request_json: string): Promise<MailHandlerResult>;
  getReplyDraft(account_id: string, request_json: string): Promise<MailHandlerResult>;
  adjustTone(request_json: string): Promise<MailHandlerResult>;
  classifyEmail(account_id: string, request_json: string): Promise<MailHandlerResult>;
  naturalLanguageSearch(request_json: string): Promise<MailHandlerResult>;
  getAiActionHistory(account_id: string, limit: bigint): Promise<MailHandlerResult>;
  saveAiConfig(config_json: string): Promise<MailHandlerResult>;
  getAiConfig(feature: string): Promise<MailHandlerResult>;
  deleteAiConfig(feature: string): Promise<MailHandlerResult>;
  testAiConnection(config_json: string): Promise<MailHandlerResult>;
  listAgentSessions(account_id: string): Promise<MailHandlerResult>;
  getAgentMessages(account_id: string, session_id: string): Promise<MailHandlerResult>;
  createAgentSession(account_id: string): Promise<MailHandlerResult>;
  deleteAgentSession(account_id: string, session_id: string): Promise<MailHandlerResult>;
  sendAgentMessage(account_id: string, session_id: string, content: string): Promise<MailHandlerResult>;
  cancelAgentMessage(account_id: string, session_id: string): Promise<MailHandlerResult>;
  getAutoDraftForEmail(account_id: string, email_id: string): Promise<MailHandlerResult>;
  updateAutoDraftStatus(draft_id: string, status: string): Promise<MailHandlerResult>;
  translateText(text: string, target_lang: string, source_lang: string): Promise<MailHandlerResult>;
  generatePgpKey(request_json: string): Promise<MailHandlerResult>;
  importPgpKey(request_json: string): Promise<MailHandlerResult>;
  exportPgpKey(key_id: string, include_private: boolean): Promise<MailHandlerResult>;
  listPgpKeys(account_id: string): Promise<MailHandlerResult>;
  deletePgpKey(key_id: string): Promise<MailHandlerResult>;
  setDefaultPgpKey(account_id: string, key_id: string): Promise<MailHandlerResult>;
  encryptEmailPgp(request_json: string): Promise<MailHandlerResult>;
  encryptAttachmentPgp(request_json: string): Promise<MailHandlerResult>;
  decryptEmailPgp(request_json: string): Promise<MailHandlerResult>;
  signEmailPgp(request_json: string): Promise<MailHandlerResult>;
  verifyEmailPgp(request_json: string): Promise<MailHandlerResult>;
  importSmimeIdentity(request_json: string): Promise<MailHandlerResult>;
  listSmimeIdentities(account_id: string): Promise<MailHandlerResult>;
  deleteSmimeIdentity(id: string): Promise<MailHandlerResult>;
  setDefaultSmimeIdentity(account_id: string, identity_id: string): Promise<MailHandlerResult>;
  exportSmimeCert(id: string): Promise<MailHandlerResult>;
  signEmailSmime(request_json: string): Promise<MailHandlerResult>;
  encryptEmailSmime(request_json: string): Promise<MailHandlerResult>;
  decryptEmailSmime(request_json: string): Promise<MailHandlerResult>;
  verifyEmailSmime(signed_body: string): Promise<MailHandlerResult>;
  cleanupSmimeForAccount(account_id: string): Promise<MailHandlerResult>;
  importCalendarEvent(account_id: string, email_id: string, ics_data: string): Promise<MailHandlerResult>;
  listCalendarEvents(account_id: string, from_date: string, to_date: string): Promise<MailHandlerResult>;
  getCalendarEvent(event_id: string): Promise<MailHandlerResult>;
  updateRsvp(event_id: string, rsvp_status: string): Promise<MailHandlerResult>;
  generateRsvpReply(event_id: string, rsvp_status: string, account_email: string): Promise<MailHandlerResult>;
  deleteCalendarEvent(event_id: string, delete_scope: string): Promise<MailHandlerResult>;
  autoImportCalendarEvents(account_id: string, email_id: string, body_html: string, body_text: string): Promise<MailHandlerResult>;
  createCalendarEvent(request_json: string): Promise<MailHandlerResult>;
  updateCalendarEvent(request_json: string): Promise<MailHandlerResult>;
  searchCalendarEvents(account_id: string, query: string, limit: bigint): Promise<MailHandlerResult>;
  checkEventConflict(account_id: string, dtstart: string, dtend: string, exclude_event_id: string): Promise<MailHandlerResult>;
  duplicateCalendarEvent(event_id: string): Promise<MailHandlerResult>;
  googleCalendarMoveEvent(account_id: string, calendar_id: string, event_id: string, destination_calendar_id: string): Promise<MailHandlerResult>;
  exportCalendarIcs(request_json: string): Promise<MailHandlerResult>;
  listCalendarCategories(account_id: string): Promise<MailHandlerResult>;
  createCalendarCategory(account_id: string, name: string, color: string): Promise<MailHandlerResult>;
  updateCalendarCategory(id: string, name: string, color: string): Promise<MailHandlerResult>;
  deleteCalendarCategory(id: string): Promise<MailHandlerResult>;
  syncGoogleCalendar(account_id: string, _full_sync: boolean): Promise<MailHandlerResult>;
  listAccountCalendars(account_id: string): Promise<MailHandlerResult>;
  setCalendarVisibility(calendar_row_id: string, visible: boolean): Promise<MailHandlerResult>;
  subscribeHolidayCalendar(account_id: string, locale_code: string): Promise<MailHandlerResult>;
  googleCalendarFreeBusy(request_json: string): Promise<MailHandlerResult>;
  importEml(file_path: string, account_id: string, folder_id: string): Promise<MailHandlerResult>;
  importMbox(file_path: string, account_id: string, folder_id: string): Promise<MailHandlerResult>;
  exportEmailsEml(email_ids_json: string, output_dir: string): Promise<MailHandlerResult>;
  exportEmailsMbox(email_ids_json: string, output_path: string): Promise<MailHandlerResult>;
  exportFolderMbox(account_id: string, folder_id: string, output_path: string): Promise<MailHandlerResult>;
  exportMigrationCredentials(): Promise<MailHandlerResult>;
  getAppSetting(key: string): Promise<MailHandlerResult>;
  setAppSetting(key: string, value: string): Promise<MailHandlerResult>;
  md5Hash(input: string): Promise<MailHandlerResult>;

  // === [W-C.Additional.Typed] ===
  getAccount(accountId: string): Promise<MailHandlerResult>;
  updateAccount(requestJson: string): Promise<MailHandlerResult>;

  // === [W-C.OpenAttachment] ===
  openAttachment(capabilityToken: string): Promise<{ok: boolean, errorMsg: string}>;
  saveAttachment(capabilityToken: string): Promise<{ok: boolean, errorMsg: string}>;
  getDownloadDir(): Promise<{path: string}>;

  // === [W-C.Additional.Refresh.Typed] ===
  refreshOAuthToken(accountId: string): Promise<MailHandlerResult>;

  callBackend(command: string, argsJson: string): Promise<MailHandlerResult>;

  // === [Browser UI Prefs Bridge] ===
  getBrowserUiPrefs(): Promise<{prefsJson: string}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
