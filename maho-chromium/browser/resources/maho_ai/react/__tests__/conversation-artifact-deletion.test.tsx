import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {MahoAiStore} from '../../store.js';
import {ConversationThread} from '../features/compact/conversation-thread.js';
import {
  type AISettingsInfo,
  type ArtifactInfo,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RuntimeEvent,
  RuntimeEventKind,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';

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

function createRouter(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  return router;
}

function session(): SessionInfo {
  return {
    sessionId: 's1',
    title: 's1',
    summary: '',
    createdAt: 1,
    updatedAt: 1,
    adapterName: 'OpenCode',
    isActive: true,
    isReadOnly: false,
    status: SessionStatus.kActive,
    eventCount: 0,
    toolCallCount: 0,
    runtimeSessionId: 's1',
    lastRuntimeState: 'idle',
  };
}

function artifact(): ArtifactInfo {
  return {
    artifactId: 'a1',
    sessionId: 's1',
    displayName: 'inline.html',
    mimeType: 'text/html',
    sizeBytes: 128n,
    createdAt: 1,
  };
}

function openMenu(container: HTMLElement): void {
  const trigger = container.querySelector<HTMLElement>('[aria-label="Actions for inline.html"]')!;
  act(() => {
    trigger.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      button: 2,
      clientX: 20,
      clientY: 20,
    }));
  });
}

function menuItem(label: string): HTMLElement {
  return Array.from(document.querySelectorAll<HTMLElement>('[role="menuitem"]'))
      .find(item => item.textContent?.trim() === label)!;
}

describe('ConversationThread artifact deletion', () => {
  let handler: PageHandlerRemote;
  let router: PageCallbackRouter;
  let store: MahoAiStore;
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(async () => {
    globalThis.requestAnimationFrame = ((callback: FrameRequestCallback) => {
      callback(0);
      return 0;
    }) as typeof requestAnimationFrame;
    handler = new PageHandlerRemote();
    router = createRouter();
    handler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
    handler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
    handler.getCreditBalance = vi.fn().mockResolvedValue({
      info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
    });
    handler.openSettingsPane = vi.fn().mockResolvedValue(undefined);
    store = new MahoAiStore(handler, router);
    await store.ingestAskMahoSessionAccepted('req-1', session());
    vi.mocked(router.onRuntimeEvent.addListener).mock.calls[0]![0]({
      sessionId: 's1',
      kind: RuntimeEventKind.kArtifactCreated,
      sequence: 1,
      timestamp: 1,
      artifact: artifact(),
    });
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
  });

  function render() {
    act(() => root.render(
        <ConversationThread
          bottomPadding={0}
          entries={store.getSnapshot().eventsBySessionId.s1 || []}
          onOpenSettings={vi.fn()}
          onRespondToApproval={vi.fn()}
          onDeleteArtifact={artifactId => store.deleteArtifact('s1', artifactId)}
          readOnly={false}
          thinkingLabel={null}
          topPadding={0}
        />));
  }

  it('removes the inline event/card after successful deletion', async () => {
    handler.deleteArtifact = vi.fn().mockResolvedValue({success: true});
    render();
    expect(container.textContent).toContain('inline.html');
    openMenu(container);
    await act(async () => {
      menuItem('Delete').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    render();

    expect(container.textContent).not.toContain('inline.html');
    expect(container.querySelector('[aria-label="Actions for inline.html"]')).toBeNull();
    expect(store.getSnapshot().eventsBySessionId.s1).toEqual([]);
  });
});
