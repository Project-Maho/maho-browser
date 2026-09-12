import {beforeEach, describe, expect, it, vi} from 'vitest';

import {
  PageCallbackRouter,
  PageHandlerRemote,
  type RuntimeEvent,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: (...args: TArgs) => void): void;
} {
  return {addListener: vi.fn()};
}

function createRouterFake(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>() as never;
  router.onConnectionStateChanged = createCallbackRoute<[never]>() as never;
  router.onSessionUpdated = createCallbackRoute<[never]>() as never;
  router.onAISettingsChanged = createCallbackRoute<[never]>() as never;
  router.onAskMahoSessionAccepted = createCallbackRoute<[never, never]>() as never;
  return router;
}

function seedSession(store: MahoAiStore, isReadOnly: boolean) {
  (store as unknown as {
    patch: (fn: (state: Record<string, unknown>) => void) => void,
  }).patch(state => {
    state.currentSessionId = 'session-1';
    state.sessionsById = {'session-1': {sessionId: 'session-1', isReadOnly}};
  });
}

describe('respondToApproval failure reporting (C4 deny path)', () => {
  let handler: PageHandlerRemote;
  let store: MahoAiStore;

  beforeEach(() => {
    handler = {
      respondToApproval: vi.fn().mockResolvedValue(undefined),
    } as unknown as PageHandlerRemote;
    store = new MahoAiStore(handler, createRouterFake());
  });

  it('reports true when the deny response actually reaches the page handler', async () => {
    seedSession(store, false);

    await expect(store.respondToApproval('approval-1', false)).resolves.toBe(true);
    expect(handler.respondToApproval)
        .toHaveBeenCalledWith('session-1', 'approval-1', false);
  });

  it('reports false instead of silent success when the session is read-only', async () => {
    seedSession(store, true);

    await expect(store.respondToApproval('approval-1', false)).resolves.toBe(false);
    expect(handler.respondToApproval).not.toHaveBeenCalled();
  });

  it('reports false instead of an unhandled rejection when the transport throws', async () => {
    seedSession(store, false);
    vi.mocked(handler.respondToApproval).mockRejectedValue(new Error('pipe closed'));

    // Must RESOLVE false, not reject: the sole caller is `void store.respondToApproval(...)`,
    // so a rejection would surface as an unhandled promise rejection.
    await expect(store.respondToApproval('approval-1', false)).resolves.toBe(false);
  });
});
