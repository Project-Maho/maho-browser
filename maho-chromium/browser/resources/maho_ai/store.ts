import {
  InteractionMode,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEvent,
  RuntimeEventKind,
  SessionInfo,
  TabInfo,
  TidyFolder,
  ViewMode,
} from './maho_ai.mojom-webui.js';
import {
    AI_REASONING_EFFORT,
    DEFAULT_AI_REASONING_OPTIONS,
    AppState,
    AIModelOptionPayload,
    AIModelOptionRecord,
    AIProviderOptionPayload,
    AIProviderOptionRecord,
    AIReasoningEffort,
    AIReasoningOptionPayload,
    AIReasoningOptionRecord,
    AISelectionRequest,
    AISettingsInfoPayload,
    AISettingsState,
    ComposerAttachment,
    CreditBalanceInfo,
     CURRENT_PAGE_ATTACHMENT,
    DEFAULT_VOICE_STATE,
    DEFAULT_HISTORY_SEARCH_STATE,
    DEFAULT_BOOKMARKS_SEARCH_STATE,
   DEFAULT_OPEN_TABS_STATE,
   createFileAttachment,
   createInitialState,
   getEventText,
   getSubmitContextAttachments,
   hasCurrentPageAttachment,
    isRenderableTimelineEvent,
    ChatIntent,
    QuickAction,
    TimelineEntry,
   VoiceStatus,
  } from './types.js';
import {notifyListeners} from '../maho_common/react/store_utils.js';
import type {Listener} from '../maho_common/react/store_utils.js';
import {bindMojoPageHandler} from '../maho_common/react/use_mojo.js';
import {toast} from 'sonner';

type ToastTone = 'danger'|'info'|'success';

type SubmissionRequest = {
  readonly prompt: string;
  readonly attachments: ReturnType<typeof getSubmitContextAttachments>;
  readonly attachBrowserContext: boolean;
  readonly mode: InteractionMode;
  readonly intent: ChatIntent;
  readonly previousBlockedReason: AppState['chatBlockedReason'];
  readonly isQuickAction: boolean;
};

interface AISelectionRemote {
  setAISelection(
      providerId: string,
      modelId: string,
      reasoningEffort: AIReasoningEffort): Promise<{accepted: boolean}>;
}

function supportsAISelection(
    pageHandler: PageHandlerRemote): pageHandler is PageHandlerRemote&AISelectionRemote {
  return 'setAISelection' in pageHandler &&
      typeof pageHandler.setAISelection === 'function';
}

function normalizeIdentifier(value?: string|null): string|null {
  const trimmed = value?.trim() || '';
  return trimmed ? trimmed : null;
}

function normalizeReasoningEffort(
    value?: AIReasoningEffort|null): AIReasoningEffort|null {
  switch (value) {
    case AI_REASONING_EFFORT.kLow:
    case AI_REASONING_EFFORT.kMedium:
    case AI_REASONING_EFFORT.kHigh:
      return value;
    default:
      return null;
  }
}

function normalizeModelOptions(
    options?: readonly AIModelOptionPayload[]|null): AIModelOptionRecord[] {
  const normalized: AIModelOptionRecord[] = [];
  for (const option of options || []) {
    const id = normalizeIdentifier(option.id);
    if (!id) {
      continue;
    }
    normalized.push({
      id,
      label: normalizeIdentifier(option.label) || id,
    });
  }
  return normalized;
}

function normalizeProviderOptions(
    options?: readonly AIProviderOptionPayload[]|null): AIProviderOptionRecord[] {
  const normalized: AIProviderOptionRecord[] = [];
  for (const option of options || []) {
    const id = normalizeIdentifier(option.id);
    if (!id) {
      continue;
    }
    normalized.push({
      id,
      label: normalizeIdentifier(option.label) || id,
      modelOptions: normalizeModelOptions(option.modelOptions),
    });
  }
  return normalized;
}

function normalizeReasoningOptions(
    options?: readonly AIReasoningOptionPayload[]|null): readonly AIReasoningOptionRecord[] {
  const normalized: AIReasoningOptionRecord[] = [];
  for (const option of options || []) {
    const effort = normalizeReasoningEffort(option.effort);
    if (effort === null) {
      continue;
    }
    normalized.push({
      effort,
      label: normalizeIdentifier(option.label) || 'Reasoning',
    });
  }
  return normalized.length ? normalized : DEFAULT_AI_REASONING_OPTIONS;
}

function normalizeAISettings(info: AISettingsInfoPayload): AISettingsState {
  return {
    activeModelId: normalizeIdentifier(info.activeModelId),
    activeProviderId: normalizeIdentifier(info.activeProviderId),
    activeReasoningEffort:
        normalizeReasoningEffort(info.activeReasoningEffort) || AI_REASONING_EFFORT.kMedium,
    providerOptions: normalizeProviderOptions(info.providerOptions),
    reasoningOptions: normalizeReasoningOptions(info.reasoningOptions),
  };
}

function toLegacyProviderRecords(providerOptions: readonly AIProviderOptionRecord[]):
    AppState['aiProviders'] {
  return providerOptions.map(provider => ({
    displayName: provider.label,
    id: provider.id,
    models: provider.modelOptions.map(model => model.id),
  }));
}

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
    voice: {...state.voice},
    openTabs: {...state.openTabs, items: [...state.openTabs.items]},
    historySearch: {...state.historySearch, items: [...state.historySearch.items]},
    bookmarksSearch: {...state.bookmarksSearch, items: [...state.bookmarksSearch.items]},
    toast: state.toast ? {...state.toast} : null,
    aiProviders: state.aiProviders.map(provider => ({...provider, models: [...provider.models]})),
    aiSettings: {
      ...state.aiSettings,
      providerOptions: state.aiSettings.providerOptions.map(provider => ({
        ...provider,
        modelOptions: provider.modelOptions.map(model => ({...model})),
      })),
      reasoningOptions: state.aiSettings.reasoningOptions.map(option => ({...option})),
    },
    turnPendingBySessionId: {...state.turnPendingBySessionId},
    contextsBySessionId: Object.fromEntries(
        Object.entries(state.contextsBySessionId).map(([key, value]) => [key, [...value]])),
    creditBalance: state.creditBalance ? {...state.creditBalance} : null,
    chatBlockedReason: state.chatBlockedReason,
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
  private readonly acceptedAskMahoSessionIds = new Set<string>();
  private readonly pendingRuntimeEventsBySessionId = new Map<string, RuntimeEvent[]>();
  private selectionRevision = 0;
  private submissionInFlight = false;
  /**
   * Intent of the most recent submission per session. Regeneration replays the
   * same typed intent so quick-action turns keep their directive and context.
   */
  private readonly lastIntentBySessionId = new Map<string, ChatIntent>();
  // RAF-coalesced notify: multiple patches within one frame (e.g. streaming
  // tokens) collapse into a single React render, killing sub-frame flicker.
  private notifyScheduled = false;
  private rafHandle: number | null = null;

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

  async bootstrap(): Promise<void> {
    const selectionRevision = this.selectionRevision;
    const currentSessionId = this.state.currentSessionId;
    this.patch(state => {
      state.booting = true;
      state.bootError = null;
    });

    try {
      const [{state, activeAdapterName}, {sessions}] = await Promise.all([
        this.pageHandler.getConnectionState(),
        this.pageHandler.getSessionList(),
      ]);

      let sessionIdToResume: string|null = null;
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
        if (this.selectionRevision === selectionRevision) {
          if (!next.currentSessionId) {
            next.currentSessionId = sessions[0]?.sessionId || null;
            sessionIdToResume = next.currentSessionId;
          } else if (next.currentSessionId === currentSessionId) {
            sessionIdToResume = currentSessionId;
          }
        }
        next.initialized = true;
        next.booting = false;
      });

      if (sessionIdToResume) {
        await this.resumeSession(sessionIdToResume);
      }

      try {
        const urlParams = new URLSearchParams(window.location.search);
        const spaceId = urlParams.get('spaceId') || urlParams.get('space_id') || null;
        await this.loadAiExtensibility(spaceId);
      } catch (err) {
        console.warn('[maho-ai] initial loadAiExtensibility failed', err);
      }

      void this.fetchCreditBalance().catch(err => {
        console.warn('[maho-ai] initial credit balance fetch failed', err);
      });
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

  openVoice() {
    this.patch(state => {
      state.voice = {
        active: true,
        status: 'idle',
        transcript: '',
        level: 0,
        error: null,
      };
    });
  }

  closeVoice() {
    this.patch(state => {
      state.voice = {...DEFAULT_VOICE_STATE};
    });
  }

  setVoiceStatus(status: VoiceStatus) {
    this.patch(state => {
      state.voice.status = status;
    });
  }

  setVoiceTranscript(transcript: string) {
    this.patch(state => {
      state.voice.transcript = transcript;
    });
  }

  setVoiceLevel(level: number) {
    const nextLevel = Number.isFinite(level) ? Math.min(1, Math.max(0, level)) : 0;
    this.patch(state => {
      state.voice.level = nextLevel;
    });
  }

  setVoiceError(error: string|null) {
    this.patch(state => {
      state.voice.error = error;
      if (error) {
        state.voice.status = 'error';
        state.voice.level = 0;
      }
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
   * StartSession(initial_prompt), then submits the captured composer request
   * after the session is ready. Use this for first-send when there is no
   * current session so async setup cannot replace the original request.
   */
  async startSessionThenSubmit(): Promise<void> {
    const request = this.beginSubmission();
    if (!request) {
      return;
    }

    let sessionId: string|null = null;
    try {
      // Pass null prompt so the backend does not auto-submit anything.
      const {session} = await this.pageHandler.startSession(null, request.mode);
      sessionId = session.sessionId;

      this.patch(state => {
        state.sessionsById[session.sessionId] = session;
        state.sessionOrder = sortSessionIds(state.sessionsById);
        state.currentSessionId = session.sessionId;
        state.selectedInspector = {kind: 'session', id: session.sessionId};
        state.turnPendingBySessionId[session.sessionId] = true;
        state.eventsBySessionId[session.sessionId] = [];
      });

      await this.resumeSession(session.sessionId, false);
      await this.submitCapturedRequest(session.sessionId, request);
    } catch (error) {
      this.rollbackSubmission(sessionId, request);
      throw error;
    } finally {
      this.submissionInFlight = false;
    }
  }

  async resumeSession(sessionId: string, fetchHistory = true): Promise<void> {
    const selectionRevision = ++this.selectionRevision;
    const {session, replayEvents} = await this.pageHandler.resumeSession(sessionId);

    this.patch(state => {
      if (this.selectionRevision === selectionRevision) {
        state.currentSessionId = sessionId;
        state.selectedInspector = {kind: 'session', id: sessionId};
      }
      if (session) {
        state.sessionsById[session.sessionId] = session;
        state.sessionOrder = sortSessionIds(state.sessionsById);
      }
      this.updateContextSnapshot(state, sessionId, replayEvents);
      if (replayEvents.length) {
        state.eventsBySessionId[sessionId] = replayEvents.filter(isRenderableTimelineEvent)
                                          .map(event => ({sessionId, event}));
      }
    });

    if (fetchHistory && this.selectionRevision === selectionRevision &&
        this.state.currentSessionId === sessionId) {
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

  addToast(text: string, tone: ToastTone = 'info') {
    switch (tone) {
      case 'danger':
        toast.error(text);
        break;
      case 'success':
        toast.success(text);
        break;
      default:
        toast.message(text);
        break;
    }
  }

  clearToast() {
    toast.dismiss();
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

  async setAISelection(selection: AISelectionRequest): Promise<boolean> {
    const providerId = selection.providerId.trim();
    const modelId = selection.modelId.trim();
    const reasoningEffort = normalizeReasoningEffort(selection.reasoningEffort);
    if (!providerId || !modelId || reasoningEffort === null) {
      await this.refreshAISettings();
      return false;
    }

    if (!supportsAISelection(this.pageHandler)) {
      console.warn('[maho-ai] SetAISelection is unavailable on this page handler');
      await this.refreshAISettings();
      return false;
    }

    try {
      const {accepted} = await this.pageHandler.setAISelection(
          providerId, modelId, reasoningEffort);
      await this.refreshAISettings();
      return accepted;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      console.warn('[maho-ai] SetAISelection failed', message);
      await this.refreshAISettings();
      return false;
    }
  }

  async submitPrompt(): Promise<void> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    if (!sessionId || session?.isReadOnly || this.state.turnPendingBySessionId[sessionId]) {
      return;
    }

    const request = this.beginSubmission();
    if (!request) {
      return;
    }

    try {
      this.patch(state => {
        state.turnPendingBySessionId[sessionId] = true;
      });
      await this.submitCapturedRequest(sessionId, request);
    } catch (error) {
      this.rollbackSubmission(sessionId, request);
      throw error;
    } finally {
      this.submissionInFlight = false;
    }
  }

  async submitQuickAction(action: QuickAction): Promise<void> {
    if (this.submissionInFlight) {
      return;
    }

    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    if (session?.isReadOnly || (sessionId && this.state.turnPendingBySessionId[sessionId])) {
      return;
    }

    const request: SubmissionRequest = {
      prompt: action.userPrompt,
      attachments: null,
      attachBrowserContext: true,
      mode: InteractionMode.kAssistant,
      intent: action.intent,
      previousBlockedReason: this.state.chatBlockedReason,
      isQuickAction: true,
    };

    this.submissionInFlight = true;

    if (!sessionId) {
      let createdSessionId: string|null = null;
      try {
        const {session: newSession} = await this.pageHandler.startSession(null, request.mode);
        createdSessionId = newSession.sessionId;

        this.patch(state => {
          state.sessionsById[newSession.sessionId] = newSession;
          state.sessionOrder = sortSessionIds(state.sessionsById);
          state.currentSessionId = newSession.sessionId;
          state.selectedInspector = {kind: 'session', id: newSession.sessionId};
          state.turnPendingBySessionId[newSession.sessionId] = true;
          state.eventsBySessionId[newSession.sessionId] = [];
        });

        await this.resumeSession(newSession.sessionId, false);
        await this.submitCapturedRequest(newSession.sessionId, request);
      } catch (error) {
        this.rollbackSubmission(createdSessionId, request);
        throw error;
      } finally {
        this.submissionInFlight = false;
      }
    } else {
      try {
        this.patch(state => {
          state.turnPendingBySessionId[sessionId] = true;
        });
        await this.submitCapturedRequest(sessionId, request);
      } catch (error) {
        this.rollbackSubmission(sessionId, request);
        throw error;
      } finally {
        this.submissionInFlight = false;
      }
    }
  }

  /**
   * Resubmits the most recent user prompt of the active session so the user can
   * ask for a different answer without retyping it. The composer is untouched:
   * the prompt is recovered from the session's own event log.
   */
  async regenerateLastResponse(): Promise<void> {
    if (this.submissionInFlight) {
      return;
    }

    const sessionId = this.state.currentSessionId;
    if (!sessionId) {
      return;
    }
    const session = this.state.sessionsById[sessionId];
    if (session?.isReadOnly || this.state.turnPendingBySessionId[sessionId]) {
      return;
    }

    const prompt = this.findLastUserPrompt(sessionId);
    if (!prompt) {
      return;
    }

    // A quick-action turn only behaves correctly when its typed intent and page
    // context are reapplied; resending it as plain freeform strips the
    // Summary/Quiz directive and leaves the model without the page it must use.
    const intent =
        this.lastIntentBySessionId.get(sessionId) ?? ChatIntent.kFreeform;
    const isPageIntent = intent === ChatIntent.kSummarizeCurrentPage ||
        intent === ChatIntent.kQuizCurrentPage;

    const request: SubmissionRequest = {
      prompt,
      attachments: null,
      attachBrowserContext: isPageIntent,
      mode: this.state.developerMode ? InteractionMode.kDeveloper :
                                      InteractionMode.kAssistant,
      intent,
      previousBlockedReason: this.state.chatBlockedReason,
      // Behaves like a quick action for rollback: the prompt was never in the
      // composer, so a failure must not push it there.
      isQuickAction: true,
    };

    this.submissionInFlight = true;
    try {
      this.patch(state => {
        state.turnPendingBySessionId[sessionId] = true;
      });
      await this.submitCapturedRequest(sessionId, request);
    } catch (error) {
      this.rollbackSubmission(sessionId, request);
      throw error;
    } finally {
      this.submissionInFlight = false;
    }
  }

  /** Returns the newest user prompt text of `sessionId`, or null when absent. */
  private findLastUserPrompt(sessionId: string): string|null {
    const events = this.state.eventsBySessionId[sessionId] || [];
    for (let i = events.length - 1; i >= 0; --i) {
      const {event} = events[i];
      if (event.kind !== RuntimeEventKind.kUserPrompt) {
        continue;
      }
      const text = getEventText(event).trim();
      if (text) {
        return text;
      }
    }
    return null;
  }

  private beginSubmission(): SubmissionRequest|null {
    if (this.submissionInFlight) {
      return null;
    }

    const prompt = this.state.composer.prompt.trim();
    if (!prompt) {
      return null;
    }

    const composerAttachments = this.state.composer.attachments.map(attachment => ({...attachment}));
    const attachments = getSubmitContextAttachments(composerAttachments);
    const request: SubmissionRequest = {
      prompt,
      attachments,
      attachBrowserContext: attachments === null && hasCurrentPageAttachment(composerAttachments),
      mode: this.state.developerMode ? InteractionMode.kDeveloper : InteractionMode.kAssistant,
      intent: ChatIntent.kFreeform,
      previousBlockedReason: this.state.chatBlockedReason,
      isQuickAction: false,
    };
    this.patch(state => {
      state.composer.prompt = '';
      state.chatBlockedReason = null;
    });
    this.submissionInFlight = true;
    return request;
  }

  private async submitCapturedRequest(
      sessionId: string, request: SubmissionRequest): Promise<void> {
    this.lastIntentBySessionId.set(sessionId, request.intent);
    await this.pageHandler.submitPrompt(
        sessionId,
        request.prompt,
        request.attachBrowserContext,
        request.mode,
        request.attachments,
        request.intent);
  }

  private rollbackSubmission(sessionId: string|null, request: SubmissionRequest): void {
    this.patch(state => {
      if (sessionId) {
        state.turnPendingBySessionId[sessionId] = false;
      }
      if (!request.isQuickAction) {
        if (!state.composer.prompt) {
          state.composer.prompt = request.prompt;
        }
        state.chatBlockedReason = request.previousBlockedReason;
      }
    });
  }

  async ingestAskMahoSessionAccepted(requestId: string, session: SessionInfo): Promise<void> {
    if (this.acceptedAskMahoSessionIds.has(session.sessionId)) {
      return;
    }

    this.acceptedAskMahoSessionIds.add(session.sessionId);
    ++this.selectionRevision;
    const pendingEvents = this.pendingRuntimeEventsBySessionId.get(session.sessionId) || [];
    this.pendingRuntimeEventsBySessionId.delete(session.sessionId);

    this.patch(state => {
      state.sessionsById[session.sessionId] = session;
      state.sessionOrder = sortSessionIds(state.sessionsById);
      state.currentSessionId = session.sessionId;
      state.selectedInspector = {kind: 'session', id: session.sessionId};
      state.eventsBySessionId[session.sessionId] = state.eventsBySessionId[session.sessionId] || [];
      state.turnPendingBySessionId[session.sessionId] = true;
      state.composer.focusRequest += 1;
      for (const event of pendingEvents) {
        this.applyRuntimeEvent(state, session.sessionId, event);
      }
    });
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

  async openSettings(paneKey?: string): Promise<void> {
    const targetPaneKey = paneKey?.trim();
    if (targetPaneKey) {
      await this.pageHandler.openSettingsPane(targetPaneKey);
      return;
    }

    await this.pageHandler.openSettings();
  }

  async getViewMode(): Promise<ViewMode> {
    const {mode} = await this.pageHandler.getViewMode();
    return mode;
  }

  async setViewMode(mode: ViewMode): Promise<void> {
    await this.pageHandler.setViewMode(mode);
  }

  async requestTabTidy(tabs: TabInfo[]): Promise<TidyFolder[]> {
    const {folders} = await this.pageHandler.requestTabTidy(tabs);
    return folders;
  }

  async applyTabTidyFolders(folders: TidyFolder[]): Promise<void> {
    await this.pageHandler.applyTabTidyFolders(folders);
  }

  async fetchCreditBalance(): Promise<CreditBalanceInfo> {
    const {info} = await this.pageHandler.getCreditBalance();
    this.patch(state => {
      state.creditBalance = info;
    });
    return info;
  }

  async getBuyCreditsUrl(packSizeUsd: 10|50|100): Promise<string> {
    const {checkoutUrl} = await this.pageHandler.getBuyCreditsUrl(packSizeUsd);
    return checkoutUrl;
  }

  setChatBlockedReason(reason: AppState['chatBlockedReason']): void {
    this.patch(state => {
      state.chatBlockedReason = reason;
    });
  }

  private bindCallbacks() {
    this.callbackRouter.onRuntimeEvent.addListener((event: RuntimeEvent) => {
      const sessionId = event.sessionId;
      if (!this.state.sessionsById[sessionId]) {
        console.warn('[maho-ai] runtime event deferred for unknown session', sessionId);
        const pendingEvents = this.pendingRuntimeEventsBySessionId.get(sessionId) || [];
        if (!pendingEvents.some(pendingEvent => pendingEvent.sequence === event.sequence)) {
          this.pendingRuntimeEventsBySessionId.set(sessionId, [...pendingEvents, event]);
        }
        return;
      }

      this.patch(state => {
        this.applyRuntimeEvent(state, sessionId, event);
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

    this.callbackRouter.onAISettingsChanged.addListener(
        (info: AISettingsInfoPayload) => {
          this.applyAISettings(info);
        });

    this.callbackRouter.onAskMahoSessionAccepted.addListener((requestId: string, session: SessionInfo) => {
      void this.ingestAskMahoSessionAccepted(requestId, session);
    });

    this.bindCreditBalanceRefresh();
  }

  private creditBalanceRefreshTimer: ReturnType<typeof setTimeout> | undefined =
      undefined;

  private bindCreditBalanceRefresh(): void {
    const scheduleRefresh = () => {
      if (this.creditBalanceRefreshTimer !== undefined) {
        clearTimeout(this.creditBalanceRefreshTimer);
      }
      this.creditBalanceRefreshTimer = setTimeout(() => {
        this.creditBalanceRefreshTimer = undefined;
        this.fetchCreditBalance().catch(err => {
          console.warn('[maho-ai] credit balance refresh failed', err);
        });
      }, 500);
    };
    window.addEventListener('focus', scheduleRefresh);
  }

  async refreshAISettings(): Promise<void> {
    try {
      const {info} = await this.pageHandler.getAISettings();
      this.applyAISettings(info);
    } catch (err) {
      console.warn('[maho-ai] refreshAISettings failed', err);
    }
  }

  private applyAISettings(info: AISettingsInfoPayload): void {
    const aiSettings = normalizeAISettings(info);
    this.patch(state => {
      state.aiSettings = aiSettings;
      state.aiProviders = toLegacyProviderRecords(aiSettings.providerOptions);
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
      };
      state.eventsBySessionId[sessionId] = [...existing.slice(0, -1), previous];
      return;
    }

    if (event.kind === RuntimeEventKind.kAssistantThinking && previous?.event.kind === RuntimeEventKind.kAssistantThinking) {
      previous.event = {
        ...previous.event,
        text: `${previous.event.text || ''}${event.text || ''}`,
        timestamp: event.timestamp,
      };
      state.eventsBySessionId[sessionId] = [...existing.slice(0, -1), previous];
      return;
    }

    const nextEntry: TimelineEntry = {sessionId, event};
    state.eventsBySessionId[sessionId] = [...existing, nextEntry];
  }

  private applyRuntimeEvent(state: AppState, sessionId: string, event: RuntimeEvent): void {
    const isSelectedSession = state.currentSessionId === sessionId;
    this.appendEventInternal(state, sessionId, event);
    if (event.toolCall?.callId) {
      state.toolCallsById[event.toolCall.callId] = event.toolCall;
      if (isSelectedSession) {
        state.selectedInspector = {kind: 'tool', id: event.toolCall.callId};
      }
    }
    if (event.toolResult?.callId) {
      state.toolResultsByCallId[event.toolResult.callId] = event.toolResult;
      if (isSelectedSession) {
        state.selectedInspector = {kind: 'tool', id: event.toolResult.callId};
      }
    }
    if (event.approvalRequest?.approvalId) {
      state.approvalsById[event.approvalRequest.approvalId] = event.approvalRequest;
      if (isSelectedSession) {
        state.selectedInspector = {kind: 'approval', id: event.approvalRequest.approvalId};
      }
    }
    if (event.approvalResult?.approvalId) {
      state.approvalResultsById[event.approvalResult.approvalId] = event.approvalResult;
      if (isSelectedSession) {
        state.selectedInspector = {kind: 'approval', id: event.approvalResult.approvalId};
      }
    }
    if (event.kind === RuntimeEventKind.kTurnComplete || event.kind === RuntimeEventKind.kError) {
      state.turnPendingBySessionId[sessionId] = false;
      if (isSelectedSession && event.kind === RuntimeEventKind.kError) {
        const errorText = event.text || '';
        if (errorText.includes('402') || errorText.toLowerCase().includes('quota') ||
            errorText.toLowerCase().includes('payment required') ||
            errorText.toLowerCase().includes('deny-paid')) {
          state.chatBlockedReason = 'quota-exhausted';
        }
      }
    }
    if (event.kind === RuntimeEventKind.kBrowserContextInjected && event.browserContext) {
      const existing = state.contextsBySessionId[sessionId] || [];
      state.contextsBySessionId[sessionId] = [...existing, event.browserContext];
    }
    if (event.kind === RuntimeEventKind.kToolAvailabilityChanged) {
      const activeSpaceId = state.activeWorkspace?.spaceId ?? null;
      void this.loadAiExtensibility(activeSpaceId);
    }
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
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    this.scheduleNotify();
  }

  private scheduleNotify(): void {
    if (this.notifyScheduled) {
      return;
    }
    this.notifyScheduled = true;
    const flush = () => {
      this.notifyScheduled = false;
      this.rafHandle = null;
      notifyListeners(this.listeners);
    };
    if (typeof requestAnimationFrame === 'function') {
      this.rafHandle = requestAnimationFrame(flush);
    } else {
      setTimeout(flush, 0);
    }
  }

  // Load AI extensibility states
  async loadAiExtensibility(spaceId: string | null = null): Promise<void> {
    try {
      const [profilesRes, workspacesRes] = await Promise.all([
        this.pageHandler.getAiProfiles(),
        this.pageHandler.getAiWorkspaces(),
      ]);

      const profiles = JSON.parse(profilesRes.profilesJson);
      const workspaces = JSON.parse(workspacesRes.workspacesJson);

      let activeWorkspace = null;
      let mcpServers = [];
      let cliTools = [];

      const targetWorkspace = workspaces.find((w: any) => w.spaceId === spaceId) || workspaces[0];
      if (targetWorkspace) {
        const [workspaceInfoRes, mcpRes, cliRes] = await Promise.all([
          this.pageHandler.getActiveAiWorkspace(spaceId || ''),
          this.pageHandler.getMcpServers(targetWorkspace.id),
          this.pageHandler.getCliTools(targetWorkspace.id),
        ]);

        if (workspaceInfoRes.workspaceJson) {
          activeWorkspace = JSON.parse(workspaceInfoRes.workspaceJson);
        }
        if (mcpRes.serversJson) {
          mcpServers = JSON.parse(mcpRes.serversJson);
        }
        if (cliRes.toolsJson) {
          cliTools = JSON.parse(cliRes.toolsJson);
        }
      }

      this.patch(state => {
        state.profiles = profiles;
        state.activeWorkspace = activeWorkspace;
        state.mcpServers = mcpServers;
        state.cliTools = cliTools;
        if (activeWorkspace) {
          state.activeProfileId = activeWorkspace.profileId;
        } else {
          const defaultProfile = profiles.find((p: any) => p.isDefault);
          state.activeProfileId = defaultProfile ? defaultProfile.id : (profiles[0]?.id || null);
        }
      });
    } catch (err) {
      console.warn('[maho-ai] loadAiExtensibility failed', err);
    }
  }

  async switchWorkspaceProfile(workspaceId: string, profileId: string): Promise<void> {
    try {
      const {success} = await this.pageHandler.switchWorkspaceProfile(workspaceId, profileId);
      if (success) {
        this.patch(state => {
          state.activeProfileId = profileId;
          if (state.activeWorkspace && state.activeWorkspace.id === workspaceId) {
            state.activeWorkspace.profileId = profileId;
          }
        });
        toast.success('Workspace profile switched');
      } else {
        toast.error('Failed to switch workspace profile');
      }
    } catch (err) {
      console.error('[maho-ai] switchWorkspaceProfile failed', err);
    }
  }

  async approveMcpServerTrust(workspaceId: string, serverName: string, tools: string[]): Promise<void> {
    try {
      const {success} = await this.pageHandler.approveMcpServerTrust(workspaceId, serverName, tools);
      if (success) {
        toast.success('MCP server trusted');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
      } else {
        toast.error('Failed to trust MCP server');
      }
    } catch (err) {
      console.error('[maho-ai] approveMcpServerTrust failed', err);
    }
  }

  async importProfileFromToml(workspaceId: string, tomlPath: string): Promise<void> {
    try {
      const {profileIdOrError} = await this.pageHandler.importProfileFromToml(workspaceId, tomlPath);
      if (profileIdOrError && !profileIdOrError.startsWith('Error')) {
        toast.success('Profile imported successfully');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
      } else {
        toast.error(profileIdOrError || 'Failed to import profile');
      }
    } catch (err) {
      console.error('[maho-ai] importProfileFromToml failed', err);
    }
  }

  async registerMcpServer(workspaceId: string, configJson: string): Promise<boolean> {
    try {
      const {success} = await this.pageHandler.registerMcpServer(workspaceId, configJson);
      if (success) {
        toast.success('MCP server registered');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
        return true;
      } else {
        toast.error('Failed to register MCP server');
        return false;
      }
    } catch (err) {
      console.error('[maho-ai] registerMcpServer failed', err);
      return false;
    }
  }

  async removeMcpServer(workspaceId: string, serverName: string): Promise<void> {
    try {
      const {success} = await this.pageHandler.removeMcpServer(workspaceId, serverName);
      if (success) {
        toast.success('MCP server removed');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
      } else {
        toast.error('Failed to remove MCP server');
      }
    } catch (err) {
      console.error('[maho-ai] removeMcpServer failed', err);
    }
  }

  async registerCliTool(workspaceId: string, toolJson: string): Promise<boolean> {
    try {
      const {success} = await this.pageHandler.registerCliTool(workspaceId, toolJson);
      if (success) {
        toast.success('CLI tool registered');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
        return true;
      } else {
        toast.error('Failed to register CLI tool');
        return false;
      }
    } catch (err) {
      console.error('[maho-ai] registerCliTool failed', err);
      return false;
    }
  }

  async removeCliTool(workspaceId: string, toolName: string): Promise<void> {
    try {
      const {success} = await this.pageHandler.removeCliTool(workspaceId, toolName);
      if (success) {
        toast.success('CLI tool removed');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
      } else {
        toast.error('Failed to remove CLI tool');
      }
    } catch (err) {
      console.error('[maho-ai] removeCliTool failed', err);
    }
  }

  async createAiProfile(name: string, systemPrompt: string, model: string | null): Promise<boolean> {
    try {
      const {profileId} = await this.pageHandler.createAiProfile(name, systemPrompt, model);
      if (profileId) {
        toast.success('Agent profile created');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
        return true;
      } else {
        toast.error('Failed to create agent profile');
        return false;
      }
    } catch (err) {
      console.error('[maho-ai] createAiProfile failed', err);
      return false;
    }
  }

  setActiveSlashEditor(editor: AppState['activeSlashEditor']) {
    this.patch(state => {
      state.activeSlashEditor = editor;
    });
  }
}
