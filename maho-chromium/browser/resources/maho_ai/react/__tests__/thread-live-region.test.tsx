import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {RuntimeEventKind} from '../../maho_ai.mojom-webui.js';
import {ConversationThread} from '../features/compact/conversation-thread.js';
import type {TimelineEntry} from '../../types.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function entry(kind: RuntimeEventKind, sequence: number, text: string): TimelineEntry {
  return {
    sessionId: 'session-1',
    event: {kind, sequence, timestamp: sequence, sessionId: 'session-1', text},
  };
}

describe('ConversationThread — screen reader turn announcements', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('announces the pending turn on a polite live region', () => {
    const entries = [entry(RuntimeEventKind.kUserPrompt, 1, 'Summarize this')];
    act(() => {
      root.render(
          <ConversationThread
            entries={entries}
            thinkingLabel="Reading…"
            readOnly={false}
            bottomPadding={0}
            topPadding={0}
            onOpenSettings={() => {}}
            onRespondToApproval={() => {}}
          />);
    });

    const region = container.querySelector('[data-turn-status]');
    expect(region).not.toBeNull();
    expect(region?.getAttribute('aria-live')).toBe('polite');
    expect(region?.getAttribute('role')).toBe('status');
    expect(region?.textContent).toContain('Reading…');
  });

  it('clears the announcement when the turn completes', () => {
    const entries = [
      entry(RuntimeEventKind.kUserPrompt, 1, 'Summarize this'),
      entry(RuntimeEventKind.kAssistantToken, 2, 'Here is the summary'),
    ];
    act(() => {
      root.render(
          <ConversationThread
            entries={entries}
            thinkingLabel={null}
            readOnly={false}
            bottomPadding={0}
            topPadding={0}
            onOpenSettings={() => {}}
            onRespondToApproval={() => {}}
          />);
    });

    const region = container.querySelector('[data-turn-status]');
    expect(region?.textContent?.trim()).toBe('');
  });
});
