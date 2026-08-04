import {MahoAiStore, createMahoAiStore} from '../store.js';

export async function bootstrapStore(store: MahoAiStore): Promise<void> {
  await store.bootstrap();
}

export {createMahoAiStore};
