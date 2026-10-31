import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it} from 'vitest';

import {ConversationMessage} from '../features/compact/conversation-message.js';
import type {ConversationItem} from '../../views/conversation_thread.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const TABLE_MARKDOWN = [
  '| Option | Description |',
  '|---|---|',
  '| sandbox | Runs the agent inside a restricted container with no network access unless allowed |',
].join('\n');

function markdownItem(text: string): ConversationItem {
  return {key: 'assistant-table', markdown: true, role: 'assistant', text, timestamp: 1};
}

// jsdom has no layout engine, so these assertions pin the Tailwind utilities
// the bundled stylesheet is generated from. Without them a long cell either
// widens the table past the chat column or, under the message's
// overflow-wrap:anywhere, collapses short columns to one glyph per line.
describe('ConversationMessage markdown tables', () => {
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
  });

  function renderedBody(): HTMLElement {
    const table = container.querySelector('table');
    if (!table?.parentElement) {
      throw new Error('markdown table was not rendered');
    }
    return table.parentElement;
  }

  it('keeps tables inside the chat column and scrolls only the table', () => {
    act(() => root.render(<ConversationMessage item={markdownItem(TABLE_MARKDOWN)} />));

    const classes = renderedBody().className.split(/\s+/);
    expect(classes).toEqual(expect.arrayContaining([
      '[&_table]:block',
      '[&_table]:max-w-full',
      '[&_table]:overflow-x-auto',
    ]));
  });

  it('wraps cells at word boundaries instead of between every glyph', () => {
    act(() => root.render(<ConversationMessage item={markdownItem(TABLE_MARKDOWN)} />));

    const classes = renderedBody().className.split(/\s+/);
    expect(classes).toEqual(expect.arrayContaining([
      '[&_th]:[overflow-wrap:break-word]',
      '[&_td]:[overflow-wrap:break-word]',
    ]));
    expect(container.querySelectorAll('td')).toHaveLength(2);
  });

  it('does not apply table styling to plain-text messages', () => {
    act(() => root.render(
        <ConversationMessage item={{...markdownItem(TABLE_MARKDOWN), markdown: false}} />));

    expect(container.querySelector('table')).toBeNull();
    expect(container.innerHTML).not.toContain('[&_table]');
  });
});
