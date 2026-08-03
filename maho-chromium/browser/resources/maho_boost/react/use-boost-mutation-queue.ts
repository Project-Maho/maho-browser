import * as React from 'react';

import type {BoostInfo, BoostUpdate} from '../maho_boost.mojom-webui.js';
import type {BoostBridge} from './boost-bridge.js';

export interface BoostMutationQueueOptions {
  readonly boostRef: React.MutableRefObject<BoostInfo | null>;
  readonly bridgeRef: React.MutableRefObject<BoostBridge | null>;
  readonly closedRef: React.MutableRefObject<boolean>;
  readonly onError: (error: unknown, fallback: string) => void;
  readonly onServerBoost: (boost: BoostInfo) => void;
  readonly revisionRef: React.MutableRefObject<number>;
}

export interface BoostMutationQueue {
  readonly enqueueUpdate: (update: BoostUpdate) => Promise<void>;
  readonly invalidatePendingUpdates: () => void;
  readonly registerUpdate: (update: BoostUpdate) => void;
  readonly settleUpdates: () => Promise<void>;
}

export function useBoostMutationQueue({
  boostRef,
  bridgeRef,
  closedRef,
  onError,
  onServerBoost,
  revisionRef,
}: BoostMutationQueueOptions): BoostMutationQueue {
  const mutationQueueRef = React.useRef<Promise<void>>(Promise.resolve());
  const generationRef = React.useRef(0);
  const updateBoostIdsRef = React.useRef(new WeakMap<BoostUpdate, string>());
  const updateGenerationsRef = React.useRef(new WeakMap<BoostUpdate, number>());
  const updateRevisionsRef = React.useRef(new WeakMap<BoostUpdate, number>());

  const invalidatePendingUpdates = React.useCallback((): void => {
    generationRef.current += 1;
  }, []);

  const registerUpdate = React.useCallback((update: BoostUpdate): void => {
    const boost = boostRef.current;
    if (!boost) {
      return;
    }
    updateBoostIdsRef.current.set(update, boost.id);
    updateGenerationsRef.current.set(update, generationRef.current);
    updateRevisionsRef.current.set(update, revisionRef.current);
  }, [boostRef, revisionRef]);

  const enqueueUpdate = React.useCallback(async (update: BoostUpdate): Promise<void> => {
    const generation = updateGenerationsRef.current.get(update);
    const revision = updateRevisionsRef.current.get(update);
    const boostId = updateBoostIdsRef.current.get(update);
    const bridge = bridgeRef.current;
    if (generation !== generationRef.current || revision === undefined ||
        boostId === undefined || boostRef.current?.id !== boostId || !bridge) {
      return;
    }

    const mutation = mutationQueueRef.current.then(async () => {
      if (closedRef.current ||
          generation !== generationRef.current ||
          boostRef.current?.id !== boostId ||
          bridgeRef.current !== bridge) {
        return;
      }
      const {boost: updatedBoost} = await bridge.pageHandler.updateBoost(boostId, update);
      if (updatedBoost &&
          !closedRef.current &&
          generation === generationRef.current &&
          revision === revisionRef.current &&
          boostRef.current?.id === boostId &&
          bridgeRef.current === bridge) {
        onServerBoost(updatedBoost);
      }
    });
    mutationQueueRef.current = mutation.catch(() => undefined);
    try {
      await mutation;
    } catch (error: unknown) {
      onError(error, 'Boost failed to update.');
      throw error;
    }
  }, [boostRef, bridgeRef, closedRef, onError, onServerBoost, revisionRef]);

  const settleUpdates = React.useCallback(async (): Promise<void> => {
    await mutationQueueRef.current;
  }, []);

  return {enqueueUpdate, invalidatePendingUpdates, registerUpdate, settleUpdates};
}
