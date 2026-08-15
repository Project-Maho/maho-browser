export type MailLifecycleState =
  | 'disabled'
  | 'starting'
  | 'ready'
  | 'draining'
  | 'stopped'
  | 'failed';

export interface MailLifecycleSnapshot {
  readonly state: MailLifecycleState;
  readonly generation: bigint;
}

export function acceptMailLifecycle(
  current: MailLifecycleSnapshot,
  state: string,
  generation: bigint,
): MailLifecycleSnapshot | null {
  if (generation < current.generation) return null;
  if (
    state !== 'disabled' &&
    state !== 'starting' &&
    state !== 'ready' &&
    state !== 'draining' &&
    state !== 'stopped' &&
    state !== 'failed'
  ) {
    return null;
  }
  return {state, generation};
}

export function lifecycleClearsSensitiveState(state: MailLifecycleState) {
  return state !== 'ready' && state !== 'starting';
}
