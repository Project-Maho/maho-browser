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
import {TooltipProvider} from '@ui/tooltip';

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

// Boots a store against a mocked page handler so bootstrap succeeds and the
// chat surface (thread + composer) actually renders.
function createStore(): MahoAiStore {
  const mockHandler = new PageHandlerRemote();
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

  return new MahoAiStore(mockHandler, mockRouter);
}

// applyTheme/watchAutoTheme query the color-scheme media query at mount.
function installMatchMediaStub(): void {
  window.matchMedia = ((query: string) => ({
    matches: false,
    media: query,
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
    addListener: vi.fn(),
    removeListener: vi.fn(),
    dispatchEvent: vi.fn(() => true),
  })) as unknown as typeof window.matchMedia;
}

// app.tsx runs import-time side effects (it requires #app and self-mounts a
// store with live mojo bindings), so the component must be loaded after the
// test DOM stubs are in place, mirroring app-boot.test.ts.
async function loadMahoAiApp(): Promise<typeof import('../app.js').MahoAiApp> {
  const module = await import('../app.js');
  return module.MahoAiApp;
}

// A store whose handler has no working transport: bootstrap fails and the
// store records the boot error.
function createFailingStore(): MahoAiStore {
  const mockHandler = new PageHandlerRemote();
  const mockRouter = createPageCallbackRouterFake();
  vi.spyOn(mockHandler, 'getConnectionState').mockRejectedValue(
      new Error('Mojo pipe closed'));
  return new MahoAiStore(mockHandler, mockRouter);
}

describe('MahoAiApp surface exclusivity', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    // The store notifies subscribers through requestAnimationFrame; the
    // synchronous stub keeps renders deterministic (see interaction test).
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    installMatchMediaStub();
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    // app.tsx import requires the #app root to exist.
    const appRoot = document.createElement('div');
    appRoot.id = 'app';
    document.body.appendChild(appRoot);
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    document.getElementById('app')?.remove();
    document.body.innerHTML = '';
    (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = undefined;
  });

  it('does not keep the chat thread and composer in the tree while the routines surface is open',
      async () => {
        const store = createStore();
        const MahoAiApp = await loadMahoAiApp();
        await store.bootstrap();
        act(() => {
          root.render(
              <TooltipProvider delayDuration={200}>
                <MahoAiApp store={store} />
              </TooltipProvider>);
        });

        // Sanity: the chat surface is mounted with its composer. Located by
        // the structural `data-composer` hook so composer placeholder copy
        // stays free to change.
        const chatComposer = () =>
            container.querySelector<HTMLTextAreaElement>(
                '[data-composer] textarea');
        expect(chatComposer()).not.toBeNull();

        // Routines moved behind the topbar More menu in the restructured
        // topbar. The chat surface keeps its own visible topbar next to the
        // hidden routines-surface topbar, so drive the visible one.
        const moreTrigger = [
          ...container.querySelectorAll<HTMLButtonElement>(
              'button[aria-label="More actions"]'),
        ].find(trigger => !trigger.closest('[hidden]'));
        expect(moreTrigger).not.toBeNull();
        act(() => {
          moreTrigger!.focus();
        });
        act(() => {
          moreTrigger!.dispatchEvent(
              new KeyboardEvent('keydown', {bubbles: true, key: 'Enter'}));
        });
        const routinesItem = document.querySelector<HTMLElement>(
            '[data-topbar-action="Open Routines"]');
        expect(routinesItem).not.toBeNull();
        await act(async () => {
          routinesItem!.dispatchEvent(
              new MouseEvent('click', {bubbles: true}));
        });

        expect(container.querySelector('section[aria-label="Routines"]'))
            .not.toBeNull();
        // The defect: `className="contents"` defeats the `hidden` attribute,
        // so the chat surface (thread + composer) stayed in the tree below
        // the routines workspace. It must be gone entirely — real
        // conditional render, not merely hidden.
        expect(chatComposer()).toBeNull();
        expect(container.querySelector('[data-composer-dock]')).toBeNull();
        expect(
            container.querySelector('main[aria-label="Empty conversation"]'))
            .toBeNull();
      },
      // This is the only test that boots the whole app (app.js import +
      // store bootstrap + Radix menu), so it costs ~2.8s alone and ~5.8s
      // under full-suite CPU contention. The 5s default made it pass by
      // timing luck; the budget is raised, the assertions are unchanged.
      20000);
});

describe('MahoAiApp boot error recovery', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    installMatchMediaStub();
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    const appRoot = document.createElement('div');
    appRoot.id = 'app';
    document.body.appendChild(appRoot);
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    document.getElementById('app')?.remove();
    document.body.innerHTML = '';
    (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = undefined;
  });

  it('offers a Retry action with the raw text behind a Details disclosure',
      async () => {
        const store = createFailingStore();
        const MahoAiApp = await loadMahoAiApp();
        await store.bootstrap().catch(() => {});
        expect(store.getSnapshot().bootError).not.toBeNull();
        act(() => {
          root.render(
              <TooltipProvider delayDuration={200}>
                <MahoAiApp store={store} />
              </TooltipProvider>);
        });

        // Mount re-runs bootstrap internally (clearing, then re-setting the
        // boot error); let that settle before asserting.
        for (let i = 0; i < 5; i++) {
          await act(async () => {
            await new Promise(resolve => setTimeout(resolve, 0));
          });
        }

        const retry = container.querySelector<HTMLButtonElement>(
            '[data-boot-retry]');
        expect(retry).not.toBeNull();

        // The raw error text is not dumped openly; it sits in a disclosure.
        const details = container.querySelector('details[data-boot-details]');
        expect(details).not.toBeNull();
        expect(details?.querySelector('pre')?.textContent?.length ?? 0)
            .toBeGreaterThan(0);

        // Retry re-runs the bootstrap against the (still failing) handler.
        await act(async () => {
          retry!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });
        expect(store.getSnapshot().bootError).not.toBeNull();
      });
});
