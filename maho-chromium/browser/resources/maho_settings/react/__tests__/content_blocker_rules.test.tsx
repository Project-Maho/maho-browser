import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {PageHandlerRemote} from '../../maho_settings.mojom-webui.js';
import {ContentBlockerRulesCard, MAX_LOCAL_FILTER_LIST_BYTES} from '../content_blocker_rules.js';

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

const okMutation = {success: true, errorCode: null, errorMessage: null, compileRequired: true, compileScheduled: true, stats: null};

function makeHandler(overrides: Record<string, unknown> = {}) {
  return {
    getCustomFilterRules: vi.fn().mockResolvedValue({rules: '||old.example^'}),
    saveCustomFilterRules: vi.fn().mockResolvedValue({success: true, mutation: okMutation}),
    testContentBlockerRequest: vi.fn().mockResolvedValue({result: {
      validRequest: true, nativeMode: true, siteExcepted: false, blocked: true, important: false,
      matchedFilter: '||ads.example^', exception: null, listId: 'easylist', redirect: null,
    }}),
    importLocalFilterList: vi.fn().mockResolvedValue({success: true, mutation: okMutation}),
    ...overrides,
  };
}

async function render(handler: ReturnType<typeof makeHandler>, onChanged = vi.fn()) {
  await act(async () => {
    root.render(<ContentBlockerRulesCard handler={handler as unknown as PageHandlerRemote} disabled={false} onChanged={onChanged} />);
  });
  return onChanged;
}

function byLabel<T extends Element>(label: string): T {
  const element = container.querySelector<T>(`[aria-label="${label}"]`);
  if (!element) throw new Error(`Missing ${label}`);
  return element;
}

function button(text: string): HTMLButtonElement {
  const found = Array.from(container.querySelectorAll('button')).find(b => b.textContent?.includes(text));
  if (!found) throw new Error(`Missing button ${text}`);
  return found as HTMLButtonElement;
}

function type(element: HTMLInputElement | HTMLTextAreaElement, value: string) {
  const proto = element instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
  Object.getOwnPropertyDescriptor(proto, 'value')!.set!.call(element, value);
  element.dispatchEvent(new Event('input', {bubbles: true}));
}

async function chooseFile(file: File) {
  const input = byLabel<HTMLInputElement>('Filter list file');
  Object.defineProperty(input, 'files', {configurable: true, value: [file]});
  await act(async () => { input.dispatchEvent(new Event('change', {bubbles: true})); });
  // FileReader completes on a later task.
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
}

describe('content blocker custom rules', () => {
  it('loads saved rules and saves edits, refreshing the pane', async () => {
    const handler = makeHandler();
    const onChanged = await render(handler);
    const textarea = byLabel<HTMLTextAreaElement>('Custom filter rules');
    expect(textarea.value).toBe('||old.example^');
    expect(button('Save rules').disabled).toBe(true);
    await act(async () => { type(textarea, '||new.example^'); });
    await act(async () => { button('Save rules').click(); });
    expect(handler.saveCustomFilterRules).toHaveBeenCalledWith('||new.example^');
    expect(onChanged).toHaveBeenCalledOnce();
    expect(container.textContent).toContain('Custom rules saved.');
  });

  it('shows the core rejection and keeps the draft', async () => {
    const handler = makeHandler({saveCustomFilterRules: vi.fn().mockResolvedValue({success: false,
      mutation: {...okMutation, success: false, errorCode: 'invalid_update_body', errorMessage: 'invalid update body: no usable filter rules'}})});
    const onChanged = await render(handler);
    await act(async () => { type(byLabel<HTMLTextAreaElement>('Custom filter rules'), 'not a rule'); });
    await act(async () => { button('Save rules').click(); });
    expect(container.textContent).toContain('no usable filter rules');
    expect(byLabel<HTMLTextAreaElement>('Custom filter rules').value).toBe('not a rule');
    expect(onChanged).not.toHaveBeenCalled();
  });
});

describe('content blocker request tester', () => {
  it('sends url, page url (defaulting to the url) and type, and shows the deciding rule', async () => {
    const handler = makeHandler();
    await render(handler);
    await act(async () => { type(byLabel<HTMLInputElement>('Request URL'), 'https://ads.example/x.js'); });
    await act(async () => { button('Test').click(); });
    expect(handler.testContentBlockerRequest).toHaveBeenCalledWith('https://ads.example/x.js', 'https://ads.example/x.js', 'script');
    const result = byLabel<HTMLElement>('Request test result');
    expect(result.textContent).toContain('Blocked.');
    expect(result.textContent).toContain('||ads.example^');
    expect(result.textContent).toContain('easylist');
  });
});

describe('local filter list import', () => {
  it('reads the chosen file and sends its text, never a path', async () => {
    const handler = makeHandler();
    const onChanged = await render(handler);
    await chooseFile(new File(['||local.example^\n'], 'my-rules.txt', {type: 'text/plain'}));
    expect(handler.importLocalFilterList).toHaveBeenCalledWith('my-rules', '||local.example^\n');
    expect(onChanged).toHaveBeenCalledOnce();
    expect(container.textContent).toContain('Imported my-rules.txt.');
  });

  it('rejects oversized files before sending anything', async () => {
    const handler = makeHandler();
    await render(handler);
    const big = new File(['x'], 'huge.txt');
    Object.defineProperty(big, 'size', {value: MAX_LOCAL_FILTER_LIST_BYTES + 1});
    await chooseFile(big);
    expect(handler.importLocalFilterList).not.toHaveBeenCalled();
    expect(container.textContent).toContain('larger than 16 MB');
  });
});
