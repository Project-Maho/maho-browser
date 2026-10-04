import {describe, expect, it} from 'vitest';
import {
  projectReceipt,
  receiptFromToolOutput,
  receiptSummary,
  redactReceiptValue,
} from '../../receipt-projection.js';

const SENTINEL = 'S3NTINEL-panel-receipt-secret';

function browserReceipt(state: string, approval = 'not_requested') {
  return {
    capabilityId: 'browser.page.read',
    executionId: 'receipt-stable-1',
    metadata: {
      controller: {
        id: 'controller-1',
        name: 'Maho Agent',
        type: 'user_agent',
        plane: 'local_agent',
      },
      target: {tabId: 7, origin: 'https://example.test'},
      category: 'observe',
      sensitivity: 'low',
      approval,
      state,
      timestamps: {startedAt: 10, completedAt: 11},
      password: SENTINEL,
      pageText: SENTINEL,
    },
  };
}

describe('browser-owned receipt panel projection', () => {
  it('projects the same stable machine fields used by CLI and external MCP', () => {
    expect(projectReceipt(browserReceipt('completed'))).toEqual({
      schemaVersion: 1,
      resultVersion: 1,
      receiptId: 'receipt-stable-1',
      capabilityId: 'browser.page.read',
      controller: {
        id: 'controller-1',
        name: 'Maho Agent',
        type: 'user_agent',
        plane: 'local_agent',
      },
      target: {tabId: 7, origin: 'https://example.test'},
      category: 'observe',
      sensitivity: 'low',
      approval: 'not_requested',
      outcome: {status: 'succeeded', code: null},
      timestamps: {startedAt: 10, completedAt: 11},
    });
  });

  it.each([
    ['failed', 'failed'],
    ['denied', 'denied'],
    ['disconnected', 'disconnected'],
    ['future_state', 'unknown'],
  ] as const)('maps %s to explicit %s outcome', (state, expected) => {
    expect(projectReceipt(browserReceipt(state, state === 'denied' ? 'denied' : 'not_requested'))
        ?.outcome.status).toBe(expected);
  });

  it('redacts secrets and page payloads deterministically', () => {
    const redacted = redactReceiptValue(browserReceipt('completed'));
    const serialized = JSON.stringify(redacted);
    expect(serialized).not.toContain(SENTINEL);
    expect(serialized).toContain('[REDACTED]');
    expect(Object.keys((redacted as {metadata: object}).metadata)).toEqual([
      'approval',
      'category',
      'controller',
      'pageText',
      'password',
      'sensitivity',
      'state',
      'target',
      'timestamps',
    ]);
  });

  it('decodes typed tool-result output without exposing the raw payload', () => {
    const receipt = receiptFromToolOutput(JSON.stringify({
      outputJson: '{"page":"must-not-render"}',
      receipt: browserReceipt('completed'),
    }));
    expect(receipt?.receiptId).toBe('receipt-stable-1');
    expect(receiptSummary(receipt!)).toBe('observe on https://example.test succeeded');
    expect(receiptSummary(receipt!)).not.toContain('must-not-render');
  });

  it('ignores legacy or malformed tool output instead of inventing a receipt', () => {
    expect(receiptFromToolOutput('{"tabs":[]}')).toBeNull();
    expect(receiptFromToolOutput('not json')).toBeNull();
  });
});
