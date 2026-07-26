

export enum AskMahoSource {
  kCommandPalette,
}

export enum AskMahoContextIntent {
  kNone,
  kCurrentPage,
}

export interface AskMahoDispatch {
  requestId: string;
  query: string;
  source: AskMahoSource;
  submit: boolean;
  mode: InteractionMode;
  contextIntent: AskMahoContextIntent;
  targetSessionId: string | null;
}

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

export interface AISettingsInfo {
  activeProviderId: string;
  activeModelId: string;
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
  kToolAvailabilityChanged,
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

export enum ApprovalPolicy {
  kPrompt,
  kAllowAll,
  kAllowMcp,
  kDenySensitive,
  kDenyAll,
}

export enum ApprovalSensitivity {
  kReadOnly,
  kSensitive,
}

export enum ApprovalState {
  kPending,
  kApproved,
  kDenied,
  kCancelled,
}

export enum ApprovalDecision {
  kNone,
  kAllow,
  kAllowOnce,
  kDeny,
}

export interface ApprovalRequestInfo {
  approvalId: string;
  description: string;
  approvalPolicy: ApprovalPolicy;
  sensitivity: ApprovalSensitivity;
  state: ApprovalState;
  pageDerivedJustification: boolean;
  relatedToolCall?: ToolCallInfo | null;
}

export interface ApprovalResultInfo {
  approvalId: string;
  approved: boolean;
  reason?: string | null;
  approvalPolicy: ApprovalPolicy;
  sensitivity: ApprovalSensitivity;
  state: ApprovalState;
  decision: ApprovalDecision;
  pageDerivedJustification: boolean;
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
  sessionId: string;
  requestId?: string | null;
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
  onAISettingsChanged: {
    addListener(listener: (info: AISettingsInfo) => void): void;
  };
  onAskMahoSessionAccepted: {
    addListener(listener: (requestId: string, sessionInfo: SessionInfo) => void): void;
  };
  onVoicePartial: {
    addListener(listener: (transcript: string) => void): number;
  };
  onVoiceFinal: {
    addListener(listener: (transcript: string) => void): number;
  };
  onVoiceError: {
    addListener(listener: (message: string) => void): number;
  };
  removeListener(id: number): boolean;
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
  openSettingsPane(paneKey: string): Promise<void>;
  getAISettings(): Promise<{info: AISettingsInfo}>;
  getViewMode(): Promise<{mode: ViewMode}>;
  setViewMode(mode: ViewMode): Promise<void>;
  requestTabTidy(tabs: TabInfo[]): Promise<{folders: TidyFolder[]}>;
  applyTabTidyFolders(folders: TidyFolder[]): Promise<void>;
  getCreditBalance(): Promise<{info: CreditBalanceInfo}>;
  getBuyCreditsUrl(packSizeUsd: number): Promise<{checkoutUrl: string}>;
  startVoiceSession(): Promise<{accepted: boolean}>;
  pushAudioChunk(pcm16k: number[]): Promise<void>;
  stopVoiceSession(): Promise<void>;

  // AI Extensibility methods
  createAiProfile(name: string, systemPrompt: string, model: string | null): Promise<{profileId: string | null}>;
  getAiProfiles(): Promise<{profilesJson: string}>;
  updateAiProfile(id: string, profileJson: string): Promise<{success: boolean}>;
  deleteAiProfile(id: string): Promise<{success: boolean}>;
  importProfileFromToml(workspaceId: string, tomlPath: string): Promise<{profileIdOrError: string | null}>;

  createAiWorkspace(name: string, spaceId: string | null): Promise<{workspaceJson: string | null}>;
  getActiveAiWorkspace(spaceId: string): Promise<{workspaceJson: string | null}>;
  getAiWorkspaces(): Promise<{workspacesJson: string}>;
  switchWorkspaceProfile(workspaceId: string, profileId: string): Promise<{success: boolean}>;

  registerMcpServer(workspaceId: string, configJson: string): Promise<{success: boolean}>;
  removeMcpServer(workspaceId: string, serverName: string): Promise<{success: boolean}>;
  getMcpServers(workspaceId: string): Promise<{serversJson: string}>;
  approveMcpServerTrust(workspaceId: string, serverName: string, tools: string[]): Promise<{success: boolean}>;

  registerCliTool(workspaceId: string, toolJson: string): Promise<{success: boolean}>;
  removeCliTool(workspaceId: string, toolName: string): Promise<{success: boolean}>;
  getCliTools(workspaceId: string): Promise<{toolsJson: string}>;

  getWorkspaceEffectiveTools(workspaceId: string): Promise<{toolsJson: string}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
