import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {RoutineOperationsClient} from '../../routine-client.js';
import {RoutineWorkspace} from '../features/compact/routine-workspace.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function makeClient(overrides: Partial<RoutineOperationsClient> = {}):
    RoutineOperationsClient {
  return {
    list: vi.fn().mockResolvedValue([
      {
        id: 'daily',
        name: 'Daily review',
        cron: '',
        description: '',
        isCustom: true,
        schedule: '0 9 * * *',
        trigger: null,
        enabled: true,
      },
    ]),
    isEligible: vi.fn().mockResolvedValue(true),
    history: vi.fn().mockResolvedValue([
      {
        resultId: 1n,
        routineId: 'daily',
        ranAt: 10n,
        success: true,
        content: 'Report ready',
        source: 'schedule',
      },
    ]),
    statuses: vi.fn().mockResolvedValue([]),
    create: vi.fn().mockResolvedValue(true),
    run: vi.fn().mockResolvedValue('run-2'),
    approve: vi.fn().mockResolvedValue(true),
    subscribe: vi.fn().mockReturnValue(() => {}),
    ...overrides,
  } as unknown as RoutineOperationsClient;
}

describe('RoutineWorkspace — humanized presentation', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.append(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('shows routine names and humanized run sources in the history rows',
      async () => {
        const client = makeClient();
        await act(async () => {
          root.render(<RoutineWorkspace client={client} />);
        });
        await act(async () => {});

        const row = container.querySelector('[data-routine-history-run]');
        expect(row).not.toBeNull();
        // The routine NAME is shown, not the raw routine id...
        expect(row?.textContent).toContain('Daily review');
        expect(row?.textContent).not.toContain('routine-daily');
        // ...and the run source is a human phrase, not a raw token.
        expect(row?.textContent).toContain('Scheduled run');
        expect(row?.textContent).not.toContain('schedule');
      });

  it('shows a humanized error with a Retry action instead of a raw reason dump',
      async () => {
        const client = makeClient({
          list: vi.fn().mockRejectedValue(new Error('mojo pipe closed')),
        });
        await act(async () => {
          root.render(<RoutineWorkspace client={client} />);
        });
        await act(async () => {});

        const alert = container.querySelector('[data-routine-error]');
        expect(alert).not.toBeNull();
        expect(alert?.textContent).toContain('could not be loaded');
        expect(alert?.textContent).not.toContain('mojo pipe closed');

        const retry = container.querySelector<HTMLButtonElement>(
            '[data-routine-retry]');
        expect(retry).not.toBeNull();
        (client.list as ReturnType<typeof vi.fn>).mockClear();
        (client.list as ReturnType<typeof vi.fn>)
            .mockResolvedValueOnce([]);
        await act(async () => {
          retry!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });
        expect(client.list).toHaveBeenCalled();
      });
});
