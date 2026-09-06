// Copyright 2026 Maho Browser. All rights reserved.

import { handler } from '../mojo_client.js';
import { isBrowserTranslatorAvailable, browserTranslate, browserTranslateSegments } from '../browser_translator.js';
import type * as Types from '../mail_types';

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

async function callBackend<T>(command: string, args?: unknown): Promise<T> {
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

export const testConnection = async (request: Types.CreateAccountRequest): Promise<boolean> => {
  const response = await wrapMojoCall<{ok: boolean}>(
    () => handler.testConnection(JSON.stringify(request)), 'testConnection');
  return response.ok;
};

export const reconnectAccount = (accountId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.reconnectAccount(accountId), 'reconnectAccount');

export const getOAuth2AuthUrl = async (config: Types.OAuth2Config, redirectUri: string): Promise<string> => {
  const res = await handler.oAuthStartUrl(config.provider, config.client_id || '', redirectUri);
  if (!res.ok) throw new Error(res.resultJson);
  const parsed = JSON.parse(res.resultJson) as { auth_url?: string };
  return parsed.auth_url || '';
};

export const startOAuth2 = async (
  provider: string,
  reauthorizeAccountId = '',
): Promise<string> => {
  const res = await handler.beginOAuth(provider, reauthorizeAccountId);
  if (!res.ok) throw new Error(res.errorJson);
  return res.state;
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
  wrapMojoCall<Types.AccountResponse>(() => handler.getAccount(accountId), 'getAccount');

export const updateAccount = (request: Types.UpdateAccountRequest): Promise<Types.AccountResponse> =>
  wrapMojoCall<Types.AccountResponse>(() => handler.updateAccount(JSON.stringify(request)), 'updateAccount');

export const refreshOAuth2Token = (accountId: string): Promise<string> =>
  wrapMojoCall<string>(() => handler.refreshOAuthToken(accountId), 'refreshOAuth2Token');

export const downloadAttachment = (
  accountId: string,
  emailUid: number,
  folderId: string,
  partId: string,
  filename: string,
): Promise<string> =>
  wrapMojoCall<string>(() => handler.downloadAttachment(accountId, BigInt(emailUid || 0), folderId, partId, filename), 'downloadAttachment');

export const openAttachment = async (capabilityToken: string): Promise<void> => {
  const res = await handler.openAttachment(capabilityToken);
  if (!res.ok) throw new Error(res.errorMsg || 'openAttachment failed');
};

export const saveAttachment = async (capabilityToken: string): Promise<void> => {
  const res = await handler.saveAttachment(capabilityToken);
  if (!res.ok) throw new Error(res.errorMsg || 'saveAttachment failed');
};

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
  wrapMojoCall<Types.ToneResponse>(() => handler.adjustTone(JSON.stringify(request)), 'adjustTone');

export function classifyEmail(request: Types.ClassifyRequest): Promise<Types.ClassifyResponse>;
export function classifyEmail(accountId: string, request: Types.ClassifyRequest): Promise<Types.ClassifyResponse>;
export function classifyEmail(arg1: string | Types.ClassifyRequest, arg2?: Types.ClassifyRequest): Promise<Types.ClassifyResponse> {
  if (typeof arg1 === 'string') {
    return wrapMojoCall<Types.ClassifyResponse>(() => handler.classifyEmail(arg1, arg2 ? JSON.stringify(arg2) : '{}'), 'classifyEmail');
  }
  return callBackend<Types.ClassifyResponse>('ClassifyEmail', arg1);
}

export const naturalLanguageSearch = (request: Types.NlSearchRequest): Promise<Types.SearchResult> =>
  wrapMojoCall<Types.SearchResult>(() => handler.naturalLanguageSearch(JSON.stringify(request)), 'naturalLanguageSearch');

export const sendEmail = async (request: Types.ComposeEmailRequest): Promise<Types.SendEmailResult> => {
  const result = await wrapMojoCall<unknown>(() => handler.sendEmail(JSON.stringify(request)), 'sendEmail');
  if (typeof result !== 'object' || result === null || !('status' in result) ||
      (result.status !== 'sent' && result.status !== 'queued' && result.status !== 'uncertain')) {
    throw new Error('Delivery could not be confirmed. Verify with the recipient before sending again.');
  }
  return { status: result.status };
};

export const saveDraft = (request: Types.ComposeEmailRequest): Promise<string> =>
  wrapMojoCall<string>(() => handler.saveDraft(JSON.stringify(request)), 'saveDraft');

export const updateDraft = (draftId: string, request: Types.ComposeEmailRequest): Promise<void> =>
  wrapMojoCall<void>(() => handler.updateDraft(draftId, JSON.stringify(request)), 'updateDraft');

export const getReplyContext = (emailId: string): Promise<Types.ReplyContext> =>
  wrapMojoCall<Types.ReplyContext>(() => handler.getReplyContext(emailId), 'getReplyContext');

export function getEmailSummary(request: Types.SummaryRequest): Promise<Types.SummaryResponse>;
export function getEmailSummary(accountId: string, request: Types.SummaryRequest): Promise<Types.SummaryResponse>;
export function getEmailSummary(arg1: string | Types.SummaryRequest, arg2?: Types.SummaryRequest): Promise<Types.SummaryResponse> {
  if (typeof arg1 === 'string') {
    return wrapMojoCall<Types.SummaryResponse>(() => handler.getEmailSummary(arg1, arg2 ? JSON.stringify(arg2) : '{}'), 'getEmailSummary');
  }
  return callBackend<Types.SummaryResponse>('GetEmailSummary', arg1);
}

export function getReplyDraft(request: Types.ReplyDraftRequest): Promise<Types.ReplyDraftResponse>;
export function getReplyDraft(accountId: string, request: Types.ReplyDraftRequest): Promise<Types.ReplyDraftResponse>;
export function getReplyDraft(arg1: string | Types.ReplyDraftRequest, arg2?: Types.ReplyDraftRequest): Promise<Types.ReplyDraftResponse> {
  if (typeof arg1 === 'string') {
    return wrapMojoCall<Types.ReplyDraftResponse>(() => handler.getReplyDraft(arg1, arg2 ? JSON.stringify(arg2) : '{}'), 'getReplyDraft');
  }
  return callBackend<Types.ReplyDraftResponse>('GetReplyDraft', arg1);
}

export const saveAiConfig = (config: Types.AiConfig): Promise<void> =>
  wrapMojoCall<void>(() => handler.saveAiConfig(JSON.stringify(config)), 'saveAiConfig');

export const getAiConfig = (feature?: string): Promise<Types.AiConfigResponse | null> =>
  wrapMojoCall<Types.AiConfigResponse | null>(() => handler.getAiConfig(feature ?? ''), 'getAiConfig');

export const deleteAiConfig = (feature?: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteAiConfig(feature ?? ''), 'deleteAiConfig');

export const testAiConnection = (config: Types.AiConfig): Promise<Types.TestAiConnectionResult> =>
  wrapMojoCall<Types.TestAiConnectionResult>(() => handler.testAiConnection(JSON.stringify(config)), 'testAiConnection');

export const translateText = (input: { text: string; targetLang: string; sourceLang?: string }): Promise<string> =>
  browserTranslate(input.text, input.targetLang, input.sourceLang);

export const translateLocal = translateText;

export const translateSegments = (input: { segments: string[]; targetLang: string; sourceLang?: string }): Promise<string[]> =>
  browserTranslateSegments(input.segments, input.targetLang, input.sourceLang);

export const isTranslationModelDownloaded = (): Promise<boolean> =>
  Promise.resolve(isBrowserTranslatorAvailable());

export const downloadTranslationModel = (): Promise<void> =>
  Promise.resolve();

export const unloadTranslationModel = (): Promise<void> =>
  Promise.resolve();

export const getAppSetting = (key: string): Promise<string | null> =>
  wrapMojoCall<string | null>(() => handler.getAppSetting(key), 'getAppSetting');

export const setAppSetting = (key: string, value: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.setAppSetting(key, value), 'setAppSetting');

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

export const startAutoSync = (accountId: string | null, intervalMinutes: number): Promise<void> =>
  callBackend<void>('StartAutoSync', { account_id: accountId, interval_minutes: intervalMinutes });

export const stopAutoSync = (): Promise<void> =>
  callBackend<void>('StopAutoSync');

export const snoozeEmail = (request: Types.SnoozeRequest): Promise<void> =>
  callBackend<void>('SnoozeEmail', request);

export const unsnoozeEmail = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.unsnoozeEmail(emailId), 'unsnoozeEmail');

export const listSnoozedEmails = (accountId?: string): Promise<Types.EmailSummary[]> =>
  wrapMojoCall<Types.EmailSummary[]>(() => handler.listSnoozedEmails(accountId ?? ''), 'listSnoozedEmails');

export const pinEmail = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.pinEmail(emailId), 'pinEmail');

export const unpinEmail = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.unpinEmail(emailId), 'unpinEmail');

export const listPinnedEmails = (accountId?: string): Promise<Types.EmailSummary[]> =>
  wrapMojoCall<Types.EmailSummary[]>(() => handler.listPinnedEmails(accountId ?? ''), 'listPinnedEmails');

export const scheduleSend = (request: Types.SendLaterRequest): Promise<Types.SendLaterItem> =>
  wrapMojoCall<Types.SendLaterItem>(() => handler.scheduleSend(JSON.stringify(request)), 'scheduleSend');

export const cancelScheduledSend = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.cancelScheduledSend(id), 'cancelScheduledSend');

export const listScheduledSends = (accountId?: string): Promise<Types.SendLaterItem[]> =>
  wrapMojoCall<Types.SendLaterItem[]>(() => handler.listScheduledSends(accountId ?? ''), 'listScheduledSends');

export const searchContacts = (accountId: string, query: string, limit?: number): Promise<Types.Contact[]> =>
  wrapMojoCall<Types.Contact[]>(() => handler.searchContacts(accountId, query, BigInt(limit || 0)), 'searchContacts');

export function listContactGroups(accountId: string): Promise<Types.ContactGroup[]> {
  return wrapMojoCall<Types.ContactGroup[]>(() => handler.listContactGroups(accountId), 'listContactGroups');
}

export function searchContactGroups(accountId: string, query: string): Promise<Types.ContactGroup[]> {
  return wrapMojoCall<Types.ContactGroup[]>(() => handler.searchContactGroups(accountId, query), 'searchContactGroups');
}

export function createContactGroup(accountId: string, name: string, memberEmails: string[]): Promise<Types.ContactGroup> {
  return wrapMojoCall<Types.ContactGroup>(() => handler.createContactGroup(accountId, name, JSON.stringify(memberEmails)), 'createContactGroup');
}

export function updateContactGroup(groupId: string, name: string, memberEmails: string[]): Promise<Types.ContactGroup> {
  return wrapMojoCall<Types.ContactGroup>(() => handler.updateContactGroup(groupId, name, JSON.stringify(memberEmails)), 'updateContactGroup');
}

export function deleteContactGroup(groupId: string): Promise<void> {
  return wrapMojoCall<void>(() => handler.deleteContactGroup(groupId), 'deleteContactGroup');
}

export const listVipContacts = (accountId: string): Promise<Types.Contact[]> =>
  wrapMojoCall<Types.Contact[]>(() => handler.listVipContacts(accountId), 'listVipContacts');

export const toggleVip = (contactId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.toggleVip(contactId), 'toggleVip');

export const listSignatures = (accountId?: string): Promise<Types.Signature[]> =>
  wrapMojoCall<Types.Signature[]>(() => handler.listSignatures(accountId ?? ''), 'listSignatures');

export const createSignature = (request: Types.CreateSignatureRequest): Promise<Types.Signature> =>
  wrapMojoCall<Types.Signature>(() => handler.createSignature(JSON.stringify(request)), 'createSignature');

export const updateSignature = (id: string, request: Types.CreateSignatureRequest): Promise<Types.Signature> =>
  wrapMojoCall<Types.Signature>(() => handler.updateSignature(id, JSON.stringify(request)), 'updateSignature');

export const deleteSignature = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteSignature(id), 'deleteSignature');

export const listTemplates = (): Promise<Types.EmailTemplate[]> =>
  wrapMojoCall<Types.EmailTemplate[]>(() => handler.listTemplates(), 'listTemplates');

export const createTemplate = (request: Types.CreateTemplateRequest): Promise<Types.EmailTemplate> =>
  wrapMojoCall<Types.EmailTemplate>(() => handler.createTemplate(JSON.stringify(request)), 'createTemplate');

export const updateTemplate = (id: string, request: Types.CreateTemplateRequest): Promise<Types.EmailTemplate> =>
  wrapMojoCall<Types.EmailTemplate>(() => handler.updateTemplate(id, JSON.stringify(request)), 'updateTemplate');

export const deleteTemplate = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteTemplate(id), 'deleteTemplate');

export const listLabels = (accountId: string): Promise<Types.Label[]> =>
  wrapMojoCall<Types.Label[]>(() => handler.listLabels(accountId), 'listLabels');

export const createLabel = (request: Types.CreateLabelRequest): Promise<Types.Label> =>
  wrapMojoCall<Types.Label>(() => handler.createLabel(JSON.stringify(request)), 'createLabel');

export const deleteLabel = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteLabel(id), 'deleteLabel');

export const addLabelToEmail = (emailId: string, labelId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.addLabelToEmail(emailId, labelId), 'addLabelToEmail');

export const removeLabelFromEmail = (emailId: string, labelId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.removeLabelFromEmail(emailId, labelId), 'removeLabelFromEmail');

export const listEmailLabels = (emailId: string): Promise<Types.Label[]> =>
  wrapMojoCall<Types.Label[]>(() => handler.listEmailLabels(emailId), 'listEmailLabels');

export const setReminder = (request: Types.ReminderRequest): Promise<void> =>
  callBackend<void>('SetReminder', request);

export const clearReminder = (emailId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.clearReminder(emailId), 'clearReminder');

export const listReminders = (accountId?: string): Promise<Types.EmailSummary[]> =>
  wrapMojoCall<Types.EmailSummary[]>(() => handler.listReminders(accountId ?? ''), 'listReminders');

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
  wrapMojoCall<string>(() => handler.md5Hash(JSON.stringify({ input })), 'md5Hash');

export const startScheduler = (): Promise<void> =>
  wrapMojoCall<void>(() => handler.startScheduler(), 'startScheduler');

export const stopScheduler = (): Promise<void> =>
  wrapMojoCall<void>(() => handler.stopScheduler(), 'stopScheduler');

export const delegateEmail = (request: { emailId: string; accountId: string; delegateTo: string; note: string }): Promise<void> =>
  callBackend<void>('DelegateEmail', request);

export const sendMdnReceipt = (request: { emailId: string; accountId: string }): Promise<void> =>
  callBackend<void>('SendMdnReceipt', request);

export const getAiActionHistory = (accountId?: string, limit?: number): Promise<Types.AiActionLog[]> =>
  wrapMojoCall<Types.AiActionLog[]>(() => handler.getAiActionHistory(accountId ?? '', BigInt(limit || 0)), 'getAiActionHistory');

export const listMailRules = (accountId: string): Promise<Types.MailRule[]> =>
  wrapMojoCall<Types.MailRule[]>(() => handler.listMailRules(accountId), 'listMailRules');

export const createMailRule = (rule: Types.CreateRuleRequest): Promise<Types.MailRule> =>
  wrapMojoCall<Types.MailRule>(() => handler.createMailRule(JSON.stringify(rule)), 'createMailRule');

export const updateMailRule = (rule: Types.UpdateRuleRequest): Promise<Types.MailRule> =>
  wrapMojoCall<Types.MailRule>(() => handler.updateMailRule(JSON.stringify(rule)), 'updateMailRule');

export const deleteMailRule = (ruleId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteMailRule(ruleId), 'deleteMailRule');

export const reorderMailRules = (accountId: string, ruleIds: string[]): Promise<void> =>
  wrapMojoCall<void>(() => handler.reorderMailRules(accountId, JSON.stringify(ruleIds)), 'reorderMailRules');

export const flushOutbox = (): Promise<number> =>
  wrapMojoCall<number>(() => handler.flushOutbox(), 'flushOutbox');

// Content-based import for the Chromium WebUI port where file paths are unavailable.
// Accepts { content_base64, filename, account_id, folder_id }; wired via callBackend.
export const importEmlContent = (contentBase64: string, filename: string, accountId: string, folderId: string): Promise<Types.Email> =>
  callBackend<Types.Email>('ImportEmlContent', { content_base64: contentBase64, filename, account_id: accountId, folder_id: folderId });

export const importMboxContent = (contentBase64: string, filename: string, accountId: string, folderId: string): Promise<Types.ImportResult> =>
  callBackend<Types.ImportResult>('ImportMboxContent', { content_base64: contentBase64, filename, account_id: accountId, folder_id: folderId });

export const generatePgpKey = (accountId: string, email: string, name: string, keyType: string): Promise<Types.PgpKeyInfo> =>
  wrapMojoCall<Types.PgpKeyInfo>(() => handler.generatePgpKey(JSON.stringify({ account_id: accountId, email, name, key_type: keyType })), 'generatePgpKey');

export const importPgpKey = (accountId: string, keyData: string): Promise<Types.PgpKeyInfo> =>
  wrapMojoCall<Types.PgpKeyInfo>(() => handler.importPgpKey(JSON.stringify({ account_id: accountId, key_data: keyData })), 'importPgpKey');

export const exportPgpKey = (keyId: string, includePrivate: boolean): Promise<string> =>
  wrapMojoCall<{ key_data: string }>(() => handler.exportPgpKey(keyId, includePrivate), 'exportPgpKey')
    .then(response => response.key_data);

export const listPgpKeys = (accountId: string): Promise<Types.PgpKeyInfo[]> =>
  wrapMojoCall<Types.PgpKeyInfo[]>(() => handler.listPgpKeys(accountId), 'listPgpKeys');

export const deletePgpKey = (keyId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deletePgpKey(keyId), 'deletePgpKey');

export const setDefaultPgpKey = (accountId: string, keyId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.setDefaultPgpKey(accountId, keyId), 'setDefaultPgpKey');

export const encryptEmailPgp = (accountId: string, recipientEmails: string[], plaintext: string): Promise<string> =>
  wrapMojoCall<{ armored: string }>(() => handler.encryptEmailPgp(JSON.stringify({ account_id: accountId, recipient_emails: recipientEmails, plaintext })), 'encryptEmailPgp')
    .then(response => response.armored);

export const encryptAttachmentPgp = (accountId: string, recipientEmails: string[], plaintext: number[]): Promise<number[]> =>
  wrapMojoCall<{ encrypted_data: string }>(() => handler.encryptAttachmentPgp(JSON.stringify({
    account_id: accountId,
    recipient_emails: recipientEmails,
    plaintext: btoa(plaintext.map(byte => String.fromCharCode(byte)).join('')),
  })), 'encryptAttachmentPgp')
    .then(response => Array.from(atob(response.encrypted_data), byte => byte.charCodeAt(0)));

export const decryptEmailPgp = (accountId: string, ciphertext: string): Promise<string> =>
  wrapMojoCall<{ plaintext: string }>(() => handler.decryptEmailPgp(JSON.stringify({ account_id: accountId, ciphertext })), 'decryptEmailPgp')
    .then(response => response.plaintext);

export const signEmailPgp = (accountId: string, message: string): Promise<string> =>
  wrapMojoCall<{ armored: string }>(() => handler.signEmailPgp(JSON.stringify({ account_id: accountId, message })), 'signEmailPgp')
    .then(response => response.armored);

export const verifyEmailPgp = (senderEmail: string, message: string, signature: string): Promise<Types.PgpVerifyResult> =>
  wrapMojoCall<Types.PgpVerifyResult>(() => handler.verifyEmailPgp(JSON.stringify({ sender_email: senderEmail, message, signature })), 'verifyEmailPgp');

export const muteThread = (accountId: string, messageId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.muteThread(accountId, messageId), 'muteThread');

export const unmuteThread = (accountId: string, messageId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.unmuteThread(accountId, messageId), 'unmuteThread');

export const isThreadMuted = (accountId: string, messageId: string): Promise<boolean> =>
  wrapMojoCall<boolean>(() => handler.isThreadMuted(accountId, messageId), 'isThreadMuted');

export const listMutedThreads = (accountId: string): Promise<Types.MutedThread[]> =>
  wrapMojoCall<Types.MutedThread[]>(() => handler.listMutedThreads(accountId), 'listMutedThreads');

export const filterMutedMessageIds = (accountId: string, messageIds: string[]): Promise<string[]> =>
  wrapMojoCall<string[]>(() => handler.filterMutedMessageIds(accountId, JSON.stringify(messageIds)), 'filterMutedMessageIds');

export const saveSearch = (name: string, query: string, accountId?: string): Promise<Types.SavedSearch> =>
  wrapMojoCall<Types.SavedSearch>(() => handler.saveSearch(name, query, accountId ?? ''), 'saveSearch');

export const listSavedSearches = (accountId?: string): Promise<Types.SavedSearch[]> =>
  wrapMojoCall<Types.SavedSearch[]>(() => handler.listSavedSearches(accountId ?? ''), 'listSavedSearches');

export const deleteSavedSearch = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteSavedSearch(id), 'deleteSavedSearch');

export const importSmimeIdentity = (accountId: string, p12Data: number[], password: string): Promise<Types.SmimeIdentity> =>
  wrapMojoCall<Types.SmimeIdentity>(() => handler.importSmimeIdentity(JSON.stringify({ account_id: accountId, p12_data: btoa(p12Data.map(byte => String.fromCharCode(byte)).join('')), password })), 'importSmimeIdentity');

export const listSmimeIdentities = (accountId: string): Promise<Types.SmimeIdentity[]> =>
  wrapMojoCall<Types.SmimeIdentity[]>(() => handler.listSmimeIdentities(accountId), 'listSmimeIdentities');

export const deleteSmimeIdentity = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteSmimeIdentity(id), 'deleteSmimeIdentity');

export const setDefaultSmimeIdentity = (accountId: string, identityId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.setDefaultSmimeIdentity(accountId, identityId), 'setDefaultSmimeIdentity');

export const exportSmimeCert = (id: string): Promise<string> =>
  wrapMojoCall<{ cert_pem: string }>(() => handler.exportSmimeCert(id), 'exportSmimeCert')
    .then(response => response.cert_pem);

export const signEmailSmime = (accountId: string, body: string): Promise<string> =>
  wrapMojoCall<{ smime: string }>(() => handler.signEmailSmime(JSON.stringify({ account_id: accountId, body })), 'signEmailSmime')
    .then(response => response.smime);

export const encryptEmailSmime = (request: {
  account_id: string;
  recipient_emails: string[];
  recipient_certs_pem: string[];
  body: string;
  body_html?: string;
  attachments?: Types.ComposeAttachment[];
  sign: boolean;
}): Promise<string> =>
  wrapMojoCall<{ smime: string }>(() => handler.encryptEmailSmime(JSON.stringify(request)), 'encryptEmailSmime')
    .then(response => response.smime);

export const decryptEmailSmime = (accountId: string, encryptedBody: string): Promise<string> =>
  wrapMojoCall<{ decrypted: string }>(() => handler.decryptEmailSmime(JSON.stringify({ account_id: accountId, encrypted_body: encryptedBody })), 'decryptEmailSmime')
    .then(response => response.decrypted);

export const verifyEmailSmime = (signedBody: string): Promise<Types.SmimeVerifyResult> =>
  wrapMojoCall<Types.SmimeVerifyResult>(() => handler.verifyEmailSmime(signedBody), 'verifyEmailSmime');

export const cleanupSmimeForAccount = (accountId: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.cleanupSmimeForAccount(accountId), 'cleanupSmimeForAccount');

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
  wrapMojoCall<Types.OutboxItem>(() => handler.queueEmail(JSON.stringify(request)), 'queueEmail');

export const listOutbox = (accountId?: string): Promise<Types.OutboxItem[]> =>
  wrapMojoCall<Types.OutboxItem[]>(() => handler.listOutbox(accountId ?? ''), 'listOutbox');

export const listCalendarEvents = (
  accountId: string,
  fromDate?: string,
  toDate?: string,
): Promise<Types.CalendarEvent[]> =>
  wrapMojoCall<Types.CalendarEvent[]>(() => handler.listCalendarEvents(accountId, fromDate ?? '', toDate ?? ''), 'listCalendarEvents');

export const searchCalendarEvents = (accountId: string, query: string, limit?: number): Promise<Types.CalendarEvent[]> =>
  wrapMojoCall<Types.CalendarEvent[]>(() => handler.searchCalendarEvents(accountId, query, BigInt(limit || 0)), 'searchCalendarEvents');

export const getCalendarEvent = (eventId: string): Promise<Types.CalendarEvent> =>
  wrapMojoCall<Types.CalendarEvent>(() => handler.getCalendarEvent(eventId), 'getCalendarEvent');

export const createCalendarEvent = (request: Types.CreateCalendarEventRequest): Promise<Types.CalendarEvent> =>
  wrapMojoCall<Types.CalendarEvent>(() => handler.createCalendarEvent(JSON.stringify(request)), 'createCalendarEvent');

export const updateCalendarEvent = (request: Types.UpdateCalendarEventRequest): Promise<Types.CalendarEvent> =>
  wrapMojoCall<Types.CalendarEvent>(() => handler.updateCalendarEvent(JSON.stringify(request)), 'updateCalendarEvent');

export const updateRsvp = (eventId: string, rsvpStatus: string): Promise<Types.CalendarEvent> =>
  wrapMojoCall<Types.CalendarEvent>(() => handler.updateRsvp(eventId, rsvpStatus), 'updateRsvp');

export const generateRsvpReply = (eventId: string, rsvpStatus: string, accountEmail: string): Promise<string> =>
  wrapMojoCall<string>(() => handler.generateRsvpReply(eventId, rsvpStatus, accountEmail), 'generateRsvpReply');

export const deleteCalendarEvent = (eventId: string, deleteScope?: "single" | "all"): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteCalendarEvent(eventId, deleteScope ?? 'single'), 'deleteCalendarEvent');

export const snoozeCalendarEvent = (eventId: string, minutes: number): Promise<void> =>
  callBackend<void>('SnoozeCalendarEvent', { event_id: eventId, minutes });

export const autoImportCalendarEvents = (
  accountId: string,
  emailId: string,
  bodyHtml?: string,
  bodyText?: string,
): Promise<Types.CalendarEvent[]> =>
  wrapMojoCall<Types.CalendarEvent[]>(() => handler.autoImportCalendarEvents(accountId, emailId, bodyHtml ?? '', bodyText ?? ''), 'autoImportCalendarEvents');

export const syncGoogleCalendar = (accountId: string): Promise<Types.GoogleCalendarSyncResult> =>
  wrapMojoCall<Types.GoogleCalendarSyncResult>(() => handler.syncGoogleCalendar(accountId, false), 'syncGoogleCalendar');

export const googleCalendarFreeBusy = (
  accountId: string,
  emails: string[],
  timeMin: string,
  timeMax: string,
): Promise<Types.GoogleCalendarFreeBusyResponse> =>
  wrapMojoCall<Types.GoogleCalendarFreeBusyResponse>(
    () =>
      handler.googleCalendarFreeBusy(
        JSON.stringify({
          account_id: accountId,
          emails,
          time_min: timeMin,
          time_max: timeMax,
        }),
      ),
    'googleCalendarFreeBusy',
  );

export const googleCalendarMoveEvent = (
  accountId: string,
  calendarId: string,
  eventId: string,
  destinationCalendarId: string,
): Promise<Types.CalendarEvent> =>
  wrapMojoCall<Types.CalendarEvent>(
    () =>
      handler.googleCalendarMoveEvent(
        accountId,
        calendarId,
        eventId,
        destinationCalendarId,
      ),
    'googleCalendarMoveEvent',
  );

export const listAccountCalendars = (accountId: string): Promise<Types.GoogleCalendarEntry[]> =>
  wrapMojoCall<Types.GoogleCalendarEntry[]>(() => handler.listAccountCalendars(accountId), 'listAccountCalendars');

export const setCalendarVisibility = (calendarRowId: string, visible: boolean): Promise<void> =>
  wrapMojoCall<void>(() => handler.setCalendarVisibility(calendarRowId, visible), 'setCalendarVisibility');

export const subscribeHolidayCalendar = (accountId: string, localeCode: string): Promise<Types.GoogleCalendarSyncResult> =>
  wrapMojoCall<Types.GoogleCalendarSyncResult>(() => handler.subscribeHolidayCalendar(accountId, localeCode), 'subscribeHolidayCalendar');

export const listAllPendingMutations = async (): Promise<Types.PendingMutation[]> => {
  const accounts = await listAccounts();
  const perAccount = await Promise.all(
    accounts.map((account) =>
      listPendingMutations(account.id).catch(() => [] as Types.PendingMutation[]),
    ),
  );
  return perAccount.flat();
};

export function getAutoDraftForEmail(emailId: string): Promise<Types.AutoDraft | null>;
export function getAutoDraftForEmail(accountId: string, emailId: string): Promise<Types.AutoDraft | null>;
export function getAutoDraftForEmail(arg1: string, arg2?: string): Promise<Types.AutoDraft | null> {
  if (arg2) {
    return wrapMojoCall<Types.AutoDraft | null>(() => handler.getAutoDraftForEmail(arg1, arg2), 'getAutoDraftForEmail');
  }
  return wrapMojoCall<Types.AutoDraft | null>(() => handler.getAutoDraftForEmail('', arg1), 'getAutoDraftForEmail');
}

export const updateAutoDraftStatus = (draftId: string, status: "accepted" | "dismissed"): Promise<void> =>
  wrapMojoCall<void>(() => handler.updateAutoDraftStatus(draftId, status), 'updateAutoDraftStatus');

export const checkEventConflict = (
  accountId: string,
  dtstart: string,
  dtend: string,
  excludeEventId?: string,
): Promise<Types.CalendarEvent[]> =>
  wrapMojoCall<Types.CalendarEvent[]>(() => handler.checkEventConflict(accountId, dtstart, dtend, excludeEventId ?? ''), 'checkEventConflict');

export const duplicateCalendarEvent = (eventId: string): Promise<Types.CalendarEvent> =>
  wrapMojoCall<Types.CalendarEvent>(() => handler.duplicateCalendarEvent(eventId), 'duplicateCalendarEvent');

export const exportCalendarIcs = (
  accountId: string,
  calendarIds?: string[],
  fromDate?: string,
  toDate?: string,
): Promise<string> =>
  wrapMojoCall<string>(() => handler.exportCalendarIcs(JSON.stringify({ account_id: accountId, calendar_ids: calendarIds, from_date: fromDate, to_date: toDate })), 'exportCalendarIcs');

export const listCalendarCategories = (accountId: string): Promise<Types.CalendarCategory[]> =>
  wrapMojoCall<Types.CalendarCategory[]>(() => handler.listCalendarCategories(accountId), 'listCalendarCategories');

export const createCalendarCategory = (accountId: string, name: string, color: string): Promise<Types.CalendarCategory> =>
  wrapMojoCall<Types.CalendarCategory>(() => handler.createCalendarCategory(accountId, name, color), 'createCalendarCategory');

export const updateCalendarCategory = (id: string, name: string, color: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.updateCalendarCategory(id, name, color), 'updateCalendarCategory');

export const deleteCalendarCategory = (id: string): Promise<void> =>
  wrapMojoCall<void>(() => handler.deleteCalendarCategory(id), 'deleteCalendarCategory');
