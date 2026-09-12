import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {CompactStage} from '../features/compact/compact-stage.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

describe('CompactStage — idle empty state', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    localStorage.clear();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    localStorage.clear();
    vi.restoreAllMocks();
  });

  it('renders exactly one empty stage that keeps an accessible name', () => {
    act(() => root.render(<CompactStage />));

    const stage = container.querySelector('main[aria-label="Empty conversation"]');
    expect(stage).not.toBeNull();
    expect(document.querySelectorAll('main[aria-label="Empty conversation"]'))
        .toHaveLength(1);
    expect(stage?.querySelector('h2')?.textContent).toContain('Ask Maho');
  });

  it('keeps the idle stage free of prompt cards and guidance boxes', () => {
    act(() => root.render(<CompactStage />));

    const stage = container.querySelector('main[aria-label="Empty conversation"]');
    expect(stage?.querySelector('[data-suggested-tasks]')).toBeNull();
    expect(stage?.querySelector('[data-suggested-task]')).toBeNull();
    expect(stage?.querySelector('[role="note"]')).toBeNull();
    expect(stage?.querySelectorAll('button')).toHaveLength(0);
  });
});
