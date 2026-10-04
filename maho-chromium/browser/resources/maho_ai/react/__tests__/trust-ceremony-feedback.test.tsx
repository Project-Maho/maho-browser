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
import {TrustCeremony} from '../features/trust-ceremony/trust-ceremony.js';

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

const WORKSPACE = {
  id: 'ws-1',
  name: 'Default Workspace',
  profileId: 'p-1',
  spaceId: null,
  workspaceRoot: null,
  createdAt: '',
  updatedAt: '',
};

const UNTRUSTED_SERVER = {
  id: 'mcp-1',
  workspaceId: 'ws-1',
  name: 'filesystem',
  transport: 0,
  command: 'npx -y @modelcontextprotocol/server-filesystem',
  url: null,
  authKeychainId: null,
  trusted: false,
  trustedTools: null,
  timeoutMs: 10000,
  outputCapBytes: 1048576,
  createdAt: '',
  updatedAt: '',
};

type ExtensibilityTestHandler = PageHandlerRemote & {
  approveMcpServerTrust: ReturnType<typeof vi.fn>;
  removeMcpServer: ReturnType<typeof vi.fn>;
  getActiveAiWorkspace: ReturnType<typeof vi.fn>;
  getMcpServers: ReturnType<typeof vi.fn>;
  getCliTools: ReturnType<typeof vi.fn>;
};

function createHarness(): {
  container: HTMLDivElement;
  root: Root;
  mockHandler: ExtensibilityTestHandler;
  store: MahoAiStore;
} {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);

  const mockHandler = new PageHandlerRemote() as ExtensibilityTestHandler;
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
  mockHandler.getAiWorkspaces =
      vi.fn().mockResolvedValue({workspacesJson: JSON.stringify([WORKSPACE])});
  mockHandler.getActiveAiWorkspace =
      vi.fn().mockResolvedValue({workspaceJson: JSON.stringify(WORKSPACE)});
  mockHandler.getMcpServers = vi.fn().mockResolvedValue({
    serversJson: JSON.stringify([UNTRUSTED_SERVER]),
  });
  mockHandler.getCliTools = vi.fn().mockResolvedValue({toolsJson: '[]'});
  mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
    info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
  });
  mockHandler.approveMcpServerTrust =
      vi.fn().mockResolvedValue({success: true});
  mockHandler.removeMcpServer = vi.fn().mockResolvedValue({success: true});

  return {container, root, mockHandler, store: new MahoAiStore(mockHandler, mockRouter)};
}

describe('MCP trust ceremony failure feedback', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: ExtensibilityTestHandler;
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
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('reports a thrown trust dispatch instead of swallowing it', async () => {
    const toastErrorSpy = vi.spyOn(toast, 'error');
    mockHandler.approveMcpServerTrust.mockRejectedValue(
        new Error('broker offline'));

    await store.approveMcpServerTrust('ws-1', 'filesystem', []);

    expect(toastErrorSpy).toHaveBeenCalled();
    expect(await store.approveMcpServerTrust('ws-1', 'filesystem', []))
        .toBe(false);
  });

  it('keeps the ceremony card open with a visible error when trusting throws',
      async () => {
        const toastErrorSpy = vi.spyOn(toast, 'error');
        mockHandler.approveMcpServerTrust.mockRejectedValue(
            new Error('broker offline'));

        act(() => root.render(<TrustCeremony store={store} />));

        const trustButton = Array.from(container.querySelectorAll('button'))
            .find(button => button.textContent?.includes('Trust All Tools'));
        expect(trustButton).not.toBeUndefined();

        await act(async () => {
          trustButton!.dispatchEvent(
              new MouseEvent('click', {bubbles: true}));
        });

        expect(toastErrorSpy).toHaveBeenCalled();
        expect(container.querySelector('h4')?.textContent)
            .toContain('Trust MCP Server?');
      });

  it('does not clear the customize tool list when the trust dispatch fails',
      async () => {
        mockHandler.approveMcpServerTrust.mockRejectedValue(
            new Error('broker offline'));

        act(() => root.render(<TrustCeremony store={store} />));

        const customize = Array.from(container.querySelectorAll('button'))
            .find(button => button.textContent?.includes('Customize'));
        act(() => {
          customize!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        const input = container.querySelector<HTMLInputElement>('input[type="text"]');
        expect(input).not.toBeNull();
        const setter =
            Object.getOwnPropertyDescriptor(
                HTMLInputElement.prototype, 'value')?.set;
        act(() => {
          setter?.call(input, 'read_file');
          input!.dispatchEvent(new Event('input', {bubbles: true}));
        });

        const approveSelected = Array.from(container.querySelectorAll('button'))
            .find(button => button.textContent?.includes('Approve Selected'));
        await act(async () => {
          approveSelected!.dispatchEvent(
              new MouseEvent('click', {bubbles: true}));
        });

        const inputAfter =
            container.querySelector<HTMLInputElement>('input[type="text"]');
        expect(inputAfter?.value).toBe('read_file');
      });
});
