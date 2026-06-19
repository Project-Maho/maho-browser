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
   ComposerAttachment,
   CURRENT_PAGE_ATTACHMENT,
   DEFAULT_HISTORY_SEARCH_STATE,
   DEFAULT_BOOKMARKS_SEARCH_STATE,
   DEFAULT_OPEN_TABS_STATE,
   createFileAttachment,
   createInitialState,
   getSubmitContextAttachments,
   hasCurrentPageAttachment,
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
    composer: {...state.composer, attachments: [...state.composer.attachments]},
    openTabs: {...state.openTabs, items: [...state.openTabs.items]},
    historySearch: {...state.historySearch, items: [...state.historySearch.items]},
    bookmarksSearch: {...state.bookmarksSearch, items: [...state.bookmarksSearch.items]},
    toast: state.toast ? {...state.toast} : null,
    aiProviders: state.aiProviders.map(provider => ({...provider, models: [...provider.models]})),
    aiSettings: {...state.aiSettings},
    turnPendingBySessionId: {...state.turnPendingBySessionId},
    contextsBySessionId: Object.fromEntries(
        Object.entries(state.contextsBySessionId).map(([key, value]) => [key, [...value]])),
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
        await this.resumeSession(this.state.currentSessionId);
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

  /**
   * Creates a new session without forwarding the prompt through
   * StartSession(initial_prompt), then submits the pending composer state
   * (prompt + attachments) via the normal submitPrompt path.  Use this for
   * first-send when there is no current session so that attachment state is
   * preserved correctly.
   */
  async startSessionThenSubmit(): Promise<void> {
    const mode = this.state.developerMode ?
        InteractionMode.kDeveloper :
        InteractionMode.kAssistant;
    // Pass null prompt so the backend does not auto-submit anything.
    const {session} = await this.pageHandler.startSession(null, mode);

    this.patch(state => {
      state.sessionsById[session.sessionId] = session;
      state.sessionOrder = sortSessionIds(state.sessionsById);
      state.currentSessionId = session.sessionId;
      state.selectedInspector = {kind: 'session', id: session.sessionId};
      // Do NOT clear the prompt or mark turnPending here; submitPrompt will.
      state.eventsBySessionId[session.sessionId] = [];
    });

    await this.resumeSession(session.sessionId, false);
    await this.submitPrompt();
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
      this.updateContextSnapshot(state, sessionId, replayEvents);
      if (replayEvents.length) {
        state.eventsBySessionId[sessionId] = replayEvents.filter(isRenderableTimelineEvent)
                                          .map(event => ({sessionId, event}));
      }
    });

    if (fetchHistory) {
      await this.loadHistory(sessionId);
    }
  }

  async loadHistory(sessionId: string, pageSize = 200): Promise<void> {
    const {events, totalCount} = await this.pageHandler.getSessionHistory(sessionId, 0, pageSize);
    this.patch(state => {
      this.updateContextSnapshot(state, sessionId, events);
      const entries = events.filter(isRenderableTimelineEvent).map(event => ({sessionId, event}));
      state.eventsBySessionId[sessionId] = entries;
      state.historiesBySessionId[sessionId] = {loaded: true, totalCount, loadedCount: entries.length};
    });
  }

  async loadEarlierHistory(sessionId: string, pageSize = 200): Promise<void> {
    const hist = this.state.historiesBySessionId[sessionId];
    if (!hist || hist.loadedCount >= hist.totalCount) {
      return;
    }

    const {events} = await this.pageHandler.getSessionHistory(sessionId, hist.loadedCount, pageSize);
    this.patch(state => {
      const incoming = events.filter(isRenderableTimelineEvent).map(event => ({sessionId, event}));
      const existing = state.eventsBySessionId[sessionId] || [];
      state.eventsBySessionId[sessionId] = [...incoming, ...existing];
      const current = state.historiesBySessionId[sessionId];
      if (current) {
        state.historiesBySessionId[sessionId] = {
          ...current,
          loadedCount: current.loadedCount + incoming.length,
        };
      }
    });
  }

  toggleAttachment(attachment: ComposerAttachment) {
    this.patch(state => {
      const idx = state.composer.attachments.findIndex(a => a.id === attachment.id);
      if (idx >= 0) {
        state.composer.attachments.splice(idx, 1);
      } else {
        state.composer.attachments.push(attachment);
      }
    });
  }

  addAttachment(attachment: ComposerAttachment) {
    this.patch(state => {
      if (!state.composer.attachments.some(a => a.id === attachment.id)) {
        state.composer.attachments.push(attachment);
      }
    });
  }

  removeAttachment(attachmentId: string) {
    this.patch(state => {
      state.composer.attachments = state.composer.attachments.filter(a => a.id !== attachmentId);
    });
  }

  toggleBrowserContext() {
    this.toggleAttachment(CURRENT_PAGE_ATTACHMENT);
  }

  async loadOpenTabs(force = false): Promise<void> {
    if (this.state.openTabs.loading || (this.state.openTabs.loaded && !force)) {
      return;
    }

    this.patch(state => {
      state.openTabs = {
        ...state.openTabs,
        loading: true,
        error: null,
      };
    });

    try {
      const {tabs} = await this.pageHandler.getOpenTabs();
      this.patch(state => {
        state.openTabs = {
          items: tabs,
          loading: false,
          loaded: true,
          error: null,
        };
      });
    } catch (error) {
      this.patch(state => {
        state.openTabs = {
          ...state.openTabs,
          loading: false,
          loaded: true,
          error: error instanceof Error ? error.message : String(error),
        };
      });
    }
  }

  resetOpenTabs() {
    this.patch(state => {
      state.openTabs = {...DEFAULT_OPEN_TABS_STATE, items: []};
    });
  }

  async searchHistory(query: string, maxResults = 20): Promise<void> {
    this.patch(state => {
      state.historySearch = {
        query,
        items: state.historySearch.items,
        loading: true,
        error: null,
      };
    });

    try {
      const {items} = await this.pageHandler.searchHistory(query, maxResults);
      this.patch(state => {
        state.historySearch = {
          query,
          items,
          loading: false,
          error: null,
        };
      });
    } catch (error) {
      this.patch(state => {
        state.historySearch = {
          ...state.historySearch,
          loading: false,
          error: error instanceof Error ? error.message : String(error),
        };
      });
    }
  }

  resetHistorySearch() {
    this.patch(state => {
      state.historySearch = {...DEFAULT_HISTORY_SEARCH_STATE};
    });
  }

  async searchBookmarks(query: string, maxResults = 20): Promise<void> {
    this.patch(state => {
      state.bookmarksSearch = {
        query,
        items: state.bookmarksSearch.items,
        loading: true,
        error: null,
      };
    });

    try {
      const {items} = await this.pageHandler.searchBookmarks(query, maxResults);
      this.patch(state => {
        state.bookmarksSearch = {
          query,
          items,
          loading: false,
          error: null,
        };
      });
    } catch (error) {
      this.patch(state => {
        state.bookmarksSearch = {
          ...state.bookmarksSearch,
          loading: false,
          error: error instanceof Error ? error.message : String(error),
        };
      });
    }
  }

  resetBookmarksSearch() {
    this.patch(state => {
      state.bookmarksSearch = {...DEFAULT_BOOKMARKS_SEARCH_STATE};
    });
  }

  addToast(text: string, tone: AppState['toast'] extends infer T ? (T extends {tone: infer Tone} ? Tone : never) : never = 'info') {
    this.patch(state => {
      state.toast = {text, tone};
    });
  }

  clearToast() {
    this.patch(state => {
      state.toast = null;
    });
  }

  async requestFileChooser(): Promise<void> {
    if (typeof document === 'undefined') {
      return;
    }

    const input = document.createElement('input');
    input.type = 'file';
    input.multiple = true;
    input.style.display = 'none';
    document.body.appendChild(input);

    const cleanup = () => {
      input.remove();
    };

    input.addEventListener('change', () => {
      const files = Array.from(input.files || []);
      cleanup();
      void (async () => {
        for (const file of files) {
          try {
            const attachment = await createFileAttachment(file);
            this.addAttachment(attachment);
          } catch (error) {
            this.addToast(error instanceof Error ? error.message : String(error), 'danger');
          }
        }
      })();
    }, {once: true});

    input.addEventListener('cancel', cleanup, {once: true});
    input.click();
  }

  async setActiveAI(providerId: string, modelId: string): Promise<void> {
    this.patch(state => {
      state.aiSettings = {
        activeModelId: modelId,
        activeProviderId: providerId,
      };
    });
  }

  async submitPrompt(): Promise<void> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    const prompt = this.state.composer.prompt.trim();
    if (!sessionId || !prompt || session?.isReadOnly) {
      return;
    }

    const attachments = getSubmitContextAttachments(this.state.composer.attachments);
    const attachBrowserContext = attachments === null &&
        hasCurrentPageAttachment(this.state.composer.attachments);
    const mode = this.state.developerMode ?
        InteractionMode.kDeveloper :
        InteractionMode.kAssistant;
    this.patch(state => {
      state.turnPendingBySessionId[sessionId] = true;
      state.composer.prompt = '';
    });

    await this.pageHandler.submitPrompt(
        sessionId, prompt, attachBrowserContext, mode, attachments);
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
          const existing = state.contextsBySessionId[sessionId] || [];
          state.contextsBySessionId[sessionId] = [...existing, event.browserContext];
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

  private updateContextSnapshot(state: AppState, sessionId: string, events: RuntimeEvent[]) {
    const payloads = events
        .filter(e => e.kind === RuntimeEventKind.kBrowserContextInjected && e.browserContext)
        .map(e => e.browserContext!);
    if (payloads.length) {
      state.contextsBySessionId[sessionId] = payloads;
      return;
    }

    delete state.contextsBySessionId[sessionId];
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
