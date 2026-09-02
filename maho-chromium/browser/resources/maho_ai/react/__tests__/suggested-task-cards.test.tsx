import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

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
import {SuggestedTaskCards} from '../features/compact/suggested-task-cards.js';
import {
  SUGGESTED_TASK_RUNTIME_CONFIG,
  SUGGESTED_TASKS,
  getSuggestedTaskCards,
} from '../../views/suggestions.js';

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
  router.onConnectionStateChanged =
      createCallbackRoute<[RuntimeConnectionState]>();
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

// The generated mojom bindings gain Get/SetRuntimeConfig only after the
// deferred regeneration; tests attach vi.fn stand-ins via this intersection.
type SuggestedTaskTestHandler = PageHandlerRemote & {
  getRuntimeConfig: ReturnType<typeof vi.fn>;
  setRuntimeConfig: ReturnType<typeof vi.fn>;
  startSession: ReturnType<typeof vi.fn>;
  submitPrompt: ReturnType<typeof vi.fn>;
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

  const mockHandler =
      new PageHandlerRemote() as SuggestedTaskTestHandler;
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
  mockHandler.getRuntimeConfig =
      vi.fn().mockResolvedValue({
        config: {permissionTier: 'guard', finalConfirm: true, proactiveMode: false},
      });
  mockHandler.setRuntimeConfig = vi.fn().mockResolvedValue({accepted: true});
  vi.spyOn(mockHandler, 'startSession').mockImplementation(
      async (_initialPrompt: string|null, _mode) =>
          ({session: createSession('session-suggested')}));
  vi.spyOn(mockHandler, 'submitPrompt').mockResolvedValue({accepted: true});

  const store = new MahoAiStore(mockHandler, mockRouter);
  return {container, root, mockHandler, store};
}

function requireSuggestedTaskButtons(container: HTMLElement):
    HTMLButtonElement[] {
  return Array.from(container.querySelectorAll<HTMLButtonElement>(
      '[data-suggested-task]'));
}

describe('suggested task card presentation helpers', () => {
  it('mirrors the backend suggestion catalog (title + prompt pairs)', () => {
    expect(SUGGESTED_TASKS.length).toBeGreaterThan(0);
    for (const task of SUGGESTED_TASKS) {
      expect(task.title.trim().length).toBeGreaterThan(0);
      expect(task.prompt.trim().length).toBeGreaterThan(0);
    }
  });

  it('returns the full catalog when suggestion settings are enabled', () => {
    expect(getSuggestedTaskCards({enabled: true})).toEqual(SUGGESTED_TASKS);
  });

  it('returns zero cards when suggestions are disabled', () => {
    // SuggestionSettings::disabled parity — no cards may surface.
    expect(getSuggestedTaskCards({enabled: false})).toEqual([]);
  });

  it('carries the guard/final-confirm/proactive flag set for new sessions', () => {
    expect(SUGGESTED_TASK_RUNTIME_CONFIG).toEqual({
      permissionTier: 'guard',
      finalConfirm: true,
      proactiveMode: true,
    });
  });
});

describe('SuggestedTaskCards rendering and dispatch', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: SuggestedTaskTestHandler;
  let store: MahoAiStore;

  beforeEach(() => {
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    mockHandler = harness.mockHandler;
    store = harness.store;
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('renders idle suggested cards with title and prompt from the catalog',
      async () => {
        await store.bootstrap();
        act(() => root.render(<SuggestedTaskCards store={store} />));

        const buttons = requireSuggestedTaskButtons(container);
        expect(buttons).toHaveLength(SUGGESTED_TASKS.length);
        buttons.forEach((button, index) => {
          const task = SUGGESTED_TASKS[index]!;
          expect(button.dataset.suggestedTask).toBe(task.title);
          expect(button.textContent).toContain(task.title);
          expect(button.textContent).toContain(task.prompt);
        });
      });

  it('creates a session with the 3-flag runtime config on card click',
      async () => {
        await store.bootstrap();
        act(() => root.render(<SuggestedTaskCards store={store} />));

        const firstCard = requireSuggestedTaskButtons(container)[0]!;
        await act(async () => {
          firstCard.dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        // Card click creates a session...
        expect(mockHandler.startSession).toHaveBeenCalledOnce();
        // ...applies the 3-flag payload via SetRuntimeConfig...
        expect(mockHandler.setRuntimeConfig).toHaveBeenCalledOnce();
        expect(mockHandler.setRuntimeConfig).toHaveBeenCalledWith(
            SUGGESTED_TASK_RUNTIME_CONFIG);
        // ...to the newly created session (flags land after creation)...
        expect(mockHandler.setRuntimeConfig.mock.invocationCallOrder[0]!)
            .toBeGreaterThan(mockHandler.startSession.mock.invocationCallOrder[0]!);
        // ...and starts the suggested task with the card's prompt.
        expect(mockHandler.submitPrompt).toHaveBeenCalledOnce();
        expect(mockHandler.submitPrompt.mock.calls[0]![0])
            .toBe('session-suggested');
        expect(mockHandler.submitPrompt.mock.calls[0]![1])
            .toBe(SUGGESTED_TASKS[0]!.prompt);
        expect(store.getSnapshot().currentSessionId).toBe('session-suggested');
      });

  it('renders zero cards when suggestion settings are disabled', async () => {
    await store.bootstrap();
    store.setSuggestionsEnabled(false);
    act(() => root.render(<SuggestedTaskCards store={store} />));

    expect(requireSuggestedTaskButtons(container)).toHaveLength(0);
    expect(container.textContent).not.toContain('Suggested tasks');
  });
});
