// Copyright 2026 Maho Browser. All rights reserved.

import { beforeEach, describe, expect, it, vi } from 'vitest';

const mojoMocks = vi.hoisted(() => {
  const listeners: Record<string, Function[]> = {};
  const add = (name: string, fn: Function) => {
    listeners[name] = listeners[name] || [];
    listeners[name].push(fn);
    return listeners[name].length;
  };
  const fire = (name: string, ...args: unknown[]) => {
    for (const fn of listeners[name] || []) {
      fn(...args);
    }
  };
  return {
    handler: {
      googleCalendarFreeBusy: vi.fn(),
      googleCalendarMoveEvent: vi.fn(),
    },
    callbackRouter: {
      onAccountsChanged: { addListener: vi.fn((fn: Function) => add('onAccountsChanged', fn)) },
      onAuthRequired: { addListener: vi.fn((fn: Function) => add('onAuthRequired', fn)) },
      onAuthRefreshSucceeded: { addListener: vi.fn((fn: Function) => add('onAuthRefreshSucceeded', fn)) },
      onStatusChanged: { addListener: vi.fn((fn: Function) => add('onStatusChanged', fn)) },
      onNewMail: { addListener: vi.fn((fn: Function) => add('onNewMail', fn)) },
      onMutation: { addListener: vi.fn((fn: Function) => add('onMutation', fn)) },
      onCalendar: { addListener: vi.fn((fn: Function) => add('onCalendar', fn)) },
      onAgentStream: { addListener: vi.fn((fn: Function) => add('onAgentStream', fn)) },
      removeListener: vi.fn(),
    },
    fire,
  };
});

vi.mock('../mojo_client.js', () => ({
  handler: mojoMocks.handler,
  callbackRouter: mojoMocks.callbackRouter,
}));

import * as api from '../api/index';
import { realListen } from '../events';

describe('events and api typing seams', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('verifies googleCalendarFreeBusy calls Mojo and returns typed response', async () => {
    const mockFreeBusy = {
      kind: 'calendar#freeBusy',
      timeMin: '2026-09-02T00:00:00Z',
      timeMax: '2026-09-02T23:59:59Z',
      calendars: {
        'test@example.com': {
          busy: [{ start: '2026-09-02T10:00:00Z', end: '2026-09-02T11:00:00Z' }],
        },
      },
    };
    mojoMocks.handler.googleCalendarFreeBusy.mockResolvedValueOnce({
      ok: true,
      resultJson: JSON.stringify(mockFreeBusy),
    });

    const result = await api.googleCalendarFreeBusy(
      'acc1',
      ['test@example.com'],
      '2026-09-02T00:00:00Z',
      '2026-09-02T23:59:59Z',
    );

    expect(result).toEqual(mockFreeBusy);
    expect(result.calendars?.['test@example.com']?.busy[0].start).toBe('2026-09-02T10:00:00Z');
  });

  it('verifies googleCalendarMoveEvent calls Mojo and returns typed CalendarEvent', async () => {
    const mockEvent = {
      id: 'event1',
      account_id: 'acc1',
      summary: 'Moved Meeting',
      dtstart: '2026-09-02T10:00:00Z',
      all_day: false,
      created_at: '2026-09-02T00:00:00Z',
      updated_at: '2026-09-02T00:00:00Z',
      status: 'confirmed',
    };
    mojoMocks.handler.googleCalendarMoveEvent.mockResolvedValueOnce({
      ok: true,
      resultJson: JSON.stringify(mockEvent),
    });

    const result = await api.googleCalendarMoveEvent('acc1', 'cal1', 'event1', 'cal2');
    expect(result).toEqual(mockEvent);
    expect(result.summary).toBe('Moved Meeting');
  });

  it('dispatches typed event payload for auth-reauth-required', async () => {
    const received: unknown[] = [];
    const unlisten = await realListen<{ account_id: string; provider: string; reason: string }>(
      'auth-reauth-required',
      (ev) => {
        received.push(ev);
      },
    );

    mojoMocks.fire('onAuthRequired', 'acc123', 'gmail', 'invalid_grant');

    expect(received).toEqual([
      {
        event: 'auth-reauth-required',
        payload: {
          account_id: 'acc123',
          provider: 'gmail',
          reason: 'invalid_grant',
        },
      },
    ]);

    unlisten();
    expect(mojoMocks.callbackRouter.removeListener).toHaveBeenCalledWith(1);
  });

  it('dispatches typed event payload for status-changed', async () => {
    const received: unknown[] = [];
    await realListen<{ account_id: string; connected: boolean }>(
      'status-changed',
      (ev) => {
        received.push(ev);
      },
    );

    mojoMocks.fire('onStatusChanged', 'acc123', true);

    expect(received).toEqual([
      {
        event: 'status-changed',
        payload: {
          account_id: 'acc123',
          connected: true,
        },
      },
    ]);
  });

  it('dispatches typed event payload for new-mail and idle:new-mail', async () => {
    const receivedNewMail: unknown[] = [];
    const receivedIdle: unknown[] = [];

    await realListen<{ account_id: string; message_ids: string[] }>(
      'new-mail',
      (ev) => {
        receivedNewMail.push(ev);
      },
    );
    await realListen<{ account_id: string; exists: number }>(
      'idle:new-mail',
      (ev) => {
        receivedIdle.push(ev);
      },
    );

    mojoMocks.fire('onNewMail', 'acc456');

    expect(receivedNewMail).toEqual([
      {
        event: 'new-mail',
        payload: {
          account_id: 'acc456',
          message_ids: [],
        },
      },
    ]);
    expect(receivedIdle).toEqual([
      {
        event: 'idle:new-mail',
        payload: {
          account_id: 'acc456',
          exists: 1,
        },
      },
    ]);
  });

  it('dispatches agent stream chunks and objects correctly', async () => {
    const received: unknown[] = [];
    await realListen<{ session_id: string; kind?: string; payload?: unknown }>(
      'agent-message-chunk',
      (ev) => {
        received.push(ev);
      },
    );

    mojoMocks.fire(
      'onAgentStream',
      'sess1',
      JSON.stringify({ kind: 'token', payload: { delta: 'hello' } }),
    );

    expect(received).toEqual([
      {
        event: 'agent-message-chunk',
        payload: {
          session_id: 'sess1',
          kind: 'token',
          payload: { delta: 'hello' },
        },
      },
    ]);
  });
});
