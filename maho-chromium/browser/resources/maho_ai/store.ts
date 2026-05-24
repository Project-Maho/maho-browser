import {
  InteractionMode,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEvent,
  RuntimeEventKind,
  SessionInfo,
  ViewMode,
} from './maho_ai.mojom-webui.js';
import {
  AppState,
  createInitialState,
  getLastBrowserContext,
  isRenderableTimelineEvent,
  TimelineEntry,
} from './types.js';
import {notifyListeners} from '../maho_common/react/store_utils.js';
import type {Listener} from '../maho_common/react/store_utils.js';
import {bindMojoPageHandler} from '../maho_common/react/use_mojo.js';

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
    lastContextBySessionId: {...state.lastContextBySessionId},
  };
}

function sortSessionIds(sessionsById: AppState['sessionsById']): string[] {
  return Object.values(sessionsById)
      .sort((a, b) => (b.updatedAt || b.createdAt) - (a.updatedAt || a.createdAt))
      .map(session => session.sessionId);
}

export function createMahoAiStore(): MahoAiStore {
  const {router, handler} = bindMojoPageHandler(
      PageCallbackRouter, PageHandlerRemote, PageHandlerFactory);
  return new MahoAiStore(handler, router);
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
    return this.state;
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
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

  setDeveloperMode(enabled: boolean) {
    this.patch(state => {
      state.developerMode = enabled;
    });
  }

  async startSession(): Promise<void> {
    const prompt = this.state.composer.prompt.trim() || null;
    const mode = this.state.developerMode ?
        InteractionMode.kDeveloper :
        InteractionMode.kAssistant;
    const {session} = await this.pageHandler.startSession(prompt, mode);

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
      this.updateLastContextSnapshot(state, sessionId, replayEvents);
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
      this.updateLastContextSnapshot(state, sessionId, events);
      state.eventsBySessionId[sessionId] = events.filter(isRenderableTimelineEvent)
                                        .map(event => ({sessionId, event}));
      state.historiesBySessionId[sessionId] = {loaded: true, totalCount};
    });
  }

  toggleBrowserContext() {
    this.patch(state => {
      state.composer.attachBrowserContext = !state.composer.attachBrowserContext;
    });
  }

  async submitPrompt(): Promise<void> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    const prompt = this.state.composer.prompt.trim();
    if (!sessionId || !prompt || session?.isReadOnly) {
      return;
    }

    const attachBrowserContext = this.state.composer.attachBrowserContext;
    const mode = this.state.developerMode ?
        InteractionMode.kDeveloper :
        InteractionMode.kAssistant;
    this.patch(state => {
      state.turnPendingBySessionId[sessionId] = true;
      state.composer.prompt = '';
    });

    await this.pageHandler.submitPrompt(sessionId, prompt, attachBrowserContext, mode);
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

  async closePanel(): Promise<void> {
    await this.pageHandler.closePanel();
  }

  async openSettings(): Promise<void> {
    await this.pageHandler.openSettings();
  }

  async getViewMode(): Promise<ViewMode> {
    const {mode} = await this.pageHandler.getViewMode();
    return mode;
  }

  async setViewMode(mode: ViewMode): Promise<void> {
    await this.pageHandler.setViewMode(mode);
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
        if (event.kind === RuntimeEventKind.kBrowserContextInjected && event.browserContext) {
          state.lastContextBySessionId[sessionId] = event.browserContext;
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

    // Thinking events accumulate separately from assistant tokens.
    if (event.kind === RuntimeEventKind.kAssistantThinking && previous?.event.kind === RuntimeEventKind.kAssistantThinking) {
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

  private updateLastContextSnapshot(state: AppState, sessionId: string, events: RuntimeEvent[]) {
    const browserContext = getLastBrowserContext(events);
    if (browserContext) {
      state.lastContextBySessionId[sessionId] = browserContext;
      return;
    }

    delete state.lastContextBySessionId[sessionId];
  }

  private patch(mutator: (state: AppState) => void) {
    // Each Mojo IPC event triggers an individual patch+notify cycle.
    // useSyncExternalStore re-renders synchronously per notify, so tokens
    // paint progressively. If tokens appear batched visually, it's because
    // the model emits faster than the 16ms frame budget — not a batching bug.
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    notifyListeners(this.listeners);
  }
}
