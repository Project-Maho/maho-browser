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
import {SlashEditors} from '../features/compact/slash-editors.js';

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

type ExtensibilityTestHandler = PageHandlerRemote & {
  createAiProfile: ReturnType<typeof vi.fn>;
  registerMcpServer: ReturnType<typeof vi.fn>;
  registerCliTool: ReturnType<typeof vi.fn>;
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
  mockHandler.getMcpServers = vi.fn().mockResolvedValue({serversJson: '[]'});
  mockHandler.getCliTools = vi.fn().mockResolvedValue({toolsJson: '[]'});
  mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
    info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
  });
  mockHandler.createAiProfile = vi.fn().mockResolvedValue({profileId: 'p-new'});
  mockHandler.registerMcpServer =
      vi.fn().mockResolvedValue({success: true});
  mockHandler.registerCliTool = vi.fn().mockResolvedValue({success: true});

  return {container, root, mockHandler, store: new MahoAiStore(mockHandler, mockRouter)};
}

function setInputValue(container: HTMLElement, index: number, value: string): void {
  const inputs =
      container.querySelectorAll<HTMLInputElement>('input:not([type="submit"])');
  const input = inputs[index]!;
  const setter =
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
  act(() => {
    setter?.call(input, value);
    input.dispatchEvent(new Event('input', {bubbles: true}));
  });
}

function submitForm(container: HTMLElement): void {
  const form = container.querySelector('form');
  if (!form) {
    throw new Error('Missing editor form');
  }
  form.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));
}

describe('slash editor forms surface failures', () => {
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

  it('toasts and keeps the form open when creating an agent profile throws',
      async () => {
        const toastErrorSpy = vi.spyOn(toast, 'error');
        mockHandler.createAiProfile.mockRejectedValue(
            new Error('handler gone'));
        act(() => store.setActiveSlashEditor('agent-new'));
        act(() => root.render(<SlashEditors store={store} />));

        setInputValue(container, 0, 'Code Reviewer');
        submitForm(container);
        await act(async () => {});

        expect(toastErrorSpy).toHaveBeenCalled();
        expect(container.querySelector('form')).not.toBeNull();
      });

  it('toasts and keeps the form open when an MCP server registration is refused',
      async () => {
        const toastErrorSpy = vi.spyOn(toast, 'error');
        mockHandler.registerMcpServer.mockRejectedValue(
            new Error('handler gone'));
        act(() => store.setActiveSlashEditor('mcp-add'));
        act(() => root.render(<SlashEditors store={store} />));

        setInputValue(container, 0, 'filesystem');
        submitForm(container);
        await act(async () => {});

        expect(toastErrorSpy).toHaveBeenCalled();
        expect(container.querySelector('form')).not.toBeNull();
      });

  it('toasts and keeps the form open when a CLI tool registration throws',
      async () => {
        const toastErrorSpy = vi.spyOn(toast, 'error');
        mockHandler.registerCliTool.mockRejectedValue(new Error('handler gone'));
        act(() => store.setActiveSlashEditor('cli-add'));
        act(() => root.render(<SlashEditors store={store} />));

        setInputValue(container, 0, 'git_diff');
        setInputValue(container, 1, 'Retrieve git diff');
        setInputValue(container, 2, 'git diff HEAD');
        submitForm(container);
        await act(async () => {});

        expect(toastErrorSpy).toHaveBeenCalled();
        expect(container.querySelector('form')).not.toBeNull();
      });
});
