import {describe, expect, it, vi} from 'vitest';

import {
  type AISettingsInfo,
  PageCallbackRouter,
  PageHandlerRemote,
  type RuntimeEvent,
  RuntimeEventKind,
  RuntimeConnectionState,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import type {ComposerAttachment} from '../../types.js';

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): void;
} {
  return {addListener: vi.fn()};
}

function createRouter(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  return router;
}

function createRuntimeEvent(sessionId: string): RuntimeEvent {
  return {
    sessionId,
    kind: RuntimeEventKind.kAssistantToken,
    sequence: 1,
    timestamp: 1,
    text: 'accepted output',
  };
}

function createSession(sessionId: string, title: string): SessionInfo {
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

async function createFixture(): Promise<{
  readonly handler: PageHandlerRemote;
  readonly newSession: SessionInfo;
  readonly oldSession: SessionInfo;
  readonly router: PageCallbackRouter;
  readonly store: MahoAiStore;
}> {
  const handler = new PageHandlerRemote();
  const router = createRouter();
  const oldSession = createSession('existing-session', 'Existing session');
  const newSession = createSession('new-session', 'Ask Maho session');
  vi.spyOn(handler, 'startSession').mockResolvedValue({session: newSession});
  vi.spyOn(handler, 'submitPrompt').mockResolvedValue({accepted: true});
  vi.spyOn(handler, 'resumeSession').mockResolvedValue({
    session: oldSession,
    replayEvents: [],
  });
  const store = new MahoAiStore(handler, router);
  await store.resumeSession(oldSession.sessionId, false);
  return {handler, newSession, oldSession, router, store};
}

describe('MahoAiStore.ingestAskMahoSessionAccepted', () => {
  it('projects the accepted session even when another session is current', async () => {
    // Given
    const {handler, newSession, store} = await createFixture();

    // When
    await store.ingestAskMahoSessionAccepted('request-new-session', newSession);

    // Then
    expect(handler.startSession).not.toHaveBeenCalled();
    expect(store.getSnapshot().currentSessionId).toBe(newSession.sessionId);
    expect(store.getSnapshot().sessionsById[newSession.sessionId]).toEqual(newSession);
  });

  it('does not call submitPrompt IPC on the pageHandler', async () => {
    // Given
    const {handler, newSession, store} = await createFixture();

    // When
    await store.ingestAskMahoSessionAccepted('request-direct-submit', newSession);

    // Then
    expect(handler.submitPrompt).not.toHaveBeenCalled();
  });

  it('preserves staged composer state', async () => {
    // Given
    const {newSession, store} = await createFixture();
    const stagedAttachment: ComposerAttachment = {
      id: 'file:staged.txt',
      kind: 'file',
      label: 'staged.txt',
      dataUrl: 'data:text/plain;base64,c3RhZ2Vk',
      mimeType: 'text/plain',
      size: 6,
    };
    store.setDeveloperMode(true);
    store.setComposerPrompt('staged composer prompt');
    store.addAttachment(stagedAttachment);

    // When
    await store.ingestAskMahoSessionAccepted('request-isolated', newSession);

    // Then
    expect(store.getSnapshot().composer.prompt).toBe('staged composer prompt');
    expect(store.getSnapshot().composer.attachments).toEqual([stagedAttachment]);
  });

  it('does not project twice for a duplicate request id', async () => {
    // Given
    const {store, newSession} = await createFixture();

    // When
    await store.ingestAskMahoSessionAccepted('request-duplicate', newSession);
    const firstSnapshot = store.getSnapshot();
    await store.ingestAskMahoSessionAccepted('request-duplicate', newSession);
    const secondSnapshot = store.getSnapshot();

    // Then
    expect(firstSnapshot).toBe(secondSnapshot);
  });

  it('converges without duplication when the event arrives before acceptance', async () => {
    // Given
    const {store, newSession, router} = await createFixture();
    const runtimeListener = vi.mocked(router.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    expect(runtimeListener).toBeTypeOf('function');

    // When
    runtimeListener?.(createRuntimeEvent(newSession.sessionId));
    await store.ingestAskMahoSessionAccepted('request-event-first', newSession);
    await store.ingestAskMahoSessionAccepted('request-event-first', newSession);

    // Then
    expect(store.getSnapshot().eventsBySessionId[newSession.sessionId]).toHaveLength(1);
    expect(warn).toHaveBeenCalledWith(
        '[maho-ai] runtime event deferred for unknown session', newSession.sessionId);
    warn.mockRestore();
  });

  it('converges without duplication when acceptance arrives before the event', async () => {
    // Given
    const handler = new PageHandlerRemote();
    const router = createRouter();
    const store = new MahoAiStore(handler, router);
    const newSession = createSession('accepted-first-session', 'Accepted first');
    const runtimeListener = vi.mocked(router.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    expect(runtimeListener).toBeTypeOf('function');

    // When
    await store.ingestAskMahoSessionAccepted('request-accepted-first', newSession);
    runtimeListener?.(createRuntimeEvent(newSession.sessionId));
    await store.ingestAskMahoSessionAccepted('request-accepted-first', newSession);

    // Then
    expect(store.getSnapshot().eventsBySessionId[newSession.sessionId]).toHaveLength(1);
  });

  it('increments the composer focus request synchronously', async () => {
    // Given
    const {newSession, store} = await createFixture();
    const initialFocusRequest = store.getSnapshot().composer.focusRequest;

    // When
    await store.ingestAskMahoSessionAccepted('request-focus-success', newSession);

    // Then
    expect(store.getSnapshot().composer.focusRequest).toBe(initialFocusRequest + 1);
  });

  it('preserves the previous session record while selecting the new session', async () => {
    // Given
    const {oldSession, newSession, store} = await createFixture();
    const previousRecord = {...oldSession};

    // When
    await store.ingestAskMahoSessionAccepted('request-preserve-session', newSession);

    // Then
    expect(store.getSnapshot().sessionsById[oldSession.sessionId]).toEqual(previousRecord);
    expect(store.getSnapshot().sessionOrder).toContain(oldSession.sessionId);
    expect(store.getSnapshot().currentSessionId).toBe('new-session');
  });

});
