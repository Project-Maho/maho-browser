import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {toast} from 'sonner';

import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RoutineRunStatus,
  type RuntimeEvent,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {SUGGESTED_TASKS} from '../../views/suggestions.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

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
  router.onAISettingsChanged = createCallbackRoute<[never]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  router.onRoutineRunStatusChanged = createCallbackRoute<[RoutineRunStatus]>();
  return router;
}

function createSession(sessionId: string): SessionInfo {
  return {
    sessionId,
    title: sessionId,
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

type SuggestedTaskTestHandler = PageHandlerRemote & {
  getRuntimeConfig: ReturnType<typeof vi.fn>;
  setRuntimeConfig: ReturnType<typeof vi.fn>;
};

function createHarness(): {
  container: HTMLDivElement;
  root: Root;
  mockHandler: SuggestedTaskTestHandler;
  store: MahoAiStore;
} {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);

  const mockHandler = new PageHandlerRemote() as SuggestedTaskTestHandler;
  const mockRouter = createPageCallbackRouterFake();

  vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
    state: RuntimeConnectionState.kConnected,
    activeAdapterName: 'OpenCode',
  });
  vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
    sessions: [createSession('session-1')],
  });
  vi.spyOn(mockHandler, 'resumeSession').mockImplementation(
      async (sessionId: string) => ({
        session: createSession(sessionId),
        replayEvents: [],
      }));
  vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
    events: [],
    totalCount: 0,
  });
  mockHandler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
  mockHandler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
  mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
    info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
  });
  mockHandler.getRuntimeConfig = vi.fn().mockResolvedValue({
    config: {permissionTier: 'guard', finalConfirm: true, proactiveMode: false},
  });
  mockHandler.setRuntimeConfig = vi.fn().mockResolvedValue({accepted: true});
  vi.spyOn(mockHandler, 'startSession').mockImplementation(
      async (_initialPrompt: string|null, _mode) =>
          ({session: createSession('session-suggested')}));
  vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});

  return {container, root, mockHandler, store: new MahoAiStore(mockHandler, mockRouter)};
}

describe('startSuggestedTask — flag rejection feedback and rollback', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: SuggestedTaskTestHandler;
  let store: MahoAiStore;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    mockHandler = harness.mockHandler;
    store = harness.store;
    await store.bootstrap();
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('rolls back the created session and reports failure when the broker refuses the guard flags',
      async () => {
        mockHandler.setRuntimeConfig.mockResolvedValue({accepted: false});

        const started =
            await store.startSuggestedTask(SUGGESTED_TASKS[0]!);

        // The dispatch must report failure instead of a silent no-op.
        expect(started).toBe(false);
        // The task was not started...
        expect(mockHandler.submitPrompt).not.toHaveBeenCalled();
        // ...and the orphan empty session was rolled back.
        expect(store.getSnapshot().currentSessionId).toBe('session-1');
        expect(store.getSnapshot().sessionsById['session-suggested'])
            .toBeUndefined();
        expect(store.getSnapshot()
            .turnPendingBySessionId['session-suggested']).toBeFalsy();
      });

  it('returns true on the happy path', async () => {
    const started = await store.startSuggestedTask(SUGGESTED_TASKS[0]!);
    expect(started).toBe(true);
    expect(store.getSnapshot().currentSessionId).toBe('session-suggested');
  });
});
