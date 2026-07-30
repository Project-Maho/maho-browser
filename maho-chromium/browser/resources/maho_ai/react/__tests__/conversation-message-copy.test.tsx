import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ConversationMessage} from '../features/compact/conversation-message.js';
import type {ConversationItem} from '../../views/conversation_thread.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const ASSISTANT_TEXT = 'Here is the plan:\n\n1. Read the file\n2. Ship the fix';

function assistantItem(overrides: Partial<ConversationItem> = {}): ConversationItem {
  return {
    key: 'assistant-1',
    markdown: false,
    role: 'assistant',
    text: ASSISTANT_TEXT,
    timestamp: 1,
    ...overrides,
  };
}

function requireElement<T extends Element>(element: T|null, description: string): T {
  if (element) {
    return element;
  }

  throw new Error(`Missing ${description}`);
}

function stubClipboard(writeText: (text: string) => Promise<void>): void {
  Object.defineProperty(navigator, 'clipboard', {
    configurable: true,
    value: {writeText},
  });
}

function copyButton(container: HTMLElement): HTMLButtonElement {
  return requireElement(
      container.querySelector<HTMLButtonElement>(
          'button[aria-label="Copy response"], button[aria-label="Copied"], ' +
          'button[aria-label="Copy failed"]'),
      'copy button');
}

function liveRegion(container: HTMLElement): HTMLElement {
  return requireElement(
      container.querySelector<HTMLElement>('[role="status"][aria-live="polite"]'),
      'copy status live region');
}

describe('ConversationMessage copy action', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    vi.useFakeTimers();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('writes the assistant message text to the clipboard when Copy is clicked', async () => {
    const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
    stubClipboard(writeText);

    act(() => root.render(<ConversationMessage item={assistantItem()} />));

    await act(async () => {
      copyButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(writeText).toHaveBeenCalledOnce();
    expect(writeText).toHaveBeenCalledWith(ASSISTANT_TEXT);
  });

  it('copies the raw markdown source for markdown-rendered responses', async () => {
    const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
    stubClipboard(writeText);

    const markdownText = '## Heading\n\n- one\n- two';
    act(() => root.render(
        <ConversationMessage item={assistantItem({markdown: true, text: markdownText})} />));

    await act(async () => {
      copyButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(writeText).toHaveBeenCalledWith(markdownText);
  });

  it('announces the transient Copied state and reverts it after the reset delay', async () => {
    stubClipboard(vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined));

    act(() => root.render(<ConversationMessage item={assistantItem()} />));

    expect(copyButton(container).getAttribute('aria-label')).toBe('Copy response');
    expect(liveRegion(container).textContent).toBe('');

    await act(async () => {
      copyButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(copyButton(container).getAttribute('aria-label')).toBe('Copied');
    expect(liveRegion(container).textContent).toBe('Copied');

    await act(async () => {
      await vi.runOnlyPendingTimersAsync();
    });

    expect(copyButton(container).getAttribute('aria-label')).toBe('Copy response');
    expect(liveRegion(container).textContent).toBe('');
  });

  it('reports a failed copy instead of swallowing the clipboard rejection', async () => {
    const writeText = vi.fn<(text: string) => Promise<void>>()
                          .mockRejectedValue(new Error('write permission denied'));
    stubClipboard(writeText);
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

    act(() => root.render(<ConversationMessage item={assistantItem()} />));

    await act(async () => {
      copyButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(writeText).toHaveBeenCalledOnce();
    expect(copyButton(container).getAttribute('aria-label')).toBe('Copy failed');
    expect(liveRegion(container).textContent).toBe('Copy failed');
    expect(consoleError).toHaveBeenCalled();
  });

  it('reports a failed copy when the clipboard API is unavailable', async () => {
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: undefined,
    });
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

    act(() => root.render(<ConversationMessage item={assistantItem()} />));

    await act(async () => {
      copyButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(copyButton(container).getAttribute('aria-label')).toBe('Copy failed');
    expect(consoleError).toHaveBeenCalled();
  });

  it('does not render feedback controls that have no backend', () => {
    const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
    stubClipboard(writeText);

    act(() => root.render(<ConversationMessage item={assistantItem()} />));

    for (const label of ['Like response', 'Dislike response']) {
      expect(container.querySelector(`button[aria-label="${label}"]`)).toBeNull();
    }

    expect(copyButton(container).getAttribute('aria-label')).toBe('Copy response');
    expect(writeText).not.toHaveBeenCalled();
  });

  it('invokes the regenerate handler when one is supplied', async () => {
    stubClipboard(vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined));
    const onRegenerate = vi.fn();

    act(() => root.render(
        <ConversationMessage item={assistantItem()} onRegenerate={onRegenerate} />));

    const button = requireElement(
        container.querySelector<HTMLButtonElement>('button[aria-label="Regenerate response"]'),
        'Regenerate response');

    await act(async () => {
      button.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(onRegenerate).toHaveBeenCalledOnce();
  });

  it('hides regenerate when the turn cannot be replayed', () => {
    stubClipboard(vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined));

    act(() => root.render(<ConversationMessage item={assistantItem()} />));

    expect(container.querySelector('button[aria-label="Regenerate response"]')).toBeNull();
  });
  it('renders no assistant actions for user messages', () => {
    act(() => root.render(<ConversationMessage item={assistantItem({role: 'user'})} />));

    expect(container.querySelector('button[aria-label="Copy response"]')).toBeNull();
  });
});
