import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {act, createElement} from 'react';
import {createRoot} from 'react-dom/client';

import {MahoAiStore} from '../../store.js';
import {ConversationThread} from '../features/compact/conversation-thread.js';
import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeEventKind,
  SessionStatus,
  type RuntimeEvent,
  type SessionInfo,
} from '../../maho_ai.mojom-webui.js';

vi.mock('../../maho_ai.mojom-webui.js', async importOriginal => {
  const bindings = await importOriginal<typeof import('../../maho_ai.mojom-webui.js')>();
  return {
    ...bindings,
    // The standalone binding omits thinking and tool availability. Use the
    // shipped enum from browser/ui/webui/maho_ai/maho_ai.mojom for these tests.
    RuntimeEventKind: {
      kAssistantToken: 0,
      kAssistantThinking: 1,
      kTurnComplete: 2,
      kError: 3,
      kToolRequest: 4,
      kToolResult: 5,
      kApprovalRequest: 6,
      kApprovalResult: 7,
      kUserPrompt: 8,
      kSessionStatus: 9,
      kConnectionStateChanged: 10,
      kBrowserContextInjected: 11,
      kToolAvailabilityChanged: 12,
      kArtifactCreated: 13,
      kInteractionRequest: 14,
    },
  };
});

function session(sessionId: string): SessionInfo {
  return {
    sessionId, title: sessionId, summary: '', createdAt: 1, updatedAt: 1,
    adapterName: 'test', isActive: true, isReadOnly: false,
    status: SessionStatus.kActive, eventCount: 0, toolCallCount: 0,
    runtimeSessionId: sessionId, lastRuntimeState: 'idle',
  };
}

function event(sessionId: string, sequence: number): RuntimeEvent {
  return {
    sessionId, sequence, timestamp: sequence,
    kind: RuntimeEventKind.kAssistantToken, text: 'token-' + sequence,
  };
}

function createStore() {
  const router = new PageCallbackRouter();
  const sessionListener = vi.fn<(session: SessionInfo) => void>();
  const runtimeListener = vi.fn<(event: RuntimeEvent) => void>();
  vi.spyOn(router.onSessionUpdated, 'addListener').mockImplementation(listener => {
    sessionListener.mockImplementation(listener);
    return 1;
  });
  vi.spyOn(router.onRuntimeEvent, 'addListener').mockImplementation(listener => {
    runtimeListener.mockImplementation(listener);
    return 2;
  });
  const store = new MahoAiStore(new PageHandlerRemote(), router);
  store.setComposerDraftPersistenceForTesting({
    clear: async () => undefined, flush: async () => undefined,
    load: async () => null, schedule: () => undefined,
  });
  sessionListener(session('active'));
  sessionListener(session('history'));
  runtimeListener(event('active', 1));
  runtimeListener(event('history', 1));
  return {store, runtimeListener};
}

describe('MahoAiStore timeline snapshot invalidation', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true);
  });
  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
    vi.restoreAllMocks();
    vi.unstubAllGlobals();
  });

  it('does not regroup the rendered conversation on composer input', () => {
    const {store, runtimeListener} = createStore();
    let historyTextReads = 0;
    runtimeListener({
      ...event('active', 2),
      kind: RuntimeEventKind.kUserPrompt,
      get text() {
        ++historyTextReads;
        return 'existing prompt';
      },
    });
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    const render = () => root.render(createElement(ConversationThread, {
      entries: store.getSnapshot().eventsBySessionId.active,
      thinkingLabel: null, readOnly: false, bottomPadding: 0, topPadding: 0,
      onOpenSettings: () => undefined,
      onRespondToApproval: () => undefined,
    }));
    try {
      act(render);
      expect(container.textContent).toContain('token-1');
      expect(container.textContent).toContain('existing prompt');
      const initialTextReads = historyTextReads;
      expect(initialTextReads).toBeGreaterThan(0);
      store.setComposerPrompt('draft changed');
      act(render);
      expect(container.textContent).toContain('token-1');
      expect(historyTextReads).toBe(initialTextReads);
      runtimeListener(event('active', 3));
      act(render);
      expect(container.textContent).toContain('token-3');
      expect(historyTextReads).toBeGreaterThan(initialTextReads);
    } finally {
      act(() => root.unmount());
      container.remove();
      store.dispose();
    }
  });

  it('does not invalidate unchanged timelines when typing in the composer', () => {
    const {store} = createStore();
    try {
      const before = store.getSnapshot();
      store.setComposerPrompt('draft changed');
      const after = store.getSnapshot();
      expect(after.composer.prompt).toBe('draft changed');
      expect(before.composer.prompt).toBe('');
      expect(after.eventsBySessionId.active).toBe(before.eventsBySessionId.active);
      expect(after.eventsBySessionId.history).toBe(before.eventsBySessionId.history);
    } finally {
      store.dispose();
    }
  });

  it('does not invalidate another session timeline when a token arrives', () => {
    const {store, runtimeListener} = createStore();
    try {
      const before = store.getSnapshot();
      runtimeListener(event('active', 2));
      const after = store.getSnapshot();
      expect(after.eventsBySessionId.active).toHaveLength(1);
      expect(before.eventsBySessionId.active).toHaveLength(1);
      expect(after.eventsBySessionId.active?.[0]?.event.text).toBe('token-1token-2');
      expect(after.eventsBySessionId.history).toBe(before.eventsBySessionId.history);
    } finally {
      store.dispose();
    }
  });

  it.each([RuntimeEventKind.kAssistantToken, RuntimeEventKind.kAssistantThinking])(
      'keeps published snapshots immutable when merging event kind %s', kind => {
        expect(Number.isInteger(kind)).toBe(true);
        const {store, runtimeListener} = createStore();
        try {
          runtimeListener({...event('active', 2), kind});
          const before = store.getSnapshot();
          const previousText = before.eventsBySessionId.active?.at(-1)?.event.text;
          runtimeListener({...event('active', 3), kind});
          const after = store.getSnapshot();
          expect(after.eventsBySessionId.active?.at(-1)?.event.text)
              .toBe(previousText + 'token-3');
          expect(before.eventsBySessionId.active?.at(-1)?.event.text)
              .toBe(previousText);
        } finally {
          store.dispose();
        }
      });
});

