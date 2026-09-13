import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {SyncKeyBackupContent} from '../pages/sync-key-backup.js';
import {MahoWelcomeStore} from '../store.js';
import {createInitialState} from '../types.js';

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('sync key save completion', () => {
  it('does not claim a backup was saved without a confirmed file write', async () => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true);
    const store = new MahoWelcomeStore();
    const snapshot = {
      ...createInitialState(),
      syncKeyBackup: {
        ...createInitialState().syncKeyBackup,
        status: 'ready' as const,
        roomId: 'test-room',
        syncKey: 'test-only-key',
        recoveryPhrase: 'test only recovery phrase',
      },
    };
    const handlers: {save: (() => Promise<void>) | null} = {save: null};
    const container = document.createElement('div');
    document.body.append(container);
    const root = createRoot(container);
    vi.stubGlobal('URL', class extends URL {
      static createObjectURL() { return 'blob:test-only'; }
      static revokeObjectURL() {}
    });
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
    try {
      await act(async () => {
        root.render(
          <SyncKeyBackupContent
            store={store}
            snapshot={snapshot}
            onRegisterSaveHandler={handler => { handlers.save = handler; }}
          />,
        );
      });
      const save = handlers.save;
      if (!save) throw new Error('Save handler was not registered');
      await act(async () => {
        await save();
      });
      expect(store.getSnapshot().syncKeyBackup.savedToFile).toBe(false);
    } finally {
      await act(async () => root.unmount());
      container.remove();
      store.dispose();
    }
  });

  it.each(['saved', 'cancelled', 'failed'] as const)(
      'waits for native write outcome %s and rejects duplicate saves', async outcome => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true);
    const store = new MahoWelcomeStore();
    const snapshot = {
      ...createInitialState(),
      syncKeyBackup: {
        ...createInitialState().syncKeyBackup,
        status: 'ready' as const,
        roomId: 'fixture-room',
        syncKey: 'fixture-raw-key-not-for-export',
        recoveryPhrase: 'test only recovery phrase',
      },
    };
    const completion: {
      resolve: ((value: boolean) => void) | null;
      reject: ((reason: Error) => void) | null;
    } = {resolve: null, reject: null};
    const completionPromise = new Promise<boolean>((resolve, reject) => {
      completion.resolve = resolve;
      completion.reject = reject;
    });
    const nativeSave = vi.spyOn(store, 'saveSyncKeyBackup')
        .mockReturnValueOnce(completionPromise)
        .mockResolvedValueOnce(true);
    const handlers: {save: (() => Promise<void>) | null} = {save: null};
    const container = document.createElement('div');
    const root = createRoot(container);
    try {
      await act(async () => {
        root.render(<SyncKeyBackupContent store={store} snapshot={snapshot}
          onRegisterSaveHandler={handler => { handlers.save = handler; }} />);
      });
      const save = handlers.save;
      if (!save) throw new Error('Save handler was not registered');
      await act(async () => {
        const pending = save();
        await save();
        expect(nativeSave).toHaveBeenCalledTimes(1);
        const backup = nativeSave.mock.calls[0]?.[0];
        expect(backup?.match(/^Room ID: (.+)$/m)?.[1])
            .toBe(snapshot.syncKeyBackup.roomId);
        expect(backup?.match(/^Recovery Phrase:\n([^\n]+)$/m)?.[1])
            .toBe(snapshot.syncKeyBackup.recoveryPhrase);
        expect(backup).not.toContain(snapshot.syncKeyBackup.syncKey);
        expect(store.getSnapshot().syncKeyBackup.savedToFile).toBe(false);
        if (!completion.resolve || !completion.reject) {
          throw new Error('Save completion was not registered');
        }
        if (outcome === 'failed') {
          completion.reject(new Error('fixture_write_failed'));
        } else {
          completion.resolve(outcome === 'saved');
        }
        await pending;
      });
      const saved = outcome === 'saved';
      expect(store.getSnapshot().syncKeyBackup.savedToFile).toBe(saved);
      if (outcome === 'failed') {
        expect(container.textContent).toContain('fixture_write_failed');
      }
      if (!saved) {
        const retry = handlers.save;
        if (!retry) throw new Error('Retry handler was not registered');
        await act(async () => { await retry(); });
        expect(nativeSave).toHaveBeenCalledTimes(2);
        expect(store.getSnapshot().syncKeyBackup.savedToFile).toBe(true);
      }
    } finally {
      await act(async () => root.unmount());
      container.remove();
      store.dispose();
    }
  });
});
