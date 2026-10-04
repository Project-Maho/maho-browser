import {describe, expect, it} from 'vitest';

import {
  acceptMailLifecycle,
  lifecycleClearsSensitiveState,
} from './mail_lifecycle';

describe('Mail lifecycle contract', () => {
  it('rejects stale generations after re-enable', () => {
    const current = {state: 'ready' as const, generation: 4n};
    expect(acceptMailLifecycle(current, 'disabled', 3n)).toBeNull();
    expect(acceptMailLifecycle(current, 'ready', 5n)).toEqual({
      state: 'ready',
      generation: 5n,
    });
  });

  it.each(['disabled', 'draining', 'stopped', 'failed'] as const)(
    'clears sensitive state for %s',
    (state) => {
      expect(lifecycleClearsSensitiveState(state)).toBe(true);
    },
  );

  it('keeps state while a fresh helper starts', () => {
    expect(lifecycleClearsSensitiveState('starting')).toBe(false);
  });

  it('rejects unknown lifecycle values', () => {
    expect(
      acceptMailLifecycle(
        {state: 'starting', generation: 1n},
        'unknown',
        2n,
      ),
    ).toBeNull();
  });
});
