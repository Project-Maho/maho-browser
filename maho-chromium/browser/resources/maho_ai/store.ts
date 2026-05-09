import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEvent,
  RuntimeEventKind,
  SessionInfo,
} from './maho_ai.mojom-webui.js';
import {
  AppState,
  createInitialState,
  isRenderableTimelineEvent,
  TimelineEntry,
} from './types.js';

type Listener = (state: AppState) => void;

function cloneState(state: AppState): AppState {
  return {
    ...state,
    sessionsById: {...state.sessionsById},
    sessionOrder: [...state.sessionOrder],
    eventsBySessionId: Object.fromEntries(
        Object.entries(state.eventsBySessionId).map(([key, value]) => [key, [...value]])),
    historiesBySessionId: {...state.historiesBySessionId},
    toolCallsById: {...state.toolCallsById},
    toolResultsByCallId: {...state.toolResultsByCallId},
    approvalsById: {...state.approvalsById},
    approvalResultsById: {...state.approvalResultsById},
    selectedInspector: {...state.selectedInspector},
    composer: {...state.composer},
    turnPendingBySessionId: {...state.turnPendingBySessionId},
  };
}

function sortSessionIds(sessionsById: AppState['sessionsById']): string[] {
  return Object.values(sessionsById)
      .sort((a, b) => (b.updatedAt || b.createdAt) - (a.updatedAt || a.createdAt))
      .map(session => session.sessionId);
}

export class MahoAiStore {
  private state = createInitialState();
  private listeners = new Set<Listener>();

  constructor(
      private readonly pageHandler: PageHandlerRemote,
      private readonly callbackRouter: PageCallbackRouter) {
    this.bindCallbacks();
  }

  getSnapshot(): AppState {
    return cloneState(this.state);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    listener(this.getSnapshot());
    return () => this.listeners.delete(listener);
  }

  async bootstrap(initialPrompt?: string): Promise<void> {
    this.patch(state => {
      state.booting = true;
      state.bootError = null;
      state.composer.prompt = initialPrompt || '';
    });

    try {
      const [{state, activeAdapterName}, {sessions}] = await Promise.all([
        this.pageHandler.getConnectionState(),
        this.pageHandler.getSessionList(),
      ]);

      this.patch(next => {
        next.connectionState = state;
        next.activeAdapterName = activeAdapterName || 'OpenCode';
        for (const session of sessions) {
          next.sessionsById[session.sessionId] = session;
          if (!next.eventsBySessionId[session.sessionId]) {
            next.eventsBySessionId[session.sessionId] = [];
          }
        }
        next.sessionOrder = sortSessionIds(next.sessionsById);
        next.currentSessionId = sessions[0]?.sessionId || null;
        next.initialized = true;
        next.booting = false;
      });

      if (this.state.currentSessionId) {
        const currentSession = this.state.sessionsById[this.state.currentSessionId];
        await this.resumeSession(
            this.state.currentSessionId, currentSession?.isReadOnly ?? false);
      }
    } catch (error) {
      this.patch(state => {
        state.booting = false;
        state.bootError = error instanceof Error ? error.message : String(error);
      });
      throw error;
    }
  }

  setComposerPrompt(prompt: string) {
    this.patch(state => {
      state.composer.prompt = prompt;
    });
  }

  setSessionSearchQuery(query: string) {
    this.patch(state => {
      state.sessionSearchQuery = query;
    });
  }

  setSelectedInspector(kind: AppState['selectedInspector']['kind'], id?: string) {
    this.patch(state => {
      state.selectedInspector = {kind, id};
    });
  }

  async startSession(): Promise<void> {
    const prompt = this.state.composer.prompt.trim() || null;
    const {session} = await this.pageHandler.startSession(prompt);

    this.patch(state => {
      state.sessionsById[session.sessionId] = session;
      state.sessionOrder = sortSessionIds(state.sessionsById);
      state.currentSessionId = session.sessionId;
      state.selectedInspector = {kind: 'session', id: session.sessionId};
      state.turnPendingBySessionId[session.sessionId] = !!prompt;
      state.composer.prompt = '';
      state.eventsBySessionId[session.sessionId] = [];
    });

    await this.resumeSession(session.sessionId, false);
  }

  async resumeSession(sessionId: string, fetchHistory = true): Promise<void> {
    const {session, replayEvents} = await this.pageHandler.resumeSession(sessionId);

    this.patch(state => {
      state.currentSessionId = sessionId;
      if (session) {
        state.sessionsById[session.sessionId] = session;
        state.sessionOrder = sortSessionIds(state.sessionsById);
      }
      state.selectedInspector = {kind: 'session', id: sessionId};
      if (replayEvents.length) {
        state.eventsBySessionId[sessionId] = replayEvents.filter(isRenderableTimelineEvent)
                                          .map(event => ({sessionId, event}));
      }
    });

    if (fetchHistory) {
      await this.loadHistory(sessionId);
    }
  }

  async loadHistory(sessionId: string): Promise<void> {
    const {events, totalCount} = await this.pageHandler.getSessionHistory(sessionId, 0, 200);
    this.patch(state => {
      state.eventsBySessionId[sessionId] = events.filter(isRenderableTimelineEvent)
                                        .map(event => ({sessionId, event}));
      state.historiesBySessionId[sessionId] = {loaded: true, totalCount};
    });
  }

  async submitPrompt(): Promise<void> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    const prompt = this.state.composer.prompt.trim();
    if (!sessionId || !prompt || session?.isReadOnly) {
      return;
    }

    this.patch(state => {
      state.turnPendingBySessionId[sessionId] = true;
      state.composer.prompt = '';
    });

    await this.pageHandler.submitPrompt(sessionId, prompt);
  }

  async cancelTurn(): Promise<void> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    if (!sessionId || session?.isReadOnly) {
      return;
    }

    await this.pageHandler.cancelTurn(sessionId);
    this.patch(state => {
      state.turnPendingBySessionId[sessionId] = false;
    });
  }

  async respondToApproval(approvalId: string, approved: boolean): Promise<void> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    if (!sessionId || session?.isReadOnly) {
      return;
    }

    await this.pageHandler.respondToApproval(sessionId, approvalId, approved);
  }

  private bindCallbacks() {
    this.callbackRouter.onRuntimeEvent.addListener((event: RuntimeEvent) => {
      const sessionId = this.state.currentSessionId;
      if (!sessionId) {
        return;
      }

      this.patch(state => {
        this.appendEventInternal(state, sessionId, event);
        if (event.toolCall?.callId) {
          state.toolCallsById[event.toolCall.callId] = event.toolCall;
          state.selectedInspector = {kind: 'tool', id: event.toolCall.callId};
        }
        if (event.toolResult?.callId) {
          state.toolResultsByCallId[event.toolResult.callId] = event.toolResult;
          state.selectedInspector = {kind: 'tool', id: event.toolResult.callId};
        }
        if (event.approvalRequest?.approvalId) {
          state.approvalsById[event.approvalRequest.approvalId] = event.approvalRequest;
          state.selectedInspector = {kind: 'approval', id: event.approvalRequest.approvalId};
        }
        if (event.approvalResult?.approvalId) {
          state.approvalResultsById[event.approvalResult.approvalId] = event.approvalResult;
          state.selectedInspector = {kind: 'approval', id: event.approvalResult.approvalId};
        }
        if (event.kind === RuntimeEventKind.kTurnComplete || event.kind === RuntimeEventKind.kError) {
          state.turnPendingBySessionId[sessionId] = false;
        }
      });
    });

    this.callbackRouter.onConnectionStateChanged.addListener((state: RuntimeConnectionState) => {
      this.patch(snapshot => {
        snapshot.connectionState = state;
      });
    });

    this.callbackRouter.onSessionUpdated.addListener((session: SessionInfo) => {
      this.patch(state => {
        state.sessionsById[session.sessionId] = session;
        state.sessionOrder = sortSessionIds(state.sessionsById);
      });
    });

  }

  private appendEventInternal(state: AppState, sessionId: string, event: RuntimeEvent) {
    if (!isRenderableTimelineEvent(event)) {
      return;
    }

    const existing = state.eventsBySessionId[sessionId] || [];
    const previous = existing[existing.length - 1];

    if (event.kind === RuntimeEventKind.kAssistantToken && previous?.event.kind === RuntimeEventKind.kAssistantToken) {
      previous.event = {
        ...previous.event,
        text: `${previous.event.text || ''}${event.text || ''}`,
        timestamp: event.timestamp,
        sequence: event.sequence,
      };
      state.eventsBySessionId[sessionId] = [...existing.slice(0, -1), previous];
      return;
    }

    const nextEntry: TimelineEntry = {sessionId, event};
    state.eventsBySessionId[sessionId] = [...existing, nextEntry];
  }

  private patch(mutator: (state: AppState) => void) {
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    const snapshot = this.getSnapshot();
    for (const listener of this.listeners) {
      listener(snapshot);
    }
  }
}
