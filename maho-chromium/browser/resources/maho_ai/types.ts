import {
  ApprovalRequestInfo,
  ApprovalResultInfo,
  BookmarkItem,
  BrowserContextPayload as BrowserContextPayloadRecord,
  BrowserContextStatus,
  ContextAttachment as ContextAttachmentRecord,
  ContextSourceKind,
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
export {RuntimeEventKind, ViewMode};
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
export type OpenTabRecord = TabItem;
export type HistoryItemRecord = HistoryItem;
export type BookmarkItemRecord = BookmarkItem;
export type SubmitContextAttachmentRecord = ContextAttachmentRecord;

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
  tabId?: number;
  url?: string;
}

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
  const hasCurrentPage = hasCurrentPageAttachment(attachments);
  const tabAttachments = attachments.filter(isTabAttachment);
  const historyAttachments = attachments.filter(a => a.kind === 'history');
  const bookmarkAttachments = attachments.filter(a => a.kind === 'bookmark');
  if (!tabAttachments.length && !historyAttachments.length && !bookmarkAttachments.length) {
    return null;
  }

  const submitAttachments: SubmitContextAttachmentRecord[] = [];
  if (hasCurrentPage) {
    submitAttachments.push({
      kind: ContextSourceKind.kCurrentPage,
      tabId: -1,
      label: CURRENT_PAGE_ATTACHMENT.label,
      url: '',
    });
  }

  submitAttachments.push(...tabAttachments.map(attachment => ({
    kind: ContextSourceKind.kOpenTab,
    tabId: attachment.tabId ?? -1,
    label: attachment.label,
    url: attachment.url || '',
  })));

  submitAttachments.push(...historyAttachments.map(attachment => ({
    kind: ContextSourceKind.kHistory,
    tabId: -1,
    label: attachment.label,
    url: attachment.url || '',
  })));

  submitAttachments.push(...bookmarkAttachments.map(attachment => ({
    kind: ContextSourceKind.kBookmark,
    tabId: -1,
    label: attachment.label,
    url: attachment.url || '',
  })));

  return submitAttachments;
}

export interface SessionHistoryState {
  loaded: boolean;
  totalCount: number;
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
  openTabs: OpenTabsState;
  historySearch: HistorySearchState;
  bookmarksSearch: BookmarksSearchState;
  turnPendingBySessionId: Record<string, boolean>;
  contextsBySessionId: Record<string, BrowserContextPayload[]>;
}

export const DEFAULT_COMPOSER_STATE: ComposerState = {
  prompt: '',
  attachments: [],
  modeLabel: 'Task',
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
    openTabs: {...DEFAULT_OPEN_TABS_STATE, items: []},
    historySearch: {...DEFAULT_HISTORY_SEARCH_STATE},
    bookmarksSearch: {...DEFAULT_BOOKMARKS_SEARCH_STATE},
    turnPendingBySessionId: {},
    contextsBySessionId: {},
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
