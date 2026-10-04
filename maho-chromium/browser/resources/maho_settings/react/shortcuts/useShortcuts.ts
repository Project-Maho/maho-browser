import {useCallback, useEffect, useState} from 'react';
import {MahoSettingsStore} from '../store.js';
import {MojoKeyCombo, ShortcutBinding} from '../../mojo.js';

export function useShortcuts(store: MahoSettingsStore) {
  const handler = store.getHandler();
  const [shortcuts, setShortcuts] = useState<ShortcutBinding[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadShortcuts = useCallback(async () => {
    setLoading(true);
    try {
      const {shortcuts: list} = await handler.getShortcuts();
      setShortcuts(list);
      setError(null);
    } catch {
      setError('Failed to load shortcuts.');
    } finally {
      setLoading(false);
    }
  }, [handler]);

  useEffect(() => {
    void loadShortcuts();
  }, [loadShortcuts]);

  const setShortcut = useCallback(async (action: string, keyCombo: MojoKeyCombo) => {
    const res = await handler.setShortcut(action, keyCombo);
    await loadShortcuts();
    return res;
  }, [handler, loadShortcuts]);

  const resetShortcut = useCallback(async (action: string) => {
    await handler.resetShortcut(action);
    await loadShortcuts();
  }, [handler, loadShortcuts]);

  const resetAllShortcuts = useCallback(async () => {
    await handler.resetAllShortcuts();
    await loadShortcuts();
  }, [handler, loadShortcuts]);

  const toggleShortcut = useCallback(async (action: string, enabled: boolean) => {
    await handler.toggleShortcut(action, enabled);
    await loadShortcuts();
  }, [handler, loadShortcuts]);

  const checkShortcutConflict = useCallback(async (keyCombo: MojoKeyCombo) => {
    return await handler.checkShortcutConflict(keyCombo);
  }, [handler]);

  const exportShortcuts = useCallback(async () => {
    return await handler.exportShortcuts();
  }, [handler]);

  const importShortcuts = useCallback(async (json: string) => {
    const res = await handler.importShortcuts(json);
    await loadShortcuts();
    return res;
  }, [handler, loadShortcuts]);

  return {
    shortcuts,
    loading,
    error,
    reload: loadShortcuts,
    setShortcut,
    resetShortcut,
    resetAllShortcuts,
    toggleShortcut,
    checkShortcutConflict,
    exportShortcuts,
    importShortcuts,
  };
}
