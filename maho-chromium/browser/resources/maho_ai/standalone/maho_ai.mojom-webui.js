const API_PREFIX = '/api';

function nowSeconds() {
  return Date.now() / 1000;
}

function jsonStringify(value) {
  if (typeof value === 'string') {
    return value;
  }

  try {
    return JSON.stringify(value ?? {});
  } catch {
    return '{}';
  }
}

function readObject(value) {
  return value && typeof value === 'object' ? value : {};
}

function readString(value, fallback = '') {
  return typeof value === 'string' ? value : fallback;
}

function readNullableString(value) {
  return typeof value === 'string' ? value : null;
}

function approvalPolicyFromPayload(value) {
  switch (readString(value, 'prompt').replaceAll('-', '_')) {
    case 'allow_all':
    case 'allow':
      return ApprovalPolicy.kAllowAll;
    case 'allow_mcp':
      return ApprovalPolicy.kAllowMcp;
    case 'deny_sensitive':
      return ApprovalPolicy.kDenySensitive;
    case 'deny_all':
    case 'deny':
      return ApprovalPolicy.kDenyAll;
    case 'prompt':
    default:
      return ApprovalPolicy.kPrompt;
  }
}

function approvalSensitivityFromPayload(value) {
  return readString(value) === 'read_only' ?
    ApprovalSensitivity.kReadOnly : ApprovalSensitivity.kSensitive;
}

function approvalDecisionFromReply(reply) {
  if (reply === 'always' || reply === 'allow') {
    return ApprovalDecision.kAllow;
  }
  if (reply === 'once' || reply === 'yes' || reply === 'approve') {
    return ApprovalDecision.kAllowOnce;
  }
  return ApprovalDecision.kDeny;
}

function approvalStateFromReply(approved) {
  return approved ? ApprovalState.kApproved : ApprovalState.kDenied;
}

function readNumber(value, fallback = 0) {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

// Extract a human-readable error message from a session.error SSE payload.
// Canonical OpenCode shape: { error: { name, data: { message } } }
// Compatibility fallbacks (in priority order after canonical):
//   payload.error.message, top-level payload.message, string payload.error
function readSessionErrorMessage(payload) {
  const err = payload.error;
  if (err && typeof err === 'object') {
    const dataMsg = readString(readObject(err.data).message);
    if (dataMsg) return dataMsg;
    const errMsg = readString(err.message);
    if (errMsg) return errMsg;
  }
  const topMsg = readString(payload.message);
  if (topMsg) return topMsg;
  if (typeof err === 'string' && err) return err;
  return 'Runtime error';
}

function eventSessionId(payload, fallbackSessionId) {
  return readString(
      payload.sessionID ?? payload.sessionId ?? payload.session?.id ?? payload.session?.sessionId,
      fallbackSessionId || '');
}

function createStandaloneRoutineState() {
  return {
    routines: [{
      id: 'routine-briefing',
      name: 'Morning briefing',
      cron: '0 9 * * 1-5',
      description: 'Summarize today’s priorities and open tabs.',
      isCustom: true,
      schedule: '0 9 * * 1-5',
      trigger: null,
      enabled: true,
    }],
    statuses: [{
      runId: 'run-approval',
      routineId: 'routine-briefing',
      source: 'manual',
      state: 2,
      revision: 3n,
      result: null,
      error: null,
      approval: {approvalId: 'approval-1'},
    }],
    history: [{
      resultId: 1n,
      routineId: 'routine-briefing',
      ranAt: 1n,
      success: true,
      content: 'Briefing prepared with three priorities.',
      source: 'manual',
    }],
  };
}

let nextListenerId = 1;

class ListenerList {
  constructor() {
    this.listeners = new Map();
  }

  addListener(listener) {
    // Mojo callback-router listener ids are unique across the router, not per
    // event. Per-list ids make removeListener(1) detach the first unrelated
    // event listener and can silently disconnect the store before a fixture
    // emits its interactive approval result.
    const id = nextListenerId++;
    this.listeners.set(id, listener);
    return id;
  }

  removeListener(id) {
    return this.listeners.delete(id);
  }

  emit(...args) {
    for (const listener of this.listeners.values()) {
      listener(...args);
    }
  }
}

export const InteractionMode = {
  kAssistant: 0,
  kDeveloper: 1,
};

export const ContextSourceKind = {
  kCurrentPage: 0,
  kOpenTab: 1,
  kHistory: 2,
  kBookmark: 3,
  kFile: 4,
};

export const ChatIntent = {
  kFreeform: 0,
  kSummarizeCurrentPage: 1,
  kQuizCurrentPage: 2,
};

export const CompactSurface = {
  kChat: 0,
  kRoutines: 1,
};

export const ViewMode = {
  kSidebar: 0,
  kFloating: 1,
};

export const BrowserContextStatus = {
  kInjected: 0,
  kDeniedInternalPage: 1,
  kNoActiveTab: 2,
  kCannotAccess: 3,
  kExtractionFailed: 4,
  kNotRequested: 5,
};

export const RuntimeConnectionState = {
  kDisconnected: 0,
  kConnecting: 1,
  kConnected: 2,
  kReconnecting: 3,
  kError: 4,
  kPausedForApproval: 5,
  kResumed: 6,
};

export const RuntimeEventKind = {
  kAssistantToken: 0,
  kTurnComplete: 1,
  kError: 2,
  kToolRequest: 3,
  kToolResult: 4,
  kApprovalRequest: 5,
  kApprovalResult: 6,
  kUserPrompt: 7,
  kSessionStatus: 8,
  kConnectionStateChanged: 9,
  kBrowserContextInjected: 10,
  kArtifactCreated: 11,
};

export const ToolCallStatus = {
  kPending: 0,
  kRunning: 1,
  kCompleted: 2,
  kFailed: 3,
  kCancelled: 4,
};

export const ApprovalPolicy = {
  kPrompt: 0,
  kAllowAll: 1,
  kAllowMcp: 2,
  kDenySensitive: 3,
  kDenyAll: 4,
};

export const ApprovalSensitivity = {
  kReadOnly: 0,
  kSensitive: 1,
};

export const ApprovalState = {
  kPending: 0,
  kApproved: 1,
  kDenied: 2,
  kCancelled: 3,
};

export const ApprovalDecision = {
  kNone: 0,
  kAllow: 1,
  kAllowOnce: 2,
  kDeny: 3,
};

export const SessionStatus = {
  kCreated: 0,
  kActive: 1,
  kIdle: 2,
  kPausedForApproval: 3,
  kCompleted: 4,
  kCancelled: 5,
  kError: 6,
};

// Mirrors enum ControlActivityState in
// browser/ui/webui/maho_ai/maho_ai.mojom.
export const ControlActivityState = {
  kIdle: 0,
  kReading: 1,
  kActing: 2,
  kWaitingApproval: 3,
  kPaused: 4,
  kDisconnected: 5,
  kFailed: 6,
};

function toSessionStatus(value) {
  switch (value) {
    case 'active':
      return SessionStatus.kActive;
    case 'idle':
      return SessionStatus.kIdle;
    case 'paused_for_approval':
    case 'paused':
      return SessionStatus.kPausedForApproval;
    case 'completed':
      return SessionStatus.kCompleted;
    case 'cancelled':
      return SessionStatus.kCancelled;
    case 'error':
      return SessionStatus.kError;
    default:
      return SessionStatus.kCreated;
  }
}

function toToolStatus(value) {
  switch (value) {
    case 'running':
      return ToolCallStatus.kRunning;
    case 'completed':
      return ToolCallStatus.kCompleted;
    case 'failed':
    case 'error':
      return ToolCallStatus.kFailed;
    case 'cancelled':
      return ToolCallStatus.kCancelled;
    default:
      return ToolCallStatus.kPending;
  }
}

export class PageCallbackRouter {
  constructor() {
    this.$ = {
      bindNewPipeAndPassRemote: () => this,
    };
    this.onRuntimeEvent = new ListenerList();
    this.onConnectionStateChanged = new ListenerList();
    this.onSessionUpdated = new ListenerList();
    this.onAISettingsChanged = new ListenerList();
    this.onAskMahoSessionAccepted = new ListenerList();
    this.onSurfaceRequested = new ListenerList();
    this.onRoutineRunStatusChanged = new ListenerList();
    this.onControlActivityChanged = new ListenerList();
    this.onVoicePartial = new ListenerList();
    this.onVoiceFinal = new ListenerList();
    this.onVoiceError = new ListenerList();
  }

  removeListener(_id) {
    // The standalone root intentionally runs React StrictMode. Its development
    // effect probe calls store.dispose() during the simulated unmount, then
    // reuses the same store instance without rebinding callbacks. Native Mojo
    // pipes survive that probe; keep the fixture callbacks connected too so
    // later interactive runtime events reach the mounted store.
    return true;
  }
}

export class PageHandlerFactory {
  static getRemote() {
    return {
      createPageHandler(callbackRemote, handlerReceiver) {
        if (handlerReceiver && typeof handlerReceiver.attachRouter === 'function') {
          handlerReceiver.attachRouter(callbackRemote);
        }
      },
    };
  }
}

const globalActionLogs = [];
const globalComposerDrafts = new Map();
const globalFixtureSessions = new Set();
const allHandlers = new Set();
const allRouters = new Set();
const globalSessionsById = new Map();
const globalSessionOrder = [];
const globalEventsBySessionId = new Map();
const globalSequenceBySessionId = new Map();
let globalCurrentSessionId = null;
let globalControlActivityEntries = [];
let primaryPageHandler = null;

export class PageHandlerRemote {
  constructor() {
    this.$ = {
      bindNewPipeAndPassReceiver: () => this,
    };
    allHandlers.add(this);
    if (!primaryPageHandler) {
      primaryPageHandler = this;
    }
    this.callbackRouter = null;
    const params = typeof window === 'undefined' ? null :
        new URLSearchParams(window.location.search);
    this.routineState = createStandaloneRoutineState();
    this.connectionState = params?.get('disconnected') === 'true' ?
        RuntimeConnectionState.kDisconnected : RuntimeConnectionState.kConnecting;
    this.eventSource = null;
    this.sseConnected = false;
    this.fixtureErrorMode = false;
    this.pendingControlActivityCompletion = null;
    this.profiles = [
      { id: 'profile-default', name: 'General Agent', isDefault: true },
      { id: 'profile-long', name: 'Enterprise Security Research Organization Lead', isDefault: false },
    ];
    this.activeProfileId = params?.get('longprofile') === 'true' ?
        'profile-long' : 'profile-default';
    this.fixtureActionMode = params?.get('fixtureactions') === 'true';
    this.workspaces = [
      { id: 'ws-default', name: 'Default Workspace', spaceId: 'space-default', profileId: this.activeProfileId },
    ];
    this.activeWorkspace = this.workspaces[0];
    this.currentViewMode = ViewMode.kSidebar;
    this.aiSettings = {
      activeProviderId: 'anthropic',
      activeModelId: 'claude-3-5-sonnet-20241022',
      activeReasoningEffort: 1,
      providerOptions: [{
        id: 'anthropic',
        label: 'Anthropic',
        modelOptions: [
          {id: 'claude-3-5-sonnet-20241022', label: 'Claude 3.5 Sonnet'},
          {id: 'claude-3-7-sonnet-20250219', label: 'Claude 3.7 Sonnet'},
        ],
      }],
      reasoningOptions: [
        {effort: 0, label: 'Low'},
        {effort: 1, label: 'Medium'},
        {effort: 2, label: 'High'},
      ],
    };
  }

  get sessionsById() {
    return globalSessionsById;
  }

  get sessionOrder() {
    return globalSessionOrder;
  }

  set sessionOrder(list) {
    globalSessionOrder.length = 0;
    globalSessionOrder.push(...list);
  }

  get eventsBySessionId() {
    return globalEventsBySessionId;
  }

  get sequenceBySessionId() {
    return globalSequenceBySessionId;
  }

  get currentSessionId() {
    return globalCurrentSessionId;
  }

  set currentSessionId(value) {
    globalCurrentSessionId = value;
  }

  attachRouter(callbackRouter) {
    this.callbackRouter = callbackRouter;
    if (callbackRouter) {
      allRouters.add(callbackRouter);
    }
    if (typeof window !== 'undefined') {
      window.__mahoAiStandalone = primaryPageHandler || this;
    }
  }

  getActionLogs() {
    return structuredClone(globalActionLogs);
  }

  clearActionLogs() {
    globalActionLogs.length = 0;
  }

  async getAiProfiles() {
    return {
      profilesJson: JSON.stringify(this.profiles),
    };
  }

  async getAiWorkspaces() {
    return {
      workspacesJson: JSON.stringify(this.workspaces),
    };
  }

  async getActiveAiWorkspace(_spaceId) {
    return {
      workspaceJson: JSON.stringify(this.activeWorkspace),
    };
  }

  async getCliTools(_workspaceId) {
    return {toolsJson: '[]'};
  }

  async getMcpServers(_workspaceId) {
    return {serversJson: '[]'};
  }

  async switchWorkspaceProfile(workspaceId, profileId) {
    this.activeProfileId = profileId;
    if (this.activeWorkspace) {
      this.activeWorkspace.profileId = profileId;
    }
    globalActionLogs.push({
      action: 'switchWorkspaceProfile',
      workspaceId,
      profileId,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    return {success: true};
  }

  async getAISettings() {
    return {
      info: {
        activeProviderId: this.aiSettings.activeProviderId,
        activeModelId: this.aiSettings.activeModelId,
        activeReasoningEffort: this.aiSettings.activeReasoningEffort,
        providerOptions: structuredClone(this.aiSettings.providerOptions),
        reasoningOptions: structuredClone(this.aiSettings.reasoningOptions),
      },
    };
  }

  async setDefaultAISelection(providerId, modelId, reasoningEffort) {
    this.aiSettings.activeProviderId = providerId;
    this.aiSettings.activeModelId = modelId;
    this.aiSettings.activeReasoningEffort = reasoningEffort;
    const action = {
      action: 'setDefaultAISelection',
      providerId,
      modelId,
      reasoningEffort,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    };
    globalActionLogs.push(action);
    window.dispatchEvent(new CustomEvent('maho-fixture-action', {detail: action}));
    return {accepted: true};
  }

  async getCreditBalance() {
    return {
      info: {
        balanceUsd: 100.0,
        lastPurchaseAt: nowSeconds() - 86400,
        lastConsumptionAt: nowSeconds() - 60,
        sessionExpired: false,
      },
    };
  }

  async getBuyCreditsUrl(packSizeUsd) {
    return {checkoutUrl: `https://checkout.maho.dev/credits?amount=${packSizeUsd}`};
  }

  async getArtifactPreviewUrl(artifactId) {
    return {url: `blob:mock-preview-${artifactId}`};
  }

  async getArtifactExportUrl(artifactId) {
    return {url: `blob:mock-export-${artifactId}`};
  }

  async renameArtifact(_sessionId, _artifactId, displayName) {
    return {displayName};
  }

  async deleteArtifact(_sessionId, _artifactId) {
    return {ok: true};
  }

  async startVoiceSession() {
    globalActionLogs.push({
      action: 'startVoiceSession',
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    return {accepted: true};
  }

  async pushAudioChunk(_pcm16k) {}

  async stopVoiceSession() {
    globalActionLogs.push({
      action: 'stopVoiceSession',
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
  }

  async respondToInteraction(sessionId, requestId, dispatch) {
    globalActionLogs.push({
      action: 'respondToInteraction',
      sessionId,
      requestId,
      dispatch,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    return {ok: true};
  }

  loadFixtureState(stateName, customOptions = {}) {
    const sessionId = customOptions.sessionId || `session-${stateName}`;
    globalFixtureSessions.add(sessionId);
    this.fixtureErrorMode = Boolean(customOptions.transportError);
    this.sessionsById.clear();
    this.sessionOrder = [sessionId];
    this.eventsBySessionId.clear();
    this.sequenceBySessionId.clear();
    this.currentSessionId = sessionId;

    const baseSession = {
      sessionId,
      title: customOptions.title || `Session ${stateName}`,
      summary: null,
      createdAt: nowSeconds(),
      updatedAt: nowSeconds(),
      adapterName: 'OpenCode',
      isActive: true,
      isReadOnly: Boolean(customOptions.isReadOnly),
      status: SessionStatus.kActive,
      eventCount: 0,
      toolCallCount: 0,
      runtimeSessionId: sessionId,
      lastRuntimeState: 'connected',
    };

    if (stateName === 'idle') {
      baseSession.title = 'New conversation';
      baseSession.status = SessionStatus.kIdle;
    }
    this.upsertSession(sessionId, baseSession);
    const historyCount = Math.max(0, Number(customOptions.historyCount ?? 1));
    for (let index = 1; index <= historyCount; index++) {
      const historySessionId = `session-history-${index}`;
      globalFixtureSessions.add(historySessionId);
      this.upsertSession(historySessionId, {
        ...baseSession,
        sessionId: historySessionId,
        title: `History item ${index}`,
        isActive: false,
        isReadOnly: index % 4 === 0,
        runtimeSessionId: historySessionId,
        updatedAt: baseSession.updatedAt - index,
      });
    }

    if (stateName === 'idle') {
      this.callbackRouter?.onSessionUpdated.emit(baseSession);
      this.emitConnectionState(customOptions.connectionState ?? RuntimeConnectionState.kConnected);
      return;
    }

    this.callbackRouter?.onAskMahoSessionAccepted.emit('req-' + stateName, baseSession);
    this.emitConnectionState(
        customOptions.connectionState ?? RuntimeConnectionState.kConnected);

    switch (stateName) {
      case 'conversation': {
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Summarize the architecture of Maho Browser sidebar.',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kAssistantToken, {
          text: 'Maho Browser uses an isolated WebUI architecture for side panels, connecting the TypeScript/React layer to Chromium services via Mojo bindings with strict permission gates.',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kTurnComplete, {
          text: null,
        });
        break;
      }
      case 'pending-tool': {
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Check workspace file structure',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kToolRequest, {
          toolCall: {
            callId: 'call-inspect-1',
            toolName: 'workspace_inspect_files',
            argumentsJson: '{"path":"maho-chromium/browser/resources/maho_ai","recursive":false}',
            status: ToolCallStatus.kRunning,
          },
        });
        break;
      }
      case 'approval': {
        baseSession.status = SessionStatus.kPausedForApproval;
        this.callbackRouter?.onSessionUpdated.emit(baseSession);
        this.emitConnectionState(RuntimeConnectionState.kPausedForApproval);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Run git status check',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kApprovalRequest, {
          approvalRequest: {
            approvalId: 'approval-baseline-1',
            approvalPolicy: ApprovalPolicy.kPrompt,
            description: 'Run shell command: git status --porcelain',
            pageDerivedJustification: false,
            relatedToolCall: {
              callId: 'call-git-status',
              toolName: 'terminal_execute',
              argumentsJson: '{"cmd":"git status --porcelain"}',
              status: ToolCallStatus.kPending,
            },
            sensitivity: ApprovalSensitivity.kSensitive,
            state: ApprovalState.kPending,
          },
        });
        break;
      }
      case 'denied': {
        // The denied frame must be produced by a real user rejection, so the
        // fixture only publishes the pending request. The QA runner clicks
        // Reject, which dispatches respondToApproval(approved=false) through
        // the standalone transport and emits the matching approvalResult.
        baseSession.status = SessionStatus.kPausedForApproval;
        this.callbackRouter?.onSessionUpdated.emit(baseSession);
        this.emitConnectionState(RuntimeConnectionState.kPausedForApproval);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Execute sensitive system command',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kApprovalRequest, {
          approvalRequest: {
            approvalId: 'approval-denied-1',
            approvalPolicy: ApprovalPolicy.kPrompt,
            description: 'Run shell command: git reset --hard HEAD',
            pageDerivedJustification: false,
            relatedToolCall: {
              callId: 'call-denied-1',
              toolName: 'terminal_execute',
              argumentsJson: '{"cmd":"git reset --hard HEAD"}',
              status: ToolCallStatus.kPending,
            },
            sensitivity: ApprovalSensitivity.kSensitive,
            state: ApprovalState.kPending,
          },
        });
        break;
      }
      case 'transport-error': {
        baseSession.status = SessionStatus.kPausedForApproval;
        this.callbackRouter?.onSessionUpdated.emit(baseSession);
        this.emitConnectionState(RuntimeConnectionState.kPausedForApproval);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Dispatch remote deployment step',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kApprovalRequest, {
          approvalRequest: {
            approvalId: 'approval-transport-err-1',
            approvalPolicy: ApprovalPolicy.kPrompt,
            description: 'Run deployment script: deploy-staging.sh',
            pageDerivedJustification: false,
            relatedToolCall: {
              callId: 'call-deploy-1',
              toolName: 'deploy_script',
              argumentsJson: '{"script":"deploy-staging.sh"}',
              status: ToolCallStatus.kPending,
            },
            sensitivity: ApprovalSensitivity.kSensitive,
            state: ApprovalState.kPending,
          },
        });
        break;
      }
      case 'activity-completed': {
        baseSession.status = SessionStatus.kCompleted;
        this.callbackRouter?.onSessionUpdated.emit(baseSession);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Run lint checks on repository',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kToolRequest, {
          toolCall: {
            callId: 'call-lint-1',
            toolName: 'run_linter',
            argumentsJson: '{"target":"browser/resources"}',
            status: ToolCallStatus.kCompleted,
          },
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kToolResult, {
          toolResult: {
            callId: 'call-lint-1',
            success: true,
            output: '{"checkedFiles":142,"errors":0,"warnings":0}',
            errorMessage: null,
          },
        });
        // A completed control activity is not a distinct mojo enum: the C++
        // controller drives active states while it works and then reports
        // kIdle (or clears the list) once the run is finished. The fixture
        // therefore replays the real active phase first and immediately
        // performs the terminal active -> idle transition, so the projection
        // under test is the genuine post-completion render (both
        // ControlActivityCard and ControlActivityTimeline drop out) rather
        // than a frozen spinner mislabelled as "completed".
        this.emitControlActivity([
          {
            controllerName: 'Maho Lint Runner',
            state: ControlActivityState.kReading,
            targetTitle: 'Maho AI Workspace',
          },
          {
            controllerName: 'Maho Lint Runner',
            state: ControlActivityState.kActing,
            targetTitle: 'Maho AI Workspace',
          },
        ]);
        // When the caller wants to witness the transition (QA proving the
        // active phase really preceded the terminal one), the idle emission is
        // deferred until completeControlActivity() is invoked explicitly.
        if (customOptions.deferControlActivityCompletion) {
          this.pendingControlActivityCompletion = {
            controllerName: 'Maho Lint Runner',
            targetTitle: 'Maho AI Workspace',
            sessionId,
          };
        } else {
          this.completeControlActivity('Maho Lint Runner', 'Maho AI Workspace');
          this.emitRuntimeEvent(sessionId, RuntimeEventKind.kTurnComplete, {
            text: null,
          });
        }
        break;
      }
      case 'artifact': {
        baseSession.status = SessionStatus.kCompleted;
        this.callbackRouter?.onSessionUpdated.emit(baseSession);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
          text: 'Export current analysis as JSON artifact',
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kArtifactCreated, {
          artifact: {
            artifactId: 'art-baseline-1',
            displayName: 'sidebar-panel-summary.json',
            kind: 'file',
            mimeType: 'application/json',
            sizeBytes: 2048n,
            createdAt: nowSeconds(),
          },
        });
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kTurnComplete, {
          text: null,
        });
        break;
      }
      default:
        throw new Error(`Unknown fixture state: ${stateName}`);
    }
  }

  async getConnectionState() {
    const connected = await this.probeConnection();
    return {
      state: connected ? RuntimeConnectionState.kConnected : RuntimeConnectionState.kDisconnected,
      activeAdapterName: 'OpenCode',
    };
  }

  // The standalone harness has no browser-side controller driving tabs, so the
  // control-activity timeline starts empty and is only fed by
  // emitControlActivity() below.
  async watchControlActivity() {
    return {entries: structuredClone(globalControlActivityEntries)};
  }

  emitControlActivity(entries) {
    globalControlActivityEntries = entries;
    for (const router of allRouters) {
      router.onControlActivityChanged?.emit(entries);
    }
  }

  // Terminal transition for a controller run: the last entry moves to kIdle,
  // which is what the browser-side controller reports when it stops driving
  // the tab. Recorded in the action log so evidence can prove the transition
  // actually happened instead of inferring it from a state name.
  completeControlActivity(controllerName, targetTitle) {
    const activeEntries = globalControlActivityEntries.map(entry => ({...entry}));
    globalActionLogs.push({
      action: 'controlActivityTerminalTransition',
      controllerName,
      fromStates: activeEntries.map(entry => entry.state),
      toState: ControlActivityState.kIdle,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    this.emitControlActivity([
      {
        controllerName,
        state: ControlActivityState.kIdle,
        targetTitle: targetTitle ?? null,
      },
    ]);
  }

  getControlActivityEntries() {
    return structuredClone(globalControlActivityEntries);
  }

  // Runs the deferred terminal transition registered by loadFixtureState.
  // Returns false when nothing was pending so callers cannot mistake a no-op
  // for a real completion.
  finishPendingControlActivity() {
    const owner = [...allHandlers].find(
        handler => handler.pendingControlActivityCompletion) ?? null;
    const pending = owner?.pendingControlActivityCompletion;
    if (!owner || !pending) {
      return false;
    }
    owner.pendingControlActivityCompletion = null;
    owner.completeControlActivity(pending.controllerName, pending.targetTitle);
    owner.emitRuntimeEvent(pending.sessionId, RuntimeEventKind.kTurnComplete, {
      text: null,
    });
    return true;
  }

  async consumePendingSurface(lastSeenGeneration) {
    return {
      request: {
        surface: CompactSurface.kChat,
        generation: lastSeenGeneration,
      },
    };
  }

  async listAllRoutines() {
    globalActionLogs.push({
      action: 'openRoutines',
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    return {routines: structuredClone(this.routineState.routines)};
  }

  async createRoutine(name, prompt, schedule, trigger) {
    this.routineState.routines.push({
      id: `routine-${this.routineState.routines.length + 1}`,
      name,
      cron: schedule ?? '',
      description: prompt,
      isCustom: true,
      schedule,
      trigger,
      enabled: true,
    });
    return {ok: true};
  }

  async startRoutine(id) {
    const status = {
      runId: `run-${this.routineState.statuses.length + 1}`,
      routineId: id,
      source: 'manual',
      state: 1,
      revision: 1n,
      result: null,
      error: null,
      approval: null,
    };
    this.routineState.statuses.push(status);
    this.callbackRouter?.onRoutineRunStatusChanged.emit(structuredClone(status));
    return {runId: status.runId, error: null};
  }

  async getRoutineRunStatuses() {
    return {statuses: structuredClone(this.routineState.statuses)};
  }

  async getRoutineUserTier() {
    return {tier: 2};
  }

  async getUserTier() {
    return this.getRoutineUserTier();
  }

  async respondToRoutineApproval(runId, approvalId, approved) {
    const status = this.routineState.statuses.find(candidate =>
      candidate.runId === runId &&
      candidate.approval?.approvalId === approvalId);
    if (!status) {
      return {ok: false};
    }
    status.state = approved ? 3 : 4;
    status.revision += 1n;
    status.result = approved ? 'Routine approved and completed.' : null;
    status.error = approved ? null : 'Routine approval denied.';
    status.approval = null;
    this.callbackRouter?.onRoutineRunStatusChanged.emit(structuredClone(status));
    return {ok: true};
  }

  async listRunHistory(routineId, limit) {
    return {
      records: structuredClone(this.routineState.history
          .filter(record => routineId === null || record.routineId === routineId)
          .slice(0, limit || this.routineState.history.length)),
    };
  }

  async getSessionList() {
    if (typeof window !== 'undefined') {
      const params = new URLSearchParams(window.location.search);
      const stateParam = params.get('state');
      if (stateParam && !this.initialStateLoaded) {
        this.initialStateLoaded = true;
        this.loadFixtureState(stateParam, {
          isReadOnly: params.get('readonly') === 'true',
          historyCount: Number(params.get('historycount') ?? '1'),
          deferControlActivityCompletion:
              params.get('deferactivitycompletion') === 'true',
          connectionState: params.get('disconnected') === 'true' ?
              RuntimeConnectionState.kDisconnected : undefined,
        });
      }
    }
    await this.refreshSessions();
    return {
      sessions: this.sessionOrder.map(sessionId => this.sessionsById.get(sessionId)).filter(Boolean),
    };
  }

  async startSession(initialPrompt, mode) {
    if (typeof window !== 'undefined' &&
        new URLSearchParams(window.location.search).get('fixtureactions') === 'true') {
      globalActionLogs.push({
        action: 'startSession',
        initialPrompt,
        mode,
        timestamp: Date.now(),
        transport: 'simulated_standalone_fixture',
      });
      const sessionId = `session-new-${Date.now()}`;
      const session = this.upsertSession(sessionId, {
        adapterName: 'OpenCode',
        isActive: true,
        isReadOnly: false,
        lastRuntimeState: 'connected',
        runtimeSessionId: sessionId,
        status: SessionStatus.kActive,
        updatedAt: nowSeconds(),
      });
      this.currentSessionId = sessionId;
      return {session};
    }
    if (this.currentSessionId && globalFixtureSessions.has(this.currentSessionId)) {
      globalActionLogs.push({
        action: 'startSession',
        initialPrompt,
        mode,
        timestamp: Date.now(),
        transport: 'simulated_standalone_fixture',
      });
      const sessionId = `session-new-${Date.now()}`;
      const session = this.upsertSession(sessionId, {
        adapterName: 'OpenCode',
        isActive: true,
        isReadOnly: false,
        lastRuntimeState: 'connected',
        runtimeSessionId: sessionId,
        status: SessionStatus.kActive,
        updatedAt: nowSeconds(),
      });
      this.currentSessionId = sessionId;
      return {session};
    }
    const response = await fetch(`${API_PREFIX}/session`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({}),
    });

    if (!response.ok) {
      throw new Error(`Failed to create session (${response.status})`);
    }

    const payload = readObject(await response.json());
    const sessionId = readString(payload.id ?? payload.sessionId);
    if (!sessionId) {
      throw new Error('OpenCode session response did not include an id.');
    }

    const session = this.upsertSession(sessionId, {
      adapterName: 'OpenCode',
      isActive: true,
      isReadOnly: false,
      lastRuntimeState: 'connected',
      runtimeSessionId: sessionId,
      status: SessionStatus.kActive,
      updatedAt: nowSeconds(),
    });
    this.currentSessionId = sessionId;
    this.emitConnectionState(RuntimeConnectionState.kConnected);
    this.ensureEventStream();

    if (initialPrompt && initialPrompt.trim()) {
      await this.submitPrompt(sessionId, initialPrompt, false, mode);
    }

    return {session};
  }

  async resumeSession(sessionId) {
    if ((typeof window !== 'undefined' &&
         new URLSearchParams(window.location.search).get('fixtureactions') === 'true') ||
        globalFixtureSessions.has(sessionId)) {
      globalActionLogs.push({
        action: 'resumeSession',
        sessionId,
        timestamp: Date.now(),
        transport: 'simulated_standalone_fixture',
      });
    } else {
      await this.refreshSessions();
    }
    const session = this.sessionsById.get(sessionId) || this.upsertSession(sessionId, {
      adapterName: 'OpenCode',
      isActive: sessionId === this.currentSessionId,
      isReadOnly: false,
      lastRuntimeState: 'connected',
      runtimeSessionId: sessionId,
      status: SessionStatus.kActive,
      updatedAt: nowSeconds(),
    });
    this.currentSessionId = sessionId;
    this.ensureEventStream();

    return {
      session,
      replayEvents: [...(this.eventsBySessionId.get(sessionId) || [])],
    };
  }

  async getSessionHistory(sessionId, offset, limit) {
    const events = this.eventsBySessionId.get(sessionId) || [];
    return {
      events: events.slice(offset, offset + limit),
      totalCount: events.length,
    };
  }

  async getOpenTabs() {
    return {tabs: []};
  }

  async searchHistory(query, maxResults) {
    return {items: []};
  }

  async searchBookmarks(query, maxResults) {
    return {items: []};
  }


  async composerDraftGet(scopeJson) {
    return {draftJson: globalComposerDrafts.get(scopeJson) ?? null};
  }

  async composerDraftSet(scopeJson, text) {
    globalComposerDrafts.set(scopeJson, JSON.stringify({text}));
    globalActionLogs.push({
      action: 'composerDraftSet',
      scopeJson,
      text,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    return {ok: true};
  }

  async composerDraftDelete(scopeJson) {
    globalComposerDrafts.delete(scopeJson);
    globalActionLogs.push({
      action: 'composerDraftDelete',
      scopeJson,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
    return {ok: true};
  }

  async submitPrompt(sessionId, prompt, attachBrowserContext, mode) {
    const trimmedPrompt = prompt.trim();
    if (!trimmedPrompt) {
      return;
    }

    this.currentSessionId = sessionId;
    this.ensureEventStream();
    this.emitBrowserContextEvent(sessionId, attachBrowserContext);
    this.emitRuntimeEvent(sessionId, RuntimeEventKind.kUserPrompt, {
      text: trimmedPrompt,
    });
    this.emitRuntimeEvent(sessionId, RuntimeEventKind.kSessionStatus, {
      text: 'active',
    });
    this.upsertSession(sessionId, {
      isActive: true,
      lastRuntimeState: 'connected',
      runtimeSessionId: sessionId,
      status: SessionStatus.kActive,
      updatedAt: nowSeconds(),
    });

    const agent = mode === InteractionMode.kDeveloper ? 'build' : 'build';
    const response = await fetch(`${API_PREFIX}/session/${encodeURIComponent(sessionId)}/prompt_async`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        agent,
        parts: [{
          type: 'text',
          text: trimmedPrompt,
        }],
      }),
    });

    if (!response.ok) {
      const errorMessage = `Failed to submit prompt (${response.status})`;
      this.emitRuntimeEvent(sessionId, RuntimeEventKind.kError, {
        text: errorMessage,
      });
      this.upsertSession(sessionId, {
        isActive: true,
        lastRuntimeState: 'error',
        runtimeSessionId: sessionId,
        status: SessionStatus.kError,
        updatedAt: nowSeconds(),
      });
      throw new Error(errorMessage);
    }
    return {accepted: true};
  }

  async cancelTurn(sessionId) {
    if (globalFixtureSessions.has(sessionId)) {
      const action = {
        action: 'cancelTurn',
        sessionId,
        timestamp: Date.now(),
        transport: 'simulated_standalone_fixture',
      };
      globalActionLogs.push(action);
      window.dispatchEvent(new CustomEvent('maho-fixture-action', {detail: action}));
      this.emitRuntimeEvent(sessionId, RuntimeEventKind.kSessionStatus, {
        text: 'stopped',
      });
      return {ok: true};
    }
    const response = await fetch(`${API_PREFIX}/session/${encodeURIComponent(sessionId)}/abort`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: '{}',
    });

    if (!response.ok) {
      throw new Error(`Failed to cancel turn (${response.status})`);
    }
  }

  async respondToApproval(sessionId, approvalId, approved) {
    if (globalFixtureSessions.has(sessionId)) {
      const isTransportError = this.fixtureErrorMode || sessionId.includes('transport-error');
      const actionRecord = {
        action: 'respondToApproval',
        sessionId,
        approvalId,
        approved,
        timestamp: Date.now(),
        transport: 'simulated_standalone_fixture',
        error: isTransportError ? '502 Bad Gateway' : null,
        requestBody: {
          response: approved ? 'once' : 'reject',
        },
      };
      globalActionLogs.push(actionRecord);

      if (isTransportError) {
        throw new Error('Simulated standalone transport failure: 502 Bad Gateway');
      }

      const approvalResult = {
        approvalId,
        approvalPolicy: ApprovalPolicy.kPrompt,
        approved,
        decision: approved ? ApprovalDecision.kAllowOnce : ApprovalDecision.kDeny,
        pageDerivedJustification: false,
        reason: approved ? 'Allowed once via fixture' : 'User explicitly denied approval',
        sensitivity: ApprovalSensitivity.kSensitive,
        state: approved ? ApprovalState.kApproved : ApprovalState.kDenied,
      };
      for (const handler of allHandlers) {
        handler.emitRuntimeEvent(sessionId, RuntimeEventKind.kApprovalResult, {
          approvalResult,
        });
      }
      // Match the production lifecycle: once a decision result is projected,
      // the session leaves the paused-for-approval state. Emitting this update
      // through the existing router keeps the fixture interactive rather than
      // preloading a terminal card.
      const session = this.upsertSession(sessionId, {
        isActive: true,
        lastRuntimeState: 'connected',
        status: SessionStatus.kActive,
        updatedAt: nowSeconds(),
      });
      for (const router of allRouters) {
        router.onSessionUpdated?.emit(session);
      }
      this.emitConnectionState(RuntimeConnectionState.kConnected);
      globalActionLogs.push({
        action: 'approvalResultEmitted',
        sessionId,
        approvalId,
        approved,
        timestamp: Date.now(),
        transport: 'simulated_standalone_fixture',
        resultBody: approvalResult,
      });
      return {ok: true};
    }

    const response = await fetch(
        `${API_PREFIX}/session/${encodeURIComponent(sessionId)}/permissions/${encodeURIComponent(approvalId)}`,
        {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({
            response: approved ? 'once' : 'reject',
          }),
        });

    if (!response.ok) {
      throw new Error(`Failed to submit approval (${response.status})`);
    }
    return {ok: true};
  }

  async closePanel() {
    globalActionLogs.push({
      action: 'closePanel',
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
  }

  async openSettings() {
    globalActionLogs.push({
      action: 'openSettings',
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
  }

  async openSettingsPane(paneKey) {
    globalActionLogs.push({
      action: 'openSettingsPane',
      paneKey,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
  }

  async getViewMode() {
    return {mode: this.currentViewMode || ViewMode.kSidebar};
  }

  async setViewMode(mode) {
    this.currentViewMode = mode;
    globalActionLogs.push({
      action: 'setViewMode',
      mode,
      timestamp: Date.now(),
      transport: 'simulated_standalone_fixture',
    });
  }

  async probeConnection() {
    try {
      const response = await fetch(`${API_PREFIX}/session`);
      this.emitConnectionState(response.ok ? RuntimeConnectionState.kConnected : RuntimeConnectionState.kDisconnected);
      return response.ok;
    } catch {
      this.emitConnectionState(RuntimeConnectionState.kDisconnected);
      return false;
    }
  }

  async refreshSessions() {
    if (typeof window !== 'undefined' &&
        new URLSearchParams(window.location.search).get('fixtureactions') === 'true') {
      return;
    }
    try {
      const response = await fetch(`${API_PREFIX}/session`);
      if (!response.ok) {
        return;
      }

      const payload = await response.json();
      const sessions = Array.isArray(payload) ? payload : [];
      for (const entry of sessions) {
        const object = readObject(entry);
        const sessionId = readString(
            object.id ?? object.sessionId ?? object.runtimeSessionId ?? object.runtime_session_id);
        if (!sessionId) {
          continue;
        }

        this.upsertSession(sessionId, {
          adapterName: readString(object.adapterName ?? object.adapter_name, 'OpenCode'),
          createdAt: readNumber(object.createdAt ?? object.created_at, nowSeconds()),
          eventCount: readNumber(object.eventCount ?? object.event_count, 0),
          isActive: !!(object.isActive ?? object.is_active ?? sessionId === this.currentSessionId),
          isReadOnly: !!(object.isReadOnly ?? object.is_read_only),
          lastRuntimeState: readNullableString(
              object.lastRuntimeState ?? object.last_runtime_state ?? 'connected'),
          runtimeSessionId: readNullableString(
              object.runtimeSessionId ?? object.runtime_session_id ?? sessionId),
          status: toSessionStatus(readString(object.status)),
          summary: readNullableString(object.summary),
          title: readNullableString(object.title),
          toolCallCount: readNumber(object.toolCallCount ?? object.tool_call_count, 0),
          updatedAt: readNumber(object.updatedAt ?? object.updated_at, nowSeconds()),
        });
      }
    } catch {
      this.emitConnectionState(RuntimeConnectionState.kDisconnected);
    }
  }

  ensureEventStream() {
    if (this.eventSource) {
      return;
    }

    this.emitConnectionState(RuntimeConnectionState.kConnecting);
    this.eventSource = new EventSource(`${API_PREFIX}/event`);
    const eventTypes = [
      'server.connected',
      'message.part.delta',
      'message.part.updated',
      'session.status',
      'session.completed',
      'message.completed',
      'session.idle',
      'session.error',
      'permission.asked',
      'permission.replied',
    ];

    for (const eventType of eventTypes) {
      this.eventSource.addEventListener(eventType, event => {
        this.handleSseEvent(eventType, event.data);
      });
    }

    this.eventSource.addEventListener('message', event => {
      this.handleSseEvent('', event.data);
    });

    this.eventSource.onerror = () => {
      this.sseConnected = false;
      this.emitConnectionState(RuntimeConnectionState.kReconnecting);
    };
  }

  handleSseEvent(namedType, rawData) {
    const envelope = readObject(this.safeParseJson(rawData));
    const eventType = namedType || readString(envelope.type);
    const payload = readObject(envelope.properties ?? envelope.payload ?? envelope.data ?? envelope);
    const sessionId = eventSessionId(payload, this.currentSessionId);

    if (!eventType || !sessionId) {
      return;
    }

    if (!this.sessionsById.has(sessionId)) {
      this.upsertSession(sessionId, {
        isActive: sessionId === this.currentSessionId,
        lastRuntimeState: 'connected',
        runtimeSessionId: sessionId,
        status: SessionStatus.kActive,
        updatedAt: nowSeconds(),
      });
    }

    switch (eventType) {
      case 'server.connected':
        this.sseConnected = true;
        this.emitConnectionState(RuntimeConnectionState.kConnected);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kConnectionStateChanged, {
          text: 'connected',
        });
        break;
      case 'message.part.delta':
        if (readString(payload.field) === 'text' && readString(payload.delta)) {
          this.emitRuntimeEvent(sessionId, RuntimeEventKind.kAssistantToken, {
            text: readString(payload.delta),
          });
        }
        break;
      case 'message.part.updated':
        this.handleUpdatedPart(sessionId, payload);
        break;
      case 'session.status': {
        const statusType = readString(payload.status?.type ?? payload.status ?? payload.type);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kSessionStatus, {
          text: statusType,
        });
        this.upsertSession(sessionId, {
          isActive: true,
          lastRuntimeState: statusType || 'connected',
          runtimeSessionId: sessionId,
          status: toSessionStatus(statusType),
          updatedAt: nowSeconds(),
        });
        if (statusType === 'idle') {
          this.emitRuntimeEvent(sessionId, RuntimeEventKind.kTurnComplete, {
            text: null,
          });
        }
        break;
      }
      case 'session.completed':
      case 'message.completed':
      case 'session.idle':
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kTurnComplete, {
          text: null,
        });
        this.upsertSession(sessionId, {
          isActive: true,
          lastRuntimeState: 'idle',
          runtimeSessionId: sessionId,
          status: SessionStatus.kIdle,
          updatedAt: nowSeconds(),
        });
        break;
      case 'session.error': {
        const errorMessage = readSessionErrorMessage(payload);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kError, {
          text: errorMessage,
        });
        this.upsertSession(sessionId, {
          isActive: true,
          lastRuntimeState: 'error',
          runtimeSessionId: sessionId,
          status: SessionStatus.kError,
          updatedAt: nowSeconds(),
        });
        this.emitConnectionState(RuntimeConnectionState.kError);
        break;
      }
      case 'permission.asked': {
        const toolCall = this.createToolCall(payload);
        const approvalId = readString(payload.id);
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kApprovalRequest, {
          approvalRequest: {
            approvalId,
            approvalPolicy: approvalPolicyFromPayload(payload.approval_policy || payload.approvalPolicy),
            description: readString(
                payload.description || payload.title || payload.method,
                'Approval required'),
            pageDerivedJustification: Boolean(payload.page_derived_justification || payload.pageDerivedJustification),
            relatedToolCall: toolCall ? {...toolCall, argumentsJson: ''} : null,
            sensitivity: approvalSensitivityFromPayload(payload.sensitivity),
            state: ApprovalState.kPending,
          },
        });
        this.upsertSession(sessionId, {
          isActive: true,
          lastRuntimeState: 'paused_for_approval',
          runtimeSessionId: sessionId,
          status: SessionStatus.kPausedForApproval,
          updatedAt: nowSeconds(),
        });
        this.emitConnectionState(RuntimeConnectionState.kPausedForApproval);
        break;
      }
      case 'permission.replied': {
        const reply = readString(payload.reply);
        const approved = reply === 'once' || reply === 'always' || reply === 'yes' || reply === 'approve';
        this.emitRuntimeEvent(sessionId, RuntimeEventKind.kApprovalResult, {
          approvalResult: {
            approvalId: readString(payload.requestID || payload.requestId || payload.id),
            approvalPolicy: approvalPolicyFromPayload(payload.approval_policy || payload.approvalPolicy),
            approved,
            decision: approvalDecisionFromReply(reply),
            pageDerivedJustification: Boolean(payload.page_derived_justification || payload.pageDerivedJustification),
            reason: readNullableString(payload.reason),
            sensitivity: approvalSensitivityFromPayload(payload.sensitivity),
            state: approvalStateFromReply(approved),
          },
        });
        this.upsertSession(sessionId, {
          isActive: true,
          lastRuntimeState: 'resumed',
          runtimeSessionId: sessionId,
          status: SessionStatus.kActive,
          updatedAt: nowSeconds(),
        });
        this.emitConnectionState(RuntimeConnectionState.kResumed);
        break;
      }
    }
  }

  handleUpdatedPart(sessionId, payload) {
    const part = readObject(payload.part);
    if (readString(part.type) !== 'tool') {
      return;
    }

    const state = readObject(part.state ?? payload.state);
    const status = readString(state.status || part.status || payload.status);
    const toolCall = this.createToolCall(payload);
    if (status === 'completed' || status === 'failed' || status === 'error' || status === 'cancelled') {
      this.emitRuntimeEvent(sessionId, RuntimeEventKind.kToolResult, {
        toolResult: {
          callId: toolCall.callId,
          success: status === 'completed',
          output: jsonStringify(
              state.output ?? part.output ?? part.result ?? payload.output ?? payload.result ?? ''),
          errorMessage: readNullableString(
              state.error ?? part.error ?? payload.error ?? payload.message),
        },
      });
      return;
    }

    this.emitRuntimeEvent(sessionId, RuntimeEventKind.kToolRequest, {
      toolCall: {
        ...toolCall,
        status: toToolStatus(status),
      },
    });
    this.bumpToolCallCount(sessionId);
  }

  createToolCall(payload) {
    const part = readObject(payload.part);
    const input = readObject(payload.input ?? part.input);
    return {
      callId: readString(part.callID ?? part.callId ?? part.id ?? payload.callID ?? payload.callId ?? payload.id),
      toolName: readString(
          input.tool ?? part.toolName ?? part.tool ?? part.name ?? payload.tool ?? payload.name,
          'tool'),
      argumentsJson: jsonStringify(input.arguments ?? part.arguments ?? payload.arguments ?? {}),
      status: toToolStatus(readString(part.state?.status ?? payload.state?.status ?? payload.status)),
    };
  }

  emitBrowserContextEvent(sessionId, attachBrowserContext) {
    this.emitRuntimeEvent(sessionId, RuntimeEventKind.kBrowserContextInjected, {
      browserContext: attachBrowserContext ? {
        status: BrowserContextStatus.kExtractionFailed,
        url: '',
        title: 'Standalone harness',
        contentSnippet: '',
        contentLength: 0,
        warnings: ['Standalone harness does not provide live browser page context.'],
      } : {
        status: BrowserContextStatus.kNotRequested,
        url: '',
        title: '',
        contentSnippet: '',
        contentLength: 0,
        warnings: [],
      },
    });
  }

  emitConnectionState(state) {
    this.connectionState = state;
    for (const router of allRouters) {
      router.onConnectionStateChanged?.emit(state);
    }
  }

  emitRuntimeEvent(sessionId, kind, extras) {
    const runtimeEvent = {
      approvalRequest: null,
      approvalResult: null,
      browserContext: null,
      kind,
      sequence: this.nextSequence(sessionId),
      text: null,
      timestamp: nowSeconds(),
      toolCall: null,
      toolResult: null,
      sessionId,
      ...extras,
    };

    const events = this.eventsBySessionId.get(sessionId) || [];
    events.push(runtimeEvent);
    this.eventsBySessionId.set(sessionId, events);
    const session = this.upsertSession(sessionId, {
      eventCount: events.length,
      isActive: true,
      runtimeSessionId: sessionId,
      updatedAt: runtimeEvent.timestamp,
    });
    for (const router of allRouters) {
      router.onRuntimeEvent?.emit(runtimeEvent);
      router.onSessionUpdated?.emit(session);
    }
  }

  upsertSession(sessionId, overrides = {}) {
    const existing = this.sessionsById.get(sessionId);
    const session = {
      sessionId,
      title: null,
      summary: null,
      createdAt: existing?.createdAt ?? nowSeconds(),
      updatedAt: existing?.updatedAt ?? null,
      adapterName: 'OpenCode',
      isActive: sessionId === this.currentSessionId,
      isReadOnly: false,
      status: SessionStatus.kCreated,
      eventCount: this.eventsBySessionId.get(sessionId)?.length || 0,
      toolCallCount: existing?.toolCallCount ?? 0,
      runtimeSessionId: sessionId,
      lastRuntimeState: 'connected',
      ...existing,
      ...overrides,
    };
    this.sessionsById.set(sessionId, session);
    if (!this.sessionOrder.includes(sessionId)) {
      this.sessionOrder.unshift(sessionId);
    }
    this.sessionOrder.sort((left, right) => {
      const leftSession = this.sessionsById.get(left);
      const rightSession = this.sessionsById.get(right);
      return readNumber(rightSession?.updatedAt ?? rightSession?.createdAt) -
          readNumber(leftSession?.updatedAt ?? leftSession?.createdAt);
    });
    return session;
  }

  bumpToolCallCount(sessionId) {
    const session = this.sessionsById.get(sessionId);
    this.upsertSession(sessionId, {
      toolCallCount: readNumber(session?.toolCallCount, 0) + 1,
      updatedAt: nowSeconds(),
    });
  }

  nextSequence(sessionId) {
    const next = (this.sequenceBySessionId.get(sessionId) || 0) + 1;
    this.sequenceBySessionId.set(sessionId, next);
    return next;
  }

  safeParseJson(value) {
    try {
      return JSON.parse(value);
    } catch {
      return null;
    }
  }

  async listArtifacts(sessionId) {
    return {artifacts: []};
  }

  async renameArtifact(artifactId, displayName) {
    return {success: true};
  }

  async deleteArtifact(artifactId) {
    return {success: true};
  }

  async getArtifactPreviewUrl(artifactId) {
    return {url: null};
  }

  async getArtifactExportUrl(artifactId) {
    return {url: null};
  }

}
