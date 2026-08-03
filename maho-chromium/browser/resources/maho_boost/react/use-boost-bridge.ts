import * as React from 'react';

import {applyTheme, watchAutoTheme} from '../../maho_common/react/theme/apply_theme.js';
import type {BoostInfo, PageObserver} from '../maho_boost.mojom-webui.js';
import {BoostBridge} from './boost-bridge.js';
import {createBoostUpdate, type BoostAction} from './boost-state.js';
import type {BoostUpdateKey} from './use-debounced-boost-updates.js';

export interface BoostBridgeRefs {
  readonly boostRef: React.MutableRefObject<BoostInfo | null>;
  readonly bridgeRef: React.MutableRefObject<BoostBridge | null>;
  readonly closedRef: React.MutableRefObject<boolean>;
}

export interface BoostBridgeCallbacks {
  readonly applyUpdate: (key: BoostUpdateKey, update: ReturnType<typeof createBoostUpdate>) => void;
  readonly acceptActiveBoostId: (boostId: string | null) => void;
  readonly bootstrap: (bridge: BoostBridge, domain: string, fonts: string[]) => Promise<void>;
  readonly onEditorKilled: () => Promise<void>;
  readonly reduce: (action: BoostAction) => void;
  readonly refreshBoostList: (bridge: BoostBridge) => Promise<void>;
  readonly refreshSelectedBoost: (bridge: BoostBridge) => Promise<void>;
  readonly reportError: (error: unknown, fallback: string) => void;
  readonly setEditorFocusRequest: React.Dispatch<React.SetStateAction<number>>;
}

export function useBoostBridge(refs: BoostBridgeRefs, callbacks: BoostBridgeCallbacks): void {
  React.useEffect(() => {
    applyTheme('auto');
    const stopWatchingTheme = watchAutoTheme(() => applyTheme('auto'));
    const bridge = BoostBridge.connect();
    refs.bridgeRef.current = bridge;
    let disposed = false;
    const observer: PageObserver = {
      onBoostsChanged: () => {
        void callbacks.refreshBoostList(bridge).catch(
            error => callbacks.reportError(error, 'Boost failed to refresh.'));
      },
      onActiveChanged: boostId => {
        if (!disposed && !refs.closedRef.current) {
          callbacks.acceptActiveBoostId(boostId);
        }
      },
      onZapStateUpdate: (isOn, anyZapped) => {
        if (!disposed && !refs.closedRef.current) {
          callbacks.reduce({type: 'set-zap-mode', isOn, anyZapped});
        }
      },
      onZapListUpdate: () => {
        void callbacks.refreshSelectedBoost(bridge).catch(
            error => callbacks.reportError(error, 'Boost failed to refresh.'));
      },
      onPickerStateUpdate: isOn => {
        if (!disposed && !refs.closedRef.current) {
          callbacks.reduce({type: 'set-picker-mode', isOn});
        }
      },
      onPickerSelectorPicked: selector => {
        void Promise.all([bridge.pageHandler.exitZapMode(), bridge.pageHandler.exitPickerMode()]).then(() => {
          const boost = refs.boostRef.current;
          if (!boost || refs.closedRef.current) {
            return;
          }
          const separator = boost.customCss === '' ? '' : '\n';
          callbacks.applyUpdate('import-selector', createBoostUpdate({
            customCss: `${boost.customCss}${separator}${selector} {\n\n}`,
          }));
          callbacks.setEditorFocusRequest(request => request + 1);
        }).catch(error => callbacks.reportError(error, 'Boost failed to add the selected element.'));
      },
      onEditorKilled: () => {
        void (async () => {
          try {
            await callbacks.onEditorKilled();
          } catch (error: unknown) {
            callbacks.reportError(error, 'Boost failed to close.');
          } finally {
            try {
              await bridge.pageHandler.hostCloseFinished();
            } catch (error: unknown) {
              callbacks.reportError(
                  error, 'Boost host close acknowledgement failed.');
            }
          }
        })();
      },
    };
    bridge.addObserverListeners(observer);

    void Promise.all([
      bridge.pageHandler.getDomain(),
      bridge.pageHandler.getSystemFonts(),
    ]).then(async ([{domain}, {fonts}]) => {
      if (!disposed && !refs.closedRef.current) {
        await callbacks.bootstrap(bridge, domain, fonts);
      }
    }).catch(error => callbacks.reportError(error, 'Boost failed to load.'));

    return () => {
      disposed = true;
      refs.bridgeRef.current = null;
      stopWatchingTheme();
    };
  }, [callbacks, refs]);
}
