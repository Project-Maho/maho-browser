import {
  InteractionMode,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEvent,
  RuntimeEventKind,
  SessionInfo,
  SessionStatus,
  TabInfo,
  TidyFolder,
  ViewMode,
} from './maho_ai.mojom-webui.js';
import {RoutineOperationsClient, RoutineSurfaceClient} from './routine-client.js';
import {
    AI_REASONING_EFFORT,
    DEFAULT_AI_REASONING_OPTIONS,
    buildRuntimeConfigDispatch,
    buildInteractionAnswerDispatch,
    buildInteractionRequestRecord,
    AppState,
    RuntimeConfigInfo,
    InteractionAnswerPayload,
    InteractionRequestInfoPayload,
    RUNTIME_EVENT_KIND_INTERACTION_REQUEST,
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
import {
  SUGGESTED_TASK_RUNTIME_CONFIG,
  type SuggestedTask,
} from './views/suggestions.js';
import {notifyListeners} from '../maho_common/react/store_utils.js';
import type {Listener} from '../maho_common/react/store_utils.js';
import {getMahoAiPageConnection} from './page_connection.js';
import {toast} from 'sonner';
import {
  ComposerDraftPersistence,
  scopeKey,
  type ComposerDraftPersistencePort,
  type ComposerDraftRemote,
  type ComposerDraftScope,
} from './react/features/compact/composer-draft-persistence.js';

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

interface ComposerDraftPageHandlerRemote extends ComposerDraftRemote {}

interface AISelectionRemote {
  setDefaultAISelection(
      providerId: string,
      modelId: string,
      reasoningEffort: AIReasoningEffort): Promise<{accepted: boolean}>;
}

function supportsComposerDraftPersistence(
    pageHandler: PageHandlerRemote): pageHandler is PageHandlerRemote&ComposerDraftPageHandlerRemote {
  return 'composerDraftGet' in pageHandler &&
      typeof pageHandler.composerDraftGet === 'function' &&
      'composerDraftSet' in pageHandler &&
      typeof pageHandler.composerDraftSet === 'function' &&
      'composerDraftDelete' in pageHandler &&
      typeof pageHandler.composerDraftDelete === 'function';
}

function supportsAISelection(
    pageHandler: PageHandlerRemote): pageHandler is PageHandlerRemote&AISelectionRemote {
  return 'setDefaultAISelection' in pageHandler &&
      typeof pageHandler.setDefaultAISelection === 'function';
}

interface RuntimeConfigRemote {
  getRuntimeConfig(): Promise<{config: RuntimeConfigInfo}>;
  setRuntimeConfig(config: RuntimeConfigInfo): Promise<{accepted: boolean}>;
}

function supportsRuntimeConfig(
    pageHandler: PageHandlerRemote): pageHandler is PageHandlerRemote&RuntimeConfigRemote {
  return 'getRuntimeConfig' in pageHandler &&
      typeof pageHandler.getRuntimeConfig === 'function' &&
      'setRuntimeConfig' in pageHandler &&
      typeof pageHandler.setRuntimeConfig === 'function';
}

// Interaction request answers (plan row 8). The generated mojom bindings gain
// RespondToInteraction only after the deferred regeneration; the store
// feature-detects the method the same way as the runtime config surface.
interface InteractionRemote {
  respondToInteraction(
      sessionId: string,
      requestId: string,
      answer: InteractionAnswerPayload): Promise<void>;
}

function supportsInteraction(
    pageHandler: PageHandlerRemote): pageHandler is PageHandlerRemote&InteractionRemote {
  return 'respondToInteraction' in pageHandler &&
      typeof pageHandler.respondToInteraction === 'function';
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
        normalizeReasoningEffort(info.activeReasoningEffort) ?? AI_REASONING_EFFORT.kMedium,
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
    eventsBySessionId: {...state.eventsBySessionId},
    historiesBySessionId: {...state.historiesBySessionId},
    artifactsBySessionId: Object.fromEntries(
        Object.entries(state.artifactsBySessionId).map(([key, value]) => [key, [...value]])),
    artifactsLoadingBySessionId: {...state.artifactsLoadingBySessionId},
    toolCallsById: {...state.toolCallsById},
    toolResultsByCallId: {...state.toolResultsByCallId},
    approvalsById: {...state.approvalsById},
    approvalResultsById: {...state.approvalResultsById},
    interactionsBySessionId: Object.fromEntries(
        Object.entries(state.interactionsBySessionId).map(([key, value]) => [key, [...value]])),
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
    runtimeConfig: {...state.runtimeConfig},
    runtimeConfigSupported: state.runtimeConfigSupported,
  };
}

function sortSessionIds(sessionsById: AppState['sessionsById']): string[] {
  return Object.values(sessionsById)
      .sort((a, b) => (b.updatedAt || b.createdAt) - (a.updatedAt || a.createdAt))
      .map(session => session.sessionId);
}

export function createMahoAiStore(): MahoAiStore {
  const {router, handler} = getMahoAiPageConnection();
  return new MahoAiStore(handler, router);
}

export class MahoAiStore {
  private state = createInitialState();
  private listeners = new Set<Listener>();
  private listenerIds: number[] = [];
  private readonly acceptedAskMahoSessionIds = new Set<string>();
  private readonly pendingRuntimeEventsBySessionId = new Map<string, RuntimeEvent[]>();
  private readonly replayTextEncoder = new TextEncoder();
  private readonly replayTextBytes = new WeakMap<RuntimeEvent, number>();
  private readonly replayWindowThroughBySessionId = new Map<string, bigint>();
  private readonly replayContextSequence =
      new WeakMap<NonNullable<RuntimeEvent['browserContext']>, bigint>();
  private readonly replayInteractionSequences = new Map<string, Map<string, bigint>>();
  private selectionRevision = 0;
  private historySearchGeneration = 0;
  private submissionInFlight = false;
  private composerDraftScope: ComposerDraftScope = {kind: 'new_task'};
  private composerDraftRevision = 0;
  private readonly composerDraftRevisionByScope = new Map<string, number>();
  private composerDraftPersistence: ComposerDraftPersistencePort|null;
  private readonly routineOperationsClient: RoutineOperationsClient;
  /**
   * Intent of the most recent submission per session. Regeneration replays the
   * same typed intent so quick-action turns keep their directive and context.
   */
  private readonly lastIntentBySessionId = new Map<string, ChatIntent>();
  // RAF-coalesced notify: multiple patches within one frame (e.g. streaming
  // tokens) collapse into a single React render, killing sub-frame flicker.
  private notifyScheduled = false;
  private rafHandle: number | null = null;

  // Capability preview/export URLs cached per artifact with a 15-minute TTL
  // (matches the registry capability lifetime); refreshed on artifact list load.
  private readonly artifactUrlCache =
      new Map<string, {url: string; expiresAt: number}>();
  private readonly artifactUrlTtlMs = 15 * 60 * 1000;
  private readonly artifactDeletionById = new Map<string, Promise<boolean>>();
  private readonly artifactRevisionById = new Map<string, number>();
  private readonly deletedArtifactIds = new Set<string>();

  constructor(
      private readonly pageHandler: PageHandlerRemote,
      private readonly callbackRouter: PageCallbackRouter) {
    this.composerDraftPersistence = supportsComposerDraftPersistence(pageHandler) ?
        new ComposerDraftPersistence(pageHandler) : null;
    this.routineOperationsClient = new RoutineOperationsClient(
        pageHandler as unknown as
            import('./routine-client.js').RoutinePageHandler,
        callbackRouter);
    this.bindCallbacks();
    // Feature-detect the runtime config surface (plan row 4): the generated
    // mojom bindings expose it once the C++ handler lands; standalone/dev
    // handlers without it keep the controls hidden.
    if (supportsRuntimeConfig(this.pageHandler)) {
      this.patch(state => {
        state.runtimeConfigSupported = true;
      });
    }
  }

  setComposerDraftPersistenceForTesting(
      persistence: ComposerDraftPersistencePort): void {
    this.composerDraftPersistence = persistence;
  }

  getSnapshot(): AppState {
    return this.state;
  }

  getRoutineOperationsClient(): RoutineOperationsClient {
    return this.routineOperationsClient;
  }

  getRoutineSurfaceClient(
      onChange: (state: {surface: 'chat'|'routines'; generation: bigint}) => void):
      RoutineSurfaceClient {
    return new RoutineSurfaceClient(this.pageHandler, this.callbackRouter, onChange);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  dispose(): void {
    for (const id of this.listenerIds) {
      this.callbackRouter.removeListener(id);
    }
    this.listenerIds = [];
    (this.callbackRouter.$ as {close?: () => void}).close?.();
    (this.pageHandler.$ as {close?: () => void}).close?.();
    if (this.focusListener) {
      window.removeEventListener('focus', this.focusListener);
      this.focusListener = null;
    }
    if (this.creditBalanceRefreshTimer !== undefined) {
      clearTimeout(this.creditBalanceRefreshTimer);
      this.creditBalanceRefreshTimer = undefined;
    }
    if (this.rafHandle !== null) {
      if (typeof cancelAnimationFrame === 'function') {
        cancelAnimationFrame(this.rafHandle);
      } else {
        clearTimeout(this.rafHandle);
      }
      this.rafHandle = null;
    }
    this.notifyScheduled = false;
    this.listeners.clear();
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
      } else {
        await this.hydrateComposerDraft({kind: 'new_task'});
      }

      try {
        const urlParams = new URLSearchParams(window.location.search);
        const spaceId = urlParams.get('spaceId') || urlParams.get('space_id') || null;
        await this.loadAiExtensibility(spaceId);
      } catch (err) {
        console.warn('[maho-ai] initial loadAiExtensibility failed', err);
      }

      void this.refreshRuntimeConfig().catch(err => {
        console.warn('[maho-ai] initial runtime config fetch failed', err);
      });

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
    ++this.composerDraftRevision;
    this.composerDraftRevisionByScope.set(scopeKey(this.composerDraftScope), this.composerDraftRevision);
    this.patch(state => {
      state.composer.prompt = prompt;
    });
    this.composerDraftPersistence?.schedule(this.composerDraftScope, prompt);
  }

  async flushComposerDraft(): Promise<void> {
    await this.composerDraftPersistence?.flush(this.composerDraftScope);
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

  /**
   * Mirrors the backend SuggestionSettings enabled flag (plan row 12). When
   * disabled, the panel renders zero idle suggested-task cards.
   */
  setSuggestionsEnabled(enabled: boolean) {
    this.patch(state => {
      state.suggestionsEnabled = enabled;
    });
  }

  async startSession(): Promise<void> {
    const prompt = this.state.composer.prompt.trim();
    if (prompt) {
      await this.startSessionThenSubmit();
      return;
    }

    await this.composerDraftPersistence?.clear(this.composerDraftScope);
    this.composerDraftScope = {kind: 'new_task'};
    const mode = this.state.developerMode ?
        InteractionMode.kDeveloper :
        InteractionMode.kAssistant;
    const {session} = await this.pageHandler.startSession(null, mode);

    this.patch(state => {
      state.sessionsById[session.sessionId] = session;
      state.sessionOrder = sortSessionIds(state.sessionsById);
      state.currentSessionId = session.sessionId;
      state.selectedInspector = {kind: 'session', id: session.sessionId};
      state.turnPendingBySessionId[session.sessionId] = false;
      state.eventsBySessionId[session.sessionId] = [];
    });

    await this.resumeSession(session.sessionId, false, false);
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

      await this.resumeSession(session.sessionId, false, false);
      await this.submitCapturedRequest(session.sessionId, request);
      await this.promoteAcceptedNewTaskDraft(session.sessionId);
    } catch (error) {
      this.rollbackSubmission(sessionId, request);
      throw error;
    } finally {
      this.submissionInFlight = false;
    }
  }

  async resumeSession(
      sessionId: string, fetchHistory = true, hydrateDraft = true): Promise<void> {
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
      this.mergeReplayEvents(state, sessionId, replayEvents);
      if (session) {
        state.turnPendingBySessionId[sessionId] ??=
            session.isActive &&
            (session.status === SessionStatus.kActive ||
             session.status === SessionStatus.kPausedForApproval);
        const pending = this.pendingRuntimeEventsBySessionId.get(sessionId) || [];
        this.pendingRuntimeEventsBySessionId.delete(sessionId);
        const through = (state.eventsBySessionId[sessionId] || []).reduce(
            (latest, entry) => BigInt(entry.event.sequence) > latest ?
                BigInt(entry.event.sequence) : latest, -1n);
        for (const event of pending) {
          if (BigInt(event.sequence) > through) {
            this.applyRuntimeEvent(state, sessionId, event);
          }
        }
      }
    });

    if (fetchHistory && this.selectionRevision === selectionRevision &&
        this.state.currentSessionId === sessionId) {
      await this.loadHistory(sessionId);
    }
    if (hydrateDraft && this.selectionRevision === selectionRevision &&
        this.state.currentSessionId === sessionId) {
      await this.hydrateComposerDraft({kind: 'conversation', conversationId: sessionId});
    }
  }

  async loadHistory(sessionId: string, pageSize = 200): Promise<void> {
    const {events, totalCount} = await this.pageHandler.getSessionHistory(sessionId, 0, pageSize);
    this.patch(state => {
      this.mergeReplayEvents(state, sessionId, events);
      state.historiesBySessionId[sessionId] = {
        loaded: true, totalCount, loadedCount: events.length,
      };
    });
    void this.loadArtifacts(sessionId);
  }

  async loadEarlierHistory(sessionId: string, pageSize = 200): Promise<void> {
    const hist = this.state.historiesBySessionId[sessionId];
    if (!hist || hist.loadedCount >= hist.totalCount) {
      return;
    }

    const {events} = await this.pageHandler.getSessionHistory(sessionId, hist.loadedCount, pageSize);
    this.patch(state => {
      this.mergeReplayEvents(state, sessionId, events);
      const current = state.historiesBySessionId[sessionId];
      if (current) {
        state.historiesBySessionId[sessionId] = {
          ...current,
          loadedCount: current.loadedCount + events.length,
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

  async loadArtifacts(sessionId: string): Promise<void> {
    if (!sessionId || this.state.artifactsLoadingBySessionId[sessionId]) {
      return;
    }
    this.patch(state => {
      state.artifactsLoadingBySessionId[sessionId] = true;
    });
    try {
      const {artifacts} = await this.pageHandler.listArtifacts(sessionId);
      const liveArtifacts = artifacts.filter(
          artifact => !this.deletedArtifactIds.has(artifact.artifactId));
      // Refresh cached capability URLs on list load.
      for (const artifact of liveArtifacts) {
        this.artifactUrlCache.delete(`preview:${artifact.artifactId}`);
        this.artifactUrlCache.delete(`export:${artifact.artifactId}`);
      }
      this.patch(state => {
        state.artifactsBySessionId[sessionId] = liveArtifacts;
        state.artifactsLoadingBySessionId[sessionId] = false;
      });
    } catch {
      this.patch(state => {
        state.artifactsLoadingBySessionId[sessionId] = false;
      });
    }
  }

  // Returns null on success, or an error message string on failure.
  async renameArtifact(sessionId: string, artifactId: string, displayName: string):
      Promise<string|null> {
    if (this.artifactDeletionById.has(artifactId)) {
      return 'Artifact is being deleted';
    }
    if (!this.hasArtifact(sessionId, artifactId)) {
      return 'Artifact no longer exists';
    }
    const revision = this.getArtifactRevision(artifactId);
    try {
      const {success, error} =
          await this.pageHandler.renameArtifact(artifactId, displayName);
      if (!success) {
        return error || 'Rename failed';
      }
      if (revision !== this.getArtifactRevision(artifactId) ||
          !this.hasArtifact(sessionId, artifactId)) {
        return 'Artifact no longer exists';
      }
      this.patch(state => {
        const list = state.artifactsBySessionId[sessionId];
        if (list) {
          state.artifactsBySessionId[sessionId] =
              list.map(a => a.artifactId === artifactId ? {...a, displayName} : a);
        }
        const entries = state.eventsBySessionId[sessionId];
        if (entries) {
          state.eventsBySessionId[sessionId] = entries.map(entry =>
            entry.event.artifact?.artifactId === artifactId ? {
              ...entry,
              event: {
                ...entry.event,
                artifact: {...entry.event.artifact, displayName},
              },
            } : entry);
        }
      });
      return null;
    } catch (error) {
      return error instanceof Error ? error.message : String(error);
    }
  }

  deleteArtifact(sessionId: string, artifactId: string): Promise<boolean> {
    const pending = this.artifactDeletionById.get(artifactId);
    if (pending) {
      return pending;
    }
    if (!this.hasArtifact(sessionId, artifactId)) {
      return Promise.resolve(true);
    }
    const deletion = this.performArtifactDeletion(sessionId, artifactId);
    this.artifactDeletionById.set(artifactId, deletion);
    void deletion.finally(() => {
      if (this.artifactDeletionById.get(artifactId) === deletion) {
        this.artifactDeletionById.delete(artifactId);
      }
    });
    return deletion;
  }

  private async performArtifactDeletion(sessionId: string, artifactId: string): Promise<boolean> {
    try {
      const {success} = await this.pageHandler.deleteArtifact(artifactId);
      if (!success) {
        return false;
      }
      this.artifactRevisionById.set(artifactId, this.getArtifactRevision(artifactId) + 1);
      this.deletedArtifactIds.add(artifactId);
      this.artifactUrlCache.delete(`preview:${artifactId}`);
      this.artifactUrlCache.delete(`export:${artifactId}`);
      this.patch(state => {
        const list = state.artifactsBySessionId[sessionId];
        if (list) {
          state.artifactsBySessionId[sessionId] =
              list.filter(a => a.artifactId !== artifactId);
        }
        const entries = state.eventsBySessionId[sessionId];
        if (entries) {
          state.eventsBySessionId[sessionId] = entries.filter(
              entry => entry.event.artifact?.artifactId !== artifactId);
        }
      });
      return true;
    } catch {
      return false;
    }
  }

  getArtifactPreviewUrl(artifactId: string): Promise<string|null> {
    return this.resolveArtifactUrl('preview', artifactId);
  }

  getArtifactExportUrl(artifactId: string): Promise<string|null> {
    return this.resolveArtifactUrl('export', artifactId);
  }

  private async resolveArtifactUrl(kind: 'preview'|'export', artifactId: string):
      Promise<string|null> {
    if (this.artifactDeletionById.has(artifactId) || !this.hasArtifactInAnySession(artifactId)) {
      return null;
    }
    const revision = this.getArtifactRevision(artifactId);
    const key = `${kind}:${artifactId}`;
    const cached = this.artifactUrlCache.get(key);
    if (cached && cached.expiresAt > Date.now()) {
      return cached.url;
    }
    try {
      const {url} = kind === 'preview'
          ? await this.pageHandler.getArtifactPreviewUrl(artifactId)
          : await this.pageHandler.getArtifactExportUrl(artifactId);
      if (revision !== this.getArtifactRevision(artifactId) ||
          this.artifactDeletionById.has(artifactId) || !this.hasArtifactInAnySession(artifactId)) {
        return null;
      }
      if (url) {
        this.artifactUrlCache.set(
            key, {url, expiresAt: Date.now() + this.artifactUrlTtlMs});
        return url;
      }
      this.artifactUrlCache.delete(key);
      return null;
    } catch {
      return null;
    }
  }

  private isLiveTimelineEvent(event: RuntimeEvent): boolean {
    return isRenderableTimelineEvent(event) &&
        !(event.kind === RuntimeEventKind.kArtifactCreated && event.artifact &&
          this.deletedArtifactIds.has(event.artifact.artifactId));
  }

  private hasArtifact(sessionId: string, artifactId: string): boolean {
    return !!this.state.artifactsBySessionId[sessionId]
        ?.some(artifact => artifact.artifactId === artifactId);
  }

  private hasArtifactInAnySession(artifactId: string): boolean {
    return Object.values(this.state.artifactsBySessionId)
        .some(artifacts => artifacts.some(artifact => artifact.artifactId === artifactId));
  }

  private getArtifactRevision(artifactId: string): number {
    return this.artifactRevisionById.get(artifactId) || 0;
  }

  async searchHistory(query: string, maxResults = 20): Promise<void> {
    const generation = ++this.historySearchGeneration;
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
      if (generation !== this.historySearchGeneration) return;
      this.patch(state => {
        state.historySearch = {
          query,
          items,
          loading: false,
          error: null,
        };
      });
    } catch (error) {
      if (generation !== this.historySearchGeneration) return;
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
    ++this.historySearchGeneration;
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

  async setDefaultAISelection(selection: AISelectionRequest): Promise<boolean> {
    const providerId = selection.providerId.trim();
    const modelId = selection.modelId.trim();
    const reasoningEffort = normalizeReasoningEffort(selection.reasoningEffort);
    if (!providerId || !modelId || reasoningEffort === null) {
      await this.refreshAISettings();
      return false;
    }

    if (!supportsAISelection(this.pageHandler)) {
      console.warn('[maho-ai] SetDefaultAISelection is unavailable on this page handler');
      await this.refreshAISettings();
      return false;
    }

    try {
      const {accepted} = await this.pageHandler.setDefaultAISelection(
          providerId, modelId, reasoningEffort);
      await this.refreshAISettings();
      return accepted;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      console.warn('[maho-ai] SetDefaultAISelection failed', message);
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

    const draftScope = this.composerDraftScope;
    const draftRevision = this.composerDraftRevisionByScope.get(scopeKey(draftScope));
    const request = this.beginSubmission();
    if (!request) {
      return;
    }

    try {
      this.patch(state => {
        state.turnPendingBySessionId[sessionId] = true;
      });
      await this.submitCapturedRequest(sessionId, request);
      if (draftScope.kind === 'new_task' &&
          scopeKey(this.composerDraftScope) === scopeKey(draftScope)) {
        await this.promoteAcceptedNewTaskDraft(sessionId);
      } else if (this.composerDraftRevisionByScope.get(scopeKey(draftScope)) === draftRevision) {
        await this.composerDraftPersistence?.clear(draftScope);
      }
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

        await this.resumeSession(newSession.sessionId, false, false);
        await this.submitCapturedRequest(newSession.sessionId, request);
        await this.promoteAcceptedNewTaskDraft(newSession.sessionId);
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
   * Starts a suggested task from an idle panel card (plan row 12): creates a
   * new session, applies the guard/final_confirm/proactive flag set to it via
   * the mojom SetRuntimeConfig path (plan rows 4/6), then submits the
   * suggestion's prompt. If the flag dispatch is rejected the task is not
   * started (the session is left empty and current).
   */
  async startSuggestedTask(suggestion: SuggestedTask): Promise<boolean> {
    if (this.submissionInFlight) {
      return false;
    }

    this.submissionInFlight = true;
    let sessionId: string|null = null;
    try {
      const mode = this.state.developerMode ?
          InteractionMode.kDeveloper :
          InteractionMode.kAssistant;
      const previousSessionId = this.state.currentSessionId;
      const previousInspector = this.state.selectedInspector;
      const {session} = await this.pageHandler.startSession(null, mode);
      sessionId = session.sessionId;

      this.patch(state => {
        state.sessionsById[session.sessionId] = session;
        state.sessionOrder = sortSessionIds(state.sessionsById);
        state.currentSessionId = session.sessionId;
        state.selectedInspector = {kind: 'session', id: session.sessionId};
        state.turnPendingBySessionId[session.sessionId] = true;
        state.eventsBySessionId[session.sessionId] = [];
      });

      await this.resumeSession(session.sessionId, false, false);

      // SetRuntimeConfig applies to the active session, so the flag set is
      // dispatched after the new session becomes current. A rejection means
      // the broker did not take the guard flags — do not run the task.
      const accepted = await this.setRuntimeConfig(SUGGESTED_TASK_RUNTIME_CONFIG);
      if (!accepted) {
        // Roll the created session back out of the store so the refused
        // launch does not leave an empty orphan session behind.
        this.patch(state => {
          delete state.sessionsById[session.sessionId];
          delete state.eventsBySessionId[session.sessionId];
          delete state.turnPendingBySessionId[session.sessionId];
          state.sessionOrder = sortSessionIds(state.sessionsById);
          state.currentSessionId = previousSessionId;
          state.selectedInspector = previousInspector;
        });
        console.warn(
            '[maho-ai] Suggested task flags rejected; task not started');
        return false;
      }

      const request: SubmissionRequest = {
        prompt: suggestion.prompt,
        attachments: null,
        attachBrowserContext: true,
        mode,
        intent: ChatIntent.kFreeform,
        previousBlockedReason: this.state.chatBlockedReason,
        isQuickAction: false,
      };
      await this.submitCapturedRequest(session.sessionId, request);
      return true;
    } catch (error) {
      const failedSessionId = sessionId;
      if (failedSessionId) {
        this.patch(state => {
          state.turnPendingBySessionId[failedSessionId] = false;
        });
      }
      throw error;
    } finally {
      this.submissionInFlight = false;
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
    const result = await this.pageHandler.submitPrompt(
        sessionId,
        request.prompt,
        request.attachBrowserContext,
        request.mode,
        request.attachments,
        request.intent);
    if (result?.accepted === false) {
      throw new Error('Maho AI did not accept the prompt.');
    }
  }

  private async promoteAcceptedNewTaskDraft(sessionId: string): Promise<void> {
    const previousScope = this.composerDraftScope;
    this.composerDraftScope = {kind: 'conversation', conversationId: sessionId};
    await this.composerDraftPersistence?.clear(previousScope);
    const replacement = this.state.composer.prompt;
    if (replacement) {
      this.composerDraftPersistence?.schedule(this.composerDraftScope, replacement);
    }
  }

  private async hydrateComposerDraft(scope: ComposerDraftScope): Promise<void> {
    const switching = scopeKey(this.composerDraftScope) !== scopeKey(scope);
    const flushed = switching ? this.composerDraftPersistence?.flush(this.composerDraftScope) : undefined;
    this.composerDraftScope = scope;
    if (switching) {
      this.patch(state => { state.composer.prompt = ''; });
    }
    const revision = this.composerDraftRevision;
    const promptAtStart = this.state.composer.prompt;
    await flushed;
    const restored = await this.composerDraftPersistence?.load(scope);
    if (restored === null || restored === undefined ||
        scopeKey(this.composerDraftScope) !== scopeKey(scope) ||
        this.composerDraftRevision !== revision ||
        this.state.composer.prompt !== promptAtStart || promptAtStart) {
      return;
    }
    this.patch(state => {
      state.composer.prompt = restored;
    });
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

  async respondToApproval(approvalId: string, approved: boolean): Promise<boolean> {
    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    if (!sessionId || session?.isReadOnly) {
      console.warn(
          '[maho-ai] RespondToApproval skipped: no writable session for approval',
          approvalId);
      return false;
    }

    try {
      await this.pageHandler.respondToApproval(sessionId, approvalId, approved);
      return true;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      console.warn('[maho-ai] RespondToApproval failed', message);
      return false;
    }
  }

  /**
   * Answers an agent interaction request (plan row 8): confirms or denies an
   * action confirmation, or answers a question with a selected option or free
   * text. Patches the card state optimistically, then dispatches the
   * mojom-shaped RespondToInteraction call; reverts on failure. Returns
   * whether the answer was dispatched.
   */
  async respondToInteraction(
      requestId: string, answer: InteractionAnswerPayload): Promise<boolean> {
    if (!supportsInteraction(this.pageHandler)) {
      console.warn('[maho-ai] RespondToInteraction is unavailable on this page handler');
      return false;
    }

    const sessionId = this.state.currentSessionId;
    const session = sessionId ? this.state.sessionsById[sessionId] : null;
    if (!sessionId || session?.isReadOnly) {
      return false;
    }

    const target = (this.state.interactionsBySessionId[sessionId] || [])
        .find(record => record.requestId === requestId);
    if (!target || target.state !== 'pending') {
      return false;
    }

    const dispatch = buildInteractionAnswerDispatch(answer);
    const resolvedState = dispatch.answerKind === 'denied' ? 'denied' : 'resolved';
    this.patch(state => {
      state.interactionsBySessionId[sessionId] =
          (state.interactionsBySessionId[sessionId] || []).map(record =>
              record.requestId === requestId ? {...record, state: resolvedState} : record);
    });

    try {
      await this.pageHandler.respondToInteraction(sessionId, requestId, dispatch);
      return true;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      console.warn('[maho-ai] RespondToInteraction failed', message);
      this.patch(state => {
        state.interactionsBySessionId[sessionId] =
            (state.interactionsBySessionId[sessionId] || []).map(record =>
                record.requestId === requestId ? {...record, state: 'pending'} : record);
      });
      return false;
    }
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

  private registerCallbackListener(id: unknown) {
    if (typeof id === 'number') {
      this.listenerIds.push(id);
    }
  }

  private bindCallbacks() {
    this.registerCallbackListener(
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
      })
    );

    this.registerCallbackListener(
      this.callbackRouter.onConnectionStateChanged.addListener((state: RuntimeConnectionState) => {
        this.patch(snapshot => {
          snapshot.connectionState = state;
        });
      })
    );

    this.registerCallbackListener(
      this.callbackRouter.onSessionUpdated.addListener((session: SessionInfo) => {
        this.patch(state => {
          state.sessionsById[session.sessionId] = session;
          state.sessionOrder = sortSessionIds(state.sessionsById);
        });
      })
    );

    this.registerCallbackListener(
      this.callbackRouter.onAISettingsChanged.addListener(
          (info: AISettingsInfoPayload) => {
            this.applyAISettings(info);
          })
    );

    const runtimeConfigChanged =
        (this.callbackRouter as {onRuntimeConfigChanged?: {
          addListener(listener: (config: RuntimeConfigInfo) => void): number;
        }}).onRuntimeConfigChanged;
    if (runtimeConfigChanged) {
      this.registerCallbackListener(
        runtimeConfigChanged.addListener((config: RuntimeConfigInfo) => {
          this.patch(state => {
            state.runtimeConfigSupported = true;
            state.runtimeConfig = buildRuntimeConfigDispatch(config);
          });
        })
      );
    }

    this.registerCallbackListener(
      this.callbackRouter.onAskMahoSessionAccepted.addListener((requestId: string, sessionInfo: SessionInfo) => {
        void this.ingestAskMahoSessionAccepted(requestId, sessionInfo);
      })
    );

    this.bindCreditBalanceRefresh();
  }

  private creditBalanceRefreshTimer: ReturnType<typeof setTimeout> | undefined =
      undefined;
  private focusListener: (() => void) | null = null;

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
    this.focusListener = scheduleRefresh;
    window.addEventListener('focus', this.focusListener);
  }

  async refreshAISettings(): Promise<void> {
    try {
      const {info} = await this.pageHandler.getAISettings();
      this.applyAISettings(info);
    } catch (err) {
      console.warn('[maho-ai] refreshAISettings failed', err);
    }
  }

  async refreshRuntimeConfig(): Promise<void> {
    if (!supportsRuntimeConfig(this.pageHandler)) {
      this.patch(state => {
        state.runtimeConfigSupported = false;
      });
      return;
    }
    try {
      const {config} = await this.pageHandler.getRuntimeConfig();
      this.patch(state => {
        state.runtimeConfigSupported = true;
        state.runtimeConfig = buildRuntimeConfigDispatch(config);
      });
    } catch (err) {
      console.warn('[maho-ai] refreshRuntimeConfig failed', err);
    }
  }

  /**
   * Applies a runtime config change (permission tier / final_confirm /
   * proactive_mode) to the active session. Patches optimistically, then
   * dispatches the mojom-shaped SetRuntimeConfig call; reverts on rejection
   * or transport failure. Returns whether the change was accepted.
   */
  async setRuntimeConfig(change: Partial<RuntimeConfigInfo>): Promise<boolean> {
    if (!supportsRuntimeConfig(this.pageHandler)) {
      console.warn('[maho-ai] SetRuntimeConfig is unavailable on this page handler');
      return false;
    }

    const previous = this.state.runtimeConfig;
    const next = buildRuntimeConfigDispatch({...previous, ...change});
    this.patch(state => {
      state.runtimeConfig = next;
    });

    try {
      const {accepted} = await this.pageHandler.setRuntimeConfig(next);
      if (!accepted) {
        this.patch(state => {
          state.runtimeConfig = previous;
        });
        return false;
      }
      return true;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      console.warn('[maho-ai] SetRuntimeConfig failed', message);
      this.patch(state => {
        state.runtimeConfig = previous;
      });
      return false;
    }
  }

  private applyAISettings(info: AISettingsInfoPayload): void {
    const aiSettings = normalizeAISettings(info);
    this.patch(state => {
      state.aiSettings = aiSettings;
      state.aiProviders = toLegacyProviderRecords(aiSettings.providerOptions);
    });
  }

  private isPlainReplayDelta(event: RuntimeEvent): boolean {
    return (event.kind === RuntimeEventKind.kAssistantToken ||
            event.kind === RuntimeEventKind.kAssistantThinking) &&
        event.text != null && !event.toolCall && !event.toolResult &&
        !event.approvalRequest && !event.approvalResult && !event.browserContext &&
        !event.artifact &&
        !('interactionRequest' in event && event.interactionRequest != null) &&
        !('credentialErrorCode' in event && event.credentialErrorCode != null);
  }

  private pruneReplayWindow(state: AppState, sessionId: string, through: bigint): void {
    if (through <= (this.replayWindowThroughBySessionId.get(sessionId) ?? -1n)) {
      return;
    }
    this.replayWindowThroughBySessionId.set(sessionId, through);
    const existing = state.eventsBySessionId[sessionId] || [];
    const removedArtifacts = new Set<string>();
    for (const {event} of existing) {
      if (BigInt(event.sequence) > through) {
        continue;
      }
      if (event.toolCall) delete state.toolCallsById[event.toolCall.callId];
      if (event.toolResult) delete state.toolResultsByCallId[event.toolResult.callId];
      if (event.approvalRequest) delete state.approvalsById[event.approvalRequest.approvalId];
      if (event.approvalResult) delete state.approvalResultsById[event.approvalResult.approvalId];
      if (event.artifact) {
        removedArtifacts.add(event.artifact.artifactId);
        this.artifactUrlCache.delete(`preview:${event.artifact.artifactId}`);
        this.artifactUrlCache.delete(`export:${event.artifact.artifactId}`);
      }
    }
    state.eventsBySessionId[sessionId] =
        existing.filter(entry => BigInt(entry.event.sequence) > through);
    state.contextsBySessionId[sessionId] =
        (state.contextsBySessionId[sessionId] || []).filter(
            context => (this.replayContextSequence.get(context) ?? -1n) > through);
    state.artifactsBySessionId[sessionId] =
        (state.artifactsBySessionId[sessionId] || []).filter(
            artifact => !removedArtifacts.has(artifact.artifactId));
    const interactions = this.replayInteractionSequences.get(sessionId);
    state.interactionsBySessionId[sessionId] =
        (state.interactionsBySessionId[sessionId] || []).filter(record => {
          if ((interactions?.get(record.requestId) ?? -1n) > through) {
            return true;
          }
          interactions?.delete(record.requestId);
          return false;
        });
    if (interactions?.size === 0) {
      this.replayInteractionSequences.delete(sessionId);
    }
  }

  private mergeReplayEvents(
      state: AppState, sessionId: string, incoming: RuntimeEvent[]): void {
    for (const event of incoming) {
      if (event.replayWindow) {
        this.pruneReplayWindow(
            state, sessionId, BigInt(event.replayWindow.omittedThroughSequence));
      }
    }
    const existing = state.eventsBySessionId[sessionId] || [];
    const existingThrough = existing.reduce(
        (latest, entry) => BigInt(entry.event.sequence) > latest ?
            BigInt(entry.event.sequence) : latest, -1n);
    const incomingThrough = incoming.reduce(
        (latest, event) => BigInt(event.sequence) > latest ?
            BigInt(event.sequence) : latest, -1n);
    const omittedThrough = this.replayWindowThroughBySessionId.get(sessionId) ?? -1n;
    const byStart = new Map<string, RuntimeEvent>();
    for (const event of [...incoming, ...existing.map(entry => entry.event)]) {
      if (BigInt(event.sequence) <= omittedThrough) {
        continue;
      }
      const start = event.sequenceStart || event.sequence;
      const key = `${start}:${event.kind}:${event.requestId ?? ''}`;
      const previous = byStart.get(key);
      if (!previous || BigInt(event.sequence) >= BigInt(previous.sequence)) {
        byStart.set(key, event);
      }
    }
    const ordered = [...byStart.values()].sort((a, b) =>
      Number(BigInt(a.sequenceStart || a.sequence) - BigInt(b.sequenceStart || b.sequence)) ||
      Number(BigInt(b.sequence) - BigInt(a.sequence)));
    const covered = new Map<string, bigint>();
    const merged = ordered.filter(event => {
      if (!this.isPlainReplayDelta(event)) {
        return true;
      }
      const key = `${event.kind}:${event.requestId ?? ''}`;
      if ((covered.get(key) ?? -1n) >= BigInt(event.sequence)) {
        return false;
      }
      covered.set(key, BigInt(event.sequence));
      return true;
    });
    state.eventsBySessionId[sessionId] = [];
    for (const event of merged) {
      this.applyRuntimeEvent(state, sessionId, event, true);
    }
    if (incomingThrough >= existingThrough ||
        !(state.contextsBySessionId[sessionId] || []).length) {
      this.updateContextSnapshot(state, sessionId, merged);
    }
  }

  private appendEventInternal(state: AppState, sessionId: string, event: RuntimeEvent) {
    if (!isRenderableTimelineEvent(event)) {
      return;
    }

    const existing = state.eventsBySessionId[sessionId] || [];
    const previous = existing[existing.length - 1];

    if (previous && previous.event.kind === event.kind &&
        previous.event.requestId === event.requestId &&
        BigInt(previous.event.sequence) + 1n === BigInt(event.sequenceStart || event.sequence) &&
        this.isPlainReplayDelta(previous.event) && this.isPlainReplayDelta(event)) {
      const previousBytes = this.replayTextBytes.get(previous.event) ??
          this.replayTextEncoder.encode(previous.event.text || '').byteLength;
      const incomingBytes = this.replayTextEncoder.encode(event.text || '').byteLength;
      if (previousBytes + incomingBytes <= 64 * 1024) {
        const mergedEvent: RuntimeEvent = {
          ...previous.event,
          text: `${previous.event.text || ''}${event.text || ''}`,
          timestamp: event.timestamp,
          sequence: event.sequence,
          sequenceStart: previous.event.sequenceStart || previous.event.sequence,
        };
        this.replayTextBytes.set(mergedEvent, previousBytes + incomingBytes);
        state.eventsBySessionId[sessionId] =
            [...existing.slice(0, -1), {...previous, event: mergedEvent}];
        return;
      }
    }

    const nextEntry: TimelineEntry = {sessionId, event};
    state.eventsBySessionId[sessionId] = [...existing, nextEntry];
  }

  private applyRuntimeEvent(
      state: AppState, sessionId: string, event: RuntimeEvent, replay = false): void {
    if (event.replayWindow) {
      this.pruneReplayWindow(
          state, sessionId, BigInt(event.replayWindow.omittedThroughSequence));
    }
    if (BigInt(event.sequence) <=
        (this.replayWindowThroughBySessionId.get(sessionId) ?? -1n)) {
      return;
    }
    const isSelectedSession = !replay && state.currentSessionId === sessionId;
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
    if (event.kind === RUNTIME_EVENT_KIND_INTERACTION_REQUEST) {
      // The generated mojom bindings do not carry interactionRequest until the
      // deferred regeneration; read it defensively off the wire event.
      const payload = (event as Partial<RuntimeEvent> &
          {interactionRequest?: InteractionRequestInfoPayload}).interactionRequest;
      if (payload?.requestId) {
        const record = buildInteractionRequestRecord(payload);
        const sequences =
            this.replayInteractionSequences.get(sessionId) || new Map<string, bigint>();
        this.replayInteractionSequences.set(sessionId, sequences);
        sequences.set(record.requestId, BigInt(event.sequence));
        const existing = state.interactionsBySessionId[sessionId] || [];
        // De-dupe by requestId so history replay and later lifecycle updates
        // (expired/cancelled) replace the stored record in arrival order.
        const filtered = existing.filter(entry => entry.requestId !== record.requestId);
        state.interactionsBySessionId[sessionId] = [...filtered, record];
      }
    }
    if (!replay &&
        (event.kind === RuntimeEventKind.kTurnComplete || event.kind === RuntimeEventKind.kError)) {
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
      this.replayContextSequence.set(event.browserContext, BigInt(event.sequence));
      if (!replay) {
        const existing = state.contextsBySessionId[sessionId] || [];
        state.contextsBySessionId[sessionId] = [...existing, event.browserContext];
      }
    }
    if (!replay && event.kind === RuntimeEventKind.kToolAvailabilityChanged) {
      const activeSpaceId = state.activeWorkspace?.spaceId ?? null;
      void this.loadAiExtensibility(activeSpaceId);
    }
    if (event.kind === RuntimeEventKind.kArtifactCreated && event.artifact) {
      const artifact = event.artifact;
      if (this.deletedArtifactIds.has(artifact.artifactId)) {
        state.eventsBySessionId[sessionId] = (state.eventsBySessionId[sessionId] || [])
            .filter(entry => entry.event.artifact?.artifactId !== artifact.artifactId);
        return;
      }
      const existing = state.artifactsBySessionId[sessionId] || [];
      // De-dupe by artifactId so history replay does not duplicate cards.
      const filtered = existing.filter(a => a.artifactId !== artifact.artifactId);
      state.artifactsBySessionId[sessionId] = [...filtered, artifact];
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

  async approveMcpServerTrust(workspaceId: string, serverName: string, tools: string[]): Promise<boolean> {
    try {
      const {success} = await this.pageHandler.approveMcpServerTrust(workspaceId, serverName, tools);
      if (success) {
        toast.success('MCP server trusted');
        await this.loadAiExtensibility(this.state.activeWorkspace?.spaceId);
        return true;
      }
      toast.error('Failed to trust MCP server');
      return false;
    } catch (err) {
      console.error('[maho-ai] approveMcpServerTrust failed', err);
      toast.error('Failed to trust MCP server');
      return false;
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
      toast.error('Failed to register MCP server');
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
      toast.error('Failed to remove MCP server');
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
      toast.error('Failed to register CLI tool');
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
      toast.error('Failed to create agent profile');
      return false;
    }
  }

  setActiveSlashEditor(editor: AppState['activeSlashEditor']) {
    this.patch(state => {
      state.activeSlashEditor = editor;
    });
  }
}
