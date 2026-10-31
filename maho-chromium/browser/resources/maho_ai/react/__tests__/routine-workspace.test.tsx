import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../../maho_ai.mojom-webui.js';
import {
  RoutineOperationsClient,
} from '../../routine-client.js';
import {
  mergeRoutineStatuses,
  RoutineWorkspace,
} from '../features/compact/routine-workspace.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function setInputValue(
    element: HTMLInputElement|HTMLTextAreaElement, value: string) {
  const prototype = element instanceof HTMLTextAreaElement ?
      HTMLTextAreaElement.prototype :
      HTMLInputElement.prototype;
  Object.getOwnPropertyDescriptor(prototype, 'value')?.set?.call(element, value);
  element.dispatchEvent(new Event('input', {bubbles: true}));
}

describe('RoutineWorkspace', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.append(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    document.documentElement.classList.remove('dark');
    container.remove();
  });

  it('shows eligibility failure and retries the eligibility lookup', async () => {
    const client = {
      isEligible: vi.fn().mockRejectedValueOnce(new Error('Disconnected')).mockResolvedValue(true),
      list: vi.fn().mockResolvedValue([]), statuses: vi.fn().mockResolvedValue([]),
      history: vi.fn().mockResolvedValue([]), subscribe: vi.fn().mockReturnValue(() => {}),
    } as unknown as RoutineOperationsClient;
    await act(async () => root.render(<RoutineWorkspace client={client} />));
    expect.soft(container.querySelector('[role="alert"]')).not.toBeNull();
    expect.soft(container.querySelector('[aria-busy="true"]')).toBeNull();
    const retry = container.querySelector<HTMLButtonElement>('[data-routine-retry]');
    expect.soft(retry).not.toBeNull();
    await act(async () => retry?.click());
    expect.soft(client.isEligible).toHaveBeenCalledTimes(2);
    expect.soft(container.querySelector('form')).not.toBeNull();
  });

  it.each(['Approve', 'Deny'])('shows rejection of a routine %s decision', async action => {
    const client = {
      isEligible: vi.fn().mockResolvedValue(true),
      list: vi.fn().mockResolvedValue([{id: 'daily', name: 'Daily', schedule: null, trigger: null}]),
      statuses: vi.fn().mockResolvedValue([{runId: 'run-1', routineId: 'daily', state: 'awaiting_approval', revision: 1n, approvalId: 'approval-1'}]),
      history: vi.fn().mockResolvedValue([]), subscribe: vi.fn().mockReturnValue(() => {}),
      approve: vi.fn().mockResolvedValue(false),
    } as unknown as RoutineOperationsClient;
    await act(async () => root.render(<RoutineWorkspace client={client} />));
    const button = [...container.querySelectorAll('button')].find(button => button.textContent === action)!;
    await act(async () => button.click());
    expect(client.approve).toHaveBeenCalledWith('run-1', 'approval-1', action === 'Approve');
    expect(container.querySelector('[role="alert"]')).not.toBeNull();
    expect(button.disabled).toBe(false);
  });

  it('keeps a newer pushed status when an older refresh arrives', () => {
    const pushed = {
      runId: 'run-1',
      routineId: 'daily',
      source: 'manual' as const,
      state: 'succeeded' as const,
      revision: 3n,
      result: null,
      error: null,
      approvalId: null,
    };
    const staleRefresh = {
      ...pushed,
      state: 'awaiting_approval' as const,
      revision: 2n,
    };

    expect(mergeRoutineStatuses([pushed], [staleRefresh])).toEqual([pushed]);
  });

  it('renders routines, status, run, and approval actions', async () => {
    let resolveEligible!: (eligible: boolean) => void;
    const client = {
      list: vi.fn().mockResolvedValue([{
        id: 'daily',
        name: 'Daily review',
        cron: '',
        description: '',
        isCustom: true,
        schedule: '0 9 * * *',
        trigger: null,
        enabled: true,
      }]),
      isEligible: vi.fn().mockReturnValue(new Promise<boolean>(resolve => {
        resolveEligible = resolve;
      })),
      history: vi.fn().mockResolvedValue([{resultId: 1n}]),
      create: vi.fn().mockResolvedValue(true),
      statuses: vi.fn().mockResolvedValue([{
        runId: 'run-1',
        routineId: 'daily',
        source: 'manual',
        state: 'awaiting_approval',
        revision: 2n,
        result: null,
        error: null,
        approvalId: 'approval-1',
      }]),
      run: vi.fn().mockResolvedValue('run-2'),
      approve: vi.fn().mockResolvedValue(true),
      subscribe: vi.fn().mockReturnValue(() => {}),
    } as unknown as RoutineOperationsClient;

    await act(async () => {
      root.render(<RoutineWorkspace client={client} />);
    });
    const rendered = new Promise<void>((resolve, reject) => {
      const timeout = window.setTimeout(
          () => reject(new Error('Routine list did not render')), 1000);
      const observer = new MutationObserver(() => {
        if (container.textContent?.includes('Daily review')) {
          window.clearTimeout(timeout);
          observer.disconnect();
          resolve();
        }
      });
      observer.observe(container, {childList: true, subtree: true});
    });
    await act(async () => resolveEligible(true));
    await rendered;

    expect(container.textContent).toContain('Daily review');
    expect(container.textContent).toContain('Awaiting approval');
    expect(container.textContent).not.toContain('awaiting_approval');
    const status = container.querySelector(
        '[data-routine-status="awaiting_approval"]');
    expect(status?.getAttribute('role')).toBe('status');
    expect(status?.className).toContain('bg-warning');

    const buttons = [...container.querySelectorAll('button')];
    await act(async () => buttons.find(button => button.textContent === 'Run')?.click());
    expect(client.run).toHaveBeenCalledWith('daily');
    expect(client.statuses).toHaveBeenCalledTimes(2);

    await act(async () => {
      buttons.find(button => button.textContent === 'Approve')?.click();
    });
    expect(client.approve).toHaveBeenCalledWith(
        'run-1', 'approval-1', true);
    expect(client.statuses).toHaveBeenCalledTimes(3);
  });

  it('cold-opens the real standalone binding into an interactive workspace', async () => {
    const router = new PageCallbackRouter();
    const handler = new PageHandlerRemote();
    PageHandlerFactory.getRemote().createPageHandler(
        router.$.bindNewPipeAndPassRemote(),
        handler.$.bindNewPipeAndPassReceiver());
    const client = new RoutineOperationsClient(handler as never, router);

    await act(async () => {
      root.render(<RoutineWorkspace client={client} />);
    });

    expect(container.querySelector(
        'button[aria-label="Back to current chat"]')).not.toBeNull();
    expect(container.textContent).toContain('Create routine');
    expect(container.textContent).toContain('Morning briefing');
    expect(container.querySelector('[aria-busy="true"]')).toBeNull();
  });

  it('renders loading and allows first Routine creation with a schedule', async () => {
    let resolveEligibility: ((eligible: boolean) => void)|undefined;
    const client = {
      isEligible: vi.fn().mockImplementation(
          () => new Promise<boolean>(resolve => {
            resolveEligibility = resolve;
          })),
      list: vi.fn().mockResolvedValue([]),
      statuses: vi.fn().mockResolvedValue([]),
      history: vi.fn().mockResolvedValue([]),
      create: vi.fn().mockResolvedValue(true),
      run: vi.fn(),
      approve: vi.fn(),
      subscribe: vi.fn().mockReturnValue(() => {}),
    } as unknown as RoutineOperationsClient;

    await act(async () => {
      root.render(<RoutineWorkspace client={client} />);
    });
    expect(container.textContent).toContain('Loading routines');

    await act(async () => resolveEligibility?.(true));
    expect(container.textContent).toContain('Create routine');
    const inputs = [...container.querySelectorAll('input')];
    await act(async () => {
      const name = inputs.find(input => input.getAttribute('aria-label') === 'Routine name');
      const prompt = container.querySelector<HTMLTextAreaElement>(
          '[aria-label="Routine prompt"]');
      setInputValue(name!, 'Daily review');
      setInputValue(prompt!, 'Review open tabs');
      [...container.querySelectorAll('button')]
          .find(button => button.textContent === 'Schedule')
          ?.click();
    });
    const scheduleMode = [...container.querySelectorAll('button')]
                             .find(button => button.textContent === 'Schedule');
    expect(scheduleMode?.getAttribute('data-state')).toBe('active');
    expect(scheduleMode?.className).toContain('bg-primary');
    const schedule = container.querySelector<HTMLInputElement>(
        '[aria-label="Cron schedule"]');
    await act(async () => {
      setInputValue(schedule!, '0 9 * * *');
      container.querySelector<HTMLFormElement>('form')!.requestSubmit();
    });
    expect(client.create).toHaveBeenCalledWith(
        'Daily review', 'Review open tabs', '0 9 * * *', null);
  });

  it('creates event-triggered routines in a narrow dark panel', async () => {
    Object.defineProperty(window, 'innerWidth', {
      configurable: true,
      value: 360,
    });
    document.documentElement.classList.add('dark');
    const client = {
      isEligible: vi.fn().mockResolvedValue(true),
      list: vi.fn().mockResolvedValue([]),
      statuses: vi.fn().mockResolvedValue([]),
      history: vi.fn().mockResolvedValue([]),
      create: vi.fn().mockResolvedValue(true),
      run: vi.fn(),
      approve: vi.fn(),
      subscribe: vi.fn().mockReturnValue(() => {}),
    } as unknown as RoutineOperationsClient;

    await act(async () => {
      root.render(<RoutineWorkspace client={client} />);
    });
    const buttons = [...container.querySelectorAll('button')];
    await act(async () => {
      buttons.find(button => button.textContent === 'Event')?.click();
    });
    expect(container.querySelector('[aria-label="Cron schedule"]')).toBeNull();
    const eventTrigger = container.querySelector<HTMLInputElement>(
        '[aria-label="Event trigger"]');
    expect(eventTrigger).not.toBeNull();
    expect(container.querySelector('section')?.className)
        .toContain('overflow-auto');
    expect(container.querySelector('form')?.className).toContain('grid');
    expect(document.documentElement.classList.contains('dark')).toBe(true);
  });

  it('returns to the current chat without mutating Routine state', async () => {
    const onBackToChat = vi.fn();
    const client = {
      isEligible: vi.fn().mockResolvedValue(true),
      list: vi.fn().mockResolvedValue([]),
      statuses: vi.fn().mockResolvedValue([]),
      history: vi.fn().mockResolvedValue([]),
      create: vi.fn(),
      run: vi.fn(),
      approve: vi.fn(),
      subscribe: vi.fn().mockReturnValue(() => {}),
    } as unknown as RoutineOperationsClient;
    await act(async () => {
      root.render(
          <RoutineWorkspace
            client={client}
            onBackToChat={onBackToChat}
          />);
    });

    const backButton = container.querySelector<HTMLButtonElement>(
        'button[aria-label="Back to current chat"]');
    expect(backButton).not.toBeNull();
    await act(async () => {
      backButton?.click();
    });

    expect(onBackToChat).toHaveBeenCalledTimes(1);
    expect(client.create).not.toHaveBeenCalled();
    expect(client.run).not.toHaveBeenCalled();
  });
});
