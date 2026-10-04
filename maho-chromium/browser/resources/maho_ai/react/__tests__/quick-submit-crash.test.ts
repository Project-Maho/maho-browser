import {describe, expect, it, vi} from 'vitest';

import {
  type AISettingsInfo,
  InteractionMode,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RuntimeEvent,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {
  type ComposerAttachment,
  getSubmitContextAttachments,
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

function createSession(sessionId: string): SessionInfo {
  return {
    sessionId,
    title: 'First session',
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

function createDeferred<T>(): {
  readonly promise: Promise<T>;
  readonly resolve: (value: T) => void;
} {
  const resolve = vi.fn<(value: T) => void>();
  const promise = new Promise<T>(complete => resolve.mockImplementation(complete));
  return {promise, resolve};
}

function createDraftPersistence() {
  return {
    clear: vi.fn(async () => undefined),
    flush: vi.fn(async () => undefined),
    load: vi.fn(async () => null),
    schedule: vi.fn(),
  };
}

function createFixture(): {
  readonly handler: PageHandlerRemote;
  readonly session: SessionInfo;
  readonly sessionCreation: ReturnType<typeof createDeferred<{session: SessionInfo}>>;
  readonly store: MahoAiStore;
} {
  const handler = new PageHandlerRemote();
  const session = createSession('first-session');
  const sessionCreation = createDeferred<{session: SessionInfo}>();
  vi.spyOn(handler, 'startSession').mockReturnValue(sessionCreation.promise);
  vi.spyOn(handler, 'resumeSession').mockResolvedValue({session, replayEvents: []});
  vi.spyOn(handler, 'submitPrompt').mockResolvedValue({accepted: true});
  const store = new MahoAiStore(handler, createPageCallbackRouterFake());
  return {handler, session, sessionCreation, store};
}

describe('MahoAiStore quick first-session submission', () => {
  it('submits the immutable composer request captured before session creation', async () => {
    // Given
    const {handler, session, sessionCreation, store} = createFixture();
    const firstAttachment: ComposerAttachment = {
      id: 'tab:41',
      kind: 'tab',
      label: 'First research tab',
      tabId: 41,
      url: 'https://first.example/',
    };
    const laterAttachment: ComposerAttachment = {
      id: 'tab:42',
      kind: 'tab',
      label: 'Later research tab',
      tabId: 42,
      url: 'https://later.example/',
    };
    store.setComposerPrompt('first request');
    store.addAttachment(firstAttachment);
    const capturedPrompt = store.getSnapshot().composer.prompt.trim();
    const capturedAttachments = getSubmitContextAttachments(
        [...store.getSnapshot().composer.attachments]);
    const submission = store.startSessionThenSubmit();
    store.setComposerPrompt('later draft');
    store.removeAttachment(firstAttachment.id);
    store.addAttachment(laterAttachment);

    // When
    sessionCreation.resolve({session});
    await submission;

    // Then
    expect.soft(handler.submitPrompt).toHaveBeenCalledOnce();
    expect.soft(handler.submitPrompt).toHaveBeenCalledWith(
        session.sessionId,
        capturedPrompt,
        false,
        InteractionMode.kAssistant,
        capturedAttachments,
        0);
    expect.soft(store.getSnapshot().composer.prompt).toBe('later draft');
    expect.soft(store.getSnapshot().composer.attachments).toEqual([laterAttachment]);
  });

  it('keeps new_task until accepted persistence, then promotes the scope', async () => {
    // Given
    const {handler, session, sessionCreation, store} = createFixture();
    const persistence = createDraftPersistence();
    const accepted = createDeferred<{accepted: boolean}>();
    vi.mocked(handler.submitPrompt).mockReturnValue(accepted.promise);
    store.setComposerDraftPersistenceForTesting(persistence);
    store.setComposerPrompt('persist before promotion');

    // When
    const submission = store.startSessionThenSubmit();
    sessionCreation.resolve({session});
    await Promise.resolve();
    await Promise.resolve();

    // Then
    expect(persistence.clear).not.toHaveBeenCalled();

    // When
    accepted.resolve({accepted: true});
    await submission;
    store.setComposerPrompt('replacement after acceptance');

    // Then
    expect(persistence.clear).toHaveBeenCalledWith({kind: 'new_task'});
    expect(persistence.schedule).toHaveBeenLastCalledWith({
      kind: 'conversation',
      conversationId: session.sessionId,
    }, 'replacement after acceptance');
  });

  it('coalesces rapid first-session submissions into one session and one Mojo submit', async () => {
    // Given
    const {handler, session, sessionCreation, store} = createFixture();
    store.setComposerPrompt('submit once');

    // When
    const firstSubmission = store.startSessionThenSubmit();
    const overlappingSubmission = store.startSessionThenSubmit();
    sessionCreation.resolve({session});
    await Promise.all([firstSubmission, overlappingSubmission]);

    // Then
    expect(handler.startSession).toHaveBeenCalledOnce();
    expect(handler.startSession).toHaveBeenCalledWith(null, InteractionMode.kAssistant);
    expect(handler.submitPrompt).toHaveBeenCalledOnce();
  });
});
