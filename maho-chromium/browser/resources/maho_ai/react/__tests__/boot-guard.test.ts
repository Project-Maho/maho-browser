import {describe, it, expect, beforeEach, vi} from 'vitest';
import {runWithBootGuard} from '../boot-guard.js';

describe('runWithBootGuard', () => {
  let root: HTMLElement;

  beforeEach(() => {
    root = document.createElement('div');
  });

  it('logs the caught error to console.error before injecting .boot-error', () => {
    const spy = vi.spyOn(console, 'error').mockImplementation(() => {});
    const err = new Error('store init exploded');

    runWithBootGuard(root, () => { throw err; });

    expect(spy).toHaveBeenCalledOnce();
    expect(spy).toHaveBeenCalledWith('[maho-ai] Boot init threw:', err);
    spy.mockRestore();
  });

  it('injects a .boot-error section with title and message when init throws', () => {
    runWithBootGuard(root, () => {
      throw new Error('store init exploded');
    });

    const section = root.querySelector('.boot-error');
    expect(section).not.toBeNull();
    expect(section!.tagName.toLowerCase()).toBe('section');

    const h1 = section!.querySelector('h1');
    expect(h1).not.toBeNull();
    expect(h1!.textContent).toBe('Maho AI failed to start');

    const pre = section!.querySelector('pre');
    expect(pre).not.toBeNull();
    expect(pre!.textContent).toBe('store init exploded');
  });

  it('surfaces non-Error thrown values as the pre message', () => {
    runWithBootGuard(root, () => {
      throw 'raw string failure';
    });

    const pre = root.querySelector('.boot-error pre');
    expect(pre).not.toBeNull();
    expect(pre!.textContent).toBe('raw string failure');
  });

  it('does not inject .boot-error when init succeeds', () => {
    runWithBootGuard(root, () => {
      root.textContent = 'mounted';
    });

    expect(root.querySelector('.boot-error')).toBeNull();
    expect(root.textContent).toBe('mounted');
  });
});
