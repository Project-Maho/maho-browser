// Account types
export type Encryption = "None" | "Tls" | "StartTls";
export type AuthType = "password" | "oauth2_gmail" | "oauth2_outlook";

export interface OAuth2Config {
  provider: string;
  client_id: string;
  client_secret: string;
}

export interface OAuth2TokenResponse {
  access_token: string;
  refresh_token?: string;
  expires_in?: number;
}

export interface Account {
  id: string;
  email: string;
  display_name: string;
  auth_type?: string;
  imap_host: string;
  imap_port: number;
  imap_encryption: Encryption;
  smtp_host: string;
  smtp_port: number;
  smtp_encryption: Encryption;
  username: string;
  created_at: string;
  updated_at: string;
}

export interface CreateAccountRequest {
  email: string;
  display_name: string;
  auth_type?: AuthType;
  imap_host: string;
  imap_port: number;
  imap_encryption: Encryption;
  smtp_host: string;
  smtp_port: number;
  smtp_encryption: Encryption;
  username: string;
  password?: string;
  oauth2_client_id?: string;
  oauth2_client_secret?: string;
  oauth2_access_token?: string;
  oauth2_refresh_token?: string;
}

export interface AccountSummary {
  id: string;
  email: string;
  display_name: string;
  auth_type?: string;
  provider?: string;
}

export interface AuthError {
  account_id: string;
  provider: string;
  reason: "invalid_grant" | "invalid_client" | "refresh_token_missing" | "unknown";
  timestamp: number;
}

export interface SyncErrorEvent {
  account_id: string;
  error: string;
  error_type: "auth" | "network" | "other";
}

export interface AccountResponse {
  id: string;
  email: string;
  display_name: string;
  auth_type: string;
  imap_host: string;
  imap_port: number;
  imap_encryption: Encryption;
  smtp_host: string;
  smtp_port: number;
  smtp_encryption: Encryption;
  username: string;
  oauth2_client_id?: string;
  created_at: string;
  updated_at: string;
  auto_draft_enabled?: boolean;
}

export interface UpdateAccountRequest {
  id: string;
  email?: string;
  display_name?: string;
  imap_host?: string;
  imap_port?: number;
  imap_encryption?: Encryption;
  smtp_host?: string;
  smtp_port?: number;
  smtp_encryption?: Encryption;
  username?: string;
  password?: string;
}

// Email types
export interface Email {
  id: string;
  account_id: string;
  folder_id: string;
  uid: number;
  message_id: string;
  in_reply_to: string | null;
  subject: string;
  from_address: string;
  from_name: string | null;
  to_addresses: string;
  cc_addresses: string | null;
  bcc_addresses: string | null;
  date: string;
  snippet: string;
  is_read: boolean;
  is_starred: boolean;
  is_draft: boolean;
  has_attachments: boolean;
  body_text: string | null;
  body_html: string | null;
  raw_size: number;
  created_at: string;
  mdn_requested: string | null;
}

export interface EmailSummary {
  id: string;
  account_id: string;
  folder_id: string;
  uid: number;
  message_id: string;
  subject: string;
  from_address: string;
  from_name: string | null;
  date: string;
  snippet: string;
  is_read: boolean;
  is_starred: boolean;
  is_draft: boolean;
  has_attachments: boolean;
}

export interface EmailDetail {
  email: Email;
  attachments: Attachment[];
}

// Folder types
export type FolderType =
  | "Inbox"
  | "Sent"
  | "Drafts"
  | "Trash"
  | "Spam"
  | "Archive"
  | "Custom";

export interface Folder {
  id: string;
  account_id: string;
  name: string;
  path: string;
  folder_type: FolderType;
  unread_count: number;
  total_count: number;
}

export interface FolderCount {
  folder_id: string;
  unread_count: number;
  total_count: number;
}

// Attachment types
export interface Attachment {
  id: string;
  part_id: string;
  email_id: string;
  filename: string | null;
  mime_type: string;
  size: number;
  content_id: string | null;
}

// Search types
export interface SearchQuery {
  query: string;
  account_id?: string;
  folder_id?: string;
  limit?: number;
  offset?: number;
  // Structured filters
  from?: string;
  to?: string;
  subject?: string;
  has_attachment?: boolean;
  is_unread?: boolean;
  is_starred?: boolean;
  date_from?: string;
  date_to?: string;
}

export interface SearchResult {
  emails: EmailSummary[];
  total_count: number;
  query: string;
}

// AI types
export type AiProvider = "OpenAi" | "Anthropic" | "Ollama" | "OpenRouter";

export interface AiConfig {
  feature?: string;
  provider: AiProvider;
  model: string;
  api_key?: string;
  has_key?: boolean;
  base_url?: string;
}

export interface AiConfigResponse {
  provider: AiProvider;
  model: string;
  has_key: boolean;
  base_url?: string;
}

export interface SummaryRequest {
  email_ids: string[];
}

export interface SummaryResponse {
  summary: string;
}

export interface ReplyDraftRequest {
  email_id: string;
  instructions?: string;
}

export interface ReplyDraftResponse {
  draft: string;
}

export interface ToneRequest {
  text: string;
  tone: "formal" | "casual" | "friendly" | "professional" | "concise";
}

export interface ToneResponse {
  adjusted_text: string;
}

export interface ClassifyRequest {
  email_id: string;
}

export interface ClassifyResponse {
  category: "important" | "general" | "promotion" | "spam";
}

export interface NlSearchRequest {
  query: string;
  account_id?: string;
  folder_id?: string;
}

export interface TestAiConnectionResult {
  success: boolean;
  message: string;
  detail?: string | null;
}

// Simplified OAuth types
export interface OAuthAccount {
  account_id: string;
  email: string;
  display_name: string;
}

// Compose types
export interface ComposeAttachment {
  filename: string;
  mime_type: string;
  data: string;
}

export interface SendEmailResult {
  status: 'sent' | 'queued' | 'uncertain';
}

export interface ComposeEmailRequest {
  account_id: string;
  to: string[];
  cc?: string[];
  bcc?: string[];
  subject: string;
  body_text?: string;
  body_html?: string;
  read_receipt?: boolean;
  attachments?: ComposeAttachment[];
  in_reply_to?: string;
  references?: string;
}

export interface ComposeStateSnapshot {
  accountId: string;
  to?: string;
  cc?: string;
  bcc?: string;
  subject?: string;
  body?: string;
  bodyHtml?: string;
  attachments?: ComposeAttachment[];
  draftId?: string | null;
  replyTo?: ReplyContext;
  forwardFrom?: ReplyContext;
  forwardEmailUid?: number;
  forwardFolderId?: string;
  readReceipt?: boolean;
  inReplyTo?: string;
  references?: string;
}

export interface SendRequestedOptions {
  draftId?: string | null;
  composition?: ComposeStateSnapshot;
}

export interface ReplyContext {
  original_subject: string;
  original_from: string;
  original_date: string;
  original_body_text: string | null;
  original_body_html: string | null;
  reply_all?: boolean;
  original_to_addresses?: string | null;
  original_cc_addresses?: string | null;
  original_message_id?: string | null;
  original_references?: string | null;
}

// Translation types
export type TranslationProvider = "local" | "byok" | "skip";

export interface TranslationConfig {
  provider: TranslationProvider;
  defaultTargetLang: string;
  alwaysTranslateFrom: string[];
}

export interface TranslationModelDownloadProgress {
  downloaded: number;
  total: number;
  percent: number;
}

// Aggregate mailbox types for Spark-style parent/child selection
export type MailboxSelection =
  | null
  | { type: "aggregate"; folderType: FolderType }
  | { type: "child"; accountId: string; folderId: string };

export interface AccountFolders {
  account: AccountSummary;
  folders: Folder[];
}

// Snooze types
export interface SnoozeRequest {
  email_id: string;
  snooze_until: string;
}

// Send Later types
export interface SendLaterRequest {
  account_id: string;
  to: string[];
  cc?: string[];
  bcc?: string[];
  subject: string;
  body_text?: string;
  body_html?: string;
  scheduled_at: string;
  attachments?: ComposeAttachment[];
  read_receipt?: boolean;
  in_reply_to?: string;
  references?: string;
}

export interface SendLaterItem {
  id: string;
  account_id: string;
  to_addresses: string;
  cc_addresses: string | null;
  bcc_addresses: string | null;
  subject: string;
  body_html: string | null;
  body_text: string | null;
  scheduled_at: string;
  status: "pending" | "sent" | "cancelled";
  created_at: string;
}

// Contact types
export interface Contact {
  id: string;
  account_id: string;
  email: string;
  name: string | null;
  frequency: number;
  last_contacted_at: string | null;
  is_vip: boolean;
  created_at: string;
  updated_at: string;
}

export interface ContactGroup {
  id: string;
  account_id: string;
  name: string;
  member_emails: string[];
  created_at: string;
  updated_at: string;
}

// Signature types
export interface Signature {
  id: string;
  account_id: string | null;
  name: string;
  body_html: string;
  body_text: string;
  is_default: boolean;
  created_at: string;
  updated_at: string;
}

export interface CreateSignatureRequest {
  account_id?: string;
  name: string;
  body_html: string;
  body_text: string;
  is_default?: boolean;
}

// Email Template types
export interface EmailTemplate {
  id: string;
  name: string;
  subject: string;
  body_html: string;
  body_text: string;
  created_at: string;
  updated_at: string;
}

export interface CreateTemplateRequest {
  name: string;
  subject: string;
  body_html: string;
  body_text: string;
}

// Label types
export interface Label {
  id: string;
  account_id: string;
  name: string;
  color: string;
  created_at: string;
}

export interface CreateLabelRequest {
  account_id: string;
  name: string;
  color?: string;
}

// Reminder types
export interface ReminderRequest {
  email_id: string;
  reminder_at: string;
}

// Smart Inbox category
export type SmartCategory = "personal" | "notification" | "newsletter" | "promotion";

// Batch action types
export interface BatchActionRequest {
  email_ids: string[];
}

export interface BatchMoveRequest {
  email_ids: string[];
  target_folder_id: string;
}

// Undo send config
export interface UndoSendConfig {
  delay_seconds: number;
}

// Read receipt types
export interface ReadReceipt {
  email_id: string;
  read_at: string | null;
  recipient: string;
}

// Email delegation types
export interface DelegatedEmail {
  id: string;
  email_id: string;
  delegated_to: string;
  delegated_by: string;
  note?: string;
  status: "pending" | "completed" | "declined";
  created_at: string;
}

// AI action log types
export interface AiActionLog {
  id: string;
  account_id: string;
  email_id: string | null;
  action_type: string;
  provider: string;
  model: string | null;
  input_summary: string | null;
  output_summary: string | null;
  status: string;
  error_message: string | null;
  tokens_used: number | null;
  created_at: string;
}

export type RuleConditionField = 'from' | 'to' | 'subject' | 'body' | 'has_attachment';
export type RuleConditionOperator = 'contains' | 'not_contains' | 'equals' | 'not_equals' | 'starts_with' | 'ends_with' | 'matches_regex';
export type RuleActionType = 'move_to_folder' | 'mark_read' | 'mark_unread' | 'add_star' | 'remove_star' | 'delete';

export interface RuleCondition {
  field: RuleConditionField;
  operator: RuleConditionOperator;
  value: string;
}

export interface RuleAction {
  type: RuleActionType;
  value?: string;
}

export interface MailRule {
  id: string;
  account_id: string;
  name: string;
  priority: number;
  is_enabled: boolean;
  conditions: RuleCondition[];
  actions: RuleAction[];
  stop_processing: boolean;
  created_at: string;
  updated_at: string;
}

export interface CreateRuleRequest {
  account_id: string;
  name: string;
  conditions: RuleCondition[];
  actions: RuleAction[];
  stop_processing?: boolean;
}

export interface UpdateRuleRequest {
  id: string;
  name?: string;
  is_enabled?: boolean;
  conditions?: RuleCondition[];
  actions?: RuleAction[];
  stop_processing?: boolean;
}

export interface ImportResult {
  imported: number;
  failed: number;
  errors: string[];
}

export interface ImportExportProgress {
  current: number;
  total: number;
}

// PGP types
export interface PgpKeyInfo {
  id: string;
  account_id: string;
  email: string;
  key_type: string;
  fingerprint: string;
  is_private: boolean;
  is_default: boolean;
  expires_at: string | null;
  created_at: string;
}

export interface PgpVerifyResult {
  is_valid: boolean;
  signer_email: string | null;
  fingerprint: string | null;
  error: string | null;
}

export interface MutedThread {
  id: string;
  account_id: string;
  thread_id: string;
  created_at: string;
}

export interface SavedSearch {
  id: string;
  name: string;
  query: string;
  account_id: string | null;
  created_at: string;
}

// S/MIME types
export interface SmimeIdentity {
  id: string;
  account_id: string;
  email: string;
  subject: string;
  issuer: string;
  serial_number: string;
  fingerprint: string;
  not_before: string;
  not_after: string;
  is_default: boolean;
  created_at: string;
}

export interface SmimeVerifyResult {
  valid: boolean;
  trusted: boolean;
  signer_email: string | null;
  signer_subject: string | null;
}

export interface PendingMutation {
  id: string;
  account_id: string;
  email_uid: number;
  folder_path: string;
  mutation_type: string;
  target_folder: string | null;
  created_at: string;
  uid_validity?: number;
}

export interface OutboxItem {
  id: string;
  account_id: string;
  to_addresses: string;
  cc_addresses: string | null;
  bcc_addresses: string | null;
  subject: string;
  body_html: string | null;
  body_text: string | null;
  status: string;
  retry_count: number;
  max_retries: number;
  last_error: string | null;
  created_at: string;
  updated_at: string;
}

export interface QueueEmailRequest {
  account_id: string;
  to: string[];
  cc?: string[];
  bcc?: string[];
  subject: string;
  body_text?: string;
  body_html?: string;
  in_reply_to?: string;
  references?: string;
  attachments?: ComposeAttachment[];
}

// Calendar Event types
export interface CalendarEvent {
  id: string;
  account_id: string;
  email_id: string | null;
  uid: string;
  summary: string;
  description: string | null;
  dtstart: string;
  dtend: string | null;
  location: string | null;
  organizer: string | null;
  status: string;
  rsvp_status: string | null;
  recurrence_rule: string | null;
  all_day: boolean;
  created_at: string;
  updated_at: string;
  google_calendar_id?: string | null;
  calendar_color?: string | null;
  calendar_summary?: string | null;
  start_tz?: string | null;
  end_tz?: string | null;
  recurring_event_id?: string | null;
  attendees_json?: string | null;
  reminders_json?: string | null;
  color?: string | null;
  hangout_link?: string | null;
  category?: string | null;
  travel_time_minutes?: number | null;
  event_type?: string | null;
}

export interface GoogleCalendarEntry {
  id: string;
  account_id: string;
  calendar_id: string;
  summary: string;
  background_color: string | null;
  foreground_color: string | null;
  is_primary: boolean;
  access_role: string | null;
  visible: boolean;
}

export interface CalendarCategory {
  id: string;
  account_id: string;
  name: string;
  color: string;
  created_at?: string;
}

export interface CreateCalendarEventRequest {
  account_id: string;
  summary: string;
  description?: string;
  dtstart: string;
  dtend?: string;
  location?: string;
  all_day: boolean;
  add_meet?: boolean;
  attendees?: string[];
  color?: string;
  time_zone?: string;
  calendar_id?: string;
  category?: string;
  travel_time_minutes?: number;
  event_type?: string;
  reminders?: {
    useDefault?: boolean;
    overrides?: { method: string; minutes: number }[];
  };
}

export interface UpdateCalendarEventRequest {
  event_id: string;
  summary: string;
  description?: string;
  dtstart: string;
  dtend?: string;
  location?: string;
  all_day: boolean;
  attendees?: string[];
  color?: string;
  time_zone?: string;
  edit_scope?: "single" | "all";
  target_calendar_id?: string;
  category?: string;
  travel_time_minutes?: number;
  event_type?: string;
  reminders?: {
    useDefault?: boolean;
    overrides?: { method: string; minutes: number }[];
  };
}

export interface DeleteCalendarEventOptions {
  event_id: string;
  delete_scope?: "single" | "all";
}

export interface GoogleCalendarSyncResult {
  calendars_synced: number;
  events_upserted: number;
  events_deleted: number;
  reauthorize_required: boolean;
}

export interface FreeBusyItem {
  start: string;
  end: string;
}

export interface FreeBusyCalendar {
  busy: FreeBusyItem[];
  errors?: Array<{ domain: string; reason: string }>;
}

export interface GoogleCalendarFreeBusyResponse {
  kind?: string;
  timeMin?: string;
  timeMax?: string;
  calendars?: Record<string, FreeBusyCalendar>;
}

// AI Agent types
export interface AgentChatSession {
  id: string;
  account_id: string;
  title: string;
  created_at: string;
  updated_at: string;
}

export interface AgentChatMessage {
  id: string;
  session_id: string;
  role: "user" | "assistant" | "tool_result";
  content: string;
  created_at: string;
  tool_calls?: string;
}

export type AgentStreamEventKind =
  | "token"
  | "tool_call_start"
  | "tool_call_result"
  | "phase"
  | "error";

export interface AgentStreamEvent {
  session_id: string;
  kind: AgentStreamEventKind;
  payload: Record<string, unknown>;
}

// Auto Draft types
export interface AutoDraft {
  id: string;
  email_id: string;
  draft_content: string;
  status: "pending" | "accepted" | "dismissed";
}
