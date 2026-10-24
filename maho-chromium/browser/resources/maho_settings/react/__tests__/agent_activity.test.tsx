// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {AgentActionJournalEntry} from '../../maho_settings.mojom-webui.js';
import {AgentActivityCard, type AgentActivityHandler} from '../agent_activity.js';

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

function entry(id: number, actor: string, action: string, origin: string): AgentActionJournalEntry {
  return {
    id: BigInt(id),
    timestamp: BigInt(1_700_000_000 + id),
    sessionId: 's',
    actor,
    action,
    origin,
    summary: action,
    outcome: 'completed',
  };
}

const ALL = [
  entry(3, 'cli', 'purchase', 'https://shop.example'),
  entry(2, 'mcp', 'mail_send', ''),
  entry(1, 'panel', 'form_submit', 'https://a.example'),
];

function makeHandler(entries = ALL) {
  let rows = [...entries];
  const getAgentActionJournal = vi.fn(async (actor: string, action: string, query: string) => ({
    entries: rows.filter(row =>
        (!actor || row.actor === actor) && (!action || row.action === action) &&
        (!query || row.origin.includes(query))),
  }));
  const clearAgentActionJournal = vi.fn(async () => {
    rows = [];
    return {success: true};
  });
  return {getAgentActionJournal, clearAgentActionJournal} satisfies AgentActivityHandler;
}

async function flush() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

async function render(handler: AgentActivityHandler) {
  await act(async () => {
    root.render(<AgentActivityCard handler={handler} />);
  });
  await flush();
}

function rows() {
  return container.querySelectorAll('[data-agent-activity-row="true"]');
}

describe('Agent activity card', () => {
  it('lists journal entries newest first with labels', async () => {
    const handler = makeHandler();
    await render(handler);
    expect(handler.getAgentActionJournal).toHaveBeenCalledWith('', '', '');
    expect(rows()).toHaveLength(3);
    expect(rows()[0]!.textContent).toContain('Purchase');
    expect(rows()[0]!.textContent).toContain('Maho CLI');
    expect(rows()[0]!.textContent).toContain('https://shop.example');
    expect(container.textContent).toContain('never synced');
  });

  it('filters by agent through the handler', async () => {
    const handler = makeHandler();
    await render(handler);
    const select = container.querySelector<HTMLSelectElement>('select[aria-label="Filter by agent"]')!;
    await act(async () => {
      select.value = 'mcp';
      select.dispatchEvent(new Event('change', {bubbles: true}));
    });
    await flush();
    expect(handler.getAgentActionJournal).toHaveBeenLastCalledWith('mcp', '', '');
    expect(rows()).toHaveLength(1);
    expect(rows()[0]!.textContent).toContain('Mail sent');
  });

  it('clears the journal', async () => {
    const handler = makeHandler();
    await render(handler);
    const clear = container.querySelector<HTMLButtonElement>('button[aria-label="Clear agent activity"]')!;
    expect(clear.disabled).toBe(false);
    await act(async () => {
      clear.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await flush();
    expect(handler.clearAgentActionJournal).toHaveBeenCalledTimes(1);
    expect(rows()).toHaveLength(0);
    expect(container.querySelector('[data-agent-activity-empty="true"]')).not.toBeNull();
  });

  it('shows an error state when the journal cannot load', async () => {
    const handler = makeHandler();
    handler.getAgentActionJournal.mockRejectedValueOnce(new Error('boom'));
    await render(handler);
    expect(container.querySelector('[role="alert"]')?.textContent).toContain('unavailable');
  });

  it('cancels queued agent mail inside the undo window', async () => {
    let queue = [
      {id: 'q1', account_id: 'a', subject: 'Invoice', to: ['bob@example.test'],
       send_at_ms: 1_790_000_010_000, status: 'pending'},
    ];
    const cancelQueuedAgentMail = vi.fn(async (id: string) => {
      const before = queue.length;
      queue = queue.filter(item => item.id !== id);
      return {cancelled: queue.length < before};
    });
    const handler = {
      ...makeHandler(),
      listQueuedAgentMail: vi.fn(async () => ({success: true, queueJson: JSON.stringify(queue)})),
      cancelQueuedAgentMail,
    } satisfies AgentActivityHandler;
    await render(handler);
    const queued = container.querySelectorAll('[data-agent-mail-queued-row="true"]');
    expect(queued).toHaveLength(1);
    expect(queued[0]!.textContent).toContain('Invoice');
    const cancel = container.querySelector<HTMLButtonElement>(
        'button[aria-label="Cancel sending Invoice"]')!;
    await act(async () => {
      cancel.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await flush();
    expect(cancelQueuedAgentMail).toHaveBeenCalledWith('q1');
    expect(container.querySelectorAll('[data-agent-mail-queued-row="true"]')).toHaveLength(0);
    expect(container.querySelector('[role="status"]')?.textContent).toContain('not sent');
  });

  it('hides the queue section when nothing is held', async () => {
    const handler = {
      ...makeHandler(),
      listQueuedAgentMail: vi.fn(async () => ({success: true, queueJson: '[]'})),
      cancelQueuedAgentMail: vi.fn(async () => ({cancelled: false})),
    } satisfies AgentActivityHandler;
    await render(handler);
    expect(container.querySelector('[data-agent-mail-queue="true"]')).toBeNull();
  });
});
