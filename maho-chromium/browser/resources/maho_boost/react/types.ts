import type {SiteRule} from '../maho_boost.mojom-webui.js';

export type StatusTone = 'neutral' | 'error' | 'success';

export interface BoostState {
  rule: SiteRule | null;
  loading: boolean;
  busy: boolean;
  status: string;
  statusTone: StatusTone;
  cssDraft: string;
}

export function createInitialState(): BoostState {
  return {
    rule: null,
    loading: true,
    busy: false,
    status: '',
    statusTone: 'neutral',
    cssDraft: '',
  };
}

export function cloneState(s: BoostState): BoostState {
  return {
    ...s,
    rule: s.rule ? {...s.rule} : null,
  };
}
