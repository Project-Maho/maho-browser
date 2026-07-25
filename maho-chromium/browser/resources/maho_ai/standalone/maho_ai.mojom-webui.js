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

class ListenerList {
  constructor() {
    this.listeners = new Set();
  }

  addListener(listener) {
    this.listeners.add(listener);
  }

  emit(value) {
    for (const listener of this.listeners) {
      listener(value);
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

export class PageHandlerRemote {
  constructor() {
    this.$ = {
      bindNewPipeAndPassReceiver: () => this,
    };
    this.callbackRouter = null;
    this.connectionState = RuntimeConnectionState.kConnecting;
    this.sessionsById = new Map();
    this.sessionOrder = [];
    this.eventsBySessionId = new Map();
    this.sequenceBySessionId = new Map();
    this.currentSessionId = null;
    this.eventSource = null;
    this.sseConnected = false;
  }

  attachRouter(callbackRouter) {
    this.callbackRouter = callbackRouter;
  }

  async getConnectionState() {
    const connected = await this.probeConnection();
    return {
      state: connected ? RuntimeConnectionState.kConnected : RuntimeConnectionState.kDisconnected,
      activeAdapterName: 'OpenCode',
    };
  }

  async getSessionList() {
    await this.refreshSessions();
    return {
      sessions: this.sessionOrder.map(sessionId => this.sessionsById.get(sessionId)).filter(Boolean),
    };
  }

  async startSession(initialPrompt, mode) {
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
    await this.refreshSessions();
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
  }

  async cancelTurn(sessionId) {
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
  }

  async closePanel() {}

  async openSettings() {
    window.open('chrome://maho-settings/', '_blank');
  }

  async getViewMode() {
    return {mode: ViewMode.kSidebar};
  }

  async setViewMode(_mode) {}

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
    this.callbackRouter?.onConnectionStateChanged.emit(state);
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
    this.callbackRouter?.onRuntimeEvent.emit(runtimeEvent);
    this.callbackRouter?.onSessionUpdated.emit(session);
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
}
