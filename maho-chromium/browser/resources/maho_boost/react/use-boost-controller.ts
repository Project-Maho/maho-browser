import * as React from 'react';

import {WindowMode} from '../maho_boost.mojom-webui.js';
import type {
  BoostInfo,
  BoostUpdate,
  PageHandlerRemote,
} from '../maho_boost.mojom-webui.js';
import {BoostBridge} from './boost-bridge.js';
import {
  boostReducer,
  createBoostUpdate,
  initialBoostState,
  sortBoostInfos,
  type BoostAction,
  type BoostState,
} from './boost-state.js';
import {type BoostUpdateKey, useDebouncedBoostUpdates} from './use-debounced-boost-updates.js';
import {useBoostBridge} from './use-boost-bridge.js';
import {useCloseLifecycle} from './use-close-lifecycle.js';
import {useBoostMutationQueue} from './use-boost-mutation-queue.js';
import {useBoostTerminalActions} from './use-boost-terminal-actions.js';

type UpdateDelay = 'general' | 'slider';

interface DomainSnapshot {
  readonly activeBoostId: string | null;
  readonly boost: BoostInfo | null;
  readonly boosts: BoostInfo[];
  readonly selectedBoostId: string | null;
}

type RecordTransition = <Result>(
  operation: (bridge: BoostBridge) => Promise<Result>,
  discardPending?: boolean,
) => Promise<Result>;

export interface BoostController {
  readonly applyUpdate: (key: BoostUpdateKey, update: BoostUpdate, delay?: UpdateDelay) => void;
  readonly close: () => Promise<void>;
  readonly deleteBoost: () => Promise<boolean>;
  readonly editorFocusRequest: number;
  readonly importBoost: (json: string) => Promise<BoostInfo>;
  readonly openInspector: () => Promise<void>;
  readonly pageHandler: PageHandlerRemote | null;
  readonly renameBoost: (name: string) => Promise<void>;
  readonly resetBoost: () => Promise<void>;
  readonly selectBoost: (boostId: string) => Promise<void>;
  readonly setMode: (mode: WindowMode) => Promise<void>;
  readonly setSiteBoostEnabled: (enabled: boolean) => Promise<void>;
  readonly shuffleBoost: () => Promise<void>;
  readonly state: BoostState;
  readonly togglePicker: () => Promise<void>;
  readonly toggleZap: () => Promise<void>;
}

class BoostControllerError extends Error {
  readonly name = 'BoostControllerError';
}

function errorMessage(error: unknown, fallback: string): string {
  return error instanceof Error ? error.message : fallback;
}

function createSnapshot(
    boosts: readonly BoostInfo[],
    activeBoost: BoostInfo | null,
    preferredSelectedBoostId: string | null,
    preferActive: boolean): DomainSnapshot {
  const allBoosts = activeBoost && !boosts.some(boost => boost.id === activeBoost.id) ?
    [...boosts, activeBoost] : [...boosts];
  const preferredBoost = preferredSelectedBoostId === null ? null :
    allBoosts.find(boost => boost.id === preferredSelectedBoostId) ?? null;
  const selectedBoost = preferActive && activeBoost ? activeBoost :
    preferredBoost ?? sortBoostInfos(allBoosts, null)[0] ?? null;
  return {
    activeBoostId: activeBoost?.id ?? null,
    boost: selectedBoost,
    boosts: sortBoostInfos(allBoosts, selectedBoost?.id ?? null),
    selectedBoostId: selectedBoost?.id ?? null,
  };
}

export function useBoostController(): BoostController {
  const [state, dispatch] = React.useReducer(boostReducer, initialBoostState);
  const [editorFocusRequest, setEditorFocusRequest] = React.useState(0);
  const stateRef = React.useRef<BoostState>(initialBoostState);
  const bridgeRef = React.useRef<BoostBridge | null>(null);
  const boostRef = React.useRef<BoostInfo | null>(null);
  const closedRef = React.useRef(false);
  const modeTransitionRef = React.useRef<Promise<void> | null>(null);
  const recordTransitionRef = React.useRef<Promise<void> | null>(null);
  const revisionRef = React.useRef(0);
  const loadGenerationRef = React.useRef(0);
  const temporaryBoostIdRef = React.useRef<string | null>(null);
  const lastHostCloseStateRef = React.useRef('');

  const reduce = React.useCallback((action: BoostAction): void => {
    const nextState = boostReducer(stateRef.current, action);
    stateRef.current = nextState;
    boostRef.current = nextState.boost;
    const nextBoost = nextState.boost;
    const hostCloseState = nextBoost ?
      `${nextBoost.id}:${nextBoost.changeWasMade ? '1' : '0'}` : ':0';
    if (lastHostCloseStateRef.current !== hostCloseState) {
      lastHostCloseStateRef.current = hostCloseState;
      void bridgeRef.current?.pageHandler.setHostCloseState(
          nextBoost?.id ?? null, nextBoost?.changeWasMade ?? false).catch(() => {});
    }
    dispatch(action);
  }, []);

  const reportError = React.useCallback((error: unknown, fallback: string): void => {
    if (!closedRef.current) {
      reduce({type: 'set-error', error: errorMessage(error, fallback)});
    }
  }, [reduce]);

  const acceptServerBoost = React.useCallback((boost: BoostInfo): void => {
    if (stateRef.current.selectedBoostId === boost.id) {
      reduce({type: 'set-boost', boost, preserveDirty: true});
    }
  }, [reduce]);
  const {
    enqueueUpdate,
    invalidatePendingUpdates,
    registerUpdate,
    settleUpdates,
  } = useBoostMutationQueue({
    boostRef,
    bridgeRef,
    closedRef,
    onError: reportError,
    onServerBoost: acceptServerBoost,
    revisionRef,
  });
  const {
    cancelAll,
    cancelUnsent,
    flushAll,
    scheduleGeneral,
    scheduleSlider,
  } = useDebouncedBoostUpdates(enqueueUpdate);

  const applyUpdate = React.useCallback((
      key: BoostUpdateKey,
      update: BoostUpdate,
      delay: UpdateDelay = 'general',
  ): void => {
    if (closedRef.current || modeTransitionRef.current || recordTransitionRef.current) {
      return;
    }
    revisionRef.current += 1;
    registerUpdate(update);
    reduce({type: 'apply-update', update});
    const schedule = delay === 'slider' ? scheduleSlider : scheduleGeneral;
    schedule(key, () => update);
  }, [reduce, registerUpdate, scheduleGeneral, scheduleSlider]);

  const loadSnapshot = React.useCallback(async (
      bridge: BoostBridge,
      preferredSelectedBoostId: string | null,
      preferActive: boolean): Promise<DomainSnapshot> => {
    const [{boost: activeBoost}, {boosts}] = await Promise.all([
      bridge.pageHandler.getActiveBoost(),
      bridge.pageHandler.listBoosts(),
    ]);
    return createSnapshot(boosts, activeBoost, preferredSelectedBoostId, preferActive);
  }, []);

  const applySnapshot = React.useCallback((snapshot: DomainSnapshot): void => {
    reduce({type: 'replace-domain-state', ...snapshot});
  }, [reduce]);

  const runRecordTransition: RecordTransition = React.useCallback(async <Result>(
      operation: (bridge: BoostBridge) => Promise<Result>,
      discardPending = false): Promise<Result> => {
    while (recordTransitionRef.current) {
      await recordTransitionRef.current;
    }
    const bridge = bridgeRef.current;
    if (!bridge || closedRef.current || modeTransitionRef.current) {
      throw new BoostControllerError('Boost actions are unavailable.');
    }
    loadGenerationRef.current += 1;
    const transition = Promise.resolve().then(async () => {
      if (discardPending) {
        cancelUnsent();
        revisionRef.current += 1;
        invalidatePendingUpdates();
      } else {
        await flushAll();
      }
      await settleUpdates();
      if (closedRef.current || bridgeRef.current !== bridge) {
        throw new BoostControllerError('Boost actions are unavailable.');
      }
      return operation(bridge);
    });
    const marker = transition.then(() => undefined, () => undefined);
    recordTransitionRef.current = marker;
    try {
      return await transition;
    } finally {
      if (recordTransitionRef.current === marker) {
        recordTransitionRef.current = null;
      }
    }
  }, [cancelUnsent, flushAll, invalidatePendingUpdates, settleUpdates]);

  const {close, closeFromHost, closeWithDialog} = useBoostTerminalActions({
    boostRef,
    bridgeRef,
    cancelPendingUpdates: cancelAll,
    closedRef,
    flushPendingUpdates: flushAll,
    reduce,
    reportError,
    settleUpdates,
    temporaryBoostIdRef,
  });

  const bootstrap = React.useCallback(async (
      bridge: BoostBridge, domain: string, fonts: string[]): Promise<void> => {
    const generation = ++loadGenerationRef.current;
    let snapshot = await loadSnapshot(bridge, null, true);
    if (snapshot.boost === null) {
      const {boost: temporaryBoost} = await bridge.pageHandler.createTempBoost();
      if (!temporaryBoost) {
        throw new BoostControllerError('Unable to load or create a Boost.');
      }
      temporaryBoostIdRef.current = temporaryBoost.id;
      snapshot = {
        activeBoostId: temporaryBoost.id,
        boost: temporaryBoost,
        boosts: sortBoostInfos([temporaryBoost], temporaryBoost.id),
        selectedBoostId: temporaryBoost.id,
      };
    } else {
      temporaryBoostIdRef.current = null;
    }
    if (closedRef.current || bridgeRef.current !== bridge ||
        generation !== loadGenerationRef.current) {
      return;
    }
    reduce({
      type: 'bootstrap',
      ...snapshot,
      domain,
      systemFonts: fonts,
    });
  }, [loadSnapshot, reduce]);

  const refreshBoostList = React.useCallback(async (bridge: BoostBridge): Promise<void> => {
    if (stateRef.current.isLoading || recordTransitionRef.current) {
      return;
    }
    const generation = loadGenerationRef.current;
    const preferredSelectedBoostId = stateRef.current.selectedBoostId;
    const currentBoost = boostRef.current;
    const snapshot = await loadSnapshot(bridge, preferredSelectedBoostId, false);
    if (closedRef.current || bridgeRef.current !== bridge || recordTransitionRef.current ||
        generation !== loadGenerationRef.current) {
      return;
    }
    const selectedStillExists = currentBoost &&
      snapshot.boosts.some(boost => boost.id === currentBoost.id);
    applySnapshot(selectedStillExists ? {...snapshot, boost: currentBoost} : snapshot);
  }, [applySnapshot, loadSnapshot]);

  const refreshSelectedBoost = React.useCallback(async (bridge: BoostBridge): Promise<void> => {
    const selectedBoostId = stateRef.current.selectedBoostId;
    if (!selectedBoostId || recordTransitionRef.current) {
      return;
    }
    const generation = loadGenerationRef.current;
    const revision = revisionRef.current;
    const [{boost}, snapshot] = await Promise.all([
      bridge.pageHandler.getBoost(selectedBoostId),
      loadSnapshot(bridge, selectedBoostId, false),
    ]);
    if (closedRef.current || bridgeRef.current !== bridge || recordTransitionRef.current ||
        generation !== loadGenerationRef.current || revision !== revisionRef.current) {
      return;
    }
    applySnapshot(boost ? {...snapshot, boost, selectedBoostId: boost.id} : snapshot);
  }, [applySnapshot, loadSnapshot]);

  const bridgeRefs = React.useMemo(() => ({boostRef, bridgeRef, closedRef}), []);
  const bridgeCallbacks = React.useMemo(() => ({
    acceptActiveBoostId: (boostId: string | null): void => {
      reduce({type: 'set-active-boost-id', boostId});
    },
    applyUpdate,
    bootstrap,
    onEditorKilled: closeFromHost,
    reduce,
    refreshBoostList,
    refreshSelectedBoost,
    reportError,
    setEditorFocusRequest,
  }), [applyUpdate, bootstrap, closeFromHost, reduce, refreshBoostList, refreshSelectedBoost, reportError]);
  useBoostBridge(bridgeRefs, bridgeCallbacks);
  useCloseLifecycle({close: closeWithDialog, closedRef, mode: state.mode});

  React.useEffect(() => {
    document.documentElement.setAttribute(
        'editor', state.mode === WindowMode.kCode ? 'code' : 'boost');
  }, [state.mode]);

  const selectBoost = React.useCallback(async (boostId: string): Promise<void> => {
    await runRecordTransition(async bridge => {
      // The page renders whichever Boost is active for the domain, so editing a
      // Boost that is merely selected produced edits — and resets — that never
      // reached the page. Move activation with the selection while Site Boosts
      // is on; an explicit Off stays off.
      const wasEnabled = stateRef.current.activeBoostId !== null;
      if (wasEnabled) {
        await bridge.pageHandler.setActiveBoost(boostId);
      }
      const snapshot = await loadSnapshot(bridge, boostId, false);
      if (snapshot.selectedBoostId !== boostId) {
        throw new BoostControllerError('The selected Boost no longer exists.');
      }
      temporaryBoostIdRef.current = temporaryBoostIdRef.current === boostId ? boostId : null;
      applySnapshot(snapshot);
    });
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const setSiteBoostEnabled = React.useCallback(async (enabled: boolean): Promise<void> => {
    await runRecordTransition(async bridge => {
      const selectedBoostId = stateRef.current.selectedBoostId;
      if (enabled && selectedBoostId === null) {
        throw new BoostControllerError('Select a Boost before turning it on.');
      }
      await bridge.pageHandler.setActiveBoost(enabled ? selectedBoostId : null);
      applySnapshot(await loadSnapshot(bridge, selectedBoostId, false));
    });
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const resetBoost = React.useCallback(async (): Promise<void> => {
    await runRecordTransition(async bridge => {
      const selectedBoostId = stateRef.current.selectedBoostId;
      if (selectedBoostId === null) {
        throw new BoostControllerError('There is no Boost to reset.');
      }
      const {boost} = await bridge.pageHandler.resetBoost(selectedBoostId);
      if (!boost) {
        throw new BoostControllerError('Boost could not be reset.');
      }
      applySnapshot(await loadSnapshot(bridge, selectedBoostId, false));
    }, true);
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const renameBoost = React.useCallback(async (name: string): Promise<void> => {
    await runRecordTransition(async bridge => {
      const selectedBoostId = stateRef.current.selectedBoostId;
      if (selectedBoostId === null) {
        throw new BoostControllerError('There is no Boost to rename.');
      }
      const {boost} = await bridge.pageHandler.updateBoost(
          selectedBoostId, createBoostUpdate({name}));
      if (!boost) {
        throw new BoostControllerError('Boost could not be renamed.');
      }
      applySnapshot(await loadSnapshot(bridge, selectedBoostId, false));
    });
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const shuffleBoost = React.useCallback(async (): Promise<void> => {
    await runRecordTransition(async bridge => {
      const selectedBoostId = stateRef.current.selectedBoostId;
      if (selectedBoostId === null) {
        throw new BoostControllerError('There is no Boost to shuffle.');
      }
      const {boost} = await bridge.pageHandler.shuffleBoost(selectedBoostId);
      if (!boost) {
        throw new BoostControllerError('Boost could not be shuffled.');
      }
      applySnapshot(await loadSnapshot(bridge, selectedBoostId, false));
    });
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const importBoost = React.useCallback(async (json: string): Promise<BoostInfo> => {
    return runRecordTransition(async bridge => {
      const {boost} = await bridge.pageHandler.importBoost(json);
      if (!boost) {
        throw new BoostControllerError('The selected file did not contain a valid Boost.');
      }
      applySnapshot(await loadSnapshot(bridge, boost.id, false));
      temporaryBoostIdRef.current = null;
      return boost;
    });
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const deleteBoost = React.useCallback(async (): Promise<boolean> => {
    return runRecordTransition(async bridge => {
      const selectedBoostId = stateRef.current.selectedBoostId;
      if (selectedBoostId === null) {
        return false;
      }
      const {success} = await bridge.pageHandler.deleteBoost(selectedBoostId);
      if (!success) {
        return false;
      }
      temporaryBoostIdRef.current = null;
      const snapshot = await loadSnapshot(bridge, null, false);
      applySnapshot(snapshot);
      if (snapshot.boost === null) {
        closedRef.current = true;
        await bridge.pageHandler.closeDialog();
      }
      return true;
    }, true);
  }, [applySnapshot, loadSnapshot, runRecordTransition]);

  const setMode = React.useCallback(async (mode: WindowMode): Promise<void> => {
    if (closedRef.current || modeTransitionRef.current || recordTransitionRef.current ||
        stateRef.current.mode === mode) {
      return;
    }
    const bridge = bridgeRef.current;
    if (!bridge) {
      return;
    }
    const transition = (async (): Promise<void> => {
      await flushAll();
      await settleUpdates();
      if (mode === WindowMode.kBoost) {
        await Promise.all([bridge.pageHandler.exitZapMode(), bridge.pageHandler.exitPickerMode()]);
      }
      await bridge.pageHandler.requestModeResize(mode);
      reduce({type: 'set-mode', mode});
      if (mode === WindowMode.kCode) {
        setEditorFocusRequest(request => request + 1);
      } else {
        window.requestAnimationFrame(() => document.getElementById('zen-boost-code')?.focus());
      }
    })();
    modeTransitionRef.current = transition;
    try {
      await transition;
    } catch (error: unknown) {
      reportError(error, 'Boost failed to change modes.');
      throw error;
    } finally {
      modeTransitionRef.current = null;
    }
  }, [flushAll, reduce, reportError, settleUpdates]);

  const toggleMode = React.useCallback(async (kind: 'picker' | 'zap'): Promise<void> => {
    const bridge = bridgeRef.current;
    const boost = boostRef.current;
    if (!bridge || !boost || closedRef.current || modeTransitionRef.current ||
        recordTransitionRef.current) {
      return;
    }
    if (kind === 'picker') {
      await (stateRef.current.pickerModeEnabled ?
        bridge.pageHandler.exitPickerMode() : bridge.pageHandler.enterPickerMode(boost.id));
      return;
    }
    await (stateRef.current.zapMode.isOn ?
      bridge.pageHandler.exitZapMode() : bridge.pageHandler.enterZapMode(boost.id));
  }, []);

  return {
    applyUpdate,
    close,
    deleteBoost,
    editorFocusRequest,
    importBoost,
    openInspector: async () => {
      await bridgeRef.current?.pageHandler.openInspector();
    },
    pageHandler: bridgeRef.current?.pageHandler ?? null,
    renameBoost,
    resetBoost,
    selectBoost,
    setMode,
    setSiteBoostEnabled,
    shuffleBoost,
    state,
    togglePicker: () => toggleMode('picker'),
    toggleZap: () => toggleMode('zap'),
  };
}
