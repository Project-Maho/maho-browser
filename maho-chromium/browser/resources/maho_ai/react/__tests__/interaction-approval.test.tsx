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
import {interactionStateOutcome} from '../../receipt-projection.js';
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
  return {
    container,
    root,
    emitRuntimeEvent: listener,
    mockHandler,
    store,
  };
}

// Mirrors the row-6 mojom InteractionRequestInfo payload the C++ page handler
// projects onto RuntimeEvent.interactionRequest. The generated JS bindings do
// not carry the field until the deferred regeneration, hence the cast.
function interactionEvent(
    sessionId: string,
    sequence: number,
    overrides: {
      kind?: number,
      question?: string,
      options?: Array<{id: string, label: string, description?: string}>,
      artifactRef?: string|null,
      state?: string|null,
      requestId?: string,
    } = {}): RuntimeEvent {
  return {
    sessionId,
    kind: RUNTIME_EVENT_KIND_INTERACTION_REQUEST,
    sequence,
    timestamp: sequence,
    interactionRequest: {
      requestId: overrides.requestId ?? 'interaction-req-7',
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

function clickButton(container: HTMLElement, ariaLabel: string): void {
  const button = container.querySelector<HTMLButtonElement>(
      `button[aria-label="${ariaLabel}"]`);
  if (!button) {
    throw new Error(`Missing button "${ariaLabel}"`);
  }
  button.dispatchEvent(new MouseEvent('click', {bubbles: true}));
}

function setInstruction(container: HTMLElement, text: string): void {
  const input = container.querySelector<HTMLInputElement>(
      'input[aria-label="Instruction for the agent"]');
  if (!input) {
    throw new Error('Missing instruction input');
  }
  const setter =
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
  setter?.call(input, text);
  input.dispatchEvent(new Event('input', {bubbles: true}));
}

// The input change triggers a React state update; route it through act.
function setInstructionInAct(container: HTMLElement, text: string): void {
  act(() => setInstruction(container, text));
}

describe('InteractionApprovalCard — action confirmations', () => {
  let container: HTMLDivElement;
  let root: Root;
  let emitRuntimeEvent: (event: RuntimeEvent) => void;
  let mockHandler: InteractionTestHandler;
  let store: MahoAiStore;

  beforeEach(async () => {
    // The store notifies subscribers through requestAnimationFrame; the
    // synchronous stub keeps renders deterministic.
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    emitRuntimeEvent = harness.emitRuntimeEvent;
    mockHandler = harness.mockHandler;
    store = harness.store;
    await store.bootstrap();
    act(() => root.render(<InteractionApprovalCard store={store} />));
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('renders the effect description with confirm/cancel actions for an interaction request',
      () => {
        act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));

        const card = requireCard(container);
        expect(card.textContent).toContain('Restart the web worker daemon');
        const confirm = container.querySelector<HTMLButtonElement>(
            'button[aria-label="Confirm action"]');
        const deny = container.querySelector<HTMLButtonElement>(
            'button[aria-label="Deny action"]');
        expect(confirm).not.toBeNull();
        expect(deny).not.toBeNull();
        expect(confirm?.disabled).toBe(false);
        expect(deny?.disabled).toBe(false);
      });

  it('dispatches the mojom-shaped RespondToInteraction answer on confirm and releases the card',
      async () => {
        act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));

        await act(async () => {
          clickButton(container, 'Confirm action');
        });

        expect(mockHandler.respondToInteraction).toHaveBeenCalledOnce();
        expect(mockHandler.respondToInteraction).toHaveBeenCalledWith(
            'session-1', 'interaction-req-7', {answerKind: 'confirmed'});
        const card = requireCard(container);
        expect(card.getAttribute('data-state')).toBe('resolved');
        const confirm = container.querySelector<HTMLButtonElement>(
            'button[aria-label="Confirm action"]');
        expect(confirm?.disabled).toBe(true);
      });

  it('dispatches a denied answer on deny', async () => {
    act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));

    await act(async () => {
      clickButton(container, 'Deny action');
    });

    expect(mockHandler.respondToInteraction).toHaveBeenCalledWith(
        'session-1', 'interaction-req-7', {answerKind: 'denied'});
    expect(requireCard(container).getAttribute('data-state')).toBe('denied');
  });

  it('retains the instruction and exposes retry feedback when dispatch fails', async () => {
    act(() => emitRuntimeEvent(interactionEvent('session-1', 1, {kind: 0})));
    setInstructionInAct(container, 'Use staging, not production');
    mockHandler.respondToInteraction.mockRejectedValueOnce(new Error('Transport disconnected'));
    await act(async () => { clickButton(container, 'Send reply'); });
    expect.soft(container.querySelector<HTMLInputElement>('input')?.value).toBe('Use staging, not production');
    expect.soft(requireCard(container).getAttribute('data-state')).toBe('pending');
    expect.soft(container.querySelector('[role="alert"]')).not.toBeNull();
    expect.soft(container.querySelector<HTMLButtonElement>('[aria-label="Send reply"]')?.disabled).toBe(false);
  });

  it('sends a typed instruction as a text answer', async () => {
    act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));

    setInstructionInAct(container, 'Use the staging cluster instead');
    await act(async () => {
      clickButton(container, 'Confirm action');
    });

    expect(mockHandler.respondToInteraction).toHaveBeenCalledWith(
        'session-1', 'interaction-req-7',
        {answerKind: 'text', text: 'Use the staging cluster instead'});
  });

  it('disables the card when the request expired', () => {
    act(() => emitRuntimeEvent(
        interactionEvent('session-1', 1, {state: 'expired'})));

    const card = requireCard(container);
    expect(card.getAttribute('data-state')).toBe('expired');
    const confirm = container.querySelector<HTMLButtonElement>(
        'button[aria-label="Confirm action"]');
    const deny = container.querySelector<HTMLButtonElement>(
        'button[aria-label="Deny action"]');
    expect(confirm?.disabled).toBe(true);
    expect(deny?.disabled).toBe(true);
    const status = card.querySelector('[role="status"]');
    expect(status?.textContent).toContain('Expired');
  });

  it('releases a pending card when a later event reports cancellation', () => {
    act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));
    expect(requireCard(container).getAttribute('data-state')).toBe('pending');

    act(() => emitRuntimeEvent(
        interactionEvent('session-1', 2, {state: 'cancelled'})));

    const card = requireCard(container);
    expect(card.getAttribute('data-state')).toBe('cancelled');
    const confirm = container.querySelector<HTMLButtonElement>(
        'button[aria-label="Confirm action"]');
    expect(confirm?.disabled).toBe(true);
  });

  it('reverts an optimistic answer when the backend rejects the dispatch',
      async () => {
        act(() => emitRuntimeEvent(interactionEvent('session-1', 1)));
        mockHandler.respondToInteraction.mockRejectedValueOnce(
            new Error('already terminal'));

        await act(async () => {
          clickButton(container, 'Confirm action');
        });

        expect(requireCard(container).getAttribute('data-state'))
            .toBe('pending');
        const confirm = container.querySelector<HTMLButtonElement>(
            'button[aria-label="Confirm action"]');
        expect(confirm?.disabled).toBe(false);
      });

  it('renders the review artifact reference when the request carries one',
      () => {
        act(() => emitRuntimeEvent(
            interactionEvent('session-1', 1, {artifactRef: 'draft-42'})));

        const artifact = requireCard(container)
            .querySelector('[aria-label="Review artifact"]');
        expect(artifact?.textContent).toContain('draft-42');
      });
});

describe('InteractionApprovalCard — questions', () => {
  let container: HTMLDivElement;
  let root: Root;
  let emitRuntimeEvent: (event: RuntimeEvent) => void;
  let mockHandler: InteractionTestHandler;
  let store: MahoAiStore;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    emitRuntimeEvent = harness.emitRuntimeEvent;
    mockHandler = harness.mockHandler;
    store = harness.store;
    await store.bootstrap();
    act(() => root.render(<InteractionApprovalCard store={store} />));
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('renders question options and dispatches a selected_option answer',
      async () => {
        act(() => emitRuntimeEvent(interactionEvent('session-1', 1, {
          kind: 0,
          question: 'Which deployment target should we use?',
          options: [
            {id: 'opt_prod', label: 'Production'},
            {id: 'opt_staging', label: 'Staging'},
          ],
        })));

        const card = requireCard(container);
        expect(card.textContent).toContain('Which deployment target should we use?');

        await act(async () => {
          clickButton(container, 'Option: Production');
        });

        expect(mockHandler.respondToInteraction).toHaveBeenCalledWith(
            'session-1', 'interaction-req-7',
            {answerKind: 'selected_option', optionId: 'opt_prod'});
        expect(requireCard(container).getAttribute('data-state'))
            .toBe('resolved');
      });
});

describe('MahoAiStore.respondToInteraction guards', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('returns false without an active session and never dispatches', async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    const harness = createHarness();
    const {mockHandler, store} = harness;

    const accepted = await store.respondToInteraction(
        'interaction-req-7', {answerKind: 'confirmed'});

    expect(accepted).toBe(false);
    expect(mockHandler.respondToInteraction).not.toHaveBeenCalled();
    harness.container.remove();
  });
});

describe('interaction receipt projection', () => {
  it('projects interaction lifecycle states onto receipt outcomes', () => {
    expect(interactionStateOutcome('pending')).toBe('unknown');
    expect(interactionStateOutcome('resolved')).toBe('succeeded');
    expect(interactionStateOutcome('denied')).toBe('denied');
    expect(interactionStateOutcome('expired')).toBe('disconnected');
    expect(interactionStateOutcome('cancelled')).toBe('disconnected');
    expect(interactionStateOutcome(null)).toBe('unknown');
    expect(interactionStateOutcome(undefined)).toBe('unknown');
  });
});
