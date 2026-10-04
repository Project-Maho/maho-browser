import {useSyncExternalStore} from 'react';

import type {MahoAiStore} from '../../store.js';
import type {AppState} from '../../types.js';

export function useAppState(store: MahoAiStore): AppState {
  return useSyncExternalStore(
      listener => store.subscribe(listener),
      () => store.getSnapshot());
}
