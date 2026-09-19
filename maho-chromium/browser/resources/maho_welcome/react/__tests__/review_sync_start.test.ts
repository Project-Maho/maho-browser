import {readFileSync} from 'node:fs';
import {expect, it, vi} from 'vitest';
import {MahoWelcomeStore} from '../store';
import {createInitialState, WelcomePage} from '../types';

it('preserves the recovery page and fields when starting sync fails', async () => {
  const store = new MahoWelcomeStore();
  const snapshot = {...createInitialState(), currentPage: WelcomePage.SyncKeyBackup,
    syncKeyBackup: {status: 'saved', savedToFile: true, roomId: 'room', recoveryPhrase: 'phrase', syncKey: 'key', errorMessage: ''}};
  Object.assign(store, {state: snapshot, pageHandler: {$: {}, startSync: vi.fn().mockResolvedValue({ok: false, errorMessage: 'native_start_failed'})}});
  const source = readFileSync(new URL('../app.tsx', import.meta.url), 'utf8');
  const start = source.indexOf('  async function onStartSync()');
  const callback = source.slice(start, source.indexOf('\n  const primaryLabel', start));
  const run = new Function('store', 'snapshot', callback + '\nreturn onStartSync();');
  try {
    await run(store, snapshot);
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
    expect(store.getSnapshot().syncKeyBackup.recoveryPhrase).toBe('phrase');
    expect(store.getSnapshot().syncKeyBackup.errorMessage).toBe('native_start_failed');
  } finally {store.dispose();}
});
