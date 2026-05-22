import type {CanvasItem} from '../maho_easel.mojom-webui.js';

export interface EaselState {
  readonly items: readonly CanvasItem[];
  readonly isAdding: boolean;
  readonly errorMessage: string | null;
}

export function createInitialState(): EaselState {
  return {
    items: [],
    isAdding: false,
    errorMessage: null,
  };
}

export function cloneState(state: EaselState): EaselState {
  return {
    items: state.items.map(item => ({...item})),
    isAdding: state.isAdding,
    errorMessage: state.errorMessage,
  };
}
