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

type ConfigTestHandler = PageHandlerRemote & {
  getRuntimeConfig: ReturnType<typeof vi.fn>;
  setRuntimeConfig: ReturnType<typeof vi.fn>;
};

function createHarness(): {
  container: HTMLDivElement;
  root: Root;
  mockHandler: ConfigTestHandler;
  store: MahoAiStore;
} {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);

  const mockHandler = new PageHandlerRemote() as ConfigTestHandler;
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
  mockHandler.getRuntimeConfig = vi.fn().mockResolvedValue({
    config: {permissionTier: 'guard', finalConfirm: true, proactiveMode: false, mailReadAllowed: false},
  });
  mockHandler.setRuntimeConfig = vi.fn().mockResolvedValue({accepted: true});

  return {container, root, mockHandler, store: new MahoAiStore(mockHandler, mockRouter)};
}

function requireRadios(container: HTMLElement): HTMLButtonElement[] {
  const group = container.querySelector('[role="radiogroup"]');
  if (!group) {
    throw new Error('Missing permission tier radiogroup');
  }
  return Array.from(
      group.querySelectorAll<HTMLButtonElement>('[role="radio"]'));
}

function requireRadio(container: HTMLElement, label: string): HTMLButtonElement {
  const radio = requireRadios(container)
      .find(candidate => candidate.textContent?.trim() === label);
  if (!radio) {
    throw new Error(`Missing tier radio "${label}"`);
  }
  return radio;
}

describe('RuntimeConfigCard — keyboard contract and failure feedback', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: ConfigTestHandler;
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
    act(() => root.render(<RuntimeConfigCard store={store} />));
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('implements the radiogroup roving-tabindex and arrow-key contract', () => {
    const radios = requireRadios(container);
    // Guard is selected: it is the only tab stop.
    expect(radios[1]!.getAttribute('tabindex')).toBe('0');
    expect(radios[0]!.getAttribute('tabindex')).toBe('-1');
    expect(radios[2]!.getAttribute('tabindex')).toBe('-1');

    act(() => {
      radios[1]!.dispatchEvent(new KeyboardEvent('keydown', {
        key: 'ArrowRight',
        bubbles: true,
      }));
    });

    // Selection and focus move to Full access.
    expect(document.activeElement).toBe(radios[2]);
    expect(radios[2]!.getAttribute('aria-checked')).toBe('true');
    expect(mockHandler.setRuntimeConfig).toHaveBeenCalledWith({
      permissionTier: 'full_access',
      finalConfirm: true,
      proactiveMode: false,
      mailReadAllowed: false,
    });
  });

  it('toasts and surfaces an inline status when the broker refuses a tier change',
      async () => {
        mockHandler.setRuntimeConfig.mockResolvedValue({accepted: false});
        const toastErrorSpy = vi.spyOn(toast, 'error');

        await act(async () => {
          requireRadio(container, 'Full access')
              .dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        expect(toastErrorSpy).toHaveBeenCalled();
        const status = container.querySelector('[data-config-status]');
        expect(status).not.toBeNull();
        expect(status?.textContent).toContain('not applied');
        // The optimistic tier change was rolled back by the store.
        expect(requireRadio(container, 'Guard').getAttribute('aria-checked'))
            .toBe('true');
      });

  it('clears the inline status after an accepted change', async () => {
    const toastErrorSpy = vi.spyOn(toast, 'error');
    await act(async () => {
      requireRadio(container, 'Full access')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(toastErrorSpy).not.toHaveBeenCalled();
    expect(container.querySelector('[data-config-status]')).toBeNull();
  });
});
