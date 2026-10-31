import {describe, it, expect, vi, beforeEach} from 'vitest';
import {MahoAiStore} from '../../store.js';
import {ComposerDraftPersistence} from '../features/compact/composer-draft-persistence.js';
import {
  CredentialErrorCode,
  getCredentialErrorPresentation,
  type CredentialRuntimeEvent,
} from '../../views/credential_error.js';
import {
  AI_REASONING_EFFORT,
  type AISelectionRequest,
} from '../../types.js';
import {
  type AISettingsInfo,
  InteractionMode,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RoutineRunStatus,
  type RuntimeEvent,
  RuntimeEventKind,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';
import {
  ChatIntent,
  type QuickAction,
} from '../../types.js';

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): number;
  removeListener(id: number): boolean;
} {
  return {
    addListener: vi.fn(() => 1),
    removeListener: vi.fn(() => true),
  };
}

function createPageCallbackRouterFake(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  router.onRoutineRunStatusChanged =
      createCallbackRoute<[RoutineRunStatus]>();
  return router;
}

function createSession(sessionId: string, title = sessionId): SessionInfo {
  return {
    sessionId,
    title,
    summary: '',
    createdAt: 1,
    updatedAt: 1,
    adapterName: 'OpenCode',
    isActive: true,
    isReadOnly: false,
    status: SessionStatus.kActive,
    eventCount: 0,
    toolCallCount: 0,
    runtimeSessionId: sessionId,
    lastRuntimeState: 'idle',
  };
}

function createRuntimeEvent(
    sessionId: string, kind: RuntimeEventKind, sequence: number, text: string): RuntimeEvent {
  return {sessionId, kind, sequence, timestamp: sequence, text};
}

function draftPersistenceWithClear(clear: (scope: {
  readonly kind: 'new_task'|'conversation';
  readonly conversationId?: string;
}) => Promise<void>, overrides: Partial<{
  flush: () => Promise<void>;
  load: (scope: {
    readonly kind: 'new_task'|'conversation';
    readonly conversationId?: string;
  }) => Promise<string|null>;
  schedule: (scope: {
    readonly kind: 'new_task'|'conversation';
    readonly conversationId?: string;
  }, text: string) => void;
}> = {}) {
  return {
    clear,
    flush: async () => undefined,
    load: async () => null,
    schedule: () => undefined,
    ...overrides,
  };
}

function createDeferred<T>(): {
  readonly promise: Promise<T>;
  readonly reject: (reason?: unknown) => void;
  readonly resolve: (value: T) => void;
} {
  const reject = vi.fn<(reason?: unknown) => void>();
  const resolve = vi.fn<(value: T) => void>();
  const promise = new Promise<T>((complete, fail) => {
    reject.mockImplementation(reason => fail(reason));
    resolve.mockImplementation(complete);
  });
  return {promise, reject, resolve};
}

describe('MahoAiStore — session resume and history fetch behavior', () => {
  let mockHandler: PageHandlerRemote;
  let mockRouter: PageCallbackRouter;
  let store: MahoAiStore;

  beforeEach(() => {
    // Standard mock setup
    mockHandler = new PageHandlerRemote();
    mockRouter = createPageCallbackRouterFake();

    // Mock connection state
    vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
      state: RuntimeConnectionState.kConnected,
      activeAdapterName: 'OpenCode',
    });

    // Mock session list returning a single session
    vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
      sessions: [
        {
          sessionId: 'test-session-123',
          adapterName: 'OpenCode',
          isActive: true,
          isReadOnly: false,
          status: SessionStatus.kIdle,
          createdAt: Date.now() / 1000,
          updatedAt: Date.now() / 1000,
          title: 'Test Session',
          summary: '',
          eventCount: 0,
          toolCallCount: 0,
          lastRuntimeState: 'idle',
          runtimeSessionId: 'test-session-123',
        },
      ],
    });

    // Mock resume session return
    vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
      session: {
        sessionId: 'test-session-123',
        adapterName: 'OpenCode',
        isActive: true,
        isReadOnly: false,
        status: SessionStatus.kIdle,
        createdAt: Date.now() / 1000,
        updatedAt: Date.now() / 1000,
        title: 'Test Session',
        summary: '',
        eventCount: 0,
        toolCallCount: 0,
        lastRuntimeState: 'idle',
        runtimeSessionId: 'test-session-123',
      },
      replayEvents: [],
    });

    // Mock session history
    vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
      events: [],
      totalCount: 0,
    });
    mockHandler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
    mockHandler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
    mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
      info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
    });
    mockHandler.openSettingsPane = vi.fn().mockResolvedValue(undefined);

    store = new MahoAiStore(mockHandler, mockRouter);
  });

  it('creates one shared renderer Routine client from the AI handler/router', async () => {
    mockHandler.listAllRoutines =
        vi.fn().mockResolvedValue({routines: [{id: 'daily'}]});
    const statusListener = vi.fn();
    const routineClient = store.getRoutineOperationsClient();
    expect(store.getRoutineOperationsClient()).toBe(routineClient);
    const dispose = routineClient.subscribe(statusListener);

    await expect(routineClient.list()).resolves.toEqual([{id: 'daily'}]);
    expect(mockHandler.listAllRoutines).toHaveBeenCalledOnce();
    expect(mockRouter.onRoutineRunStatusChanged.addListener)
        .toHaveBeenCalledOnce();
    dispose();
  });

  it('boots and automatically resumes session, loading its history', async () => {
    const resumeSessionSpy = vi.spyOn(store, 'resumeSession');
    const loadHistorySpy = vi.spyOn(store, 'loadHistory');

    await store.bootstrap();

    // Verify it automatically resumes the session
    expect(resumeSessionSpy).toHaveBeenCalledOnce();
    expect(resumeSessionSpy).toHaveBeenCalledWith('test-session-123');

    // Verify history was fetched because fetchHistory defaults to true
    expect(loadHistorySpy).toHaveBeenCalledOnce();
    expect(loadHistorySpy).toHaveBeenCalledWith('test-session-123');
    expect(mockHandler.getSessionHistory).toHaveBeenCalledWith('test-session-123', 0, 200);
  });

  it('loads history on explicit resumeSession call when fetchHistory defaults to true', async () => {
    const loadHistorySpy = vi.spyOn(store, 'loadHistory');

    // Bootstrap first (ignoring previous calls)
    await store.bootstrap();
    loadHistorySpy.mockClear();
    vi.mocked(mockHandler.getSessionHistory).mockClear();

    // Call resumeSession directly without the second argument (fetchHistory should default to true)
    await store.resumeSession('test-session-123');

    expect(loadHistorySpy).toHaveBeenCalledOnce();
    expect(loadHistorySpy).toHaveBeenCalledWith('test-session-123');
    expect(mockHandler.getSessionHistory).toHaveBeenCalledWith('test-session-123', 0, 200);
  });

  it('does not load history if fetchHistory is explicitly set to false', async () => {
    const loadHistorySpy = vi.spyOn(store, 'loadHistory');

    await store.bootstrap();
    loadHistorySpy.mockClear();
    vi.mocked(mockHandler.getSessionHistory).mockClear();

    // Call resumeSession with fetchHistory = false
    await store.resumeSession('test-session-123', false);

    expect(loadHistorySpy).not.toHaveBeenCalled();
    expect(mockHandler.getSessionHistory).not.toHaveBeenCalled();
  });

  it('searches bookmarks and handles result, and resets properly', async () => {
    vi.spyOn(mockHandler, 'searchBookmarks').mockResolvedValue({
      items: [
        {
          id: 'b1',
          url: 'https://google.com',
          title: 'Google',
        },
      ],
    });

    await store.bootstrap();

    // Initial state check
    expect(store.getSnapshot().bookmarksSearch.items).toEqual([]);

    // Trigger search
    const searchPromise = store.searchBookmarks('google');
    expect(store.getSnapshot().bookmarksSearch.loading).toBe(true);

    await searchPromise;

    expect(store.getSnapshot().bookmarksSearch.loading).toBe(false);
    expect(store.getSnapshot().bookmarksSearch.items).toEqual([
      {
        id: 'b1',
        url: 'https://google.com',
        title: 'Google',
      },
    ]);

    // Reset search
    store.resetBookmarksSearch();
    expect(store.getSnapshot().bookmarksSearch.items).toEqual([]);
    expect(store.getSnapshot().bookmarksSearch.query).toBe('');
  });

  it('routes token, error, and terminal events only to their immutable owning session', async () => {
    // Given
    const sessionA = createSession('session-a');
    const sessionB = createSession('session-b');
    await store.ingestAskMahoSessionAccepted('request-a', sessionA);
    await store.ingestAskMahoSessionAccepted('request-b', sessionB);
    const sessionBBefore = store.getSnapshot().eventsBySessionId[sessionB.sessionId];
    const runtimeListener = vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    expect(runtimeListener).toBeTypeOf('function');

    // When
    runtimeListener?.(createRuntimeEvent(
        sessionA.sessionId, RuntimeEventKind.kAssistantToken, 1, 'token'));
    runtimeListener?.(createRuntimeEvent(
        sessionA.sessionId, RuntimeEventKind.kError, 2, 'runtime failed'));
    runtimeListener?.(createRuntimeEvent(
        sessionA.sessionId, RuntimeEventKind.kTurnComplete, 3, 'complete'));

    // Then
    const snapshot = store.getSnapshot();
    expect(snapshot.currentSessionId).toBe(sessionB.sessionId);
    expect(snapshot.eventsBySessionId[sessionA.sessionId]?.map(entry => entry.event.kind))
        .toEqual([
          RuntimeEventKind.kAssistantToken,
          RuntimeEventKind.kError,
          RuntimeEventKind.kTurnComplete,
        ]);
    expect(snapshot.turnPendingBySessionId[sessionA.sessionId]).toBe(false);
    expect(snapshot.eventsBySessionId[sessionB.sessionId]).toEqual(sessionBBefore);
    expect(snapshot.turnPendingBySessionId[sessionB.sessionId]).toBe(true);
  });

  it('logs and ignores a runtime event for an unknown session id', async () => {
    // Given
    const selectedSession = createSession('selected-session');
    await store.ingestAskMahoSessionAccepted('selected-request', selectedSession);
    const snapshotBefore = store.getSnapshot();
    const runtimeListener = vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    expect(runtimeListener).toBeTypeOf('function');

    // When
    runtimeListener?.(createRuntimeEvent(
        'unknown-session', RuntimeEventKind.kAssistantToken, 1, 'orphan'));

    // Then
    expect(store.getSnapshot()).toBe(snapshotBefore);
    expect(warn).toHaveBeenCalledWith(
        '[maho-ai] runtime event deferred for unknown session', 'unknown-session');
    warn.mockRestore();
  });

  it('keeps a newer manual resume selection when bootstrap and an older resume finish late', async () => {
    // Given
    const bootstrapSession = createSession('bootstrap-session');
    const manualSession = createSession('manual-session');
    const sessionList = createDeferred<{sessions: SessionInfo[]}>();
    const bootstrapResume = createDeferred<{session: SessionInfo|null; replayEvents: RuntimeEvent[]}>();
    const manualResume = createDeferred<{session: SessionInfo|null; replayEvents: RuntimeEvent[]}>();
    vi.mocked(mockHandler.getSessionList).mockReturnValue(sessionList.promise);
    vi.mocked(mockHandler.resumeSession).mockImplementation(sessionId =>
      sessionId === manualSession.sessionId ? manualResume.promise : bootstrapResume.promise);
    const bootstrapPromise = store.bootstrap();
    const manualPromise = store.resumeSession(manualSession.sessionId, false);

    // When
    sessionList.resolve({sessions: [bootstrapSession]});
    await Promise.resolve();
    bootstrapResume.resolve({session: bootstrapSession, replayEvents: []});
    await Promise.resolve();
    manualResume.resolve({session: manualSession, replayEvents: []});
    await manualPromise;
    await bootstrapPromise;

    // Then
    expect(store.getSnapshot().currentSessionId).toBe(manualSession.sessionId);
  });

  it('does not resume a stale bootstrap snapshot after a newer accepted session is selected', async () => {
    // Given
    const sessionList = createDeferred<{sessions: SessionInfo[]}>();
    const bootstrapSession = createSession('bootstrap-session');
    const acceptedSession = createSession('accepted-session');
    vi.mocked(mockHandler.getSessionList).mockReturnValue(sessionList.promise);
    const bootstrapPromise = store.bootstrap();
    await store.ingestAskMahoSessionAccepted('accepted-request', acceptedSession);

    // When
    sessionList.resolve({sessions: [bootstrapSession]});
    await bootstrapPromise;

    // Then
    expect(store.getSnapshot().currentSessionId).toBe(acceptedSession.sessionId);
    expect(mockHandler.resumeSession).not.toHaveBeenCalled();
  });

  it('serializes overlapping submissions for an existing session', async () => {
    // Given
    const firstSubmission = createDeferred<{accepted: boolean}>();
    const submitPrompt = vi.spyOn(mockHandler, 'submitPrompt')
                             .mockReturnValue(firstSubmission.promise);
    await store.resumeSession('test-session-123', false);
    store.setComposerPrompt('first request');
    const firstPromise = store.submitPrompt();
    store.setComposerPrompt('next draft');

    // When
    const overlappingPromise = store.submitPrompt();
    firstSubmission.resolve({accepted: true});
    await Promise.all([firstPromise, overlappingPromise]);

    // Then
    expect.soft(submitPrompt).toHaveBeenCalledOnce();
    expect.soft(store.getSnapshot().composer.prompt).toBe('next draft');
  });

  it('keeps a session blocked after Mojo resolves until a terminal runtime event', async () => {
    // Given
    const submitPrompt = vi.spyOn(mockHandler, 'submitPrompt')
                             .mockResolvedValue({accepted: true});
    await store.resumeSession('test-session-123', false);
    const runtimeListener = vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    expect(runtimeListener).toBeTypeOf('function');
    store.setComposerPrompt('first request');
    await store.submitPrompt();
    store.setComposerPrompt('second draft');
    store.setChatBlockedReason('quota-exhausted');

    // When
    await store.submitPrompt();

    // Then
    expect.soft(submitPrompt).toHaveBeenCalledOnce();
    expect.soft(store.getSnapshot().turnPendingBySessionId['test-session-123']).toBe(true);
    expect.soft(store.getSnapshot().composer.prompt).toBe('second draft');
    expect.soft(store.getSnapshot().chatBlockedReason).toBe('quota-exhausted');

    // When
    runtimeListener?.(createRuntimeEvent(
        'test-session-123', RuntimeEventKind.kTurnComplete, 1, 'complete'));
    await store.submitPrompt();

    // Then
    expect(submitPrompt).toHaveBeenCalledTimes(2);
    expect(submitPrompt).toHaveBeenLastCalledWith(
        'test-session-123', 'second draft', false, InteractionMode.kAssistant, null, ChatIntent.kFreeform);
  });

  it('blocks resumed active turns until a live terminal event', async () => {
    // Given
    const submitPrompt = vi.spyOn(mockHandler, 'submitPrompt')
                             .mockResolvedValue({accepted: true});
    vi.mocked(mockHandler.resumeSession).mockResolvedValue({
      session: {
        ...createSession('resumed-active-session'),
        isActive: true,
        status: SessionStatus.kActive,
      },
      replayEvents: [],
    });
    await store.resumeSession('resumed-active-session', false);

    const runtimeListener =
        vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    expect(runtimeListener).toBeTypeOf('function');

    // When
    store.setComposerPrompt('prompt during active turn');
    await store.submitPrompt();

    // Then
    expect(submitPrompt).not.toHaveBeenCalled();
    expect(store.getSnapshot().turnPendingBySessionId['resumed-active-session']).toBe(true);
    expect(store.getSnapshot().composer.prompt).toBe('prompt during active turn');

    // When
    runtimeListener?.(createRuntimeEvent(
        'resumed-active-session', RuntimeEventKind.kTurnComplete, 1, 'turn complete'));
    expect(store.getSnapshot().turnPendingBySessionId['resumed-active-session']).toBe(false);

    // Then
    await store.submitPrompt();
    expect(submitPrompt).toHaveBeenCalledOnce();
    expect(submitPrompt).toHaveBeenLastCalledWith(
        'resumed-active-session',
        'prompt during active turn',
        false,
        InteractionMode.kAssistant,
        null,
        ChatIntent.kFreeform);
  });

  it('persists AI provider, model, and reasoning selection through the page handler', async () => {
    // Given
    const setDefaultAISelection = vi.fn<(providerId: string, modelId: string,
        reasoningEffort: number) => Promise<{accepted: boolean}>>()
        .mockResolvedValue({accepted: true});
    const refreshedInfo = {
      activeModelId: 'gpt-4.1',
      activeProviderId: 'openai',
      activeReasoningEffort: AI_REASONING_EFFORT.kHigh,
      providerOptions: [{
        id: 'openai',
        label: 'OpenAI',
        modelOptions: [{id: 'gpt-4.1', label: 'GPT-4.1'}],
      }],
      reasoningOptions: [{effort: AI_REASONING_EFFORT.kHigh, label: 'High'}],
    };
    const getAISettings = vi.fn<() => Promise<{info: typeof refreshedInfo}>>()
        .mockResolvedValue({info: refreshedInfo});
    Object.assign(mockHandler, {getAISettings, setDefaultAISelection});
    const selection: AISelectionRequest = {
      modelId: 'gpt-4.1',
      providerId: 'openai',
      reasoningEffort: AI_REASONING_EFFORT.kHigh,
    };

    // When
    const accepted = await store.setDefaultAISelection(selection);

    // Then
    expect(accepted).toBe(true);
    expect(setDefaultAISelection).toHaveBeenCalledWith(
        'openai', 'gpt-4.1', AI_REASONING_EFFORT.kHigh);
    expect(getAISettings).toHaveBeenCalledOnce();
    expect(store.getSnapshot().aiSettings).toMatchObject({
      activeModelId: 'gpt-4.1',
      activeProviderId: 'openai',
      activeReasoningEffort: AI_REASONING_EFFORT.kHigh,
    });
    expect(store.getSnapshot().aiSettings.providerOptions[0]?.modelOptions[0]?.label)
        .toBe('GPT-4.1');
  });

  it('preserves Low reasoning across a settings refresh', async () => {
    const info = {
      activeModelId: 'model', activeProviderId: 'provider',
      activeReasoningEffort: AI_REASONING_EFFORT.kLow,
      providerOptions: [], reasoningOptions: [],
    };
    vi.spyOn(mockHandler, 'getAISettings').mockResolvedValue({info});
    await store.refreshAISettings();
    expect(store.getSnapshot().aiSettings.activeReasoningEffort).toBe(AI_REASONING_EFFORT.kLow);
  });

  it.each(['clear', 'newer', 'stale error'] as const)('ignores superseded history responses after %s', async action => {
    const response = createDeferred<{items: []}>();
    vi.spyOn(mockHandler, 'searchHistory').mockReturnValueOnce(response.promise);
    const searching = store.searchHistory('old query');
    if (action === 'clear') store.resetHistorySearch();
    else {
      vi.mocked(mockHandler.searchHistory).mockResolvedValueOnce({items: []});
      await store.searchHistory('new query');
    }
    if (action === 'stale error') response.reject(new Error('Obsolete failure'));
    else response.resolve({items: []});
    await searching;
    expect(store.getSnapshot().historySearch).toEqual({
      query: action === 'clear' ? '' : 'new query', items: [], loading: false, error: null,
    });
  });

  it.each([false, true])('keeps a newer persisted draft after acceptance (switch=%s)', async switchSession => {
    const drafts = new Map<string, string>();
    const persistence = new ComposerDraftPersistence({
      composerDraftGet: async scope => ({draftJson: drafts.has(scope) ? JSON.stringify({version: 1, text: drafts.get(scope)}) : null}),
      composerDraftSet: async (scope, text) => { drafts.set(scope, text); return {ok: true}; },
      composerDraftDelete: async scope => { drafts.delete(scope); return {ok: true}; },
    });
    store.setComposerDraftPersistenceForTesting(persistence);
    await store.resumeSession('test-session-123', false);
    const submission = createDeferred<{accepted: boolean}>();
    vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(submission.promise);
    store.setComposerPrompt('submitted prompt');
    const sending = store.submitPrompt();
    if (switchSession) {
      vi.mocked(mockHandler.resumeSession).mockResolvedValueOnce({session: createSession('session-b'), replayEvents: []});
      await store.resumeSession('session-b', false);
    }
    store.setComposerPrompt('next unsent message');
    await persistence.flush();
    submission.resolve({accepted: true});
    await sending;
    const scope = {kind: 'conversation' as const, conversationId: switchSession ? 'session-b' : 'test-session-123'};
    expect(await persistence.load(scope)).toBe('next unsent message');
  });

  it('restores destination text and preserves outgoing text when switching conversations', async () => {
    const drafts = new Map<string, string>([['session-b', 'saved B draft']]);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(async () => {}, {
      load: async scope => drafts.get(scope.conversationId!) ?? null,
      schedule: (scope, text) => { drafts.set(scope.conversationId!, text); },
    }));
    await store.resumeSession('test-session-123', false);
    store.setComposerPrompt('A draft');
    vi.mocked(mockHandler.resumeSession).mockResolvedValueOnce({session: createSession('session-b'), replayEvents: []});
    await store.resumeSession('session-b', false);
    expect.soft(store.getSnapshot().composer.prompt).toBe('saved B draft');
    store.setComposerPrompt(store.getSnapshot().composer.prompt + ' edited');
    expect.soft(drafts.get('session-b')).toBe('saved B draft edited');
    expect.soft(drafts.get('test-session-123')).toBe('A draft');
  });

  it('clears the persisted draft only after Mojo accepts the submission', async () => {
    // Given
    const submission = createDeferred<{accepted: boolean}>();
    const clear = vi.fn(async () => undefined);
    vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(submission.promise);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear));
    await store.resumeSession('test-session-123', false);
    store.setComposerPrompt('send this request');

    // When
    const submitPromise = store.submitPrompt();

    // Then
    expect(clear).not.toHaveBeenCalled();

    // When
    submission.resolve({accepted: true});
    await submitPromise;

    // Then
    expect(clear).toHaveBeenCalledOnce();
    expect(clear).toHaveBeenCalledWith({
      kind: 'conversation',
      conversationId: 'test-session-123',
    });
  });

  it('preserves the draft when Mojo explicitly declines acceptance', async () => {
    // Given
    const clear = vi.fn(async () => undefined);
    vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: false});
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear));
    await store.resumeSession('test-session-123', false);
    store.setComposerPrompt('keep this request');

    // When / Then
    await expect(store.submitPrompt()).rejects.toThrow(
        'Maho AI did not accept the prompt.');
    expect(store.getSnapshot().composer.prompt).toBe('keep this request');
    expect(clear).not.toHaveBeenCalled();
  });

  it('promotes an empty provisional session draft only after its first accepted send', async () => {
    const provisional = createSession('provisional-session');
    const submission = createDeferred<{accepted: boolean}>();
    const clear = vi.fn(async () => undefined);
    const schedule = vi.fn();
    vi.spyOn(mockHandler, 'startSession').mockResolvedValue({session: provisional});
    vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({session: provisional, replayEvents: []});
    vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(submission.promise);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear, {schedule}));

    await store.startSession();
    const clearsBeforeFirstSend = clear.mock.calls.length;
    store.setComposerPrompt('first prompt');
    const sent = store.submitPrompt();
    await Promise.resolve();

    expect(clear).toHaveBeenCalledTimes(clearsBeforeFirstSend);
    submission.resolve({accepted: true});
    await sent;

    expect(clear).toHaveBeenCalledWith({kind: 'new_task'});
    store.setComposerPrompt('second draft');
    expect(schedule).toHaveBeenLastCalledWith(
        {kind: 'conversation', conversationId: 'provisional-session'},
        'second draft');
  });

  it('ignores late draft hydration after the user types', async () => {
    // Given
    const load = createDeferred<string|null>();
    const clear = vi.fn(async () => undefined);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear, {
      load: () => load.promise,
    }));
    const resume = store.resumeSession('test-session-123', false);
    await Promise.resolve();
    store.setComposerPrompt('typed while loading');

    // When
    load.resolve('stale persisted text');
    await resume;

    // Then
    expect(store.getSnapshot().composer.prompt).toBe('typed while loading');
  });

  it('ignores late hydration from a scope that is no longer selected', async () => {
    // Given
    const firstLoad = createDeferred<string|null>();
    const load = vi.fn(scope => scope.conversationId === 'test-session-123' ?
        firstLoad.promise : Promise.resolve('new scope draft'));
    const clear = vi.fn(async () => undefined);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear, {load}));
    const firstResume = store.resumeSession('test-session-123', false);
    await Promise.resolve();
    vi.mocked(mockHandler.resumeSession).mockResolvedValueOnce({
      session: createSession('session-b'),
      replayEvents: [],
    });
    await store.resumeSession('session-b', false);

    // When
    firstLoad.resolve('stale old scope draft');
    await firstResume;

    // Then
    expect(store.getSnapshot().currentSessionId).toBe('session-b');
    expect(store.getSnapshot().composer.prompt).toBe('new scope draft');
  });

  it('rolls back composer state when Mojo rejects a submission', async () => {
    // Given
    const submission = createDeferred<{accepted: boolean}>();
    const clear = vi.fn(async () => undefined);
    vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(submission.promise);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear));
    await store.resumeSession('test-session-123', false);
    store.setChatBlockedReason('quota-exhausted');
    store.setComposerPrompt('retry this request');
    const submitPromise = store.submitPrompt();
    const rejection = new Error('Mojo submit rejected');

    // When
    submission.reject(rejection);

    // Then
    await expect(submitPromise).rejects.toBe(rejection);
    const snapshot = store.getSnapshot();
    expect.soft(snapshot.turnPendingBySessionId['test-session-123']).toBe(false);
    expect.soft(snapshot.composer.prompt).toBe('retry this request');
    expect.soft(snapshot.chatBlockedReason).toBe('quota-exhausted');
    expect(clear).not.toHaveBeenCalled();
  });

  it('preserves a user replacement when a submission fails', async () => {
    // Given
    const submission = createDeferred<{accepted: boolean}>();
    const clear = vi.fn(async () => undefined);
    vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(submission.promise);
    store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear));
    await store.resumeSession('test-session-123', false);
    store.setComposerPrompt('submitted text');
    const submitPromise = store.submitPrompt();
    store.setComposerPrompt('replacement typed while sending');
    const rejection = new Error('Mojo submit rejected');

    // When
    submission.reject(rejection);

    // Then
    await expect(submitPromise).rejects.toBe(rejection);
    expect(store.getSnapshot().composer.prompt)
        .toBe('replacement typed while sending');
    expect(clear).not.toHaveBeenCalled();
  });

  it('opens default settings or a requested pane', async () => {
    const openSettings = vi.spyOn(mockHandler, 'openSettings').mockResolvedValue(undefined);
    const openSettingsPane = vi.mocked(mockHandler.openSettingsPane);

    await store.openSettings();
    await store.openSettings(' maho-ai-developers ');

    expect(openSettings).toHaveBeenCalledOnce();
    expect(openSettingsPane).toHaveBeenCalledOnce();
    expect(openSettingsPane).toHaveBeenCalledWith('maho-ai-developers');
  });

  it('preserves typed credential metadata from persisted replay events', async () => {
    // Given: resumeSession returns the same typed shape reconstructed from persistence.
    const replayEvent: CredentialRuntimeEvent = {
      credentialErrorCode: CredentialErrorCode.kCredentialDecryptFailed,
      kind: RuntimeEventKind.kError,
      sequence: 7,
      sessionId: 'test-session-123',
      text: 'Your saved AI credential could not be used.',
      timestamp: 7,
    };
    vi.mocked(mockHandler.resumeSession).mockResolvedValue({
      replayEvents: [replayEvent],
      session: createSession('test-session-123'),
    });

    // When: the session is resumed without a second history fetch.
    await store.resumeSession('test-session-123', false);

    // Then: the typed code reaches the existing timeline entry unchanged.
    const replayedEvent = store.getSnapshot()
        .eventsBySessionId['test-session-123']?.[0]?.event;
    expect(replayedEvent?.kind).toBe(RuntimeEventKind.kError);
    expect(replayedEvent ? getCredentialErrorPresentation(replayedEvent) : null)
        .toMatchObject({
          settingsPane: 'maho-ai',
          title: 'Re-enter your AI credential',
        });
  });

  describe('regenerateLastResponse', () => {
    it('resubmits the last user prompt without touching the composer', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);
      const runtimeListener =
          vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kUserPrompt, 1, 'explain this'));
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kAssistantToken, 2, 'bad answer'));
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kTurnComplete, 3, 'done'));
      store.setComposerPrompt('unrelated draft');

      await store.regenerateLastResponse();

      expect(mockHandler.submitPrompt).toHaveBeenCalledOnce();
      expect(mockHandler.submitPrompt).toHaveBeenCalledWith(
          'test-session-123',
          'explain this',
          false,
          InteractionMode.kAssistant,
          null,
          ChatIntent.kFreeform);
      expect(store.getSnapshot().composer.prompt).toBe('unrelated draft');
      expect(store.getSnapshot().turnPendingBySessionId['test-session-123'])
          .toBe(true);
    });

    it('uses the newest user prompt when several turns exist', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);
      const runtimeListener =
          vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kUserPrompt, 1, 'first ask'));
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kTurnComplete, 2, 'done'));
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kUserPrompt, 3, 'second ask'));
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kTurnComplete, 4, 'done'));

      await store.regenerateLastResponse();

      expect(mockHandler.submitPrompt).toHaveBeenCalledWith(
          'test-session-123',
          'second ask',
          false,
          InteractionMode.kAssistant,
          null,
          ChatIntent.kFreeform);
    });

    it('replays a quick-action turn with its typed intent and page context', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);
      await store.submitQuickAction({
        id: 'quiz-me',
        label: 'Quiz me',
        userPrompt: 'Quiz me on this page.',
        intent: ChatIntent.kQuizCurrentPage,
      });
      const runtimeListener =
          vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kUserPrompt, 1,
          'Quiz me on this page.'));
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kTurnComplete, 2, 'done'));
      vi.mocked(mockHandler.submitPrompt).mockClear();

      await store.regenerateLastResponse();

      // Resending as plain freeform would drop the quiz directive and the page,
      // which is exactly how the panel produced a contextless answer before.
      expect(mockHandler.submitPrompt).toHaveBeenCalledWith(
          'test-session-123',
          'Quiz me on this page.',
          true,
          InteractionMode.kAssistant,
          null,
          ChatIntent.kQuizCurrentPage);
    });

    it('does nothing when the session has no user prompt to replay', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);

      await store.regenerateLastResponse();

      expect(mockHandler.submitPrompt).not.toHaveBeenCalled();
    });

    it('keeps the composer clean when the resubmission fails', async () => {
      vi.spyOn(mockHandler, 'submitPrompt')
          .mockRejectedValue(new Error('remote failed'));
      await store.resumeSession('test-session-123', false);
      const runtimeListener =
          vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
      runtimeListener?.(createRuntimeEvent(
          'test-session-123', RuntimeEventKind.kUserPrompt, 1, 'explain this'));

      await expect(store.regenerateLastResponse()).rejects.toThrow('remote failed');

      const snapshot = store.getSnapshot();
      expect(snapshot.composer.prompt).toBe('');
      expect(snapshot.turnPendingBySessionId['test-session-123']).toBe(false);
    });
  });

  describe('submitQuickAction', () => {
    const summarizeAction: QuickAction = {
      id: 'summarize',
      label: 'Summarize',
      userPrompt: 'Summarize this page.',
      intent: ChatIntent.kSummarizeCurrentPage,
    };

    const quizAction: QuickAction = {
      id: 'quiz-me',
      label: 'Quiz me',
      userPrompt: 'Quiz me on this page.',
      intent: ChatIntent.kQuizCurrentPage,
    };

    it('submits a quick action for an existing session with exact captured parameters and ChatIntent', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);
      store.setComposerPrompt('draft prompt text');

      await store.submitQuickAction(summarizeAction);

      expect(mockHandler.submitPrompt).toHaveBeenCalledOnce();
      expect(mockHandler.submitPrompt).toHaveBeenCalledWith(
          'test-session-123',
          'Summarize this page.',
          true,
          InteractionMode.kAssistant,
          null,
          ChatIntent.kSummarizeCurrentPage);

      const snapshot = store.getSnapshot();
      expect(snapshot.composer.prompt).toBe('draft prompt text');
      expect(snapshot.turnPendingBySessionId['test-session-123']).toBe(true);
    });

    it('creates a new session and promotes quick-action draft scope only after acceptance', async () => {
      const newSession = createSession('new-session-456');
      const accepted = createDeferred<{accepted: boolean}>();
      const clear = vi.fn(async () => undefined);
      const schedule = vi.fn();
      vi.spyOn(mockHandler, 'startSession').mockResolvedValue({session: newSession});
      vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({session: newSession, replayEvents: []});
      vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(accepted.promise);
      store.setComposerDraftPersistenceForTesting(draftPersistenceWithClear(clear, {schedule}));

      const submission = store.submitQuickAction(quizAction);
      await Promise.resolve();
      await Promise.resolve();
      expect(clear).not.toHaveBeenCalled();
      accepted.resolve({accepted: true});
      await submission;

      expect(mockHandler.startSession).toHaveBeenCalledOnce();
      expect(mockHandler.startSession).toHaveBeenCalledWith(null, InteractionMode.kAssistant);
      expect(mockHandler.submitPrompt).toHaveBeenCalledOnce();
      expect(mockHandler.submitPrompt).toHaveBeenCalledWith(
          'new-session-456',
          'Quiz me on this page.',
          true,
          InteractionMode.kAssistant,
          null,
          ChatIntent.kQuizCurrentPage);

      const snapshot = store.getSnapshot();
      expect(snapshot.currentSessionId).toBe('new-session-456');
      expect(snapshot.turnPendingBySessionId['new-session-456']).toBe(true);
      expect(clear).toHaveBeenCalledWith({kind: 'new_task'});
    });

    it('normal submitPrompt passes kFreeform intent', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);
      store.setComposerPrompt('freeform question');

      await store.submitPrompt();

      expect(mockHandler.submitPrompt).toHaveBeenCalledWith(
          'test-session-123',
          'freeform question',
          false,
          InteractionMode.kAssistant,
          null,
          ChatIntent.kFreeform);
    });

    it('does not mutate composer prompt, composer attachments, or chatBlockedReason', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});
      await store.resumeSession('test-session-123', false);
      store.setComposerPrompt('preserved prompt');
      store.setChatBlockedReason('quota-exhausted');
      const attachment = {id: 'tab:1', kind: 'tab' as const, label: 'Tab 1', tabId: 1, url: 'http://a.com'};
      store.addAttachment(attachment);

      const initialComposer = structuredClone(store.getSnapshot().composer);
      const initialBlockedReason = store.getSnapshot().chatBlockedReason;

      await store.submitQuickAction(summarizeAction);

      const snapshot = store.getSnapshot();
      expect(snapshot.composer.prompt).toBe(initialComposer.prompt);
      expect(snapshot.composer.attachments).toEqual(initialComposer.attachments);
      expect(snapshot.chatBlockedReason).toBe(initialBlockedReason);
    });

    it('coalesces double-clicks / concurrent submissions into a single submission', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockImplementation(() => new Promise(res => setTimeout(res, 50)));
      await store.resumeSession('test-session-123', false);

      const firstCall = store.submitQuickAction(summarizeAction);
      const secondCall = store.submitQuickAction(quizAction);

      await Promise.all([firstCall, secondCall]);

      expect(mockHandler.submitPrompt).toHaveBeenCalledOnce();
    });

    it('prevents submission when current session is read-only or has a pending turn', async () => {
      const submitSpy = vi.spyOn(mockHandler, 'submitPrompt');
      submitSpy.mockImplementation(() => new Promise(() => {}));
      await store.resumeSession('test-session-123', false);

      store.setComposerPrompt('hello prompt');
      void store.submitPrompt();
      expect(submitSpy).toHaveBeenCalledTimes(1);

      await store.submitQuickAction(summarizeAction);
      expect(submitSpy).toHaveBeenCalledTimes(1);

      submitSpy.mockResolvedValue({accepted: true});
      vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
        session: {...createSession('read-only-session'), isReadOnly: true},
        replayEvents: [],
      });
      await store.resumeSession('read-only-session', true);
      expect(store.getSnapshot().sessionsById['read-only-session']?.isReadOnly).toBe(true);

      await store.submitQuickAction(summarizeAction);
      expect(submitSpy).toHaveBeenCalledTimes(1);
    });

    it('rolls back pending state and rejects on quick-action remote failure without mutating composer prompt, attachments, or blocked reason when composer is empty', async () => {
      const error = new Error('Remote submit prompt failed');
      vi.spyOn(mockHandler, 'submitPrompt').mockRejectedValue(error);
      await store.resumeSession('test-session-123', false);

      await expect(store.submitQuickAction(summarizeAction)).rejects.toBe(error);

      const snapshot = store.getSnapshot();
      expect(snapshot.turnPendingBySessionId['test-session-123']).toBe(false);
      expect(snapshot.composer.prompt).toBe('');
      expect(snapshot.composer.attachments).toEqual([]);
      expect(snapshot.chatBlockedReason).toBeNull();
    });

    it('rolls back pending state and rejects on quick-action remote failure preserving pre-existing non-empty composer prompt byte-for-byte', async () => {
      const error = new Error('Remote submit prompt failed');
      vi.spyOn(mockHandler, 'submitPrompt').mockRejectedValue(error);
      await store.resumeSession('test-session-123', false);
      store.setComposerPrompt('keep prompt intact');

      await expect(store.submitQuickAction(summarizeAction)).rejects.toBe(error);

      const snapshot = store.getSnapshot();
      expect(snapshot.turnPendingBySessionId['test-session-123']).toBe(false);
      expect(snapshot.composer.prompt).toBe('keep prompt intact');
    });

    it('rolls back pending state and restores typed prompt on ordinary composer remote failure', async () => {
      const error = new Error('Remote submit prompt failed');
      vi.spyOn(mockHandler, 'submitPrompt').mockRejectedValue(error);
      await store.resumeSession('test-session-123', false);
      store.setComposerPrompt('my typed prompt');

      await expect(store.submitPrompt()).rejects.toBe(error);

      const snapshot = store.getSnapshot();
      expect(snapshot.turnPendingBySessionId['test-session-123']).toBe(false);
      expect(snapshot.composer.prompt).toBe('my typed prompt');
    });
  });
});
