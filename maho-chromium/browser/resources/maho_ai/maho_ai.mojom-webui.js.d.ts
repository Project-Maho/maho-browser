

export enum ChatIntent {
  kFreeform,
  kAnalyzeCurrentPage,
  kExplainCurrentPage,
  kSummarizeCurrentPage,
}

export enum ContextSourceKind {
  kCurrentPage,
  kOpenTab,
  kHistory,
  kBookmark,
  kFile,
}

export interface HistoryItem {
  url: string;
  title: string;
  visitedAt: number;
}

export interface BookmarkItem {
  id: string;
  url: string;
  title: string;
}


export interface TabItem {
  tabId: number;
  url: string;
  title: string;
  isActive: boolean;
  index: number;
}

export interface TabInfo {
  tabId: number;
  url: string;
  title: string;
}

export interface TidyFolder {
  name: string;
  tabIds: number[];
}

export interface CreditBalanceInfo {
  balanceUsd: number;
  lastPurchaseAt: number;
  lastConsumptionAt: number;
}

export interface ContextAttachment {
  kind: ContextSourceKind;
  tabId: number;
  label: string;
  url: string;
}

export enum InteractionMode {
  kAssistant,
  kDeveloper,
}

export enum ViewMode {
  kSidebar,
  kFloating,
}

export enum BrowserContextStatus {
  kInjected,
  kDeniedInternalPage,
  kNoActiveTab,
  kCannotAccess,
  kExtractionFailed,
  kNotRequested,
}

export interface BrowserContextPayload {
  status: BrowserContextStatus;
  url: string;
  title: string;
  contentSnippet: string;
  contentLength: number;
  warnings: string[];
  label: string;
}

export enum RuntimeConnectionState {
  kDisconnected,
  kConnecting,
  kConnected,
  kReconnecting,
  kError,
  kPausedForApproval,
  kResumed,
}

export enum RuntimeEventKind {
  kAssistantToken,
  kAssistantThinking,
  kTurnComplete,
  kError,
  kToolRequest,
  kToolResult,
  kApprovalRequest,
  kApprovalResult,
  kUserPrompt,
  kSessionStatus,
  kConnectionStateChanged,
  kBrowserContextInjected,
}

export enum ToolCallStatus {
  kPending,
  kRunning,
  kCompleted,
  kFailed,
  kCancelled,
}

export interface ToolCallInfo {
  callId: string;
  toolName: string;
  argumentsJson: string;
  status: ToolCallStatus;
}

export interface ToolResultInfo {
  callId: string;
  success: boolean;
  output: string;
  errorMessage?: string | null;
}

export interface ApprovalRequestInfo {
  approvalId: string;
  description: string;
  relatedToolCall?: ToolCallInfo | null;
}

export interface ApprovalResultInfo {
  approvalId: string;
  approved: boolean;
  reason?: string | null;
}

export interface RuntimeEvent {
  kind: RuntimeEventKind;
  sequence: number;
  timestamp: number;
  text?: string | null;
  toolCall?: ToolCallInfo | null;
  toolResult?: ToolResultInfo | null;
  approvalRequest?: ApprovalRequestInfo | null;
  approvalResult?: ApprovalResultInfo | null;
  browserContext?: BrowserContextPayload | null;
}

export enum SessionStatus {
  kCreated,
  kActive,
  kIdle,
  kPausedForApproval,
  kCompleted,
  kCancelled,
  kError,
}

export interface SessionInfo {
  sessionId: string;
  title?: string | null;
  summary?: string | null;
  createdAt: number;
  updatedAt?: number | null;
  adapterName: string;
  isActive: boolean;
  isReadOnly: boolean;
  status: SessionStatus;
  eventCount: number;
  toolCallCount: number;
  runtimeSessionId?: string | null;
  lastRuntimeState?: string | null;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  onRuntimeEvent: {
    addListener(listener: (event: RuntimeEvent) => void): void;
  };
  onConnectionStateChanged: {
    addListener(listener: (state: RuntimeConnectionState) => void): void;
  };
  onSessionUpdated: {
    addListener(listener: (session: SessionInfo) => void): void;
  };
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getConnectionState(): Promise<{
    state: RuntimeConnectionState;
    activeAdapterName: string;
  }>;
  getSessionList(): Promise<{sessions: SessionInfo[]}>;
  startSession(initialPrompt: string | null, mode: InteractionMode): Promise<{
    session: SessionInfo;
  }>;
  resumeSession(sessionId: string): Promise<{
    session: SessionInfo | null;
    replayEvents: RuntimeEvent[];
  }>;
  getSessionHistory(sessionId: string, offset: number, limit: number): Promise<{
    events: RuntimeEvent[];
    totalCount: number;
  }>;
  getOpenTabs(): Promise<{tabs: TabItem[]}>;
  searchHistory(query: string, maxResults: number): Promise<{items: HistoryItem[]}>;
  searchBookmarks(query: string, maxResults: number): Promise<{items: BookmarkItem[]}>;
  submitPrompt(
      sessionId: string,
      prompt: string,
      attachBrowserContext: boolean,
      mode: InteractionMode,
      attachments: ContextAttachment[] | null): Promise<void>;
  cancelTurn(sessionId: string): Promise<void>;
  respondToApproval(
      sessionId: string,
      approvalId: string,
      approved: boolean): Promise<void>;
  closePanel(): Promise<void>;
  openSettings(): Promise<void>;
  getViewMode(): Promise<{mode: ViewMode}>;
  setViewMode(mode: ViewMode): Promise<void>;
  requestTabTidy(tabs: TabInfo[]): Promise<{folders: TidyFolder[]}>;
  applyTabTidyFolders(folders: TidyFolder[]): Promise<void>;
  getCreditBalance(): Promise<{info: CreditBalanceInfo}>;
  getBuyCreditsUrl(packSizeUsd: number): Promise<{checkoutUrl: string}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
