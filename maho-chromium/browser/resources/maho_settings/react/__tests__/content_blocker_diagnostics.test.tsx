import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ALL_PANE_DEFINITIONS} from '../../schema/panes.js';
import {DomainPaneContent} from '../domain_panes.js';
import type {MahoSettingsStore} from '../store.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

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

describe('content blocker diagnostics', () => {
  it('shows per-list accepted counts and rejected samples from health data', async () => {
    const handler = {
      getCustomFilterRules: vi.fn().mockResolvedValue({rules: ''}),
      getFilterLists: vi.fn().mockResolvedValue({lists: [{
        id: 'easy-list',
        name: 'EasyList',
        url: 'https://example.com/easylist.txt',
        enabled: true,
        ruleCount: 17,
        etag: null,
        lastModified: null,
        sha256: null,
        lastAttemptTimestamp: null,
        lastSuccessTimestamp: null,
        failureCount: 0,
        acceptedNetworkRuleCount: 12,
        acceptedCosmeticRuleCount: 3,
        rejectedRuleCount: 2,
        rejectedRuleSamples: ['||ads.example^', 'invalid rule'],
        lastStatus: null,
        lastError: null,
        nextRetryTimestamp: null,
      }]}),
      getContentBlockerStats: vi.fn().mockResolvedValue({stats: {
        mode: 0,
        totalRuleCount: 15,
        filterListCount: 1,
        engineGeneration: 1n,
        healthStatus: 'healthy',
        overallError: null,
        lastUpdateTimestamp: 0n,
      }}),
    };
    const snapshot = {settings: []};
    const store = {
      getHandler: () => handler,
      getSnapshot: () => snapshot,
      subscribe: () => () => {},
    } as unknown as MahoSettingsStore;
    const pane = ALL_PANE_DEFINITIONS.find(item => item.contentKind === 'content-blocker')!;

    await act(async () => {
      root.render(<DomainPaneContent pane={pane} settings={[]} store={store} />);
    });

    expect(await container.querySelector('[data-testid="filter-diagnostics-easy-list"]')?.textContent)
        .toContain('12 network rules accepted');
    expect(container.textContent).toContain('3 cosmetic rules accepted');
    expect(container.textContent).toContain('2 rules rejected');
    expect(container.textContent).toContain('||ads.example^');
    expect(container.textContent).toContain('invalid rule');
  });
});
