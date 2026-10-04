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
import {
  DEFAULT_RUNTIME_CONFIG,
  buildRuntimeConfigDispatch,
  normalizePermissionTier,
  type RuntimeConfigInfo,
} from '../../types.js';
import {RuntimeConfigCard} from '../features/compact/runtime-config-card.js';

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

interface HarnessOptions {
  readonly supported: boolean;
  readonly config?: RuntimeConfigInfo;
}

// The generated mojom bindings gain Get/SetRuntimeConfig only after the
// deferred regeneration; tests attach vi.fn stand-ins via this intersection.
type RuntimeConfigTestHandler = PageHandlerRemote & {
  getRuntimeConfig: ReturnType<typeof vi.fn>;
  setRuntimeConfig: ReturnType<typeof vi.fn>;
};

function createHarness({supported, config}: HarnessOptions): {
  container: HTMLDivElement;
  root: Root;
  mockHandler: RuntimeConfigTestHandler;
  store: MahoAiStore;
} {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);

  const mockHandler = new PageHandlerRemote() as RuntimeConfigTestHandler;
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

  if (supported) {
    mockHandler.getRuntimeConfig = vi.fn().mockResolvedValue({
      config: config ? {...config} : {permissionTier: 'guard', finalConfirm: true, proactiveMode: false, mailReadAllowed: false},
    });
    mockHandler.setRuntimeConfig =
        vi.fn().mockResolvedValue({accepted: true});
  }

  const store = new MahoAiStore(mockHandler, mockRouter);
  return {container, root, mockHandler, store};
}

function requireRoleRadioGroup(container: HTMLElement): HTMLElement {
  const group = container.querySelector('[role="radiogroup"]');
  if (!group) {
    throw new Error('Missing permission tier radiogroup');
  }
  return group as HTMLElement;
}

function requireRadios(container: HTMLElement): HTMLButtonElement[] {
  return Array.from(requireRoleRadioGroup(container)
      .querySelectorAll<HTMLButtonElement>('[role="radio"]'));
}

function requireRadio(container: HTMLElement, label: string): HTMLButtonElement {
  const radio = requireRadios(container)
      .find(candidate => candidate.textContent?.trim() === label);
  if (radio) {
    return radio;
  }
  throw new Error(`Missing tier radio with label "${label}"`);
}

function requireSwitch(container: HTMLElement, label: string): HTMLButtonElement {
  const button = Array.from(
      container.querySelectorAll<HTMLButtonElement>('[role="switch"]'))
      .find(candidate => candidate.textContent?.includes(label));
  if (button) {
    return button;
  }
  throw new Error(`Missing switch with label "${label}"`);
}

describe('runtime config presentation helpers', () => {
  it('defaults mirror the broker fail-closed values', () => {
    expect(DEFAULT_RUNTIME_CONFIG).toEqual({
      permissionTier: 'guard',
      finalConfirm: true,
      proactiveMode: false,
      mailReadAllowed: false,
    });
  });

  it('normalizes unknown tiers fail-closed to guard', () => {
    expect(normalizePermissionTier('read_only')).toBe('read_only');
    expect(normalizePermissionTier('guard')).toBe('guard');
    expect(normalizePermissionTier('full_access')).toBe('full_access');
    expect(normalizePermissionTier('yolo')).toBe('guard');
    expect(normalizePermissionTier(null)).toBe('guard');
    expect(normalizePermissionTier(undefined)).toBe('guard');
  });

  it('builds the mojom-shaped SetRuntimeConfig dispatch object', () => {
    const dispatch = buildRuntimeConfigDispatch({
      permissionTier: 'full_access',
      finalConfirm: false,
      proactiveMode: true,
    });
    expect(Object.keys(dispatch).sort()).toEqual(
        ['finalConfirm', 'mailReadAllowed', 'permissionTier', 'proactiveMode']);
    expect(dispatch).toEqual({
      permissionTier: 'full_access',
      finalConfirm: false,
      proactiveMode: true,
      mailReadAllowed: false,
    });
  });
});

describe('RuntimeConfigCard rendering and dispatch', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: RuntimeConfigTestHandler;
  let store: MahoAiStore;

  beforeEach(() => {
    const harness = createHarness({supported: true});
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

  it('renders three controls with the current runtime config values', async () => {
    await store.bootstrap();

    act(() => root.render(<RuntimeConfigCard store={store} />));

    const radios = requireRadios(container);
    expect(radios).toHaveLength(3);
    expect(requireRadio(container, 'Read-only').getAttribute('aria-checked'))
        .toBe('false');
    expect(requireRadio(container, 'Guard').getAttribute('aria-checked'))
        .toBe('true');
    expect(requireRadio(container, 'Full access').getAttribute('aria-checked'))
        .toBe('false');
    expect(requireSwitch(container, 'Final confirmation')
        .getAttribute('aria-checked')).toBe('true');
    expect(requireSwitch(container, 'Proactive mode')
        .getAttribute('aria-checked')).toBe('false');
  });

  it('dispatches the mojom-shaped SetRuntimeConfig call when the tier changes',
      async () => {
        await store.bootstrap();
        act(() => root.render(<RuntimeConfigCard store={store} />));

        await act(async () => {
          requireRadio(container, 'Full access')
              .dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        expect(mockHandler.setRuntimeConfig).toHaveBeenCalledOnce();
        expect(mockHandler.setRuntimeConfig).toHaveBeenCalledWith({
          permissionTier: 'full_access',
          finalConfirm: true,
          proactiveMode: false,
          mailReadAllowed: false,
        });
      });

  it('dispatches the mojom-shaped SetRuntimeConfig call when a flag toggles',
      async () => {
        await store.bootstrap();
        act(() => root.render(<RuntimeConfigCard store={store} />));

        await act(async () => {
          requireSwitch(container, 'Proactive mode')
              .dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });
        await act(async () => {
          requireSwitch(container, 'Final confirmation')
              .dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        expect(mockHandler.setRuntimeConfig).toHaveBeenCalledTimes(2);
        expect(mockHandler.setRuntimeConfig).toHaveBeenNthCalledWith(1, {
          permissionTier: 'guard',
          finalConfirm: true,
          proactiveMode: true,
          mailReadAllowed: false,
        });
        expect(mockHandler.setRuntimeConfig).toHaveBeenNthCalledWith(2, {
          permissionTier: 'guard',
          finalConfirm: false,
          proactiveMode: true,
          mailReadAllowed: false,
        });
      });

  it('reverts optimistic state when the backend rejects the change', async () => {
    mockHandler.setRuntimeConfig.mockResolvedValue({accepted: false});
    await store.bootstrap();
    act(() => root.render(<RuntimeConfigCard store={store} />));

    await act(async () => {
      requireRadio(container, 'Full access')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(store.getSnapshot().runtimeConfig).toEqual({
      permissionTier: 'guard',
      finalConfirm: true,
      proactiveMode: false,
      mailReadAllowed: false,
    });
  });
});

describe('RuntimeConfigCard on unsupported page handlers', () => {
  let container: HTMLDivElement;
  let root: Root;
  let store: MahoAiStore;

  beforeEach(() => {
    const harness = createHarness({supported: false});
    container = harness.container;
    root = harness.root;
    store = harness.store;
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('renders nothing when the handler lacks runtime config methods', async () => {
    await store.bootstrap();
    act(() => root.render(<RuntimeConfigCard store={store} />));

    expect(container.querySelector('[role="radiogroup"]')).toBeNull();
    expect(container.querySelectorAll('[role="switch"]')).toHaveLength(0);
  });
});
