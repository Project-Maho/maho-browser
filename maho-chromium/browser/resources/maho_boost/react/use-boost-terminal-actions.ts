import * as React from 'react';

import type {BoostInfo} from '../maho_boost.mojom-webui.js';
import type {BoostBridge} from './boost-bridge.js';
import {closeBoostLifecycle, deleteBoostLifecycle, type BoostOwnership} from './boost-lifecycle.js';
import type {BoostAction} from './boost-state.js';

export interface BoostTerminalActionsOptions {
  readonly boostRef: React.MutableRefObject<BoostInfo | null>;
  readonly bridgeRef: React.MutableRefObject<BoostBridge | null>;
  readonly cancelPendingUpdates: () => void;
  readonly closedRef: React.MutableRefObject<boolean>;
  readonly flushPendingUpdates: () => Promise<void>;
  readonly reduce: (action: BoostAction) => void;
  readonly reportError: (error: unknown, fallback: string) => void;
  readonly settleUpdates: () => Promise<void>;
  readonly temporaryBoostIdRef: React.MutableRefObject<string | null>;
}

export interface BoostTerminalActions {
  readonly close: () => Promise<void>;
  readonly closeFromHost: () => Promise<void>;
  readonly closeWithDialog: (closeDialog: boolean) => Promise<void>;
  readonly deleteBoost: () => Promise<boolean>;
}

export function useBoostTerminalActions({
  boostRef,
  bridgeRef,
  cancelPendingUpdates,
  closedRef,
  flushPendingUpdates,
  reduce,
  reportError,
  settleUpdates,
  temporaryBoostIdRef,
}: BoostTerminalActionsOptions): BoostTerminalActions {
  const closeRef = React.useRef<Promise<void> | null>(null);
  const deleteRef = React.useRef<Promise<boolean> | null>(null);

  const closeLifecycle = React.useCallback(async (closeDialog: boolean): Promise<void> => {
    if (deleteRef.current) {
      await deleteRef.current;
      return;
    }
    if (closeRef.current) {
      return closeRef.current;
    }
    const bridge = bridgeRef.current;
    if (!bridge) {
      return;
    }

    const lifecycle = closeBoostLifecycle({
      closeDialog,
      flushPendingUpdates,
      getBoost: () => boostRef.current,
      getOwnership: (): BoostOwnership =>
        temporaryBoostIdRef.current === boostRef.current?.id ? 'temporary' : 'persisted',
      markClosed: () => {
        closedRef.current = true;
      },
      pageHandler: bridge.pageHandler,
      settleUpdates,
    });
    closeRef.current = lifecycle;
    try {
      await lifecycle;
    } catch (error: unknown) {
      closedRef.current = false;
      closeRef.current = null;
      reportError(error, 'Boost failed to close.');
      throw error;
    }
  }, [boostRef, bridgeRef, closedRef, flushPendingUpdates, reportError, settleUpdates, temporaryBoostIdRef]);

  const deleteBoost = React.useCallback(async (): Promise<boolean> => {
    if (deleteRef.current) {
      return deleteRef.current;
    }
    if (closeRef.current) {
      return false;
    }
    const bridge = bridgeRef.current;
    if (!bridge || closedRef.current || !boostRef.current) {
      return false;
    }

    closedRef.current = true;
    const lifecycle = deleteBoostLifecycle({
      cancelPendingUpdates,
      closeDialog: true,
      getBoost: () => boostRef.current,
      pageHandler: bridge.pageHandler,
      settleUpdates,
    });
    deleteRef.current = lifecycle;
    try {
      const deleted = await lifecycle;
      if (deleted) {
        temporaryBoostIdRef.current = null;
        reduce({type: 'set-boost', boost: null, preserveDirty: false});
      } else {
        closedRef.current = false;
        deleteRef.current = null;
      }
      return deleted;
    } catch (error: unknown) {
      closedRef.current = false;
      deleteRef.current = null;
      reportError(error, 'Boost failed to delete.');
      throw error;
    }
  }, [boostRef, bridgeRef, cancelPendingUpdates, closedRef, reduce, reportError, settleUpdates, temporaryBoostIdRef]);

  const close = React.useCallback(() => closeLifecycle(true), [closeLifecycle]);
  const closeFromHost = React.useCallback(() => closeLifecycle(false), [closeLifecycle]);

  return {
    close,
    closeFromHost,
    closeWithDialog: closeLifecycle,
    deleteBoost,
  };
}
