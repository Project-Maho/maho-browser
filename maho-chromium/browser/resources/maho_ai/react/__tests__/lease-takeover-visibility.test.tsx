import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {RuntimeEventKind} from '../../maho_ai.mojom-webui.js';
import {collectConversationItems} from '../../views/conversation_thread.js';
import {ConversationSystemItem} from '../features/compact/conversation-system-item.js';
import type {TimelineEntry} from '../../types.js';

// Backlog P2-1 (criterion 4) and P2-2 (criterion 4): when a session lease is
// released / forcibly taken over — including an MCP backend lease that is
// forcibly revoked — the panel must immediately surface an unavailable/error
// indication rather than silently continuing.
//
// GAP FINDING: the desktop maho-ai WebUI has NO dedicated typed "lease
// released" / "forced takeover" / "MCP lease revoked" UI state. There is no
// such enum in maho_ai.mojom and no such branch in the store or the conversation
// components. The closest existing representation is the generic runtime error
// surface (RuntimeEventKind.kError -> ConversationSystemItem "Something went
// wrong" notice with an "Issue" badge). These tests lock in that the takeover /
// lease-revocation scenarios are surfaced through that existing error affordance,
// and document the missing first-class takeover state for follow-up.

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

interface RuntimeErrorLike {
  credentialErrorCode: number | null;
  kind: RuntimeEventKind;
  sequence: number;
  sessionId: string;
  text: string;
  timestamp: number;
}

function createErrorEvent(text: string): RuntimeErrorLike {
  return {
    credentialErrorCode: null,
    kind: RuntimeEventKind.kError,
    sequence: 1,
    sessionId: 'session-1',
    text,
    timestamp: 1,
  };
}

function renderErrorEntry(root: Root, text: string): void {
  const event = createErrorEvent(text);
  const entries = [{sessionId: 'session-1', event}] as unknown as TimelineEntry[];
  const items = collectConversationItems(entries);
  const item = items[0];
  expect(item).toBeDefined();

  act(() => root.render(
      <ConversationSystemItem
        item={item!}
        onOpenSettings={vi.fn()}
        onRespondToApproval={vi.fn()}
        readOnly={false}
      />));
}

describe('Lease release / forced-takeover visibility in rendered DOM', () => {
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

  it('surfaces a forced session-lease takeover as an immediate error/unavailable notice', () => {
    // Given: the runtime reports the active session lease was taken over.
    renderErrorEntry(
        root, 'Session was taken over by another window and is no longer available.');

    // Then: the panel immediately shows an error/unavailable indication.
    expect(container.textContent).toContain('Something went wrong');
    expect(container.textContent).toContain('taken over');
    expect(container.textContent).toContain('Issue');
  });

  it('surfaces an MCP backend lease forced-takeover as an immediate error/unavailable notice', () => {
    // Given: the runtime reports the MCP tool lease was forcibly revoked.
    renderErrorEntry(
        root, 'MCP tool session was taken over; the connection is no longer available.');

    // Then: the panel immediately shows an error/unavailable indication.
    expect(container.textContent).toContain('Something went wrong');
    expect(container.textContent).toContain('MCP');
    expect(container.textContent).toContain('Issue');
  });
});
