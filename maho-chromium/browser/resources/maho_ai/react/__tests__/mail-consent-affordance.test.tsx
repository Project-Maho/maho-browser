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
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {DEFAULT_RUNTIME_CONFIG, type RuntimeConfigInfo} from '../../types.js';
import {RuntimeConfigCard} from '../features/compact/runtime-config-card.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

type StubListener = (...args: never[]) => void;

function createCallbackRoute() {
  const listeners: StubListener[] = [];
  return {
    addListener: (listener: StubListener) => listeners.push(listener),
    removeListener: () => true,
    emit: (...args: never[]) => listeners.forEach(listener => listener(...args)),
  };
}

function createRouterFake(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute() as never;
  router.onConnectionStateChanged = createCallbackRoute() as never;
  router.onSessionUpdated = createCallbackRoute() as never;
  router.onAISettingsChanged = createCallbackRoute() as never;
  router.onAskMahoSessionAccepted = createCallbackRoute() as never;
  router.onRoutineRunStatusChanged = createCallbackRoute() as never;
  (router as {onRuntimeConfigChanged?: unknown}).onRuntimeConfigChanged =
      createCallbackRoute();
  return router;
}

let lastRouter: PageCallbackRouter;

function createStore(config: Partial<RuntimeConfigInfo>): MahoAiStore {
  const handler = {
    getConnectionState: vi.fn(async () => ({state: RuntimeConnectionState.kConnected})),
    getSessionList: vi.fn(async () => ({sessions: [] as SessionInfo[]})),
    getRuntimeConfig: vi.fn(async () => ({
      config: {...DEFAULT_RUNTIME_CONFIG, ...config},
    })),
    setRuntimeConfig: vi.fn(async () => ({accepted: true})),
    $: {close: () => {}},
  } as unknown as PageHandlerRemote;
  lastRouter = createRouterFake();
  return new MahoAiStore(handler, lastRouter);
}

describe('Mail <-> agent consent affordance in the panel', () => {
  let container: HTMLDivElement;
  let root: Root;
  let store: MahoAiStore;

  beforeEach(() => {
    localStorage.clear();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    store?.dispose?.();
    localStorage.clear();
    vi.restoreAllMocks();
  });

  it('exposes a mail-read consent switch reflecting the SSOT value', async () => {
    store = createStore({mailReadAllowed: true} as Partial<RuntimeConfigInfo>);
    await act(async () => {
      await store.refreshRuntimeConfig();
    });
    act(() => root.render(<RuntimeConfigCard store={store} />));

    const control = container.querySelector('[data-mail-read-consent]');
    expect(control).not.toBeNull();
    expect(control?.getAttribute('aria-checked')).toBe('true');
  });

  it('renders mail consent as off when the SSOT denies mail reads', async () => {
    store = createStore({mailReadAllowed: false} as Partial<RuntimeConfigInfo>);
    await act(async () => {
      await store.refreshRuntimeConfig();
    });
    act(() => root.render(<RuntimeConfigCard store={store} />));

    const control = container.querySelector('[data-mail-read-consent]');
    expect(control).not.toBeNull();
    expect(control?.getAttribute('aria-checked')).toBe('false');
  });

  it('adopts an externally changed mail consent without a refetch', () => {
    store = createStore({mailReadAllowed: false} as Partial<RuntimeConfigInfo>);
    const router = lastRouter as unknown as {
      onRuntimeConfigChanged: {emit: (config: RuntimeConfigInfo) => void};
    };

    router.onRuntimeConfigChanged.emit({
      ...DEFAULT_RUNTIME_CONFIG,
      mailReadAllowed: true,
    } as unknown as RuntimeConfigInfo);

    expect(
        (store.getSnapshot().runtimeConfig as {mailReadAllowed?: boolean})
            .mailReadAllowed).toBe(true);
  });
});
