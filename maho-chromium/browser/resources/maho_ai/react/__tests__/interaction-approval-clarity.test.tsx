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
import {RUNTIME_EVENT_KIND_INTERACTION_REQUEST} from '../../types.js';
import {InteractionApprovalCard} from '../features/compact/interaction-approval-card.js';

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

type InteractionTestHandler = PageHandlerRemote & {
  respondToInteraction: ReturnType<typeof vi.fn>;
};

function createHarness(): {
  container: HTMLDivElement;
  root: Root;
  emitRuntimeEvent: (event: RuntimeEvent) => void;
  mockHandler: InteractionTestHandler;
  store: MahoAiStore;
} {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);

  const mockHandler = new PageHandlerRemote() as InteractionTestHandler;
  const mockRouter = createPageCallbackRouterFake();

  vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
    state: RuntimeConnectionState.kConnected,
    activeAdapterName: 'OpenCode',
  });
  vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
    sessions: [createSession('session-1')],
  });
  vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
    session: createSession('session-1'),
    replayEvents: [],
  });
  vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
    events: [],
    totalCount: 0,
  });
  mockHandler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
  mockHandler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
  mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
    info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
  });
  mockHandler.respondToInteraction = vi.fn().mockResolvedValue(undefined);

  const store = new MahoAiStore(mockHandler, mockRouter);
  const listener = vi.mocked(mockRouter.onRuntimeEvent.addListener)
      .mock.calls[0]?.[0] as ((event: RuntimeEvent) => void)|undefined;
  if (!listener) {
    throw new Error('Runtime event listener was not registered');
  }
  return {container, root, emitRuntimeEvent: listener, mockHandler, store};
}

function interactionEvent(
    sessionId: string,
    sequence: number,
    overrides: {
      kind?: number,
      question?: string,
      options?: Array<{id: string, label: string, description?: string}>,
      artifactRef?: string|null,
      state?: string|null,
    } = {}): RuntimeEvent {
  return {
    sessionId,
    kind: RUNTIME_EVENT_KIND_INTERACTION_REQUEST,
    sequence,
    timestamp: sequence,
    interactionRequest: {
      requestId: 'interaction-req-7',
      kind: overrides.kind ?? 1,
      question: overrides.question ?? 'Restart the web worker daemon',
      options: overrides.options ?? [],
      artifactRef: overrides.artifactRef ?? null,
      state: overrides.state ?? 'pending',
    },
  } as unknown as RuntimeEvent;
}

function requireCard(container: HTMLElement): HTMLElement {
  const card = container.querySelector<HTMLElement>(
      'section[aria-label="Agent approval request"]');
  if (!card) {
    throw new Error('Missing agent approval card');
  }
  return card;
}

describe('InteractionApprovalCard — explaining what is approved', () => {
  let container: HTMLDivElement;
  let root: Root;
  let emitRuntimeEvent: (event: RuntimeEvent) => void;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    emitRuntimeEvent = harness.emitRuntimeEvent;
    await harness.store.bootstrap();
    act(() => root.render(<InteractionApprovalCard store={harness.store} />));
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('summarizes a JSON artifact ref into human-readable fields with the raw blob behind a disclosure',
      () => {
        act(() => emitRuntimeEvent(interactionEvent('session-1', 1, {
          artifactRef: JSON.stringify({
            path: '/Users/me/site/config/deploy.json',
            action: 'overwrite',
            size_bytes: 48213,
          }),
        })));

        const card = requireCard(container);
        const summary = card.querySelector('dl[aria-label="Artifact summary"]');
        expect(summary).not.toBeNull();
        expect(summary?.textContent).toContain('Action');
        expect(summary?.textContent).toContain('overwrite');
        // summarizePath: only the tail of the path is shown, not a raw dump.
        expect(summary?.textContent).toContain('deploy.json');
        // The raw text stays reachable but is tucked into a disclosure.
        const details = card.querySelector('details');
        expect(details).not.toBeNull();
        expect(details?.textContent).toContain('deploy.json');
      });

  it('states the consequence of confirming above the actions', () => {
    act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));

    const card = requireCard(container);
    expect(card.querySelector('[data-consequence]')?.textContent)
        .toContain('Confirming lets the agent');
  });

  it('shows option descriptions as visible text, not hover-only titles', () => {
    act(() => emitRuntimeEvent(interactionEvent('session-1', 1, {
      kind: 0,
      question: 'Which deployment target should we use?',
      options: [
        {id: 'opt_prod', label: 'Production', description: 'Serves live traffic to customers'},
        {id: 'opt_staging', label: 'Staging', description: 'Internal pre-release environment'},
      ],
    })));

    const card = requireCard(container);
    const prodDescription =
        card.querySelector<HTMLElement>('[data-option-description="opt_prod"]');
    expect(prodDescription).not.toBeNull();
    expect(prodDescription?.textContent).toContain('Serves live traffic');
    const prodButton =
        card.querySelector<HTMLButtonElement>('button[aria-label="Option: Production"]');
    expect(prodButton?.getAttribute('title')).toBeNull();
  });
});
