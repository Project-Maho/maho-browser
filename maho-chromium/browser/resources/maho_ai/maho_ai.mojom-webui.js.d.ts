export interface ChatMessage {
  role: string;
  content: string;
  timestamp: number;
}

export enum ChatIntent {
  kFreeform,
  kAnalyzeCurrentPage,
  kExplainCurrentPage,
  kSummarizeCurrentPage,
}

export enum InteractionMode {
  kAssistant,
  kDeveloper,
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
  onStreamingResponse: {
    addListener(listener: (partialContent: string) => void): void;
  };
  onStreamingComplete: {
    addListener(listener: (response: ChatMessage) => void): void;
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
  sendMessage(intent: ChatIntent, message: string): Promise<void>;
  getPageContent(): Promise<{title: string; url: string; textContent: string}>;
  getChatHistory(): Promise<{messages: ChatMessage[]}>;
  clearHistory(): Promise<void>;
  setApiKey(key: string): Promise<void>;
  getApiKeyStatus(): Promise<{hasKey: boolean}>;
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
  submitPrompt(
      sessionId: string,
      prompt: string,
      attachBrowserContext: boolean,
      mode: InteractionMode): Promise<void>;
  cancelTurn(sessionId: string): Promise<void>;
  respondToApproval(
      sessionId: string,
      approvalId: string,
      approved: boolean): Promise<void>;
  closePanel(): Promise<void>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
