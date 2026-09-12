import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ComposerTextarea} from '../components/composer-textarea.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

describe('ComposerTextarea — auto-expanding height', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => {
      root.unmount();
    });
    container.remove();
  });

  it('adjusts textarea height based on scrollHeight as text changes', () => {
    const onValueChange = vi.fn();
    const onSubmit = vi.fn();

    act(() => {
      root.render(
        <ComposerTextarea
          focusRequest={0}
          onSubmit={onSubmit}
          onValueChange={onValueChange}
          placeholder="Type here..."
          readOnly={false}
          rows={1}
          value="hello"
        />
      );
    });

    const textarea = container.querySelector('textarea')!;
    expect(textarea).not.toBeNull();

    Object.defineProperty(textarea, 'scrollHeight', {
      configurable: true,
      value: 28,
    });

    act(() => {
      root.render(
        <ComposerTextarea
          focusRequest={0}
          onSubmit={onSubmit}
          onValueChange={onValueChange}
          placeholder="Type here..."
          readOnly={false}
          rows={1}
          value="line 1\nline 2\nline 3"
        />
      );
    });

    Object.defineProperty(textarea, 'scrollHeight', {
      configurable: true,
      value: 72,
    });

    act(() => {
      textarea.dispatchEvent(new Event('input', {bubbles: true}));
    });

    expect(textarea.style.height).toBe('72px');

    Object.defineProperty(textarea, 'scrollHeight', {
      configurable: true,
      value: 28,
    });

    act(() => {
      root.render(
        <ComposerTextarea
          focusRequest={0}
          onSubmit={onSubmit}
          onValueChange={onValueChange}
          placeholder="Type here..."
          readOnly={false}
          rows={1}
          value="short"
        />
      );
    });

    expect(textarea.style.height).toBe('28px');
  });
});
