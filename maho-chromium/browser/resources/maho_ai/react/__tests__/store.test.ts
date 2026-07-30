import {describe, it, expect, vi, beforeEach} from 'vitest';
import {MahoAiStore} from '../../store.js';
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
  addListener(listener: Callback<TArgs>): void;
} {
  return {addListener: vi.fn()};
}

function createPageCallbackRouterFake(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
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
          status: 1, // SessionStatus.kActive
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
        status: 1,
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
    const firstSubmission = createDeferred<void>();
    const submitPrompt = vi.spyOn(mockHandler, 'submitPrompt')
                             .mockReturnValue(firstSubmission.promise);
    await store.resumeSession('test-session-123', false);
    store.setComposerPrompt('first request');
    const firstPromise = store.submitPrompt();
    store.setComposerPrompt('next draft');

    // When
    const overlappingPromise = store.submitPrompt();
    firstSubmission.resolve(undefined);
    await Promise.all([firstPromise, overlappingPromise]);

    // Then
    expect.soft(submitPrompt).toHaveBeenCalledOnce();
    expect.soft(store.getSnapshot().composer.prompt).toBe('next draft');
  });

  it('keeps a session blocked after Mojo resolves until a terminal runtime event', async () => {
    // Given
    const submitPrompt = vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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

  it('persists AI provider, model, and reasoning selection through the page handler', async () => {
    // Given
    const setAISelection = vi.fn<(providerId: string, modelId: string,
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
    Object.assign(mockHandler, {getAISettings, setAISelection});
    const selection: AISelectionRequest = {
      modelId: 'gpt-4.1',
      providerId: 'openai',
      reasoningEffort: AI_REASONING_EFFORT.kHigh,
    };

    // When
    const accepted = await store.setAISelection(selection);

    // Then
    expect(accepted).toBe(true);
    expect(setAISelection).toHaveBeenCalledWith(
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

  it('rolls back composer state when Mojo rejects a submission', async () => {
    // Given
    const submission = createDeferred<void>();
    vi.spyOn(mockHandler, 'submitPrompt').mockReturnValue(submission.promise);
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
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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

    it('creates a new session and submits quick action when no current session exists', async () => {
      const newSession = createSession('new-session-456');
      vi.spyOn(mockHandler, 'startSession').mockResolvedValue({session: newSession});
      vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({session: newSession, replayEvents: []});
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();

      await store.submitQuickAction(quizAction);

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
    });

    it('normal submitPrompt passes kFreeform intent', async () => {
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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
      vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue();
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

      submitSpy.mockResolvedValue();
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
