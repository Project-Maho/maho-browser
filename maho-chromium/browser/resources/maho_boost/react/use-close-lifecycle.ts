import * as React from 'react';

import {WindowMode} from '../maho_boost.mojom-webui.js';

export interface CloseLifecycleOptions {
  readonly close: (closeDialog: boolean) => Promise<void>;
  readonly closedRef: React.MutableRefObject<boolean>;
  readonly mode: WindowMode;
}

export function useCloseLifecycle({
  close,
  closedRef,
  mode,
}: CloseLifecycleOptions): void {
  React.useEffect(() => {
    const onUnload = (): void => {
      if (closedRef.current) {
        return;
      }
      void close(false);
    };
    window.addEventListener('unload', onUnload, {once: true});
    return () => window.removeEventListener('unload', onUnload);
  }, [close, closedRef]);

  React.useEffect(() => {
    const onKeyDown = (event: KeyboardEvent): void => {
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'w') {
        event.preventDefault();
        void close(true);
        return;
      }
      if (event.key !== 'Escape' || event.defaultPrevented) {
        return;
      }
      if (mode === WindowMode.kCode && event.target instanceof Element && event.target.closest('.cm-editor')) {
        return;
      }
      event.preventDefault();
      void close(true);
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [close, mode]);
}
