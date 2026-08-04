import {
  ApprovalDecision,
  ApprovalPolicy,
  ApprovalRequestInfo,
  ApprovalResultInfo,
  ApprovalSensitivity,
  ApprovalState,
  BookmarkItem,
  BrowserContextPayload as BrowserContextPayloadRecord,
  BrowserContextStatus,
  ContextAttachment as ContextAttachmentRecord,
  ContextSourceKind,
  ChatIntent as MojoChatIntent,
  CreditBalanceInfo,
  HistoryItem,
  RuntimeConnectionState,
  RuntimeEvent,
  RuntimeEventKind,
  SessionInfo,
  SessionStatus,
  TabItem,
  ToolCallInfo,
  ToolResultInfo,
  ViewMode,
} from './maho_ai.mojom-webui.js';

export type ConnectionState = RuntimeConnectionState;
export {ApprovalDecision, ApprovalPolicy, ApprovalSensitivity, ApprovalState, RuntimeEventKind, ViewMode};
export type {CreditBalanceInfo};
export type SessionRecord = SessionInfo;
export type RuntimeEventRecord = RuntimeEvent;
export type SessionStatusValue = SessionStatus;
export type ToolCallRecord = ToolCallInfo;
export type ToolResultRecord = ToolResultInfo;
export type ApprovalRequestRecord = ApprovalRequestInfo;
export type ApprovalResultRecord = ApprovalResultInfo;
export type EventKind = RuntimeEventKind;
export type BrowserContextPayload = BrowserContextPayloadRecord;
export type BrowserContextStatusValue = BrowserContextStatus;
export interface OpenTabRecord extends TabItem {
  isPinned?: boolean;
}
export type HistoryItemRecord = HistoryItem;
export type BookmarkItemRecord = BookmarkItem;
export interface SubmitContextAttachmentRecord extends ContextAttachmentRecord {
  bytes?: number[];
  mimeType?: string;
}

export interface TimelineEntry {
  sessionId: string;
  event: RuntimeEventRecord;
}

export interface InspectorSelection {
  kind: 'session'|'tool'|'approval'|'context';
  id?: string;
}

// currentPage uses the legacy attach_browser_context path when selected alone.
// tab is backed by GetOpenTabs + SubmitPrompt attachments. The remaining kinds
// stay as honest frontend placeholders for now.
export type ComposerAttachmentKind =
    'currentPage'|'tab'|'history'|'bookmark'|'file';

export interface ComposerAttachment {
  id: string;
  kind: ComposerAttachmentKind;
  label: string;
  detail?: string;
  dataUrl?: string;
  mimeType?: string;
  size?: number;
  tabId?: number;
  url?: string;
}

export interface ToastState {
  text: string;
  tone: 'danger'|'info';
}

export interface AISettingsState {
  activeModelId: string|null;
  activeProviderId: string|null;
  activeReasoningEffort: AIReasoningEffort|null;
  providerOptions: readonly AIProviderOptionRecord[];
  reasoningOptions: readonly AIReasoningOptionRecord[];
}

export interface AIProviderRecord {
  displayName: string;
  id: string;
  models: string[];
}

export const AI_REASONING_EFFORT = {
  kLow: 0,
  kMedium: 1,
  kHigh: 2,
} as const;

export type AIReasoningEffort =
    typeof AI_REASONING_EFFORT[keyof typeof AI_REASONING_EFFORT];

export interface AIModelOptionRecord {
  readonly id: string;
  readonly label: string;
}

export interface AIProviderOptionRecord {
  readonly id: string;
  readonly label: string;
  readonly modelOptions: readonly AIModelOptionRecord[];
}

export interface AIReasoningOptionRecord {
  readonly effort: AIReasoningEffort;
  readonly label: string;
}

export interface AISelectionRequest {
  readonly modelId: string;
  readonly providerId: string;
  readonly reasoningEffort: AIReasoningEffort;
}

export interface AIModelOptionPayload {
  readonly id?: string|null;
  readonly label?: string|null;
}

export interface AIProviderOptionPayload {
  readonly id?: string|null;
  readonly label?: string|null;
  readonly modelOptions?: readonly AIModelOptionPayload[]|null;
}

export interface AIReasoningOptionPayload {
  readonly effort?: AIReasoningEffort|null;
  readonly label?: string|null;
}

export interface AISettingsInfoPayload {
  readonly activeModelId?: string|null;
  readonly activeProviderId?: string|null;
  readonly activeReasoningEffort?: AIReasoningEffort|null;
  readonly providerOptions?: readonly AIProviderOptionPayload[]|null;
  readonly reasoningOptions?: readonly AIReasoningOptionPayload[]|null;
}

export const DEFAULT_AI_REASONING_OPTIONS: readonly AIReasoningOptionRecord[] = [
  {effort: AI_REASONING_EFFORT.kLow, label: 'Low'},
  {effort: AI_REASONING_EFFORT.kMedium, label: 'Medium'},
  {effort: AI_REASONING_EFFORT.kHigh, label: 'High'},
] as const;

export interface OpenTabsState {
  items: OpenTabRecord[];
  loading: boolean;
  loaded: boolean;
  error: string|null;
}

export interface HistorySearchState {
  query: string;
  items: HistoryItemRecord[];
  loading: boolean;
  error: string|null;
}

export interface BookmarksSearchState {
  query: string;
  items: BookmarkItemRecord[];
  loading: boolean;
  error: string|null;
}

export const DEFAULT_BOOKMARKS_SEARCH_STATE: BookmarksSearchState = {
  query: '',
  items: [],
  loading: false,
  error: null,
};

export interface ComposerState {
  prompt: string;
  // Replaces the old `attachBrowserContext: boolean`.
  // `currentPage` (id='currentPage') maps to the backend bool; others are
  // frontend-only until the Mojo contract is extended.
  attachments: ComposerAttachment[];
  modeLabel: string;
  focusRequest: number;
}

export type VoiceStatus = 'idle'|'listening'|'error'|'unsupported';

export interface VoiceState {
  active: boolean;
  status: VoiceStatus;
  transcript: string;
  level: number;
  error: string|null;
}

export const CURRENT_PAGE_ATTACHMENT: ComposerAttachment = {
  id: 'currentPage',
  kind: 'currentPage',
  label: 'Current page',
};

export const DEFAULT_OPEN_TABS_STATE: OpenTabsState = {
  items: [],
  loading: false,
  loaded: false,
  error: null,
};

export const DEFAULT_HISTORY_SEARCH_STATE: HistorySearchState = {
  query: '',
  items: [],
  loading: false,
  error: null,
};

export function hasCurrentPageAttachment(attachments: ComposerAttachment[]): boolean {
  return attachments.some(a => a.id === 'currentPage');
}

export function isTabAttachment(attachment: ComposerAttachment): boolean {
  return attachment.kind === 'tab';
}

export function isHistoryAttachment(attachment: ComposerAttachment): boolean {
  return attachment.kind === 'history';
}

export function isFileAttachment(attachment: ComposerAttachment): boolean {
  return attachment.kind === 'file';
}

export const ChatIntent = (MojoChatIntent as typeof MojoChatIntent | undefined) ?? {
  kFreeform: 0,
  kSummarizeCurrentPage: 1,
  kQuizCurrentPage: 2,
};
export type ChatIntent = (typeof ChatIntent)[keyof typeof ChatIntent];

export interface QuickAction {
  id: string;
  label: string;
  userPrompt: string;
  intent: ChatIntent;
}

function getFileAttachmentId(file: File): string {
  return `file:${file.name}:${file.size}:${file.lastModified}`;
}

function inferMimeType(file: File): string {
  const trimmedType = file.type.trim();
  if (trimmedType) {
    return trimmedType;
  }

  const extension = file.name.split('.').pop()?.toLowerCase();
  switch (extension) {
    case 'png':
      return 'image/png';
    case 'jpg':
    case 'jpeg':
      return 'image/jpeg';
    case 'gif':
      return 'image/gif';
    case 'webp':
      return 'image/webp';
    case 'pdf':
      return 'application/pdf';
    default:
      return 'application/octet-stream';
  }
}

function ensureSupportedFile(file: File, mimeType: string): void {
  const lowerName = file.name.trim().toLowerCase();
  if (mimeType === 'image/svg+xml' || lowerName.endsWith('.svg')) {
    throw new Error('SVG attachments are not supported. Please upload a PNG, JPEG, WebP, GIF, PDF, or text file instead.');
  }
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  const chunkSize = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    const chunk = bytes.subarray(offset, offset + chunkSize);
    binary += String.fromCharCode(...chunk);
  }
  return btoa(binary);
}

function dataUrlToByteArray(dataUrl?: string): number[] {
  if (!dataUrl) {
    return [];
  }

  const commaIndex = dataUrl.indexOf(',');
  if (commaIndex < 0) {
    return [];
  }

  try {
    const decoded = atob(dataUrl.slice(commaIndex + 1));
    return Array.from(decoded, char => char.charCodeAt(0));
  } catch {
    return [];
  }
}

async function readFileBytes(file: File): Promise<Uint8Array> {
  if (typeof file.arrayBuffer === 'function') {
    return new Uint8Array(await file.arrayBuffer());
  }

  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(reader.error || new Error('Failed to read file attachment.'));
    reader.onload = () => {
      const result = reader.result;
      if (!(result instanceof ArrayBuffer)) {
        reject(new Error('Failed to read file attachment.'));
        return;
      }
      resolve(new Uint8Array(result));
    };
    reader.readAsArrayBuffer(file);
  });
}

export async function createFileAttachment(file: File): Promise<ComposerAttachment> {
  const mimeType = inferMimeType(file);
  ensureSupportedFile(file, mimeType);

  const bytes = await readFileBytes(file);
  const dataUrl = `data:${mimeType};base64,${bytesToBase64(bytes)}`;
  return {
    dataUrl,
    detail: `${mimeType} · ${bytes.length} byte${bytes.length === 1 ? '' : 's'}`,
    id: getFileAttachmentId(file),
    kind: 'file',
    label: file.name,
    mimeType,
    size: bytes.length,
  };
}

export function getTabAttachmentId(tabId: number): string {
  return `tab:${tabId}`;
}

export function redactUrlForDisplay(url?: string|null): string {
  const trimmed = url?.trim() || '';
  if (!trimmed) {
    return '';
  }

  try {
    const parsed = new URL(trimmed);
    parsed.search = '';
    parsed.hash = '';
    return parsed.toString();
  } catch {
    const queryIndex = trimmed.indexOf('?');
    const hashIndex = trimmed.indexOf('#');
    const cutoff = [queryIndex, hashIndex]
        .filter(index => index >= 0)
        .reduce((smallest, index) => Math.min(smallest, index), trimmed.length);
    return trimmed.slice(0, cutoff);
  }
}

export function getOpenTabTitle(tab: OpenTabRecord): string {
  return tab.title.trim() || redactUrlForDisplay(tab.url) || `Tab ${tab.index + 1}`;
}

export function getOpenTabDetail(tab: OpenTabRecord): string {
  return redactUrlForDisplay(tab.url) || (tab.isActive ? 'Active tab' : `Tab ${tab.index + 1}`);
}

export function createTabAttachment(tab: OpenTabRecord): ComposerAttachment {
  return {
    id: getTabAttachmentId(tab.tabId),
    kind: 'tab',
    label: getOpenTabTitle(tab),
    detail: getOpenTabDetail(tab),
    tabId: tab.tabId,
    url: tab.url,
  };
}

export function getHistoryAttachmentId(url: string): string {
  return `history:${url}`;
}

function normalizeHistoryTimestamp(value?: number|string|null): Date|null {
  if (value === null || value === undefined || value === '') {
    return null;
  }

  if (typeof value === 'string') {
    const parsed = Date.parse(value);
    return Number.isNaN(parsed) ? null : new Date(parsed);
  }

  const absolute = Math.abs(value);
  if (absolute >= 1e14) {
    return new Date(value / 1000);
  }

  if (absolute >= 1e12) {
    return new Date(value);
  }

  return new Date(value * 1000);
}

function formatHistoryVisitedAt(value?: number|string|null): string|null {
  const date = normalizeHistoryTimestamp(value);
  if (!date) {
    return null;
  }

  return new Intl.DateTimeFormat(undefined, {
    day: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
    month: 'short',
  }).format(date);
}

export function getHistoryItemTitle(item: HistoryItemRecord): string {
  return item.title.trim() || redactUrlForDisplay(item.url) || 'Visited page';
}

export function getHistoryVisitedLabel(item: HistoryItemRecord): string|null {
  return formatHistoryVisitedAt(item.visitedAt);
}

export function getHistoryItemDetail(item: HistoryItemRecord): string {
  const visitedLabel = getHistoryVisitedLabel(item);
  const url = redactUrlForDisplay(item.url);
  if (!visitedLabel) {
    return url || 'Visited page';
  }

  return url ? `${url} · ${visitedLabel}` : visitedLabel;
}

export function getBookmarkAttachmentId(id: string): string {
  return `bookmark:${id}`;
}

export function getBookmarkItemTitle(item: BookmarkItemRecord): string {
  return item.title.trim() || redactUrlForDisplay(item.url) || 'Bookmarked page';
}

export function getBookmarkItemDetail(item: BookmarkItemRecord): string {
  return redactUrlForDisplay(item.url) || 'Bookmarked page';
}

export function createBookmarkAttachment(item: BookmarkItemRecord): ComposerAttachment {
  return {
    id: getBookmarkAttachmentId(item.id),
    kind: 'bookmark',
    label: getBookmarkItemTitle(item),
    detail: getBookmarkItemDetail(item),
    url: item.url,
  };
}

export function createHistoryAttachment(item: HistoryItemRecord): ComposerAttachment {
  return {
    id: getHistoryAttachmentId(item.url),
    kind: 'history',
    label: getHistoryItemTitle(item),
    detail: getHistoryItemDetail(item),
    url: item.url,
  };
}

export function getSubmitContextAttachments(
    attachments: ComposerAttachment[]): SubmitContextAttachmentRecord[]|null {
  const submitAttachments = attachments.flatMap((attachment): SubmitContextAttachmentRecord[] => {
    switch (attachment.kind) {
      case 'currentPage':
        return [{
          bytes: [],
          kind: ContextSourceKind.kCurrentPage,
          label: attachment.label,
          tabId: -1,
          url: '',
        }];
      case 'tab':
        return [{
          bytes: [],
          kind: ContextSourceKind.kOpenTab,
          label: attachment.label,
          tabId: attachment.tabId ?? -1,
          url: attachment.url || '',
        }];
      case 'history':
        return [{
          bytes: [],
          kind: ContextSourceKind.kHistory,
          label: attachment.label,
          tabId: -1,
          url: attachment.url || '',
        }];
      case 'bookmark':
        return [{
          bytes: [],
          kind: ContextSourceKind.kBookmark,
          label: attachment.label,
          tabId: -1,
          url: attachment.url || '',
        }];
      case 'file':
        return [{
          bytes: dataUrlToByteArray(attachment.dataUrl),
          kind: ContextSourceKind.kFile,
          label: attachment.label,
          mimeType: attachment.mimeType || 'application/octet-stream',
          tabId: -1,
          url: attachment.url || '',
        }];
      default:
        return [];
    }
  });

  if (!submitAttachments.length) {
    return null;
  }

  return submitAttachments;
}

export interface SessionHistoryState {
  loaded: boolean;
  totalCount: number;
  loadedCount: number;
}

export interface AppState {
  initialized: boolean;
  booting: boolean;
  bootError: string|null;
  developerMode: boolean;
  connectionState: ConnectionState;
  activeAdapterName: string;
  sessionsById: Record<string, SessionRecord>;
  sessionOrder: string[];
  sessionSearchQuery: string;
  currentSessionId: string|null;
  eventsBySessionId: Record<string, TimelineEntry[]>;
  historiesBySessionId: Record<string, SessionHistoryState>;
  toolCallsById: Record<string, ToolCallRecord>;
  toolResultsByCallId: Record<string, ToolResultRecord>;
  approvalsById: Record<string, ApprovalRequestRecord>;
  approvalResultsById: Record<string, ApprovalResultRecord>;
  selectedInspector: InspectorSelection;
  composer: ComposerState;
  voice: VoiceState;
  openTabs: OpenTabsState;
  historySearch: HistorySearchState;
  bookmarksSearch: BookmarksSearchState;
  toast: ToastState|null;
  aiProviders: AIProviderRecord[];
  aiSettings: AISettingsState;
  turnPendingBySessionId: Record<string, boolean>;
  contextsBySessionId: Record<string, BrowserContextPayload[]>;
  creditBalance: CreditBalanceInfo|null;
  chatBlockedReason: 'quota-exhausted'|null;
  activeProfileId: string|null;
  profiles: AiProfile[];
  activeWorkspace: AiWorkspace|null;
  mcpServers: AiMcpServer[];
  cliTools: AiCliTool[];
  activeSlashEditor: 'agent-new' | 'mcp-add' | 'cli-add' | null;
}

export const DEFAULT_COMPOSER_STATE: ComposerState = {
  prompt: '',
  attachments: [],
  modeLabel: 'Task',
  focusRequest: 0,
};

export const DEFAULT_VOICE_STATE: VoiceState = {
  active: false,
  status: 'idle',
  transcript: '',
  level: 0,
  error: null,
};

export function createInitialState(): AppState {
  return {
    initialized: false,
    booting: true,
    bootError: null,
    developerMode: false,
    connectionState: RuntimeConnectionState.kConnecting,
    activeAdapterName: 'OpenCode',
    sessionsById: {},
    sessionOrder: [],
    sessionSearchQuery: '',
    currentSessionId: null,
    eventsBySessionId: {},
    historiesBySessionId: {},
    toolCallsById: {},
    toolResultsByCallId: {},
    approvalsById: {},
    approvalResultsById: {},
    selectedInspector: {kind: 'session'},
    composer: {...DEFAULT_COMPOSER_STATE},
    voice: {...DEFAULT_VOICE_STATE},
    openTabs: {...DEFAULT_OPEN_TABS_STATE, items: []},
    historySearch: {...DEFAULT_HISTORY_SEARCH_STATE},
    bookmarksSearch: {...DEFAULT_BOOKMARKS_SEARCH_STATE},
    toast: null,
    aiProviders: [],
    aiSettings: {
      activeModelId: null,
      activeProviderId: null,
      activeReasoningEffort: null,
      providerOptions: [],
      reasoningOptions: DEFAULT_AI_REASONING_OPTIONS,
    },
    turnPendingBySessionId: {},
    contextsBySessionId: {},
    creditBalance: null,
    chatBlockedReason: null,
    activeProfileId: null,
    profiles: [],
    activeWorkspace: null,
    mcpServers: [],
    cliTools: [],
    activeSlashEditor: null,
  };
}

export function getSessionTitle(session: SessionRecord): string {
  return session.title?.trim() || session.summary?.trim() || 'Untitled session';
}

export function getSessionSubtitle(session: SessionRecord): string {
  return session.summary?.trim() || session.adapterName || 'OpenCode runtime session';
}

export function isSessionReadOnly(session?: SessionRecord|null): boolean {
  return !!session?.isReadOnly;
}

export function getSessionAccessLabel(session?: SessionRecord|null): string {
  return isSessionReadOnly(session) ? 'Read-only history' : 'Live session';
}

export function getStatusLabel(status: SessionStatusValue): string {
  switch (status) {
    case SessionStatus.kCreated:
      return 'Created';
    case SessionStatus.kActive:
      return 'Active';
    case SessionStatus.kIdle:
      return 'Idle';
    case SessionStatus.kPausedForApproval:
      return 'Awaiting approval';
    case SessionStatus.kCompleted:
      return 'Completed';
    case SessionStatus.kCancelled:
      return 'Cancelled';
    case SessionStatus.kError:
      return 'Error';
  }
}

export function getConnectionStateLabel(state: ConnectionState): string {
  switch (state) {
    case RuntimeConnectionState.kDisconnected:
      return 'Disconnected';
    case RuntimeConnectionState.kConnecting:
      return 'Connecting';
    case RuntimeConnectionState.kConnected:
      return 'Connected';
    case RuntimeConnectionState.kReconnecting:
      return 'Reconnecting';
    case RuntimeConnectionState.kError:
      return 'Error';
    case RuntimeConnectionState.kPausedForApproval:
      return 'Paused for approval';
    case RuntimeConnectionState.kResumed:
      return 'Resumed';
  }
}

export function getConnectionStateTone(state: ConnectionState): string {
  switch (state) {
    case RuntimeConnectionState.kConnected:
    case RuntimeConnectionState.kResumed:
      return 'success';
    case RuntimeConnectionState.kError:
      return 'danger';
    case RuntimeConnectionState.kPausedForApproval:
    case RuntimeConnectionState.kDisconnected:
    case RuntimeConnectionState.kConnecting:
    case RuntimeConnectionState.kReconnecting:
      return 'warning';
  }
}

export function getBrowserContextStatusLabel(status: BrowserContextStatusValue): string {
  switch (status) {
    case BrowserContextStatus.kInjected:
      return 'Injected';
    case BrowserContextStatus.kDeniedInternalPage:
      return 'Denied: internal page';
    case BrowserContextStatus.kNoActiveTab:
      return 'No active tab';
    case BrowserContextStatus.kCannotAccess:
      return 'Cannot access page';
    case BrowserContextStatus.kExtractionFailed:
      return 'Extraction failed';
    case BrowserContextStatus.kNotRequested:
      return 'Not requested';
  }
}

export function getBrowserContextStatusTone(status: BrowserContextStatusValue): string {
  switch (status) {
    case BrowserContextStatus.kInjected:
      return 'success';
    case BrowserContextStatus.kNotRequested:
      return 'neutral';
    case BrowserContextStatus.kDeniedInternalPage:
    case BrowserContextStatus.kNoActiveTab:
    case BrowserContextStatus.kCannotAccess:
      return 'warning';
    case BrowserContextStatus.kExtractionFailed:
      return 'danger';
  }
}

export function getLastBrowserContext(events: RuntimeEventRecord[]): BrowserContextPayload|undefined {
  for (let index = events.length - 1; index >= 0; index--) {
    const event = events[index];
    if (!event) {
      continue;
    }

    if (event.kind === RuntimeEventKind.kBrowserContextInjected && event.browserContext) {
      return event.browserContext;
    }
  }

  return undefined;
}

function toTitleCaseRuntimeStateLabel(state: string): string {
  return state.split(/[_-]+/).filter(Boolean).map(part =>
    part[0] ? `${part[0].toUpperCase()}${part.slice(1)}` : '').join(' ');
}

export function getRuntimeStateLabel(state?: string|null): string {
  if (!state) {
    return 'Unknown';
  }

  switch (state) {
    case 'created':
      return 'Created';
    case 'active':
      return 'Active';
    case 'idle':
      return 'Idle';
    case 'paused_for_approval':
      return 'Paused for approval';
    case 'completed':
      return 'Completed';
    case 'cancelled':
      return 'Cancelled';
    case 'error':
      return 'Error';
    case 'disconnected':
      return 'Disconnected';
    case 'connecting':
      return 'Connecting';
    case 'connected':
      return 'Connected';
    case 'reconnecting':
      return 'Reconnecting';
    case 'resumed':
      return 'Resumed';
  }

  return toTitleCaseRuntimeStateLabel(state);
}

export function getApprovalPolicyLabel(policy?: ApprovalPolicy|null): string {
  switch (policy) {
    case ApprovalPolicy.kAllowAll:
      return 'Allow all';
    case ApprovalPolicy.kAllowMcp:
      return 'Allow MCP';
    case ApprovalPolicy.kDenySensitive:
      return 'Deny sensitive';
    case ApprovalPolicy.kDenyAll:
      return 'Deny all';
    case ApprovalPolicy.kPrompt:
    default:
      return 'Prompt';
  }
}

export function getApprovalSensitivityLabel(
    sensitivity?: ApprovalSensitivity|null): string {
  switch (sensitivity) {
    case ApprovalSensitivity.kReadOnly:
      return 'Read-only';
    case ApprovalSensitivity.kSensitive:
    default:
      return 'Sensitive';
  }
}

export function getApprovalStateLabel(state?: ApprovalState|null): string {
  switch (state) {
    case ApprovalState.kApproved:
      return 'Approved';
    case ApprovalState.kDenied:
      return 'Denied';
    case ApprovalState.kCancelled:
      return 'Cancelled';
    case ApprovalState.kPending:
    default:
      return 'Pending';
  }
}

export function getApprovalDecisionLabel(
    decision?: ApprovalDecision|null): string {
  switch (decision) {
    case ApprovalDecision.kAllow:
      return 'Allow';
    case ApprovalDecision.kAllowOnce:
      return 'Allow once';
    case ApprovalDecision.kDeny:
      return 'Deny';
    case ApprovalDecision.kNone:
    default:
      return 'None';
  }
}

export function getEventText(event: RuntimeEventRecord): string {
  return event.text || '';
}

export function isRenderableTimelineEvent(event: RuntimeEventRecord): boolean {
  switch (event.kind) {
    case RuntimeEventKind.kUserPrompt:
    case RuntimeEventKind.kSessionStatus:
    case RuntimeEventKind.kAssistantToken:
    case RuntimeEventKind.kAssistantThinking:
    case RuntimeEventKind.kTurnComplete:
    case RuntimeEventKind.kToolRequest:
    case RuntimeEventKind.kToolResult:
    case RuntimeEventKind.kApprovalRequest:
    case RuntimeEventKind.kApprovalResult:
    case RuntimeEventKind.kError:
    case RuntimeEventKind.kConnectionStateChanged:
      return true;
  }

  return false;
}

export interface AiProfile {
  id: string;
  name: string;
  systemPrompt: string;
  preferredModel: string | null;
  tools: string[];
  mcpServers: string[];
  isDefault: boolean;
  createdAt: string;
  updatedAt: string;
}

export interface AiWorkspace {
  id: string;
  name: string;
  profileId: string | null;
  spaceId: string | null;
  workspaceRoot: string | null;
  createdAt: string;
  updatedAt: string;
}

export interface AiMcpServer {
  id: string;
  workspaceId: string;
  name: string;
  transport: 'stdio' | 'http';
  command: string | null;
  url: string | null;
  authKeychainId: string | null;
  trusted: boolean;
  trustedTools: string[] | null;
  timeoutMs: number;
  outputCapBytes: number;
  createdAt: string;
  updatedAt: string;
}

export interface AiCliTool {
  id: string;
  workspaceId: string;
  name: string;
  description: string;
  commandTemplate: string;
  parametersSchema: any;
  sensitive: boolean;
  timeoutMs: number;
  outputCapBytes: number;
  workingDirectory: string | null;
  createdAt: string;
  updatedAt: string;
}
