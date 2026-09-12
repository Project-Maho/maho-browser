import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  ViewMode,
  type RoutineRunStatus,
  type RuntimeEvent,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {CompactTopbar} from '../features/compact/compact-topbar.js';
import {TooltipProvider} from '@ui/tooltip';
import {getSessionTitle, isSessionReadOnly} from '../../types.js';

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

function createHarness(sessionOverrides: {
  readonly sessions?: SessionInfo[];
  readonly resumeSessionId?: string;
} = {}): {
  container: HTMLDivElement;
  root: Root;
  emitConnectionState: (state: RuntimeConnectionState) => void;
  store: MahoAiStore;
} {
  const sessions = sessionOverrides.sessions ?? [createSession('session-1')];
  const resumeSessionId = sessionOverrides.resumeSessionId ?? 'session-1';
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);

  const mockHandler = new PageHandlerRemote();
  const mockRouter = createPageCallbackRouterFake();

  vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
    state: RuntimeConnectionState.kConnected,
    activeAdapterName: 'OpenCode',
  });
  vi.spyOn(mockHandler, 'getViewMode').mockResolvedValue({
    mode: ViewMode.kSidebar,
  });
  vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
    sessions,
  });
  vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
    session: sessions.find(s => s.sessionId === resumeSessionId) ?? sessions[0]!,
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

  const store = new MahoAiStore(mockHandler, mockRouter);
  const connectionListener = vi.mocked(mockRouter.onConnectionStateChanged.addListener)
      .mock.calls[0]?.[0] as ((state: RuntimeConnectionState) => void)|undefined;
  if (!connectionListener) {
    throw new Error('Connection state listener was not registered');
  }
  return {
    container,
    root,
    emitConnectionState: connectionListener,
    store,
  };
}

/**
 * Sidebar-only surface: narrow width is the primary design width. Measured via
 * CDP on the real bundle at 320/380/420px, 33 elements rendered past the
 * viewport because a grid ancestor lacking `min-width: 0` resolved a 514.9px
 * column inside a 362px track. jsdom has no layout engine, so the measured
 * geometry stays browser-proven (CDP on the real bundle); these tests assert
 * the functional contract the narrow surface must keep instead.
 */
describe('sidebar narrow-width overflow contract', () => {
  let container: HTMLDivElement;
  let root: Root;
  let store: MahoAiStore;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => { cb(0); return 0; }) as typeof requestAnimationFrame;
    globalThis.ResizeObserver = class {
      observe(): void {}
      unobserve(): void {}
      disconnect(): void {}
    } as unknown as typeof ResizeObserver;
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    store = harness.store;
    await store.bootstrap();
    act(() => {
      root.render(
          <TooltipProvider delayDuration={200}>
            <CompactTopbar
              store={store}
              onClosePanel={() => {}}
              onGetViewMode={() => store.getViewMode()}
              onOpenSettings={() => {}}
              onSetViewMode={() => {}}
              onStartSession={() => {}}
            />
          </TooltipProvider>);
    });
  });

  afterEach(() => { act(() => root.unmount()); container.remove(); vi.restoreAllMocks(); });

  it('keeps essential controls mounted and secondary actions reachable through the More menu',
      () => {
        // The measured overflow geometry stays browser-proven (CDP on the
        // real bundle). jsdom asserts the functional contract instead: the
        // primary controls stay mounted and every action — including those
        // relocated into the More menu — remains keyboard-reachable.
        expect(container.querySelector('button[aria-label="Start new session"]'))
            .not.toBeNull();
        expect(container.querySelector('[role="status"]')).not.toBeNull();
        expect(container.querySelector('button[aria-label="Close panel"]'))
            .not.toBeNull();

        const trigger = container.querySelector<HTMLButtonElement>(
            'button[aria-label="More actions"]');
        expect(trigger).not.toBeNull();
        act(() => {
          trigger!.focus();
        });
        act(() => {
          trigger!.dispatchEvent(
              new KeyboardEvent('keydown', {bubbles: true, key: 'Enter'}));
        });

        const historyTrigger = document.querySelector<HTMLElement>(
            '[data-topbar-history-submenu]');
        expect(historyTrigger).not.toBeNull();
        expect(document.activeElement).toBe(historyTrigger);
        expect(trigger!.getAttribute('aria-expanded')).toBe('true');
        expect(document.querySelector('[data-topbar-action="Open Routines"]'))
            .not.toBeNull();
        expect(document.querySelector('[data-topbar-action="Open settings"]'))
            .not.toBeNull();
      });
});
