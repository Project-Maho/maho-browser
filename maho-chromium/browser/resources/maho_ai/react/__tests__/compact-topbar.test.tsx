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

describe('CompactTopbar connection-state indicator', () => {
  let container: HTMLDivElement;
  let root: Root;
  let emitConnectionState: (state: RuntimeConnectionState) => void;
  let store: MahoAiStore;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    globalThis.ResizeObserver = class {
      observe(): void {}
      unobserve(): void {}
      disconnect(): void {}
    } as unknown as typeof ResizeObserver;
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    const harness = createHarness();
    container = harness.container;
    root = harness.root;
    emitConnectionState = harness.emitConnectionState;
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

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    (globalThis as Record<string, unknown>)['ResizeObserver'] = undefined;
    vi.restoreAllMocks();
  });

  it('shows a healthy connection indicator once bootstrapped', () => {
    const badge = container.querySelector('[data-connection-state="Connected"]');
    expect(badge).not.toBeNull();
  });

  it('keeps the transient Connecting handshake out of the visible topbar', () => {
    act(() => {
      emitConnectionState(RuntimeConnectionState.kConnecting);
    });

    const badge = container.querySelector('[data-connection-state="Connecting"]');
    expect(badge).not.toBeNull();
    expect(badge!.classList.contains('sr-only')).toBe(true);
    expect(badge!.querySelector('[aria-hidden="true"]')).toBeNull();
  });

  it('surfaces disconnects visibly instead of rendering them invisible on this surface',
      () => {
        act(() => {
          emitConnectionState(RuntimeConnectionState.kDisconnected);
        });

        const badge = container.querySelector('[data-connection-state="Disconnected"]');
        expect(badge).not.toBeNull();
        // The label must be visually present for a problem state (not hidden
        // behind an sr-only span like the healthy "Connected" label).
        const label = badge!.querySelector('[data-connection-label]');
        expect(label?.textContent).toBe('Disconnected');
        expect(label?.classList.contains('sr-only')).toBe(false);
      });
});

describe('CompactTopbar session context', () => {
  let container: HTMLDivElement;
  let root: Root;
  let store: MahoAiStore;
  const archivedSession: SessionInfo = {
    ...createSession('session-archived'),
    title: 'Archived chat',
    isReadOnly: true,
    updatedAt: 5,
  };
  const liveSession: SessionInfo = {
    ...createSession('session-1'),
    title: 'Live chat',
    updatedAt: 100,
  };

  function renderTopbar(onResumeSession: (sessionId: string) => void): void {
    act(() => {
      root.render(
          <TooltipProvider delayDuration={200}>
            <CompactTopbar
              store={store}
              onClosePanel={() => {}}
              onGetViewMode={() => store.getViewMode()}
              onOpenSettings={() => {}}
              onResumeSession={onResumeSession}
              onSetViewMode={() => {}}
              onStartSession={() => {}}
            />
          </TooltipProvider>);
    });
  }

  describe('with a read-only archived session current', () => {
    const onResumeSession = vi.fn();

    beforeEach(async () => {
      globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
        cb(0);
        return 0;
      }) as typeof requestAnimationFrame;
      globalThis.ResizeObserver = class {
        observe(): void {}
        unobserve(): void {}
        disconnect(): void {}
      } as unknown as typeof ResizeObserver;
      window.HTMLElement.prototype.scrollIntoView = vi.fn();
      const harness = createHarness({
        sessions: [liveSession, archivedSession],
        resumeSessionId: 'session-archived',
      });
      container = harness.container;
      root = harness.root;
      store = harness.store;
      await store.bootstrap();
      // Boot auto-resumes the newest session; the user then opened an
      // archived one from history.
      await store.resumeSession('session-archived');
      onResumeSession.mockClear();
      renderTopbar(onResumeSession);
    });

    afterEach(() => {
      act(() => root.unmount());
      container.remove();
      (globalThis as Record<string, unknown>)['ResizeObserver'] = undefined;
      vi.restoreAllMocks();
    });

    it('labels the read-only state with a badge', () => {
      const badge = container.querySelector('[data-readonly-badge]');
      expect(badge).not.toBeNull();
      expect(badge?.textContent).toContain('Read-only');
    });

    it('offers session history in More and resumes the picked session', () => {
      const trigger = container.querySelector<HTMLButtonElement>(
          'button[aria-label="More actions"]');
      expect(trigger).not.toBeNull();
      act(() => {
        trigger!.dispatchEvent(new KeyboardEvent('keydown', {bubbles: true, key: 'Enter'}));
      });
      const historyTrigger = document.querySelector<HTMLElement>(
          '[data-topbar-history-submenu]');
      expect(historyTrigger).not.toBeNull();
      act(() => {
        historyTrigger!.dispatchEvent(
            new KeyboardEvent('keydown', {bubbles: true, key: 'ArrowRight'}));
      });

      const item = document.querySelector<HTMLButtonElement>(
          '[data-history-session="session-1"]');
      expect(item).not.toBeNull();
      expect(item?.textContent).toContain(getSessionTitle(liveSession));

      act(() => {
        item!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(onResumeSession).toHaveBeenCalledWith('session-1');
    });
  });

  describe('with a live session current', () => {
    beforeEach(async () => {
      globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
        cb(0);
        return 0;
      }) as typeof requestAnimationFrame;
      const harness = createHarness({
        sessions: [liveSession, archivedSession],
        resumeSessionId: 'session-1',
      });
      container = harness.container;
      root = harness.root;
      store = harness.store;
      await store.bootstrap();
      renderTopbar(() => {});
    });

    afterEach(() => {
      act(() => root.unmount());
      container.remove();
      vi.restoreAllMocks();
    });

    it('does not render the read-only badge', () => {
      expect(container.querySelector('[data-readonly-badge]')).toBeNull();
    });
  });
});

describe('CompactTopbar sidebar actions', () => {
  let container: HTMLDivElement;
  let root: Root;
  let store: MahoAiStore;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((cb: FrameRequestCallback) => {
      cb(0);
      return 0;
    }) as typeof requestAnimationFrame;
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    // One current session plus twenty prior sessions. The product correctly
    // excludes the current session from History.
    const sessions = Array.from({length: 21}, (_, index) => ({
      ...createSession(`session-${index + 1}`),
      title: `History item ${index + 1}`,
      updatedAt: 20 - index,
    }));
    const harness = createHarness({sessions, resumeSessionId: 'session-1'});
    container = harness.container;
    root = harness.root;
    store = harness.store;
    await store.bootstrap();
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  function render(callbacks: {
    onClosePanel?: () => void;
    onOpenRoutines?: () => void;
    onOpenSettings?: () => void;
    onResumeSession?: (sessionId: string) => void;
    onStartSession?: () => void;
  } = {}): void {
    act(() => {
      root.render(
          <TooltipProvider delayDuration={200}>
            <CompactTopbar
              store={store}
              onClosePanel={callbacks.onClosePanel ?? (() => {})}
              onGetViewMode={() => store.getViewMode()}
              onOpenRoutines={callbacks.onOpenRoutines}
              onOpenSettings={callbacks.onOpenSettings ?? (() => {})}
              onResumeSession={callbacks.onResumeSession}
              onSetViewMode={() => {}}
              onStartSession={callbacks.onStartSession ?? (() => {})}
            />
          </TooltipProvider>);
    });
  }

  function press(element: Element, key: string): void {
    act(() => {
      element.dispatchEvent(new KeyboardEvent('keydown', {bubbles: true, key}));
    });
  }

  function nextFocus(): Promise<HTMLElement> {
    return new Promise((resolve, reject) => {
      const timeout = window.setTimeout(() => {
        document.removeEventListener('focusin', onFocus);
        reject(new Error('Timed out waiting for focus transition'));
      }, 1000);
      const onFocus = (event: FocusEvent) => {
        window.clearTimeout(timeout);
        document.removeEventListener('focusin', onFocus);
        resolve(event.target as HTMLElement);
      };
      document.addEventListener('focusin', onFocus);
    });
  }

  it('keeps only sidebar-primary controls in the topbar', () => {
    render();

    expect(container.querySelector('button[aria-label="Select profile: Select Profile"]'))
        .not.toBeNull();
    expect(container.querySelector('button[aria-label="Start new session"]')).not.toBeNull();
    expect(container.querySelector('[role="status"]')).not.toBeNull();
    expect(container.querySelector('button[aria-label="More actions"]')).not.toBeNull();
    expect(container.querySelector('button[aria-label="Close panel"]')).not.toBeNull();
    expect(container.querySelector('button[aria-label="Session history"]')).toBeNull();
    expect(container.querySelector('button[aria-label="Open Routines"]')).toBeNull();
    expect(container.querySelector('button[aria-label="Open settings"]')).toBeNull();
    expect(container.querySelector('button[aria-label="View mode"]')).toBeNull();
  });

  it('moves from History to Open Routines with ArrowDown before entering history', async () => {
    render();
    const trigger = container.querySelector<HTMLButtonElement>(
        'button[aria-label="More actions"]')!;

    act(() => trigger.focus());
    press(trigger, 'Enter');
    const historyTrigger = document.querySelector<HTMLElement>(
        '[data-topbar-history-submenu]')!;
    expect(document.activeElement).toBe(historyTrigger);

    const focused: {current: HTMLElement|null} = {current: null};
    await act(async () => {
      const focusTransition = nextFocus();
      historyTrigger.dispatchEvent(
          new KeyboardEvent('keydown', {bubbles: true, key: 'ArrowDown'}));
      focused.current = await focusTransition;
    });
    expect(focused.current?.getAttribute('data-topbar-action'))
        .toBe('Open Routines');
  });

  it('opens More with Enter, enters History with ArrowRight, resumes a session, and returns focus on Escape', () => {
    const onResumeSession = vi.fn();
    render({onResumeSession});
    const trigger = container.querySelector<HTMLButtonElement>(
        'button[aria-label="More actions"]')!;

    act(() => trigger.focus());
    press(trigger, 'Enter');
    const historyTrigger = document.querySelector<HTMLElement>(
        '[data-topbar-history-submenu]');
    expect(historyTrigger).not.toBeNull();
    expect(document.activeElement).toBe(historyTrigger);

    press(historyTrigger!, 'Escape');
    expect(trigger.getAttribute('aria-expanded')).toBe('false');

    act(() => trigger.focus());
    press(trigger, 'Enter');
    const reopenedHistoryTrigger = document.querySelector<HTMLElement>(
        '[data-topbar-history-submenu]')!;
    press(reopenedHistoryTrigger, 'ArrowRight');
    const historyItems = document.querySelectorAll<HTMLElement>(
        '[data-history-session]');
    expect(historyItems).toHaveLength(20);
    const historyItem = document.querySelector<HTMLElement>(
        '[data-history-session="session-21"]');
    expect(historyItem).not.toBeNull();
    act(() => historyItem!.dispatchEvent(new MouseEvent('click', {bubbles: true})));
    expect(onResumeSession).toHaveBeenCalledWith('session-21');
  });

  it('renders a disabled empty item when no prior session history exists', () => {
    act(() => {
      root.unmount();
    });
    container.remove();
    const emptyHarness = createHarness({sessions: []});
    container = emptyHarness.container;
    root = emptyHarness.root;
    store = emptyHarness.store;
    render();

    const trigger = container.querySelector<HTMLButtonElement>(
        'button[aria-label="More actions"]')!;
    press(trigger, 'Enter');
    const historyTrigger = document.querySelector<HTMLElement>(
        '[data-topbar-history-submenu]')!;
    press(historyTrigger, 'ArrowRight');
    const emptyItem = document.querySelector<HTMLElement>('[data-disabled]');
    expect(emptyItem?.textContent).toContain('No session history');
  });

  it('dispatches routines, settings, new-session, and close through their existing callbacks', () => {
    const onClosePanel = vi.fn();
    const onOpenRoutines = vi.fn();
    const onOpenSettings = vi.fn();
    const onStartSession = vi.fn();
    render({onClosePanel, onOpenRoutines, onOpenSettings, onStartSession});

    act(() => container.querySelector<HTMLButtonElement>(
        'button[aria-label="Start new session"]')!.click());
    expect(onStartSession).toHaveBeenCalledOnce();

    const trigger = container.querySelector<HTMLButtonElement>(
        'button[aria-label="More actions"]')!;
    for (const [label, callback] of [
      ['Open Routines', onOpenRoutines],
      ['Open settings', onOpenSettings],
    ] as const) {
      press(trigger, 'Enter');
      const item = document.querySelector<HTMLElement>(`[data-topbar-action="${label}"]`);
      expect(item).not.toBeNull();
      act(() => item!.dispatchEvent(new MouseEvent('click', {bubbles: true})));
      expect(callback).toHaveBeenCalledOnce();
    }

    act(() => container.querySelector<HTMLButtonElement>(
        'button[aria-label="Close panel"]')!.click());
    expect(onClosePanel).toHaveBeenCalledOnce();
  });
});
