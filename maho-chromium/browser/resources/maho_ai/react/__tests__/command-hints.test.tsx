import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {CommandHints} from '../features/compact/command-hints.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

describe('CommandHints — slash command discoverability', () => {
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

  it('keeps slash commands in a disclosure that is closed by default', () => {
    const onPick = vi.fn();
    act(() => root.render(<CommandHints disabled={false} onPick={onPick} />));

    const disclosure = container.querySelector<HTMLDetailsElement>(
        'details[data-command-disclosure]');
    expect(disclosure).not.toBeNull();
    expect(disclosure?.open).toBe(false);
    expect(disclosure?.querySelector('summary')?.textContent).toContain('Commands');

    act(() => disclosure!.open = true);
    const commands = Array.from(disclosure!.querySelectorAll('[data-command-hint]'))
        .map(node => node.textContent?.trim());
    expect(commands).toEqual(['/agent new', '/tools add mcp', '/tools add cli']);
  });

  it('dispatches the picked command into the composer', () => {
    const onPick = vi.fn();
    act(() => root.render(<CommandHints disabled={false} onPick={onPick} />));

    const disclosure = container.querySelector<HTMLDetailsElement>(
        'details[data-command-disclosure]')!;
    act(() => disclosure.open = true);
    const hint = disclosure.querySelector<HTMLElement>(
        '[data-command-hint="/tools add mcp"]')!;
    act(() => {
      hint.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(onPick).toHaveBeenCalledWith('/tools add mcp');
  });

  it('renders nothing while disabled (read-only session or active prompt)', () => {
    act(() => root.render(<CommandHints disabled onPick={() => {}} />));
    expect(container.querySelector('[data-command-hint]')).toBeNull();
  });
});
