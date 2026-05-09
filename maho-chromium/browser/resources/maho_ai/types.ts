import {
  ApprovalRequestInfo,
  ApprovalResultInfo,
  RuntimeConnectionState,
  RuntimeEvent,
  RuntimeEventKind,
  SessionInfo,
  SessionStatus,
  ToolCallInfo,
  ToolResultInfo,
} from './maho_ai.mojom-webui.js';

export type ConnectionState = RuntimeConnectionState;
export type SessionRecord = SessionInfo;
export type RuntimeEventRecord = RuntimeEvent;
export type SessionStatusValue = SessionStatus;
export type ToolCallRecord = ToolCallInfo;
export type ToolResultRecord = ToolResultInfo;
export type ApprovalRequestRecord = ApprovalRequestInfo;
export type ApprovalResultRecord = ApprovalResultInfo;
export type EventKind = RuntimeEventKind;

export interface TimelineEntry {
  sessionId: string;
  event: RuntimeEventRecord;
}

export interface InspectorSelection {
  kind: 'session'|'tool'|'approval'|'context';
  id?: string;
}

export interface ComposerState {
  prompt: string;
  attachBrowserContext: boolean;
  modeLabel: string;
}

export interface SessionHistoryState {
  loaded: boolean;
  totalCount: number;
}

export interface AppState {
  initialized: boolean;
  booting: boolean;
  bootError: string|null;
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
  turnPendingBySessionId: Record<string, boolean>;
}

export const DEFAULT_COMPOSER_STATE: ComposerState = {
  prompt: '',
  attachBrowserContext: true,
  modeLabel: 'Task',
};

export function createInitialState(): AppState {
  return {
    initialized: false,
    booting: true,
    bootError: null,
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
    turnPendingBySessionId: {},
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
    case RuntimeEventKind.kTurnComplete:
    case RuntimeEventKind.kToolRequest:
    case RuntimeEventKind.kToolResult:
    case RuntimeEventKind.kApprovalRequest:
    case RuntimeEventKind.kApprovalResult:
    case RuntimeEventKind.kError:
    case RuntimeEventKind.kConnectionStateChanged:
      return true;
  }
}
