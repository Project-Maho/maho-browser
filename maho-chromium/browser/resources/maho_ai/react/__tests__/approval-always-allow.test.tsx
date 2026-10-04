import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ConversationSystemItem} from '../features/compact/conversation-system-item.js';
import type {ConversationItem} from '../../views/conversation_thread.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function approvalItem(overrides: Partial<ConversationItem> = {}): ConversationItem {
  return {
    key: 'approval-1',
    kind: 'approval',
    role: 'system',
    text: 'Run rm -rf ./build',
    markdown: false,
    timestamp: 1,
    approval: {
      approvalId: 'approval-1',
      status: 'pending',
      approvalPolicy: 'prompt',
      sensitivity: 'sensitive',
    },
    ...overrides,
  };
}

function clickButton(container: HTMLElement, label: string): HTMLButtonElement {
  const button = Array.from(container.querySelectorAll<HTMLButtonElement>('button'))
      .find(candidate => candidate.textContent?.trim() === label);
  if (!button) {
    throw new Error(`Missing button "${label}"`);
  }
  act(() => {
    button.dispatchEvent(new MouseEvent('click', {bubbles: true}));
  });
  return button;
}

describe('ConversationSystemItem — always-allow decision', () => {
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

  it('offers one Allow once action alongside Reject', () => {
    const onRespond = vi.fn();
    act(() => root.render(
        <ConversationSystemItem
          item={approvalItem()}
          onOpenSettings={() => {}}
          onRespondToApproval={onRespond}
          readOnly={false}
        />));

    expect(Array.from(container.querySelectorAll('button'))
        .filter(button => button.textContent?.trim() === 'Allow once')).toHaveLength(1);
    expect(Array.from(container.querySelectorAll('button'))
        .some(button => button.textContent?.trim() === 'Accept')).toBe(false);
    expect(Array.from(container.querySelectorAll('button'))
        .filter(button => button.textContent?.trim() === 'Reject')).toHaveLength(1);

    clickButton(container, 'Allow once');

    // The existing boolean allow decision remains the broker-gated one-time
    // authorization path; only the duplicate label is removed.
    expect(onRespond).toHaveBeenCalledOnce();
    expect(onRespond).toHaveBeenCalledWith('approval-1', true);
  });

  it('disables both decisions for read-only sessions', () => {
    const onRespond = vi.fn();
    act(() => root.render(
        <ConversationSystemItem
          item={approvalItem()}
          onOpenSettings={() => {}}
          onRespondToApproval={onRespond}
          readOnly
        />));

    expect(clickButtonThrows('Accept')).toBe(false);
    expect(clickButtonThrows('Allow once')).toBe(true);
    expect(clickButtonThrows('Reject')).toBe(true);

    function clickButtonThrows(label: string): boolean {
      const button = Array.from(container.querySelectorAll<HTMLButtonElement>('button'))
          .find(candidate => candidate.textContent?.trim() === label);
      return !!button && button.disabled;
    }
  });
});
