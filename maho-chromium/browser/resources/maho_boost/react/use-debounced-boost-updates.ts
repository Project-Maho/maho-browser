// Copyright 2026 Maho Browser. All rights reserved.

import {useCallback, useRef} from 'react';

import type {BoostUpdate} from '../maho_boost.mojom-webui.js';

export const GENERAL_UPDATE_DEBOUNCE_MS = 150;
export const SLIDER_UPDATE_DEBOUNCE_MS = 100;

export type BoostUpdateKey =
  | 'color-picker-primary'
  | 'color-picker-secondary'
  | 'magic-theme'
  | 'invert'
  | 'disable'
  | 'contrast'
  | 'brightness'
  | 'saturation'
  | 'case'
  | 'size'
  | 'font'
  | 'name'
  | 'custom-css'
  | 'import-selector';

export type DebouncedBoostUpdate = () => BoostUpdate;

export interface DebouncedBoostUpdates {
  cancel: (key: BoostUpdateKey) => void;
  cancelAll: () => void;
  cancelUnsent: () => void;
  flush: (key: BoostUpdateKey) => Promise<void>;
  flushAll: () => Promise<void>;
  scheduleGeneral: (key: BoostUpdateKey, createUpdate: DebouncedBoostUpdate) => void;
  scheduleSlider: (key: BoostUpdateKey, createUpdate: DebouncedBoostUpdate) => void;
}

export function useDebouncedBoostUpdates(
  onUpdate: (update: BoostUpdate) => void | Promise<void>,
): DebouncedBoostUpdates {
  const timers = useRef(new Map<BoostUpdateKey, number>());
  const pendingUpdates = useRef(new Map<BoostUpdateKey, DebouncedBoostUpdate>());
  const inFlightUpdates = useRef(new Map<BoostUpdateKey, Promise<void>>());
  const onUpdateRef = useRef(onUpdate);
  onUpdateRef.current = onUpdate;

  const cancel = useCallback((key: BoostUpdateKey): void => {
    const timer = timers.current.get(key);
    if (timer !== undefined) {
      window.clearTimeout(timer);
      timers.current.delete(key);
    }
    pendingUpdates.current.delete(key);
  }, []);

  const cancelAll = useCallback((): void => {
    for (const timer of timers.current.values()) {
      window.clearTimeout(timer);
    }
    timers.current.clear();
    pendingUpdates.current.clear();
  }, []);

  const flush = useCallback(async (key: BoostUpdateKey): Promise<void> => {
    const timer = timers.current.get(key);
    if (timer !== undefined) {
      window.clearTimeout(timer);
      timers.current.delete(key);
    }
    const createUpdate = pendingUpdates.current.get(key);
    if (!createUpdate) {
      await inFlightUpdates.current.get(key);
      return;
    }
    pendingUpdates.current.delete(key);
    const update = Promise.resolve(onUpdateRef.current(createUpdate()));
    inFlightUpdates.current.set(key, update);
    try {
      await update;
    } finally {
      if (inFlightUpdates.current.get(key) === update) {
        inFlightUpdates.current.delete(key);
      }
    }
  }, []);

  const flushAll = useCallback(async (): Promise<void> => {
    while (pendingUpdates.current.size > 0 || inFlightUpdates.current.size > 0) {
      const keys = new Set([...pendingUpdates.current.keys(), ...inFlightUpdates.current.keys()]);
      await Promise.all([...keys].map(key => flush(key)));
    }
  }, [flush]);

  const schedule = useCallback((
    key: BoostUpdateKey,
    delayMs: number,
    createUpdate: DebouncedBoostUpdate,
  ): void => {
    const existing = timers.current.get(key);
    if (existing !== undefined) {
      window.clearTimeout(existing);
    }
    pendingUpdates.current.set(key, createUpdate);

    const timer = window.setTimeout(() => {
      void flush(key).catch(() => undefined);
    }, delayMs);
    timers.current.set(key, timer);
  }, [flush]);

  const scheduleGeneral = useCallback((key: BoostUpdateKey, createUpdate: DebouncedBoostUpdate): void => {
    schedule(key, GENERAL_UPDATE_DEBOUNCE_MS, createUpdate);
  }, [schedule]);

  const scheduleSlider = useCallback((key: BoostUpdateKey, createUpdate: DebouncedBoostUpdate): void => {
    schedule(key, SLIDER_UPDATE_DEBOUNCE_MS, createUpdate);
  }, [schedule]);

  return {
    cancel,
    cancelAll,
    cancelUnsent: cancelAll,
    flush,
    flushAll,
    scheduleGeneral,
    scheduleSlider,
  };
}
