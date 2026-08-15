# Copyright 2026 Maho Browser. All rights reserved.

target_file = "/Users/indo/code/project/maho-workspace/maho-chromium/browser/resources/maho_mail/react/api/index.ts"

ts_content = """// Copyright 2026 Maho Browser. All rights reserved.

import { handler } from '../mojo_client.js';
import type * as Types from '../../../../../../mail/src/types';

// Helper to wrap PageHandler responses to match Tauri's return signatures
async function wrapMojoCall<T>(call: () => Promise<{ ok: boolean; resultJson: string }>, fallbackDesc: string): Promise<T> {
  const response = await call();
  if (!response.ok) {
    throw new Error(response.resultJson || `Mojo call failed for ${fallbackDesc}`);
  }
  try {
    return JSON.parse(response.resultJson) as T;
  } catch (e) {
    throw new Error(`Failed to parse Mojo response JSON: ${e instanceof Error ? e.message : e}`);
  }
}

async function callBackend<T>(command: string, args?: any): Promise<T> {
  const argsJson = args !== undefined ? JSON.stringify(args) : '{}';
  const response = await handler.callBackend(command, argsJson);
  if (!response.ok) {
    throw new Error(response.resultJson || `CallBackend failed for ${command}`);
  }
  try {
    return JSON.parse(response.resultJson) as T;
  } catch (e) {
    throw new Error(`Failed to parse CallBackend response JSON: ${e instanceof Error ? e.message : e}`);
  }
}

// === Supported Mojo API calls ===

export const createAccount = (request: Types.CreateAccountRequest): Promise<Types.Account> =>
  wrapMojoCall<Types.Account>(() => handler.addAccount(JSON.stringify(request)), 'createAccount');

export const listAccounts = (): Promise<Types.AccountSummary[]> =>
  wrapMojoCall<Types.AccountSummary[]>(() => handler.listAccounts(), 'listAccounts');

export const deleteAccount = (accountId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteAccount(accountId), 'deleteAccount');

export const testConnection = (request: Types.CreateAccountRequest): Promise<boolean> =>
  wrapMojoCall<boolean>(() => handler.testConnection(JSON.stringify(request)), 'testConnection');

export const reconnectAccount = (accountId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.reconnectAccount(accountId), 'reconnectAccount');

export const getOAuth2AuthUrl = async (config: Types.OAuth2Config, redirectUri: string): Promise<string> => {
  const res = await handler.oAuthStartUrl(config.provider, config.clientId || '', redirectUri);
  if (!res.ok) throw new Error(res.resultJson);
  const parsed = JSON.parse(res.resultJson) as { auth_url?: string };
  return parsed.auth_url || '';
};

export const startOAuth2 = async (provider: string): Promise<string> => {
  const res = await handler.beginOAuth(provider);
  if (!res.ok) throw new Error(res.errorJson);
  return '';
};

export const completeOAuth2 = (provider: string, authCode: string, stateParam: string): Promise<Types.OAuthAccount> =>
  wrapMojoCall<Types.OAuthAccount>(() => handler.oAuthComplete(stateParam, authCode), 'completeOAuth2');

export const listFolders = (accountId: string): Promise<Types.Folder[]> =>
  wrapMojoCall<Types.Folder[]>(() => handler.listFolders(accountId), 'listFolders');

export const listEmails = (
  accountId: string,
  folderId: string,
  limit?: number,
  offset?: number,
): Promise<Types.EmailSummary[]> =>
  wrapMojoCall<Types.EmailSummary[]>(() => handler.listEmails(accountId, folderId, BigInt(limit || 50), BigInt(offset || 0)), 'listEmails');

export const listThreadEmails = (accountId: string, messageId: string): Promise<Types.EmailSummary[]> =>
  wrapMojoCall<Types.EmailSummary[]>(() => handler.listThread(accountId, messageId), 'listThreadEmails');

export const getEmail = (emailId: string): Promise<Types.EmailDetail> =>
  wrapMojoCall<Types.EmailDetail>(() => handler.getEmail(emailId), 'getEmail');

export const searchEmails = (query: Types.SearchQuery): Promise<Types.SearchResult> =>
  wrapMojoCall<Types.SearchResult>(() => handler.searchEmails(JSON.stringify(query)), 'searchEmails');

// === CallBackend API mappings ===

export const getAccount = (accountId: string): Promise<Types.AccountResponse> =>
  callBackend<Types.AccountResponse>('GetAccount', { account_id: accountId });

export const updateAccount = (request: Types.UpdateAccountRequest): Promise<Types.AccountResponse> =>
  callBackend<Types.AccountResponse>('UpdateAccount', request);

export const exchangeOAuth2Code = (config: Types.OAuth2Config, code: string, redirectUri: string, stateParam: string): Promise<Types.OAuth2TokenResponse> =>
  callBackend<Types.OAuth2TokenResponse>('ExchangeOAuth2Code', { config, code, redirect_uri: redirectUri, state: stateParam });

export const refreshOAuth2Token = (accountId: string): Promise<string> =>
  callBackend<string>('RefreshOAuth2Token', { account_id: accountId });

export const downloadAttachment = (
  accountId: string,
  emailUid: number,
  folderId: string,
  partId: string,
  filename: string,
): Promise<string> =>
  callBackend<string>('DownloadAttachment', { account_id: accountId, email_uid: emailUid, folder_id: folderId, part_id: partId, filename });

export const openAttachment = (path: string): Promise<void> =>
  callBackend<void>('OpenAttachment', { path });

export const getForwardedAttachments = (accountId: string, emailUid: number, folderId: string): Promise<Types.ComposeAttachment[]> =>
  callBackend<Types.ComposeAttachment[]>('GetForwardedAttachments', { account_id: accountId, email_uid: emailUid, folder_id: folderId });

export const markRead = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.markRead(emailId), 'markRead');

export const markUnread = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.markUnread(emailId), 'markUnread');

export const toggleStar = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.toggleStar(emailId), 'toggleStar');

export const deleteEmail = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteEmail(emailId), 'deleteEmail');

export const moveEmail = (emailId: string, targetFolderId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.moveEmail(emailId, targetFolderId), 'moveEmail');

export const getFolderCounts = (accountId: string): Promise<Types.FolderCount[]> =>
  wrapMojoCall<Types.FolderCount[]>(() => handler.getFolderCounts(accountId), 'getFolderCounts');

export const adjustTone = (request: Types.ToneRequest): Promise<Types.ToneResponse> =>
  callBackend<Types.ToneResponse>('AdjustTone', request);

export function classifyEmail(request: Types.ClassifyRequest): Promise<Types.ClassifyResponse>;
export function classifyEmail(accountId: string, request: Types.ClassifyRequest): Promise<Types.ClassifyResponse>;
export function classifyEmail(arg1: string | Types.ClassifyRequest, arg2?: Types.ClassifyRequest): Promise<Types.ClassifyResponse> {
  if (typeof arg1 === 'string') {
    return callBackend<Types.ClassifyResponse>('ClassifyEmail', { account_id: arg1, ...arg2 });
  }
  return callBackend<Types.ClassifyResponse>('ClassifyEmail', arg1);
}

export const naturalLanguageSearch = (request: Types.NlSearchRequest): Promise<Types.SearchResult> =>
  callBackend<Types.SearchResult>('NaturalLanguageSearch', request);

export const sendEmail = (request: Types.ComposeEmailRequest): Promise<void> =>
  callBackend<void>('SendEmail', request);

export const saveDraft = (request: Types.ComposeEmailRequest): Promise<string> =>
  callBackend<string>('SaveDraft', request);

export const updateDraft = (draftId: string, request: Types.ComposeEmailRequest): Promise<void> =>
  callBackend<void>('UpdateDraft', { draft_id: draftId, ...request });

export const getReplyContext = (emailId: string): Promise<Types.ReplyContext> =>
  callBackend<Types.ReplyContext>('GetReplyContext', { email_id: emailId });

export function getEmailSummary(request: Types.SummaryRequest): Promise<Types.SummaryResponse>;
export function getEmailSummary(accountId: string, request: Types.SummaryRequest): Promise<Types.SummaryResponse>;
export function getEmailSummary(arg1: string | Types.SummaryRequest, arg2?: Types.SummaryRequest): Promise<Types.SummaryResponse> {
  if (typeof arg1 === 'string') {
    return callBackend<Types.SummaryResponse>('GetEmailSummary', { account_id: arg1, ...arg2 });
  }
  return callBackend<Types.SummaryResponse>('GetEmailSummary', arg1);
}

export function getReplyDraft(request: Types.ReplyDraftRequest): Promise<Types.ReplyDraftResponse>;
export function getReplyDraft(accountId: string, request: Types.ReplyDraftRequest): Promise<Types.ReplyDraftResponse>;
export function getReplyDraft(arg1: string | Types.ReplyDraftRequest, arg2?: Types.ReplyDraftRequest): Promise<Types.ReplyDraftResponse> {
  if (typeof arg1 === 'string') {
    return callBackend<Types.ReplyDraftResponse>('GetReplyDraft', { account_id: arg1, ...arg2 });
  }
  return callBackend<Types.ReplyDraftResponse>('GetReplyDraft', arg1);
}

export const saveAiConfig = (config: Types.AiConfig): Promise<void> =>
  callBackend<void>('SaveAiConfig', config);

export const getAiConfig = (feature?: string): Promise<Types.AiConfigResponse | null> =>
  callBackend<Types.AiConfigResponse | null>('GetAiConfig', { feature });

export const deleteAiConfig = (feature?: string): Promise<void> =>
  callBackend<void>('DeleteAiConfig', { feature });

export const testAiConnection = (config: Types.AiConfig): Promise<Types.TestAiConnectionResult> =>
  callBackend<Types.TestAiConnectionResult>('TestAiConnection', config);

export const translateText = (input: { text: string; targetLang: string; sourceLang?: string }): Promise<string> =>
  callBackend<string>('TranslateText', input);

export const translateLocal = (input: { text: string; targetLang: string; sourceLang?: string }): Promise<string> =>
  callBackend<string>('TranslateText', input);

export const translateSegments = (input: { segments: string[]; targetLang: string; sourceLang?: string }): Promise<string[]> =>
  callBackend<string[]>('TranslateText', input);

export const isTranslationModelDownloaded = (): Promise<boolean> =>
  Promise.resolve(false);

export const downloadTranslationModel = (): Promise<void> =>
  Promise.resolve();

export const unloadTranslationModel = (): Promise<void> =>
  Promise.resolve();

export const getAppSetting = (key: string): Promise<string | null> =>
  callBackend<string | null>('GetAppSetting', { key });

export const setAppSetting = (key: string, value: string): Promise<void> =>
  callBackend<void>('SetAppSetting', { key, value });

export const syncFolders = (accountId: string): Promise<Types.Folder[]> =>
  wrapMojoCall<Types.Folder[]>(() => handler.syncFolders(accountId), 'syncFolders');

export const createFolder = (accountId: string, folderName: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.createFolder(accountId, folderName), 'createFolder');

export const renameFolder = (accountId: string, folderId: string, newName: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.renameFolder(accountId, folderId, newName), 'renameFolder');

export const deleteFolder = (accountId: string, folderId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteFolder(accountId, folderId), 'deleteFolder');

export const syncFolder = (accountId: string, folderId: string): Promise<Types.EmailSummary[]> =>
  wrapMojoCall<Types.EmailSummary[]>(() => handler.syncFolder(accountId, folderId), 'syncFolder');

export const startAutoSync = (accountId: string | null, intervalSecs: number): Promise<void> =>
  callBackend<void>('StartAutoSync', { account_id: accountId, interval_secs: intervalSecs });

export const stopAutoSync = (): Promise<void> =>
  callBackend<void>('StopAutoSync');

export const signInWithOAuth2 = (provider: string): Promise<Types.OAuthAccount> =>
  callBackend<Types.OAuthAccount>('SignInWithOAuth2', { provider });

export const snoozeEmail = (request: Types.SnoozeRequest): Promise<void> =>
  callBackend<void>('SnoozeEmail', request);

export const unsnoozeEmail = (emailId: string): Promise<void> =>
  callBackend<void>('UnsnoozeEmail', { email_id: emailId });

export const listSnoozedEmails = (accountId?: string): Promise<Types.EmailSummary[]> =>
  callBackend<Types.EmailSummary[]>('ListSnoozedEmails', { account_id: accountId });

export const pinEmail = (emailId: string): Promise<void> =>
  callBackend<void>('PinEmail', { email_id: emailId });

export const unpinEmail = (emailId: string): Promise<void> =>
  callBackend<void>('UnpinEmail', { email_id: emailId });

export const listPinnedEmails = (accountId?: string): Promise<Types.EmailSummary[]> =>
  callBackend<Types.EmailSummary[]>('ListPinnedEmails', { account_id: accountId });

export const scheduleSend = (request: Types.SendLaterRequest): Promise<Types.SendLaterItem> =>
  callBackend<Types.SendLaterItem>('ScheduleSend', request);

export const cancelScheduledSend = (id: string): Promise<void> =>
  callBackend<void>('CancelScheduledSend', { item_id: id });

export const listScheduledSends = (accountId?: string): Promise<Types.SendLaterItem[]> =>
  callBackend<Types.SendLaterItem[]>('ListScheduledSends', { account_id: accountId });

export const searchContacts = (accountId: string, query: string, limit?: number): Promise<Types.Contact[]> =>
  callBackend<Types.Contact[]>('SearchContacts', { account_id: accountId, query, limit });

export function listContactGroups(accountId: string): Promise<Types.ContactGroup[]> {
  return callBackend<Types.ContactGroup[]>('ListContactGroups', { account_id: accountId });
}

export function searchContactGroups(accountId: string, query: string): Promise<Types.ContactGroup[]> {
  return callBackend<Types.ContactGroup[]>('SearchContactGroups', { account_id: accountId, query });
}

export function createContactGroup(accountId: string, name: string, memberEmails: string[]): Promise<Types.ContactGroup> {
  return callBackend<Types.ContactGroup>('CreateContactGroup', { account_id: accountId, name, member_emails: memberEmails });
}

export function updateContactGroup(groupId: string, name: string, memberEmails: string[]): Promise<Types.ContactGroup> {
  return callBackend<Types.ContactGroup>('UpdateContactGroup', { group_id: groupId, name, member_emails: memberEmails });
}

export function deleteContactGroup(groupId: string): Promise<void> {
  return callBackend<void>('DeleteContactGroup', { group_id: groupId });
}

export const listVipContacts = (accountId: string): Promise<Types.Contact[]> =>
  callBackend<Types.Contact[]>('ListVipContacts', { account_id: accountId });

export const toggleVip = (contactId: string): Promise<void> =>
  callBackend<void>('ToggleVip', { contact_id: contactId });

export const listSignatures = (accountId?: string): Promise<Types.Signature[]> =>
  callBackend<Types.Signature[]>('ListSignatures', { account_id: accountId });

export const createSignature = (request: Types.CreateSignatureRequest): Promise<Types.Signature> =>
  callBackend<Types.Signature>('CreateSignature', request);

export const updateSignature = (id: string, request: Types.CreateSignatureRequest): Promise<Types.Signature> =>
  callBackend<Types.Signature>('UpdateSignature', { id, ...request });

export const deleteSignature = (id: string): Promise<void> =>
  callBackend<void>('DeleteSignature', { id });

export const listTemplates = (): Promise<Types.EmailTemplate[]> =>
  callBackend<Types.EmailTemplate[]>('ListTemplates');

export const createTemplate = (request: Types.CreateTemplateRequest): Promise<Types.EmailTemplate> =>
  callBackend<Types.EmailTemplate>('CreateTemplate', request);

export const updateTemplate = (id: string, request: Types.CreateTemplateRequest): Promise<Types.EmailTemplate> =>
  callBackend<Types.EmailTemplate>('UpdateTemplate', { id, ...request });

export const deleteTemplate = (id: string): Promise<void> =>
  callBackend<void>('DeleteTemplate', { id });

export const listLabels = (accountId: string): Promise<Types.Label[]> =>
  callBackend<Types.Label[]>('ListLabels', { account_id: accountId });

export const createLabel = (request: Types.CreateLabelRequest): Promise<Types.Label> =>
  callBackend<Types.Label>('CreateLabel', request);

export const deleteLabel = (id: string): Promise<void> =>
  callBackend<void>('DeleteLabel', { label_id: id });

export const addLabelToEmail = (emailId: string, labelId: string): Promise<void> =>
  callBackend<void>('AddLabelToEmail', { email_id: emailId, label_id: labelId });

export const removeLabelFromEmail = (emailId: string, labelId: string): Promise<void> =>
  callBackend<void>('RemoveLabelFromEmail', { email_id: emailId, label_id: labelId });

export const listEmailLabels = (emailId: string): Promise<Types.Label[]> =>
  callBackend<Types.Label[]>('ListEmailLabels', { email_id: emailId });

export const setReminder = (request: Types.ReminderRequest): Promise<void> =>
  callBackend<void>('SetReminder', request);

export const clearReminder = (emailId: string): Promise<void> =>
  callBackend<void>('ClearReminder', { email_id: emailId });

export const listReminders = (accountId?: string): Promise<Types.EmailSummary[]> =>
  callBackend<Types.EmailSummary[]>('ListReminders', { account_id: accountId });

export const classifyEmailsSmart = (accountId: string): Promise<void> =>
  callBackend<void>('ClassifyEmailsSmart', { account_id: accountId });

export const listBySmartCategory = (category: Types.SmartCategory, accountId?: string, limit?: number, offset?: number): Promise<Types.EmailSummary[]> =>
  callBackend<Types.EmailSummary[]>('ListBySmartCategory', { category, account_id: accountId, limit, offset });

export const batchMarkRead = (request: Types.BatchActionRequest): Promise<void> =>
  wrapMojoCall<void>(() => handler.batchMarkRead(JSON.stringify(request)), 'batchMarkRead');

export const batchMarkUnread = (request: Types.BatchActionRequest): Promise<void> =>
  wrapMojoCall<void>(() => handler.batchMarkUnread(JSON.stringify(request)), 'batchMarkUnread');

export const batchDelete = (request: Types.BatchActionRequest): Promise<void> =>
  wrapMojoCall<void>(() => handler.batchDelete(JSON.stringify(request)), 'batchDelete');

export const batchMove = (request: Types.BatchMoveRequest): Promise<void> =>
  wrapMojoCall<void>(() => handler.batchMove(JSON.stringify(request)), 'batchMove');

export const batchToggleStar = (request: Types.BatchActionRequest): Promise<void> =>
  wrapMojoCall<void>(() => handler.batchToggleStar(JSON.stringify(request)), 'batchToggleStar');

export const md5Hash = (input: string): Promise<string> =>
  callBackend<string>('Md5Hash', { input });

export const startScheduler = (): Promise<void> =>
  callBackend<void>('StartScheduler');

export const stopScheduler = (): Promise<void> =>
  callBackend<void>('StopScheduler');

export const delegateEmail = (request: { emailId: string; accountId: string; delegateTo: string; note: string }): Promise<void> =>
  callBackend<void>('DelegateEmail', request);

export const sendMdnReceipt = (request: { emailId: string; accountId: string }): Promise<void> =>
  callBackend<void>('SendMdnReceipt', request);

export const getAiActionHistory = (accountId?: string, limit?: number): Promise<Types.AiActionLog[]> =>
  callBackend<Types.AiActionLog[]>('GetAiActionHistory', { account_id: accountId, limit });

export const listMailRules = (accountId: string): Promise<Types.MailRule[]> =>
  callBackend<Types.MailRule[]>('ListMailRules', { account_id: accountId });

export const createMailRule = (rule: Types.CreateRuleRequest): Promise<Types.MailRule> =>
  callBackend<Types.MailRule>('CreateMailRule', rule);

export const updateMailRule = (rule: Types.UpdateRuleRequest): Promise<Types.MailRule> =>
  callBackend<Types.MailRule>('UpdateMailRule', rule);

export const deleteMailRule = (ruleId: string): Promise<void> =>
  callBackend<void>('DeleteMailRule', { rule_id: ruleId });

export const reorderMailRules = (accountId: string, ruleIds: string[]): Promise<void> =>
  callBackend<void>('ReorderMailRules', { account_id: accountId, rule_ids: ruleIds });

export const flushOutbox = (): Promise<number> =>
  callBackend<number>('FlushOutbox');

export const importEml = (filePath: string, accountId: string, folderId: string): Promise<Types.Email> =>
  callBackend<Types.Email>('ImportEml', { file_path: filePath, account_id: accountId, folder_id: folderId });

export const importMbox = (filePath: string, accountId: string, folderId: string): Promise<Types.ImportResult> =>
  callBackend<Types.ImportResult>('ImportMbox', { file_path: filePath, account_id: accountId, folder_id: folderId });

export const exportEmailsEml = (emailIds: string[], outputDir: string): Promise<number> =>
  callBackend<number>('ExportEmailsEml', { email_ids: emailIds, output_dir: outputDir });

export const exportEmailsMbox = (emailIds: string[], outputPath: string): Promise<number> =>
  callBackend<number>('ExportEmailsMbox', { email_ids: emailIds, output_path: outputPath });

export const exportFolderMbox = (accountId: string, folderId: string, outputPath: string): Promise<number> =>
  callBackend<number>('ExportFolderMbox', { account_id: accountId, folder_id: folderId, output_path: outputPath });

export const generatePgpKey = (accountId: string, email: string, name: string, keyType: string): Promise<Types.PgpKeyInfo> =>
  callBackend<Types.PgpKeyInfo>('GeneratePgpKey', { account_id: accountId, email, name, key_type: keyType });

export const importPgpKey = (accountId: string, keyData: string): Promise<Types.PgpKeyInfo> =>
  callBackend<Types.PgpKeyInfo>('ImportPgpKey', { account_id: accountId, key_data: keyData });

export const exportPgpKey = (keyId: string, includePrivate: boolean): Promise<string> =>
  callBackend<string>('ExportPgpKey', { key_id: keyId, include_private: includePrivate });

export const listPgpKeys = (accountId: string): Promise<Types.PgpKeyInfo[]> =>
  callBackend<Types.PgpKeyInfo[]>('ListPgpKeys', { account_id: accountId });

export const deletePgpKey = (keyId: string): Promise<void> =>
  callBackend<void>('DeletePgpKey', { key_id: keyId });

export const setDefaultPgpKey = (accountId: string, keyId: string): Promise<void> =>
  callBackend<void>('SetDefaultPgpKey', { account_id: accountId, key_id: keyId });

export const encryptEmailPgp = (accountId: string, recipientEmails: string[], plaintext: string): Promise<string> =>
  callBackend<string>('EncryptEmailPgp', { account_id: accountId, recipient_emails: recipientEmails, plaintext });

export const encryptAttachmentPgp = (accountId: string, recipientEmails: string[], plaintext: number[]): Promise<number[]> =>
  callBackend<number[]>('EncryptAttachmentPgp', { account_id: accountId, recipient_emails: recipientEmails, plaintext });

export const decryptEmailPgp = (accountId: string, ciphertext: string): Promise<string> =>
  callBackend<string>('DecryptEmailPgp', { account_id: accountId, ciphertext });

export const signEmailPgp = (accountId: string, message: string): Promise<string> =>
  callBackend<string>('SignEmailPgp', { account_id: accountId, message });

export const verifyEmailPgp = (senderEmail: string, message: string, signature: string): Promise<Types.PgpVerifyResult> =>
  callBackend<Types.PgpVerifyResult>('VerifyEmailPgp', { sender_email: senderEmail, message, signature });

export const muteThread = (accountId: string, messageId: string): Promise<void> =>
  callBackend<void>('MuteThread', { account_id: accountId, message_id: messageId });

export const unmuteThread = (accountId: string, messageId: string): Promise<void> =>
  callBackend<void>('UnmuteThread', { account_id: accountId, message_id: messageId });

export const isThreadMuted = (accountId: string, messageId: string): Promise<boolean> =>
  callBackend<boolean>('IsThreadMuted', { account_id: accountId, message_id: messageId });

export const listMutedThreads = (accountId: string): Promise<Types.MutedThread[]> =>
  callBackend<Types.MutedThread[]>('ListMutedThreads', { account_id: accountId });

export const filterMutedMessageIds = (accountId: string, messageIds: string[]): Promise<string[]> =>
  callBackend<string[]>('FilterMutedMessageIds', { account_id: accountId, message_ids: messageIds });

export const saveSearch = (name: string, query: string, accountId?: string): Promise<Types.SavedSearch> =>
  callBackend<Types.SavedSearch>('SaveSearch', { name, query, account_id: accountId });

export const listSavedSearches = (accountId?: string): Promise<Types.SavedSearch[]> =>
  callBackend<Types.SavedSearch[]>('ListSavedSearches', { account_id: accountId });

export const deleteSavedSearch = (id: string): Promise<void> =>
  callBackend<void>('DeleteSavedSearch', { search_id: id });

export const importSmimeIdentity = (accountId: string, p12Data: number[], password: string): Promise<Types.SmimeIdentity> =>
  callBackend<Types.SmimeIdentity>('ImportSmimeIdentity', { account_id: accountId, p12_data: p12Data, password });

export const listSmimeIdentities = (accountId: string): Promise<Types.SmimeIdentity[]> =>
  callBackend<Types.SmimeIdentity[]>('ListSmimeIdentities', { account_id: accountId });

export const deleteSmimeIdentity = (id: string): Promise<void> =>
  callBackend<void>('DeleteSmimeIdentity', { id });

export const setDefaultSmimeIdentity = (accountId: string, identityId: string): Promise<void> =>
  callBackend<void>('SetDefaultSmimeIdentity', { account_id: accountId, identity_id: identityId });

export const exportSmimeCert = (id: string): Promise<string> =>
  callBackend<string>('ExportSmimeCert', { id });

export const signEmailSmime = (accountId: string, body: string): Promise<string> =>
  callBackend<string>('SignEmailSmime', { account_id: accountId, body });

export const encryptEmailSmime = (accountId: string, recipientCertsPem: string[], body: string): Promise<string> =>
  callBackend<string>('EncryptEmailSmime', { account_id: accountId, recipient_certs_pem: recipientCertsPem, body });

export const decryptEmailSmime = (accountId: string, encryptedBody: string): Promise<string> =>
  callBackend<string>('DecryptEmailSmime', { account_id: accountId, encrypted_body: encryptedBody });

export const verifyEmailSmime = (signedBody: string): Promise<Types.SmimeVerifyResult> =>
  callBackend<Types.SmimeVerifyResult>('VerifyEmailSmime', { signed_body: signedBody });

export const cleanupSmimeForAccount = (accountId: string): Promise<void> =>
  callBackend<void>('CleanupSmimeForAccount', { account_id: accountId });

export const startIdleMonitor = (accountId: string): Promise<void> =>
  callBackend<void>('StartIdleMonitor', { account_id: accountId });

export const stopIdleMonitor = (accountId: string): Promise<void> =>
  callBackend<void>('StopIdleMonitor', { account_id: accountId });

export const flushPendingMutations = (accountId: string): Promise<number> =>
  wrapMojoCall<number>(() => handler.flushPendingMutations(accountId), 'flushPendingMutations');

export const getPendingMutationCount = (): Promise<number> =>
  wrapMojoCall<number>(() => handler.getPendingMutationCount(), 'getPendingMutationCount');

export const listPendingMutations = (accountId: string): Promise<Types.PendingMutation[]> =>
  wrapMojoCall<Types.PendingMutation[]>(() => handler.listPendingMutations(accountId), 'listPendingMutations');

export const queueEmail = (request: Types.QueueEmailRequest): Promise<Types.OutboxItem> =>
  callBackend<Types.OutboxItem>('QueueEmail', request);

export const listOutbox = (accountId?: string): Promise<Types.OutboxItem[]> =>
  callBackend<Types.OutboxItem[]>('ListOutbox', { account_id: accountId });

export const checkForAppUpdate = (): Promise<any> =>
  Promise.resolve({ available: false });

export const relaunchApp = (): Promise<void> =>
  Promise.resolve();

export const listCalendarEvents = (
  accountId: string,
  fromDate?: string,
  toDate?: string,
): Promise<Types.CalendarEvent[]> =>
  callBackend<Types.CalendarEvent[]>('ListCalendarEvents', { account_id: accountId, from_date: fromDate, to_date: toDate });

export const searchCalendarEvents = (accountId: string, query: string, limit?: number): Promise<Types.CalendarEvent[]> =>
  callBackend<Types.CalendarEvent[]>('SearchCalendarEvents', { account_id: accountId, query, limit });

export const getCalendarEvent = (eventId: string): Promise<Types.CalendarEvent> =>
  callBackend<Types.CalendarEvent>('GetCalendarEvent', { event_id: eventId });

export const createCalendarEvent = (request: Types.CreateCalendarEventRequest): Promise<Types.CalendarEvent> =>
  callBackend<Types.CalendarEvent>('CreateCalendarEvent', request);

export const updateCalendarEvent = (request: Types.UpdateCalendarEventRequest): Promise<Types.CalendarEvent> =>
  callBackend<Types.CalendarEvent>('UpdateCalendarEvent', request);

export const updateRsvp = (eventId: string, rsvpStatus: string): Promise<Types.CalendarEvent> =>
  callBackend<Types.CalendarEvent>('UpdateRsvp', { event_id: eventId, rsvp_status: rsvpStatus });

export const generateRsvpReply = (eventId: string, rsvpStatus: string, accountEmail: string): Promise<string> =>
  callBackend<string>('GenerateRsvpReply', { event_id: eventId, rsvp_status: rsvpStatus, account_email: accountEmail });

export const deleteCalendarEvent = (eventId: string, deleteScope?: "single" | "all"): Promise<void> =>
  callBackend<void>('DeleteCalendarEvent', { event_id: eventId, delete_scope: deleteScope });

export const snoozeCalendarEvent = (eventId: string, minutes: number): Promise<void> =>
  callBackend<void>('SnoozeCalendarEvent', { event_id: eventId, minutes });

export const autoImportCalendarEvents = (
  accountId: string,
  emailId: string,
  bodyHtml?: string,
  bodyText?: string,
): Promise<Types.CalendarEvent[]> =>
  callBackend<Types.CalendarEvent[]>('AutoImportCalendarEvents', { account_id: accountId, email_id: emailId, body_html: bodyHtml, body_text: bodyText });

export const syncGoogleCalendar = (accountId: string): Promise<Types.GoogleCalendarSyncResult> =>
  callBackend<Types.GoogleCalendarSyncResult>('SyncGoogleCalendar', { account_id: accountId });

export const googleCalendarFreeBusy = (accountId: string, emails: string[], timeMin: string, timeMax: string): Promise<any> =>
  callBackend<any>('GoogleCalendarFreeBusy', { account_id: accountId, emails, time_min: timeMin, time_max: timeMax });

export const googleCalendarMoveEvent = (
  accountId: string,
  calendarId: string,
  eventId: string,
  destinationCalendarId: string,
): Promise<any> =>
  callBackend<any>('GoogleCalendarMoveEvent', { account_id: accountId, calendar_id: calendarId, event_id: eventId, destination_calendar_id: destinationCalendarId });

export const listAccountCalendars = (accountId: string): Promise<Types.GoogleCalendarEntry[]> =>
  callBackend<Types.GoogleCalendarEntry[]>('ListAccountCalendars', { account_id: accountId });

export const setCalendarVisibility = (calendarRowId: string, visible: boolean): Promise<void> =>
  callBackend<void>('SetCalendarVisibility', { calendar_row_id: calendarRowId, visible });

export const subscribeHolidayCalendar = (accountId: string, localeCode: string): Promise<Types.GoogleCalendarSyncResult> =>
  callBackend<Types.GoogleCalendarSyncResult>('SubscribeHolidayCalendar', { account_id: accountId, locale_code: localeCode });

export const listAllPendingMutations = async (): Promise<Types.PendingMutation[]> => {
  return [];
};

export const listAgentSessions = (accountId: string): Promise<Types.AgentChatSession[]> =>
  callBackend<Types.AgentChatSession[]>('ListAgentSessions', { account_id: accountId });

export const getAgentMessages = (accountId: string, sessionId: string): Promise<Types.AgentChatMessage[]> =>
  callBackend<Types.AgentChatMessage[]>('GetAgentMessages', { account_id: accountId, session_id: sessionId });

export const createAgentSession = (accountId: string): Promise<Types.AgentChatSession> =>
  callBackend<Types.AgentChatSession>('CreateAgentSession', { account_id: accountId });

export const deleteAgentSession = (accountId: string, sessionId: string): Promise<void> =>
  callBackend<void>('DeleteAgentSession', { account_id: accountId, session_id: sessionId });

export const sendAgentMessage = (accountId: string, sessionId: string, content: string): Promise<Types.AgentChatMessage> =>
  callBackend<Types.AgentChatMessage>('SendAgentMessage', { account_id: accountId, session_id: sessionId, content });

export const cancelAgentMessage = (accountId: string, sessionId: string): Promise<void> =>
  callBackend<void>('CancelAgentMessage', { account_id: accountId, session_id: sessionId });

export function getAutoDraftForEmail(emailId: string): Promise<Types.AutoDraft | null>;
export function getAutoDraftForEmail(accountId: string, emailId: string): Promise<Types.AutoDraft | null>;
export function getAutoDraftForEmail(arg1: string, arg2?: string): Promise<Types.AutoDraft | null> {
  if (arg2) {
    return callBackend<Types.AutoDraft | null>('GetAutoDraftForEmail', { account_id: arg1, email_id: arg2 });
  }
  return callBackend<Types.AutoDraft | null>('GetAutoDraftForEmail', { email_id: arg1 });
}

export const updateAutoDraftStatus = (draftId: string, status: "accepted" | "dismissed"): Promise<void> =>
  callBackend<void>('UpdateAutoDraftStatus', { draft_id: draftId, status });

export const checkEventConflict = (
  accountId: string,
  dtstart: string,
  dtend: string,
  excludeEventId?: string,
): Promise<Types.CalendarEvent[]> =>
  callBackend<Types.CalendarEvent[]>('CheckEventConflict', { account_id: accountId, dtstart, dtend, exclude_event_id: excludeEventId });

export const duplicateCalendarEvent = (eventId: string): Promise<Types.CalendarEvent> =>
  callBackend<Types.CalendarEvent>('DuplicateCalendarEvent', { event_id: eventId });

export const exportCalendarIcs = (
  accountId: string,
  calendarIds?: string[],
  fromDate?: string,
  toDate?: string,
): Promise<string> =>
  callBackend<string>('ExportCalendarIcs', { account_id: accountId, calendar_ids: calendarIds, from_date: fromDate, to_date: toDate });

export const listCalendarCategories = (accountId: string): Promise<Types.CalendarCategory[]> =>
  callBackend<Types.CalendarCategory[]>('ListCalendarCategories', { account_id: accountId });

export const createCalendarCategory = (accountId: string, name: string, color: string): Promise<Types.CalendarCategory> =>
  callBackend<Types.CalendarCategory>('CreateCalendarCategory', { account_id: accountId, name, color });

export const updateCalendarCategory = (id: string, name: string, color: string): Promise<void> =>
  callBackend<void>('UpdateCalendarCategory', { id, name, color });

export const deleteCalendarCategory = (id: string): Promise<void> =>
  callBackend<void>('DeleteCalendarCategory', { id });
"""

with open(target_file, "w") as f:
    f.write(ts_content)
print("api/index.ts stubs updated to CallBackend routes!")
