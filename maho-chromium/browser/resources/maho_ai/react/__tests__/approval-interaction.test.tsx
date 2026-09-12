import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  ApprovalPolicy,
  ApprovalSensitivity,
  ApprovalState,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEventKind,
  ToolCallStatus,
  type RuntimeEvent,

} from '../../maho_ai.mojom-webui.js';
import {collectConversationItems} from '../../views/conversation_thread.js';
import {ConversationSystemItem} from '../features/compact/conversation-system-item.js';
import {MahoAiStore} from '../../store.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): void;
} {
  return {addListener: vi.fn()};
}

function createPageCallbackRouterFake(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[never]>();
  router.onAISettingsChanged = createCallbackRoute<[never]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[never, never]>();
  return router;
}

function createApprovalRequestEvent(
    approvalId = 'approval-test-1',
    sessionId = 'session-1'): RuntimeEvent {
  return {
    kind: RuntimeEventKind.kApprovalRequest,
    sequence: 1,
    timestamp: 1000,
    sessionId,
    text: 'Execute bash command',
    approvalRequest: {
      approvalId,
      approvalPolicy: ApprovalPolicy.kPrompt,
      description: 'The runtime requests permission to execute a shell script.',
      pageDerivedJustification: false,
      relatedToolCall: {
        callId: 'call-1',
        toolName: 'bash',
        argumentsJson: '{"command":"ls -la"}',
        status: ToolCallStatus.kPending,
      },
      sensitivity: ApprovalSensitivity.kSensitive,
      state: ApprovalState.kPending,
    },
  };
}

function requireButton(
    container: HTMLElement,
    label: string): HTMLButtonElement {
  const button = Array.from(container.querySelectorAll('button'))
      .find(candidate => candidate.textContent?.trim() === label);
  if (button) {
    return button;
  }
  throw new Error(`Missing button with label "${label}"`);
}

describe('Approval interaction in rendered DOM', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: PageHandlerRemote;
  let mockRouter: PageCallbackRouter;
  let store: MahoAiStore;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);

    mockHandler = new PageHandlerRemote();
    mockRouter = createPageCallbackRouterFake();

    vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
      state: RuntimeConnectionState.kConnected,
      activeAdapterName: 'OpenCode',
    });
    vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
      sessions: [
        {
          sessionId: 'session-1',
          adapterName: 'OpenCode',
          isActive: true,
          isReadOnly: false,
          status: 1,
          createdAt: Date.now() / 1000,
          updatedAt: Date.now() / 1000,
          title: 'Test Session',
          summary: '',
          eventCount: 1,
          toolCallCount: 1,
          lastRuntimeState: 'idle',
          runtimeSessionId: 'session-1',
        },
      ],
    });
    vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
      session: {
        sessionId: 'session-1',
        adapterName: 'OpenCode',
        isActive: true,
        isReadOnly: false,
        status: 1,
        createdAt: Date.now() / 1000,
        updatedAt: Date.now() / 1000,
        title: 'Test Session',
        summary: '',
        eventCount: 1,
        toolCallCount: 1,
        lastRuntimeState: 'idle',
        runtimeSessionId: 'session-1',
      },
      replayEvents: [],
    });
    vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
      events: [],
      totalCount: 0,
    });
    vi.spyOn(mockHandler, 'respondToApproval').mockResolvedValue(undefined);
    mockHandler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
    mockHandler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
    mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
      info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
    });

    store = new MahoAiStore(mockHandler, mockRouter);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('renders approval prompt and dispatches true (Allow once) to store.respondToApproval and PageHandler', async () => {
    await store.bootstrap();

    const event = createApprovalRequestEvent('approval-123', 'session-1');
    const items = collectConversationItems([{sessionId: 'session-1', event}]);
    const item = items[0];
    expect(item?.approval).toBeDefined();

    const respondToApprovalSpy = vi.spyOn(store, 'respondToApproval');

    act(() => root.render(
        <ConversationSystemItem
          item={item!}
          onOpenSettings={vi.fn()}
          onRespondToApproval={(approvalId, approved) => {
            void store.respondToApproval(approvalId, approved);
          }}
          readOnly={false}
        />));

    const allowOnceButton = requireButton(container, 'Allow once');
    expect(allowOnceButton).toBeDefined();
    expect(allowOnceButton.disabled).toBe(false);
    expect(Array.from(container.querySelectorAll('button'))
        .some(button => button.textContent?.trim() === 'Accept')).toBe(false);

    await act(async () => {
      allowOnceButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(respondToApprovalSpy).toHaveBeenCalledOnce();
    expect(respondToApprovalSpy).toHaveBeenCalledWith('approval-123', true);
    expect(mockHandler.respondToApproval).toHaveBeenCalledOnce();
    expect(mockHandler.respondToApproval).toHaveBeenCalledWith('session-1', 'approval-123', true);
  });

  it('renders approval prompt and dispatches false (Deny/Reject) to store.respondToApproval and PageHandler', async () => {
    await store.bootstrap();

    const event = createApprovalRequestEvent('approval-456', 'session-1');
    const items = collectConversationItems([{sessionId: 'session-1', event}]);
    const item = items[0];
    expect(item?.approval).toBeDefined();

    const respondToApprovalSpy = vi.spyOn(store, 'respondToApproval');

    act(() => root.render(
        <ConversationSystemItem
          item={item!}
          onOpenSettings={vi.fn()}
          onRespondToApproval={(approvalId, approved) => {
            void store.respondToApproval(approvalId, approved);
          }}
          readOnly={false}
        />));

    const rejectButton = requireButton(container, 'Reject');
    expect(rejectButton).toBeDefined();
    expect(rejectButton.disabled).toBe(false);

    await act(async () => {
      rejectButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(respondToApprovalSpy).toHaveBeenCalledOnce();
    expect(respondToApprovalSpy).toHaveBeenCalledWith('approval-456', false);
    expect(mockHandler.respondToApproval).toHaveBeenCalledOnce();
    expect(mockHandler.respondToApproval).toHaveBeenCalledWith('session-1', 'approval-456', false);
  });

  it('disables Allow once and Reject buttons when readOnly is true and prevents dispatch', async () => {
    await store.bootstrap();

    const event = createApprovalRequestEvent('approval-789', 'session-1');
    const items = collectConversationItems([{sessionId: 'session-1', event}]);
    const item = items[0];

    const respondToApprovalSpy = vi.spyOn(store, 'respondToApproval');

    act(() => root.render(
        <ConversationSystemItem
          item={item!}
          onOpenSettings={vi.fn()}
          onRespondToApproval={(approvalId, approved) => {
            void store.respondToApproval(approvalId, approved);
          }}
          readOnly={true}
        />));

    const allowOnceButton = requireButton(container, 'Allow once');
    const rejectButton = requireButton(container, 'Reject');

    expect(allowOnceButton.disabled).toBe(true);
    expect(rejectButton.disabled).toBe(true);

    await act(async () => {
      allowOnceButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      rejectButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(respondToApprovalSpy).not.toHaveBeenCalled();
    expect(mockHandler.respondToApproval).not.toHaveBeenCalled();
  });
});
