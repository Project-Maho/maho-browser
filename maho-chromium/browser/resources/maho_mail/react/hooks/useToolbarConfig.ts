import { useCallback, useEffect, useState } from "react";
import * as api from "../api";
import {
  DEFAULT_TOOLBAR_CONFIG,
  sanitizeConfig,
  type ActionId,
  type ToolbarConfig,
} from "../types/toolbar";

const STORAGE_KEY = "maho-toolbar-config-cache";
const CHANGE_EVENT = "maho-toolbar-config-changed";
const BACKEND_KEY = "toolbar_config";

function loadFromCache(): ToolbarConfig {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return DEFAULT_TOOLBAR_CONFIG;
    return sanitizeConfig(JSON.parse(raw));
  } catch {
    return DEFAULT_TOOLBAR_CONFIG;
  }
}

function writeCache(config: ToolbarConfig): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(config));
  } catch {
    // Ignore quota / private-mode errors
  }
}

export function configsEqual(a: ToolbarConfig, b: ToolbarConfig): boolean {
  if (a.primary.length !== b.primary.length) return false;
  if (a.overflow.length !== b.overflow.length) return false;
  for (let i = 0; i < a.primary.length; i++) if (a.primary[i] !== b.primary[i]) return false;
  for (let i = 0; i < a.overflow.length; i++) if (a.overflow[i] !== b.overflow[i]) return false;
  const aKeys = Object.keys(a.shortcuts) as ActionId[];
  const bKeys = Object.keys(b.shortcuts) as ActionId[];
  if (aKeys.length !== bKeys.length) return false;
  for (const id of aKeys) {
    if (!Object.prototype.hasOwnProperty.call(b.shortcuts, id)) return false;
    if (a.shortcuts[id] !== b.shortcuts[id]) return false;
  }
  return true;
}

export interface UseToolbarConfigReturn {
  config: ToolbarConfig;
  setPrimary: (next: ActionId[]) => void;
  setOverflow: (next: ActionId[]) => void;
  setShortcut: (id: ActionId, shortcut: string | null) => void;
  moveToPrimary: (id: ActionId) => void;
  moveToOverflow: (id: ActionId) => void;
  hideAction: (id: ActionId) => void;
  resetToDefaults: () => void;
  replaceConfig: (next: ToolbarConfig) => void;
}

export function useToolbarConfig(): UseToolbarConfigReturn {
  const [config, setConfig] = useState<ToolbarConfig>(loadFromCache);

  useEffect(() => {
    let cancelled = false;
    void api
      .getAppSetting(BACKEND_KEY)
      .then((value) => {
        if (cancelled || !value) return;
        try {
          const remote = sanitizeConfig(JSON.parse(value));
          setConfig((current) => (configsEqual(current, remote) ? current : remote));
          writeCache(remote);
        } catch {
          // Corrupt remote value — keep local
        }
      })
      .catch(() => {
        // Backend not available (e.g. tests) — silently fall back to cache
      });
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    const handler = (event: Event) => {
      if (event instanceof StorageEvent && event.key !== STORAGE_KEY) return;
      setConfig((current) => {
        const next = loadFromCache();
        return configsEqual(current, next) ? current : next;
      });
    };
    window.addEventListener(CHANGE_EVENT, handler);
    window.addEventListener("storage", handler);
    return () => {
      window.removeEventListener(CHANGE_EVENT, handler);
      window.removeEventListener("storage", handler);
    };
  }, []);

  const persist = useCallback((next: ToolbarConfig) => {
    setConfig(next);
    writeCache(next);
    window.dispatchEvent(new Event(CHANGE_EVENT));
    void api.setAppSetting(BACKEND_KEY, JSON.stringify(next)).catch(() => {
      // Backend write failed — local cache is still authoritative for this window
    });
  }, []);

  const setPrimary = useCallback(
    (next: ActionId[]) => {
      persist({ ...config, primary: next });
    },
    [config, persist],
  );

  const setOverflow = useCallback(
    (next: ActionId[]) => {
      persist({ ...config, overflow: next });
    },
    [config, persist],
  );

  const setShortcut = useCallback(
    (id: ActionId, shortcut: string | null) => {
      persist({ ...config, shortcuts: { ...config.shortcuts, [id]: shortcut } });
    },
    [config, persist],
  );

  const moveToPrimary = useCallback(
    (id: ActionId) => {
      if (config.primary.includes(id)) return;
      persist({
        ...config,
        primary: [...config.primary, id],
        overflow: config.overflow.filter((x) => x !== id),
      });
    },
    [config, persist],
  );

  const moveToOverflow = useCallback(
    (id: ActionId) => {
      if (config.overflow.includes(id)) return;
      persist({
        ...config,
        primary: config.primary.filter((x) => x !== id),
        overflow: [...config.overflow, id],
      });
    },
    [config, persist],
  );

  const hideAction = useCallback(
    (id: ActionId) => {
      persist({
        ...config,
        primary: config.primary.filter((x) => x !== id),
        overflow: config.overflow.filter((x) => x !== id),
      });
    },
    [config, persist],
  );

  const resetToDefaults = useCallback(() => {
    persist(DEFAULT_TOOLBAR_CONFIG);
  }, [persist]);

  const replaceConfig = useCallback(
    (next: ToolbarConfig) => {
      persist(sanitizeConfig(next));
    },
    [persist],
  );

  return {
    config,
    setPrimary,
    setOverflow,
    setShortcut,
    moveToPrimary,
    moveToOverflow,
    hideAction,
    resetToDefaults,
    replaceConfig,
  };
}
