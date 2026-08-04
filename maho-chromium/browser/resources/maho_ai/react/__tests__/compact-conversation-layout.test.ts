import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {CompactShell} from '../features/compact/compact-shell.js';
import {CompactTopbar} from '../features/compact/compact-topbar.js';
import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type AISettingsInfo,
  type RuntimeEvent,
  type SessionInfo,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {ViewMode, type TimelineEntry} from '../../types.js';
import {TooltipProvider} from '@ui/tooltip';

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
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  return router;
}

function createMockStore(): MahoAiStore {
  const handler = new PageHandlerRemote();
  const router = createPageCallbackRouterFake();
  return new MahoAiStore(handler, router);
}

describe('Compact conversation layout', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('keeps the message header in normal flow above an independently scrollable thread', () => {
    // Given: a compact shell with messages rendered in message mode.
    const store = createMockStore();
    const entries: TimelineEntry[] = [
      {
        event: {
          kind: 1,
          sequence: 1,
          sessionId: 'session-1',
          text: 'Hello Maho',
          timestamp: 1,
        },
        sessionId: 'session-1',
      },
    ];

    // When: rendering CompactShell into jsdom.
    act(() => root.render(
      React.createElement(TooltipProvider, null,
      React.createElement(CompactShell, {
        store,
        entries,
        hasMessages: true,
        onClosePanel: vi.fn(),
        onGetViewMode: vi.fn().mockResolvedValue(ViewMode.kSidebar),
        onOpenSettings: vi.fn(),
        onRespondToApproval: vi.fn(),
        onSetViewMode: vi.fn(),
        onStartSession: vi.fn(),
        readOnly: false,
        thinkingLabel: null,
      })
      )
    ));

    // Then: the shell root container flexes in normal column layout.
    const shellRoot = container.firstElementChild as HTMLElement;
    expect(shellRoot).not.toBeNull();
    expect(shellRoot.className).toContain('flex h-full min-h-0 flex-col');

    // And: topbar header container comes before conversation thread in DOM order, in normal flow (not absolute pinned).
    const topOverlay = shellRoot.children[0] as HTMLElement;
    const threadMain = shellRoot.children[1] as HTMLElement;

    expect(topOverlay.className).toContain('relative');
    expect(topOverlay.className).not.toContain('absolute pinned');
    expect(topOverlay.querySelector('button[aria-label="Close panel"]')).not.toBeNull();

    // And: conversation thread main element is flex-1 with an independently scrollable container.
    expect(threadMain.tagName.toLowerCase()).toBe('main');
    expect(threadMain.className).toContain('flex min-h-0 flex-1 flex-col');

    const scrollContainer = threadMain.firstElementChild as HTMLElement;
    expect(scrollContainer.className).toContain('overflow-y-auto');
  });

  it('omits the Tidy Tabs affordance from the compact topbar', () => {
    // Given: a rendered CompactTopbar.
    const store = createMockStore();

    // When: CompactTopbar is rendered in jsdom.
    act(() => root.render(
      React.createElement(TooltipProvider, null,
      React.createElement(CompactTopbar, {
        store,
        onClosePanel: vi.fn(),
        onGetViewMode: vi.fn().mockResolvedValue(ViewMode.kSidebar),
        onOpenSettings: vi.fn(),
        onSetViewMode: vi.fn(),
        onStartSession: vi.fn(),
      })
      )
    ));

    // Then: no Tidy Tabs button or aria-label exists in the rendered DOM.
    expect(container.querySelector('[aria-label="Tidy tabs"]')).toBeNull();
    expect(container.textContent).not.toContain('Tidy tabs');
  });
});
