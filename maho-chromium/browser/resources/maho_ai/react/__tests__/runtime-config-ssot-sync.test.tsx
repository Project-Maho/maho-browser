import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RoutineRunStatus,
  type RuntimeEvent,
  type SessionInfo,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {
  DEFAULT_RUNTIME_CONFIG,
  type RuntimeConfigInfo,
} from '../../types.js';

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

// Records the registered listener so a test can fire the event the browser
// would push, instead of asserting that addListener was merely called.
function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): number;
  removeListener(id: number): boolean;
  emit(...args: TArgs): void;
  listenerCount(): number;
} {
  const listeners: Array<Callback<TArgs>> = [];
  return {
    addListener: (listener: Callback<TArgs>) => listeners.push(listener),
    removeListener: () => true,
    emit: (...args: TArgs) => listeners.forEach(listener => listener(...args)),
    listenerCount: () => listeners.length,
  };
}

type RouterFake = PageCallbackRouter & {
  onRuntimeConfigChanged: ReturnType<typeof createCallbackRoute<[RuntimeConfigInfo]>>;
};

function createPageCallbackRouterFake(): RouterFake {
  const router = new PageCallbackRouter() as RouterFake;
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[never]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  router.onRoutineRunStatusChanged = createCallbackRoute<[RoutineRunStatus]>();
  router.onRuntimeConfigChanged = createCallbackRoute<[RuntimeConfigInfo]>();
  return router;
}

function createHandler(): PageHandlerRemote {
  const handler = {
    getConnectionState: vi.fn(async () => ({state: RuntimeConnectionState.kConnected})),
    getSessionList: vi.fn(async () => ({sessions: []})),
    getRuntimeConfig: vi.fn(async () => ({config: {...DEFAULT_RUNTIME_CONFIG}})),
    setRuntimeConfig: vi.fn(async () => ({accepted: true})),
    $: {close: () => {}},
  } as unknown as PageHandlerRemote;
  return handler;
}

describe('Permission SSOT — Settings -> panel live sync', () => {
  let router: RouterFake;
  let store: MahoAiStore;

  beforeEach(() => {
    localStorage.clear();
    router = createPageCallbackRouterFake();
    store = new MahoAiStore(createHandler(), router);
  });

  afterEach(() => {
    store.dispose?.();
    localStorage.clear();
    vi.restoreAllMocks();
  });

  it('subscribes to the runtime-config change event', () => {
    expect(router.onRuntimeConfigChanged.listenerCount()).toBeGreaterThan(0);
  });

  it('adopts an externally changed permission tier without a refetch', () => {
    // full_access is deliberately NOT the default tier (guard), so this
    // assertion cannot pass by inheriting the initial state.
    expect(store.getSnapshot().runtimeConfig.permissionTier).not.toBe('full_access');

    router.onRuntimeConfigChanged.emit({
      permissionTier: 'full_access',
      finalConfirm: false,
      proactiveMode: true,
    } as unknown as RuntimeConfigInfo);

    const config = store.getSnapshot().runtimeConfig;
    expect(config.permissionTier).toBe('full_access');
    expect(config.finalConfirm).toBe(false);
    expect(config.proactiveMode).toBe(true);
  });

  it('fails closed to guard when the pushed tier is not a known value', () => {
    router.onRuntimeConfigChanged.emit({
      permissionTier: 'root',
      finalConfirm: true,
      proactiveMode: false,
    } as unknown as RuntimeConfigInfo);

    expect(store.getSnapshot().runtimeConfig.permissionTier).toBe('guard');
  });
});
