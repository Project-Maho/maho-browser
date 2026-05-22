import {loadTimeData} from 'chrome://resources/js/load_time_data.js';

import {MahoAiStore, createMahoAiStore} from '../store.js';

export function readPendingAiQuery(): string {
  try {
    return loadTimeData.getString('pendingAiQuery').trim();
  } catch {
    return '';
  }
}

export async function bootstrapStore(store: MahoAiStore): Promise<void> {
  await store.bootstrap(readPendingAiQuery() || undefined);
}

export {createMahoAiStore};
