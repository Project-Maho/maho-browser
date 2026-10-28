import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {SiteSessionsSection} from '../site_sessions_section.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {configurable: true, value: true});

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

function type(value: string) {
  const input = container.querySelector<HTMLInputElement>('[aria-label="HTTPS origin"]');
  if (!input) throw new Error('Missing origin input');
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, value);
  input.dispatchEvent(new Event('input', {bubbles: true}));
}

function button(label: string): HTMLButtonElement {
  const found = Array.from(container.querySelectorAll('button')).find(item => item.textContent?.includes(label));
  if (!found) throw new Error(`Missing button ${label}`);
  return found;
}

describe('site session settings', () => {
  it('adds normalized HTTPS origins and removes opt-ins', async () => {
    const handler = {
      getSiteSessionOptedOrigins: vi.fn().mockResolvedValue({origins: []}),
      setSiteSessionOptedOrigins: vi.fn().mockResolvedValue({success: true, errorMessage: ''}),
    };
    await act(async () => root.render(<SiteSessionsSection handler={handler as never} />));
    await act(async () => { type('https://Example.com/'); });
    await act(async () => { button('Add site').click(); });
    expect(handler.setSiteSessionOptedOrigins).toHaveBeenCalledWith(['https://example.com']);
    expect(container.textContent).toContain('https://example.com');
    await act(async () => { button('Remove').click(); });
    expect(handler.setSiteSessionOptedOrigins).toHaveBeenLastCalledWith([]);
  });

  it('rejects non-HTTPS origins and IP literals', async () => {
    const handler = {
      getSiteSessionOptedOrigins: vi.fn().mockResolvedValue({origins: []}),
      setSiteSessionOptedOrigins: vi.fn().mockResolvedValue({success: true, errorMessage: ''}),
    };
    await act(async () => root.render(<SiteSessionsSection handler={handler as never} />));
    await act(async () => { type('http://example.com'); });
    await act(async () => { button('Add site').click(); });
    expect(handler.setSiteSessionOptedOrigins).not.toHaveBeenCalled();
    expect(container.textContent).toContain('valid HTTPS origin');
  });
});
