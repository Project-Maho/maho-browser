import {useEffect} from 'react';
import {MahoSettingsStore} from './store.js';
import {MojoKeyCombo} from '../maho_settings.mojom-webui.js';

export function useShortcutInterceptor(
    store: MahoSettingsStore,
    active: boolean,
    onRecord: (keyCombo: MojoKeyCombo) => void
) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();

  useEffect(() => {
    if (!active) {
      return;
    }

    void handler.setRecordingMode(true);

    const listener = (keyCombo: MojoKeyCombo) => {
      onRecord(keyCombo);
    };
    router.onShortcutRecorded.addListener(listener);

    return () => {
      void handler.setRecordingMode(false);
      router.onShortcutRecorded.removeListener(listener);
    };
  }, [handler, router, active, onRecord]);
}
